#pragma once
 
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "LLM/IChatBackend.h"
#include "Avatar/EmotionMapper.h"
#include "Voice/VisemeEnvelope.h"
#include "MetaHumanCompanionCharacter.generated.h"
 
class UOllamaConversation;
class USpeechToTextComponent;
class UTextToSpeechComponent;
 
// Separate from the BlueprintImplementableEvents below: this is a bindable multicast
// delegate so plain-C++ listeners (e.g. UCompanionHUDWidget) can subscribe too, not
// just a Blueprint subclass overriding an event.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCompanionSpoke, const FString&, Text);
 
// Orchestrates one conversational turn: user text/speech -> Ollama -> emotion mapping
// -> TTS. Deliberately does NOT touch the MetaHuman skeletal mesh/AnimBP directly —
// this class only fires BlueprintImplementableEvents (OnSpeak/OnSetEmotion/
// OnSetVisemeWeights); a thin editor-made Blueprint subclass (BP_CompanionCharacter)
// wires those events to the actual MetaHuman curves, so no binary editor asset is
// required for this class to compile and be unit tested.
UCLASS()
class CICOMPANION_API AMetaHumanCompanionCharacter : public ACharacter
{
	GENERATED_BODY()
 
public:
	AMetaHumanCompanionCharacter();
 
	// Submits a user turn (from the HUD text box, or from STT transcription). Cancels
	// any in-flight response first (barge-in), mirroring CI-Spatial-Companion-WebXR's
	// AbortController-on-new-turn behavior.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM")
	void SubmitUserText(const FString& Text);
 
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM")
	void ResetConversation();

	// Routes the HUD mic control through the configured STT backend. Returns false when the
	// project is using the null backend or a live capture device could not be opened.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Voice")
	bool StartListening();

	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Voice")
	void StopListening();

	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Voice")
	bool IsListening() const;
 
	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM")
	bool IsWaitingForResponse() const { return CurrentRequestHandle != InvalidOllamaRequestHandle; }
 
	// Fired alongside OnSpeak, for plain-C++ listeners (e.g. the HUD widget's chat log).
	UPROPERTY(BlueprintAssignable, Category = "MetaHumanLLM")
	FOnCompanionSpoke OnCompanionSpoke;
 
	virtual void Tick(float DeltaTime) override;
 
protected:
	virtual void BeginPlay() override;
 
	// Fired with each streamed token as it arrives, e.g. for a HUD "typing" effect.
	UFUNCTION(BlueprintImplementableEvent, Category = "MetaHumanLLM")
	void OnPartialResponse(const FString& Token);
 
	// Fired once the full assistant reply is known.
	UFUNCTION(BlueprintImplementableEvent, Category = "MetaHumanLLM")
	void OnSpeak(const FString& Text);
 
	UFUNCTION(BlueprintImplementableEvent, Category = "MetaHumanLLM")
	void OnSetEmotion(EEmotionType Emotion);
 
	// Receives the active TTS jaw timeline. The Blueprint layer maps these weights to the
	// concrete MetaHuman face rig / ARKit curves.
	UFUNCTION(BlueprintImplementableEvent, Category = "MetaHumanLLM")
	void OnSetVisemeWeights(const TMap<FName, float>& Weights);
 
	UFUNCTION(BlueprintImplementableEvent, Category = "MetaHumanLLM")
	void OnConversationError(const FString& ErrorMessage);
 
	// BlueprintReadOnly requires at least protected access (UHT rejects it on private
	// members); these are otherwise only ever written to from within this class.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MetaHumanLLM")
	TObjectPtr<UOllamaConversation> Conversation;
 
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MetaHumanLLM")
	TObjectPtr<USpeechToTextComponent> SpeechToText;
 
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MetaHumanLLM")
	TObjectPtr<UTextToSpeechComponent> TextToSpeech;
 
private:
	void HandleToken(const FString& Token);
	void HandleComplete(const FString& FullText);
	void HandleError(const FString& ErrorMessage);
 
	UFUNCTION()
	void HandleTranscribed(const FString& Text);
 
	UFUNCTION()
	void HandleVisemesReady(const TArray<FVisemeKey>& Keys);

	UFUNCTION()
	void HandleSpeechFinished(const FString& SpokenText);

	IChatBackend* GetBackend() const;
	void StopVisemePlayback();
 
	FOllamaRequestHandle CurrentRequestHandle = InvalidOllamaRequestHandle;
 
	TArray<FVisemeKey> ActiveVisemeKeys;
	float SpeechStartTime = 0.0f;
	bool bPlayingVisemes = false;
};
