#pragma once

#include "CoreMinimal.h"
#include "FaceProfile.generated.h"

// The vendor-neutral viseme vocabulary (the OVR-15 classes ANIMATION-PLAN.md §1 specifies).
// Phoneme -> EViseme is rig-independent language math; EViseme -> concrete rig curves is the
// ONLY rig-specific stage, and it lives in FFaceProfile below. Keeping the split here is what
// lets one mapper drive both a Reallusion CC face and a MetaHuman/ARKit face.
UENUM(BlueprintType)
enum class EViseme : uint8
{
	Sil,	// silence / rest
	PP,		// p, b, m      (bilabial closure)
	FF,		// f, v         (labiodental)
	TH,		// th           (dental, tongue between teeth)
	DD,		// t, d         (alveolar stop)
	KK,		// k, g         (velar)
	CH,		// ch, j, sh    (postalveolar affricate)
	SS,		// s, z         (sibilant, narrow aperture)
	NN,		// n, l         (alveolar nasal/lateral)
	RR,		// r            (retroflex/curled)
	AA,		// father       (open)
	E,		// bed          (wide, mid)
	IH,		// bit          (wide, close)
	OH,		// boat         (rounded, mid)
	OU		// boot, w      (rounded, close)
};

// One rig curve contribution. A viseme is a BLEND, not a single morph: "AA" is an open jaw plus
// an open mouth shape, and driving only one of them reads as a hinge rather than speech.
USTRUCT(BlueprintType)
struct CICOMPANION_API FFaceCurve
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaHumanLLM|Face")
	FName Curve;

	// Multiplied by the viseme's activation weight, so a profile can express "this shape only
	// ever contributes 60% jaw" without the caller knowing anything about the rig.
	UPROPERTY(BlueprintReadOnly, Category = "MetaHumanLLM|Face")
	float Scale = 1.0f;

	FFaceCurve() = default;
	FFaceCurve(FName InCurve, float InScale)
		: Curve(InCurve)
		, Scale(InScale)
	{
	}
};

USTRUCT(BlueprintType)
struct CICOMPANION_API FVisemeShape
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaHumanLLM|Face")
	TArray<FFaceCurve> Curves;
};

// Maps the neutral viseme vocabulary onto one specific rig's curve names.
//
// Two concrete profiles ship: CC (Reallusion Character Creator, e.g. the resident "Nia") and
// ARKit (MetaHuman). They are NOT interchangeable — Nia was probed and carries 0 of 10 sampled
// ARKit names but all 15 CC viseme morphs (Docs/reference/nia-morph-targets.txt), which is why
// the rig names had to move behind a profile at all.
USTRUCT(BlueprintType)
struct CICOMPANION_API FFaceProfile
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaHumanLLM|Face")
	FName ProfileName;

	UPROPERTY(BlueprintReadOnly, Category = "MetaHumanLLM|Face")
	TMap<EViseme, FVisemeShape> Shapes;

	// Resolves a viseme to its weighted curves, accumulating into OutCurves so overlapping
	// visemes (coarticulation) sum rather than overwrite. Unknown visemes contribute nothing.
	void Accumulate(EViseme Viseme, float Weight, TMap<FName, float>& OutCurves) const;

	// Every curve name this profile can ever touch. The driver uses it to zero the face without
	// having to remember what it set last frame.
	TArray<FName> AllCurves() const;

	// Reallusion Character Creator naming (V_* viseme morphs + Jaw_/Mouth_ articulators).
	static const FFaceProfile& CharacterCreator();

	// Apple ARKit naming, as used by MetaHuman faces.
	static const FFaceProfile& ARKit();

	// Looks up a profile by name ("CC" / "ARKit"), defaulting to CC — the resident's rig.
	static const FFaceProfile& ByName(const FString& Name);
};
