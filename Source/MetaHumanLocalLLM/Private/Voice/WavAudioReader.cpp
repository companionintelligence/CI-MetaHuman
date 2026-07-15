#include "Voice/WavAudioReader.h"

namespace
{
	// Little-endian readers with bounds checking. Offset is advanced on success.
	bool ReadU32(const TArray<uint8>& Bytes, int32& Offset, uint32& Out)
	{
		if (Offset + 4 > Bytes.Num())
		{
			return false;
		}
		Out = static_cast<uint32>(Bytes[Offset])
			| (static_cast<uint32>(Bytes[Offset + 1]) << 8)
			| (static_cast<uint32>(Bytes[Offset + 2]) << 16)
			| (static_cast<uint32>(Bytes[Offset + 3]) << 24);
		Offset += 4;
		return true;
	}

	bool ReadU16(const TArray<uint8>& Bytes, int32& Offset, uint16& Out)
	{
		if (Offset + 2 > Bytes.Num())
		{
			return false;
		}
		Out = static_cast<uint16>(Bytes[Offset]) | (static_cast<uint16>(Bytes[Offset + 1]) << 8);
		Offset += 2;
		return true;
	}

	bool TagEquals(const TArray<uint8>& Bytes, int32 Offset, const char* Tag)
	{
		if (Offset + 4 > Bytes.Num())
		{
			return false;
		}
		for (int32 i = 0; i < 4; ++i)
		{
			if (Bytes[Offset + i] != static_cast<uint8>(Tag[i]))
			{
				return false;
			}
		}
		return true;
	}
}

bool FWavAudioReader::Parse(const TArray<uint8>& Bytes, FWavData& OutData)
{
	// RIFF....WAVE
	if (!TagEquals(Bytes, 0, "RIFF") || !TagEquals(Bytes, 8, "WAVE"))
	{
		return false;
	}

	int32 Offset = 12; // past "RIFF" + size + "WAVE"

	uint16 AudioFormat = 0;
	uint16 NumChannels = 0;
	uint32 SampleRate = 0;
	uint16 BitsPerSample = 0;
	bool bHaveFmt = false;

	int32 DataOffset = INDEX_NONE;
	uint32 DataSize = 0;

	// Walk chunks until we have both fmt and data (skip LIST/fact/etc.).
	while (Offset + 8 <= Bytes.Num())
	{
		const int32 ChunkIdOffset = Offset;
		uint32 ChunkSize = 0;
		int32 SizeOffset = Offset + 4;
		if (!ReadU32(Bytes, SizeOffset, ChunkSize))
		{
			break;
		}
		const int32 ChunkBodyOffset = Offset + 8;

		if (TagEquals(Bytes, ChunkIdOffset, "fmt "))
		{
			int32 P = ChunkBodyOffset;
			if (!ReadU16(Bytes, P, AudioFormat)) return false;
			if (!ReadU16(Bytes, P, NumChannels)) return false;
			if (!ReadU32(Bytes, P, SampleRate)) return false;
			uint32 ByteRate; if (!ReadU32(Bytes, P, ByteRate)) return false;
			uint16 BlockAlign; if (!ReadU16(Bytes, P, BlockAlign)) return false;
			if (!ReadU16(Bytes, P, BitsPerSample)) return false;
			bHaveFmt = true;
		}
		else if (TagEquals(Bytes, ChunkIdOffset, "data"))
		{
			DataOffset = ChunkBodyOffset;
			DataSize = ChunkSize;
		}

		// Chunks are word-aligned: an odd size is followed by a pad byte.
		int32 Advance = ChunkBodyOffset + static_cast<int32>(ChunkSize);
		if (ChunkSize % 2 == 1)
		{
			Advance += 1;
		}
		if (Advance <= Offset)
		{
			break; // malformed / zero-progress guard
		}
		Offset = Advance;
	}

	if (!bHaveFmt || DataOffset == INDEX_NONE || NumChannels == 0)
	{
		return false;
	}
	// Only uncompressed integer PCM (format 1) is supported.
	if (AudioFormat != 1 || (BitsPerSample != 16 && BitsPerSample != 8))
	{
		return false;
	}

	// Clamp declared data size to what's actually present.
	const int32 AvailableBytes = FMath::Min(static_cast<int32>(DataSize), Bytes.Num() - DataOffset);
	if (AvailableBytes <= 0)
	{
		return false;
	}

	OutData.SampleRate = static_cast<int32>(SampleRate);
	OutData.NumChannels = NumChannels;
	OutData.BitsPerSample = BitsPerSample;
	OutData.MonoSamples.Reset();

	const int32 BytesPerSample = BitsPerSample / 8;
	const int32 FrameBytes = BytesPerSample * NumChannels;
	if (FrameBytes <= 0)
	{
		return false;
	}
	const int32 NumFrames = AvailableBytes / FrameBytes;
	OutData.MonoSamples.Reserve(NumFrames);

	for (int32 Frame = 0; Frame < NumFrames; ++Frame)
	{
		const int32 FrameStart = DataOffset + Frame * FrameBytes;
		float ChannelSum = 0.0f;

		for (int32 Ch = 0; Ch < NumChannels; ++Ch)
		{
			const int32 SampleStart = FrameStart + Ch * BytesPerSample;
			if (BitsPerSample == 16)
			{
				const int16 S = static_cast<int16>(
					static_cast<uint16>(Bytes[SampleStart]) | (static_cast<uint16>(Bytes[SampleStart + 1]) << 8));
				ChannelSum += static_cast<float>(S) / 32768.0f;
			}
			else // 8-bit PCM is unsigned, centered at 128
			{
				const uint8 S = Bytes[SampleStart];
				ChannelSum += (static_cast<float>(S) - 128.0f) / 128.0f;
			}
		}

		OutData.MonoSamples.Add(ChannelSum / NumChannels);
	}

	return true;
}

TArray<int16> FWavAudioReader::ToPcm16(const TArray<float>& MonoSamples)
{
	TArray<int16> Out;
	Out.Reserve(MonoSamples.Num());
	for (float Sample : MonoSamples)
	{
		const float Clamped = FMath::Clamp(Sample, -1.0f, 1.0f);
		Out.Add(static_cast<int16>(FMath::RoundToInt(Clamped * 32767.0f)));
	}
	return Out;
}
