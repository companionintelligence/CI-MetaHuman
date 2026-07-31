#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/Function.h"
#include "CompanionStageDirector.generated.h"

class ACameraActor;
class AActor;

// One keyframe of the opening move: where the camera is, and what it is aimed at.
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
	Flythrough,	// camera sweeps the hall
	Settle,		// camera arrives at the call framing, behind her
	Turn,		// she rotates to face the lens
	Speak,		// scripted opening line
	Live		// handed over to normal conversation
};

// Directs the opening shot: a camera move through the machine hall that settles behind the
// resident, who then turns to camera and speaks.
//
// Driven from C++ rather than a LevelSequence for the same reason the face driver is: the map is
// authored headlessly by Python in a content-only project that cannot reference plugin C++, embed
// a Blueprint, or carry a Sequencer asset (HANDOFF.md Lesson 6). A tickable subsystem is the only
// place this can live and still survive the cook.
//
// It also owns its own camera and sets it as the view target, which deliberately supersedes the
// authored StageCamera's auto-activation for the duration of the move.
UCLASS()
class CICOMPANION_API UCompanionStageDirector : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
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

	// The shot is STATIC unless the opening move is explicitly asked for.
	//
	// It used to be the other way round -- the flythrough ran unless MHLLM_OPENING_SHOT was set to
	// 0 -- which meant every plain `run the app` began with an eleven-second camera move nobody
	// asked for, and the only way to get the framing the set was solved for was to know an
	// undocumented env var. A camera that moves is the special case, so it is the one that has to
	// be requested. Accepts 1/true/yes; anything else, including unset, is static.
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

private:
	void BuildPath(const FVector& SubjectLocation, float EyeZ);
	void ApplyCameraAt(float TimeSeconds);
	// Parks the camera on the LAST key -- the framing the set was solved for. Independent of
	// FlyDuration, which is zero in the static case and would make ApplyCameraAt sample key 0.
	void ApplyFinalFraming();
	// Points the player at the stage camera and, unless free-look was asked for, stops the pawn
	// from acting on look/move input. Retries briefly: the player controller is not guaranteed to
	// exist by the time a world subsystem's BeginPlay runs.
	void LockPlayerView();
	static FVector SampleSpline(const TArray<FStageCameraKey>& Keys, float T, bool bLookAt);

	UPROPERTY(Transient) TObjectPtr<ACameraActor> Camera;
	UPROPERTY(Transient) TObjectPtr<AActor> Resident;

	TArray<FStageCameraKey> Path;

	bool bActive = false;
	EStageBeat Beat = EStageBeat::Flythrough;
	float Elapsed = 0.0f;

	// Beat boundaries, seconds from level start.
	float FlyDuration = 11.0f;
	float TurnDuration = 1.6f;
	float SpeakDelay = 0.35f;	// beat of stillness after the turn before she speaks
	// Held before the shot begins, so a viewer connecting at startup is present for it rather than
	// joining after the camera has already landed.
	float StartDelay = 3.0f;
	// While >0, the opening loops with this gap once live. An attract loop: a kiosk or a demo
	// stream has no other way to show the opening to someone who arrives late.
	float ReplayEvery = 0.0f;
	float LiveSince = 0.0f;

	float ResidentStartYaw = 0.0f;
	float ResidentFinalYaw = 0.0f;
	bool bSpoken = false;

	// Resolved once at BeginPlay so the whole run agrees on the policy.
	bool bOpeningShot = false;
	bool bFreeLook = false;

	// Bounded retry for the player controller. 20 x 0.25s covers a slow map load without spinning
	// forever in a world that genuinely has no local player.
	int32 ViewLockAttempts = 0;
	static constexpr int32 MaxViewLockAttempts = 20;
};
