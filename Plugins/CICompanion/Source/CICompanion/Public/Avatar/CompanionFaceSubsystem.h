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
