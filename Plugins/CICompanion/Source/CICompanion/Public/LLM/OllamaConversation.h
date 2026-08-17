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
class CICOMPANION_API UOllamaConversation : public UObject
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

	// Upper bound on retained user+assistant PAIRS, excluding the system prompt. 0 or negative
	// means unlimited.
	//
	// WHY A CAP IS NOT OPTIONAL HERE. Ollama's /api/chat has no server-side session, so the ENTIRE
	// transcript is re-sent on every single turn. An uncapped history therefore does not merely use
	// more memory -- it makes every turn slower and more expensive than the last, until the request
	// exceeds the model's context window. What happens then is the worst part: the server silently
	// truncates from the FRONT, which is where the system prompt lives, so the first thing a
	// long conversation loses is her persona. She does not error; she stops being Eliza.
	void SetMaxRetainedTurns(int32 InMaxTurns);
	int32 GetMaxRetainedTurns() const { return MaxRetainedTurns; }

	// --- Persistence: the difference between a companion and a chatbot with a face ---

	// Writes the transcript to AbsPath as JSON. Returns false if the file could not be written.
	//
	// The system prompt is deliberately NOT saved. It is configuration, not conversation: persisting
	// it would pin whatever persona was live when the file was first written, so an operator editing
	// MHLLM_SYSTEM_PROMPT would find the change silently ignored forever afterwards on any machine
	// that had already talked to her once. Initialize() seeds the current prompt on load instead.
	bool SaveToJson(const FString& AbsPath) const;

	// Replaces any non-system history with the transcript at AbsPath, preserving whatever system
	// prompt is currently seeded. Returns false if the file is missing or unreadable -- both of
	// which are ordinary (first run), so callers should not treat false as an error worth surfacing.
	bool LoadFromJson(const FString& AbsPath);

	// Where a companion's memory lives by default. Under Saved/ because it is per-installation
	// state, not content: it must not end up in the cook, and it must survive one.
	static FString DefaultMemoryPath();

private:
	// Evicts the oldest exchanges once the cap is exceeded, in WHOLE PAIRS and never touching a
	// system prompt at index 0.
	//
	// Whole pairs because a transcript that begins with an assistant turn reads to the model as
	// though it had spoken unprompted, and models answer that by continuing their own imagined
	// previous turn rather than by addressing the user.
	void TrimToCap();

	// Removes any leading non-user turns after the system prompt, so the transcript always opens on
	// a user turn. A history starting with an assistant message reads to the model as though it had
	// spoken unprompted, and it answers by continuing that imagined turn rather than addressing the
	// user. Reachable from a persisted file as well as from a trim, so it is enforced in both.
	void DropLeadingNonUserTurns();

	UPROPERTY()
	TArray<FOllamaMessage> History;

	UPROPERTY()
	int32 MaxRetainedTurns = 24;
};
