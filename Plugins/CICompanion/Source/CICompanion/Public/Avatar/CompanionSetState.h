#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CompanionSetState.generated.h"

class UMaterialInstanceDynamic;

// What the visitor can see her doing (design 10.7): the ONE runtime state the set shows, so the room
// answers the conversation even on a resident with no face (rc1's mannequin). Published by
// UCompanionRuntimeSubsystem::GetPresence.
UENUM()
enum class ECompanionPresence : uint8
{
	Idle,
	Listening,	// the visitor has text in the box
	Thinking,	// a turn is with the model, no reply yet
	Speaking	// her voice is playing
};

// The set's responses, pure so the specs can pin them (design 3.1 "Set and light"). Nothing here
// changes room light, only practicals and machines, so the pinned exposure never pumps.
namespace CompanionSet
{
	// The amber INQUIRY lamp on the 1407 key panel: lit while the visitor types. The layout authors it
	// OFF (1.5 cd/m2); lit is this many times that (150 cd/m2, like the 729 lamps).
	constexpr float InquiryLitGain = 100.0f;
	CICOMPANION_API float InquiryLampScale(ECompanionPresence Presence);

	// The reacting tape unit's reels (tag StageReact.Tape), in degrees a second, Seconds into the state:
	// thinking runs the tape-search pattern (0.8 s forward, 0.3 s stop, 0.4 s back, 0.1 s stop, every
	// 1.6 s); speaking reads steadily at 0.5 rev/s; listening holds still; idle creeps at a quarter
	// of the reel's own rate.
	CICOMPANION_API float TapeDegreesPerSecond(ECompanionPresence Presence, float Seconds, float OwnRate);

	// The wall clock's hands for a time of day, in degrees clockwise from 12 (host time: the clock
	// is the stream's proof of life).
	CICOMPANION_API void ClockAngles(int32 Hour, int32 Minute, float Second, float& OutHourDeg, float& OutMinuteDeg);
}

// Drives the tagged set pieces from her presence. The map is authored headlessly and cannot carry
// Blueprints (HANDOFF.md Lesson 6), so like the reels and the face this is a world subsystem that
// finds its actors by tag at BeginPlay:
//   InquiryLamp          the lamp's "Emissive" material parameter (a dynamic instance of its material)
//   WallClock            the dial; its hands are WallClock.Hour and WallClock.Minute (Movable),
//                        turned about the dial's centre to host time
// The reacting tape unit's reels (StageReact.Tape) are turned by UTapeReelSpinner, which reads the same presence.
UCLASS()
class CICOMPANION_API UCompanionSetStateDisplay : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static const FName InquiryLampTag;
	static const FName WallClockTag;
	static const FName ClockHourTag;
	static const FName ClockMinuteTag;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UCompanionSetStateDisplay, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return bReady; }

	ECompanionPresence GetPresence() const { return Presence; }

private:
	struct FLamp
	{
		TWeakObjectPtr<UMaterialInstanceDynamic> Material;
		float Off = 0.0f;
		float Level = 0.0f;
	};
	struct FHand
	{
		TWeakObjectPtr<AActor> Actor;
		FVector RestLocation = FVector::ZeroVector;
		FQuat RestRotation = FQuat::Identity;
		float RestDegrees = 0.0f;
		bool bHour = false;
	};
	void UpdateClock();

	TArray<FLamp> Lamps;
	TArray<FHand> Hands;
	FVector ClockCentre = FVector::ZeroVector;
	FVector ClockAxis = FVector::XAxisVector;
	FVector ClockUp = FVector::ZAxisVector;
	FVector ClockRight = FVector::YAxisVector;
	ECompanionPresence Presence = ECompanionPresence::Idle;
	bool bReady = false;
	float ClockTimer = 0.0f;
};
