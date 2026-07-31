#pragma once

#include "CoreMinimal.h"
#include "Avatar/FaceProfile.h"
#include "VisemePlanner.generated.h"

// One viseme held over a span of the utterance. A track is a sorted, non-overlapping list of
// these; the face driver interpolates between neighbours so the mouth flows rather than snapping.
USTRUCT(BlueprintType)
struct CICOMPANION_API FVisemeSpan
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaHumanLLM|Voice")
	float StartSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaHumanLLM|Voice")
	float DurationSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaHumanLLM|Voice")
	EViseme Viseme = EViseme::Sil;

	FVisemeSpan() = default;
	FVisemeSpan(float InStart, float InDuration, EViseme InViseme)
		: StartSeconds(InStart), DurationSeconds(InDuration), Viseme(InViseme) {}

	float EndSeconds() const { return StartSeconds + DurationSeconds; }
};

// Turns reply TEXT into a viseme timeline, with no phoneme aligner and no audio.
//
// ANIMATION-PLAN.md's preferred source is Piper's phoneme timings, which are exact but need a
// patched Piper build that does not exist yet. This is the deliberate fallback: English
// orthography predicts mouth shape well enough that a viewer reads it as speech, because lip-sync
// is judged on plausibility and rhythm rather than phonetic truth. It is strictly better than
// FVisemeEnvelope's single jaw-open channel, which can only chew.
//
// It is also the honest boundary of what we can do without alignment: spans are ESTIMATED from a
// speaking rate, so a track drifts against real audio over a long utterance. Callers should
// rescale a planned track to the measured audio duration once TTS returns (see PlanForDuration).
// One phoneme as the TTS model actually voiced it: the symbol, when it began, and how long it
// was held. Produced by the kokoro-align sidecar from Kokoro's own duration predictor -- the
// same tensor the length regulator used to build the waveform -- so these are measurements of
// the emitted audio, not predictions about it.
USTRUCT(BlueprintType)
struct CICOMPANION_API FPhonemeSpan
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaHumanLLM|Voice")
	FString Symbol;

	UPROPERTY(BlueprintReadOnly, Category = "MetaHumanLLM|Voice")
	float StartSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MetaHumanLLM|Voice")
	float DurationSeconds = 0.0f;
};

// Which clock a viseme track was actually built from, worst to best kept explicit so a deployed
// build can say plainly which one it used rather than leaving an operator to infer it from how
// the mouth looks.
enum class EVisemeTimingSource : uint8
{
	// Per-phoneme spans from the TTS model's own duration predictor. A measurement.
	Alignment,
	// Spelling estimate, uniformly rescaled to the clip's true length. Total is right; the
	// distribution is still a guess, so a long word and a short one share time by letter count.
	MeasuredDuration,
	// Spelling estimate at an assumed speaking rate. No audio was involved at all.
	TextEstimate
};

class CICOMPANION_API FVisemePlanner
{
public:
	// Picks the best timing available and says which one it picked. The ORDER is the whole point:
	// real alignment beats a measured total beats a pure guess, and each step down is a step
	// further from what the voice is actually doing. Falls through rather than failing, so an
	// unreachable aligner costs precision and never costs speech.
	static TArray<FVisemeSpan> PlanBest(const FString& Text, const TArray<FPhonemeSpan>& Alignment,
		float MeasuredSeconds, EVisemeTimingSource& OutSource);

	// Log-friendly name for a source. Deliberately shouty about the estimates.
	static const TCHAR* TimingSourceName(EVisemeTimingSource Source);

	// Builds a track from REAL alignment. This is the accurate path and should be preferred
	// whenever the aligner is reachable: PlanFromText can only distribute time by spelling,
	// so "though" and "throw" get identical timing despite sounding nothing alike.
	static TArray<FVisemeSpan> PlanFromAlignment(const TArray<FPhonemeSpan>& Spans);

	// Maps one misaki/IPA symbol to a viseme. Note misaki writes DIPHTHONGS as capitals --
	// I=/aɪ/, W=/aʊ/, A=/eɪ/, Y=/ɔɪ/, O=/oʊ/ -- which look like spelled-out letters and are not.
	static EViseme VisemeForIpa(const FString& Symbol);
	// Plans a track from text at an assumed speaking rate. Punctuation inserts silence, which is
	// what makes the result read as speech rather than an unbroken chatter of shapes.
	static TArray<FVisemeSpan> PlanFromText(const FString& Text, float WordsPerMinute = 165.0f);

	// Plans, then uniformly rescales to a known audio length — the path to use once TTS has
	// produced a clip, since the clip's true duration removes the speaking-rate guess.
	static TArray<FVisemeSpan> PlanForDuration(const FString& Text, float TargetSeconds);

	// The viseme an English letter most nearly produces. Vowels dominate the visible shape, so
	// consonant clusters collapse toward the following vowel rather than each getting a frame.
	static EViseme VisemeForChar(TCHAR Char);

	// Samples a track at a point in time, blending across the boundary between adjacent spans so
	// the mouth moves continuously. Returns silence past the end.
	static TMap<EViseme, float> SampleAt(const TArray<FVisemeSpan>& Track, float TimeSeconds, float BlendSeconds = 0.045f);

	// Total length of a track, 0 for an empty one.
	static float TrackDuration(const TArray<FVisemeSpan>& Track);
};
