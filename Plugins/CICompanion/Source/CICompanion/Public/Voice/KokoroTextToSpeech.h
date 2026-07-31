#pragma once

#include "CoreMinimal.h"
#include "Voice/TextToSpeechComponent.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "KokoroTextToSpeech.generated.h"

class UAudioComponent;

// Fired once synthesized speech is decoded: the text that was spoken and the clip's true length.
// The duration is the point of this delegate — FVisemePlanner can only ESTIMATE timing from a
// speaking rate, and an estimate drifts against real audio over a long utterance. Handing back the
// measured length lets the planner rescale so the mouth cannot fall out of sync with the voice.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSpeechAudioReady, const FString&, Text, float, DurationSeconds);

// Text-to-speech against AMD Lemonade Server's Kokoro endpoint (`/api/v1/audio/speech`).
//
// Why a sibling of UPiperTextToSpeech rather than a tweak to it: the contracts genuinely differ.
// Piper takes {"text": ...} at a bare URL; Kokoro takes the OpenAI shape
// ({"model", "input", "voice", "response_format"}) at a pathed endpoint. Sharing one class would
// mean a mode flag threaded through every method for no benefit.
//
// The reason to prefer it: Lemonade serves chat, Whisper transcription AND Kokoro speech from one
// ROCm-accelerated OpenAI-compatible service, so adopting it collapses three deployments into one
// on AMD hardware we already run. Kokoro is also the better-sounding voice.
//
// NOTE the path is `/api/v1/...`, not the `/v1/...` the marketing implies — verified by probe
// against a running instance on beta-1.
UCLASS(ClassGroup = (MetaHumanLLM), meta = (BlueprintSpawnableComponent))
class CICOMPANION_API UKokoroTextToSpeech : public UTextToSpeechComponent
{
	GENERATED_BODY()

public:
	virtual void Speak(const FString& Text) override;
	virtual void StopSpeaking() override;

	UPROPERTY(BlueprintAssignable, Category = "MetaHumanLLM|Voice")
	FOnSpeechAudioReady OnSpeechAudioReady;

	// Pure helpers, exposed for unit testing without a server.
	static FString BuildRequestBody(const FString& Text, const FString& Model, const FString& Voice);
	static FString BuildEndpointUrl(const FString& BaseUrl);

private:
	void HandleSynthesisComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	UFUNCTION()
	void HandleAudioFinished();

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ActiveAudio;

	FHttpRequestPtr ActiveRequest;
	FString PendingText;
};
