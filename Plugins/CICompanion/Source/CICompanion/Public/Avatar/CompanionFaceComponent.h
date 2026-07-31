#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Avatar/CompanionEmotionState.h"
#include "Avatar/FaceProfile.h"
#include "Voice/VisemePlanner.h"
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

	// Speaks a planned viseme track (see FVisemePlanner). Replaces any track already playing, so a
	// new reply interrupts the old one rather than the two summing into mush.
	//
	// StartOffsetSeconds enters the track partway through, for the case where the AUDIO began
	// before the track was ready — waiting on the phoneme aligner, typically. Without it the whole
	// mouth would run late by exactly however long the wait was, which is the drift the alignment
	// was fetched to remove.
	void PlayVisemeTrack(const TArray<struct FVisemeSpan>& Track, float StartOffsetSeconds = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Face")
	void StopSpeaking();

	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Face")
	bool IsSpeaking() const { return bSpeaking; }

	// Involuntary blinking. Cheap, and its absence is precisely what makes an otherwise good
	// render read as a mannequin — a still face with open eyes looks dead in a way a viewer
	// notices immediately even when they cannot say why.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Face")
	void SetBlinkEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Face")
	bool IsBlinkEnabled() const { return bBlinkEnabled; }

	// Sets the sustained emotional bias, blended in over ~0.4s and held for the utterance.
	//
	// This is NOT one of the shapes speech selects between: it is composited ON TOP of the visemes,
	// on curves the profile deliberately keeps disjoint from them (brow, cheek, eye). An emotion
	// that competed with speech for the same curves would smear every word she said, and swapping
	// wholesale between "talking" and "feeling" is what makes a driven face read as a puppet.
	// Ignored entirely when emotion is disabled — see SetEmotionEnabled.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Face")
	void SetEmotion(EEmotionType Emotion, float Intensity = 1.0f);

	// Begins letting go: holds the expression for HoldSeconds, then blends back to neutral. Called
	// automatically when an utterance ends, so an expression cannot latch on for the session.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Face")
	void ReleaseEmotion(float HoldSeconds = 1.2f);

	// Drops the expression immediately, with no blend.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Face")
	void ResetEmotion();

	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Face")
	EEmotionType GetEmotion() const { return Emotion.GetEmotion(); }

	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Face")
	float GetEmotionWeight() const { return Emotion.GetWeight(); }

	// Kill switch, resolved from MHLLM_EMOTION at construction and overridable at runtime. Turning
	// it off also drops whatever is currently on the face, since a disabled subsystem that leaves
	// its last frame latched on the mesh is indistinguishable from one that is still running.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Face")
	void SetEmotionEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Face")
	bool IsEmotionEnabled() const { return bEmotionEnabled; }

	// Advances every channel by one frame and returns the curve set they compose to: speech (or the
	// self-test) underneath, blink and the emotional bias on top.
	//
	// Public and separate from TickComponent purely so the COMPOSITING ORDER is testable — it
	// touches no mesh, no world and no tick group, which is what lets the whole face pipeline be
	// exercised headlessly in CI rather than only by looking at a cooked build.
	TMap<FName, float> ComposeFrame(float DeltaTime);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	// Counts how many profile curves the bound mesh actually provides, and logs the missing ones
	// once. Called on bind rather than per frame.
	void ResolveCurvesAgainstMesh();

	// Advances the blink clock and returns the current eyelid closure, 0..1.
	float TickBlink(float DeltaTime);

	// Tears down the speech state WITHOUT touching the mesh, and starts the emotion decay. Split
	// out of StopSpeaking so the end-of-track case inside ComposeFrame does not write the face
	// mid-composition and then have the caller write it again on the same frame.
	void EndSpeech();

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> TargetMesh;

	FFaceProfile Profile;

	int32 ResolvedCurveCount = 0;

	bool bSelfTesting = false;
	float SelfTestSecondsPerViseme = 0.4f;
	float SelfTestElapsed = 0.0f;

	TArray<FVisemeSpan> SpeechTrack;
	bool bSpeaking = false;
	float SpeechElapsed = 0.0f;

	bool bBlinkEnabled = true;
	float BlinkTimer = 0.0f;
	float NextBlinkIn = 3.0f;
	float BlinkProgress = -1.0f;	// <0 == eyes open

	FCompanionEmotionState Emotion;
	bool bEmotionEnabled = true;

	// Whether the last frame actually wrote curves. The idle path deliberately does not touch the
	// mesh when there is nothing to show, so this exists to guarantee ONE final zeroing pass on the
	// way into idle — without it the last expression would stay latched on the mesh forever, which
	// is the same trap FFaceProfile::AllCurves() documents for blink.
	bool bFaceWritten = false;
};
