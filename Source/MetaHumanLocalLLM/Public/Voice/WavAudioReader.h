#pragma once

#include "CoreMinimal.h"

// Decoded contents of a PCM WAV clip: metadata plus the audio downmixed to a single
// normalized float channel (what the viseme envelope and a mono procedural sound wave need).
struct METAHUMANLOCALLLM_API FWavData
{
	int32 SampleRate = 0;
	int32 NumChannels = 0;
	int32 BitsPerSample = 0;

	// Downmixed mono, normalized to [-1, 1].
	TArray<float> MonoSamples;

	float DurationSeconds() const
	{
		return SampleRate > 0 ? static_cast<float>(MonoSamples.Num()) / SampleRate : 0.0f;
	}
};

// Parses little-endian PCM WAV bytes (RIFF/WAVE with a `fmt ` chunk and a `data` chunk,
// 8- or 16-bit integer PCM). This is what Piper returns from its HTTP endpoint. Kept as a
// plain static helper with no engine audio dependencies so it is unit-testable on synthetic
// byte buffers. Non-PCM / compressed WAVs and truncated headers are rejected (returns false).
class METAHUMANLOCALLLM_API FWavAudioReader
{
public:
	static bool Parse(const TArray<uint8>& Bytes, FWavData& OutData);

	// Converts normalized mono float samples to interleaved 16-bit PCM, for feeding a
	// USoundWaveProcedural playback queue.
	static TArray<int16> ToPcm16(const TArray<float>& MonoSamples);
};
