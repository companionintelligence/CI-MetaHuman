#pragma once

#include "CoreMinimal.h"
#include "VisemeEnvelope.generated.h"

// One point on a jaw-open timeline: at TimeSeconds into the utterance, open the mouth by
// JawOpen (0 = closed, 1 = fully open). Consumed by the avatar layer to drive the ARKit
// `jawOpen` curve on a MetaHuman face in sync with TTS audio playback.
USTRUCT(BlueprintType)
struct CICOMPANION_API FVisemeKey
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaHumanLLM|Voice")
	float TimeSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaHumanLLM|Voice")
	float JawOpen = 0.0f;

	FVisemeKey() = default;
	FVisemeKey(float InTime, float InJawOpen)
		: TimeSeconds(InTime)
		, JawOpen(InJawOpen)
	{
	}
};

// Derives a lightweight amplitude-driven lip-sync timeline from speech audio, without any
// phoneme model — the loudness-envelope approach the planning doc references (SALSA-style
// LipSync). This is deliberately a plain, dependency-free function so it can be unit tested
// on synthetic sample buffers with no audio device.
class CICOMPANION_API FVisemeEnvelope
{
public:
	// Computes jaw-open keyframes from mono float samples ([-1,1]) by taking the RMS of each
	// fixed-length window and normalizing to the loudest window, so the mouth uses its full
	// range regardless of absolute recording volume. Pure silence yields all-zero keys.
	// One key is emitted per window plus a final closed-mouth key at the clip end, so callers
	// can safely interpolate through the final audio window and reset the face deterministically.
	static TArray<FVisemeKey> Compute(const TArray<float>& MonoSamples, int32 SampleRate, float WindowSeconds = 0.06f);
};
