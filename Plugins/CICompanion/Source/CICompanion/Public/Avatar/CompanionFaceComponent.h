#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Avatar/FaceProfile.h"
#include "CompanionFaceComponent.generated.h"

class USkeletalMeshComponent;

// Drives a character's face by writing morph-target weights directly.
//
// Why direct morph targets rather than the existing OnSetVisemeWeights Blueprint event:
// AMetaHumanCompanionCharacter only fires BlueprintImplementableEvents, which do nothing unless
// a hand-authored BP_CompanionCharacter subclass wires them to a rig. The diorama map is authored
// headlessly by Python in a CONTENT-ONLY project (HANDOFF.md Lesson 6) that cannot reference
// plugin C++ or carry a Blueprint at all. Since the resident's 15 visemes are real morph targets
// (not merely PoseAsset entries), a plain C++ component can move the face with no Blueprint, no
// AnimBP node and no Control Rig — which is the only route that survives that constraint.
UCLASS(ClassGroup = (MetaHumanLLM), meta = (BlueprintSpawnableComponent))
class CICOMPANION_API UCompanionFaceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCompanionFaceComponent();

	// The mesh whose morph targets are driven. Safe to pass null; the component simply idles.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Face")
	void SetTargetMesh(USkeletalMeshComponent* InMesh);

	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Face")
	USkeletalMeshComponent* GetTargetMesh() const { return TargetMesh; }

	void SetProfile(const FFaceProfile& InProfile);
	const FFaceProfile& GetProfile() const { return Profile; }

	// Applies a set of simultaneously-active visemes (coarticulation: overlapping shapes sum).
	// Every curve the profile knows about is zeroed first, so a shape can never stick on when
	// the next frame stops mentioning it.
	void ApplyVisemes(const TMap<EViseme, float>& Weights);

	// Escape hatch for callers that already speak the rig's own curve names (e.g. emotion poses).
	void ApplyCurves(const TMap<FName, float>& Curves);

	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Face")
	void ClearFace();

	// How many of this profile's curves actually exist on the bound mesh. This is deliberately
	// exposed rather than assumed: a rig that has these names in the editor can still ship
	// without them if morph targets are stripped at import/cook, and the failure is silent —
	// SetMorphTarget on an unknown name is a no-op, so the face just never moves.
	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Face")
	int32 GetResolvedCurveCount() const { return ResolvedCurveCount; }

	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Face")
	int32 GetProfileCurveCount() const { return Profile.AllCurves().Num(); }

	// Cycles every viseme in turn, holding each for SecondsPerViseme. This is the milestone-1
	// proof that the whole face path works end-to-end in a COOKED Linux build, before any TTS
	// exists: cook it, capture frames, and look at the mouth.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Face")
	void StartSelfTest(float SecondsPerViseme = 0.4f);

	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Face")
	void StopSelfTest();

	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Face")
	bool IsSelfTesting() const { return bSelfTesting; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	// Counts how many profile curves the bound mesh actually provides, and logs the missing ones
	// once. Called on bind rather than per frame.
	void ResolveCurvesAgainstMesh();

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> TargetMesh;

	FFaceProfile Profile;

	int32 ResolvedCurveCount = 0;

	bool bSelfTesting = false;
	float SelfTestSecondsPerViseme = 0.4f;
	float SelfTestElapsed = 0.0f;
};
