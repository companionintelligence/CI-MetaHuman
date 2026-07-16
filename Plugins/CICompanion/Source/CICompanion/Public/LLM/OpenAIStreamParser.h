#pragma once

#include "CoreMinimal.h"

// Incrementally parses an OpenAI-style Server-Sent-Events chat stream (the
// /v1/chat/completions "stream": true response). Each event is a line of the form
//   data: {"choices":[{"delta":{"content":"..."},"finish_reason":null}]}
// terminated by the sentinel line `data: [DONE]`. Like FOllamaStreamParser this is a
// plain, HTTP-free class fed raw bytes as they arrive (a chunk may split a line, contain
// several, or none) so it can be unit tested without a live server or the HTTP module.
class CICOMPANION_API FOpenAIStreamParser
{
public:
	struct FParsedEvent
	{
		FString Content;    // Delta text for this event (may be empty on the final event).
		bool bDone = false; // True for the `[DONE]` sentinel or a non-null finish_reason.
	};

	// Returns any newly-completed events found after appending NewData to the buffer.
	// Non-`data:` lines (SSE comments, blank separators, `event:`/`id:` fields) and
	// malformed JSON payloads are skipped rather than treated as fatal.
	TArray<FParsedEvent> Feed(const FString& NewData);

	void Reset();

private:
	FString Buffer;
};
