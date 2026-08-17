#include "Misc/AutomationTest.h"
#include "Avatar/CompanionFaceComponent.h"
#include "Avatar/CompanionGazeState.h"
#include "Avatar/FaceProfile.h"
#include "UObject/Package.h"

BEGIN_DEFINE_SPEC(FCompanionGazeSpec, "MetaHumanLocalLLM.Backend.CompanionGaze", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FCompanionGazeSpec)

// Guards the two ways synthetic gaze goes wrong, both of which are INVISIBLE to every other kind of
// check this project runs.
//
//  1. WALL-EYE. The eight gaze curves are named per-eye, and the ARKit set spells the same physical
//     direction as "Out" on one eye and "In" on the other. Pair them by name instead of by meaning
//     and the eyes diverge on every glance. A frame grab shows a face with moving eyes; a curve-count
//     check shows 8/8 resolved; the unit suite shows green. Only an assertion on which curve carries
//     which sign catches it.
//
//  2. A NEGATIVE MORPH WEIGHT. These are one-sided morphs, so "look left" is a separate target from
//     "look right". Writing -X into the right-hand curve rather than +X into the left-hand one is
//     arithmetically tempting and pushes geometry inside out on any rig that does not clamp.
//
// FCompanionGazeState is a plain value type for the same reason FCompanionEmotionState is: the
// policy runs here with no mesh, no world and no rig. Its RNG is an owned FRandomStream rather than
// the global FMath::FRand specifically so these tests can pin a seed and assert on a real sequence
// — the blink clock next to it uses the global stream and can therefore only be tested for bounds.
void FCompanionGazeSpec::Define()
{
	// Steps rather than one jump, so a glide and a snap stay distinguishable.
	auto AdvanceBy = [](FCompanionGazeState& State, float Seconds, float Step = 1.0f / 120.0f)
	{
		for (float T = 0.0f; T < Seconds; T += Step)
		{
			State.Advance(Step);
		}
	};

	// One Advance long enough to guarantee the fixation expired, so each call is exactly one saccade.
	// Scaled past the Thinking multiplier so it holds for every mode.
	auto ForceOneSaccade = [](FCompanionGazeState& State)
	{
		State.Advance(FCompanionGazeState::MaxFixationSeconds * 2.0f);
	};

	auto MakeLookup = [](const TMap<FString, FString>& Env)
	{
		return [Env](const TCHAR* Name) -> FString
		{
			const FString* Found = Env.Find(FString(Name));
			return Found ? *Found : FString();
		};
	};

	Describe("curve mapping", [this, AdvanceBy]()
	{
		It("drives the matching side on BOTH eyes, and never the opposite side", [this, AdvanceBy]()
		{
			// Driven from real sequences rather than a hand-set aim, because the state deliberately
			// exposes no aim setter: asserting that the curves agree with whatever GetAim() actually
			// produced is a stronger property than asserting they agree with a value the test forced.
			const FFaceProfile& P = FFaceProfile::CharacterCreator();

			bool bSawLeft = false, bSawRight = false, bSawUp = false, bSawDown = false;

			for (int32 Seed = 1; Seed <= 60; ++Seed)
			{
				FCompanionGazeState Gaze;
				Gaze.SetSeed(Seed);
				Gaze.SetMode(FCompanionGazeState::EGazeMode::Thinking);	// widest deflections
				AdvanceBy(Gaze, 0.5f);

				const FVector2D Aim = Gaze.GetAim();

				TMap<FName, float> Curves;
				Gaze.AccumulateInto(P, Curves);

				const float LeftL = Curves.FindRef(TEXT("Eye_L_Look_L"));
				const float LeftR = Curves.FindRef(TEXT("Eye_L_Look_R"));
				const float RightL = Curves.FindRef(TEXT("Eye_R_Look_L"));
				const float RightR = Curves.FindRef(TEXT("Eye_R_Look_R"));
				const float LeftU = Curves.FindRef(TEXT("Eye_L_Look_Up"));
				const float LeftD = Curves.FindRef(TEXT("Eye_L_Look_Down"));
				const float RightU = Curves.FindRef(TEXT("Eye_R_Look_Up"));
				const float RightD = Curves.FindRef(TEXT("Eye_R_Look_Down"));

				// THE WALL-EYE ASSERTION: the two eyes must carry the SAME deflection. At
				// conversational distance the true convergence angle is under a degree — far below
				// anything these curves can express — so "correct" here means "identical".
				TestEqual(TEXT("both eyes deflect left equally"), RightL, LeftL, KINDA_SMALL_NUMBER);
				TestEqual(TEXT("both eyes deflect right equally"), RightR, LeftR, KINDA_SMALL_NUMBER);
				TestEqual(TEXT("both eyes deflect up equally"), RightU, LeftU, KINDA_SMALL_NUMBER);
				TestEqual(TEXT("both eyes deflect down equally"), RightD, LeftD, KINDA_SMALL_NUMBER);

				// Never both sides of one axis at once — that is a squint, not a glance.
				TestTrue(TEXT("horizontal is one-sided"), LeftL <= KINDA_SMALL_NUMBER || LeftR <= KINDA_SMALL_NUMBER);
				TestTrue(TEXT("vertical is one-sided"), LeftU <= KINDA_SMALL_NUMBER || LeftD <= KINDA_SMALL_NUMBER);

				if (Aim.X < -KINDA_SMALL_NUMBER)
				{
					bSawLeft = true;
					// static_cast<float> on every aim component throughout this spec, and it is not
					// cosmetic: FVector2D is UE::Math::TVector2<double> in UE5, so Aim.X is a
					// DOUBLE while the curve weights out of TMap<FName,float> are floats.
					// FAutomationTestBase declares both a (float,float,float) and a
					// (double,double,double) TestEqual, and a mixed call makes neither a better
					// match in every argument -- float->double is a promotion, double->float a
					// conversion -- so it is an ambiguous overload (MSVC C2666) and the whole
					// module fails to build. Casting to float picks one overload unambiguously.
					TestEqual(TEXT("negative X drives look-left"), LeftL, static_cast<float>(-Aim.X), 1e-4f);
					TestEqual(TEXT("negative X leaves look-right at zero"), LeftR, 0.0f, KINDA_SMALL_NUMBER);
				}
				else if (Aim.X > KINDA_SMALL_NUMBER)
				{
					bSawRight = true;
					TestEqual(TEXT("positive X drives look-right"), LeftR, static_cast<float>(Aim.X), 1e-4f);
					TestEqual(TEXT("positive X leaves look-left at zero"), LeftL, 0.0f, KINDA_SMALL_NUMBER);
				}

				if (Aim.Y < -KINDA_SMALL_NUMBER)
				{
					bSawDown = true;
					TestEqual(TEXT("negative Y drives look-down"), LeftD, static_cast<float>(-Aim.Y), 1e-4f);
				}
				else if (Aim.Y > KINDA_SMALL_NUMBER)
				{
					bSawUp = true;
					TestEqual(TEXT("positive Y drives look-up"), LeftU, static_cast<float>(Aim.Y), 1e-4f);
				}

				for (const TPair<FName, float>& Pair : Curves)
				{
					TestTrue(TEXT("no negative morph weight is ever written"), Pair.Value >= 0.0f);
				}
			}

			// Without this the assertions above could all be vacuously true on a state that never
			// moved — which is precisely the bug the feature exists to fix.
			TestTrue(TEXT("the sweep exercised a leftward aim"), bSawLeft);
			TestTrue(TEXT("the sweep exercised a rightward aim"), bSawRight);
			TestTrue(TEXT("the sweep exercised an upward aim"), bSawUp);
			TestTrue(TEXT("the sweep exercised a downward aim"), bSawDown);
		});

		It("writes no zero-weight entries, so the driver's idle latch still works", [this, AdvanceBy]()
		{
			// UCompanionFaceComponent::ApplyCurves sets bFaceWritten from Curves.Num(), and the tick
			// skips the whole mesh write when the map is empty AND nothing was written last frame.
			// Emitting eight zeroes every frame would pin that latch on permanently and turn the idle
			// path into ~20 pointless SetMorphTarget calls a frame, forever.
			FCompanionGazeState Gaze;
			Gaze.SetSeed(4242);
			AdvanceBy(Gaze, 2.0f);

			TMap<FName, float> Curves;
			Gaze.AccumulateInto(FFaceProfile::CharacterCreator(), Curves);

			for (const TPair<FName, float>& Pair : Curves)
			{
				TestTrue(TEXT("every emitted curve carries real weight"), Pair.Value > KINDA_SMALL_NUMBER);
			}
		});

		It("contributes nothing to a profile that cannot express gaze", [this, AdvanceBy]()
		{
			// Half-driving a rig would aim one eye and leave the other straight ahead, which reads
			// far worse than not moving at all.
			FFaceProfile Partial = FFaceProfile::CharacterCreator();
			Partial.GazeCurves = { TEXT("Eye_L_Look_L"), TEXT("Eye_L_Look_R") };	// only 2 of 8

			FCompanionGazeState Gaze;
			Gaze.SetSeed(7);
			Gaze.SetMode(FCompanionGazeState::EGazeMode::Thinking);
			AdvanceBy(Gaze, 1.0f);

			TMap<FName, float> Curves;
			Gaze.AccumulateInto(Partial, Curves);
			TestEqual(TEXT("an incomplete gaze set drives nothing"), Curves.Num(), 0);

			FFaceProfile None = FFaceProfile::CharacterCreator();
			None.GazeCurves.Reset();
			TMap<FName, float> NoCurves;
			Gaze.AccumulateInto(None, NoCurves);
			TestEqual(TEXT("an absent gaze set drives nothing"), NoCurves.Num(), 0);
		});

		It("keeps gaze curves disjoint from every other layer on both rigs", [this]()
		{
			// This disjointness is WHY the compositing order in ComposeFrame is safe to reason about
			// for gaze. If it ever stopped holding, gaze and emotion would start clipping each other
			// through FFaceProfile::Accumulate's clamp and the failure would look like a rig bug.
			for (const FFaceProfile* P : { &FFaceProfile::CharacterCreator(), &FFaceProfile::ARKit() })
			{
				TSet<FName> Others;
				for (const TPair<EViseme, FVisemeShape>& Pair : P->Shapes)
				{
					for (const FFaceCurve& C : Pair.Value.Curves) { Others.Add(C.Curve); }
				}
				for (const TPair<EEmotionType, FVisemeShape>& Pair : P->EmotionShapes)
				{
					for (const FFaceCurve& C : Pair.Value.Curves) { Others.Add(C.Curve); }
				}
				for (const FName& B : P->BlinkCurves) { Others.Add(B); }

				TestEqual(TEXT("the profile declares a full gaze set"), P->GazeCurves.Num(), 8);
				for (const FName& G : P->GazeCurves)
				{
					TestFalse(FString::Printf(TEXT("%s is gaze-only on %s"), *G.ToString(), *P->ProfileName.ToString()),
						Others.Contains(G));
				}

				// Eight DISTINCT names. A copy-paste slip that repeats one would silently halve the
				// rig's expressible directions.
				TSet<FName> Unique(P->GazeCurves);
				TestEqual(TEXT("the eight gaze curves are distinct"), Unique.Num(), 8);
			}
		});
	});

	Describe("saccades", [this, AdvanceBy]()
	{
		It("arrives ballistically inside a single 30fps frame", [this]()
		{
			// The defining property. Eyes do not glide: a saccade is 30-80ms and effectively
			// instantaneous at frame rate. Easing a pupil between fixations is the single most common
			// way synthetic gaze reads as a doll's eyes being turned by hand.
			FCompanionGazeState Gaze;
			Gaze.SetSeed(11);
			Gaze.SetMode(FCompanionGazeState::EGazeMode::Contact);

			Gaze.Advance(1.0f / 30.0f);

			TestEqual(TEXT("X reached its target within one frame"),
				static_cast<float>(Gaze.GetAim().X), static_cast<float>(Gaze.GetTarget().X), 1e-4f);
			TestEqual(TEXT("Y reached its target within one frame"),
				static_cast<float>(Gaze.GetAim().Y), static_cast<float>(Gaze.GetTarget().Y), 1e-4f);
		});

		It("moves at a bounded constant rate rather than snapping", [this]()
		{
			// The complement of the test above: it must still be an INTERPOLATION, not an
			// assignment. A snap would pass the ballistic test — it arrives, after all — and be
			// wrong at every frame rate above 30fps, jumping the eye the instant a target changed.
			//
			// Asserted as an upper bound on per-step movement rather than as "it has not arrived
			// yet". The obvious version of that assertion is FLAKY and it is worth saying why: a
			// quarter-saccade covers 0.045 * 0.25 * (1/0.045) = 0.25 units per axis, which is
			// further than most randomly chosen targets are away, so "it should not have arrived"
			// fails whenever the draw happens to be close. The rate bound holds for every target.
			constexpr float Step = FCompanionGazeState::SaccadeSeconds * 0.25f;
			const float MaxTravel = Step / FCompanionGazeState::SaccadeSeconds;	// == 0.25

			for (int32 Seed = 1; Seed <= 25; ++Seed)
			{
				FCompanionGazeState Gaze;
				Gaze.SetSeed(Seed);
				Gaze.SetMode(FCompanionGazeState::EGazeMode::Thinking);	// widest targets available
				Gaze.Advance(KINDA_SMALL_NUMBER);						// pick a target, move ~nothing

				const FVector2D Before = Gaze.GetAim();
				Gaze.Advance(Step);
				const FVector2D After = Gaze.GetAim();

				// KINDA_SMALL_NUMBER of slack for float accumulation only.
				TestTrue(TEXT("X moved no further than the constant rate allows"),
					FMath::Abs(After.X - Before.X) <= MaxTravel + KINDA_SMALL_NUMBER);
				TestTrue(TEXT("Y moved no further than the constant rate allows"),
					FMath::Abs(After.Y - Before.Y) <= MaxTravel + KINDA_SMALL_NUMBER);
			}
		});

		It("keeps re-aiming rather than settling", [this, AdvanceBy]()
		{
			// A gaze driver that picks one target and stops is indistinguishable from the dead stare
			// it replaced after the first half second.
			FCompanionGazeState Gaze;
			Gaze.SetSeed(31337);
			TSet<FString> DistinctTargets;
			for (int32 i = 0; i < 40; ++i)
			{
				AdvanceBy(Gaze, 0.5f);
				DistinctTargets.Add(Gaze.GetTarget().ToString());
			}
			TestTrue(TEXT("the eyes re-aim repeatedly over 20 seconds"), DistinctTargets.Num() > 3);
		});
	});

	Describe("conversational modes", [this, ForceOneSaccade, AdvanceBy]()
	{
		auto MeanTargetDistance = [ForceOneSaccade](FCompanionGazeState::EGazeMode Mode, int32 Samples) -> float
		{
			FCompanionGazeState Gaze;
			Gaze.SetSeed(20260802);
			double Sum = 0.0;
			for (int32 i = 0; i < Samples; ++i)
			{
				// Re-asserted every iteration, and NOT merely once before the loop. ForceOneSaccade
				// advances several seconds at a time, so a Thinking sample run crosses
				// ThinkingTimeoutSeconds long before it finishes and the backstop drops it to
				// Contact — which would quietly measure the wrong mode. SetMode is idempotent, so
				// this is a no-op except on exactly the iterations after a timeout fired.
				Gaze.SetMode(Mode);
				ForceOneSaccade(Gaze);
				Sum += Gaze.GetTarget().Size();
			}
			return static_cast<float>(Sum / Samples);
		};

		It("breaks eye contact more while speaking than while listening", [this, MeanTargetDistance]()
		{
			// The dyadic asymmetry: roughly 70% gaze as a listener, 40% as a speaker. Getting this
			// backwards produces a companion who stares while she talks and looks away while you do,
			// which is legible as wrong even to a viewer who cannot name why.
			const float Listening = MeanTargetDistance(FCompanionGazeState::EGazeMode::Contact, 400);
			const float Speaking = MeanTargetDistance(FCompanionGazeState::EGazeMode::Speaking, 400);

			TestTrue(FString::Printf(TEXT("speaking averts more than listening (%f > %f)"), Speaking, Listening),
				Speaking > Listening);
			// And listening is still genuinely ON the viewer, not merely less far off.
			TestTrue(TEXT("listening holds near the lens"), Listening < 0.10f);
		});

		It("averts furthest and upward while thinking", [this, MeanTargetDistance, ForceOneSaccade]()
		{
			const float Thinking = MeanTargetDistance(FCompanionGazeState::EGazeMode::Thinking, 400);
			const float Speaking = MeanTargetDistance(FCompanionGazeState::EGazeMode::Speaking, 400);
			TestTrue(TEXT("thinking averts furthest"), Thinking > Speaking);

			FCompanionGazeState Gaze;
			Gaze.SetSeed(5150);
			double SumY = 0.0;
			constexpr int32 Samples = 400;
			for (int32 i = 0; i < Samples; ++i)
			{
				Gaze.SetMode(FCompanionGazeState::EGazeMode::Thinking);	// see MeanTargetDistance
				ForceOneSaccade(Gaze);
				SumY += Gaze.GetTarget().Y;
			}
			// Upward-and-away is the recognisable planning break; downward reads as shame.
			TestTrue(TEXT("thinking biases the eyes upward on average"), (SumY / Samples) > 0.05);
		});

		It("gives up thinking on its own if the reply never arrives", [this, AdvanceBy]()
		{
			// The latch guard. Thinking is entered when a question goes to the model and left when
			// the reply drives the mouth — an event produced by a network round trip that is allowed
			// to fail. Without the backstop a dead backend leaves her staring into the middle
			// distance for the rest of the session, which is precisely the defect
			// FCompanionEmotionState's sustain timer already exists to prevent for expressions.
			FCompanionGazeState Gaze;
			Gaze.SetSeed(2468);
			Gaze.SetMode(FCompanionGazeState::EGazeMode::Thinking);
			TestEqual(TEXT("starts out thinking"), static_cast<int32>(Gaze.GetMode()),
				static_cast<int32>(FCompanionGazeState::EGazeMode::Thinking));

			// Still thinking well before the timeout: a slow model must not be cut short.
			AdvanceBy(Gaze, FCompanionGazeState::ThinkingTimeoutSeconds * 0.5f, 0.05f);
			TestEqual(TEXT("a slow model is not cut short"), static_cast<int32>(Gaze.GetMode()),
				static_cast<int32>(FCompanionGazeState::EGazeMode::Thinking));

			AdvanceBy(Gaze, FCompanionGazeState::ThinkingTimeoutSeconds * 0.6f, 0.05f);
			TestEqual(TEXT("recovers to eye contact once the timeout passes"), static_cast<int32>(Gaze.GetMode()),
				static_cast<int32>(FCompanionGazeState::EGazeMode::Contact));
		});

		It("ignores a repeated mode so a per-frame caller cannot freeze the eyes", [this]()
		{
			// The runtime subsystem pushes the mode every frame — it has no cheap way to know the
			// conversational state changed. If SetMode restarted the fixation on each call, the eyes
			// would be permanently mid-saccade toward a target that was replaced before they arrived.
			FCompanionGazeState Gaze;
			Gaze.SetSeed(77);
			Gaze.SetMode(FCompanionGazeState::EGazeMode::Speaking);
			const FVector2D First = Gaze.GetTarget();

			for (int32 i = 0; i < 50; ++i)
			{
				Gaze.SetMode(FCompanionGazeState::EGazeMode::Speaking);
			}
			TestEqual(TEXT("re-setting the same mode does not re-aim"),
				static_cast<float>(Gaze.GetTarget().X), static_cast<float>(First.X), KINDA_SMALL_NUMBER);
			TestEqual(TEXT("re-setting the same mode does not re-aim"),
				static_cast<float>(Gaze.GetTarget().Y), static_cast<float>(First.Y), KINDA_SMALL_NUMBER);
		});

		It("re-aims immediately when the mode genuinely changes", [this]()
		{
			// The glance IS the signal that she started thinking. Deferring it to the end of the
			// current fixation would decouple it from the event that caused it.
			FCompanionGazeState Gaze;
			Gaze.SetSeed(88);
			Gaze.Advance(0.01f);
			const FVector2D Before = Gaze.GetTarget();
			Gaze.SetMode(FCompanionGazeState::EGazeMode::Thinking);
			TestTrue(TEXT("a mode change re-aims at once"),
				!Gaze.GetTarget().Equals(Before, KINDA_SMALL_NUMBER));
		});
	});

	Describe("the kill switch", [this, MakeLookup]()
	{
		It("is ON when unset, because a dead stare is the regression this fixes", [this, MakeLookup]()
		{
			TestTrue(TEXT("absent means enabled"), FCompanionGazeState::IsEnabled(MakeLookup({})));
		});

		It("is off only for an explicit 0 or false", [this, MakeLookup]()
		{
			TestFalse(TEXT("0 disables"), FCompanionGazeState::IsEnabled(MakeLookup({ { TEXT("MHLLM_GAZE"), TEXT("0") } })));
			TestFalse(TEXT("false disables"), FCompanionGazeState::IsEnabled(MakeLookup({ { TEXT("MHLLM_GAZE"), TEXT("false") } })));
			TestFalse(TEXT("FALSE disables"), FCompanionGazeState::IsEnabled(MakeLookup({ { TEXT("MHLLM_GAZE"), TEXT("FALSE") } })));
			// A compose file interpolating an unset variable leaves whitespace behind.
			TestFalse(TEXT("padded 0 disables"), FCompanionGazeState::IsEnabled(MakeLookup({ { TEXT("MHLLM_GAZE"), TEXT("  0  ") } })));
			// A typo must fail toward the working behaviour for a cosmetic subsystem.
			TestTrue(TEXT("a typo stays enabled"), FCompanionGazeState::IsEnabled(MakeLookup({ { TEXT("MHLLM_GAZE"), TEXT("no") } })));
			TestTrue(TEXT("1 stays enabled"), FCompanionGazeState::IsEnabled(MakeLookup({ { TEXT("MHLLM_GAZE"), TEXT("1") } })));
		});
	});

	Describe("through the face driver", [this]()
	{
		// Blink off: it fires on a random timer off the GLOBAL stream and would add eyelid curves to
		// assertions below. Emotion off: it is a separate layer with its own spec.
		auto MakeFace = []() -> UCompanionFaceComponent*
		{
			UCompanionFaceComponent* Face = NewObject<UCompanionFaceComponent>(GetTransientPackage());
			Face->SetProfile(FFaceProfile::CharacterCreator());
			Face->SetBlinkEnabled(false);
			Face->SetEmotionEnabled(false);
			Face->SetGazeEnabled(true);
			return Face;
		};

		It("puts eye curves on the face while idle", [this, MakeFace]()
		{
			// The whole point: with nothing being said and no expression held, the eyes still move.
			// Before this feature the idle frame was completely empty.
			UCompanionFaceComponent* Face = MakeFace();
			Face->SetGazeSeed(123);
			Face->SetGazeMode(FCompanionGazeState::EGazeMode::Thinking);

			const TSet<FName> GazeNames(FFaceProfile::CharacterCreator().GazeCurves);
			bool bSawGaze = false;
			for (int32 i = 0; i < 120 && !bSawGaze; ++i)
			{
				for (const TPair<FName, float>& Pair : Face->ComposeFrame(1.0f / 60.0f))
				{
					if (GazeNames.Contains(Pair.Key)) { bSawGaze = true; break; }
				}
			}
			TestTrue(TEXT("an idle face still moves its eyes"), bSawGaze);
		});

		It("emits no eye curves at all once gaze is disabled", [this, MakeFace]()
		{
			UCompanionFaceComponent* Face = MakeFace();
			Face->SetGazeSeed(456);
			Face->SetGazeMode(FCompanionGazeState::EGazeMode::Thinking);
			Face->ComposeFrame(0.5f);			// let it deflect first
			Face->SetGazeEnabled(false);

			const TSet<FName> GazeNames(FFaceProfile::CharacterCreator().GazeCurves);
			for (int32 i = 0; i < 60; ++i)
			{
				for (const TPair<FName, float>& Pair : Face->ComposeFrame(1.0f / 60.0f))
				{
					TestFalse(TEXT("a disabled gaze writes nothing"), GazeNames.Contains(Pair.Key));
				}
			}
		});

		It("keeps the eyes moving underneath speech rather than freezing them", [this, MakeFace]()
		{
			// Gaze and visemes occupy disjoint curves, so an utterance must not suppress the eyes.
			// A face whose eyes lock the instant she starts talking is the exact uncanny artefact
			// this layer exists to remove.
			UCompanionFaceComponent* Face = MakeFace();
			Face->SetGazeSeed(789);
			Face->SetGazeMode(FCompanionGazeState::EGazeMode::Speaking);

			TArray<FVisemeSpan> Track;
			Track.Add(FVisemeSpan(0.0f, 2.0f, EViseme::AA));
			Face->PlayVisemeTrack(Track);

			const TSet<FName> GazeNames(FFaceProfile::CharacterCreator().GazeCurves);
			bool bSawGazeWhileSpeaking = false;
			bool bSawViseme = false;
			for (int32 i = 0; i < 90; ++i)
			{
				for (const TPair<FName, float>& Pair : Face->ComposeFrame(1.0f / 60.0f))
				{
					if (GazeNames.Contains(Pair.Key)) { bSawGazeWhileSpeaking = true; }
					if (Pair.Key == TEXT("V_Open")) { bSawViseme = true; }
				}
			}
			TestTrue(TEXT("the mouth was actually driven"), bSawViseme);
			TestTrue(TEXT("the eyes kept moving under speech"), bSawGazeWhileSpeaking);
		});
	});
}
