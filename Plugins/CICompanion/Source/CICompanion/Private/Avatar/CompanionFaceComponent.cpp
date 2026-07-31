#include "Avatar/CompanionFaceComponent.h"
#include "CICompanionLog.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"


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

	// Blink is involuntary and composites over whatever the mouth is doing, so it is computed
	// every frame regardless of speech or self-test state.
	const float Blink = TickBlink(DeltaTime);

	// Speech wins over the self-test: once there is real dialogue to say, the diagnostic loop
	// should get out of the way rather than fight it for the same curves.
	if (bSpeaking)
	{
		SpeechElapsed += DeltaTime;

		TMap<EViseme, float> Active = FVisemePlanner::SampleAt(SpeechTrack, SpeechElapsed);
		TMap<FName, float> Curves;
		for (const TPair<EViseme, float>& Pair : Active)
		{
			Profile.Accumulate(Pair.Key, Pair.Value, Curves);
		}
		for (const FName& BlinkCurve : Profile.BlinkCurves)
		{
			Curves.Add(BlinkCurve, Blink);
		}
		ApplyCurves(Curves);

		if (SpeechElapsed >= FVisemePlanner::TrackDuration(SpeechTrack))
		{
			StopSpeaking();
		}
		return;
	}

	if (!bSelfTesting)
	{
		// Idle: nothing to say, but keep blinking so the face is not a waxwork.
		if (Blink > 0.0f)
		{
			TMap<FName, float> Curves;
			for (const FName& BlinkCurve : Profile.BlinkCurves)
			{
				Curves.Add(BlinkCurve, Blink);
			}
			ApplyCurves(Curves);
		}
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

void UCompanionFaceComponent::PlayVisemeTrack(const TArray<FVisemeSpan>& Track)
{
	// A new utterance replaces the old one outright. Blending two tracks would let a stale reply
	// keep moving the mouth under a fresh one, which reads as a glitch rather than as speech.
	SpeechTrack = Track;
	SpeechElapsed = 0.0f;
	bSpeaking = Track.Num() > 0;
	if (bSpeaking)
	{
		bSelfTesting = false;	// real speech supersedes the diagnostic loop
	}
}

void UCompanionFaceComponent::StopSpeaking()
{
	if (!bSpeaking)
	{
		return;
	}
	bSpeaking = false;
	SpeechTrack.Reset();
	SpeechElapsed = 0.0f;
	ClearFace();
}

void UCompanionFaceComponent::SetBlinkEnabled(bool bEnabled)
{
	bBlinkEnabled = bEnabled;
	if (!bEnabled)
	{
		BlinkProgress = -1.0f;
	}
}

float UCompanionFaceComponent::TickBlink(float DeltaTime)
{
	if (!bBlinkEnabled || Profile.BlinkCurves.Num() == 0)
	{
		return 0.0f;
	}

	// Mid-blink: a fast close and a slightly slower open, roughly 120ms end to end. Symmetric
	// linear motion looks mechanical, so the two halves differ.
	if (BlinkProgress >= 0.0f)
	{
		constexpr float BlinkSeconds = 0.12f;
		BlinkProgress += DeltaTime / BlinkSeconds;
		if (BlinkProgress >= 1.0f)
		{
			BlinkProgress = -1.0f;
			return 0.0f;
		}
		return BlinkProgress < 0.4f
			? FMath::Clamp(BlinkProgress / 0.4f, 0.0f, 1.0f)			// closing
			: FMath::Clamp(1.0f - ((BlinkProgress - 0.4f) / 0.6f), 0.0f, 1.0f);	// opening
	}

	// Between blinks. Humans blink irregularly every 2-6s; a fixed interval is immediately
	// legible as a machine, so the next gap is randomised each time.
	BlinkTimer += DeltaTime;
	if (BlinkTimer >= NextBlinkIn)
	{
		BlinkTimer = 0.0f;
		NextBlinkIn = FMath::FRandRange(2.0f, 6.0f);
		BlinkProgress = 0.0f;
	}
	return 0.0f;
}
