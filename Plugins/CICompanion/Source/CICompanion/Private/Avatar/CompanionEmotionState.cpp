#include "Avatar/CompanionEmotionState.h"

#include "HAL/PlatformMisc.h"

void FCompanionEmotionState::Set(EEmotionType InEmotion, float Intensity)
{
	Intensity = FMath::Clamp(Intensity, 0.0f, 1.0f);

	if (InEmotion != Current)
	{
		// Cross-fade rather than a pass through neutral. Whatever was on the face keeps its current
		// weight and walks itself out while the new expression walks in; both are accumulated, so
		// the transition is one continuous move.
		Outgoing = Current;
		OutgoingWeight = CurrentWeight;
		Current = InEmotion;
		CurrentWeight = 0.0f;
	}

	TargetWeight = Intensity;

	// Re-arm the backstop on every set, so a fresh reply refreshes the expression's lease rather
	// than inheriting the remains of the previous one's.
	SustainRemaining = DefaultSustainSeconds;
}

void FCompanionEmotionState::Release(float HoldSeconds)
{
	// Min, not assignment: a second release must be able to shorten the hold but never lengthen it,
	// or a stream of end-of-utterance events would keep pushing the decay out indefinitely.
	SustainRemaining = FMath::Min(SustainRemaining, FMath::Max(0.0f, HoldSeconds));
}

void FCompanionEmotionState::Sustain(float Seconds)
{
	// Max, so a lease can only grow here. See the header for why shortening is Release's job alone.
	SustainRemaining = FMath::Max(SustainRemaining, FMath::Max(0.0f, Seconds));
}

void FCompanionEmotionState::Reset()
{
	Current = EEmotionType::Neutral;
	CurrentWeight = 0.0f;
	TargetWeight = 0.0f;
	Outgoing = EEmotionType::Neutral;
	OutgoingWeight = 0.0f;
	SustainRemaining = 0.0f;
}

void FCompanionEmotionState::Advance(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	if (SustainRemaining > 0.0f)
	{
		SustainRemaining = FMath::Max(0.0f, SustainRemaining - DeltaTime);
		if (SustainRemaining <= 0.0f)
		{
			// Lease expired: start decaying back to neutral. The identity is kept until the weight
			// actually reaches zero, so the fade-out still shows the right expression.
			TargetWeight = 0.0f;
		}
	}

	// Constant rate rather than exponential: a full 0->1 change takes exactly BlendSeconds, which
	// is both what the comment above promises and what makes the timing testable. An exponential
	// ease never technically arrives, so "has it finished blending" would have no answer.
	if (BlendSeconds > KINDA_SMALL_NUMBER)
	{
		const float Rate = 1.0f / BlendSeconds;
		CurrentWeight = FMath::FInterpConstantTo(CurrentWeight, TargetWeight, DeltaTime, Rate);
		OutgoingWeight = FMath::FInterpConstantTo(OutgoingWeight, 0.0f, DeltaTime, Rate);
	}
	else
	{
		CurrentWeight = TargetWeight;
		OutgoingWeight = 0.0f;
	}

	// Fully decayed: report Neutral. Callers ask GetEmotion() to know whether the face has let go,
	// and "Happy at weight 0" is a confusing way to say "nothing".
	if (CurrentWeight <= KINDA_SMALL_NUMBER && TargetWeight <= KINDA_SMALL_NUMBER)
	{
		CurrentWeight = 0.0f;
		Current = EEmotionType::Neutral;
	}
	if (OutgoingWeight <= KINDA_SMALL_NUMBER)
	{
		OutgoingWeight = 0.0f;
		Outgoing = EEmotionType::Neutral;
	}
}

void FCompanionEmotionState::AccumulateInto(const FFaceProfile& Profile, TMap<FName, float>& OutCurves) const
{
	// Outgoing first, then current, so the arithmetic matches the visual order of a cross-fade.
	// Both are additive; FFaceProfile::Neutral maps to an empty shape, so a neutral slot at any
	// weight contributes nothing and needs no special case.
	if (OutgoingWeight > KINDA_SMALL_NUMBER)
	{
		Profile.AccumulateEmotion(Outgoing, OutgoingWeight, OutCurves);
	}
	if (CurrentWeight > KINDA_SMALL_NUMBER)
	{
		Profile.AccumulateEmotion(Current, CurrentWeight, OutCurves);
	}
}

bool FCompanionEmotionState::IsActive() const
{
	return CurrentWeight > KINDA_SMALL_NUMBER || OutgoingWeight > KINDA_SMALL_NUMBER;
}

bool FCompanionEmotionState::IsEnabled(TFunctionRef<FString(const TCHAR*)> Lookup)
{
	// Trimmed because a compose file interpolating an unset variable leaves whitespace behind, and
	// " 0 " very obviously means off.
	const FString Value = Lookup(TEXT("MHLLM_EMOTION")).TrimStartAndEnd();
	return !(Value == TEXT("0") || Value.Equals(TEXT("false"), ESearchCase::IgnoreCase));
}

bool FCompanionEmotionState::IsEnabled()
{
	auto Env = [](const TCHAR* Name) { return FPlatformMisc::GetEnvironmentVariable(Name); };
	return IsEnabled(Env);
}
