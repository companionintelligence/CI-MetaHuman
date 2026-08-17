#include "Misc/AutomationTest.h"
#include "Avatar/CompanionEmotionState.h"
#include "Avatar/CompanionFaceComponent.h"
#include "Avatar/FaceProfile.h"
#include "UObject/Package.h"

BEGIN_DEFINE_SPEC(FCompanionEmotionSpec, "MetaHumanLocalLLM.Backend.CompanionEmotion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FCompanionEmotionSpec)

// Covers the thing the whole feature turns on: an expression must COMPOSITE OVER speech, not
// compete with it for the face.
//
// FFaceProfile keeps EmotionShapes separate from Shapes and weights it brow/cheek/eye-first so an
// expression physically cannot smear a viseme. That separation is only worth anything if the
// driver actually sums the two layers, and nothing but a test can tell "summed" from "replaced"
// once a frame has been captured — both produce a face that is doing something.
//
// FCompanionEmotionState is a plain value type on purpose, so the blend/hold/decay policy runs
// here with no mesh, no world and no rig, the same way UCompanionStageDirector's camera policy
// does. The component-level tests build a real UCompanionFaceComponent but never register it into
// a world; ComposeFrame() touches nothing but its own state.
void FCompanionEmotionSpec::Define()
{
	// Runs Advance in small steps rather than one big one, because a single huge DeltaTime would
	// hide any rate bug — a constant-rate blend and a snap are indistinguishable in one jump.
	auto AdvanceBy = [](FCompanionEmotionState& State, float Seconds, float Step = 1.0f / 60.0f)
	{
		for (float T = 0.0f; T < Seconds; T += Step)
		{
			State.Advance(Step);
		}
	};

	auto MakeLookup = [](const TMap<FString, FString>& Env)
	{
		return [Env](const TCHAR* Name) -> FString
		{
			const FString* Found = Env.Find(FString(Name));
			return Found ? *Found : FString();
		};
	};

	// A real UCompanionFaceComponent with deterministic inputs: blink off (it fires on a random
	// timer and would add eyelid curves to half the assertions below) and emotion explicitly on
	// (the constructor reads MHLLM_EMOTION, and a developer running with it set to 0 must not
	// silently turn these tests green).
	//
	// Gaze off for the SAME reason blink is, and it is a stronger version of the problem. Blink is
	// intermittent, so it only sometimes dirtied a frame; gaze is continuous by design -- the eyes
	// are always aimed somewhere, so from the moment it landed EVERY composed frame carried four eye
	// curves. That is correct behaviour and it is why two assertions here read "the face composes to
	// nothing at all" and got 4: they were written when an idle face genuinely was empty, and they
	// are asserting the EMOTION contract, not the gaze one. Turning the channel off restores the
	// question each test is actually asking. Gaze composition has its own coverage in
	// CompanionGazeSpec.
	auto MakeFace = []() -> UCompanionFaceComponent*
	{
		UCompanionFaceComponent* Face = NewObject<UCompanionFaceComponent>(GetTransientPackage());
		Face->SetProfile(FFaceProfile::CharacterCreator());
		Face->SetBlinkEnabled(false);
		Face->SetGazeEnabled(false);
		Face->SetEmotionEnabled(true);
		return Face;
	};

	Describe("compositing over speech", [this, MakeFace]()
	{
		It("adds to a curve a viseme already drives rather than overwriting it", [this]()
		{
			// ARKit is the profile where the two layers genuinely overlap: mouthSmileLeft is part of
			// the E/IH vowel shapes AND part of Happy. That overlap is the only place "additive" and
			// "replacing" produce different numbers, so it is where the property has to be pinned.
			const FFaceProfile& P = FFaceProfile::ARKit();

			TMap<FName, float> SpeechOnly;
			P.Accumulate(EViseme::E, 1.0f, SpeechOnly);

			TMap<FName, float> Composited = SpeechOnly;
			P.AccumulateEmotion(EEmotionType::Happy, 1.0f, Composited);

			TestTrue("viseme drives mouthSmileLeft at all", SpeechOnly.Contains(TEXT("mouthSmileLeft")));
			TestTrue("emotion raises it rather than replacing it",
				Composited[TEXT("mouthSmileLeft")] > SpeechOnly[TEXT("mouthSmileLeft")]);
			TestEqual("and it is exactly the sum",
				Composited[TEXT("mouthSmileLeft")], SpeechOnly[TEXT("mouthSmileLeft")] + 0.35f, 0.001f);
		});

		It("leaves every curve the viseme wrote untouched", [this]()
		{
			// The other half of "does not replace": nothing the mouth was doing may go missing or
			// change value when an expression is laid on top of it.
			const FFaceProfile& P = FFaceProfile::CharacterCreator();

			TMap<FName, float> SpeechOnly;
			P.Accumulate(EViseme::AA, 1.0f, SpeechOnly);

			TMap<FName, float> Composited = SpeechOnly;
			P.AccumulateEmotion(EEmotionType::Angry, 1.0f, Composited);

			for (const TPair<FName, float>& Pair : SpeechOnly)
			{
				TestTrue(*FString::Printf(TEXT("%s survived"), *Pair.Key.ToString()),
					Composited.Contains(Pair.Key));
				TestEqual(*FString::Printf(TEXT("%s unchanged"), *Pair.Key.ToString()),
					Composited[Pair.Key], Pair.Value, 0.001f);
			}
			TestTrue("and the expression is present too", Composited.Contains(TEXT("Brow_Drop_L")));
			TestTrue("frame is strictly larger", Composited.Num() > SpeechOnly.Num());
		});

		It("composites in the driver, not just in the profile", [this, MakeFace]()
		{
			// The same property one level up: a real frame from the face driver, with a viseme track
			// playing and an expression held, must contain BOTH layers.
			UCompanionFaceComponent* Face = MakeFace();
			Face->PlayVisemeTrack({ FVisemeSpan(0.0f, 2.0f, EViseme::AA) });
			Face->SetEmotion(EEmotionType::Happy, 1.0f);

			TMap<FName, float> Frame;
			for (int32 I = 0; I < 30; ++I)		// 0.5s: past the 0.4s blend, well inside the track
			{
				Frame = Face->ComposeFrame(1.0f / 60.0f);
			}

			TestTrue("speaking", Face->IsSpeaking());
			TestTrue("mouth open (viseme layer)", Frame.FindRef(TEXT("V_Open")) > 0.5f);
			TestTrue("jaw driven (viseme layer)", Frame.FindRef(TEXT("Jaw_Open")) > 0.0f);
			TestTrue("smiling (emotion layer)", Frame.FindRef(TEXT("Mouth_Smile_L")) > 0.0f);
			TestTrue("cheeks raised (emotion layer)", Frame.FindRef(TEXT("Cheek_Raise_L")) > 0.0f);
			TestTrue("brow raised (emotion layer)", Frame.FindRef(TEXT("Brow_Raise_Outer_L")) > 0.0f);
		});
	});

	Describe("curve ownership", [this]()
	{
		It("never writes a curve the CC visemes own", [this]()
		{
			// CC is the resident's actual rig, and there the two vocabularies are fully disjoint:
			// speech drives V_*/Jaw_/Mouth_Close, expression drives brow/cheek/eye. Anything that
			// crosses that line would let a held expression bias every word she says.
			const FFaceProfile& P = FFaceProfile::CharacterCreator();

			TSet<FName> VisemeCurves;
			for (const TPair<EViseme, FVisemeShape>& Pair : P.Shapes)
			{
				for (const FFaceCurve& Curve : Pair.Value.Curves) { VisemeCurves.Add(Curve.Curve); }
			}

			TArray<FString> Collisions;
			for (const TPair<EEmotionType, FVisemeShape>& Pair : P.EmotionShapes)
			{
				for (const FFaceCurve& Curve : Pair.Value.Curves)
				{
					if (VisemeCurves.Contains(Curve.Curve)) { Collisions.Add(Curve.Curve.ToString()); }
				}
			}
			TestEqual(*FString::Printf(TEXT("emotion curves also owned by visemes: %s"),
				*FString::Join(Collisions, TEXT(", "))), Collisions.Num(), 0);
		});

		It("never touches an aperture articulator in either profile", [this]()
		{
			// The rule that matters even where the two layers DO share a curve. ARKit's Happy and its
			// E/IH vowels both use mouthSmileLeft, and summing them is fine — both pull the corner of
			// the mouth the same way. What must never happen is an expression driving how far the
			// mouth is OPEN, because aperture is the whole of what makes a viseme legible: a held
			// jawOpen under speech turns every consonant into the same slack shape.
			const TArray<FName> Aperture = {
				TEXT("Jaw_Open"), TEXT("jawOpen"), TEXT("Mouth_Close"), TEXT("mouthClose"),
				TEXT("mouthFunnel"), TEXT("mouthPucker"), TEXT("V_Open"), TEXT("V_Tight_O"),
				TEXT("V_Tight"), TEXT("V_Wide"), TEXT("V_Explosive") };

			const FFaceProfile* Profiles[] = { &FFaceProfile::CharacterCreator(), &FFaceProfile::ARKit() };
			for (const FFaceProfile* P : Profiles)
			{
				for (const TPair<EEmotionType, FVisemeShape>& Pair : P->EmotionShapes)
				{
					for (const FFaceCurve& Curve : Pair.Value.Curves)
					{
						TestFalse(*FString::Printf(TEXT("%s emotion drives aperture curve %s"),
							*P->ProfileName.ToString(), *Curve.Curve.ToString()),
							Aperture.Contains(Curve.Curve));
					}
				}
			}
		});

		It("includes emotion curves in the zeroing set", [this]()
		{
			// THE LATCH TRAP, the same one blink hit. AllCurves() is what the driver zeroes every
			// frame; a curve missing from it is never cleared, so the first expression that raised a
			// brow would leave it raised for the rest of the session.
			const FFaceProfile* Profiles[] = { &FFaceProfile::CharacterCreator(), &FFaceProfile::ARKit() };
			for (const FFaceProfile* P : Profiles)
			{
				const TArray<FName> All = P->AllCurves();
				for (const TPair<EEmotionType, FVisemeShape>& Pair : P->EmotionShapes)
				{
					for (const FFaceCurve& Curve : Pair.Value.Curves)
					{
						TestTrue(*FString::Printf(TEXT("%s AllCurves contains %s"),
							*P->ProfileName.ToString(), *Curve.Curve.ToString()),
							All.Contains(Curve.Curve));
					}
				}
			}
		});
	});

	Describe("blending", [this, AdvanceBy]()
	{
		It("does not snap to the new expression", [this]()
		{
			FCompanionEmotionState State;
			State.Set(EEmotionType::Happy, 1.0f);
			TestEqual("nothing on the face yet", State.GetWeight(), 0.0f, 0.001f);

			State.Advance(1.0f / 60.0f);
			TestTrue("has started", State.GetWeight() > 0.0f);
			TestTrue("but is nowhere near arrived", State.GetWeight() < 0.25f);
		});

		It("reaches full strength in about the advertised blend time", [this, AdvanceBy]()
		{
			FCompanionEmotionState State;
			State.Set(EEmotionType::Happy, 1.0f);

			AdvanceBy(State, FCompanionEmotionState::DefaultBlendSeconds * 0.5f);
			TestTrue("half way in, roughly half way there",
				State.GetWeight() > 0.35f && State.GetWeight() < 0.65f);

			AdvanceBy(State, FCompanionEmotionState::DefaultBlendSeconds);
			TestEqual("arrived", State.GetWeight(), 1.0f, 0.01f);
		});

		It("cross-fades between two expressions instead of passing through neutral", [this, AdvanceBy]()
		{
			// happy -> neutral -> sad takes twice as long and reads as a double take. Mid-change both
			// expressions must be on the face at once.
			const FFaceProfile& P = FFaceProfile::CharacterCreator();

			FCompanionEmotionState State;
			State.Set(EEmotionType::Happy, 1.0f);
			AdvanceBy(State, FCompanionEmotionState::DefaultBlendSeconds * 1.5f);

			State.Set(EEmotionType::Sad, 1.0f);
			AdvanceBy(State, FCompanionEmotionState::DefaultBlendSeconds * 0.5f);

			TMap<FName, float> Curves;
			State.AccumulateInto(P, Curves);
			TestTrue("the outgoing smile is still there", Curves.FindRef(TEXT("Mouth_Smile_L")) > 0.0f);
			TestTrue("and the incoming brow is coming in", Curves.FindRef(TEXT("Brow_Raise_Inner_L")) > 0.0f);
			TestTrue("identity has already switched",
				State.GetEmotion() == EEmotionType::Sad);
		});

		It("scales the profile's tuned weights by intensity rather than replacing them", [this, AdvanceBy]()
		{
			FCompanionEmotionState Full, Half;
			Full.Set(EEmotionType::Happy, 1.0f);
			Half.Set(EEmotionType::Happy, 0.5f);
			AdvanceBy(Full, 1.0f);
			AdvanceBy(Half, 1.0f);

			TMap<FName, float> FullCurves, HalfCurves;
			Full.AccumulateInto(FFaceProfile::CharacterCreator(), FullCurves);
			Half.AccumulateInto(FFaceProfile::CharacterCreator(), HalfCurves);

			// 0.35 is the profile's tuned FACS weight for a CC smile; intensity multiplies it.
			TestEqual("full", FullCurves[TEXT("Mouth_Smile_L")], 0.35f, 0.01f);
			TestEqual("half", HalfCurves[TEXT("Mouth_Smile_L")], 0.175f, 0.01f);
		});
	});

	Describe("decay", [this, AdvanceBy, MakeFace]()
	{
		It("returns to neutral once the sustain runs out, with nobody having to ask", [this, AdvanceBy]()
		{
			// The backstop. If the utterance never reports an end — which is exactly what happens
			// when a TTS service is configured but down, since the mouth is driven from its audio
			// callback and the viseme track never starts — the expression still has to let go.
			FCompanionEmotionState State;
			State.Set(EEmotionType::Angry, 1.0f);
			AdvanceBy(State, 1.0f);
			TestTrue("held", State.GetWeight() > 0.9f);

			AdvanceBy(State, FCompanionEmotionState::DefaultSustainSeconds
				+ FCompanionEmotionState::DefaultBlendSeconds + 0.5f);

			TestEqual("faded out", State.GetWeight(), 0.0f, 0.001f);
			TestTrue("and reports neutral", State.GetEmotion() == EEmotionType::Neutral);
			TestFalse("nothing left active", State.IsActive());

			TMap<FName, float> Curves;
			State.AccumulateInto(FFaceProfile::CharacterCreator(), Curves);
			TestEqual("no curves emitted", Curves.Num(), 0);
		});

		It("releases on a short hold and then blends out", [this, AdvanceBy]()
		{
			FCompanionEmotionState State;
			State.Set(EEmotionType::Happy, 1.0f);
			AdvanceBy(State, 1.0f);

			State.Release(1.2f);
			AdvanceBy(State, 1.0f);
			TestTrue("still held through the beat after the line", State.GetWeight() > 0.9f);

			AdvanceBy(State, 0.2f + FCompanionEmotionState::DefaultBlendSeconds + 0.2f);
			TestEqual("let go", State.GetWeight(), 0.0f, 0.001f);
		});

		It("cannot have its release pushed back by repeating it", [this, AdvanceBy]()
		{
			// Release() takes the MIN. Otherwise a stream of end-of-utterance events would keep
			// renewing the hold and the expression would never leave.
			FCompanionEmotionState State;
			State.Set(EEmotionType::Happy, 1.0f);
			AdvanceBy(State, 1.0f);

			State.Release(0.5f);
			const float AfterShort = State.GetSustainRemaining();
			State.Release(5.0f);
			TestTrue("a longer hold does not extend it", State.GetSustainRemaining() <= AfterShort);
		});

		It("lets go after the utterance ends, in the driver", [this, MakeFace]()
		{
			// End to end through the component: an expression set for a reply must not outlive it.
			UCompanionFaceComponent* Face = MakeFace();
			Face->PlayVisemeTrack({ FVisemeSpan(0.0f, 0.5f, EViseme::AA) });
			Face->SetEmotion(EEmotionType::Happy, 1.0f);

			// 40 frames, not the 30 that exactly cover a 0.5s track: 30 additions of 1/60 land
			// within a float ULP of the end and whether the >= fires is then a property of the
			// compiler's rounding, not of the code. Overshoot deliberately.
			for (int32 I = 0; I < 40; ++I) { Face->ComposeFrame(1.0f / 60.0f); }
			TestFalse("track finished", Face->IsSpeaking());
			TestTrue("expression still on the face right after the line",
				Face->GetEmotionWeight() > 0.9f);

			TMap<FName, float> Frame;
			for (int32 I = 0; I < 180; ++I) { Frame = Face->ComposeFrame(1.0f / 60.0f); }

			TestEqual("decayed", Face->GetEmotionWeight(), 0.0f, 0.001f);
			TestTrue("back to neutral", Face->GetEmotion() == EEmotionType::Neutral);
			TestEqual("and the face composes to nothing at all", Frame.Num(), 0);
		});
	});

	Describe("the MHLLM_EMOTION kill switch", [this, MakeLookup, MakeFace]()
	{
		It("is on when nothing is configured", [this, MakeLookup]()
		{
			// An expressionless face is the regression this feature exists to fix, so unset must
			// mean on. This is the opposite default to the stage director's opt-IN switches, and
			// deliberately so: a camera that moves is a surprise, a face that emotes is the product.
			TMap<FString, FString> Env;
			TestTrue("default on", FCompanionEmotionState::IsEnabled(MakeLookup(Env)));
		});

		It("is off only for an explicit 0 or false", [this, MakeLookup]()
		{
			TMap<FString, FString> Env;
			Env.Add(TEXT("MHLLM_EMOTION"), TEXT("0"));
			TestFalse("zero", FCompanionEmotionState::IsEnabled(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_EMOTION"), TEXT("false"));
			TestFalse("false", FCompanionEmotionState::IsEnabled(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_EMOTION"), TEXT("FALSE"));
			TestFalse("case insensitive", FCompanionEmotionState::IsEnabled(MakeLookup(Env)));

			// Compose files interpolate blanks around a substituted value.
			Env.Add(TEXT("MHLLM_EMOTION"), TEXT(" 0 "));
			TestFalse("padded zero", FCompanionEmotionState::IsEnabled(MakeLookup(Env)));
		});

		It("stays on for 1, an empty value, or a typo", [this, MakeLookup]()
		{
			// Fails toward WORKING, which is the right direction for something cosmetic: a mistyped
			// variable should not silently ship a mannequin.
			TMap<FString, FString> Env;
			Env.Add(TEXT("MHLLM_EMOTION"), TEXT("1"));
			TestTrue("one", FCompanionEmotionState::IsEnabled(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_EMOTION"), TEXT(""));
			TestTrue("empty means unconfigured", FCompanionEmotionState::IsEnabled(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_EMOTION"), TEXT("flase"));
			TestTrue("typo", FCompanionEmotionState::IsEnabled(MakeLookup(Env)));
		});

		It("makes SetEmotion a no-op in the driver when disabled", [this, MakeFace]()
		{
			UCompanionFaceComponent* Face = MakeFace();
			Face->SetEmotionEnabled(false);
			Face->PlayVisemeTrack({ FVisemeSpan(0.0f, 2.0f, EViseme::AA) });
			Face->SetEmotion(EEmotionType::Happy, 1.0f);

			TMap<FName, float> Frame;
			for (int32 I = 0; I < 30; ++I) { Frame = Face->ComposeFrame(1.0f / 60.0f); }

			TestEqual("no expression weight", Face->GetEmotionWeight(), 0.0f, 0.001f);
			TestFalse("no smile curve", Frame.Contains(TEXT("Mouth_Smile_L")));
			TestFalse("no brow curve", Frame.Contains(TEXT("Brow_Raise_Outer_L")));

			// The mouth must be entirely unaffected: the switch turns emotion off, not the face.
			TestTrue("speech still drives the mouth", Frame.FindRef(TEXT("V_Open")) > 0.5f);
		});

		It("drops an expression already on the face when switched off mid-utterance", [this, MakeFace]()
		{
			// Switching it off must not leave the last frame latched on the mesh, which would be
			// indistinguishable from the switch not working at all.
			UCompanionFaceComponent* Face = MakeFace();
			Face->SetEmotion(EEmotionType::Happy, 1.0f);
			for (int32 I = 0; I < 30; ++I) { Face->ComposeFrame(1.0f / 60.0f); }
			TestTrue("expression established", Face->GetEmotionWeight() > 0.9f);

			Face->SetEmotionEnabled(false);
			const TMap<FName, float> Frame = Face->ComposeFrame(1.0f / 60.0f);

			TestEqual("gone", Face->GetEmotionWeight(), 0.0f, 0.001f);
			TestEqual("frame is empty", Frame.Num(), 0);
		});
	});
}
