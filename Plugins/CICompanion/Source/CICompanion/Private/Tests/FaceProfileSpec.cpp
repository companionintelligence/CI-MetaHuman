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
