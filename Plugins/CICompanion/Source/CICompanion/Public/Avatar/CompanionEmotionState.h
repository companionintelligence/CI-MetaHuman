#pragma once

#include "CoreMinimal.h"
#include "Avatar/EmotionMapper.h"
#include "Avatar/FaceProfile.h"
#include "Templates/Function.h"

// The sustained emotional bias on the face: which expression is being held, how strongly, and how
// it is getting there.
//
// WHY THIS IS NOT A VISEME, AND WHY IT COMPOSITES RATHER THAN COMPETES.
// A viseme is a shape the mouth passes THROUGH in about 80ms; an emotion is a bias the whole face
// HOLDS across an utterance. They live on different clocks and, in this rig, on different curves
// (FFaceProfile keeps EmotionShapes separate from Shapes and weights it brow/cheek/eye-first for
// exactly that reason). Selecting between them would mean the face could either speak or feel, and
// the mouth would jump every time the expression changed. Summing them means she says the words
// with a face that means them, which is the entire point.
//
// Deliberately a plain value type with no engine dependencies -- no UObject, no world, no tick
// group -- so the blend/hold/decay policy is testable headlessly, the same way
// UCompanionStageDirector's camera policy is.
struct CICOMPANION_API FCompanionEmotionState
{
	// Seconds to cross from one expression to the next. Snapping between poses reads as a glitch
	// -- a face that changes in one frame looks like a texture swap, not a reaction. Much slower
	// than this and she is still arranging her face a second after the line that caused it.
	static constexpr float DefaultBlendSeconds = 0.4f;

	// How long an expression is held after the utterance ends before it lets go. A reaction that
	// vanishes on the last syllable reads as a switch being thrown rather than as a person.
	static constexpr float DefaultHoldSeconds = 1.2f;

	// The backstop. Every Set() arms this, so an expression decays on its own even if nothing ever
	// tells it the utterance ended -- which is not hypothetical: with a TTS service configured the
	// mouth is driven from an audio callback, and if that service is down the viseme track never
	// starts and therefore never finishes. Without this she would smile for the rest of the session.
	static constexpr float DefaultSustainSeconds = 12.0f;

	// Seconds for a full 0->1 change. Exposed so a caller can slow a deliberate beat down; the
	// default is what everything actually uses.
	float BlendSeconds = DefaultBlendSeconds;

	// Adopts an expression and holds it. Re-setting the SAME emotion just retargets the intensity;
	// setting a different one starts a cross-fade rather than a pass through neutral, because
	// going happy -> neutral -> sad takes twice as long and reads as a double take.
	void Set(EEmotionType InEmotion, float Intensity = 1.0f);

	// Starts letting go: holds for HoldSeconds, then blends out. Never EXTENDS a sustain already
	// shorter than the hold, so releasing twice cannot keep an expression alive.
	void Release(float HoldSeconds = DefaultHoldSeconds);

	// Extends the lease to cover an utterance whose length is now KNOWN.
	//
	// DefaultSustainSeconds is a backstop sized for the case where nothing ever reports the end of
	// speech, and 12s was chosen against that failure rather than against real replies. It is
	// therefore too SHORT for a long one: an expression is set once per reply, so any utterance
	// running past 12 seconds watched her face drain to neutral partway through the sentence it
	// belonged to, and then stay neutral for the rest of it. Kokoro at ~150wpm reaches 12s at about
	// thirty words, which is an ordinary paragraph, not an edge case.
	//
	// Max, not assignment: this can only ever lengthen a lease. Shortening is Release's job, and
	// letting this shorten too would mean the arrival of a *longer* track could cut an expression
	// off early depending on which call landed last.
	void Sustain(float Seconds);

	// Drops the expression instantly, with no blend. For teardown and the kill switch, where a
	// half-second fade of something that should not be running at all is just a slower bug.
	void Reset();

	// Advances the blend and the sustain countdown by one frame.
	void Advance(float DeltaTime);

	// Adds this state's curves into OutCurves. ADDS: callers accumulate visemes first and this
	// second, so the expression lands on top of speech instead of overwriting it. Nothing is
	// clamped here -- the driver clamps once, at the point it writes the mesh.
	void AccumulateInto(const FFaceProfile& Profile, TMap<FName, float>& OutCurves) const;

	// The expression currently being held. Reads Neutral once a decay has fully run out, so
	// "decayed back to neutral" is observable and not merely inferred from a weight near zero.
	EEmotionType GetEmotion() const { return Current; }

	// 0..1 activation of GetEmotion(), before the profile's own (deliberately low) FACS weights.
	float GetWeight() const { return CurrentWeight; }

	// True while anything is still on the face, including an expression on its way out.
	bool IsActive() const;

	// Seconds left before the expression starts blending out. 0 once it already is.
	float GetSustainRemaining() const { return SustainRemaining; }

	// THE KILL SWITCH. Emotion is ON unless explicitly turned off: an expressionless face is the
	// regression this feature exists to fix, so "unset" has to mean "on". Only MHLLM_EMOTION=0 or
	// =false disables it, matching CICompanionEnv::IsDisabled -- absent means "not configured",
	// which callers treat as the default rather than as disabled. A typo therefore fails toward
	// the working behaviour, which is the right direction for a cosmetic subsystem.
	static bool IsEnabled(TFunctionRef<FString(const TCHAR*)> Lookup);

	// Process-environment overload. The injectable form above exists so the policy is testable
	// without mutating the real environment (same pattern as EnvConfigSpec and CompanionStageSpec).
	static bool IsEnabled();

private:
	EEmotionType Current = EEmotionType::Neutral;
	float CurrentWeight = 0.0f;
	float TargetWeight = 0.0f;

	// The expression being cross-faded OUT. Kept alive rather than snapped away so a change of
	// mood is one continuous move instead of a drop to neutral followed by a rise.
	EEmotionType Outgoing = EEmotionType::Neutral;
	float OutgoingWeight = 0.0f;

	float SustainRemaining = 0.0f;
};
