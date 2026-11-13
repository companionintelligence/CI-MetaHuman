#include "Avatar/CompanionStageDirector.h"
#include "CICompanionEnv.h"
#include "CICompanionLog.h"

#include "Avatar/CompanionFaceSubsystem.h"
#include "Avatar/CompanionRuntimeSubsystem.h"
#include "Settings/CompanionStageSettings.h"
#include "Async/Async.h"
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
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PixelStreaming2Delegates.h"
#include "TimerManager.h"

const FName UCompanionStageDirector::StageMarkTag(TEXT("StageMark.Home"));
const FName UCompanionStageDirector::ExteriorLightTag(TEXT("ExteriorLight"));

// Sees every key and button press before any widget does, and passes all of them on. Over Pixel
// Streaming the browser's input reaches Slate through the same FSlateApplication entry points, so
// this hears a streamed visitor exactly as it hears a local keyboard. It only QUEUES: the director
// applies the input on its next tick, after Slate has finished delivering the same press (an Enter
// that submits a turn reaches UCompanionRuntimeSubsystem in this frame and must win over the key).
class FCompanionStageInputWatcher : public IInputProcessor
{
public:
	explicit FCompanionStageInputWatcher(UCompanionStageDirector* InDirector) : Director(InDirector) {}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		UCompanionStageDirector* Stage = Director.Get();
		if (!Stage || InKeyEvent.IsRepeat())
		{
			return false;
		}
		const FKey Key = InKeyEvent.GetKey();
		// A modifier on its own is not anyone asking for anything.
		if (Key.IsModifierKey() || Key == EKeys::CapsLock)
		{
			return false;
		}
		Stage->QueueInput(Key == EKeys::Escape ? EStageInput::Escape
			: Key == EKeys::Enter ? EStageInput::Enter
			: EStageInput::Key);
		return false;
	}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		if (UCompanionStageDirector* Stage = Director.Get())
		{
			Stage->QueueInput(EStageInput::Click);
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("CompanionStageInput"); }

private:
	TWeakObjectPtr<UCompanionStageDirector> Director;
};

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

	// A key or click in the lobby is G, and the cut inside follows this long after (design 3.1).
	constexpr float StageCutAfterGesture = 0.3f;

	// Esc during the move keeps every line: the greeting now, the turn-yield line this long after.
	constexpr float StageLineAfterEscape = 1.2f;

	// After the turn-yield line, a key lets the line finish (design 3.2's "During the turn-yield
	// line" column) for about as long as the line takes.
	constexpr float StageLineWindowSeconds = 4.0f;

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

	const TCHAR* StageInputName(EStageInput Input)
	{
		switch (Input)
		{
		case EStageInput::Key: return TEXT("key");
		case EStageInput::Click: return TEXT("click");
		case EStageInput::Escape: return TEXT("Esc");
		case EStageInput::Enter: return TEXT("Enter");
		default: return TEXT("turn");
		}
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
	// The move is the default: every session opens with it. Explicit false values give the
	// reduced-motion path (the lobby still cuts straight to HOME).
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

EStageTrigger UCompanionStageDirector::ResolveTrigger(TFunctionRef<FString(const TCHAR*)> Lookup, const TCHAR* CommandLine)
{
	const FString Value = Lookup(TEXT("MHLLM_OPENING_TRIGGER")).TrimStartAndEnd();
	if (Value.Equals(TEXT("load"), ESearchCase::IgnoreCase))
	{
		return EStageTrigger::Load;
	}
	if (Value.Equals(TEXT("viewer"), ESearchCase::IgnoreCase))
	{
		return EStageTrigger::Viewer;
	}
	// Auto: a process told where its signalling server is streams, and a streamed opening waits for
	// the person it is for. FParse::Value matches the key case-insensitively, as Pixel Streaming does.
	static const TCHAR* const UrlKeys[] = { TEXT("PixelStreamingURL="), TEXT("PixelStreamingConnectionURL="), TEXT("PixelStreamingSignallingURL=") };
	for (const TCHAR* Key : UrlKeys)
	{
		// An empty value makes FParse::Value read the NEXT switch ("-PixelStreamingURL= -windowed"
		// gives "-windowed"); a URL never starts with a dash.
		FString Url;
		if (CommandLine && FParse::Value(CommandLine, Key, Url) && !Url.TrimStartAndEnd().IsEmpty() && !Url.StartsWith(TEXT("-")))
		{
			return EStageTrigger::Viewer;
		}
	}
	return EStageTrigger::Load;
}

FStageInputResponse UCompanionStageDirector::RespondToInput(EStagePhase Phase, EStageInput Input, float SecondsUntilGreeting)
{
	FStageInputResponse R;
	if (Input == EStageInput::Enter)
	{
		// Enter with nothing typed: a key like any other. Enter WITH text arrives as a Turn.
		Input = EStageInput::Key;
	}
	const bool bGreetingPending = SecondsUntilGreeting >= 0.0f;
	switch (Phase)
	{
	case EStagePhase::Lobby:
		// Anything is G. A turn typed here is kept and answered once she is in front of the
		// visitor; she then has nothing to yield, so the line is dropped.
		R.bStartSession = true;
		R.HoldSeconds = StageCutAfterGesture;
		if (Input == EStageInput::Turn)
		{
			R.bHoldTurn = true;
			R.bDropLine = true;
		}
		break;
	case EStagePhase::Arrival:
		R.bCutToHome = true;
		if (Input == EStageInput::Turn)
		{
			// Pending lines dropped; she answers.
			R.bDropLine = true;
			R.bDropGreeting = bGreetingPending;
		}
		else if (Input == EStageInput::Escape)
		{
			// Esc stops the motion and keeps the words.
			R.bKeepLines = true;
			R.bGreetNow = bGreetingPending;
		}
		else
		{
			// The visitor already has the floor: no turn-yield line. "Oh, hello." only if it was
			// about to be said anyway.
			R.bDropLine = true;
			R.bGreetNow = bGreetingPending && SecondsUntilGreeting <= 1.0f;
			R.bDropGreeting = bGreetingPending && !R.bGreetNow;
		}
		break;
	case EStagePhase::Line:
	case EStagePhase::Live:
	default:
		// The line finishes; live input belongs to the conversation, not the camera.
		break;
	}
	return R;
}

bool UCompanionStageDirector::ShouldReplay(float ReplayEverySeconds, float SecondsSinceLastInput, bool bViewerConnected, bool bLive)
{
	return ReplayEverySeconds > 0.0f && bLive && !bViewerConnected && SecondsSinceLastInput >= ReplayEverySeconds;
}

float UCompanionStageDirector::CloseCutTime(const FString& Text, float DurationSeconds, float MinReplySeconds, float MinSeconds)
{
	const int32 N = Text.Len();
	if (DurationSeconds < MinReplySeconds || N == 0)
	{
		return -1.0f;
	}
	for (int32 i = 0; i < N; ++i)
	{
		const TCHAR C = Text[i];
		if ((C == TEXT('.') || C == TEXT('!') || C == TEXT('?')) && (i + 1 == N || FChar::IsWhitespace(Text[i + 1])))
		{
			// Where the boundary falls in the audio, by its share of the characters.
			const float T = DurationSeconds * float(i + 1) / float(N);
			if (T >= MinSeconds && T <= DurationSeconds - 1.0f)
			{
				return T;
			}
		}
	}
	return -1.0f;
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
			"(%.0f deg), settle %.2f s, line %.2f s; hold %.1f s after a session starts; CLOSE %s."),
		Plan.bFromConfig ? *FString::Printf(TEXT("the stage layout (%s)"), *Settings->LayoutSource) : TEXT("the built-in path"),
		Plan.StatureCm, Subject.EyeZ, *EyeHow, *Subject.Mark.ToCompactString(), Mark ? TEXT(" (StageMark.Home)") : TEXT(" (her position)"),
		Subject.FacingYaw, bPortrait ? TEXT("portrait") : TEXT("landscape"), Plan.MoveSeconds, Plan.Opening.GetLength(),
		Plan.PeakSpeedCmPerSecond / 100.0f, Plan.MinEyeDistanceCm / 100.0f, Plan.MinTimeToContactSeconds, Plan.GreetingAt,
		Plan.LookUpAt, Plan.TurnStart, Plan.TurnEnd, Plan.WorkTurnDegrees, Plan.SettleAt, Plan.SpeakAt, Plan.HoldSeconds,
		Plan.Close.bValid ? *FString::Printf(TEXT("%.0f mm"), Plan.Close.FocalMm) : TEXT("none"));
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
	Trigger = ResolveTrigger(Env, FCommandLine::Get());
	// Reduced motion means no breathing either (design 3.3).
	BreathDegrees = (bOpeningShot && !CICompanionEnv::IsDisabled(TEXT("MHLLM_STAGE_BREATHING")))
		? FMath::Max(Settings->BreathingDegrees, 0.0f) : 0.0f;

	RestageLobby();
	LockPlayerView();

	// Every key and click, local or streamed (design 3.2). Absent under -nullrhi commandlets, which
	// have no Slate application and no visitors.
	if (FSlateApplication::IsInitialized())
	{
		InputWatcher = MakeShared<FCompanionStageInputWatcher>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(InputWatcher);
	}
	BindViewerEvents();

	UE_LOG(LogCompanionStage, Log,
		TEXT("Opening: %s; the lobby still is held %.1f s after a session starts (0.3 s after a key or click), then %s; "
			"breathing %.2f deg; kiosk reset %s."),
		Trigger == EStageTrigger::Viewer
			? TEXT("a session starts when a Pixel Streaming viewer connects (MHLLM_OPENING_TRIGGER=viewer, or a streaming command line)")
			: TEXT("a session starts at level load (MHLLM_OPENING_TRIGGER=load, or no streaming command line)"),
		Plan.HoldSeconds,
		bOpeningShot ? *FString::Printf(TEXT("%s a %.2f s move; she turns %.0f deg back to the lens"),
			Plan.bCutIntoMove ? TEXT("a cut into") : TEXT("straight into"), Plan.MoveSeconds, Plan.WorkTurnDegrees)
			: TEXT("a cut straight to HOME (reduced motion, MHLLM_OPENING_SHOT=0)"),
		BreathDegrees, ReplayEvery > 0.0f ? *FString::Printf(TEXT("after %.0f s idle with no viewer"), ReplayEvery) : TEXT("off"));

	if (Trigger == EStageTrigger::Load)
	{
		BeginSession(Plan.HoldSeconds);
	}
}

void UCompanionStageDirector::Deinitialize()
{
	if (InputWatcher.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputWatcher);
	}
	InputWatcher.Reset();
	// The delegates object is a rooted singleton that outlives this world.
	if (ViewerArrivedHandle.IsValid() || ViewersLeftHandle.IsValid())
	{
		if (UPixelStreaming2Delegates* StreamDelegates = UPixelStreaming2Delegates::Get())
		{
			StreamDelegates->OnDataTrackOpenNative.Remove(ViewerArrivedHandle);
			StreamDelegates->OnAllConnectionsClosedNative.Remove(ViewersLeftHandle);
		}
		ViewerArrivedHandle.Reset();
		ViewersLeftHandle.Reset();
	}
	Super::Deinitialize();
}

void UCompanionStageDirector::BindViewerEvents()
{
	UPixelStreaming2Delegates* StreamDelegates = UPixelStreaming2Delegates::Get();
	if (!StreamDelegates)
	{
		return;
	}
	// A WEAK pointer and a hop to the game thread, as UCompanionRuntimeSubsystem does: these are
	// thread-safe multicast delegates, and 5.5's streamer broadcasts them from its own thread.
	//
	// The data channel opening is the viewer's arrival: it is the WebRTC connection becoming usable,
	// not the signalling handshake, so the hold that follows is time the visitor can see.
	ViewerArrivedHandle = StreamDelegates->OnDataTrackOpenNative.AddLambda(
		[WeakThis = TWeakObjectPtr<UCompanionStageDirector>(this)](FString StreamerId, FString PlayerId)
		{
			auto Arrived = [WeakThis, PlayerId]()
			{
				if (UCompanionStageDirector* Self = WeakThis.Get())
				{
					Self->HandleViewerArrived(PlayerId);
				}
			};
			if (IsInGameThread())
			{
				Arrived();
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, MoveTemp(Arrived));
			}
		});
	ViewersLeftHandle = StreamDelegates->OnAllConnectionsClosedNative.AddLambda(
		[WeakThis = TWeakObjectPtr<UCompanionStageDirector>(this)](FString StreamerId)
		{
			auto Left = [WeakThis]()
			{
				if (UCompanionStageDirector* Self = WeakThis.Get())
				{
					Self->HandleViewersLeft();
				}
			};
			if (IsInGameThread())
			{
				Left();
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, MoveTemp(Left));
			}
		});
}

void UCompanionStageDirector::HandleViewerArrived(const FString& PlayerId)
{
	const bool bFirst = !bViewerConnected;
	bViewerConnected = true;
	LastInputAt = Clock;
	UE_LOG(LogCompanionStage, Log, TEXT("Pixel Streaming viewer %s connected (data channel open)%s."), *PlayerId,
		bFirst ? TEXT("") : TEXT("; a viewer was already here"));
	if (bFirst && Trigger == EStageTrigger::Viewer && bAwaitingSession)
	{
		BeginSession(Plan.HoldSeconds);
	}
}

void UCompanionStageDirector::HandleViewersLeft()
{
	bViewerConnected = false;
	LastInputAt = Clock;
	if (Trigger == EStageTrigger::Viewer)
	{
		// The next visitor gets the whole opening, from the street.
		UE_LOG(LogCompanionStage, Log, TEXT("Last Pixel Streaming viewer left: restaging the lobby for the next one."));
		RestageLobby();
	}
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
	// off-centre projection reads the same way: the validation capture of HOME put her eyes on the
	// design's (0.63, 0.33) with +1 and on the wrong side with -1. MHLLM_STAGE_SHIFT_SIGN=-1 stays as
	// a switch for a renderer that disagrees.
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

void UCompanionStageDirector::HoldFraming(const FCompanionStageFraming& Framing, bool bCut)
{
	if (!Framing.bValid)
	{
		return;
	}
	Held = Framing;
	ApplyFraming(Framing);
	if (bCut)
	{
		MarkCameraCut();
	}
}

void UCompanionStageDirector::Breathe()
{
	if (BreathDegrees <= 0.0f || !Camera || !Held.bValid)
	{
		return;
	}
	const FRotator Base = CompanionStage::AimRotation(Held.Location, Held.LookAt, Plan.bLevel);
	Camera->SetActorRotation(Base + CompanionStage::BreathOffset(Clock, BreathDegrees));
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
	LastCutAt = Clock;
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

void UCompanionStageDirector::RestageLobby()
{
	if (!Camera || !Resident)
	{
		return;
	}
	bAwaitingSession = true;
	Beat = EStageBeat::Flythrough;
	Elapsed = 0.0f;
	ActiveHold = Plan.HoldSeconds;
	bMoveStarted = false;
	bGreeted = false;
	bSpoken = false;
	bDropLine = false;
	HeldTurn.Reset();
	LineDueAt = -1.0f;
	bInClose = false;
	CloseAt = -1.0f;
	CloseReturnAt = -1.0f;
	PendingInputs.Reset();
	bStaticShot = !bOpeningShot;
	SetExteriorLightsVisible(true);
	// At work, her body turned toward the desk and the door (the reduced-motion path too: the cut
	// to HOME completes her turn under it).
	SetResidentYawOffset(Plan.WorkTurnDegrees);
	HoldFraming(Plan.Lobby, /*bCut*/ true);
	bActive = true;
	UE_LOG(LogCompanionStage, Log, TEXT("Lobby staged: the still from the street, nothing spoken, waiting for %s."),
		Trigger == EStageTrigger::Viewer ? TEXT("a viewer, a key or a click") : TEXT("the session"));
}

void UCompanionStageDirector::BeginSession(float HoldSeconds)
{
	if (!Camera || !Resident)
	{
		return;
	}
	if (!bAwaitingSession)
	{
		UE_LOG(LogCompanionStage, Verbose, TEXT("BeginSession: a session is already running."));
		return;
	}
	bAwaitingSession = false;
	++Sessions;
	Elapsed = 0.0f;
	ActiveHold = HoldSeconds >= 0.0f ? HoldSeconds : Plan.HoldSeconds;
	Beat = EStageBeat::Flythrough;
	LastInputAt = Clock;
	UE_LOG(LogCompanionStage, Log, TEXT("Session %d: the lobby still holds %.1f s more, then %s."), Sessions, ActiveHold,
		bStaticShot ? TEXT("a cut to HOME") : TEXT("the cut inside and the move"));
}

void UCompanionStageDirector::QueueInput(EStageInput Input)
{
	PendingInputs.Add(Input);
}

EStagePhase UCompanionStageDirector::CurrentPhase() const
{
	if (bAwaitingSession)
	{
		return EStagePhase::Lobby;
	}
	if (Beat == EStageBeat::Live)
	{
		return (bSpoken && !bDropLine && Clock - LiveSince < StageLineWindowSeconds) ? EStagePhase::Line : EStagePhase::Live;
	}
	if (LineDueAt >= 0.0f)
	{
		return EStagePhase::Arrival;		// Esc'd onto HOME, the lines still to come
	}
	return Elapsed < ActiveHold ? EStagePhase::Lobby : EStagePhase::Arrival;
}

float UCompanionStageDirector::SecondsUntilGreeting() const
{
	if (bGreeted)
	{
		return -1.0f;
	}
	const float At = bStaticShot ? Plan.StaticGreetingAt : Plan.GreetingAt;
	if (At < 0.0f)
	{
		return -1.0f;
	}
	return FMath::Max(ActiveHold + At - Elapsed, 0.0f);
}

bool UCompanionStageDirector::IsOnHome() const
{
	// Landed (Settle, or the built-in path's Turn and Speak beats), or the reduced-motion cut done:
	// a cut to HOME would change nothing but the anti-aliasing history.
	return Beat == EStageBeat::Settle || Beat == EStageBeat::Turn || Beat == EStageBeat::Speak || (bStaticShot && bMoveStarted);
}

void UCompanionStageDirector::CutToHome(const TCHAR* Why)
{
	// Her remaining turn (30 degrees at most) completes under the cut, so it is never seen as a snap.
	SetResidentYawOffset(0.0f);
	SetExteriorLightsVisible(false);
	bMoveStarted = true;
	bInClose = false;
	HoldFraming(Plan.Home, /*bCut*/ true);
	UE_LOG(LogCompanionStage, Log, TEXT("Cut to HOME (%s)."), Why);
}

void UCompanionStageDirector::ApplyInput(EStageInput Input)
{
	LastInputAt = Clock;
	if (!Camera || !Resident)
	{
		return;
	}
	const EStagePhase Phase = CurrentPhase();
	const FStageInputResponse R = RespondToInput(Phase, Input, SecondsUntilGreeting());
	if (R.bStartSession)
	{
		if (bAwaitingSession)
		{
			UE_LOG(LogCompanionStage, Log, TEXT("The visitor's %s in the lobby starts the session."), StageInputName(Input));
			BeginSession(R.HoldSeconds);
		}
		else if (Elapsed + R.HoldSeconds < ActiveHold)
		{
			// Already started (a viewer arrived, or the level loaded): the visitor is ready sooner.
			ActiveHold = Elapsed + R.HoldSeconds;
			UE_LOG(LogCompanionStage, Log, TEXT("The visitor's %s shortens the lobby hold: the cut comes in %.1f s."), StageInputName(Input), R.HoldSeconds);
		}
	}
	if (R.bDropLine && !bDropLine)
	{
		bDropLine = true;
		UE_LOG(LogCompanionStage, Log, TEXT("The turn-yield line is dropped: the visitor has the floor (%s)."), StageInputName(Input));
	}
	if (R.bGreetNow && !bGreeted)
	{
		bGreeted = true;
		Speak(Plan.GreetingLine, TEXT("Greeting (on the visitor's input)"));
	}
	else if (R.bDropGreeting && !bGreeted)
	{
		bGreeted = true;
		UE_LOG(LogCompanionStage, Log, TEXT("The greeting is dropped: it was not due within 1 s."));
	}
	if (R.bCutToHome && Phase == EStagePhase::Arrival && LineDueAt < 0.0f)
	{
		if (!IsOnHome())
		{
			CutToHome(*FString::Printf(TEXT("the visitor's %s"), StageInputName(Input)));
		}
		if (R.bKeepLines)
		{
			// Esc: nothing dropped. The line follows the greeting.
			LineDueAt = Clock + (R.bGreetNow ? StageLineAfterEscape : 0.6f);
		}
		else
		{
			FinishOpening();
		}
	}
	if (bInClose && Phase == EStagePhase::Live && Input != EStageInput::Turn)
	{
		// Back from the punch-in on the visitor's first key or click (design 3.1 "Reply").
		bInClose = false;
		CloseReturnAt = -1.0f;
		HoldFraming(Plan.Home, /*bCut*/ true);
		UE_LOG(LogCompanionStage, Log, TEXT("Back to HOME from CLOSE (the visitor's %s)."), StageInputName(Input));
	}
}

bool UCompanionStageDirector::OnVisitorTurn(const FString& Text)
{
	// The Enter that submitted this turn was queued by the input watcher a moment ago in the same
	// frame; it IS this turn, not a separate key.
	PendingInputs.RemoveAll([](EStageInput In) { return In == EStageInput::Enter; });
	LastInputAt = Clock;
	if (!Camera || !Resident)
	{
		return false;
	}
	const EStagePhase Phase = CurrentPhase();
	const FStageInputResponse R = RespondToInput(Phase, EStageInput::Turn, SecondsUntilGreeting());
	if (R.bStartSession)
	{
		if (bAwaitingSession)
		{
			BeginSession(R.HoldSeconds);
		}
		else if (Elapsed + R.HoldSeconds < ActiveHold)
		{
			ActiveHold = Elapsed + R.HoldSeconds;
		}
	}
	if (R.bDropLine)
	{
		bDropLine = true;
	}
	if (R.bDropGreeting)
	{
		bGreeted = true;
	}
	if (R.bHoldTurn)
	{
		HeldTurn = Text;
		UE_LOG(LogCompanionStage, Log, TEXT("A turn typed in the lobby is held: the opening plays, and she answers it when the move lands."));
		return true;
	}
	if (R.bCutToHome && LineDueAt < 0.0f)
	{
		if (!IsOnHome())
		{
			CutToHome(TEXT("a submitted turn"));
		}
		FinishOpening();
	}
	else if (R.bCutToHome)
	{
		// Esc'd already: the lines it kept are now dropped as well.
		LineDueAt = -1.0f;
		FinishOpening();
	}
	if (bInClose)
	{
		bInClose = false;
		CloseReturnAt = -1.0f;
		HoldFraming(Plan.Home, /*bCut*/ true);
	}
	return false;
}

void UCompanionStageDirector::OnReplyAudioStarted(const FString& Text, float DurationSeconds)
{
	if (!Camera || bAwaitingSession || Beat != EStageBeat::Live || !Plan.Close.bValid)
	{
		return;
	}
	// The scripted lines are not replies.
	if (Text.TrimStartAndEnd().Equals(Plan.GreetingLine.TrimStartAndEnd()) || Text.TrimStartAndEnd().Equals(SpokenLine.TrimStartAndEnd()))
	{
		return;
	}
	const UCompanionStageSettings* Settings = GetDefault<UCompanionStageSettings>();
	if (bInClose)
	{
		// One punch-in per reply: already in, so this reply only moves the return.
		CloseReturnAt = Clock + DurationSeconds + Settings->CloseReturnSeconds;
		return;
	}
	const float At = CloseCutTime(Text, DurationSeconds, Settings->CloseMinReplySeconds, 1.5f);
	if (At < 0.0f)
	{
		return;
	}
	CloseAt = Clock + At;
	CloseReturnAt = Clock + DurationSeconds + Settings->CloseReturnSeconds;
	UE_LOG(LogCompanionStage, Log, TEXT("A %.1f s reply: CLOSE at its sentence boundary %.1f s in, HOME again %.0f s after it ends."),
		DurationSeconds, At, Settings->CloseReturnSeconds);
}

void UCompanionStageDirector::FinishOpening()
{
	if (!bSpoken)
	{
		bSpoken = true;
		if (bDropLine)
		{
			UE_LOG(LogCompanionStage, Log, TEXT("Opening line not spoken: the visitor took the floor first."));
		}
		else
		{
			SpokenLine = CICompanionEnv::Get(TEXT("MHLLM_OPENING_LINE"), TEXT("Welcome to Companion Intelligence, I'm Eliza. How can I help you?"));
			Speak(SpokenLine, TEXT("Opening line"));
		}
	}
	SetResidentYawOffset(0.0f);
	SetExteriorLightsVisible(false);
	if (!Held.bValid || !Held.Location.Equals(Plan.Home.Location, 0.1))
	{
		// The move has landed on HOME (within 1 mm, CompanionStageSpec): hold it, no cut.
		HoldFraming(Plan.Home, /*bCut*/ false);
	}
	Beat = EStageBeat::Live;
	LiveSince = Clock;
	LineDueAt = -1.0f;
	bActive = true;
	if (!HeldTurn.IsEmpty())
	{
		const FString Turn = MoveTemp(HeldTurn);
		HeldTurn.Reset();
		if (UWorld* World = GetWorld())
		{
			if (UCompanionRuntimeSubsystem* Runtime = World->GetSubsystem<UCompanionRuntimeSubsystem>())
			{
				UE_LOG(LogCompanionStage, Log, TEXT("She answers the turn held since the lobby."));
				Runtime->SubmitUserText(Turn);
			}
		}
	}
	UE_LOG(LogCompanionStage, Log, TEXT("Stage handed over to live conversation (session %d)."), Sessions);
}

void UCompanionStageDirector::TickLive()
{
	const UCompanionStageSettings* Settings = GetDefault<UCompanionStageSettings>();
	if (CloseAt >= 0.0f && Clock >= CloseAt)
	{
		CloseAt = -1.0f;
		if (!bInClose && Plan.Close.bValid && Clock - LastCutAt >= Settings->CutSpacingSeconds)
		{
			bInClose = true;
			HoldFraming(Plan.Close, /*bCut*/ true);
			UE_LOG(LogCompanionStage, Log, TEXT("Punch in: CLOSE (%.0f mm)."), Plan.Close.FocalMm);
		}
	}
	if (bInClose && CloseReturnAt >= 0.0f && Clock >= CloseReturnAt)
	{
		bInClose = false;
		CloseReturnAt = -1.0f;
		HoldFraming(Plan.Home, /*bCut*/ true);
		UE_LOG(LogCompanionStage, Log, TEXT("Back to HOME after the reply."));
	}
	// The kiosk reset: idle with nobody streaming. Counted from the last input or the landing,
	// whichever is later, so it never interrupts anyone.
	if (ShouldReplay(ReplayEvery, Clock - FMath::Max(LastInputAt, LiveSince), bViewerConnected, true))
	{
		UE_LOG(LogCompanionStage, Log, TEXT("Idle %.0f s with no viewer: the kiosk reset replays the opening."), ReplayEvery);
		ReplayOpening();
		return;
	}
	Breathe();
}

void UCompanionStageDirector::Tick(float DeltaTime)
{
	if (!bActive)
	{
		return;
	}
	const float Dt = FMath::Clamp(DeltaTime, 0.0f, StageMaxCameraStep);
	Clock += Dt;

	if (PendingInputs.Num() > 0)
	{
		TArray<EStageInput> Inputs = MoveTemp(PendingInputs);
		PendingInputs.Reset();
		for (const EStageInput Input : Inputs)
		{
			ApplyInput(Input);
		}
	}
	if (!Camera || !Resident)
	{
		return;
	}
	if (bAwaitingSession)
	{
		// The lobby still, held until a session starts.
		Breathe();
		return;
	}
	if (Beat == EStageBeat::Live)
	{
		TickLive();
		return;
	}
	Elapsed += Dt;

	if (LineDueAt >= 0.0f)
	{
		// Esc during the move: on HOME, the turn-yield line still to come.
		if (Clock >= LineDueAt)
		{
			FinishOpening();
		}
		else
		{
			Breathe();
		}
		return;
	}

	if (bStaticShot)
	{
		// Reduced motion: the lobby still, then one flagged cut to HOME; no move, no breathing.
		if (Elapsed < ActiveHold)
		{
			return;
		}
		if (!bMoveStarted)
		{
			bMoveStarted = true;
			SetResidentYawOffset(0.0f);
			SetExteriorLightsVisible(false);
			HoldFraming(Plan.Home, /*bCut*/ true);
			UE_LOG(LogCompanionStage, Log, TEXT("Reduced motion: cut straight to HOME."));
		}
		const float SinceHold = Elapsed - ActiveHold;
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

	const float MoveTime = Elapsed - ActiveHold;
	if (MoveTime < 0.0f)
	{
		// The lobby still, still held after G: the visitor sees the street before the cut.
		Breathe();
		return;
	}
	if (!bMoveStarted)
	{
		bMoveStarted = true;
		Held.bValid = false;		// moving: nothing to breathe on
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

	// BeatAt counts from the start of the hold the PLAN knows; this session's hold may be shorter.
	const EStageBeat Next = BeatAt(Plan, MoveTime + Plan.HoldSeconds);
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
	UE_LOG(LogCompanionStage, Log, TEXT("Replaying the opening."));
	RestageLobby();
	BeginSession(Plan.HoldSeconds);
}

void UCompanionStageDirector::SkipToLive()
{
	if (!Camera || !Resident || (Beat == EStageBeat::Live && !bAwaitingSession))
	{
		return;
	}
	if (bAwaitingSession)
	{
		bAwaitingSession = false;
		++Sessions;
	}
	bDropLine = true;
	bGreeted = true;
	CutToHome(TEXT("SkipToLive"));
	FinishOpening();
}
