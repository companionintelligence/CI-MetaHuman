#include "Avatar/TapeReelSpinner.h"

#include "CICompanionLog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

const FName UTapeReelSpinner::ReelTag(TEXT("TapeReel"));

bool UTapeReelSpinner::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UTapeReelSpinner::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	for (TActorIterator<AActor> It(&InWorld); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || !Actor->ActorHasTag(ReelTag))
		{
			continue;
		}

		// Seed speed and direction from world position: deterministic across runs (so a captured
		// frame is reproducible) but visually uncorrelated between neighbouring machines.
		const FVector P = Actor->GetActorLocation();
		const float Seed = FMath::Frac((P.X * 0.013f) + (P.Y * 0.029f));
		const float Speed = 90.0f + Seed * 150.0f;			// 90-240 deg/s
		const bool bReverse = FMath::Frac(P.Y * 0.017f) > 0.5f;

		Reels.Add({ Actor, bReverse ? -Speed : Speed });
	}

	UE_LOG(LogCompanionScene, Log, TEXT("Tape reels spinning: %d"), Reels.Num());
}

void UTapeReelSpinner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	for (int32 i = Reels.Num() - 1; i >= 0; --i)
	{
		AActor* Actor = Reels[i].Actor.Get();
		if (!Actor)
		{
			Reels.RemoveAtSwap(i);
			continue;
		}
		// Reels spin about their own axis. The GLB gives each reel its own origin, which is what
		// makes this a rotation rather than an orbit around the cabinet.
		FRotator R = Actor->GetActorRotation();
		R.Roll += Reels[i].DegreesPerSecond * DeltaTime;
		Actor->SetActorRotation(R);
	}
}
