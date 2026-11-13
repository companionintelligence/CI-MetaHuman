#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/Function.h"
#include "Avatar/CompanionStagePath.h"
#include "CompanionStageDirector.generated.h"

class ACameraActor;
class AActor;
class UCompanionStageSettings;

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

// Directs the stage camera: the lobby still, the opening move into the conversation framing, and
// the beats around it (the distance salutation, her look-up and turn, the turn-yield line).
//
// DATA-DRIVEN. Shots, lenses, composition and beat timings come from UCompanionStageSettings
// (Config/DefaultGame.ini), which Deployment/stage_layout.py writes from the same
// Deployment/stage_layout.json that author_diorama.py builds the map from. Resident-frame shots
// are solved at BeginPlay against her mark (the StageMark.Home TargetPoint) and her MEASURED eyes,
// so any human-scale resident gets the same framing. With no stage section in the config the
// director flies its built-in path instead (CompanionStage::FallbackKeys).
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

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UCompanionStageDirector, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return bActive; }

	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Stage")
	EStageBeat GetBeat() const { return Beat; }

	// Aborts the opening and hands straight to live conversation. Any real user input should call
	// this -- a scripted flourish must never make someone wait to be answered.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Stage")
	void SkipToLive();

	// Restarts the opening from the top. The shot otherwise plays ONCE, at level load, before any
	// viewer has connected -- so in a live stream the intended first impression is delivered to an
	// empty room and every real visitor arrives mid-scene.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Stage")
	void ReplayOpening();

	// The plan the director is playing (for logs and the automation specs).
	const FCompanionStagePlan& GetPlan() const { return Plan; }

	// The opening move plays on level load by default. Set MHLLM_OPENING_SHOT=0 to use the static
	// call framing; explicit 1/true/yes/on also enables the move.
	static bool ShouldPlayOpeningShot(TFunctionRef<FString(const TCHAR*)> Lookup);

	// Free-look is likewise opt-IN. Left on, the GameModeBase DefaultPawn's default movement
	// bindings turn every mouse move into camera yaw/pitch, so a shot that was framed to the
	// centimetre drifts off the subject as soon as anyone touches the mouse -- and over Pixel
	// Streaming, as soon as any VIEWER does.
	static bool ShouldAllowFreeLook(TFunctionRef<FString(const TCHAR*)> Lookup);

	// Process-environment overloads of the two above; the injectable form exists so the policy is
	// testable without mutating the real environment (see EnvConfigSpec for the same pattern).
	static bool ShouldPlayOpeningShot();
	static bool ShouldAllowFreeLook();

	// The built-in opening path, resolved against a subject (the fallback when the config has no
	// stage shots).
	static TArray<FStageCameraKey> FallbackKeys(const FVector& Subject, float EyeZ, float FlySeconds);

	// The beat the opening is in, SecondsSinceStart after BeginPlay (the lobby hold included). Beats
	// only ever advance: Flythrough, then Settle (config) or Turn and Speak (built-in), then Live.
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

	UPROPERTY(Transient) TObjectPtr<ACameraActor> Camera;
	UPROPERTY(Transient) TObjectPtr<AActor> Resident;
	TArray<TWeakObjectPtr<AActor>> ExteriorLights;

	FCompanionStagePlan Plan;

	bool bActive = false;
	EStageBeat Beat = EStageBeat::Flythrough;
	float Elapsed = 0.0f;
	bool bMoveStarted = false;
	bool bGreeted = false;
	bool bSpoken = false;
	bool bStaticShot = false;
	// Depth of field (MHLLM_STAGE_DOF, on by default for the data-driven shots).
	bool bDepthOfField = true;
	// +1 applies sensor shift with the stage design's (Blender's) sign; MHLLM_STAGE_SHIFT_SIGN=-1
	// flips it if a captured frame shows her eyes on the wrong side.
	float ShiftSign = 1.0f;

	// While >0, the opening loops with this gap once live. An attract loop: a kiosk or a demo
	// stream has no other way to show the opening to someone who arrives late.
	float ReplayEvery = 0.0f;
	float LiveSince = 0.0f;

	float ResidentFinalYaw = 0.0f;

	// Resolved once at BeginPlay so the whole run agrees on the policy.
	bool bOpeningShot = false;
	bool bFreeLook = false;

	// Bounded retry for the player controller. 20 x 0.25s covers a slow map load without spinning
	// forever in a world that genuinely has no local player.
	int32 ViewLockAttempts = 0;
	static constexpr int32 MaxViewLockAttempts = 20;
};
