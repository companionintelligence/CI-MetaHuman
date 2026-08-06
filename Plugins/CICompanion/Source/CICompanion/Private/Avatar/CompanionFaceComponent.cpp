#include "Avatar/CompanionFaceComponent.h"
#include "CICompanionLog.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"


UCompanionFaceComponent::UCompanionFaceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	Profile = FFaceProfile::CharacterCreator();
	CachedAllCurves = Profile.AllCurves();
	// Resolved here rather than at BeginPlay so the switch is observable on a component that was
	// never registered into a world — which is how the tests reach it.
	bEmotionEnabled = FCompanionEmotionState::IsEnabled();
	bGazeEnabled = FCompanionGazeState::IsEnabled();
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

	if (!bGazeEnabled)
	{
		// Same reasoning: a dead stare is indistinguishable from a broken gaze driver in a frame
		// grab, and this project has already lost time to a back-to-camera pose that survived weeks
		// of "verified" (TESTING.md §5.2).
		UE_LOG(LogCompanionFace, Log, TEXT("Gaze disabled by MHLLM_GAZE=0."));
	}
	else if (Profile.GazeCurves.Num() < 8)
	{
		UE_LOG(LogCompanionFace, Warning,
			TEXT("Face profile '%s' declares %d gaze curves; 8 are required, so the eyes will not move."),
			*Profile.ProfileName.ToString(), Profile.GazeCurves.Num());
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
	// The one place the zeroing set can change, so the one place it is rebuilt.
	CachedAllCurves = Profile.AllCurves();
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
	for (const FName& Curve : CachedAllCurves)
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
			*Profile.ProfileName.ToString(), Missing.Num(), CachedAllCurves.Num(),
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
	for (const FName& Curve : CachedAllCurves)
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
	//
	// ADDITIVE, like every other layer. This was an overwriting Add() and happened to be correct
	// only because no viseme in either shipped profile touches a lid curve — so the contract and the
	// behaviour agreed by luck rather than by construction, and the first profile to disagree would
	// have silently discarded the other contribution.
	if (Blink > 0.0f)
	{
		for (const FName& BlinkCurve : Profile.BlinkCurves)
		{
			Curves.FindOrAdd(BlinkCurve, 0.0f) += Blink;
		}
	}

	// LAYER 3: the eyes. Advanced unconditionally and BEFORE the enabled check, for the same reason
	// emotion is: the gaze clock belongs to the character, not to any one utterance, and a channel
	// that only ticks while it is being rendered would jump the moment it was switched back on.
	//
	// Ordering against the other layers is not load-bearing here — the profile keeps the eight look
	// curves disjoint from every viseme, blink and emotion curve, so nothing can clobber anything.
	// It sits with blink because both are involuntary and neither is chosen by the sentence.
	Gaze.Advance(DeltaTime);
	if (bGazeEnabled)
	{
		Gaze.AccumulateInto(Profile, Curves);
	}

	// LAYER 4: the expression, ON TOP and LAST.
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

		// ...and then the one place two layers genuinely contradict each other, resolved in favour
		// of the blink. Fear and Alert hold the eyes WIDE; a blink shuts them. Both were being
		// written, so during those expressions the lid closed against a widener holding it open and
		// she read as though she had stopped blinking — the precise "corpse" tell that the blink
		// clock exists to avoid, reintroduced by the expression system.
		//
		// Damped rather than dropped: a blink is a 120ms transient, so scaling by (1 - Blink) lets
		// the expression return to full strength the instant the lid reopens. Zeroing it outright
		// would make the brow visibly flinch on every blink.
		if (Blink > 0.0f)
		{
			for (const FName& WidenCurve : Profile.EyeWidenCurves)
			{
				if (float* Value = Curves.Find(WidenCurve))
				{
					*Value *= (1.0f - Blink);
				}
			}
		}
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

void UCompanionFaceComponent::PlayVisemeTrack(const TArray<FVisemeSpan>& Track, float StartOffsetSeconds)
{
	// A new utterance replaces the old one outright. Blending two tracks would let a stale reply
	// keep moving the mouth under a fresh one, which reads as a glitch rather than as speech.
	SpeechTrack = Track;
	SpeechElapsed = FMath::Max(0.0f, StartOffsetSeconds);
	// An offset past the end means the utterance is already over — say nothing rather than replay
	// the tail of it against silence.
	bSpeaking = Track.Num() > 0 && SpeechElapsed < FVisemePlanner::TrackDuration(Track);
	if (bSpeaking)
	{
		bSelfTesting = false;	// real speech supersedes the diagnostic loop

		// Speakers break eye contact markedly more than listeners do. This component is the only
		// thing that knows when an utterance actually STARTS and ENDS — the runtime subsystem hands
		// off a track and never hears about it again — so the speaking/listening half of the gaze
		// policy is owned here rather than pushed in from outside and inevitably left stale.
		Gaze.SetMode(FCompanionGazeState::EGazeMode::Speaking);

		// Now that the utterance's real length is known, extend the expression's lease to cover it.
		// The emotion is set once per reply against a 12s backstop sized for the case where the end
		// of speech is never reported at all; a reply longer than that drained to neutral partway
		// through the sentence it belonged to. EndSpeech's Release() still shortens the lease at the
		// true end, so this only ever moves the failure case, never the normal one.
		Emotion.Sustain((FVisemePlanner::TrackDuration(Track) - SpeechElapsed)
			+ FCompanionEmotionState::DefaultHoldSeconds);
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

	// And she goes back to looking at whoever she was talking to. Note this is the LISTENING mode,
	// not merely "not speaking": having just finished a turn, attending to the other party is the
	// correct behaviour, and it is what makes the hand-back of the conversational floor legible.
	Gaze.SetMode(FCompanionGazeState::EGazeMode::Contact);
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

void UCompanionFaceComponent::SetGazeMode(FCompanionGazeState::EGazeMode InMode)
{
	// Passed through even while gaze is disabled, so the state stays truthful and re-enabling does
	// not resume in whatever mode was current when it was switched off.
	Gaze.SetMode(InMode);
}

void UCompanionFaceComponent::SetGazeEnabled(bool bEnabled)
{
	bGazeEnabled = bEnabled;
	if (!bEnabled)
	{
		// Recentred rather than frozen: a disabled gaze driver that leaves the eyes deflected mid-
		// saccade looks exactly like a broken one. The next tick's zeroing pass finishes the job.
		Gaze.Reset();
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
