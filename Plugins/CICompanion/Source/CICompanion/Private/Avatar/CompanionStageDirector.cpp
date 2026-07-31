#include "Avatar/CompanionStageDirector.h"
#include "CICompanionEnv.h"
#include "CICompanionLog.h"

#include "Avatar/CompanionFaceSubsystem.h"
#include "Avatar/CompanionRuntimeSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"



namespace
{
	// Shared by both opt-in switches. Only an affirmative value counts: unset, empty, "0" and
	// anything unrecognised all mean "no", so the safe/static behaviour is what you get by default
	// and by typo.
	bool StageOptedIn(const FString& RawValue)
	{
		const FString V = RawValue.TrimStartAndEnd();
		return V == TEXT("1")
			|| V.Equals(TEXT("true"), ESearchCase::IgnoreCase)
			|| V.Equals(TEXT("yes"), ESearchCase::IgnoreCase)
			|| V.Equals(TEXT("on"), ESearchCase::IgnoreCase);
	}

	// Smoothstep. Linear interpolation between keys makes the camera start and stop abruptly at
	// every waypoint, which reads as a machine executing moves rather than as a shot.
	// Named uniquely because Unreal's unity build merges anonymous namespaces across the .cpp
	// files it concatenates, so a bare "Ease" here can collide with one in a neighbouring file.
	float StageEase(float T)
	{
		T = FMath::Clamp(T, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}
}

bool UCompanionStageDirector::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

bool UCompanionStageDirector::ShouldPlayOpeningShot(TFunctionRef<FString(const TCHAR*)> Lookup)
{
	return StageOptedIn(Lookup(TEXT("MHLLM_OPENING_SHOT")));
}

bool UCompanionStageDirector::ShouldAllowFreeLook(TFunctionRef<FString(const TCHAR*)> Lookup)
{
	return StageOptedIn(Lookup(TEXT("MHLLM_FREE_LOOK")));
}

bool UCompanionStageDirector::ShouldPlayOpeningShot()
{
	auto Env = [](const TCHAR* Name) { return FPlatformMisc::GetEnvironmentVariable(Name); };
	return ShouldPlayOpeningShot(Env);
}

bool UCompanionStageDirector::ShouldAllowFreeLook()
{
	auto Env = [](const TCHAR* Name) { return FPlatformMisc::GetEnvironmentVariable(Name); };
	return ShouldAllowFreeLook(Env);
}

void UCompanionStageDirector::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	bOpeningShot = ShouldPlayOpeningShot();
	bFreeLook = ShouldAllowFreeLook();

	// The resident is located the same way the face driver locates it -- by tag, because actor
	// labels are editor-only and do not survive a cook. Needed in BOTH modes now: the static shot
	// is framed on her, so it cannot be built without knowing where she is.
	for (TActorIterator<AActor> It(&InWorld); It; ++It)
	{
		if (It->ActorHasTag(UCompanionFaceSubsystem::ResidentTag))
		{
			Resident = *It;
			break;
		}
	}
	if (!Resident)
	{
		// No subject means no framing to solve, so whatever the map auto-activates is the view.
		// Still lock the pawn's input: a map with no resident is exactly the case where the view
		// falls back to the DefaultPawn, which is the thing that drifts.
		UE_LOG(LogCompanionStage, Warning, TEXT("No tagged resident; stage camera not built."));
		Beat = EStageBeat::Live;
		LockPlayerView();
		return;
	}

	ResidentFinalYaw = Resident->GetActorRotation().Yaw;
	ResidentStartYaw = ResidentFinalYaw;
	if (bOpeningShot)
	{
		// She begins with her back to where the camera will settle, and turns through 180 to meet
		// it. Only in the opening shot: turning her round and then NOT playing the move would leave
		// a static shot of the back of her head.
		ResidentStartYaw = ResidentFinalYaw + 180.0f;
		Resident->SetActorRotation(FRotator(0.0f, ResidentStartYaw, 0.0f));
	}

	float EyeZ = 180.0f;
	if (const USkeletalMeshComponent* Mesh = Resident->FindComponentByClass<USkeletalMeshComponent>())
	{
		// Measure rather than assume: a different character changes the framing, and a hard-coded
		// eyeline is what cropped her head off the first time portrait was tried.
		const FVector Extent = Mesh->Bounds.BoxExtent;
		EyeZ = Extent.Z * 2.0f * 0.93f;
	}

	BuildPath(Resident->GetActorLocation(), EyeZ);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector SpawnAt = bOpeningShot ? Path[0].Position : Path.Last().Position;
	Camera = InWorld.SpawnActor<ACameraActor>(ACameraActor::StaticClass(), SpawnAt, FRotator::ZeroRotator, Params);
	if (!Camera)
	{
		UE_LOG(LogCompanionStage, Error, TEXT("Failed to spawn stage camera; falling back to the map's view."));
		Beat = EStageBeat::Live;
		LockPlayerView();
		return;
	}
	if (UCameraComponent* Cam = Camera->GetCameraComponent())
	{
		Cam->SetFieldOfView(21.0f);
		// Portrait needs BOTH the resolution and this: a CameraComponent constrains its aspect
		// ratio by default, which letterboxes a 16:9 image inside the 9:16 canvas.
		Cam->SetConstraintAspectRatio(false);
	}

	StartDelay  = FCString::Atof(*CICompanionEnv::Get(TEXT("MHLLM_OPENING_DELAY"), TEXT("3.0")));

	if (!bOpeningShot)
	{
		// DEFAULT PATH. The camera is planted on the solved call framing and never touched again.
		//
		// It still owns the view target rather than deferring to the map's auto-activating
		// StageCamera, because that camera only exists in the authored diorama: a map without it
		// (the synthesized fallback stage, or any test map) falls through to the DefaultPawn and
		// the shot becomes whatever the pawn happens to be looking at.
		ApplyFinalFraming();
		// Zeroed so the Speak beat below is timed purely by StartDelay -- there is no move and no
		// turn to wait out.
		FlyDuration = 0.0f;
		TurnDuration = 0.0f;
		ReplayEvery = 0.0f;
		// The scripted opening line is not part of the camera move; it is the thing that proves the
		// build is alive. It still plays, just without a flythrough in front of it.
		Beat = EStageBeat::Speak;
		bActive = true;
		LockPlayerView();

		UE_LOG(LogCompanionStage, Log,
			TEXT("Static shot: camera pinned to the call framing (free-look %s). Set MHLLM_OPENING_SHOT=1 for the opening move."),
			bFreeLook ? TEXT("ON") : TEXT("locked"));
		return;
	}

	FlyDuration = FCString::Atof(*CICompanionEnv::Get(TEXT("MHLLM_FLY_SECONDS"), TEXT("11.0")));
	ReplayEvery = FCString::Atof(*CICompanionEnv::Get(TEXT("MHLLM_OPENING_REPLAY"), TEXT("0")));
	bActive = true;
	Beat = EStageBeat::Flythrough;
	ApplyCameraAt(0.0f);
	LockPlayerView();

	UE_LOG(LogCompanionStage, Log, TEXT("Opening shot: %.1fs flythrough, then turn %.0f -> %.0f."),
		FlyDuration, ResidentStartYaw, ResidentFinalYaw);
}

void UCompanionStageDirector::LockPlayerView()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	// Free look additionally needs the PAWN, which arrives after the controller does. Waiting for
	// both here is what makes the handover below reliable; without it the first attempt finds a
	// controller with no pawn, falls through to the fixed camera, and never comes back.
	if (!PC || (bFreeLook && !PC->GetPawn()))
	{
		// A world subsystem's BeginPlay is not guaranteed to run after the local player exists, and
		// a missed view target here is the difference between the solved shot and a pawn's-eye view
		// of the floor. Retry briefly rather than assume.
		if (ViewLockAttempts++ < MaxViewLockAttempts)
		{
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle,
				FTimerDelegate::CreateUObject(this, &UCompanionStageDirector::LockPlayerView), 0.25f, false);
		}
		else
		{
			UE_LOG(LogCompanionStage, Warning, TEXT("No player controller; camera and look input left as the map set them."));
		}
		return;
	}

	if (bFreeLook)
	{
		// FREE LOOK HAS TO MOVE THE VIEW TARGET, not just stop ignoring input. Un-ignoring input
		// while the view stays pointed at a camera actor that never moves is why this switch read
		// as inert: the input was live the whole time and the pawn really was walking around the
		// set, but every streamed frame still came from the camera bolted to the call framing.
		//
		// The pawn is also started ON that framing rather than left where GameModeBase spawned it.
		// That spawn is the world origin -- inside the floor plane -- so anyone who turned free
		// look on got a grazing view of the tiles, which looks exactly like a switch that did
		// nothing. Beginning at the solved shot makes the handover invisible: the picture does not
		// change until the viewer moves.
		APawn* Pawn = PC->GetPawn();
		if (Camera)
		{
			Pawn->SetActorLocation(Camera->GetActorLocation(), false, nullptr, ETeleportType::TeleportPhysics);
			PC->SetControlRotation(Camera->GetActorRotation());
		}
		PC->SetViewTarget(Pawn);
		UE_LOG(LogCompanionStage, Log,
			TEXT("Free look ON: view handed to the pawn, started on the call framing."));
		return;
	}

	if (Camera)
	{
		PC->SetViewTarget(Camera);
	}

	// The actual anti-drift. GameModeBase's DefaultPawn installs its own movement bindings, so
	// mouse X/Y become AddYawInput/AddPitchInput on the control rotation -- which moves the view
	// the moment the view target is a pawn, and quietly walks the pawn out of the set even when
	// it is not. Both are ignored at the controller, which is one place rather than one per
	// binding.
	PC->SetIgnoreLookInput(true);
	PC->SetIgnoreMoveInput(true);
}

void UCompanionStageDirector::BuildPath(const FVector& Subject, float EyeZ)
{
	// The move: begin deep in the hall looking down the rank of machines, sweep forward and
	// around, and arrive at the call framing. The final key MUST match the framing solved for the
	// static shot (160cm back, aimed 22cm below the eyeline) or the shot would jump at handover.
	const float FinalDist = 160.0f;
	const FVector Settle(Subject.X - FinalDist, Subject.Y, EyeZ - 8.0f);
	const FVector SettleLook(Subject.X, Subject.Y, EyeZ - 12.0f);	// must match author_diorama LOOK_AT

	Path.Reset();
	// Wide, high, deep in the hall — establishes the room before it establishes her.
	Path.Add({ FVector(Subject.X + 950.0f, Subject.Y - 520.0f, 300.0f), FVector(Subject.X + 400.0f, Subject.Y - 120.0f, 140.0f), 0.0f });
	// Drop and swing across the aisle, machines passing camera-left.
	Path.Add({ FVector(Subject.X + 620.0f, Subject.Y + 430.0f, 225.0f), FVector(Subject.X + 250.0f, Subject.Y + 40.0f, 130.0f), 0.34f });
	// Come around behind her shoulder — she is still facing away here.
	Path.Add({ FVector(Subject.X + 150.0f, Subject.Y + 250.0f, 190.0f), FVector(Subject.X, Subject.Y, EyeZ - 30.0f), 0.68f });
	// Arrive.
	Path.Add({ Settle, SettleLook, 1.0f });
}

FVector UCompanionStageDirector::SampleSpline(const TArray<FStageCameraKey>& Keys, float T, bool bLookAt)
{
	if (Keys.Num() == 0)
	{
		return FVector::ZeroVector;
	}
	if (T <= Keys[0].Time)
	{
		return bLookAt ? Keys[0].LookAt : Keys[0].Position;
	}
	for (int32 i = 1; i < Keys.Num(); ++i)
	{
		if (T <= Keys[i].Time)
		{
			const float Span = FMath::Max(Keys[i].Time - Keys[i - 1].Time, KINDA_SMALL_NUMBER);
			const float Alpha = StageEase((T - Keys[i - 1].Time) / Span);
			const FVector A = bLookAt ? Keys[i - 1].LookAt : Keys[i - 1].Position;
			const FVector B = bLookAt ? Keys[i].LookAt : Keys[i].Position;
			return FMath::Lerp(A, B, Alpha);
		}
	}
	const FStageCameraKey& Last = Keys.Last();
	return bLookAt ? Last.LookAt : Last.Position;
}

void UCompanionStageDirector::ApplyCameraAt(float TimeSeconds)
{
	if (!Camera || Path.Num() == 0)
	{
		return;
	}
	const float T = FMath::Clamp(TimeSeconds / FMath::Max(FlyDuration, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	const FVector Pos = SampleSpline(Path, T, false);
	const FVector Look = SampleSpline(Path, T, true);
	Camera->SetActorLocation(Pos);
	Camera->SetActorRotation((Look - Pos).Rotation());
}

void UCompanionStageDirector::ApplyFinalFraming()
{
	if (!Camera || Path.Num() == 0)
	{
		return;
	}
	// Deliberately NOT ApplyCameraAt(FlyDuration): that divides by max(FlyDuration, epsilon), so
	// with the static path's FlyDuration of 0 it would sample T=0 and park the camera on the wide
	// establishing key at the far end of the hall.
	const FStageCameraKey& Last = Path.Last();
	Camera->SetActorLocation(Last.Position);
	Camera->SetActorRotation((Last.LookAt - Last.Position).Rotation());
}

void UCompanionStageDirector::Tick(float DeltaTime)
{
	if (!bActive)
	{
		return;
	}
	Elapsed += DeltaTime;

	if (Beat == EStageBeat::Flythrough)
	{
		// Hold on the opening pose through StartDelay so a viewer joining at startup sees the move
		// begin rather than arriving to find it finished.
		if (Elapsed < StartDelay)
		{
			ApplyCameraAt(0.0f);
			return;
		}
		ApplyCameraAt(Elapsed - StartDelay);
		if (Elapsed - StartDelay >= FlyDuration)
		{
			Beat = EStageBeat::Turn;
			UE_LOG(LogCompanionStage, Log, TEXT("Camera settled; turning to face."));
		}
		return;
	}

	if (Beat == EStageBeat::Turn)
	{
		const float T = StageEase(FMath::Clamp((Elapsed - StartDelay - FlyDuration) / TurnDuration, 0.0f, 1.0f));
		if (Resident)
		{
			// Interpolate the SHORT way round, so she never spins the long way to arrive.
			const float Yaw = ResidentStartYaw + FMath::UnwindDegrees(ResidentFinalYaw - ResidentStartYaw) * T;
			Resident->SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
		}
		if (T >= 1.0f)
		{
			Beat = EStageBeat::Speak;
		}
		return;
	}

	if (Beat == EStageBeat::Live)
	{
		if (ReplayEvery > 0.0f && (Elapsed - LiveSince) >= ReplayEvery)
		{
			ReplayOpening();
		}
		return;
	}

	if (Beat == EStageBeat::Speak)
	{
		if (Elapsed < StartDelay + FlyDuration + TurnDuration + SpeakDelay)
		{
			return;
		}
		if (!bSpoken)
		{
			bSpoken = true;
			const FString Line = CICompanionEnv::Get(TEXT("MHLLM_OPENING_LINE"),
				TEXT("Welcome to Companion Intelligence, I'm Eliza. How can I help you?"));
			if (UWorld* World = GetWorld())
			{
				if (UCompanionRuntimeSubsystem* Runtime = World->GetSubsystem<UCompanionRuntimeSubsystem>())
				{
					// Spoken VERBATIM, not generated. An opening line is a brand statement; it must
					// be the same every time and must not wait on an LLM round trip.
					Runtime->SpeakLiteral(Line);
				}
			}
			UE_LOG(LogCompanionStage, Log, TEXT("Opening line: %s"), *Line);
		}
		Beat = EStageBeat::Live;
		LiveSince = Elapsed;
		// Keep ticking ONLY to time the attract loop; with replay off this subsystem goes quiet.
		bActive = ReplayEvery > 0.0f;
		UE_LOG(LogCompanionStage, Log, TEXT("Stage handed over to live conversation.%s"),
			ReplayEvery > 0.0f ? *FString::Printf(TEXT(" Replaying every %.0fs."), ReplayEvery) : TEXT(""));
		return;
	}
}

void UCompanionStageDirector::ReplayOpening()
{
	if (!Camera || !Resident || Path.Num() == 0)
	{
		return;
	}
	Resident->SetActorRotation(FRotator(0.0f, ResidentStartYaw, 0.0f));
	Elapsed = 0.0f;
	bSpoken = false;
	Beat = EStageBeat::Flythrough;
	bActive = true;
	ApplyCameraAt(0.0f);
	UE_LOG(LogCompanionStage, Log, TEXT("Replaying opening shot."));
}

void UCompanionStageDirector::SkipToLive()
{
	if (Beat == EStageBeat::Live)
	{
		return;
	}
	if (Resident)
	{
		Resident->SetActorRotation(FRotator(0.0f, ResidentFinalYaw, 0.0f));
	}
	ApplyFinalFraming();
	Beat = EStageBeat::Live;
	bActive = false;
	UE_LOG(LogCompanionStage, Log, TEXT("Cut to live conversation framing."));
}
