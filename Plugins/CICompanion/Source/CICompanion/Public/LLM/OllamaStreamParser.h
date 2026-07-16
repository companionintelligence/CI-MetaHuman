#pragma once

#include "CoreMinimal.h"

// Incrementally parses Ollama's newline-delimited JSON /api/chat stream. Feed raw
// bytes as they arrive (in any chunk size — a chunk may split a JSON line in half,
// contain several complete lines, or contain none); a trailing partial line is
// buffered across calls. This is deliberately a plain, HTTP-free class so it can be
// unit tested without a live Ollama server or the HTTP module.
class CICOMPANION_API FOllamaStreamParser
{
public:
	struct FParsedLine
	{
		FString Content;
		bool bDone = false;
	};

	// Returns any newly-completed lines found after appending NewData to the buffer.
	// Malformed lines are skipped rather than treated as fatal, so one bad line
	// doesn't take down an otherwise-good stream.
	TArray<FParsedLine> Feed(const FString& NewData);

	void Reset();

private:
	FString Buffer;
};
