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

			// WAVE_FORMAT_EXTENSIBLE announces 0xFFFE and puts the REAL format code in the first
			// two bytes of a SubFormat GUID that follows cbSize(2) + validBits(2) + mask(4).
			// Without this an otherwise ordinary float or PCM clip is rejected purely for being
			// tagged extensible, which is common from encoders that emit >2 channels or >16 bits.
			if (AudioFormat == 0xFFFE && ChunkSize >= 40)
			{
				int32 SubFormatOffset = ChunkBodyOffset + 24;
				uint16 SubFormat = 0;
				if (ReadU16(Bytes, SubFormatOffset, SubFormat))
				{
					AudioFormat = SubFormat;
				}
			}
			bHaveFmt = true;
		}
		else if (TagEquals(Bytes, ChunkIdOffset, "data"))
		{
			DataOffset = ChunkBodyOffset;
			DataSize = ChunkSize;
		}

		// Chunks are word-aligned: an odd size is followed by a pad byte.
		//
		// Arithmetic is done in int64 and clamped because a chunk size is an unsigned 32-bit
		// field and real encoders DO write 0xFFFFFFFF to mean "streaming, length unknown" --
		// Kokoro does. Casting that to int32 yields -1, which walked the cursor BACKWARDS into
		// the middle of the header, where misaligned bytes could spuriously match a "fmt "/"data"
		// tag and hand the sample loop a bogus offset. The result was a segfault triggered purely
		// by a well-formed response from our own TTS service.
		const int64 ChunkEnd = static_cast<int64>(ChunkBodyOffset) + static_cast<int64>(ChunkSize);
		int64 Advance64 = ChunkEnd + (ChunkSize % 2 == 1 ? 1 : 0);
		if (Advance64 > Bytes.Num())
		{
			// A size that runs past the buffer means this is the last chunk (or a lie); either
			// way there is nothing further to walk to.
			break;
		}
		int32 Advance = static_cast<int32>(Advance64);
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
	// Accept integer PCM (format 1) at 8/16/24/32-bit and IEEE float (format 3) at 32-bit.
	//
	// The float case is not hypothetical: AMD Lemonade's Kokoro endpoint returns 24kHz MONO
	// 32-BIT FLOAT, and this reader previously rejected it outright. Because Parse() failing is
	// indistinguishable from a network error to the caller, the symptom was a companion that
	// simply never spoke, with one warning line to explain it. The conversation E2E test caught
	// it before it shipped. WAVE_FORMAT_EXTENSIBLE (0xFFFE) carries the true format in a
	// subformat GUID whose first two bytes mirror these codes, so it is resolved above.
	const bool bIntegerPcm = (AudioFormat == 1) &&
		(BitsPerSample == 8 || BitsPerSample == 16 || BitsPerSample == 24 || BitsPerSample == 32);
	const bool bFloatPcm = (AudioFormat == 3) && (BitsPerSample == 32);
	if (!bIntegerPcm && !bFloatPcm)
	{
		return false;
	}

	// Clamp the DECLARED size to what actually arrived, in 64-bit. The old int32 cast turned the
	// 0xFFFFFFFF streaming placeholder into -1 and rejected the clip outright, so even once the
	// crash above was avoided a real Kokoro response still decoded to nothing.
	const int64 DeclaredBytes = static_cast<int64>(DataSize);
	const int64 PresentBytes = static_cast<int64>(Bytes.Num()) - static_cast<int64>(DataOffset);
	const int32 AvailableBytes = static_cast<int32>(FMath::Clamp<int64>(
		FMath::Min(DeclaredBytes, PresentBytes), 0, MAX_int32));
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
	// Belt and braces: derive the frame count from bytes that provably exist. Every read below
	// indexes Bytes[] directly, so a header that over-promises must not be able to reach past it.
	const int32 SafeBytes = FMath::Min(AvailableBytes, Bytes.Num() - DataOffset);
	const int32 NumFrames = SafeBytes / FrameBytes;
	OutData.MonoSamples.Reserve(NumFrames);

	for (int32 Frame = 0; Frame < NumFrames; ++Frame)
	{
		const int32 FrameStart = DataOffset + Frame * FrameBytes;
		float ChannelSum = 0.0f;

		for (int32 Ch = 0; Ch < NumChannels; ++Ch)
		{
			const int32 SampleStart = FrameStart + Ch * BytesPerSample;
			if (bFloatPcm)
			{
				// Already normalised to [-1,1] by definition; copy the bit pattern rather than
				// casting the pointer, which would trip strict-aliasing and alignment.
				uint32 Bits = static_cast<uint32>(Bytes[SampleStart])
					| (static_cast<uint32>(Bytes[SampleStart + 1]) << 8)
					| (static_cast<uint32>(Bytes[SampleStart + 2]) << 16)
					| (static_cast<uint32>(Bytes[SampleStart + 3]) << 24);
				float S;
				FMemory::Memcpy(&S, &Bits, sizeof(S));
				// Encoders occasionally overshoot unity; clamp so one hot sample cannot wrap to
				// full-scale noise once quantised to int16 downstream.
				ChannelSum += FMath::Clamp(S, -1.0f, 1.0f);
			}
			else if (BitsPerSample == 32)
			{
				const int32 S = static_cast<int32>(
					static_cast<uint32>(Bytes[SampleStart])
					| (static_cast<uint32>(Bytes[SampleStart + 1]) << 8)
					| (static_cast<uint32>(Bytes[SampleStart + 2]) << 16)
					| (static_cast<uint32>(Bytes[SampleStart + 3]) << 24));
				ChannelSum += static_cast<float>(S) / 2147483648.0f;
			}
			else if (BitsPerSample == 24)
			{
				// Sign-extend the 24-bit little-endian value into 32 bits.
				int32 S = static_cast<int32>(
					static_cast<uint32>(Bytes[SampleStart])
					| (static_cast<uint32>(Bytes[SampleStart + 1]) << 8)
					| (static_cast<uint32>(Bytes[SampleStart + 2]) << 16));
				if (S & 0x00800000)
				{
					S |= ~0x00FFFFFF;
				}
				ChannelSum += static_cast<float>(S) / 8388608.0f;
			}
			else if (BitsPerSample == 16)
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
