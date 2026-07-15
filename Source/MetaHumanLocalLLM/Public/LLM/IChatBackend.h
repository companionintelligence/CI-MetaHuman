#pragma once

#include "CoreMinimal.h"
#include "LLM/OllamaTypes.h"

DECLARE_DELEGATE_OneParam(FOnOllamaToken, const FString& /*TokenText*/);
DECLARE_DELEGATE_OneParam(FOnOllamaComplete, const FString& /*FullText*/);
DECLARE_DELEGATE_OneParam(FOnOllamaError, const FString& /*ErrorMessage*/);

typedef uint32 FOllamaRequestHandle;
constexpr FOllamaRequestHandle InvalidOllamaRequestHandle = 0;

// Plain (non-UObject) interface so tests can inject a fake backend without touching
// the network or the HTTP module. UOllamaClient implements this for real requests.
class IChatBackend
{
public:
	virtual ~IChatBackend() = default;

	// Sends the full message history as one chat turn. Returns a handle usable with
	// CancelRequest (e.g. on barge-in, when a newer user turn should abort this one).
	virtual FOllamaRequestHandle SendChat(
		const TArray<FOllamaMessage>& History,
		FOnOllamaToken OnToken,
		FOnOllamaComplete OnComplete,
		FOnOllamaError OnError) = 0;

	virtual void CancelRequest(FOllamaRequestHandle Handle) = 0;
};
