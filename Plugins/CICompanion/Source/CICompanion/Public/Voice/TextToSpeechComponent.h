#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TextToSpeechComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSpeechFinished, const FString&, SpokenText);

// Base class for pluggable text-to-speech backends. See UNullTextToSpeech for the
// default (pass-through, no audio) implementation; a real local TTS/lip-sync plugin
// (e.g. Piper, or a MetaHuman runtime lip-sync plugin) should subclass this.
UCLASS(Abstract, Blueprintable, ClassGroup = (MetaHumanLLM), meta = (BlueprintSpawnableComponent))
class CICOMPANION_API UTextToSpeechComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Fired once synthesis/playback of a Speak() call completes (or immediately, for
	// backends with no real audio, so downstream logic — HUD, emotes — still runs).
	UPROPERTY(BlueprintAssignable, Category = "MetaHumanLLM|Voice")
	FOnSpeechFinished OnSpeechFinished;

	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Voice")
	virtual void Speak(const FString& Text) PURE_VIRTUAL(UTextToSpeechComponent::Speak, );

	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Voice")
	virtual void StopSpeaking() PURE_VIRTUAL(UTextToSpeechComponent::StopSpeaking, );
};
