#include "Avatar/TapeReelSpinner.h"

#include "Avatar/CompanionRuntimeSubsystem.h"
#include "Avatar/CompanionSetState.h"
#include "CICompanionLog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

const FName UTapeReelSpinner::ReelTag(TEXT("TapeReel"));
static const FName TapeReactTag(TEXT("StageReact.Tape"));

bool UTapeReelSpinner::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

FVector UTapeReelSpinner::AxleOf(const FQuat& Base)
{
	// Only the yaw: a pitch increment on a rotator is a turn about the YAWED Y axis whatever the
	// roll, which is the axle the reels were authored around.
	const double Yaw = Base.Rotator().Yaw;
	return FRotator(0.0, Yaw, 0.0).RotateVector(FVector::YAxisVector);
}

FQuat UTapeReelSpinner::SpunRotation(const FQuat& Base, double AngleDegrees)
{
	return FQuat(AxleOf(Base), FMath::DegreesToRadians(AngleDegrees)) * Base;
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

		FReel Reel;
		Reel.Actor = Actor;
		Reel.DegreesPerSecond = bReverse ? -Speed : Speed;
		Reel.BaseQuat = Actor->GetActorQuat();
		Reel.bReactive = Actor->ActorHasTag(TapeReactTag);
		Reels.Add(Reel);
	}

	UE_LOG(LogCompanionScene, Log, TEXT("Tape reels spinning: %d (about their own axles, from their rest rotation)"), Reels.Num());
}

void UTapeReelSpinner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	RunSeconds += DeltaTime;
	// The tape unit tagged StageReact.Tape answers the conversation (design 10.7; in the 1979 room the
	// 3420 behind her left shoulder in HOME): it searches while she thinks and reads while she speaks.
	// Every other reel keeps its own job.
	ECompanionPresence Presence = ECompanionPresence::Idle;
	if (const UWorld* World = GetWorld())
	{
		if (const UCompanionRuntimeSubsystem* Runtime = World->GetSubsystem<UCompanionRuntimeSubsystem>())
		{
			Presence = Runtime->GetPresence();
		}
	}
	if ((uint8)Presence != LastPresence)
	{
		LastPresence = (uint8)Presence;
		PresenceSince = RunSeconds;
	}
	for (int32 i = Reels.Num() - 1; i >= 0; --i)
	{
		FReel& Reel = Reels[i];
		AActor* Actor = Reel.Actor.Get();
		if (!Actor)
		{
			Reels.RemoveAtSwap(i);
			continue;
		}
		// Accumulated HERE, never read back from the actor (see the header: the read-back rotator is
		// normalised and stalls at 90 degrees of pitch). Wrapped so a kiosk left running for days
		// keeps its precision.
		const float Rate = Reel.bReactive
			? CompanionSet::TapeDegreesPerSecond(Presence, float(RunSeconds - PresenceSince), Reel.DegreesPerSecond)
			: Reel.DegreesPerSecond;
		Reel.AngleDegrees = FMath::Fmod(Reel.AngleDegrees + Rate * DeltaTime, 360.0);
		Actor->SetActorRotation(SpunRotation(Reel.BaseQuat, Reel.AngleDegrees));
	}

	// One line a minute at most: proof in the log that the reels are still turning past a quarter turn.
	if (Reels.Num() > 0 && RunSeconds - LoggedAtSeconds >= 60.0)
	{
		LoggedAtSeconds = RunSeconds;
		const FReel& First = Reels[0];
		UE_LOG(LogCompanionScene, Verbose, TEXT("Tape reels: %d turning; the first is %.0f deg into its turn at %.0f deg/s."),
			Reels.Num(), First.AngleDegrees, First.DegreesPerSecond);
	}
}
