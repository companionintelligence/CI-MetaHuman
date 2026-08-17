#include "Misc/AutomationTest.h"
#include "Animation/MorphTarget.h"
#include "Avatar/FaceProfile.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/UObjectGlobals.h"

BEGIN_DEFINE_SPEC(FMorphDataSpec, "MetaHumanLocalLLM.Backend.MorphData", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FMorphDataSpec)

// Does the resident's mesh actually carry the GEOMETRY the face driver needs?
//
// Every other check in this suite is pure arithmetic on the profile tables. This one reaches for
// the real asset, because the profile can be perfect and the face still never move: a morph target
// that exists by NAME but has no vertex deltas accepts every SetMorphTarget, returns every
// GetMorphTarget, survives every frame, and deforms nothing. That combination is invisible to a
// log -- UCompanionFaceComponent reported "all 42 curves resolved" against exactly such a mesh --
// and it cost a long debugging session that ruled out tick order, the GPU morph path, Nanite, LOD
// and the anim graph before anyone thought to count the deltas.
//
// Runs in the EDITOR, so it reads the SOURCE asset rather than a cooked one. That is deliberate:
// it answers "is the content right" separately from "did the cook preserve it", which are
// different bugs with different owners.
void FMorphDataSpec::Define()
{
	Describe("The resident's skeletal mesh", [this]()
	{
		It("carries real vertex deltas for every curve the CC profile drives", [this]()
		{
			const TCHAR* MeshPath = TEXT("/Game/CC_ControlRig_Nia/RLContent/Nia/SK_Nia.SK_Nia");
			USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath);
			if (!Mesh)
			{
				// Not a failure: a content-light checkout legitimately has no Nia. Say so and stop,
				// rather than reporting a red suite for an absent 232 MB asset.
				AddInfo(FString::Printf(TEXT("%s not present in this checkout; skipping."), MeshPath));
				return;
			}

			const FFaceProfile& Profile = FFaceProfile::CharacterCreator();
			const TArray<FName> Curves = Profile.AllCurves();
			TestTrue("profile declares curves", Curves.Num() > 0);

			TArray<FString> NameMissing;
			TArray<FString> DeltaMissing;
			int32 WithData = 0;

			for (const FName& Curve : Curves)
			{
				const UMorphTarget* MT = Mesh->FindMorphTarget(Curve);
				if (!MT)
				{
					NameMissing.Add(Curve.ToString());
					continue;
				}
				const TArray<FMorphTargetLODModel>& LODs = MT->GetMorphLODModels();
				const int32 Deltas = (LODs.Num() > 0) ? LODs[0].Vertices.Num() : 0;
				if (Deltas > 0) { ++WithData; }
				else { DeltaMissing.Add(Curve.ToString()); }
			}

			AddInfo(FString::Printf(TEXT("%d/%d curves have LOD0 vertex deltas on %s"),
				WithData, Curves.Num(), *Mesh->GetName()));

			TestEqual(FString::Printf(TEXT("every profile curve exists by name (missing: %s)"),
				*FString::Join(NameMissing, TEXT(", "))), NameMissing.Num(), 0);

			// THE ASSERTION THAT MATTERS. Name resolution is not deformation capability, and
			// conflating the two is what let a geometry-free rig report a clean bill of health.
			TestEqual(FString::Printf(TEXT("every profile curve has vertex deltas (empty: %s)"),
				*FString::Join(DeltaMissing, TEXT(", "))), DeltaMissing.Num(), 0);
		});
	});
}
