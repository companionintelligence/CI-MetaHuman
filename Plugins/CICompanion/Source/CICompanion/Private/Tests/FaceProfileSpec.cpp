#include "Misc/AutomationTest.h"
#include "Avatar/FaceProfile.h"

BEGIN_DEFINE_SPEC(FFaceProfileSpec, "MetaHumanLocalLLM.Backend.FaceProfile", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FFaceProfileSpec)

// Pure table/arithmetic tests — no mesh, no rig, no engine world, so this runs headlessly in CI
// alongside the rest of the backend suite.
void FFaceProfileSpec::Define()
{
	Describe("CharacterCreator profile", [this]()
	{
		It("covers every viseme in the vocabulary", [this]()
		{
			const FFaceProfile& P = FFaceProfile::CharacterCreator();
			for (uint8 I = 0; I <= static_cast<uint8>(EViseme::OU); ++I)
			{
				TestTrue(FString::Printf(TEXT("viseme %d mapped"), I), P.Shapes.Contains(static_cast<EViseme>(I)));
			}
		});

		It("uses Reallusion V_* names, which is what the resident's rig actually has", [this]()
		{
			TMap<FName, float> Curves;
			FFaceProfile::CharacterCreator().Accumulate(EViseme::AA, 1.0f, Curves);
			TestTrue("V_Open driven", Curves.Contains(TEXT("V_Open")));
		});

		It("does not emit ARKit names (the resident has none of them)", [this]()
		{
			for (const FName& Curve : FFaceProfile::CharacterCreator().AllCurves())
			{
				TestFalse("no jawOpen", Curve == FName(TEXT("jawOpen")));
				TestFalse("no mouthPucker", Curve == FName(TEXT("mouthPucker")));
			}
		});

		// REGRESSION GUARD. Every vowel previously satisfied "Jaw_Open is present and > 0" while
		// carrying so little of it (0.10 on OU, the most common voiced shape) that the jaw never
		// visibly moved: a duration-weighted mean of 0.094 across a real aligned utterance, and
		// screen-captured mouth darkness during speech statistically identical to silence. The
		// face read as a fish -- lips rounding, mouth never opening -- and NOTHING in this suite
		// failed, because presence and sign were all anyone asserted. Assert MAGNITUDE.
		It("opens the jaw enough on vowels to be visible, not merely non-zero", [this]()
		{
			const FFaceProfile& P = FFaceProfile::CharacterCreator();
			// Floors, not equalities: re-tuning the shapes is expected, flattening them is not.
			const TArray<TPair<EViseme, float>> Floors = {
				{ EViseme::AA, 0.60f },   // the open vowel; if this is low nothing else matters
				{ EViseme::OH, 0.40f },
				{ EViseme::OU, 0.30f },   // the fish case: full purse over a shut jaw
				{ EViseme::E,  0.30f },
				{ EViseme::IH, 0.20f },
			};
			for (const TPair<EViseme, float>& Floor : Floors)
			{
				TMap<FName, float> Curves;
				P.Accumulate(Floor.Key, 1.0f, Curves);
				const float Jaw = Curves.FindRef(TEXT("Jaw_Open"));
				TestTrue(FString::Printf(TEXT("viseme %d drives Jaw_Open >= %.2f (got %.3f)"),
					static_cast<int32>(Floor.Key), Floor.Value, Jaw), Jaw >= Floor.Value);
			}
		});

		// A pure purse shuts the lips over whatever the jaw is doing, which is what made "moo"
		// and "oh" render identically. Rounding must leave room for aperture to read through it.
		It("never rounds the lips at full strength, so aperture still reads", [this]()
		{
			const FFaceProfile& P = FFaceProfile::CharacterCreator();
			for (const EViseme V : { EViseme::OU, EViseme::OH })
			{
				TMap<FName, float> Curves;
				P.Accumulate(V, 1.0f, Curves);
				const float Round = Curves.FindRef(TEXT("V_Tight_O"));
				TestTrue(FString::Printf(TEXT("viseme %d rounds below full (got %.3f)"),
					static_cast<int32>(V), Round), Round < 1.0f);
			}
		});
	});

	Describe("ARKit profile", [this]()
	{
		It("covers every viseme too, so either rig is drivable from one mapper", [this]()
		{
			const FFaceProfile& P = FFaceProfile::ARKit();
			for (uint8 I = 0; I <= static_cast<uint8>(EViseme::OU); ++I)
			{
				TestTrue(FString::Printf(TEXT("viseme %d mapped"), I), P.Shapes.Contains(static_cast<EViseme>(I)));
			}
		});

		It("uses ARKit names rather than V_*", [this]()
		{
			TMap<FName, float> Curves;
			FFaceProfile::ARKit().Accumulate(EViseme::AA, 1.0f, Curves);
			TestTrue("jawOpen driven", Curves.Contains(TEXT("jawOpen")));
		});
	});

	Describe("Accumulate", [this]()
	{
		It("scales contributions by the activation weight", [this]()
		{
			TMap<FName, float> Full, Half;
			FFaceProfile::CharacterCreator().Accumulate(EViseme::AA, 1.0f, Full);
			FFaceProfile::CharacterCreator().Accumulate(EViseme::AA, 0.5f, Half);
			TestTrue("half is lower", Half[TEXT("V_Open")] < Full[TEXT("V_Open")]);
		});

		It("sums overlapping visemes so coarticulation blends rather than overwrites", [this]()
		{
			// OH and OU share V_Tight_O; both active must read higher than either alone.
			TMap<FName, float> Both, Single;
			const FFaceProfile& P = FFaceProfile::CharacterCreator();
			P.Accumulate(EViseme::OH, 0.4f, Both);
			P.Accumulate(EViseme::OU, 0.4f, Both);
			P.Accumulate(EViseme::OH, 0.4f, Single);
			TestTrue("blended is stronger", Both[TEXT("V_Tight_O")] > Single[TEXT("V_Tight_O")]);
		});

		It("clamps to 1 so stacked shapes cannot over-drive a curve", [this]()
		{
			TMap<FName, float> Curves;
			const FFaceProfile& P = FFaceProfile::CharacterCreator();
			for (int32 I = 0; I < 5; ++I)
			{
				P.Accumulate(EViseme::OU, 1.0f, Curves);
			}
			TestTrue("clamped", Curves[TEXT("V_Tight_O")] <= 1.0f);
		});

		It("ignores an unmapped viseme instead of inventing curves", [this]()
		{
			FFaceProfile Empty;
			TMap<FName, float> Curves;
			Empty.Accumulate(EViseme::AA, 1.0f, Curves);
			TestEqual("nothing emitted", Curves.Num(), 0);
		});
	});

	Describe("ByName", [this]()
	{
		It("resolves ARKit", [this]()
		{
			TestEqual("arkit", FFaceProfile::ByName(TEXT("ARKit")).ProfileName, FName(TEXT("ARKit")));
		});

		It("defaults to CC, the resident's rig, for anything unrecognized", [this]()
		{
			TestEqual("cc", FFaceProfile::ByName(TEXT("nonsense")).ProfileName, FName(TEXT("CC")));
		});
	});

	Describe("AllCurves", [this]()
	{
		It("deduplicates curves shared across visemes", [this]()
		{
			const FFaceProfile& P = FFaceProfile::CharacterCreator();
			const TArray<FName> All = P.AllCurves();
			TSet<FName> Unique(All);
			TestEqual("no duplicates", All.Num(), Unique.Num());
			TestTrue("non-empty", All.Num() > 0);
		});
	});
}
