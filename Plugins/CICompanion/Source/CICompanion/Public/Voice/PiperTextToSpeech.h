#pragma once

#include "CoreMinimal.h"
#include "Voice/TextToSpeechComponent.h"
#include "Voice/VisemeEnvelope.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "PiperTextToSpeech.generated.h"

class UAudioComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVisemeEnvelopeReady, const TArray<FVisemeKey>&, Keys);

// Text-to-speech against a local Piper HTTP server (the planning doc's Piper reference).
// Speak() POSTs the text, receives WAV audio, decodes it, plays it through a procedural
// sound wave, and publishes an amplitude-derived jaw-open timeline (OnVisemeEnvelopeReady)
// so the avatar layer can drive the MetaHuman's ARKit `jawOpen` curve in sync — the concrete
// backend for AMetaHumanCompanionCharacter::OnSetVisemeWeights that FOLLOW-UP.md called for.
UCLASS(ClassGroup = (MetaHumanLLM), meta = (BlueprintSpawnableComponent))
class CICOMPANION_API UPiperTextToSpeech : public UTextToSpeechComponent
{
	GENERATED_BODY()

public:
	virtual void Speak(const FString& Text) override;
	virtual void StopSpeaking() override;

	// Fired when synthesized audio has been decoded and its jaw-open timeline computed.
	UPROPERTY(BlueprintAssignable, Category = "MetaHumanLLM|Voice")
	FOnVisemeEnvelopeReady OnVisemeEnvelopeReady;

	// Pure helper, exposed for unit testing: builds the Piper request JSON ({"text": ...}).
	static FString BuildRequestBody(const FString& Text);

private:
	void HandleSynthesisComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	void PlayDecodedWav(const struct FWavData& Wav);

	UFUNCTION()
	void HandleAudioFinished();

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ActiveAudio;

	FHttpRequestPtr ActiveRequest;

	FString PendingText;
};
