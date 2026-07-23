#include "Voice/VisemeEnvelope.h"

TArray<FVisemeKey> FVisemeEnvelope::Compute(const TArray<float>& MonoSamples, int32 SampleRate, float WindowSeconds)
{
	TArray<FVisemeKey> Keys;

	if (MonoSamples.Num() == 0 || SampleRate <= 0 || WindowSeconds <= 0.0f)
	{
		return Keys;
	}

	const int32 WindowSamples = FMath::Max(1, FMath::RoundToInt(SampleRate * WindowSeconds));
	const int32 NumWindows = FMath::DivideAndRoundUp(MonoSamples.Num(), WindowSamples);

	// First pass: RMS per window, tracking the loudest for normalization.
	TArray<float> WindowRms;
	WindowRms.Reserve(NumWindows);
	float MaxRms = 0.0f;

	for (int32 Window = 0; Window < NumWindows; ++Window)
	{
		const int32 Start = Window * WindowSamples;
		const int32 End = FMath::Min(Start + WindowSamples, MonoSamples.Num());

		double SumSquares = 0.0;
		for (int32 i = Start; i < End; ++i)
		{
			SumSquares += static_cast<double>(MonoSamples[i]) * MonoSamples[i];
		}
		const int32 Count = End - Start;
		const float Rms = Count > 0 ? static_cast<float>(FMath::Sqrt(SumSquares / Count)) : 0.0f;

		WindowRms.Add(Rms);
		MaxRms = FMath::Max(MaxRms, Rms);
	}

	// Second pass: normalize to [0,1] and stamp times. If the whole clip is silent, every
	// key stays at 0 (mouth closed) rather than dividing by zero.
	Keys.Reserve(NumWindows + 1);
	for (int32 Window = 0; Window < NumWindows; ++Window)
	{
		const float JawOpen = MaxRms > 0.0f ? FMath::Clamp(WindowRms[Window] / MaxRms, 0.0f, 1.0f) : 0.0f;
		const float TimeSeconds = Window * WindowSeconds;
		Keys.Add(FVisemeKey(TimeSeconds, JawOpen));
	}

	const float DurationSeconds = static_cast<float>(MonoSamples.Num()) / SampleRate;
	if (Keys.Last().TimeSeconds < DurationSeconds)
	{
		Keys.Add(FVisemeKey(DurationSeconds, 0.0f));
	}

	return Keys;
}
