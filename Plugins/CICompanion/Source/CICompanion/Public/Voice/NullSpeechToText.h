#pragma once

#include "CoreMinimal.h"
#include "Voice/SpeechToTextComponent.h"
#include "NullSpeechToText.generated.h"

// Default STT backend: does not listen to the microphone at all. Text input keeps
// working end-to-end through it (the HUD's text box calls the orchestrator directly),
// so the rest of the loop is testable with no third-party STT plugin installed.
// Swap for a real local STT component once one is chosen (see Docs/FOLLOW-UP.md).
UCLASS(meta = (DisplayName = "Null Speech To Text (text input only)"))
class CICOMPANION_API UNullSpeechToText : public USpeechToTextComponent
{
	GENERATED_BODY()

public:
	virtual void StartListening() override;
	virtual void StopListening() override;
};
