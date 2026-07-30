#include "Misc/AutomationTest.h"
#include "Avatar/FaceProfile.h"
#include "Voice/CIVisemeInterop.h"

BEGIN_DEFINE_SPEC(FFaceVocabularySpec, "MetaHumanLocalLLM.Backend.FaceVocabulary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FFaceVocabularySpec)

// Guards the curve NAMES the face driver writes.
//
// This is the highest-value cheap test in the project, because the failure it catches is completely
// silent: USkeletalMeshComponent::SetMorphTarget on a name the mesh does not have does nothing at
// all -- no warning, no error, no exception. A single mistyped curve just means that part of the
// face never moves, and it looks exactly like a rig problem rather than a typo.
//
// ANIMATION-PLAN.md section 5.3 asks for precisely this ("all FNames exist in a curated ARKit-52
// allow-list (guards the mouthSmileLeft vs mouthSmile_L naming trap)").
void FFaceVocabularySpec::Define()
{
	Describe("ARKit-52 allow-list", [this]()
	{
		It("contains exactly 52 names", [this]()
		{
			TestEqual("count", CIArkit::Names52().Num(), CIArkit::ArkitBlendShapeCount);
		});

		It("has no duplicates", [this]()
		{
			const TSet<FName> Unique(CIArkit::Names52());
			TestEqual("unique count", Unique.Num(), CIArkit::Names52().Num());
		});

		It("starts at eyeBlinkLeft and ends at tongueOut", [this]()
		{
			// Pins the list to EARFaceBlendShape declaration order. If anyone re-sorts it
			// alphabetically for tidiness, index-based interop with an ARKit source silently breaks.
			TestEqual("first", CIArkit::Names52()[0], FName(TEXT("eyeBlinkLeft")));
			TestEqual("last", CIArkit::Names52()[51], FName(TEXT("tongueOut")));
		});

		It("records that the engine enum has 61 entries, not 52", [this]()
		{
			// THE TRAP. EARFaceBlendShape declares 61 enumerators before MAX. Entries 53-61 are
			// HeadYaw/HeadPitch/HeadRoll and LeftEye/RightEye Yaw/Pitch/Roll -- head and eye POSE
			// channels, not blendshapes. Identical in 5.5.4 and 5.8.1.
			//
			// Any `for (i = 0; i < 52; ++i)` over StaticEnum<EARFaceBlendShape>(), or any
			// `Num() == 52` assertion, is mis-sized. This test exists so the two numbers are stated
			// side by side and can never quietly be conflated again.
			TestEqual("engine enumerators", CIArkit::EngineEnumeratorCount, 61);
			TestEqual("true blendshapes", CIArkit::ArkitBlendShapeCount, 52);
			TestTrue("the two differ", CIArkit::EngineEnumeratorCount != CIArkit::ArkitBlendShapeCount);
		});

		It("recognises canonical names and rejects near-misses", [this]()
		{
			TestTrue("jawOpen", CIArkit::IsCanonical(TEXT("jawOpen")));
			TestTrue("mouthSmileLeft", CIArkit::IsCanonical(TEXT("mouthSmileLeft")));

			// The exact naming trap ANIMATION-PLAN.md calls out: the underscore-suffix convention is
			// a different vendor's spelling and resolves against nothing on an ARKit rig.
			TestFalse("mouthSmile_L", CIArkit::IsCanonical(TEXT("mouthSmile_L")));
			// A head pose channel is in the engine enum but is NOT one of the 52 blendshapes.
			TestFalse("headYaw", CIArkit::IsCanonical(TEXT("headYaw")));
		});

		It("matches case-insensitively, because FName does", [this]()
		{
			// Asserted deliberately rather than left implicit. FName comparison ignores case, so
			// FName("JawOpen") == FName("jawOpen") and this check cannot be made case-sensitive
			// without abandoning FName.
			//
			// That is the right behaviour here, and not merely a limitation: the engine looks
			// morph targets up by FName too, so a rig authored with different capitalisation still
			// resolves. Case only matters when a name is serialised OUT to a sibling service in
			// JavaScript or C#, where string comparison IS case-sensitive -- covered by the
			// "preserves canonical spelling" test below.
			TestTrue("JawOpen matches jawOpen", CIArkit::IsCanonical(TEXT("JawOpen")));
			TestTrue("MOUTHSMILELEFT matches", CIArkit::IsCanonical(TEXT("MOUTHSMILELEFT")));
		});

		It("preserves canonical spelling for the wire", [this]()
		{
			// What actually has to be exact. The stored FName keeps its authored capitalisation, so
			// ToString() hands a sibling service the spelling it expects even though lookups here
			// are case-insensitive.
			const int32 Index = CIArkit::Names52().IndexOfByKey(FName(TEXT("jawOpen")));
			TestTrue("jawOpen present", Index != INDEX_NONE);
			TestEqual("exact spelling", CIArkit::Names52()[Index].ToString(), FString(TEXT("jawOpen")));

			const int32 SmileIndex = CIArkit::Names52().IndexOfByKey(FName(TEXT("mouthSmileLeft")));
			TestEqual("exact spelling", CIArkit::Names52()[SmileIndex].ToString(), FString(TEXT("mouthSmileLeft")));
		});
	});

	Describe("FFaceProfile::ARKit()", [this]()
	{
		It("drives only canonical ARKit blendshape names", [this]()
		{
			// The real payload of this file: every curve the ARKit profile can ever write must be a
			// name a MetaHuman face actually carries.
			TArray<FString> Offenders;
			for (const FName& Curve : FFaceProfile::ARKit().AllCurves())
			{
				if (!CIArkit::IsCanonical(Curve))
				{
					Offenders.Add(Curve.ToString());
				}
			}
			TestEqual(*FString::Printf(TEXT("non-canonical ARKit curves: %s"), *FString::Join(Offenders, TEXT(", "))),
				Offenders.Num(), 0);
		});

		It("uses canonical names for its blink curves", [this]()
		{
			// Blink is held separately from Shapes (it composites over speech rather than being one
			// of the shapes speech selects), so AllCurves() coverage alone would not prove this if
			// the profile were ever restructured.
			for (const FName& Blink : FFaceProfile::ARKit().BlinkCurves)
			{
				TestTrue(*FString::Printf(TEXT("blink curve %s is canonical"), *Blink.ToString()),
					CIArkit::IsCanonical(Blink));
			}
			TestEqual("two eyelids", FFaceProfile::ARKit().BlinkCurves.Num(), 2);
		});

		It("defines a shape for every viseme in the vocabulary", [this]()
		{
			// A missing entry is silent: FFaceProfile::Accumulate contributes nothing for an unknown
			// viseme, so that speech class would just produce a motionless mouth.
			for (int32 Index = 0; Index <= static_cast<int32>(EViseme::OU); ++Index)
			{
				const EViseme Viseme = static_cast<EViseme>(Index);
				TestTrue(*FString::Printf(TEXT("ARKit shape for viseme %d"), Index),
					FFaceProfile::ARKit().Shapes.Contains(Viseme));
			}
		});
	});

	Describe("FFaceProfile::CharacterCreator()", [this]()
	{
		It("defines a shape for every viseme in the vocabulary", [this]()
		{
			for (int32 Index = 0; Index <= static_cast<int32>(EViseme::OU); ++Index)
			{
				const EViseme Viseme = static_cast<EViseme>(Index);
				TestTrue(*FString::Printf(TEXT("CC shape for viseme %d"), Index),
					FFaceProfile::CharacterCreator().Shapes.Contains(Viseme));
			}
		});

		It("does NOT use ARKit names", [this]()
		{
			// The two profiles are deliberately disjoint vocabularies, not variants of one. The
			// resident "Nia" was probed and carries all 15 CC viseme morphs and 0 of 10 sampled ARKit
			// names (Docs/reference/nia-morph-targets.txt), which is why the rig names had to move
			// behind a profile at all. If a CC curve ever matched an ARKit name it would mean the two
			// tables had been cross-contaminated.
			for (const FName& Curve : FFaceProfile::CharacterCreator().AllCurves())
			{
				TestFalse(*FString::Printf(TEXT("CC curve %s is not an ARKit name"), *Curve.ToString()),
					CIArkit::IsCanonical(Curve));
			}
		});

		It("includes blink curves in AllCurves", [this]()
		{
			// AllCurves() is what the driver zeroes each frame; omitting the blink curves would leave
			// a blink latched shut the moment the eyes closed. FaceProfile.cpp:87-92 handles this
			// deliberately, so it is worth pinning.
			const TArray<FName> All = FFaceProfile::CharacterCreator().AllCurves();
			for (const FName& Blink : FFaceProfile::CharacterCreator().BlinkCurves)
			{
				TestTrue(*FString::Printf(TEXT("AllCurves contains %s"), *Blink.ToString()), All.Contains(Blink));
			}
		});
	});

	Describe("FFaceProfile::ByName", [this]()
	{
		It("selects ARKit by name, case-insensitively", [this]()
		{
			TestEqual("ARKit", FFaceProfile::ByName(TEXT("ARKit")).ProfileName, FName(TEXT("ARKit")));
			TestEqual("arkit lowercase", FFaceProfile::ByName(TEXT("arkit")).ProfileName, FName(TEXT("ARKit")));
		});

		It("defaults to CC for anything else", [this]()
		{
			// CC is the resident's rig, so an unrecognised or empty setting must not leave the face
			// pointed at a vocabulary the mesh does not have.
			TestEqual("CC", FFaceProfile::ByName(TEXT("CC")).ProfileName, FName(TEXT("CC")));
			TestEqual("empty", FFaceProfile::ByName(TEXT("")).ProfileName, FName(TEXT("CC")));
			TestEqual("nonsense", FFaceProfile::ByName(TEXT("Wobble")).ProfileName, FName(TEXT("CC")));
		});
	});
}
