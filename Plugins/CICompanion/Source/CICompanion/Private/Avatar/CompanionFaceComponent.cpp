#include "Avatar/CompanionFaceComponent.h"
#include "CICompanionLog.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/MorphTarget.h"
#include "CICompanionEnv.h"


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
	// 1.0 keeps the historical behaviour; MHLLM_FACE_GAIN dials the CC rig's subtle morphs up
	// toward the 5x ceiling Config/DefaultEngine.ini already grants the renderer.
	FaceGain = FCString::Atof(*CICompanionEnv::Get(TEXT("MHLLM_FACE_GAIN"), TEXT("3.0")));
	if (FaceGain <= 0.0f) { FaceGain = 1.0f; }
	// Bounds the composed pose so overlapping visemes cannot stack past what the rig was sculpted
	// for. 2.0 keeps the open vowels clearly open while removing the torn-lip frames gain 3.0
	// produced on its own.
	VisemePoseCap = FCString::Atof(*CICompanionEnv::Get(TEXT("MHLLM_FACE_POSE_CAP"), TEXT("2.0")));
	if (VisemePoseCap <= 0.0f) { VisemePoseCap = 2.0f; }
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
	// EDITOR ONLY, and the guard is the whole point.
	//
	// UMorphTarget::MorphLODModels holds the authored deltas, and the cook STRIPS them once they
	// have been baked into the render data's morph vertex buffers. So in a packaged build this
	// array is legitimately empty on a perfectly healthy mesh. An earlier version of this check ran
	// unguarded and duly reported "42/42 curves with ZERO vertex deltas -- they will never deform
	// anything" against a mesh whose source carries all 42 sets of deltas (see
	// MorphDataSpec, which asserts exactly that in the editor). It was a false alarm that read like
	// a smoking gun, which is worse than no check at all.
	//
	// Kept because it is genuinely useful where it CAN run: an editor session or a spec catches a
	// bad import immediately. For the cooked runtime the equivalent question has to be asked of
	// FSkeletalMeshRenderData's morph vertex buffers instead, which is a different API and not
	// something to fake with this one.
#if WITH_EDITORONLY_DATA
	TArray<FString> Empty;
#endif
	for (const FName& Curve : CachedAllCurves)
	{
		if (const UMorphTarget* MT = Asset->FindMorphTarget(Curve))
		{
			++ResolvedCurveCount;
#if WITH_EDITORONLY_DATA
			const TArray<FMorphTargetLODModel>& LODs = MT->GetMorphLODModels();
			if (LODs.Num() == 0 || LODs[0].Vertices.Num() == 0)
			{
				Empty.Add(Curve.ToString());
			}
#endif
		}
		else
		{
			Missing.Add(Curve);
		}
	}

#if WITH_EDITORONLY_DATA
	if (Empty.Num() > 0)
	{
		UE_LOG(LogCompanionFace, Warning,
			TEXT("Face profile '%s': %d/%d curves resolve to morph targets with no authored deltas at LOD0 on '%s' (editor check) -- verify the import: %s"),
			*Profile.ProfileName.ToString(), Empty.Num(), CachedAllCurves.Num(), *Asset->GetName(),
			*FString::Join(Empty, TEXT(", ")));
	}
#endif

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

	// CEILING, not gain. The mouth gain is applied per-layer in ComposeFrame, because it must not
	// touch blink, gaze or emotion -- see the note there. What is left here is the bound.
	//
	// It used to be a hardcoded 1.0, while Config/DefaultEngine.ini raises
	//   r.MorphTarget.MaxBlendWeight=5.0
	// i.e. the renderer had deliberately been granted 5x of headroom that this line then clamped
	// away, so none of it was ever reachable. Read from the same CVar rather than restated, so the
	// two cannot drift apart again.
	static const auto* MaxBlendCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MorphTarget.MaxBlendWeight"));
	const float Ceiling = MaxBlendCVar ? FMath::Max(1.0f, MaxBlendCVar->GetFloat()) : 1.0f;

	for (const TPair<FName, float>& Pair : Curves)
	{
		// Gain is already applied per-layer in ComposeFrame; here we only bound to what the
		// renderer will actually blend.
		TargetMesh->SetMorphTarget(Pair.Key, FMath::Clamp(Pair.Value, 0.0f, Ceiling));
	}

	// SURVIVAL CHECK. Records what was written; the verdict is logged at the START of the next
	// tick by SampleSurvival(), because that is the only place the answer is meaningful.
	//
	// An immediate read-back proves nothing. GetMorphTarget called one line after SetMorphTarget
	// re-reads the map that was just written, so it reports "ok" whether or not the value ever
	// reaches skinning -- I chased that false positive already. What matters is whether the weight
	// is STILL set after the mesh has evaluated its own animation, because
	// USkeletalMeshComponent rebuilds its morph/curve state from the anim instance during
	// evaluation and a single-node MF_Idle carries no morph curves at all, which would zero
	// everything written before it.
	if (Curves.Num() > 0)
	{
		float TopApplied = 0.0f;
		FName TopName = NAME_None;
		for (const TPair<FName, float>& Pair : Curves)
		{
			if (Pair.Value > TopApplied) { TopApplied = Pair.Value; TopName = Pair.Key; }
		}
		LastWrittenCurve = TopName;
		LastWrittenWeight = TopApplied;
	}
}

void UCompanionFaceComponent::SampleSurvival()
{
	// Called at the top of TickComponent, i.e. one full frame after ApplyCurves wrote the value and
	// AFTER the mesh's animation has evaluated (this component holds a tick prerequisite on it).
	// If Written is high and Survived is ~0, the weights are being cleared between frames and the
	// fault is downstream of this class, not in the compositing above.
	if (!TargetMesh || LastWrittenCurve.IsNone() || LastWrittenWeight <= 0.01f)
	{
		return;
	}
	static double LastLogTime = 0.0;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastLogTime <= 1.0)
	{
		return;
	}
	LastLogTime = Now;

	const float Survived = TargetMesh->GetMorphTarget(LastWrittenCurve);
	// Verbose: useful when the face is under suspicion, silent otherwise. At Log level this
	// prints every second of every session for a question nobody is asking most of the time.
	UE_LOG(LogCompanionFace, Verbose,
		TEXT("FACESURVIVE '%s' written=%.3f survived_next_frame=%.3f %s"),
		*LastWrittenCurve.ToString(), LastWrittenWeight, Survived,
		(Survived < 0.01f) ? TEXT("<-- CLEARED BETWEEN FRAMES") : TEXT("held"));
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
	// Kept apart from Curves so the output gain can be applied to the MOUTH alone -- see the
	// note where it is merged in below.
	TMap<FName, float> VisemeCurves;

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
				Profile.Accumulate(Pair.Key, Pair.Value, VisemeCurves);
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

		Profile.Accumulate(static_cast<EViseme>(Index), Weight, VisemeCurves);
	}

	// GAIN APPLIES TO THE MOUTH ONLY, which is why the visemes were accumulated into their own map.
	//
	// The CC rig's V_* morphs are subtle: at unity the whole vocabulary produces lip motion an
	// order of magnitude below the idle head movement sitting on top of it, which is why the face
	// reads as dead. Measured across a gain sweep, lip deformation at the self-test's 2.5Hz drive
	// frequency roughly doubles from 1.0 to 2.0 while an undriven NOSE control stays flat, and by
	// 3.0 the open vowels are unmistakable on screen -- teeth visible, mouth genuinely open.
	//
	// It must NOT reach the other layers. AddEmotion deliberately keeps every FACS weight <= 0.45
	// because "a full-strength FACS pose reads as a caricature", and blink is already a full close
	// at 1.0; multiplying either by 3 would give a permanently mugging face and eyes that slam
	// shut. Gaze is likewise authored in its own normalised space.
	//
	// 5.0 was measured too: the mouth distorts into a stretched oval and stops reading as a mouth,
	// so the useful range ends well below the engine's ceiling.
	if (VisemeCurves.Num() > 0)
	{
		// BOUND THE COMPOSED POSE, not each curve on its own.
		//
		// FFaceProfile::Accumulate clamps every curve to 1.0 INDEPENDENTLY, so two overlapping
		// visemes can each legitimately arrive at full strength and then both get multiplied by the
		// gain. The result is a pose no one ever sculpted -- at gain 3.0 the sample utterance
		// produced a stretched upper lip with torn corners about 1.9s in, which is the rig being
		// asked for 3x of two contradictory shapes at once rather than any one shape being wrong.
		//
		// Scaling the whole set by a single factor keeps the RELATIONSHIP between the curves (which
		// is what makes the shape read as a particular viseme) while bounding the peak. Lowering
		// the gain instead would fix the distortion by going back to invisible, which is the
		// failure this gain exists to correct.
		float Peak = 0.0f;
		for (const TPair<FName, float>& Pair : VisemeCurves)
		{
			Peak = FMath::Max(Peak, Pair.Value);
		}

		const float Gain = FMath::Max(0.1f, FaceGain);
		// Effective gain never pushes the strongest curve past the cap. A quiet frame gets the full
		// gain; a frame already near full travel gets only what is left.
		const float Effective = (Peak > KINDA_SMALL_NUMBER)
			? FMath::Min(Gain, VisemePoseCap / Peak)
			: Gain;

		for (const TPair<FName, float>& Pair : VisemeCurves)
		{
			float& Value = Curves.FindOrAdd(Pair.Key, 0.0f);
			Value = FMath::Max(Value, Pair.Value * Effective);
		}
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

	// Before composing anything: did last frame's write survive the mesh's animation pass?
	SampleSurvival();

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

