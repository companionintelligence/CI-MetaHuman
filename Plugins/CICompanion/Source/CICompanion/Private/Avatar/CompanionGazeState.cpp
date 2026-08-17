#include "Avatar/CompanionGazeState.h"

#include "HAL/PlatformMisc.h"

namespace
{
	// Index layout of FFaceProfile::GazeCurves. BOTH shipped profiles agree on this ordering, and
	// the agreement is not a coincidence -- it is the reason one gaze implementation drives a
	// Reallusion CC rig and a MetaHuman/ARKit rig without knowing which it has.
	//
	// It is worth spelling out because the ARKit names do NOT read as left/right on their face:
	//
	//   idx  CC                  ARKit               means
	//   0    Eye_L_Look_L        eyeLookOutLeft      left eye, toward HER left  (temporal)
	//   1    Eye_L_Look_R        eyeLookInLeft       left eye, toward HER right (nasal)
	//   2    Eye_L_Look_Up       eyeLookUpLeft       left eye, up
	//   3    Eye_L_Look_Down     eyeLookDownLeft     left eye, down
	//   4    Eye_R_Look_L        eyeLookInRight      right eye, toward HER left  (nasal)
	//   5    Eye_R_Look_R        eyeLookOutRight     right eye, toward HER right (temporal)
	//   6    Eye_R_Look_Up       eyeLookUpRight      right eye, up
	//   7    Eye_R_Look_Down     eyeLookDownRight    right eye, down
	//
	// "Out" and "In" swap sides between the two eyes, which is exactly the trap: pairing index 0
	// with index 4 by NAME rather than by MEANING gives a rig that crosses its eyes on every glance.
	constexpr int32 LeftEyeLookLeft = 0;
	constexpr int32 LeftEyeLookRight = 1;
	constexpr int32 LeftEyeLookUp = 2;
	constexpr int32 LeftEyeLookDown = 3;
	constexpr int32 RightEyeLookLeft = 4;
	constexpr int32 RightEyeLookRight = 5;
	constexpr int32 RightEyeLookUp = 6;
	constexpr int32 RightEyeLookDown = 7;

	constexpr int32 RequiredGazeCurves = 8;

	// How far up the planning aversion pushes. Upward-and-away is the recognisable "thinking" break;
	// downward reads as shame and sideways as distraction.
	constexpr float ThinkingUpwardBias = 0.18f;

	// Thinking fixations are held longer -- a planning break that saccades at conversational speed
	// looks like searching for an exit rather than composing a sentence.
	constexpr float ThinkingFixationScale = 1.6f;

	void AddCurve(const FFaceProfile& Profile, int32 Index, float Weight, TMap<FName, float>& OutCurves)
	{
		// Below-threshold contributions are DROPPED rather than written as zero. The face driver
		// treats a non-empty curve map as "something is on the face" and uses that to decide whether
		// it still owes the mesh a zeroing pass; eight zeroes every frame would pin that latch on
		// forever and defeat the idle path entirely.
		if (Weight > KINDA_SMALL_NUMBER)
		{
			OutCurves.FindOrAdd(Profile.GazeCurves[Index], 0.0f) += Weight;
		}
	}
}

void FCompanionGazeState::SetSeed(int32 Seed)
{
	Rng.Initialize(Seed);
	bSeeded = true;

	// Re-aim from the PINNED stream, so "after SetSeed the state is fully determined by the seed"
	// is actually true. Without this, a SetMode() before the first Advance() would already have
	// self-seeded from the global FMath::Rand and chosen a target this stream never produced --
	// leaving a spec that looks deterministic and quietly is not.
	PickNewTarget();
}

void FCompanionGazeState::SetMode(EGazeMode InMode)
{
	// Idempotent. A caller that pushes the mode every frame -- which the runtime subsystem does,
	// because it has no cheap way to know the conversational state changed -- must not restart the
	// fixation clock on each tick and freeze the eyes mid-saccade.
	if (InMode == Mode)
	{
		return;
	}

	Mode = InMode;

	// Armed on entry, cleared on exit, so a mode that is never left cannot outlive its cause.
	ThinkingRemaining = (Mode == EGazeMode::Thinking) ? ThinkingTimeoutSeconds : 0.0f;

	// The mode change IS the visible event: she looks away as she begins to think and back as she
	// begins to answer. Deferring it to the end of the current fixation would decouple the glance
	// from the thing that caused it, which is the whole information content of the movement.
	PickNewTarget();
}

void FCompanionGazeState::Advance(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	if (!bSeeded)
	{
		// Seeded from the global stream rather than a constant, so two companions in one world do
		// not glance in lockstep. Tests call SetSeed first and never reach this. SetSeed picks the
		// first target itself, so there is deliberately no second PickNewTarget here.
		SetSeed(FMath::Rand());
	}

	// Checked BEFORE the fixation clock so the recovery lands on this frame rather than waiting for
	// the current fixation to expire on top of an already-expired timeout.
	if (Mode == EGazeMode::Thinking)
	{
		ThinkingRemaining -= DeltaTime;
		if (ThinkingRemaining <= 0.0f)
		{
			// Whatever was supposed to end the thought never arrived. Go back to the viewer: a
			// companion who has given up waiting should look at you, not past you.
			SetMode(EGazeMode::Contact);

			// RETURN, rather than falling through to the fixation clock. SetMode has just aimed her
			// back at the lens, and that recovery saccade IS this frame's re-aim. Continuing would
			// decrement the fresh fixation it just set and, on any DeltaTime longer than the new
			// fixation, immediately pick a THIRD target -- throwing away the recovery before it was
			// ever rendered. That is not hypothetical at large steps, which is exactly how the
			// timeout is reached in the first place.
			return;
		}
	}

	FixationRemaining -= DeltaTime;
	if (FixationRemaining <= 0.0f)
	{
		PickNewTarget();
	}

	// Constant-rate, so a full 0->1 deflection takes exactly SaccadeSeconds and the movement
	// genuinely ARRIVES. An exponential ease never technically finishes, which would leave the eye
	// permanently drifting a little short of where it was aimed.
	const float Rate = 1.0f / SaccadeSeconds;
	Aim.X = FMath::FInterpConstantTo(Aim.X, Target.X, DeltaTime, Rate);
	Aim.Y = FMath::FInterpConstantTo(Aim.Y, Target.Y, DeltaTime, Rate);
}

void FCompanionGazeState::PickNewTarget()
{
	if (!bSeeded)
	{
		// Reachable only from SetMode() on a state that has never been seeded or advanced. SetSeed
		// sets bSeeded BEFORE it calls back into here, so this cannot recurse.
		SetSeed(FMath::Rand());
		return;	// SetSeed has already chosen a target from the freshly pinned stream
	}

	if (Rng.FRand() < ReturnToContactChance())
	{
		// Back to the lens. Not exactly (0,0): a pupil pinned to a mathematically perfect centre
		// across successive fixations reads as a mannequin whose eyes were aligned by hand.
		Target = FVector2D(Rng.FRandRange(-0.02f, 0.02f), Rng.FRandRange(-0.02f, 0.02f));
	}
	else
	{
		// Uniform over the DISC, not the square: sqrt on the radius is what stops the fixations
		// clustering in the middle, and picking X and Y independently would put corners in reach
		// that the eye cannot actually deflect to.
		const float Angle = Rng.FRandRange(0.0f, 2.0f * PI);
		const float Radius = AversionRadius() * FMath::Sqrt(Rng.FRand());
		Target = FVector2D(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle));

		if (Mode == EGazeMode::Thinking)
		{
			Target.Y += ThinkingUpwardBias;
		}
	}

	Target.X = FMath::Clamp(Target.X, -1.0f, 1.0f);
	Target.Y = FMath::Clamp(Target.Y, -1.0f, 1.0f);

	float Seconds = Rng.FRandRange(MinFixationSeconds, MaxFixationSeconds);
	if (Mode == EGazeMode::Thinking)
	{
		Seconds *= ThinkingFixationScale;
	}
	FixationRemaining = Seconds;
}

float FCompanionGazeState::AversionRadius() const
{
	switch (Mode)
	{
	// Listening. Small enough that she is unambiguously looking AT you; large enough that the eyes
	// are not welded in place.
	case EGazeMode::Contact:	return 0.12f;
	// Speaking. Speakers hold markedly less eye contact than listeners, and the breaks are wider.
	case EGazeMode::Speaking:	return 0.25f;
	case EGazeMode::Thinking:	return 0.40f;
	default:					return 0.12f;
	}
}

float FCompanionGazeState::ReturnToContactChance() const
{
	switch (Mode)
	{
	// ~70% gaze while listening, ~40% while speaking: the classic dyadic asymmetry. These are the
	// numbers that make her feel attentive rather than either vacant or staring.
	case EGazeMode::Contact:	return 0.70f;
	case EGazeMode::Speaking:	return 0.40f;
	// Planning aversion is nearly total, and is the point of the mode.
	case EGazeMode::Thinking:	return 0.10f;
	default:					return 0.70f;
	}
}

void FCompanionGazeState::AccumulateInto(const FFaceProfile& Profile, TMap<FName, float>& OutCurves) const
{
	// A profile without the full set is a profile that cannot express gaze. Partially driving it
	// would aim one eye and leave the other straight ahead, which is worse than not moving at all.
	if (Profile.GazeCurves.Num() < RequiredGazeCurves)
	{
		return;
	}

	const float X = FMath::Clamp(Aim.X, -1.0f, 1.0f);
	const float Y = FMath::Clamp(Aim.Y, -1.0f, 1.0f);

	// One-sided morphs: a leftward aim drives the "look left" pair and leaves "look right" untouched
	// at zero. Writing -X into the right-hand curve instead would hand a negative morph weight to a
	// rig that may not clamp it, and negative weights extrapolate geometry inside out.
	if (X < 0.0f)
	{
		AddCurve(Profile, LeftEyeLookLeft, -X, OutCurves);
		AddCurve(Profile, RightEyeLookLeft, -X, OutCurves);
	}
	else
	{
		AddCurve(Profile, LeftEyeLookRight, X, OutCurves);
		AddCurve(Profile, RightEyeLookRight, X, OutCurves);
	}

	if (Y < 0.0f)
	{
		AddCurve(Profile, LeftEyeLookDown, -Y, OutCurves);
		AddCurve(Profile, RightEyeLookDown, -Y, OutCurves);
	}
	else
	{
		AddCurve(Profile, LeftEyeLookUp, Y, OutCurves);
		AddCurve(Profile, RightEyeLookUp, Y, OutCurves);
	}
}

void FCompanionGazeState::Reset()
{
	Aim = FVector2D::ZeroVector;
	Target = FVector2D::ZeroVector;
	FixationRemaining = 0.0f;
	ThinkingRemaining = 0.0f;
	Mode = EGazeMode::Contact;
}

bool FCompanionGazeState::IsEnabled(TFunctionRef<FString(const TCHAR*)> Lookup)
{
	// Trimmed for the same reason MHLLM_EMOTION is: a compose file interpolating an unset variable
	// leaves whitespace behind, and " 0 " very obviously means off.
	const FString Value = Lookup(TEXT("MHLLM_GAZE")).TrimStartAndEnd();
	return !(Value == TEXT("0") || Value.Equals(TEXT("false"), ESearchCase::IgnoreCase));
}

bool FCompanionGazeState::IsEnabled()
{
	auto Env = [](const TCHAR* Name) { return FPlatformMisc::GetEnvironmentVariable(Name); };
	return IsEnabled(Env);
}
