#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LLM/OllamaTypes.h"
#include "OllamaConversation.generated.h"

// Owns the full message history for one conversation. Ollama's /api/chat has no
// server-side session, so the full history is re-sent on every request; this class
// is the single source of truth for that history (mirrors CI-Spatial-Companion-WebXR's
// LLMState.messages pattern).
UCLASS(BlueprintType)
class METAHUMANLOCALLLM_API UOllamaConversation : public UObject
{
	GENERATED_BODY()

public:
	// Clears history and seeds it with a system message (skipped if empty).
	void Initialize(const FString& InSystemPrompt);

	void AppendUserMessage(const FString& Text);
	void AppendAssistantMessage(const FString& Text);

	// Equivalent to calling Initialize again; kept as a distinct name for call-site clarity.
	void Reset(const FString& InSystemPrompt);

	const TArray<FOllamaMessage>& GetHistory() const { return History; }

	bool HasSystemPrompt() const;
	int32 Num() const { return History.Num(); }

private:
	UPROPERTY()
	TArray<FOllamaMessage> History;
};
