#include "Voice/WhisperSpeechToText.h"
#include "Settings/MetaHumanLLMSettings.h"
#include "HttpModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "AudioCapture.h"
#include "CICompanion.h"

namespace
{
	// Appends the UTF-8 bytes of Text to Out (multipart framing is ASCII, so UTF-8 is safe).
	void AppendUtf8(TArray<uint8>& Out, const FString& Text)
	{
		FTCHARToUTF8 Converted(*Text);
		Out.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
	}
}

FString UWhisperSpeechToText::MultipartContentType(const FString& Boundary)
{
	return FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary);
}

TArray<uint8> UWhisperSpeechToText::BuildMultipartFormData(const TArray<uint8>& WavBytes, const FString& Boundary, const FString& FileName)
{
	TArray<uint8> Body;

	// file part
	AppendUtf8(Body, FString::Printf(TEXT("--%s\r\n"), *Boundary));
	AppendUtf8(Body, FString::Printf(TEXT("Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"), *FileName));
	AppendUtf8(Body, TEXT("Content-Type: audio/wav\r\n\r\n"));
	Body.Append(WavBytes);
	AppendUtf8(Body, TEXT("\r\n"));

	// response_format part
	AppendUtf8(Body, FString::Printf(TEXT("--%s\r\n"), *Boundary));
	AppendUtf8(Body, TEXT("Content-Disposition: form-data; name=\"response_format\"\r\n\r\n"));
	AppendUtf8(Body, TEXT("json\r\n"));

	// closing boundary
	AppendUtf8(Body, FString::Printf(TEXT("--%s--\r\n"), *Boundary));

	return Body;
}

bool UWhisperSpeechToText::ParseTranscriptionResponse(const FString& ResponseBody, FString& OutText)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	FString Text;
	if (!JsonObject->TryGetStringField(TEXT("text"), Text))
	{
		return false;
	}

	Text.TrimStartAndEndInline();
	if (Text.IsEmpty())
	{
		return false;
	}

	OutText = Text;
	return true;
}

void UWhisperSpeechToText::StartListening()
{
	if (bIsListening)
	{
		return;
	}

	if (!AudioCapture)
	{
		AudioCapture = NewObject<UAudioCapture>(this, TEXT("AudioCapture"));
	}

	if (!AudioCapture)
	{
		UE_LOG(LogMetaHumanLLM, Error, TEXT("WhisperSpeechToText: Failed to create audio capture"));
		return;
	}

	if (!bCaptureStreamOpened)
	{
		if (!AudioCapture->OpenDefaultAudioStream())
		{
			UE_LOG(LogMetaHumanLLM, Error, TEXT("WhisperSpeechToText: Failed to open default audio stream"));
			return;
		}
		bCaptureStreamOpened = true;
	}

	{
		FScopeLock Lock(&CapturedFloatsMutex);
		CapturedFloats.Empty();
	}

	if (!bCaptureDelegateRegistered)
	{
		AudioCaptureHandle = AudioCapture->AddGeneratorDelegate([this](const float* InAudio, int32 NumSamples)
		{
			if (!bIsListening || !AudioCapture)
			{
				return;
			}

			const int32 Channels = AudioCapture->GetNumChannels();
			if (Channels <= 0)
			{
				return;
			}

			FScopeLock Lock(&CapturedFloatsMutex);
			const int32 NumFrames = NumSamples / Channels;
			for (int32 Frame = 0; Frame < NumFrames; ++Frame)
			{
				float Sum = 0.0f;
				for (int32 Channel = 0; Channel < Channels; ++Channel)
				{
					Sum += InAudio[Frame * Channels + Channel];
				}
				CapturedFloats.Add(Sum / Channels);
			}
		});
		bCaptureDelegateRegistered = true;
	}

	bIsListening = true;
	AudioCapture->StartCapturingAudio();
	if (!AudioCapture->IsCapturingAudio())
	{
		bIsListening = false;
		AudioCapture->RemoveGeneratorDelegate(AudioCaptureHandle);
		bCaptureDelegateRegistered = false;
		UE_LOG(LogMetaHumanLLM, Error, TEXT("WhisperSpeechToText: Failed to start default audio capture"));
		return;
	}

	UE_LOG(LogMetaHumanLLM, Log, TEXT("WhisperSpeechToText: Started default microphone capture"));
}

void UWhisperSpeechToText::StopListening()
{
	if (!bIsListening)
	{
		return;
	}

	bIsListening = false;

	if (AudioCapture)
	{
		AudioCapture->StopCapturingAudio();
		if (bCaptureDelegateRegistered)
		{
			AudioCapture->RemoveGeneratorDelegate(AudioCaptureHandle);
			bCaptureDelegateRegistered = false;
		}
	}

	TArray<float> CapturedBuffer;
	{
		FScopeLock Lock(&CapturedFloatsMutex);
		CapturedBuffer = MoveTemp(CapturedFloats);
		CapturedFloats.Empty();
	}

	UE_LOG(LogMetaHumanLLM, Log, TEXT("WhisperSpeechToText: Stopped live microphone capture. Captured %d samples."), CapturedBuffer.Num());

	if (CapturedBuffer.Num() > 0)
	{
		int32 DeviceSampleRate = 48000;
		if (AudioCapture)
		{
			FAudioCaptureDeviceInfo DeviceInfo;
			if (AudioCapture->GetAudioCaptureDeviceInfo(DeviceInfo) && DeviceInfo.SampleRate > 0)
			{
				DeviceSampleRate = DeviceInfo.SampleRate;
			}
		}

		TArray<uint8> WavBytes = ConvertToWav(CapturedBuffer, DeviceSampleRate);
		TranscribeWav(WavBytes);
	}
}

TArray<uint8> UWhisperSpeechToText::ConvertToWav(const TArray<float>& FloatSamples, int32 InputSampleRate)
{
	int32 TargetSampleRate = 16000;
	TArray<float> ResampledSamples;

	if (InputSampleRate == TargetSampleRate || FloatSamples.Num() == 0)
	{
		ResampledSamples = FloatSamples;
	}
	else
	{
		double Ratio = (double)InputSampleRate / TargetSampleRate;
		int32 OutputLength = FMath::TruncToInt(FloatSamples.Num() / Ratio);
		ResampledSamples.SetNumZeroed(OutputLength);

		for (int32 i = 0; i < OutputLength; ++i)
		{
			double SourceIndex = i * Ratio;
			int32 IndexA = FMath::TruncToInt(SourceIndex);
			int32 IndexB = FMath::Min(IndexA + 1, FloatSamples.Num() - 1);
			double Fraction = SourceIndex - IndexA;

			ResampledSamples[i] = FMath::Lerp(FloatSamples[IndexA], FloatSamples[IndexB], (float)Fraction);
		}
	}

	TArray<int16> Pcm16Samples;
	Pcm16Samples.SetNumUninitialized(ResampledSamples.Num());
	for (int32 i = 0; i < ResampledSamples.Num(); ++i)
	{
		float ScaledSample = ResampledSamples[i] * 32767.0f;
		Pcm16Samples[i] = static_cast<int16>(FMath::Clamp(ScaledSample, -32768.0f, 32767.0f));
	}

	TArray<uint8> WavBytes;
	int32 NumSamples = Pcm16Samples.Num();
	int32 NumChannels = 1;
	int32 BitsPerSample = 16;
	int32 Subchunk2Size = NumSamples * NumChannels * BitsPerSample / 8;
	int32 ChunkSize = 36 + Subchunk2Size;
	int32 ByteRate = TargetSampleRate * NumChannels * BitsPerSample / 8;
	int32 BlockAlign = NumChannels * BitsPerSample / 8;

	WavBytes.SetNumUninitialized(44 + Subchunk2Size);
	uint8* Data = WavBytes.GetData();

	FMemory::Memcpy(Data + 0, "RIFF", 4);
	FMemory::Memcpy(Data + 4, &ChunkSize, 4);
	FMemory::Memcpy(Data + 8, "WAVE", 4);

	FMemory::Memcpy(Data + 12, "fmt ", 4);
	int32 Subchunk1Size = 16;
	FMemory::Memcpy(Data + 16, &Subchunk1Size, 4);
	int16 AudioFormat = 1;
	FMemory::Memcpy(Data + 20, &AudioFormat, 2);
	int16 ChannelsVal = NumChannels;
	FMemory::Memcpy(Data + 22, &ChannelsVal, 2);
	FMemory::Memcpy(Data + 24, &TargetSampleRate, 4);
	FMemory::Memcpy(Data + 28, &ByteRate, 4);
	int16 AlignVal = BlockAlign;
	FMemory::Memcpy(Data + 32, &AlignVal, 2);
	int16 BitsVal = BitsPerSample;
	FMemory::Memcpy(Data + 34, &BitsVal, 2);

	FMemory::Memcpy(Data + 36, "data", 4);
	FMemory::Memcpy(Data + 40, &Subchunk2Size, 4);

	if (Subchunk2Size > 0)
	{
		FMemory::Memcpy(Data + 44, Pcm16Samples.GetData(), Subchunk2Size);
	}

	return WavBytes;
}

void UWhisperSpeechToText::TranscribeWav(const TArray<uint8>& WavBytes)
{
	if (WavBytes.Num() == 0)
	{
		return;
	}

	const UMetaHumanLLMSettings* Settings = GetDefault<UMetaHumanLLMSettings>();
	FString BaseUrl = Settings->WhisperServerUrl;
	if (BaseUrl.IsEmpty())
	{
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("WhisperSpeechToText: WhisperServerUrl is empty; cannot transcribe"));
		return;
	}
	if (BaseUrl.EndsWith(TEXT("/")))
	{
		BaseUrl.LeftChopInline(1);
	}

	// A fixed boundary is fine — it just needs to be a token that does not appear in the
	// payload; a WAV header never contains this ASCII string.
	const FString Boundary = TEXT("----MetaHumanLLMWhisperBoundary7a1b");
	const TArray<uint8> Body = BuildMultipartFormData(WavBytes, Boundary, TEXT("audio.wav"));

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(BaseUrl + Settings->WhisperEndpointPath);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), MultipartContentType(Boundary));
	Request->SetContent(Body);
	Request->OnProcessRequestComplete().BindUObject(this, &UWhisperSpeechToText::HandleTranscriptionComplete);

	if (!Request->ProcessRequest())
	{
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("WhisperSpeechToText: failed to start transcription request"));
	}
}

void UWhisperSpeechToText::HandleTranscriptionComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!bWasSuccessful || !Response.IsValid())
	{
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("WhisperSpeechToText: transcription request failed"));
		return;
	}

	FString Text;
	if (ParseTranscriptionResponse(Response->GetContentAsString(), Text))
	{
		OnTranscribed.Broadcast(Text);
	}
	else
	{
		UE_LOG(LogMetaHumanLLM, Verbose, TEXT("WhisperSpeechToText: empty or unparseable transcription"));
	}
}
