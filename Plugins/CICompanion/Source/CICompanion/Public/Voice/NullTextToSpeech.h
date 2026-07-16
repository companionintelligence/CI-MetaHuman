#pragma once

#include "CoreMinimal.h"
#include "Voice/TextToSpeechComponent.h"
#include "NullTextToSpeech.generated.h"

// Default TTS backend: no audio synthesis. Immediately fires OnSpeechFinished so the
// rest of the loop (emotion mapping, HUD chat log, etc.) is exercisable end-to-end with
// no third-party TTS/lip-sync plugin installed. Swap for a real local TTS component once
// one is chosen (see Docs/FOLLOW-UP.md).
UCLASS(meta = (DisplayName = "Null Text To Speech (no audio)"))
class CICOMPANION_API UNullTextToSpeech : public UTextToSpeechComponent
{
	GENERATED_BODY()

public:
	virtual void Speak(const FString& Text) override;
	virtual void StopSpeaking() override;
};
