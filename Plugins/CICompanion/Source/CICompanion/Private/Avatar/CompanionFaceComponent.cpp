#include "Avatar/CompanionFaceComponent.h"
#include "CICompanionLog.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"


UCompanionFaceComponent::UCompanionFaceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	Profile = FFaceProfile::CharacterCreator();
	// Resolved here rather than at BeginPlay so the switch is observable on a component that was
	// never registered into a world — which is how the tests reach it.
	bEmotionEnabled = FCompanionEmotionState::IsEnabled();
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

	if (!bEmotionEnabled)
	{
		// Logged, because a still brow is not obviously a CHOICE when you are looking at a frame.
		UE_LOG(LogCompanionFace, Log, TEXT("Facial emotion disabled by MHLLM_EMOTION=0."));
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
	// Recorded here rather than at the call sites so every path — tick, ClearFace, ApplyVisemes —
	// agrees on whether the mesh currently holds anything that will need clearing.
	bFaceWritten = Curves.Num() > 0;

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
	// The EXPRESSION SURVIVES a face clear. ClearFace means "stop whatever the mouth was doing",
	// and the emotional bias is not part of that — it outlives any one utterance by design. Wiping
	// it here would make every StopSpeaking() punch a one-frame hole in her expression. Anything
	// that genuinely wants a blank face calls ResetEmotion() as well.
	TMap<FName, float> Curves;
	Emotion.AccumulateInto(Profile, Curves);
	ApplyCurves(Curves);
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

TMap<FName, float> UCompanionFaceComponent::ComposeFrame(float DeltaTime)
{
	TMap<FName, float> Curves;

	// Blink is involuntary and composites over whatever the mouth is doing, so it is computed
	// every frame regardless of speech or self-test state.
	const float Blink = TickBlink(DeltaTime);

	// So is emotion, and for the same reason — but on its own clock. It is advanced BEFORE the
	// speech branch and unconditionally, because an expression is not owned by an utterance: it
	// has to keep blending in while she starts talking and keep decaying after she stops.
	Emotion.Advance(DeltaTime);

	// LAYER 1: the mouth. Speech wins over the self-test — once there is real dialogue to say, the
	// diagnostic loop should get out of the way rather than fight it for the same curves.
	if (bSpeaking)
	{
		SpeechElapsed += DeltaTime;

		if (SpeechElapsed >= FVisemePlanner::TrackDuration(SpeechTrack))
		{
			// Ends the utterance and arms the emotion decay. Checked before sampling so the final
			// frame is not a stale viseme held one tick past the end of the track.
			EndSpeech();
		}
		else
		{
			for (const TPair<EViseme, float>& Pair : FVisemePlanner::SampleAt(SpeechTrack, SpeechElapsed))
			{
				Profile.Accumulate(Pair.Key, Pair.Value, Curves);
			}
		}
	}
	else if (bSelfTesting)
	{
		SelfTestElapsed += DeltaTime;

		// Walk the vocabulary in order, looping. Each viseme eases in and out within its slot so a
		// captured frame is unlikely to land on a fully-closed mouth and read as "nothing happened".
		const int32 VisemeCount = static_cast<int32>(EViseme::OU) + 1;
		const float Cycle = SelfTestSecondsPerViseme * VisemeCount;
		const float Phase = FMath::Fmod(SelfTestElapsed, Cycle);
		const int32 Index = FMath::Clamp(static_cast<int32>(Phase / SelfTestSecondsPerViseme), 0, VisemeCount - 1);
		const float Local = (Phase - (Index * SelfTestSecondsPerViseme)) / SelfTestSecondsPerViseme;
		const float Weight = FMath::Sin(Local * PI);	// 0 -> 1 -> 0 across the slot

		Profile.Accumulate(static_cast<EViseme>(Index), Weight, Curves);
	}

	// LAYER 2: the eyelids, over whatever the mouth is doing.
	if (Blink > 0.0f)
	{
		for (const FName& BlinkCurve : Profile.BlinkCurves)
		{
			Curves.Add(BlinkCurve, Blink);
		}
	}

	// LAYER 3: the expression, ON TOP and LAST.
	//
	// Order is load-bearing in both directions. It must come after the visemes because
	// FFaceProfile::Accumulate clamps as it sums, so an emotion accumulated first could be clipped
	// away by the speech piled on afterwards. And it must ADD rather than replace, because the two
	// are answering different questions: the visemes say what shape the mouth is passing through
	// right now, the expression says what she means by the sentence. Choosing between them gives
	// you a face that can either talk or feel.
	if (bEmotionEnabled)
	{
		Emotion.AccumulateInto(Profile, Curves);
	}

	return Curves;
}

void UCompanionFaceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const TMap<FName, float> Curves = ComposeFrame(DeltaTime);

	// Skip the write when there is nothing to show AND nothing was shown last frame — ApplyCurves
	// zeroes the profile's entire curve set, which is ~20 SetMorphTarget calls to accomplish
	// nothing. The bFaceWritten latch is what still guarantees the ONE zeroing pass that clears the
	// last expression on the way into idle.
	if (Curves.Num() > 0 || bFaceWritten)
	{
		ApplyCurves(Curves);
	}
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
	EndSpeech();
	ClearFace();
}

void UCompanionFaceComponent::EndSpeech()
{
	bSpeaking = false;
	SpeechTrack.Reset();
	SpeechElapsed = 0.0f;

	// The utterance is over, so the expression that belonged to it starts letting go — held for a
	// beat first, then blended out. This is what stops an expression sticking on for the session:
	// nothing else ever tells the face the reply is finished.
	Emotion.Release();
}

void UCompanionFaceComponent::SetEmotion(EEmotionType InEmotion, float Intensity)
{
	if (!bEmotionEnabled)
	{
		return;
	}
	Emotion.Set(InEmotion, Intensity);
}

void UCompanionFaceComponent::ReleaseEmotion(float HoldSeconds)
{
	Emotion.Release(HoldSeconds);
}

void UCompanionFaceComponent::ResetEmotion()
{
	Emotion.Reset();
}

void UCompanionFaceComponent::SetEmotionEnabled(bool bEnabled)
{
	bEmotionEnabled = bEnabled;
	if (!bEnabled)
	{
		// Dropped immediately rather than faded: a subsystem that has been switched off must not
		// keep animating for another half second, and whatever it last wrote must not stay latched
		// on the mesh. The next tick's zeroing pass finishes the job.
		Emotion.Reset();
	}
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
