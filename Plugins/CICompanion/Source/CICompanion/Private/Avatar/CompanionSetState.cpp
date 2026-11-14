#include "Avatar/CompanionSetState.h"

#include "Avatar/CompanionRuntimeSubsystem.h"
#include "CICompanionLog.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/DateTime.h"

const FName UCompanionSetStateDisplay::InquiryLampTag(TEXT("InquiryLamp"));
const FName UCompanionSetStateDisplay::WallClockTag(TEXT("WallClock"));
const FName UCompanionSetStateDisplay::ClockHourTag(TEXT("WallClock.Hour"));
const FName UCompanionSetStateDisplay::ClockMinuteTag(TEXT("WallClock.Minute"));

namespace
{
	const FName SetStateEmissiveParam(TEXT("Emissive"));
}

float CompanionSet::InquiryLampScale(ECompanionPresence Presence)
{
	return Presence == ECompanionPresence::Listening ? InquiryLitGain : 1.0f;
}

float CompanionSet::TapeDegreesPerSecond(ECompanionPresence Presence, float Seconds, float OwnRate)
{
	switch (Presence)
	{
	case ECompanionPresence::Thinking:
	{
		// The tape-search pattern, every 1.6 s: forward, stop, back, stop.
		const float Phase = FMath::Fmod(FMath::Max(Seconds, 0.0f), 1.6f);
		const float Search = 240.0f;
		if (Phase < 0.8f) { return Search; }
		if (Phase < 1.1f) { return 0.0f; }
		if (Phase < 1.5f) { return -Search; }
		return 0.0f;
	}
	case ECompanionPresence::Speaking:
		return 180.0f;			// reads at 0.5 rev/s
	case ECompanionPresence::Listening:
		return 0.0f;			// waits on her
	default:
		return 0.25f * OwnRate;	// creeps
	}
}

void CompanionSet::ClockAngles(int32 Hour, int32 Minute, float Second, float& OutHourDeg, float& OutMinuteDeg)
{
	const float M = float(((Minute % 60) + 60) % 60) + FMath::Clamp(Second, 0.0f, 60.0f) / 60.0f;
	OutMinuteDeg = M * 6.0f;
	OutHourDeg = (float(((Hour % 12) + 12) % 12) + M / 60.0f) * 30.0f;
}

bool UCompanionSetStateDisplay::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UCompanionSetStateDisplay::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	AActor* Clock = nullptr;
	TArray<AActor*> HandActors;
	for (TActorIterator<AActor> It(&InWorld); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}
		if (Actor->ActorHasTag(InquiryLampTag))
		{
			if (UStaticMeshComponent* Mesh = Actor->FindComponentByClass<UStaticMeshComponent>())
			{
				if (UMaterialInstanceDynamic* Mid = Mesh->CreateAndSetMaterialInstanceDynamic(0))
				{
					FLamp Lamp;
					Lamp.Material = Mid;
					float Off = 0.0f;
					Mid->GetScalarParameterValue(FHashedMaterialParameterInfo(SetStateEmissiveParam), Off);
					Lamp.Off = Off;
					Lamp.Level = Off;
					Lamps.Add(Lamp);
				}
			}
		}
		if (!Clock && Actor->ActorHasTag(WallClockTag))
		{
			Clock = Actor;
		}
		if (Actor->ActorHasTag(ClockHourTag) || Actor->ActorHasTag(ClockMinuteTag))
		{
			HandActors.Add(Actor);
		}
	}

	if (Clock && HandActors.Num() > 0)
	{
		// The dial is a cylinder blockout: its face normal is the mesh's local Z (author_diorama.py
		// rolls cylinders onto their axis). The hands stand in front of the dial, so the viewer looks
		// along the normal from their side; 12 o'clock is world up, 3 o'clock the viewer's right.
		ClockCentre = Clock->GetActorLocation();
		FVector Normal = Clock->GetActorQuat().RotateVector(FVector::ZAxisVector).GetSafeNormal();
		FVector Toward = FVector::ZeroVector;
		for (const AActor* Hand : HandActors)
		{
			Toward += Hand->GetActorLocation() - ClockCentre;
		}
		// The viewing direction runs from the hands into the dial.
		ClockAxis = FVector::DotProduct(Normal, Toward) > 0.0f ? -Normal : Normal;
		ClockUp = FVector::VectorPlaneProject(FVector::UpVector, ClockAxis).GetSafeNormal();
		// Unreal is left-handed: looking along +X with +Z up, +Y is to the right, i.e. Up x Forward.
		ClockRight = FVector::CrossProduct(ClockUp, ClockAxis).GetSafeNormal();
		for (AActor* HandActor : HandActors)
		{
			FHand Hand;
			Hand.Actor = HandActor;
			Hand.bHour = HandActor->ActorHasTag(ClockHourTag);
			Hand.RestLocation = HandActor->GetActorLocation();
			Hand.RestRotation = HandActor->GetActorQuat();
			// Its pointing direction: the longest of its local axes, signed away from the centre.
			const FVector Scale = HandActor->GetActorScale3D().GetAbs();
			const FVector LocalLong = (Scale.Y >= Scale.Z && Scale.Y >= Scale.X) ? FVector::YAxisVector
				: (Scale.Z >= Scale.X ? FVector::ZAxisVector : FVector::XAxisVector);
			FVector Dir = Hand.RestRotation.RotateVector(LocalLong);
			if (FVector::DotProduct(Dir, Hand.RestLocation - ClockCentre) < 0.0f)
			{
				Dir = -Dir;
			}
			Hand.RestDegrees = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Dir, ClockRight), FVector::DotProduct(Dir, ClockUp)));
			Hands.Add(Hand);
		}
	}

	bReady = Lamps.Num() > 0 || Hands.Num() > 0;
	UE_LOG(LogCompanionScene, Log, TEXT("Set as a state display: %d INQUIRY lamp(s), %d clock hand(s) on host time%s."),
		Lamps.Num(), Hands.Num(), Clock ? TEXT("") : TEXT(" (no WallClock dial)"));
	if (bReady)
	{
		UpdateClock();
	}
}

void UCompanionSetStateDisplay::UpdateClock()
{
	if (Hands.Num() == 0)
	{
		return;
	}
	const FDateTime Now = FDateTime::Now();
	float HourDeg = 0.0f;
	float MinuteDeg = 0.0f;
	CompanionSet::ClockAngles(Now.GetHour(), Now.GetMinute(), float(Now.GetSecond()) + Now.GetMillisecond() / 1000.0f, HourDeg, MinuteDeg);
	for (const FHand& Hand : Hands)
	{
		AActor* Actor = Hand.Actor.Get();
		if (!Actor)
		{
			continue;
		}
		const float Delta = (Hand.bHour ? HourDeg : MinuteDeg) - Hand.RestDegrees;
		// Clockwise as the viewer sees it, looking along ClockAxis: Up turns toward Right.
		const FQuat Turn(ClockAxis, -FMath::DegreesToRadians(Delta));
		const FVector Location = ClockCentre + Turn.RotateVector(Hand.RestLocation - ClockCentre);
		Actor->SetActorLocationAndRotation(Location, Turn * Hand.RestRotation);
	}
}

void UCompanionSetStateDisplay::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (const UWorld* World = GetWorld())
	{
		if (const UCompanionRuntimeSubsystem* Runtime = World->GetSubsystem<UCompanionRuntimeSubsystem>())
		{
			Presence = Runtime->GetPresence();
		}
	}

	// A lamp filament: about 60 ms to light or go dark.
	const float Blend = 1.0f - FMath::Exp(-FMath::Max(DeltaTime, 0.0f) / 0.06f);
	for (FLamp& Lamp : Lamps)
	{
		if (UMaterialInstanceDynamic* Mid = Lamp.Material.Get())
		{
			const float Target = Lamp.Off * CompanionSet::InquiryLampScale(Presence);
			Lamp.Level += (Target - Lamp.Level) * Blend;
			Mid->SetScalarParameterValue(SetStateEmissiveParam, Lamp.Level);
		}
	}

	// The hands move once a second; a sweep second hand would be a different clock.
	ClockTimer += DeltaTime;
	if (ClockTimer >= 1.0f)
	{
		ClockTimer = 0.0f;
		UpdateClock();
	}
}
