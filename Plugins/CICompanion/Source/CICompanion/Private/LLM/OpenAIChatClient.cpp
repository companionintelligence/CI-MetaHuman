#include "LLM/OpenAIChatClient.h"
#include "LLM/CIStreamUtf8.h"
#include "Settings/MetaHumanLLMSettings.h"
#include "HttpModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "CICompanion.h"

FString UOpenAIChatClient::BuildRequestBody(
	const TArray<FOllamaMessage>& History,
	const FString& Model,
	bool bEnableThinking,
	bool bSendThinkingSuppression)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), Model);
	Root->SetBoolField(TEXT("stream"), true);

	// Chain-of-thought suppression. On this path it is not merely a latency saving the way "think":
	// false is for UOllamaClient -- it is the difference between an answer and no answer at all.
	//
	// Measured this session against the vLLM server on :8000 that CI-Hub on core-2 distributes as
	// hermes-agent's backend (cyankiwi/Qwen3.5-35B-A3B-AWQ-4bit), same prompt each time ("Say hello
	// in one short sentence."), stream:false:
	//
	//   max_tokens 40,  no extra field                                content NULL, reasoning field
	//                                                                 present, finish=length,  2.2s
	//   max_tokens 600, no extra field                                content NULL, reasoning 2118
	//                                                                 chars,   finish=length, 28.1s
	//   max_tokens 200, chat_template_kwargs {enable_thinking:false}  content "Hello!", reasoning 0
	//                                                                 chars,   finish=stop,  0.335s
	//
	// The reasoning spends the entire token budget before the model reaches its answer, so `content`
	// comes back null and this client accumulates nothing: 28 seconds of wall clock for a companion
	// that says NOTHING, versus a real reply in a third of a second.
	//
	// Emitted ONLY when thinking is disabled, because this is a suppression HINT and not a toggle.
	// Sending {"enable_thinking": true} was not measured and has no upside to weigh against the risk:
	// the backend measured above already reasoned with NO extra field present (rows one and two), so
	// true would be a no-op there, and against a server that does not recognise the key it is simply
	// an unrecognised top-level body field -- the one thing that can turn a working request into a
	// 400. So the enabled path sends nothing extra at all.
	//
	// bSendThinkingSuppression is the operator's way out if some strict gateway 400s on that field;
	// see UMetaHumanLLMSettings::bOpenAISendChatTemplateKwargs, including the explicit note that
	// api.openai.com's behaviour here was NOT tested.
	if (!bEnableThinking && bSendThinkingSuppression)
	{
		TSharedRef<FJsonObject> TemplateKwargs = MakeShared<FJsonObject>();
		TemplateKwargs->SetBoolField(TEXT("enable_thinking"), false);
		Root->SetObjectField(TEXT("chat_template_kwargs"), TemplateKwargs);
	}

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

	// Condensed: an outgoing API payload, not a file for humans to read.
	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

FOllamaRequestHandle UOpenAIChatClient::SendChat(
	const TArray<FOllamaMessage>& History,
	FOnOllamaToken OnToken,
	FOnOllamaComplete OnComplete,
	FOnOllamaError OnError)
{
	const UMetaHumanLLMSettings* Settings = GetDefault<UMetaHumanLLMSettings>();
	FString BaseUrl = Settings->OpenAIBaseUrl;
	if (BaseUrl.EndsWith(TEXT("/")))
	{
		BaseUrl.LeftChopInline(1);
	}
	const FString Url = BaseUrl + TEXT("/chat/completions");
	// bEnableThinking is read here as well as by UOllamaClient. It is deliberately NOT gated on
	// BackendType any more: FMetaHumanLLMEnvConfig has always parsed MHLLM_THINKING into it whatever
	// the backend, and until this call passed it through, a deployment on the OpenAI-compatible path
	// that set MHLLM_THINKING=0 got no suppression at all.
	const FString Body = BuildRequestBody(
		History,
		Settings->OpenAIModel,
		Settings->bEnableThinking,
		Settings->bOpenAISendChatTemplateKwargs);

	const FOllamaRequestHandle Handle = NextHandle++;
	TSharedPtr<FActiveRequest> Active = MakeShared<FActiveRequest>();
	Active->OnToken = OnToken;
	Active->OnComplete = OnComplete;
	Active->OnError = OnError;

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("text/event-stream"));
	if (!Settings->OpenAIApiKey.IsEmpty())
	{
		Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Settings->OpenAIApiKey));
	}
	// See the matching note in UOllamaClient::SendChat: an unbounded request is how "thinking..."
	// becomes permanent.
	if (Settings->ChatRequestTimeoutSeconds > 0.0f)
	{
		Request->SetTimeout(Settings->ChatRequestTimeoutSeconds);
	}
	Request->SetContentAsString(Body);
	Request->OnRequestProgress64().BindUObject(this, &UOpenAIChatClient::HandleRequestProgress, Handle);
	Request->OnProcessRequestComplete().BindUObject(this, &UOpenAIChatClient::HandleRequestComplete, Handle);

	Active->HttpRequest = Request;
	ActiveRequests.Add(Handle, Active);

	if (!Request->ProcessRequest())
	{
		ActiveRequests.Remove(Handle);
		OnError.ExecuteIfBound(TEXT("Failed to start OpenAI-compatible HTTP request"));
		return InvalidOllamaRequestHandle;
	}

	return Handle;
}

void UOpenAIChatClient::CancelRequest(FOllamaRequestHandle Handle)
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

void UOpenAIChatClient::ConsumeNewBytes(FActiveRequest& Active, const TArray<uint8>& FullContentSoFar)
{
	// Byte-diffed, holding back a trailing partial UTF-8 character -- see CIStreamUtf8.h. This
	// client is if anything MORE exposed than the Ollama one: SSE frames are base-delimited by
	// blank lines, so a corrupted character can also break the frame boundary and lose a whole
	// token rather than just mangling one glyph.
	const FString NewChunk = CIStreamUtf8::ConsumeCompleteUtf8(FullContentSoFar, Active.LastProcessedBytes);
	if (NewChunk.IsEmpty())
	{
		return;
	}

	for (const FOpenAIStreamParser::FParsedEvent& Event : Active.Parser.Feed(NewChunk))
	{
		if (!Event.Content.IsEmpty())
		{
			Active.AccumulatedText += Event.Content;
			Active.OnToken.ExecuteIfBound(Event.Content);
		}
	}
}

void UOpenAIChatClient::HandleRequestProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived, FOllamaRequestHandle Handle)
{
	TSharedPtr<FActiveRequest>* Found = ActiveRequests.Find(Handle);
	if (!Found || !Request.IsValid() || !Request->GetResponse().IsValid())
	{
		return;
	}

	// See the matching note in UOllamaClient::HandleRequestProgress: GetContent() warns on every
	// in-flight read, so ticks that advanced nothing are skipped rather than paid for.
	if (BytesReceived <= static_cast<uint64>((*Found)->LastProcessedBytes))
	{
		return;
	}

	ConsumeNewBytes(**Found, Request->GetResponse()->GetContent());
}

void UOpenAIChatClient::HandleRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FOllamaRequestHandle Handle)
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
		Active->OnError.ExecuteIfBound(TEXT("OpenAI-compatible request failed (network error or no response)"));
		return;
	}

	if (Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
	{
		Active->OnError.ExecuteIfBound(FString::Printf(TEXT("OpenAI-compatible server returned HTTP %d"), Response->GetResponseCode()));
		return;
	}

	// Catch any trailing bytes the progress delegate hadn't seen yet.
	ConsumeNewBytes(*Active, Response->GetContent());

	Active->OnComplete.ExecuteIfBound(Active->AccumulatedText);
}

void UOpenAIChatClient::Deinitialize()
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
