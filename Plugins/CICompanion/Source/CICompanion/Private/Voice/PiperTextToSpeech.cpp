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

	StopSpeaking();

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
	ActiveRequest = Request;

	if (!Request->ProcessRequest())
	{
		ActiveRequest.Reset();
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("PiperTextToSpeech: failed to start synthesis request"));
		OnSpeechFinished.Broadcast(Text);
		PendingText.Empty();
	}
}

void UPiperTextToSpeech::HandleSynthesisComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!ActiveRequest.IsValid() || ActiveRequest.Get() != Request.Get())
	{
		return;
	}

	ActiveRequest.Reset();
	const FString CompletedText = PendingText;

	if (!bWasSuccessful || !Response.IsValid())
	{
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("PiperTextToSpeech: synthesis request failed"));
		OnSpeechFinished.Broadcast(CompletedText);
		PendingText.Empty();
		return;
	}

	FWavData Wav;
	if (!FWavAudioReader::Parse(Response->GetContent(), Wav) || Wav.MonoSamples.Num() == 0)
	{
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("PiperTextToSpeech: response was not a decodable PCM WAV"));
		OnSpeechFinished.Broadcast(CompletedText);
		PendingText.Empty();
		return;
	}

	const TArray<FVisemeKey> Envelope = FVisemeEnvelope::Compute(Wav.MonoSamples, Wav.SampleRate);
	PlayDecodedWav(Wav);

	if (ActiveAudio)
	{
		// Start the face clock only after the sound component exists, so it is anchored to the
		// same game frame as audio playback rather than to the completed HTTP request.
		OnVisemeEnvelopeReady.Broadcast(Envelope);
	}
	else
	{
		OnSpeechFinished.Broadcast(CompletedText);
		PendingText.Empty();
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
	const FString CompletedText = MoveTemp(PendingText);
	OnSpeechFinished.Broadcast(CompletedText);
}

void UPiperTextToSpeech::StopSpeaking()
{
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->OnProcessRequestComplete().Unbind();
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}

	if (ActiveAudio)
	{
		ActiveAudio->OnAudioFinished.RemoveDynamic(this, &UPiperTextToSpeech::HandleAudioFinished);
		ActiveAudio->Stop();
		ActiveAudio = nullptr;
	}

	PendingText.Empty();
}
