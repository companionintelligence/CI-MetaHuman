#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/Function.h"
#include "Avatar/CompanionStagePath.h"
#include "CompanionStageDirector.generated.h"

class ACameraActor;
class AActor;
class UCompanionStageSettings;
class FCompanionStageInputWatcher;

// One keyframe of the built-in opening path: where the camera is, and what it is aimed at.
USTRUCT()
struct FStageCameraKey
{
	GENERATED_BODY()

	UPROPERTY() FVector Position = FVector::ZeroVector;
	UPROPERTY() FVector LookAt = FVector::ZeroVector;
	// Seconds from the start of the move at which the camera should BE here.
	UPROPERTY() float Time = 0.0f;
};

UENUM()
enum class EStageBeat : uint8
{
	Flythrough,	// the lobby still is held, then the camera makes its move
	Settle,		// the move has landed on the home framing; the turn-yield line is next
	Turn,		// she rotates to face the lens (the built-in path: after the move)
	Speak,		// scripted opening line
	Live		// handed over to normal conversation
};

// What starts a session (design 3.1's G). LOAD: the level loading, as a kiosk or a native window
// does. VIEWER: the first Pixel Streaming viewer's data channel opening -- a streamed process holds
// the lobby still until someone is there to see it, and goes back to it when the last viewer leaves.
UENUM()
enum class EStageTrigger : uint8
{
	Load,
	Viewer
};

// A visitor's input, as design 3.2 sorts it.
UENUM()
enum class EStageInput : uint8
{
	Key,		// any key but Enter and Esc; also Enter with nothing typed
	Click,		// a mouse button or a tap
	Escape,
	Enter,		// queued by the input watcher; a submitted turn arrives separately (Turn)
	Turn		// Enter with text, or a transcribed spoken turn
};

// Where the opening stands, for input routing (design 3.2's columns).
UENUM()
enum class EStagePhase : uint8
{
	Lobby,		// the lobby still (before a session, or before the cut inside)
	Arrival,	// the move, or the reduced-motion wait on HOME before the line
	Line,		// the turn-yield line has just been given
	Live
};

// What one input does to the opening. Pure data, so the specs can walk design 3.2's table.
struct CICOMPANION_API FStageInputResponse
{
	// G: start the session now; the cut inside comes HoldSeconds later.
	bool bStartSession = false;
	float HoldSeconds = 0.0f;
	// A flagged cut to HOME this frame, her turn completed under it.
	bool bCutToHome = false;
	// The turn-yield line is not spoken: the visitor already has the floor.
	bool bDropLine = false;
	// "Oh, hello.": spoken now, or never.
	bool bGreetNow = false;
	bool bDropGreeting = false;
	// A turn typed in the lobby: kept, and answered when the move lands.
	bool bHoldTurn = false;
	// Esc: everything still plays, from HOME.
	bool bKeepLines = false;
};

// Directs the stage camera: the lobby still, the opening move into the conversation framing, and
// the beats around it (the distance salutation, her look-up and turn, the turn-yield line), then the
// live conversation's framing (HOME, the CLOSE punch-in on long replies, rotation-only breathing).
//
// DATA-DRIVEN. Shots, lenses, composition and beat timings come from UCompanionStageSettings
// (Config/DefaultGame.ini), which Deployment/stage_layout.py writes from the same
// Deployment/stage_layout.json that author_diorama.py builds the map from. Resident-frame shots
// are solved at BeginPlay against her mark (the StageMark.Home TargetPoint) and her MEASURED eyes,
// so any human-scale resident gets the same framing. With no stage section in the config the
// director flies its built-in path instead (CompanionStage::FallbackKeys).
//
// SESSIONS. The opening plays once per session, and a session starts on the trigger (see
// EStageTrigger; MHLLM_OPENING_TRIGGER, default: VIEWER when the process streams, LOAD otherwise) or
// on the visitor's first key, click or Esc in the lobby (design 3.2). Until then the room holds the
// lobby still with nothing spoken, so a stream visitor sees the whole opening from the street and
// hears both lines. When the last viewer leaves the room goes back to the lobby still.
//
// Driven from C++ rather than a LevelSequence for the same reason the face driver is: the map is
// authored headlessly by Python in a content-only project that cannot reference plugin C++, embed
// a Blueprint, or carry a Sequencer asset (HANDOFF.md Lesson 6). A tickable subsystem is the only
// place this can live and still survive the cook.
//
// It owns its own CineCamera and sets it as the view target, which deliberately supersedes the
// authored StageCamera.
UCLASS()
class CICOMPANION_API UCompanionStageDirector : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// Tags the authored map carries (author_diorama.py): her home mark, and the lights that only
	// light the street side, culled once the camera is inside.
	static const FName StageMarkTag;
	static const FName ExteriorLightTag;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UCompanionStageDirector, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return bActive; }

	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Stage")
	EStageBeat GetBeat() const { return Beat; }

	// Aborts the opening wherever it is (the lobby included) and hands straight to live
	// conversation: a flagged cut to HOME, pending lines dropped.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Stage")
	void SkipToLive();

	// Restages the lobby and plays the opening from the top, honouring MHLLM_OPENING_SHOT (the
	// reduced-motion path cuts from the lobby straight to HOME).
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Stage")
	void ReplayOpening();

	// Back to the lobby still, waiting for the next session: the street-side lights on, her at work,
	// nothing pending. Called when the last viewer leaves.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Stage")
	void RestageLobby();

	// G from outside (a session-start message, a viewer arriving): starts the opening if the room is
	// waiting in the lobby. HoldSeconds < 0 uses the plan's hold.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Stage")
	void BeginSession(float HoldSeconds = -1.0f);

	// A key, click, tap or Esc from the visitor (the input watcher calls this; applied next tick).
	void QueueInput(EStageInput Input);

	// A submitted turn (Enter with text, or a transcribed one), from UCompanionRuntimeSubsystem
	// before it reaches the model. Returns true when the stage HOLDS it (a turn typed in the lobby is
	// answered when the move lands, design 3.2): the caller must not submit it now.
	bool OnVisitorTurn(const FString& Text);

	// A reply's audio started (UCompanionRuntimeSubsystem, from the voice): long replies get the CLOSE
	// punch-in at the first sentence boundary at least 1.5 s in.
	void OnReplyAudioStarted(const FString& Text, float DurationSeconds);

	// The plan the director is playing (for logs and the automation specs).
	const FCompanionStagePlan& GetPlan() const { return Plan; }

	EStageTrigger GetTrigger() const { return Trigger; }
	bool IsAwaitingSession() const { return bAwaitingSession; }

	// The opening move plays by default. Set MHLLM_OPENING_SHOT=0 to use the reduced-motion path;
	// explicit 1/true/yes/on also enables the move.
	static bool ShouldPlayOpeningShot(TFunctionRef<FString(const TCHAR*)> Lookup);

	// Free-look is likewise opt-IN. Left on, the GameModeBase DefaultPawn's default movement
	// bindings turn every mouse move into camera yaw/pitch, so a shot that was framed to the
	// centimetre drifts off the subject as soon as anyone touches the mouse -- and over Pixel
	// Streaming, as soon as any VIEWER does.
	static bool ShouldAllowFreeLook(TFunctionRef<FString(const TCHAR*)> Lookup);

	// What starts a session: MHLLM_OPENING_TRIGGER=load|viewer, else VIEWER when the command line
	// names a Pixel Streaming server (-PixelStreamingURL=, -PixelStreamingConnectionURL= or
	// -PixelStreamingSignallingURL=) and LOAD when it does not.
	static EStageTrigger ResolveTrigger(TFunctionRef<FString(const TCHAR*)> Lookup, const TCHAR* CommandLine);

	// Design 3.2: what one input does in one phase. SecondsUntilGreeting is how far off "Oh, hello."
	// still is (negative: already given, or none).
	static FStageInputResponse RespondToInput(EStagePhase Phase, EStageInput Input, float SecondsUntilGreeting);

	// The kiosk reset (MHLLM_OPENING_REPLAY): replay only with nobody watching the stream, and only
	// after the visitor has been idle that long. Never during a session someone is in.
	static bool ShouldReplay(float ReplayEverySeconds, float SecondsSinceLastInput, bool bViewerConnected, bool bLive);

	// When to punch in to CLOSE on a reply of DurationSeconds whose text is Text: the first sentence
	// boundary at least MinSeconds in, by its share of the characters. Negative: no punch-in (a short
	// reply, or no boundary in range).
	static float CloseCutTime(const FString& Text, float DurationSeconds, float MinReplySeconds = 6.0f, float MinSeconds = 1.5f);

	// Process-environment overloads of the two above; the injectable form exists so the policy is
	// testable without mutating the real environment (see EnvConfigSpec for the same pattern).
	static bool ShouldPlayOpeningShot();
	static bool ShouldAllowFreeLook();

	// The built-in opening path, resolved against a subject (the fallback when the config has no
	// stage shots).
	static TArray<FStageCameraKey> FallbackKeys(const FVector& Subject, float EyeZ, float FlySeconds);

	// The beat the opening is in, SecondsSinceStart after the session started (the lobby hold
	// included). Beats only ever advance: Flythrough, then Settle (config) or Turn and Speak
	// (built-in), then Live.
	static EStageBeat BeatAt(const FCompanionStagePlan& Plan, float SecondsSinceStart);

private:
	void LockPlayerView();
	// Holds a framing: position, level aim or look-at, lens and composition.
	void ApplyFraming(const FCompanionStageFraming& Framing);
	// The opening move MoveTime seconds in.
	void ApplyMoveAt(float MoveTime);
	void ApplyLens(float FocalMm, const FVector2D& FilmbackMm, float Aperture, const FVector2D& ShiftMm, float FocusCm);
	void SetResidentYawOffset(float Degrees);
	void SetExteriorLightsVisible(bool bVisible);
	void MarkCameraCut();
	void Speak(const FString& Line, const TCHAR* What);
	void FinishOpening();
	// The flagged cut to HOME an input asks for (design 3.2): her turn completed under the cut.
	void CutToHome(const TCHAR* Why);
	bool IsOnHome() const;
	void ApplyInput(EStageInput Input);
	EStagePhase CurrentPhase() const;
	float SecondsUntilGreeting() const;
	void TickLive();
	void Breathe();
	void HoldFraming(const FCompanionStageFraming& Framing, bool bCut);
	void BindViewerEvents();
	void HandleViewerArrived(const FString& PlayerId);
	void HandleViewersLeft();

	UPROPERTY(Transient) TObjectPtr<ACameraActor> Camera;
	UPROPERTY(Transient) TObjectPtr<AActor> Resident;
	TArray<TWeakObjectPtr<AActor>> ExteriorLights;

	FCompanionStagePlan Plan;

	bool bActive = false;
	EStageBeat Beat = EStageBeat::Flythrough;
	// Seconds since the session started (the opening's clock; the hold included).
	float Elapsed = 0.0f;
	// Seconds since BeginPlay, never reset: input, cut and punch-in times are on this clock.
	float Clock = 0.0f;
	bool bMoveStarted = false;
	bool bGreeted = false;
	bool bSpoken = false;
	bool bStaticShot = false;
	// Depth of field (MHLLM_STAGE_DOF, on by default for the data-driven shots).
	bool bDepthOfField = true;
	// +1 applies sensor shift with the stage design's (Blender's) sign; MHLLM_STAGE_SHIFT_SIGN=-1
	// flips it if a captured frame shows her eyes on the wrong side.
	float ShiftSign = 1.0f;

	// Sessions.
	EStageTrigger Trigger = EStageTrigger::Load;
	bool bAwaitingSession = false;
	bool bViewerConnected = false;
	int32 Sessions = 0;
	// The hold of the session in progress: the plan's, or 0.3 s when the visitor's own input is G.
	float ActiveHold = 3.0f;
	FDelegateHandle ViewerArrivedHandle;
	FDelegateHandle ViewersLeftHandle;

	// Input (design 3.2).
	TSharedPtr<FCompanionStageInputWatcher> InputWatcher;
	TArray<EStageInput> PendingInputs;
	float LastInputAt = 0.0f;
	// The turn-yield line is dropped: the visitor took the floor first.
	bool bDropLine = false;
	// A turn typed in the lobby, answered when the move lands.
	FString HeldTurn;
	// The turn-yield line as spoken, so its audio is never taken for a reply (no CLOSE on it).
	FString SpokenLine;
	// Esc during the move: the cut to HOME keeps every line; the turn-yield line comes this long after.
	float LineDueAt = -1.0f;

	// The framing held while live (HOME or CLOSE), and the CLOSE punch-in.
	FCompanionStageFraming Held;
	bool bInClose = false;
	float CloseAt = -1.0f;
	float CloseReturnAt = -1.0f;
	float LastCutAt = -1000.0f;
	float LiveSince = 0.0f;
	// Rotation-only breathing on the held shots (BreathingDegrees; MHLLM_STAGE_BREATHING=0 turns it off).
	float BreathDegrees = 0.0f;

	// While >0, the kiosk reset (MHLLM_OPENING_REPLAY): idle this long with no stream viewer.
	float ReplayEvery = 0.0f;

	float ResidentFinalYaw = 0.0f;

	// Resolved once at BeginPlay so the whole run agrees on the policy.
	bool bOpeningShot = false;
	bool bFreeLook = false;

	// Bounded retry for the player controller. 20 x 0.25s covers a slow map load without spinning
	// forever in a world that genuinely has no local player.
	int32 ViewLockAttempts = 0;
	static constexpr int32 MaxViewLockAttempts = 20;
};
