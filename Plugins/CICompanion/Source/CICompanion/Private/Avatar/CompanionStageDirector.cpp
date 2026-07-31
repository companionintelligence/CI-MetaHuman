#include "Avatar/CompanionStageDirector.h"
#include "CICompanionLog.h"

#include "Avatar/CompanionFaceSubsystem.h"
#include "Avatar/CompanionRuntimeSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"


namespace
{
	FString GetEnv(const TCHAR* Name, const FString& Default)
	{
		const FString Value = FPlatformMisc::GetEnvironmentVariable(Name);
		return Value.IsEmpty() ? Default : Value;
	}

	bool EnvDisabled(const TCHAR* Name)
	{
		const FString V = FPlatformMisc::GetEnvironmentVariable(Name);
		return V == TEXT("0") || V.Equals(TEXT("false"), ESearchCase::IgnoreCase);
	}

	// Smoothstep. Linear interpolation between keys makes the camera start and stop abruptly at
	// every waypoint, which reads as a machine executing moves rather than a shot.
	float Ease(float T)
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

void UCompanionStageDirector::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (EnvDisabled(TEXT("MHLLM_OPENING_SHOT")))
	{
		UE_LOG(LogCompanionStage, Log, TEXT("Opening shot disabled; going straight to live."));
		Beat = EStageBeat::Live;
		return;
	}

	// The resident is located the same way the face driver locates it -- by tag, because actor
	// labels are editor-only and do not survive a cook.
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
		UE_LOG(LogCompanionStage, Warning, TEXT("No tagged resident; opening shot skipped."));
		Beat = EStageBeat::Live;
		return;
	}

	// She begins with her back to where the camera will settle, and turns through 180 to meet it.
	ResidentFinalYaw = Resident->GetActorRotation().Yaw;
	ResidentStartYaw = ResidentFinalYaw + 180.0f;
	Resident->SetActorRotation(FRotator(0.0f, ResidentStartYaw, 0.0f));

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
	Camera = InWorld.SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Path[0].Position, FRotator::ZeroRotator, Params);
	if (!Camera)
	{
		UE_LOG(LogCompanionStage, Error, TEXT("Failed to spawn stage camera; opening shot skipped."));
		Beat = EStageBeat::Live;
		return;
	}
	if (UCameraComponent* Cam = Camera->GetCameraComponent())
	{
		Cam->SetFieldOfView(21.0f);
		// Portrait needs BOTH the resolution and this: a CameraComponent constrains its aspect
		// ratio by default, which letterboxes a 16:9 image inside the 9:16 canvas.
		Cam->SetConstraintAspectRatio(false);
	}

	if (APlayerController* PC = InWorld.GetFirstPlayerController())
	{
		PC->SetViewTarget(Camera);
	}

	FlyDuration = FCString::Atof(*GetEnv(TEXT("MHLLM_FLY_SECONDS"), TEXT("11.0")));
	StartDelay  = FCString::Atof(*GetEnv(TEXT("MHLLM_OPENING_DELAY"), TEXT("3.0")));
	ReplayEvery = FCString::Atof(*GetEnv(TEXT("MHLLM_OPENING_REPLAY"), TEXT("0")));
	bActive = true;
	Beat = EStageBeat::Flythrough;
	ApplyCameraAt(0.0f);

	UE_LOG(LogCompanionStage, Log, TEXT("Opening shot: %.1fs flythrough, then turn %.0f -> %.0f."),
		FlyDuration, ResidentStartYaw, ResidentFinalYaw);
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
			const float Alpha = Ease((T - Keys[i - 1].Time) / Span);
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
		const float T = Ease(FMath::Clamp((Elapsed - StartDelay - FlyDuration) / TurnDuration, 0.0f, 1.0f));
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
			const FString Line = GetEnv(TEXT("MHLLM_OPENING_LINE"),
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
	ApplyCameraAt(FlyDuration);
	Beat = EStageBeat::Live;
	bActive = false;
	UE_LOG(LogCompanionStage, Log, TEXT("Opening shot skipped; live."));
}
