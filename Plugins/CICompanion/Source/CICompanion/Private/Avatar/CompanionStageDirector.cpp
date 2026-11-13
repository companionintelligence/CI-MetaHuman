#include "Avatar/CompanionStageDirector.h"
#include "CICompanionEnv.h"
#include "CICompanionLog.h"

#include "Avatar/CompanionFaceSubsystem.h"
#include "Avatar/CompanionRuntimeSubsystem.h"
#include "Settings/CompanionStageSettings.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

const FName UCompanionStageDirector::StageMarkTag(TEXT("StageMark.Home"));
const FName UCompanionStageDirector::ExteriorLightTag(TEXT("ExteriorLight"));

namespace
{
	// Explicit positive values for the opt-in free-look switch. The opening shot has its own
	// default-on policy and uses this parser only when a value is actually supplied.
	bool StageOptedIn(const FString& RawValue)
	{
		const FString V = RawValue.TrimStartAndEnd();
		return V == TEXT("1")
			|| V.Equals(TEXT("true"), ESearchCase::IgnoreCase)
			|| V.Equals(TEXT("yes"), ESearchCase::IgnoreCase)
			|| V.Equals(TEXT("on"), ESearchCase::IgnoreCase);
	}

	// The camera clock never advances more than this per frame: after a hitch the move continues
	// from where it was instead of jumping ahead (design 10.4).
	constexpr float StageMaxCameraStep = 1.0f / 30.0f;

	// Her eye height in world Z, measured rather than assumed (a guessed eyeline once cropped her
	// head off). Eye bones first (MetaHuman FACIAL_*, Character Creator CC_Base_*, generic eye_*),
	// then the head bone plus 9 cm (the mannequin has no eye bones), then 93 % of the height of the
	// whole resident including the actors ATTACHED to it: the MetaHuman resident is three skeletal
	// meshes, and the face mesh's own bounds are a head-sized box. (GetActorBounds'
	// bIncludeFromChildActors does not cover attached actors.) A bone outside a human's range means
	// the pose was not evaluated yet, so it falls through to the bounds.
	float StageMeasureEyeZ(AActor* Resident, FString& OutHow)
	{
		const float Floor = Resident->GetActorLocation().Z;
		auto Plausible = [Floor](float Z) { return Z > Floor + 80.0f && Z < Floor + 230.0f; };
		if (USkeletalMeshComponent* Mesh = Resident->FindComponentByClass<USkeletalMeshComponent>())
		{
			static const FName EyePairs[][2] = {
				{ FName(TEXT("FACIAL_L_Eye")), FName(TEXT("FACIAL_R_Eye")) },
				{ FName(TEXT("CC_Base_L_Eye")), FName(TEXT("CC_Base_R_Eye")) },
				{ FName(TEXT("eye_l")), FName(TEXT("eye_r")) } };
			for (const auto& Pair : EyePairs)
			{
				if (Mesh->DoesSocketExist(Pair[0]) && Mesh->DoesSocketExist(Pair[1]))
				{
					const float Z = 0.5f * (Mesh->GetSocketLocation(Pair[0]).Z + Mesh->GetSocketLocation(Pair[1]).Z);
					if (Plausible(Z))
					{
						OutHow = Pair[0].ToString() + TEXT(" bones");
						return Z;
					}
				}
			}
			const FName Head(TEXT("head"));
			if (Mesh->DoesSocketExist(Head))
			{
				const float Z = Mesh->GetSocketLocation(Head).Z + 9.0f;
				if (Plausible(Z))
				{
					OutHow = TEXT("head bone + 9 cm");
					return Z;
				}
			}
		}
		FBox Box = Resident->GetComponentsBoundingBox(/*bNonColliding*/ true, /*bIncludeFromChildActors*/ true);
		TArray<AActor*> Attached;
		Resident->GetAttachedActors(Attached, /*bResetArray*/ true, /*bRecursivelyIncludeAttachedActors*/ true);
		for (const AActor* Part : Attached)
		{
			if (Part)
			{
				Box += Part->GetComponentsBoundingBox(/*bNonColliding*/ true, /*bIncludeFromChildActors*/ true);
			}
		}
		if (Box.IsValid && Box.GetExtent().Z > KINDA_SMALL_NUMBER)
		{
			OutHow = TEXT("0.93 x her bounds");
			return Box.Min.Z + Box.GetSize().Z * 0.93f;
		}
		OutHow = TEXT("default 158 cm");
		return Floor + 158.1f;
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
	const FString Value = Lookup(TEXT("MHLLM_OPENING_SHOT")).TrimStartAndEnd();
	// A level load is the opening's natural trigger: show the move, then settle on the resident.
	// Explicit false values still give editor and capture workflows a static frame.
	return Value.IsEmpty() || StageOptedIn(Value);
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

TArray<FStageCameraKey> UCompanionStageDirector::FallbackKeys(const FVector& Subject, float EyeZ, float FlySeconds)
{
	TArray<FVector> Positions;
	TArray<FVector> LookAts;
	CompanionStage::FallbackKeys(Subject, EyeZ, Positions, LookAts);
	TArray<FStageCameraKey> Keys;
	for (int32 i = 0; i < Positions.Num(); ++i)
	{
		FStageCameraKey Key;
		Key.Position = Positions[i];
		Key.LookAt = LookAts[i];
		Key.Time = FlySeconds * i / FMath::Max(Positions.Num() - 1, 1);
		Keys.Add(Key);
	}
	return Keys;
}

EStageBeat UCompanionStageDirector::BeatAt(const FCompanionStagePlan& InPlan, float SecondsSinceStart)
{
	const float MoveTime = SecondsSinceStart - InPlan.HoldSeconds;
	if (MoveTime < InPlan.MoveSeconds)
	{
		return EStageBeat::Flythrough;
	}
	if (InPlan.bFromConfig)
	{
		// Her look-up and turn happen DURING the move; once it lands, the frame holds until the
		// turn-yield line.
		return MoveTime < InPlan.SpeakAt ? EStageBeat::Settle : EStageBeat::Live;
	}
	// The built-in path: she turns once the camera has landed, then a beat of stillness, then the line.
	if (MoveTime < InPlan.TurnEnd)
	{
		return EStageBeat::Turn;
	}
	return MoveTime < InPlan.SpeakAt ? EStageBeat::Speak : EStageBeat::Live;
}

void UCompanionStageDirector::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	bOpeningShot = ShouldPlayOpeningShot();
	bFreeLook = ShouldAllowFreeLook();
	bDepthOfField = !CICompanionEnv::IsDisabled(TEXT("MHLLM_STAGE_DOF"));
	ShiftSign = CICompanionEnv::Get(TEXT("MHLLM_STAGE_SHIFT_SIGN"), TEXT("1")).TrimStartAndEnd() == TEXT("-1") ? -1.0f : 1.0f;

	// The resident, her mark and the street-side lights are found the way the face driver finds
	// her -- by tag, because actor labels are editor-only and do not survive a cook.
	AActor* Mark = nullptr;
	for (TActorIterator<AActor> It(&InWorld); It; ++It)
	{
		if (!Resident && It->ActorHasTag(UCompanionFaceSubsystem::ResidentTag))
		{
			Resident = *It;
		}
		if (!Mark && It->ActorHasTag(StageMarkTag))
		{
			Mark = *It;
		}
		if (It->ActorHasTag(ExteriorLightTag))
		{
			ExteriorLights.Add(*It);
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

	const UCompanionStageSettings* Settings = GetDefault<UCompanionStageSettings>();
	FCompanionStageSubject Subject;
	Subject.Mark = Mark ? Mark->GetActorLocation() : Resident->GetActorLocation();
	Subject.FacingYaw = Mark ? Mark->GetActorRotation().Yaw : Settings->HomeFacingYaw;
	FString EyeHow;
	Subject.EyeZ = StageMeasureEyeZ(Resident, EyeHow);

	bool bPortrait = false;
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D Size;
		GEngine->GameViewport->GetViewportSize(Size);
		bPortrait = Size.Y > Size.X;
	}
	auto Env = [](const TCHAR* Name) { return FPlatformMisc::GetEnvironmentVariable(Name); };
	Plan = CompanionStage::BuildPlan(Settings, Subject, Env, bPortrait);
	ResidentFinalYaw = Resident->GetActorRotation().Yaw;

	UE_LOG(LogCompanionStage, Log,
		TEXT("Stage plan from %s: resident %.0f cm (eyes at %.1f cm by %s), mark %s%s, facing %.0f; %s; move %.2f s over %.0f cm, "
			"peak %.2f m/s, nearest %.2f m, time to contact >= %.2f s; greeting %.2f s, look-up %.2f s, turn %.2f-%.2f s "
			"(%.0f deg), settle %.2f s, line %.2f s; hold %.1f s."),
		Plan.bFromConfig ? *FString::Printf(TEXT("the stage layout (%s)"), *Settings->LayoutSource) : TEXT("the built-in path"),
		Plan.StatureCm, Subject.EyeZ, *EyeHow, *Subject.Mark.ToCompactString(), Mark ? TEXT(" (StageMark.Home)") : TEXT(" (her position)"),
		Subject.FacingYaw, bPortrait ? TEXT("portrait") : TEXT("landscape"), Plan.MoveSeconds, Plan.Opening.GetLength(),
		Plan.PeakSpeedCmPerSecond / 100.0f, Plan.MinEyeDistanceCm / 100.0f, Plan.MinTimeToContactSeconds, Plan.GreetingAt,
		Plan.LookUpAt, Plan.TurnStart, Plan.TurnEnd, Plan.WorkTurnDegrees, Plan.SettleAt, Plan.SpeakAt, Plan.HoldSeconds);
	for (const FString& Note : Plan.Notes)
	{
		UE_LOG(LogCompanionStage, Log, TEXT("Stage plan: %s"), *Note);
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// A CineCamera: a focal length on a filmback per shot, level, composed with sensor shift.
	Camera = InWorld.SpawnActor<ACineCameraActor>(ACineCameraActor::StaticClass(), Plan.Lobby.Location, FRotator::ZeroRotator, Params);
	if (!Camera)
	{
		UE_LOG(LogCompanionStage, Error, TEXT("Failed to spawn stage camera; falling back to the map's view."));
		Beat = EStageBeat::Live;
		LockPlayerView();
		return;
	}
	if (UCameraComponent* Cam = Camera->GetCameraComponent())
	{
		// Portrait needs BOTH the resolution and this: a camera constrains its aspect ratio by
		// default, which letterboxes a 16:9 image inside the 9:16 canvas.
		Cam->SetConstraintAspectRatio(false);
	}

	ReplayEvery = FCString::Atof(*CICompanionEnv::Get(TEXT("MHLLM_OPENING_REPLAY"), TEXT("0")));

	if (!bOpeningShot)
	{
		// STATIC (MHLLM_OPENING_SHOT=0, the reduced-motion path). The camera is on the home framing
		// from the first frame and never moves; she is already home. The greeting and the line
		// still play: the line is the thing that proves the build is alive.
		//
		// It still owns the view target rather than deferring to the map's StageCamera, because that
		// camera only exists in the authored diorama: a map without it falls through to the
		// DefaultPawn and the shot becomes whatever the pawn happens to be looking at.
		bStaticShot = true;
		SetResidentYawOffset(0.0f);
		ApplyFraming(Plan.Home);
		SetExteriorLightsVisible(false);
		ReplayEvery = 0.0f;
		Beat = EStageBeat::Speak;
		bActive = true;
		LockPlayerView();
		UE_LOG(LogCompanionStage, Log,
			TEXT("Static shot: camera pinned to the home framing (free-look %s; set MHLLM_OPENING_SHOT=1 for the opening move)."),
			bFreeLook ? TEXT("ON") : TEXT("locked"));
		return;
	}

	Beat = EStageBeat::Flythrough;
	bActive = true;
	SetResidentYawOffset(Plan.WorkTurnDegrees);
	ApplyFraming(Plan.Lobby);
	LockPlayerView();
	UE_LOG(LogCompanionStage, Log, TEXT("Opening: lobby held %.1f s, then %s a %.2f s move; she turns %.0f deg back to the lens."),
		Plan.HoldSeconds, Plan.bCutIntoMove ? TEXT("a cut into") : TEXT("straight into"), Plan.MoveSeconds, Plan.WorkTurnDegrees);
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

void UCompanionStageDirector::ApplyLens(float FocalMm, const FVector2D& FilmbackMm, float Aperture, const FVector2D& ShiftMm, float FocusCm)
{
	if (!Camera)
	{
		return;
	}
	UCineCameraComponent* Cine = Cast<UCineCameraComponent>(Camera->GetCameraComponent());
	if (!Cine)
	{
		if (UCameraComponent* Cam = Camera->GetCameraComponent())
		{
			Cam->SetFieldOfView(FMath::RadiansToDegrees(2.0f * FMath::Atan(FilmbackMm.X / (2.0f * FMath::Max(FocalMm, 1.0f)))));
		}
		return;
	}
	FCameraFilmbackSettings Filmback = Cine->Filmback;
	Filmback.SensorWidth = FilmbackMm.X;
	Filmback.SensorHeight = FilmbackMm.Y;
	// The stage design writes shift with Blender's sign (+ moves the window right or up). Unreal's
	// off-centre projection reads the same way by its maths, but nobody has checked it on a
	// captured frame yet: MHLLM_STAGE_SHIFT_SIGN=-1 flips it without a rebuild.
	Filmback.SensorHorizontalOffset = ShiftSign * ShiftMm.X;
	Filmback.SensorVerticalOffset = ShiftSign * ShiftMm.Y;
	Filmback.RecalcSensorAspectRatio();
	Cine->SetFilmback(Filmback);
	Cine->SetCurrentFocalLength(FocalMm);
	FCameraFocusSettings Focus = Cine->FocusSettings;
	if (bDepthOfField && Aperture > 0.0f)
	{
		Focus.FocusMethod = ECameraFocusMethod::Manual;
		Focus.ManualFocusDistance = FMath::Max(FocusCm, 15.0f);
		Cine->SetCurrentAperture(Aperture);
	}
	else
	{
		Focus.FocusMethod = ECameraFocusMethod::Disable;
	}
	Cine->SetFocusSettings(Focus);
}

void UCompanionStageDirector::ApplyFraming(const FCompanionStageFraming& Framing)
{
	if (!Camera || !Framing.bValid)
	{
		return;
	}
	Camera->SetActorLocationAndRotation(Framing.Location, CompanionStage::AimRotation(Framing.Location, Framing.LookAt, Plan.bLevel));
	const FVector2D Shift = Framing.bHoldScreen
		? CompanionStage::SensorShiftFor(Framing.Location, Framing.LookAt, Framing.Screen, Framing.FocalMm, Framing.FilmbackMm)
		: Framing.ShiftMm;
	ApplyLens(Framing.FocalMm, Framing.FilmbackMm, Framing.Aperture, Shift, FVector::Dist(Framing.Location, Framing.LookAt));
}

void UCompanionStageDirector::ApplyMoveAt(float MoveTime)
{
	if (!Camera)
	{
		return;
	}
	const FVector Pos = Plan.Opening.PositionAt(MoveTime);
	const FVector Look = Plan.Opening.LookAtAt(MoveTime);
	Camera->SetActorLocationAndRotation(Pos, CompanionStage::AimRotation(Pos, Look, Plan.bLevel));
	const FVector2D Shift = Plan.bLevel
		? CompanionStage::SensorShiftFor(Pos, Look, Plan.Opening.ScreenAt(MoveTime), Plan.FocalMm, Plan.FilmbackMm)
		: FVector2D::ZeroVector;
	ApplyLens(Plan.FocalMm, Plan.FilmbackMm, Plan.Aperture, Shift, FVector::Dist(Pos, Look));
}

void UCompanionStageDirector::SetResidentYawOffset(float Degrees)
{
	if (Resident)
	{
		Resident->SetActorRotation(FRotator(0.0f, ResidentFinalYaw + Degrees, 0.0f));
	}
}

void UCompanionStageDirector::SetExteriorLightsVisible(bool bVisible)
{
	int32 Changed = 0;
	for (const TWeakObjectPtr<AActor>& Weak : ExteriorLights)
	{
		if (AActor* Actor = Weak.Get())
		{
			TInlineComponentArray<ULightComponent*> Lights(Actor);
			for (ULightComponent* Light : Lights)
			{
				if (Light && Light->IsVisible() != bVisible)
				{
					Light->SetVisibility(bVisible);
					++Changed;
				}
			}
		}
	}
	if (Changed > 0)
	{
		UE_LOG(LogCompanionStage, Log, TEXT("%s %d street-side light(s) (tag %s)."), bVisible ? TEXT("Restored") : TEXT("Culled"),
			Changed, *ExteriorLightTag.ToString());
	}
}

void UCompanionStageDirector::MarkCameraCut()
{
	// Every discontinuity is flagged, so motion blur, TAA and the streamer's encoder treat it as a
	// cut rather than smearing one frame into the next.
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->SetGameCameraCutThisFrame();
			}
		}
	}
}

void UCompanionStageDirector::Speak(const FString& Line, const TCHAR* What)
{
	if (Line.TrimStartAndEnd().IsEmpty())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (UCompanionRuntimeSubsystem* Runtime = World->GetSubsystem<UCompanionRuntimeSubsystem>())
		{
			// Spoken VERBATIM, not generated. A scripted line must be the same every time and must
			// not wait on an LLM round trip.
			Runtime->SpeakLiteral(Line);
		}
	}
	UE_LOG(LogCompanionStage, Log, TEXT("%s: %s"), What, *Line);
}

void UCompanionStageDirector::FinishOpening()
{
	if (!bSpoken)
	{
		bSpoken = true;
		Speak(CICompanionEnv::Get(TEXT("MHLLM_OPENING_LINE"), TEXT("Welcome to Companion Intelligence, I'm Eliza. How can I help you?")),
			TEXT("Opening line"));
	}
	SetResidentYawOffset(0.0f);
	Beat = EStageBeat::Live;
	LiveSince = Elapsed;
	// Keep ticking ONLY to time the attract loop; with replay off this subsystem goes quiet.
	bActive = ReplayEvery > 0.0f;
	UE_LOG(LogCompanionStage, Log, TEXT("Stage handed over to live conversation.%s"),
		ReplayEvery > 0.0f ? *FString::Printf(TEXT(" Replaying every %.0fs."), ReplayEvery) : TEXT(""));
}

void UCompanionStageDirector::Tick(float DeltaTime)
{
	if (!bActive)
	{
		return;
	}
	Elapsed += FMath::Clamp(DeltaTime, 0.0f, StageMaxCameraStep);

	if (Beat == EStageBeat::Live)
	{
		if (ReplayEvery > 0.0f && (Elapsed - LiveSince) >= ReplayEvery)
		{
			ReplayOpening();
		}
		return;
	}

	if (bStaticShot)
	{
		const float SinceHold = Elapsed - Plan.HoldSeconds;
		if (!bGreeted && Plan.StaticGreetingAt >= 0.0f && SinceHold >= Plan.StaticGreetingAt)
		{
			bGreeted = true;
			Speak(Plan.GreetingLine, TEXT("Greeting"));
		}
		if (SinceHold >= Plan.StaticSpeakAt)
		{
			FinishOpening();
		}
		return;
	}

	const float MoveTime = Elapsed - Plan.HoldSeconds;
	if (MoveTime < 0.0f)
	{
		// The lobby still: held, so a viewer connecting at startup is present for the move rather
		// than joining after the camera has already landed.
		return;
	}
	if (!bMoveStarted)
	{
		bMoveStarted = true;
		if (Plan.bCutIntoMove)
		{
			MarkCameraCut();
		}
		if (Plan.bFromConfig)
		{
			// Inside now: the street-side lights only lit what the lobby still saw.
			SetExteriorLightsVisible(false);
		}
		UE_LOG(LogCompanionStage, Log, TEXT("Opening move: %.2f s, %s."), Plan.MoveSeconds,
			Plan.bCutIntoMove ? TEXT("cut in from the lobby still") : TEXT("from the held first key"));
	}
	ApplyMoveAt(FMath::Min(MoveTime, Plan.MoveSeconds));
	SetResidentYawOffset(CompanionStage::ResidentYawOffsetAt(Plan, MoveTime));
	if (!bGreeted && Plan.GreetingAt >= 0.0f && MoveTime >= Plan.GreetingAt)
	{
		bGreeted = true;
		Speak(Plan.GreetingLine, TEXT("Greeting"));
	}

	const EStageBeat Next = BeatAt(Plan, Elapsed);
	if (Next == Beat)
	{
		return;
	}
	if (Next == EStageBeat::Live)
	{
		FinishOpening();
		return;
	}
	UE_LOG(LogCompanionStage, Log, TEXT("Stage beat %s at %.2f s into the move."),
		*StaticEnum<EStageBeat>()->GetNameStringByValue((int64)Next), MoveTime);
	Beat = Next;
}

void UCompanionStageDirector::ReplayOpening()
{
	if (!Camera || !Resident)
	{
		return;
	}
	Elapsed = 0.0f;
	bMoveStarted = false;
	bGreeted = false;
	bSpoken = false;
	bStaticShot = false;
	Beat = EStageBeat::Flythrough;
	bActive = true;
	SetExteriorLightsVisible(true);
	SetResidentYawOffset(Plan.WorkTurnDegrees);
	ApplyFraming(Plan.Lobby);
	MarkCameraCut();
	UE_LOG(LogCompanionStage, Log, TEXT("Replaying opening shot."));
}

void UCompanionStageDirector::SkipToLive()
{
	if (Beat == EStageBeat::Live)
	{
		return;
	}
	SetResidentYawOffset(0.0f);
	ApplyFraming(Plan.Home);
	MarkCameraCut();
	SetExteriorLightsVisible(false);
	Beat = EStageBeat::Live;
	bActive = false;
	UE_LOG(LogCompanionStage, Log, TEXT("Cut to live conversation framing."));
}
