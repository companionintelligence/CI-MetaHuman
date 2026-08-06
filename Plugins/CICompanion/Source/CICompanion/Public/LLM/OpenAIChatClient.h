#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "LLM/IChatBackend.h"
#include "LLM/OpenAIStreamParser.h"
#include "OpenAIChatClient.generated.h"

// Talks to any OpenAI-compatible /v1/chat/completions endpoint with "stream": true,
// consuming the Server-Sent-Events response via FOpenAIStreamParser. This is the pluggable
// backend the planning doc asked for: point it at Ollama's built-in /v1 shim, the CI
// server, OpenAI, or any compatible gateway by changing only the settings. Streaming is
// read the same way as UOllamaClient — by diffing the in-flight response's accumulated
// content on each progress tick — since UE's HTTP module has no first-class SSE support.
UCLASS()
class CICOMPANION_API UOpenAIChatClient : public UGameInstanceSubsystem, public IChatBackend
{
	GENERATED_BODY()

public:
	// IChatBackend
	virtual FOllamaRequestHandle SendChat(
		const TArray<FOllamaMessage>& History,
		FOnOllamaToken OnToken,
		FOnOllamaComplete OnComplete,
		FOnOllamaError OnError) override;

	virtual void CancelRequest(FOllamaRequestHandle Handle) override;

	// USubsystem
	virtual void Deinitialize() override;

	// Pure helper, exposed for unit testing without a live request. Produces the
	// OpenAI chat-completions request JSON (model, stream, messages[]).
	static FString BuildRequestBody(const TArray<FOllamaMessage>& History, const FString& Model);

private:
	struct FActiveRequest
	{
		FHttpRequestPtr HttpRequest;
		FOpenAIStreamParser Parser;
		FString AccumulatedText;
		FOnOllamaToken OnToken;
		FOnOllamaComplete OnComplete;
		FOnOllamaError OnError;
		// BYTES consumed, not TCHARs -- see CIStreamUtf8.h and the matching note in OllamaClient.h.
		int32 LastProcessedBytes = 0;
	};

	void HandleRequestProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived, FOllamaRequestHandle Handle);
	void HandleRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FOllamaRequestHandle Handle);
	void ConsumeNewBytes(FActiveRequest& Active, const TArray<uint8>& FullContentSoFar);

	TMap<FOllamaRequestHandle, TSharedPtr<FActiveRequest>> ActiveRequests;
	FOllamaRequestHandle NextHandle = 1;
};
