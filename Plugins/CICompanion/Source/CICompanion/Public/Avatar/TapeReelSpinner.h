#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TapeReelSpinner.generated.h"

// Turns the tape reels in the machine hall.
//
// Same pattern as the face driver and for the same reason: the map is authored headlessly by
// Python in a content-only project that cannot reference plugin C++ (HANDOFF.md Lesson 6), so the
// reels are placed as plain StaticMeshActors carrying a "TapeReel" tag and the behaviour is bolted
// on here at BeginPlay.
//
// Rates are deliberately NOT uniform. A hall of reels turning in lockstep reads as a screensaver;
// real machines run independent jobs, so each reel gets its own speed and direction seeded from
// its position. That variation is the entire difference between "animated" and "alive".
//
// THE AXLE. A reel turns about its yawed local Y axis (the reel disc lies in the cabinet's local XZ
// plane; author_diorama.py rolls the cylinder blockouts so theirs lies the same way). Each tick sets
//     FQuat(Axle, Angle) * BaseQuat
// from the reel's BeginPlay rotation and an angle accumulated here. It used to read the actor's
// rotation back, add to Pitch and write it again, which can never pass 90 degrees: Unreal hands the
// rotation back as a rotator NORMALISED from the component's quaternion, with pitch in [-90, 90], so
// every reel crept to a quarter turn and stopped there. Never round-trip a spin through
// FRotator::Pitch.
UCLASS()
class CICOMPANION_API UTapeReelSpinner : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static const FName ReelTag;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UTapeReelSpinner, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return Reels.Num() > 0; }

	// The world rotation of a reel AngleDegrees into its spin, from its rotation at rest: a turn of
	// AngleDegrees about its axle (the yawed Y axis of Base). Pure, so the specs can turn a reel past
	// the quarter turn the old rotator arithmetic stalled at.
	static FQuat SpunRotation(const FQuat& Base, double AngleDegrees);

	// The reel's axle in world space: Base's yaw applied to +Y.
	static FVector AxleOf(const FQuat& Base);

	// Degrees turned so far by the first reel (for the specs and the log; -1 with no reels).
	double GetFirstReelAngle() const { return Reels.Num() > 0 ? Reels[0].AngleDegrees : -1.0; }

private:
	struct FReel
	{
		TWeakObjectPtr<AActor> Actor;
		float DegreesPerSecond = 0.0f;
		FQuat BaseQuat = FQuat::Identity;
		double AngleDegrees = 0.0;
	};
	TArray<FReel> Reels;
	double LoggedAtSeconds = 0.0;
	double RunSeconds = 0.0;
};
