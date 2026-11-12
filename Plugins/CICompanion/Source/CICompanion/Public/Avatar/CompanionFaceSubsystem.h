#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CompanionFaceSubsystem.generated.h"

class UCompanionFaceComponent;

// Attaches the face driver to the resident at runtime.
//
// The diorama map is authored headlessly by Python in a content-only project, so it cannot
// reference plugin C++ classes or embed a Blueprint (HANDOFF.md Lesson 6). The map therefore
// stays purely visual — a plain SkeletalMeshActor — and behaviour is bolted on here at
// BeginPlay instead. This keeps the authoring pipeline free of engine-module dependencies while
// still giving the cooked game a driven face.
//
// The resident is located by ACTOR TAG, not by label: labels are editor-only and do not survive
// a cook, so a label-based lookup would work in the editor and silently find nothing in the
// shipped build.
UCLASS()
class CICOMPANION_API UCompanionFaceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// The tag author_diorama.py stamps on the resident actor.
	static const FName ResidentTag;

	// Prefix of the second tag author_diorama.py stamps on the resident, naming the rig its face
	// carries: "FaceProfile=CC" on the Reallusion "Nia" resident, "FaceProfile=ARKit" on the
	// MetaHuman one, none on the default mannequin (no facial rig, so whatever resolves, 0 curves
	// bind). The map says which rig it placed because the authoring script is the only thing that
	// knows; before this the C++ assumed CC (Nia's rig) for every map.
	static const FString ProfileTagPrefix;

	// The face profile to drive the resident with, by precedence:
	//   1. MHLLM_FACE_PROFILE, when set and non-empty (an explicit operator choice always wins);
	//   2. the resident's "FaceProfile=<name>" tag;
	//   3. "CC" -- what every map authored before the tag existed was built for, so an old map
	//      behaves exactly as it always did.
	// Returned as a name rather than a profile so the console can show it; FFaceProfile::ByName
	// resolves it. Static and side-effect free so the runtime console and the specs can ask the
	// same question the face driver does.
	static FString ResolveProfileName(const AActor* Resident);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Face")
	UCompanionFaceComponent* GetFace() const { return Face; }

	// Public because this is the seam the project's worst shipped bug lived in: for weeks nothing
	// bound to the resident and the only symptom was a face that never moved. A const query with
	// no side effects, so exposing it costs nothing and lets the selection logic be tested.
	//
	// Tagged actor first; failing that, the skeletal-mesh actor with the most morph targets,
	// which in this scene is unambiguously the character rather than a prop.
	AActor* FindResident(UWorld& InWorld) const;

private:

	UPROPERTY(Transient)
	TObjectPtr<UCompanionFaceComponent> Face;
};
