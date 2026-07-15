#include "LLM/OpenAIChatClient.h"
#include "Settings/MetaHumanLLMSettings.h"
#include "HttpModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "MetaHumanLocalLLM.h"

FString UOpenAIChatClient::BuildRequestBody(const TArray<FOllamaMessage>& History, const FString& Model)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), Model);
	Root->SetBoolField(TEXT("stream"), true);

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
	const FString Body = BuildRequestBody(History, Settings->OpenAIModel);

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

void UOpenAIChatClient::ConsumeNewBytes(FActiveRequest& Active, const FString& FullContentSoFar)
{
	if (FullContentSoFar.Len() <= Active.LastProcessedLength)
	{
		return;
	}

	const FString NewChunk = FullContentSoFar.Mid(Active.LastProcessedLength);
	Active.LastProcessedLength = FullContentSoFar.Len();

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

	ConsumeNewBytes(**Found, Request->GetResponse()->GetContentAsString());
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
	ConsumeNewBytes(*Active, Response->GetContentAsString());

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
