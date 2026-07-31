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

private:
	struct FReel
	{
		TWeakObjectPtr<AActor> Actor;
		float DegreesPerSecond = 0.0f;
	};
	TArray<FReel> Reels;
};
