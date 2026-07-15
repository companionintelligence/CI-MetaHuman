#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "LLM/IChatBackend.h"
#include "LLM/OllamaStreamParser.h"
#include "OllamaClient.generated.h"

// Talks directly to a local Ollama server's /api/chat endpoint (mirrors the
// CI-Spatial-Companion-Unity sibling's direct-REST pattern, but with full chat history
// per request and streaming NDJSON parsing, mirroring CI-Spatial-Companion-WebXR).
// Streaming is read by diffing the in-flight HTTP response's accumulated content on
// each progress tick and feeding new bytes to FOllamaStreamParser — Unreal's HTTP
// module has no first-class SSE support, so this is the standard workaround.
UCLASS()
class METAHUMANLOCALLLM_API UOllamaClient : public UGameInstanceSubsystem, public IChatBackend
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

	// Pure helper, exposed for unit testing without a live request.
	static FString BuildRequestBody(const TArray<FOllamaMessage>& History, const FString& Model);

private:
	struct FActiveRequest
	{
		FHttpRequestPtr HttpRequest;
		FOllamaStreamParser Parser;
		FString AccumulatedText;
		FOnOllamaToken OnToken;
		FOnOllamaComplete OnComplete;
		FOnOllamaError OnError;
		int32 LastProcessedLength = 0;
	};

	void HandleRequestProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived, FOllamaRequestHandle Handle);
	void HandleRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FOllamaRequestHandle Handle);
	void ConsumeNewBytes(FActiveRequest& Active, const FString& FullContentSoFar);

	TMap<FOllamaRequestHandle, TSharedPtr<FActiveRequest>> ActiveRequests;
	FOllamaRequestHandle NextHandle = 1;
};
