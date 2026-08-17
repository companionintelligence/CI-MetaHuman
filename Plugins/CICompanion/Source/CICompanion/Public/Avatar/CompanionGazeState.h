#pragma once

#include "CoreMinimal.h"
#include "Avatar/FaceProfile.h"
#include "Math/RandomStream.h"
#include "Templates/Function.h"

// Where the eyes are pointing, and the involuntary motion that keeps them alive.
//
// WHY THIS EXISTS AT ALL.
// FFaceProfile has declared GazeCurves for both rigs since the profile was written, and nothing has
// ever written to them -- so every frame zeroed eight eye curves and set them to nothing. The result
// is a face whose mouth and brow are driven correctly and whose eyes are welded straight ahead. In a
// video-call framing that is the single most visible absence: a person who never looks at you reads
// as absent, and no amount of lip-sync accuracy compensates for it.
//
// WHY THE MOTION IS BALLISTIC AND NOT A LERP.
// Eyes do not glide. A saccade is 30-80ms and effectively instantaneous at 30fps -- the eye is
// either at A or at B, almost never between. Easing a pupil from one fixation to the next is the
// single most common way synthetic gaze goes wrong: it reads as a doll's eyes being turned by hand.
// SaccadeSeconds is therefore deliberately shorter than a frame at 30fps, and the interpolation is
// constant-rate rather than exponential so it genuinely ARRIVES rather than asymptoting.
//
// WHY EYE CONTACT IS NOT 100%.
// Constant unbroken staring is not "maximum presence", it is a threat display -- uncanny in exactly
// the way this whole subsystem exists to avoid. Human dyads run roughly 70% gaze while listening and
// 40% while speaking, with the breaks clustered at the start of an utterance while the speaker plans
// it. EGazeMode encodes that asymmetry, and it is the reason the mode is driven by conversational
// state rather than being one fixed wander.
//
// Deliberately a plain value type with no engine dependencies -- no UObject, no world, no tick group
// -- so the policy is testable headlessly, exactly like FCompanionEmotionState next to it. The RNG
// is an owned FRandomStream rather than the global FMath::FRand so a spec can pin a seed and assert
// on an actual sequence; the blink clock beside it uses the global stream and consequently can only
// be tested for bounds.
struct CICOMPANION_API FCompanionGazeState
{
	// What she is doing, which is what decides how much she looks at you.
	enum class EGazeMode : uint8
	{
		// Idle or listening. Highest eye contact -- the listener's role.
		Contact,

		// Speaking. Speakers break gaze markedly more than listeners do.
		Speaking,

		// Waiting on the model. The classic planning aversion: eyes off-axis and biased upward,
		// held longer than a conversational saccade. This is also the one mode that does real work
		// for the product -- it turns LLM latency from a dead stare into a visible thought.
		Thinking
	};

	// A saccade is ballistic. See the class comment: this is intentionally sub-frame at 30fps.
	static constexpr float SaccadeSeconds = 0.045f;

	// How long a fixation lasts before the next saccade, per mode. Real fixations run ~0.2-2s and
	// the variance matters more than the mean -- a fixed interval reads as a metronome immediately.
	static constexpr float MinFixationSeconds = 0.35f;
	static constexpr float MaxFixationSeconds = 2.20f;

	// THE BACKSTOP on the planning aversion, and the reason it exists is worth stating.
	//
	// Thinking is entered when a question goes to the model and left when the reply starts driving
	// the mouth. If the reply never comes -- the backend is down, the request times out, the stream
	// errors -- nothing ever leaves the mode, and she spends the rest of the session gazing into the
	// middle distance. That is the exact shape of the latch FCompanionEmotionState::DefaultSustain-
	// Seconds was added to fix, for the exact same reason: the event that ends the state is produced
	// by a network round trip that is allowed to fail.
	//
	// Generous, because a slow model is not an error: a cold 27B load can legitimately take tens of
	// seconds, and cutting the aversion short would make her look away, look back, and then answer.
	static constexpr float ThinkingTimeoutSeconds = 30.0f;

	// Aim, in normalised eye-space: X positive = her right, Y positive = up, each -1..1 where 1 is
	// the rig's full deflection on that curve. Values stay well inside that: a fully deflected eye
	// is a cartoon.
	FVector2D GetAim() const { return Aim; }
	FVector2D GetTarget() const { return Target; }
	EGazeMode GetMode() const { return Mode; }

	// Switches behaviour and immediately re-triggers a saccade, because the mode change IS the event
	// a viewer should see -- she looks away as she starts thinking, and back as she starts to answer.
	// Re-setting the same mode is a no-op, so a per-frame caller cannot freeze the eyes by
	// continuously restarting the fixation clock.
	void SetMode(EGazeMode InMode);

	// Advances the fixation clock and the in-flight saccade by one frame.
	void Advance(float DeltaTime);

	// Adds the eye curves for the current aim into OutCurves.
	//
	// ADDS, and never subtracts: the four directions per eye are separate one-sided morphs, so a
	// leftward aim writes the "look left" curve and leaves "look right" at zero rather than writing
	// a negative weight. A negative morph weight is not clamped away by the driver on every rig and
	// can push geometry inside out.
	//
	// Both eyes receive the SAME deflection. Driving them independently is how you get a wall-eyed
	// avatar: at conversational distance the convergence angle is under a degree, far below what any
	// of these curves can express, so "correct" here means "identical".
	void AccumulateInto(const FFaceProfile& Profile, TMap<FName, float>& OutCurves) const;

	// Centres the eyes and clears any in-flight saccade, with no motion.
	void Reset();

	// Pins the random sequence. Tests call this; production leaves the stream seeded from the global
	// RNG so two companions in one world do not blink and glance in lockstep.
	void SetSeed(int32 Seed);

	// THE KILL SWITCH, matching FCompanionEmotionState::IsEnabled exactly: gaze is ON unless
	// explicitly disabled, because a dead stare is the regression this exists to fix and "unset"
	// must therefore mean "on". Only MHLLM_GAZE=0 or =false turns it off, so a typo fails toward the
	// working behaviour -- the right direction for a cosmetic subsystem.
	static bool IsEnabled(TFunctionRef<FString(const TCHAR*)> Lookup);
	static bool IsEnabled();

private:
	// Chooses the next fixation point and starts the saccade toward it.
	void PickNewTarget();

	// Per-mode policy: how far off-axis she is willing to look, and how often she comes back.
	float AversionRadius() const;
	float ReturnToContactChance() const;

	EGazeMode Mode = EGazeMode::Contact;

	FVector2D Aim = FVector2D::ZeroVector;
	FVector2D Target = FVector2D::ZeroVector;

	float FixationRemaining = 0.0f;

	// Counts down only while Thinking. See ThinkingTimeoutSeconds.
	float ThinkingRemaining = 0.0f;

	// Seeded lazily on first use so a default-constructed state is still deterministic under a test
	// that calls SetSeed, and still varied in production where nothing does.
	FRandomStream Rng;
	bool bSeeded = false;
};
