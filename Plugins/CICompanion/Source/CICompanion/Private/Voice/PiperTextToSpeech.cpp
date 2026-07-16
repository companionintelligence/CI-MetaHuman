#include "Voice/PiperTextToSpeech.h"
#include "Voice/WavAudioReader.h"
#include "Settings/MetaHumanLLMSettings.h"
#include "HttpModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "CICompanion.h"

FString UPiperTextToSpeech::BuildRequestBody(const FString& Text)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("text"), Text);

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

void UPiperTextToSpeech::Speak(const FString& Text)
{
	if (Text.IsEmpty())
	{
		OnSpeechFinished.Broadcast(Text);
		return;
	}

	const UMetaHumanLLMSettings* Settings = GetDefault<UMetaHumanLLMSettings>();
	FString BaseUrl = Settings->PiperServerUrl;
	if (BaseUrl.IsEmpty())
	{
		// No server configured: behave like the null backend so downstream logic still runs.
		OnSpeechFinished.Broadcast(Text);
		return;
	}
	if (BaseUrl.EndsWith(TEXT("/")))
	{
		BaseUrl.LeftChopInline(1);
	}

	PendingText = Text;

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(BaseUrl);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("audio/wav"));
	Request->SetContentAsString(BuildRequestBody(Text));
	Request->OnProcessRequestComplete().BindUObject(this, &UPiperTextToSpeech::HandleSynthesisComplete);

	if (!Request->ProcessRequest())
	{
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("PiperTextToSpeech: failed to start synthesis request"));
		OnSpeechFinished.Broadcast(Text);
	}
}

void UPiperTextToSpeech::HandleSynthesisComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!bWasSuccessful || !Response.IsValid())
	{
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("PiperTextToSpeech: synthesis request failed"));
		OnSpeechFinished.Broadcast(PendingText);
		return;
	}

	FWavData Wav;
	if (!FWavAudioReader::Parse(Response->GetContent(), Wav) || Wav.MonoSamples.Num() == 0)
	{
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("PiperTextToSpeech: response was not a decodable PCM WAV"));
		OnSpeechFinished.Broadcast(PendingText);
		return;
	}

	// Publish the jaw-open timeline for the avatar layer, then start audio playback.
	const TArray<FVisemeKey> Envelope = FVisemeEnvelope::Compute(Wav.MonoSamples, Wav.SampleRate);
	OnVisemeEnvelopeReady.Broadcast(Envelope);

	PlayDecodedWav(Wav);

	// If playback couldn't start (e.g. headless / no audio device), signal completion now so
	// downstream logic isn't left waiting; otherwise wait for the audio-finished callback.
	if (!ActiveAudio)
	{
		OnSpeechFinished.Broadcast(PendingText);
	}
}

void UPiperTextToSpeech::PlayDecodedWav(const FWavData& Wav)
{
	UWorld* World = GetWorld();
	if (!World || Wav.SampleRate <= 0)
	{
		return;
	}

	USoundWaveProcedural* Sound = NewObject<USoundWaveProcedural>();
	Sound->SetSampleRate(Wav.SampleRate);
	Sound->NumChannels = 1;
	Sound->Duration = Wav.DurationSeconds();
	Sound->bLooping = false;

	const TArray<int16> Pcm = FWavAudioReader::ToPcm16(Wav.MonoSamples);
	Sound->QueueAudio(reinterpret_cast<const uint8*>(Pcm.GetData()), Pcm.Num() * sizeof(int16));

	ActiveAudio = UGameplayStatics::SpawnSound2D(World, Sound);
	if (ActiveAudio)
	{
		ActiveAudio->OnAudioFinished.AddDynamic(this, &UPiperTextToSpeech::HandleAudioFinished);
	}
}

void UPiperTextToSpeech::HandleAudioFinished()
{
	if (ActiveAudio)
	{
		ActiveAudio->OnAudioFinished.RemoveDynamic(this, &UPiperTextToSpeech::HandleAudioFinished);
		ActiveAudio = nullptr;
	}
	OnSpeechFinished.Broadcast(PendingText);
}

void UPiperTextToSpeech::StopSpeaking()
{
	if (ActiveAudio)
	{
		ActiveAudio->OnAudioFinished.RemoveDynamic(this, &UPiperTextToSpeech::HandleAudioFinished);
		ActiveAudio->Stop();
		ActiveAudio = nullptr;
	}
}
