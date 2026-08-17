#include "Voice/KokoroTextToSpeech.h"
#include "CICompanionLog.h"

#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HttpModule.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Settings/MetaHumanLLMSettings.h"
#include "Sound/SoundWaveProcedural.h"
#include "Voice/WavAudioReader.h"


FString UKokoroTextToSpeech::BuildRequestBody(const FString& Text, const FString& Model, const FString& Voice)
{
	// OpenAI /audio/speech shape. response_format is pinned to wav because FWavAudioReader is the
	// only decoder here; asking for mp3 would return bytes nothing in this codebase can read.
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), Model);
	Root->SetStringField(TEXT("input"), Text);
	Root->SetStringField(TEXT("voice"), Voice);
	Root->SetStringField(TEXT("response_format"), TEXT("wav"));

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}

FString UKokoroTextToSpeech::BuildEndpointUrl(const FString& BaseUrl)
{
	FString Base = BaseUrl;
	while (Base.EndsWith(TEXT("/")))
	{
		Base.LeftChopInline(1);
	}
	// Accept a base given either as the server root or as the API root, so operators can paste
	// whichever Lemonade printed at them without it silently 404ing.
	if (Base.EndsWith(TEXT("/api/v1")))
	{
		return Base + TEXT("/audio/speech");
	}
	return Base + TEXT("/api/v1/audio/speech");
}

void UKokoroTextToSpeech::Speak(const FString& Text)
{
	const UMetaHumanLLMSettings* Settings = GetDefault<UMetaHumanLLMSettings>();
	if (!Settings || Settings->KokoroServerUrl.IsEmpty())
	{
		UE_LOG(LogCompanionVoice, Warning, TEXT("Kokoro: KokoroServerUrl is empty; cannot synthesize."));
		OnSpeechFinished.Broadcast(Text);
		return;
	}

	StopSpeaking();	// barge-in: a new line cancels the old one rather than overlapping voices
	PendingText = Text;

	ActiveRequest = FHttpModule::Get().CreateRequest();
	ActiveRequest->SetURL(BuildEndpointUrl(Settings->KokoroServerUrl));
	ActiveRequest->SetVerb(TEXT("POST"));
	ActiveRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ActiveRequest->SetHeader(TEXT("Accept"), TEXT("audio/wav"));
	// The ONLY HTTP client in this plugin without a timeout. Both chat clients and the phoneme
	// aligner set one; synthesis did not, so a Lemonade that accepted the connection and then went
	// quiet left the reply permanently un-voiced -- no audio, no callback, no error, and no way
	// back for that turn. That is the same hang the chat clients' timeout was added to close, and
	// it is worse here because HandleCompanionSpoke has already armed an utterance by this point.
	// Shares the chat budget rather than inventing a second knob: an operator who raised one for a
	// slow box meant both.
	ActiveRequest->SetTimeout(Settings->ChatRequestTimeoutSeconds);
	ActiveRequest->SetContentAsString(
		BuildRequestBody(Text, Settings->KokoroModel, Settings->KokoroVoice));
	ActiveRequest->OnProcessRequestComplete().BindUObject(this, &UKokoroTextToSpeech::HandleSynthesisComplete);
	ActiveRequest->ProcessRequest();
}

void UKokoroTextToSpeech::StopSpeaking()
{
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
	// Cleared with the request that owned it. Left set, it would be reported as the "finished" text
	// of whatever utterance happened to complete next.
	PendingText.Empty();
	if (ActiveAudio)
	{
		ActiveAudio->OnAudioFinished.RemoveDynamic(this, &UKokoroTextToSpeech::HandleAudioFinished);
		ActiveAudio->Stop();
		ActiveAudio = nullptr;
	}
}

void UKokoroTextToSpeech::HandleSynthesisComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	// IDENTITY GUARD, matching UKokoroPhonemeAligner. Cancelling an HTTP request does not un-bind
	// its completion delegate: a cancelled request still calls back, typically with
	// bWasSuccessful=false. On barge-in Speak() calls StopSpeaking() and then immediately starts a
	// NEW request, so the dead one's callback arrives with ActiveRequest already pointing at the
	// live request -- and the unconditional Reset() below would drop the new request's handle on
	// the floor. StopSpeaking could then no longer cancel it, so the reply the user interrupted
	// would still be spoken over the one they interrupted it with.
	if (!ActiveRequest.IsValid() || Request.Get() != ActiveRequest.Get())
	{
		return;
	}

	ActiveRequest.Reset();
	const FString CompletedText = PendingText;

	if (!bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
	{
		UE_LOG(LogCompanionVoice, Warning, TEXT("Kokoro: synthesis failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
		PendingText.Empty();
		OnSpeechFinished.Broadcast(CompletedText);
		return;
	}

	FWavData Wav;
	if (!FWavAudioReader::Parse(Response->GetContent(), Wav) || Wav.MonoSamples.Num() == 0)
	{
		UE_LOG(LogCompanionVoice, Warning, TEXT("Kokoro: response was not decodable WAV (%d bytes)."),
			Response->GetContent().Num());
		PendingText.Empty();
		OnSpeechFinished.Broadcast(CompletedText);
		return;
	}

	UWorld* World = GetWorld();
	if (World && Wav.SampleRate > 0)
	{
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
			ActiveAudio->OnAudioFinished.AddDynamic(this, &UKokoroTextToSpeech::HandleAudioFinished);
		}
	}

	UE_LOG(LogCompanionVoice, Log, TEXT("Kokoro: %.2fs of speech for %d chars."),
		Wav.DurationSeconds(), CompletedText.Len());

	// Broadcast AFTER the audio component exists so the face clock starts on the same frame as
	// playback, rather than on the frame the HTTP response landed.
	OnSpeechAudioReady.Broadcast(CompletedText, Wav.DurationSeconds());
}

void UKokoroTextToSpeech::HandleAudioFinished()
{
	if (ActiveAudio)
	{
		ActiveAudio->OnAudioFinished.RemoveDynamic(this, &UKokoroTextToSpeech::HandleAudioFinished);
		ActiveAudio = nullptr;
	}
	const FString CompletedText = PendingText;
	PendingText.Empty();
	OnSpeechFinished.Broadcast(CompletedText);
}
