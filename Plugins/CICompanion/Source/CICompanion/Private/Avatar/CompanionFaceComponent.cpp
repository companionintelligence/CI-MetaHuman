#include "Avatar/CompanionFaceComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

DEFINE_LOG_CATEGORY_STATIC(LogCompanionFace, Log, All);

UCompanionFaceComponent::UCompanionFaceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	Profile = FFaceProfile::CharacterCreator();
}

void UCompanionFaceComponent::BeginPlay()
{
	Super::BeginPlay();

	// Fall back to whatever skeletal mesh the owner already has, so the component is useful when
	// simply added to an existing SkeletalMeshActor without further wiring.
	if (!TargetMesh && GetOwner())
	{
		SetTargetMesh(GetOwner()->FindComponentByClass<USkeletalMeshComponent>());
	}
}

void UCompanionFaceComponent::SetTargetMesh(USkeletalMeshComponent* InMesh)
{
	TargetMesh = InMesh;
	ResolveCurvesAgainstMesh();
}

void UCompanionFaceComponent::SetProfile(const FFaceProfile& InProfile)
{
	Profile = InProfile;
	ResolveCurvesAgainstMesh();
}

void UCompanionFaceComponent::ResolveCurvesAgainstMesh()
{
	ResolvedCurveCount = 0;
	if (!TargetMesh)
	{
		return;
	}

	const USkeletalMesh* Asset = TargetMesh->GetSkeletalMeshAsset();
	if (!Asset)
	{
		return;
	}

	TArray<FName> Missing;
	for (const FName& Curve : Profile.AllCurves())
	{
		if (Asset->FindMorphTarget(Curve))
		{
			++ResolvedCurveCount;
		}
		else
		{
			Missing.Add(Curve);
		}
	}

	// Loud, because the failure mode is otherwise invisible: SetMorphTarget on a name the mesh
	// does not have silently does nothing, and the character just stares back with a still face.
	if (Missing.Num() > 0)
	{
		TArray<FString> Names;
		Algo::Transform(Missing, Names, [](const FName& N) { return N.ToString(); });
		UE_LOG(LogCompanionFace, Warning,
			TEXT("Face profile '%s': %d/%d curves missing on '%s' -> %s"),
			*Profile.ProfileName.ToString(), Missing.Num(), Profile.AllCurves().Num(),
			*Asset->GetName(), *FString::Join(Names, TEXT(", ")));
	}
	else
	{
		UE_LOG(LogCompanionFace, Log, TEXT("Face profile '%s': all %d curves resolved on '%s'"),
			*Profile.ProfileName.ToString(), ResolvedCurveCount, *Asset->GetName());
	}
}

void UCompanionFaceComponent::ApplyVisemes(const TMap<EViseme, float>& Weights)
{
	TMap<FName, float> Curves;
	for (const TPair<EViseme, float>& Pair : Weights)
	{
		Profile.Accumulate(Pair.Key, Pair.Value, Curves);
	}
	ApplyCurves(Curves);
}

void UCompanionFaceComponent::ApplyCurves(const TMap<FName, float>& Curves)
{
	if (!TargetMesh)
	{
		return;
	}

	// Zero the profile's whole curve set first. Cheaper and far more robust than tracking which
	// curves were touched last frame, and it guarantees a shape cannot stick on.
	for (const FName& Curve : Profile.AllCurves())
	{
		TargetMesh->SetMorphTarget(Curve, 0.0f);
	}

	for (const TPair<FName, float>& Pair : Curves)
	{
		TargetMesh->SetMorphTarget(Pair.Key, FMath::Clamp(Pair.Value, 0.0f, 1.0f));
	}
}

void UCompanionFaceComponent::ClearFace()
{
	ApplyCurves(TMap<FName, float>());
}

void UCompanionFaceComponent::StartSelfTest(float SecondsPerViseme)
{
	SelfTestSecondsPerViseme = FMath::Max(0.05f, SecondsPerViseme);
	SelfTestElapsed = 0.0f;
	bSelfTesting = true;
}

void UCompanionFaceComponent::StopSelfTest()
{
	bSelfTesting = false;
	ClearFace();
}

void UCompanionFaceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bSelfTesting)
	{
		return;
	}

	SelfTestElapsed += DeltaTime;

	// Walk the vocabulary in order, looping. Each viseme eases in and out within its slot so a
	// captured frame is unlikely to land on a fully-closed mouth and read as "nothing happened".
	const int32 VisemeCount = static_cast<int32>(EViseme::OU) + 1;
	const float Cycle = SelfTestSecondsPerViseme * VisemeCount;
	const float Phase = FMath::Fmod(SelfTestElapsed, Cycle);
	const int32 Index = FMath::Clamp(static_cast<int32>(Phase / SelfTestSecondsPerViseme), 0, VisemeCount - 1);
	const float Local = (Phase - (Index * SelfTestSecondsPerViseme)) / SelfTestSecondsPerViseme;
	const float Weight = FMath::Sin(Local * PI);	// 0 -> 1 -> 0 across the slot

	TMap<EViseme, float> Active;
	Active.Add(static_cast<EViseme>(Index), Weight);
	ApplyVisemes(Active);
}
