#pragma once

#include "CoreMinimal.h"
#include "Avatar/FaceProfile.h"
#include "Voice/VisemePlanner.h"

// Makes this puppet speak the same lip-sync dialect as the org's other embodied agents.
//
// WHY
// CI-Spatial-Companion-WebXR, CI-Spatial-Companion-Unity and the archived WebSpatial client all
// already agree on two contracts, arrived at independently:
//
//   1. The viseme vocabulary is the Oculus/OVR-15 name set
//      (viseme_sil, viseme_PP, viseme_FF, viseme_TH, viseme_DD, viseme_kk, viseme_CH, viseme_SS,
//       viseme_nn, viseme_RR, viseme_aa, viseme_E, viseme_I, viseme_O, viseme_U).
//      Verified in the Unity sibling's Ready Player Me GLB targetNames (indices 1-15) and in the
//      WebSpatial client's OCULUS_TO_VRM_MAPPING. The WebXR sibling uses 14 of the 15, omitting
//      viseme_sil from its runtime target type.
//
//   2. Word-level timing is carried as "WhisperData":
//      { words: string[], wtimes: number[], wdurations: number[] }  -- milliseconds.
//      This shape is used whether the timings come from a real aligner or an approximation, which is
//      what lets a generator be swapped without touching consumers.
//
// UE's EViseme is ALREADY the same 15-class vocabulary, so no re-modelling is needed -- only a name
// bridge. Three class names are spelled differently and that is the entire divergence:
//     EViseme::IH  <-> viseme_I
//     EViseme::OH  <-> viseme_O
//     EViseme::OU  <-> viseme_U
// Left as-is rather than renamed: the UE names are phonetically clearer, and a rename would churn
// FaceProfile.cpp, VisemePlanner.cpp and their specs for no behavioural gain. The mapping lives here
// instead, in one place, tested.
//
// Everything in this file is pure: no engine calls, no UObject, no network. It is unit-testable
// headlessly on either engine, which is the property the rest of the plugin's pure units already have.

namespace CIArkit
{
	// The 52 canonical Apple ARKit blendshape names, in EARFaceBlendShape declaration order,
	// lowerCamelCase as they appear on the wire and on MetaHuman face meshes.
	//
	// Extracted from the engine's own enum rather than typed from memory --
	// Engine/Source/Runtime/AugmentedReality/Public/ARTrackable.h (5.5:260, 5.8:262), identical in
	// both installs.
	//
	// This is the allow-list ANIMATION-PLAN.md section 5.3 asks for by name, to guard the
	// "mouthSmileLeft vs mouthSmile_L" naming trap: a curve name that is subtly wrong does not error,
	// it just never moves the face.
	CICOMPANION_API const TArray<FName>& Names52();

	// True if Name is one of the 52.
	CICOMPANION_API bool IsCanonical(FName Name);

	// THE 52-vs-61 TRAP, recorded as a constant so nobody has to rediscover it.
	//
	// EARFaceBlendShape declares 61 enumerators before MAX, not 52. Entries 53-61 are HeadYaw,
	// HeadPitch, HeadRoll, LeftEyeYaw, LeftEyePitch, LeftEyeRoll, RightEyeYaw, RightEyePitch,
	// RightEyeRoll -- head and eye POSE channels, not blendshapes. Identical in 5.5.4 and 5.8.1, so
	// this is a latent bug rather than a migration one.
	//
	// Any loop written as `for (i = 0; i < 52; ++i)` over StaticEnum<EARFaceBlendShape>(), or any
	// `Num() == 52` assertion, is mis-sized and will silently mis-map. Use ArkitBlendShapeCount for
	// the blendshapes and EngineEnumeratorCount when indexing the engine enum.
	inline constexpr int32 ArkitBlendShapeCount = 52;
	inline constexpr int32 EngineEnumeratorCount = 61;
}

namespace CIOvr
{
	// The Oculus/OVR-15 viseme names exactly as the sibling repos spell them. Case matters: the
	// vowel and nasal classes are lowercase (viseme_kk, viseme_nn, viseme_aa) while the plosive and
	// fricative classes are uppercase (viseme_PP, viseme_FF). That inconsistency is Oculus's, is
	// baked into every Ready Player Me avatar's blendshape list, and must be reproduced verbatim --
	// a case-normalised name simply will not resolve against an RPM mesh.
	CICOMPANION_API const TArray<FName>& Names15();

	// EViseme -> "viseme_XX". Total: every EViseme has a name.
	CICOMPANION_API FName ToOvrName(EViseme Viseme);

	// "viseme_XX" -> EViseme. Returns false for anything unrecognised rather than guessing, so a
	// typo from an upstream service surfaces instead of silently becoming silence.
	CICOMPANION_API bool FromOvrName(FName OvrName, EViseme& OutViseme);
}

// The org's word-timing interchange envelope: { words, wtimes, wdurations }, milliseconds.
//
// Named for the "WhisperData" type the WebXR and WebSpatial siblings pass between their TTS layer
// and their viseme generator. Adopting the shape verbatim means a timing source written for any
// sibling -- a real forced aligner, Piper's phoneme alignments, or the WPM approximation the
// WebSpatial client fabricates -- can drive this MetaHuman with no adapter.
struct CICOMPANION_API FCIWordTimings
{
	TArray<FString> Words;
	TArray<int32> WTimesMs;		// start of each word, milliseconds from utterance start
	TArray<int32> WDurationsMs;	// duration of each word, milliseconds

	// The three arrays are parallel; anything else is malformed.
	bool IsValid() const
	{
		return Words.Num() == WTimesMs.Num() && Words.Num() == WDurationsMs.Num();
	}

	int32 Num() const { return Words.Num(); }

	// Total utterance length in seconds: the end of the last word, or 0 when empty.
	float TotalSeconds() const;

	// Parses the sibling JSON envelope. Returns false on malformed input or mismatched array lengths,
	// leaving OutTimings untouched.
	//
	// Tolerated by design, because real senders do all of these:
	//   - float millisecond values (the WPM approximator emits fractional ms) -> rounded
	//   - extra unknown fields -> ignored
	//   - an empty words array -> valid, yields an empty timing set
	static bool FromJson(const FString& Json, FCIWordTimings& OutTimings);

	FString ToJson() const;

	// Converts word timings into a viseme track this project's face driver can already play.
	//
	// Each word's letters are distributed across that word's own measured window using the existing
	// FVisemePlanner::VisemeForChar mapping, and the GAPS BETWEEN words become explicit Sil spans.
	// That last part is what makes externally-timed speech read correctly: without silence in the
	// pauses the mouth keeps moving between words and the whole thing looks like chewing.
	//
	// This is strictly better than FVisemePlanner::PlanFromText because the timing is measured rather
	// than assumed at 165 WPM -- the track cannot drift against the audio it came from.
	TArray<FVisemeSpan> ToVisemeTrack() const;
};
