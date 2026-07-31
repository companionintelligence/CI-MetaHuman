#include "Misc/AutomationTest.h"
#include "Voice/WavAudioReader.h"
#include "Voice/VisemeEnvelope.h"
#include "Voice/PiperTextToSpeech.h"

namespace
{
	void AppendU32(TArray<uint8>& B, uint32 V)
	{
		B.Add(V & 0xFF); B.Add((V >> 8) & 0xFF); B.Add((V >> 16) & 0xFF); B.Add((V >> 24) & 0xFF);
	}
	void AppendU16(TArray<uint8>& B, uint16 V)
	{
		B.Add(V & 0xFF); B.Add((V >> 8) & 0xFF);
	}
	void AppendTag(TArray<uint8>& B, const char* Tag)
	{
		for (int32 i = 0; i < 4; ++i) { B.Add(static_cast<uint8>(Tag[i])); }
	}

	// Builds a minimal mono 16-bit PCM WAV from int16 samples.
	TArray<uint8> MakeWav(const TArray<int16>& Samples, int32 SampleRate)
	{
		TArray<uint8> B;
		const uint32 DataBytes = Samples.Num() * sizeof(int16);
		AppendTag(B, "RIFF");
		AppendU32(B, 36 + DataBytes);
		AppendTag(B, "WAVE");
		AppendTag(B, "fmt ");
		AppendU32(B, 16);            // PCM fmt chunk size
		AppendU16(B, 1);             // AudioFormat = PCM
		AppendU16(B, 1);             // NumChannels = mono
		AppendU32(B, SampleRate);
		AppendU32(B, SampleRate * sizeof(int16)); // ByteRate
		AppendU16(B, sizeof(int16)); // BlockAlign
		AppendU16(B, 16);            // BitsPerSample
		AppendTag(B, "data");
		AppendU32(B, DataBytes);
		for (int16 S : Samples)
		{
			AppendU16(B, static_cast<uint16>(S));
		}
		return B;
	}
}

BEGIN_DEFINE_SPEC(FWavVisemeSpec, "MetaHumanLocalLLM.Backend.WavViseme", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FWavVisemeSpec)

void FWavVisemeSpec::Define()
{
	Describe("FWavAudioReader", [this]()
	{
		It("parses a minimal mono 16-bit PCM WAV", [this]()
		{
			TArray<int16> Samples = { 0, 16384, -16384, 32767, -32768 };
			const TArray<uint8> WavBytes = MakeWav(Samples, 22050);

			FWavData Data;
			const bool bOk = FWavAudioReader::Parse(WavBytes, Data);

			TestTrue("parsed", bOk);
			TestEqual("sample rate", Data.SampleRate, 22050);
			TestEqual("channels", Data.NumChannels, 1);
			TestEqual("bits", Data.BitsPerSample, 16);
			TestEqual("sample count", Data.MonoSamples.Num(), 5);
			if (Data.MonoSamples.Num() == 5)
			{
				TestTrue("0 -> ~0", FMath::IsNearlyZero(Data.MonoSamples[0], 0.001f));
				TestTrue("16384 -> ~0.5", FMath::IsNearlyEqual(Data.MonoSamples[1], 0.5f, 0.01f));
				TestTrue("-16384 -> ~-0.5", FMath::IsNearlyEqual(Data.MonoSamples[2], -0.5f, 0.01f));
			}
		});

		It("rejects non-RIFF bytes", [this]()
		{
			TArray<uint8> Junk = { 'N', 'O', 'P', 'E', 1, 2, 3, 4 };
			FWavData Data;
			TestFalse("rejected", FWavAudioReader::Parse(Junk, Data));
		});

		It("rejects a truncated header", [this]()
		{
			TArray<uint8> Short = { 'R', 'I', 'F', 'F' };
			FWavData Data;
			TestFalse("rejected", FWavAudioReader::Parse(Short, Data));
		});

		It("skips an unknown chunk before data (e.g. LIST) and still finds samples", [this]()
		{
			// Hand-build: RIFF/WAVE, fmt, a bogus "LIST" chunk, then data.
			TArray<int16> Samples = { 100, 200, 300 };
			TArray<uint8> B;
			const uint32 DataBytes = Samples.Num() * sizeof(int16);
			AppendTag(B, "RIFF"); AppendU32(B, 0); AppendTag(B, "WAVE");
			AppendTag(B, "fmt "); AppendU32(B, 16);
			AppendU16(B, 1); AppendU16(B, 1); AppendU32(B, 16000);
			AppendU32(B, 32000); AppendU16(B, 2); AppendU16(B, 16);
			AppendTag(B, "LIST"); AppendU32(B, 4); AppendTag(B, "junk");
			AppendTag(B, "data"); AppendU32(B, DataBytes);
			for (int16 S : Samples) { AppendU16(B, static_cast<uint16>(S)); }

			FWavData Data;
			const bool bOk = FWavAudioReader::Parse(B, Data);
			TestTrue("parsed past the LIST chunk", bOk);
			TestEqual("found all samples", Data.MonoSamples.Num(), 3);
		});

		It("round-trips through ToPcm16", [this]()
		{
			TArray<float> Mono = { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f };
			const TArray<int16> Pcm = FWavAudioReader::ToPcm16(Mono);
			TestEqual("count", Pcm.Num(), 5);
			if (Pcm.Num() == 5)
			{
				TestEqual("0", Pcm[0], (int16)0);
				TestTrue("0.5 -> ~16384", FMath::Abs(Pcm[1] - 16384) <= 1);
				TestTrue("1.0 -> 32767", Pcm[3] == 32767);
				TestTrue("-1.0 -> -32767", Pcm[4] == -32767);
			}
		});
	});

	Describe("FVisemeEnvelope", [this]()
	{
		It("returns no keys for empty input", [this]()
		{
			TestEqual("empty", FVisemeEnvelope::Compute(TArray<float>{}, 22050).Num(), 0);
		});

		It("keeps the mouth closed through silence", [this]()
		{
			TArray<float> Silence;
			Silence.Init(0.0f, 22050); // 1s of silence
			const TArray<FVisemeKey> Keys = FVisemeEnvelope::Compute(Silence, 22050, 0.1f);
			TestTrue("some keys produced", Keys.Num() > 0);
			bool bAllZero = true;
			for (const FVisemeKey& K : Keys) { if (K.JawOpen != 0.0f) { bAllZero = false; break; } }
			TestTrue("all jaw-open zero for silence", bAllZero);
		});

		It("opens fullest at the loudest window and ends with a closed-mouth key", [this]()
		{
			// 3 windows: quiet, loud, medium.
			const int32 SampleRate = 1000;
			const int32 WindowSamples = 100; // 0.1s
			TArray<float> S;
			for (int32 i = 0; i < WindowSamples; ++i) { S.Add(0.1f); } // quiet
			for (int32 i = 0; i < WindowSamples; ++i) { S.Add(1.0f); } // loud
			for (int32 i = 0; i < WindowSamples; ++i) { S.Add(0.5f); } // medium

			const TArray<FVisemeKey> Keys = FVisemeEnvelope::Compute(S, SampleRate, 0.1f);
			TestEqual("three windows plus final close", Keys.Num(), 4);
			if (Keys.Num() == 4)
			{
				TestTrue("loud window is fully open", FMath::IsNearlyEqual(Keys[1].JawOpen, 1.0f, 0.001f));
				TestTrue("quiet < loud", Keys[0].JawOpen < Keys[1].JawOpen);
				TestTrue("medium between", Keys[2].JawOpen > Keys[0].JawOpen && Keys[2].JawOpen < Keys[1].JawOpen);
				TestTrue("times increase", Keys[0].TimeSeconds < Keys[1].TimeSeconds && Keys[1].TimeSeconds < Keys[2].TimeSeconds && Keys[2].TimeSeconds < Keys[3].TimeSeconds);
				TestTrue("first key at t=0", FMath::IsNearlyZero(Keys[0].TimeSeconds));
				TestTrue("final key is closed", FMath::IsNearlyZero(Keys[3].JawOpen));
				TestTrue("final key matches audio duration", FMath::IsNearlyEqual(Keys[3].TimeSeconds, 0.3f, 0.001f));
			}
		});
	});

	Describe("UPiperTextToSpeech::BuildRequestBody", [this]()
	{
		It("wraps the text in a json text field", [this]()
		{
			const FString Body = UPiperTextToSpeech::BuildRequestBody(TEXT("hello"));
			TestTrue("has text field", Body.Contains(TEXT("\"text\":\"hello\"")));
		});
	});

	Describe("float and wide PCM decoding", [this]()
	{
		// Builds a WAV header by hand so the exact shapes seen in the wild can be asserted.
		auto MakeWav = [](uint16 Format, uint16 Bits, const TArray<uint8>& Data, uint32 DeclaredDataSize)
		{
			TArray<uint8> B;
			auto PutStr = [&B](const char* S) { for (int i = 0; i < 4; ++i) B.Add((uint8)S[i]); };
			auto Put32 = [&B](uint32 V) { B.Add(V & 0xFF); B.Add((V >> 8) & 0xFF); B.Add((V >> 16) & 0xFF); B.Add((V >> 24) & 0xFF); };
			auto Put16 = [&B](uint16 V) { B.Add(V & 0xFF); B.Add((V >> 8) & 0xFF); };
			PutStr("RIFF"); Put32(0xFFFFFFFF); PutStr("WAVE");
			PutStr("fmt "); Put32(16);
			Put16(Format); Put16(1); Put32(24000); Put32(24000 * (Bits / 8)); Put16(Bits / 8); Put16(Bits);
			PutStr("data"); Put32(DeclaredDataSize);
			B.Append(Data);
			return B;
		};

		It("decodes 32-bit IEEE float, which is what Kokoro actually returns", [this, MakeWav]()
		{
			// Regression: this was rejected outright, and because a parse failure looks identical
			// to a network error the only symptom was a companion that never spoke.
			TArray<uint8> Data;
			const float Samples[] = { 0.0f, 0.5f, -0.5f, 1.0f };
			for (float F : Samples)
			{
				uint32 Bits; FMemory::Memcpy(&Bits, &F, sizeof(Bits));
				Data.Add(Bits & 0xFF); Data.Add((Bits >> 8) & 0xFF); Data.Add((Bits >> 16) & 0xFF); Data.Add((Bits >> 24) & 0xFF);
			}
			FWavData Out;
			TestTrue("parsed", FWavAudioReader::Parse(MakeWav(3, 32, Data, 0xFFFFFFFF), Out));
			TestEqual("sample count", Out.MonoSamples.Num(), 4);
			TestTrue("mid value survives", FMath::IsNearlyEqual(Out.MonoSamples[1], 0.5f, 0.001f));
			TestTrue("negative survives", FMath::IsNearlyEqual(Out.MonoSamples[2], -0.5f, 0.001f));
		});

		It("survives the 0xFFFFFFFF streaming placeholder data size", [this, MakeWav]()
		{
			// Kokoro declares an unknown-length data chunk. Trusting it would mean reading 4GB.
			TArray<uint8> Data;
			for (int i = 0; i < 8; ++i) { Data.Add(0); Data.Add(0); }
			FWavData Out;
			TestTrue("parsed", FWavAudioReader::Parse(MakeWav(1, 16, Data, 0xFFFFFFFF), Out));
			TestEqual("clamped to real bytes", Out.MonoSamples.Num(), 8);
		});

		It("resolves WAVE_FORMAT_EXTENSIBLE to its subformat", [this]()
		{
			TArray<uint8> B;
			auto PutStr = [&B](const char* S) { for (int i = 0; i < 4; ++i) B.Add((uint8)S[i]); };
			auto Put32 = [&B](uint32 V) { B.Add(V & 0xFF); B.Add((V >> 8) & 0xFF); B.Add((V >> 16) & 0xFF); B.Add((V >> 24) & 0xFF); };
			auto Put16 = [&B](uint16 V) { B.Add(V & 0xFF); B.Add((V >> 8) & 0xFF); };
			PutStr("RIFF"); Put32(0xFFFFFFFF); PutStr("WAVE");
			PutStr("fmt "); Put32(40);
			Put16(0xFFFE); Put16(1); Put32(24000); Put32(24000 * 2); Put16(2); Put16(16);
			Put16(22); Put16(16); Put32(4);
			Put16(1);            // SubFormat = PCM
			for (int i = 0; i < 14; ++i) B.Add(0);
			PutStr("data"); Put32(4);
			for (int i = 0; i < 4; ++i) B.Add(0);
			FWavData Out;
			TestTrue("extensible parsed as pcm", FWavAudioReader::Parse(B, Out));
			TestEqual("two samples", Out.MonoSamples.Num(), 2);
		});
	});
}
