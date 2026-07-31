#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
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

private:
	void BuildPath(const FVector& SubjectLocation, float EyeZ);
	void ApplyCameraAt(float TimeSeconds);
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
};
