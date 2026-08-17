#include "LLM/OllamaClient.h"
#include "LLM/CIStreamUtf8.h"
#include "Settings/MetaHumanLLMSettings.h"
#include "HttpModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "CICompanion.h"

FString UOllamaClient::BuildRequestBody(const TArray<FOllamaMessage>& History, const FString& Model, bool bEnableThinking)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), Model);
	Root->SetBoolField(TEXT("stream"), true);
	// Suppress chain-of-thought unless explicitly wanted: it is the single largest latency term
	// against a reasoning model and none of it is ever spoken. See bEnableThinking.
	Root->SetBoolField(TEXT("think"), bEnableThinking);

	TArray<TSharedPtr<FJsonValue>> Messages;
	Messages.Reserve(History.Num());
	for (const FOllamaMessage& Msg : History)
	{
		TSharedRef<FJsonObject> MsgObj = MakeShared<FJsonObject>();
		MsgObj->SetStringField(TEXT("role"), FOllamaMessage::RoleToString(Msg.Role));
		MsgObj->SetStringField(TEXT("content"), Msg.Content);
		Messages.Add(MakeShared<FJsonValueObject>(MsgObj));
	}
	Root->SetArrayField(TEXT("messages"), Messages);

	// Condensed (not pretty-printed): this is an outgoing API payload, not a file for
	// humans to read, and Ollama does not care either way — no reason to pay for the
	// extra whitespace on every request.
	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

FOllamaRequestHandle UOllamaClient::SendChat(
	const TArray<FOllamaMessage>& History,
	FOnOllamaToken OnToken,
	FOnOllamaComplete OnComplete,
	FOnOllamaError OnError)
{
	const UMetaHumanLLMSettings* Settings = GetDefault<UMetaHumanLLMSettings>();
	FString BaseUrl = Settings->OllamaBaseUrl;
	if (BaseUrl.EndsWith(TEXT("/")))
	{
		BaseUrl.LeftChopInline(1);
	}
	const FString Url = BaseUrl + TEXT("/api/chat");
	const FString Body = BuildRequestBody(History, Settings->OllamaModel, Settings->bEnableThinking);

	const FOllamaRequestHandle Handle = NextHandle++;
	TSharedPtr<FActiveRequest> Active = MakeShared<FActiveRequest>();
	Active->OnToken = OnToken;
	Active->OnComplete = OnComplete;
	Active->OnError = OnError;

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	// Without this a backend that accepts the connection and then goes quiet never completes and
	// never errors, so the companion waits on it forever. Mirrors KokoroPhonemeAligner.
	if (Settings->ChatRequestTimeoutSeconds > 0.0f)
	{
		Request->SetTimeout(Settings->ChatRequestTimeoutSeconds);
	}
	Request->SetContentAsString(Body);
	Request->OnRequestProgress64().BindUObject(this, &UOllamaClient::HandleRequestProgress, Handle);
	Request->OnProcessRequestComplete().BindUObject(this, &UOllamaClient::HandleRequestComplete, Handle);

	Active->HttpRequest = Request;
	ActiveRequests.Add(Handle, Active);

	if (!Request->ProcessRequest())
	{
		ActiveRequests.Remove(Handle);
		OnError.ExecuteIfBound(TEXT("Failed to start Ollama HTTP request"));
		return InvalidOllamaRequestHandle;
	}

	return Handle;
}

void UOllamaClient::CancelRequest(FOllamaRequestHandle Handle)
{
	if (TSharedPtr<FActiveRequest>* Found = ActiveRequests.Find(Handle))
	{
		if ((*Found)->HttpRequest.IsValid())
		{
			(*Found)->HttpRequest->CancelRequest();
		}
		ActiveRequests.Remove(Handle);
	}
}

void UOllamaClient::ConsumeNewBytes(FActiveRequest& Active, const TArray<uint8>& FullContentSoFar)
{
	// Byte-diffed, and a trailing partial UTF-8 character is left for the next tick. See
	// CIStreamUtf8.h for why diffing the decoded FString instead silently corrupted every reply
	// containing a non-ASCII character.
	const FString NewChunk = CIStreamUtf8::ConsumeCompleteUtf8(FullContentSoFar, Active.LastProcessedBytes);
	if (NewChunk.IsEmpty())
	{
		return;
	}

	for (const FOllamaStreamParser::FParsedLine& Line : Active.Parser.Feed(NewChunk))
	{
		if (!Line.Content.IsEmpty())
		{
			Active.AccumulatedText += Line.Content;
			Active.OnToken.ExecuteIfBound(Line.Content);
		}
	}
}

void UOllamaClient::HandleRequestProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived, FOllamaRequestHandle Handle)
{
	TSharedPtr<FActiveRequest>* Found = ActiveRequests.Find(Handle);
	if (!Found || !Request.IsValid() || !Request->GetResponse().IsValid())
	{
		return;
	}

	// Skipped entirely when the tick carries nothing new. This is not just a micro-optimisation:
	// FHttpResponseCommon::GetContent() logs a LogHttp Warning ("Payload is incomplete. Response
	// still processing") on EVERY call while a response is in flight -- which is every call we make,
	// by definition, since we are reading a stream. GetContentAsString() has no such guard, so
	// moving to bytes traded a silent correctness bug for a noisy log. Gating on BytesReceived keeps
	// the read to ticks that actually advanced the payload.
	//
	// The residual noise is accepted deliberately: diffing the decoded string cannot be made correct
	// (see CIStreamUtf8.h), and a warning that is merely wrong about severity is a better failure
	// than silently corrupting every reply containing a non-ASCII character. An operator who wants
	// it gone should raise LogHttp's threshold rather than have this code go back to being wrong.
	if (BytesReceived <= static_cast<uint64>((*Found)->LastProcessedBytes))
	{
		return;
	}

	ConsumeNewBytes(**Found, Request->GetResponse()->GetContent());
}

void UOllamaClient::HandleRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FOllamaRequestHandle Handle)
{
	TSharedPtr<FActiveRequest> Active;
	if (TSharedPtr<FActiveRequest>* Found = ActiveRequests.Find(Handle))
	{
		Active = *Found;
		ActiveRequests.Remove(Handle);
	}

	if (!Active.IsValid())
	{
		// Already cancelled/removed before completion; nothing to report.
		return;
	}

	if (!bWasSuccessful || !Response.IsValid())
	{
		Active->OnError.ExecuteIfBound(TEXT("Ollama request failed (network error or no response)"));
		return;
	}

	if (Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
	{
		Active->OnError.ExecuteIfBound(FString::Printf(TEXT("Ollama returned HTTP %d"), Response->GetResponseCode()));
		return;
	}

	// Catch any trailing bytes the progress delegate hadn't seen yet (fast/small responses
	// can complete before a single progress tick fires).
	ConsumeNewBytes(*Active, Response->GetContent());

	Active->OnComplete.ExecuteIfBound(Active->AccumulatedText);
}

void UOllamaClient::Deinitialize()
{
	for (TPair<FOllamaRequestHandle, TSharedPtr<FActiveRequest>>& Pair : ActiveRequests)
	{
		if (Pair.Value->HttpRequest.IsValid())
		{
			Pair.Value->HttpRequest->CancelRequest();
		}
	}
	ActiveRequests.Empty();

	Super::Deinitialize();
}
