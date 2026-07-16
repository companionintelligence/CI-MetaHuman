#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpeechToTextComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSpeechTranscribed, const FString&, TranscribedText);

// Base class for pluggable speech-to-text backends. See UNullSpeechToText for the
// default (no-op) implementation; a real local STT plugin (e.g. a whisper.cpp wrapper)
// should subclass this and override StartListening/StopListening.
UCLASS(Abstract, Blueprintable, ClassGroup = (MetaHumanLLM), meta = (BlueprintSpawnableComponent))
class CICOMPANION_API USpeechToTextComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "MetaHumanLLM|Voice")
	FOnSpeechTranscribed OnTranscribed;

	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Voice")
	virtual void StartListening() PURE_VIRTUAL(USpeechToTextComponent::StartListening, );

	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Voice")
	virtual void StopListening() PURE_VIRTUAL(USpeechToTextComponent::StopListening, );

	// Backends with a live capture path override this so callers can distinguish a requested
	// mic state from an active one (for example, when no host audio device is available).
	virtual bool IsListening() const { return false; }
};
