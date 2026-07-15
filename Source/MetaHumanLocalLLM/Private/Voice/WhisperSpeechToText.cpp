#include "Voice/WhisperSpeechToText.h"
#include "Settings/MetaHumanLLMSettings.h"
#include "HttpModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "MetaHumanLocalLLM.h"

namespace
{
	// Appends the UTF-8 bytes of Text to Out (multipart framing is ASCII, so UTF-8 is safe).
	void AppendUtf8(TArray<uint8>& Out, const FString& Text)
	{
		FTCHARToUTF8 Converted(*Text);
		Out.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
	}
}

FString UWhisperSpeechToText::MultipartContentType(const FString& Boundary)
{
	return FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary);
}

TArray<uint8> UWhisperSpeechToText::BuildMultipartFormData(const TArray<uint8>& WavBytes, const FString& Boundary, const FString& FileName)
{
	TArray<uint8> Body;

	// file part
	AppendUtf8(Body, FString::Printf(TEXT("--%s\r\n"), *Boundary));
	AppendUtf8(Body, FString::Printf(TEXT("Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"), *FileName));
	AppendUtf8(Body, TEXT("Content-Type: audio/wav\r\n\r\n"));
	Body.Append(WavBytes);
	AppendUtf8(Body, TEXT("\r\n"));

	// response_format part
	AppendUtf8(Body, FString::Printf(TEXT("--%s\r\n"), *Boundary));
	AppendUtf8(Body, TEXT("Content-Disposition: form-data; name=\"response_format\"\r\n\r\n"));
	AppendUtf8(Body, TEXT("json\r\n"));

	// closing boundary
	AppendUtf8(Body, FString::Printf(TEXT("--%s--\r\n"), *Boundary));

	return Body;
}

bool UWhisperSpeechToText::ParseTranscriptionResponse(const FString& ResponseBody, FString& OutText)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	FString Text;
	if (!JsonObject->TryGetStringField(TEXT("text"), Text))
	{
		return false;
	}

	Text.TrimStartAndEndInline();
	if (Text.IsEmpty())
	{
		return false;
	}

	OutText = Text;
	return true;
}

void UWhisperSpeechToText::StartListening()
{
	bIsListening = true;
	// Live mic capture is bound by a subclass/Blueprint capture source; on StopListening it
	// should hand the buffered WAV to TranscribeWav(). See class comment.
	UE_LOG(LogMetaHumanLLM, Verbose, TEXT("WhisperSpeechToText: StartListening (awaiting a bound capture source to buffer audio)"));
}

void UWhisperSpeechToText::StopListening()
{
	bIsListening = false;
	UE_LOG(LogMetaHumanLLM, Verbose, TEXT("WhisperSpeechToText: StopListening"));
}

void UWhisperSpeechToText::TranscribeWav(const TArray<uint8>& WavBytes)
{
	if (WavBytes.Num() == 0)
	{
		return;
	}

	const UMetaHumanLLMSettings* Settings = GetDefault<UMetaHumanLLMSettings>();
	FString BaseUrl = Settings->WhisperServerUrl;
	if (BaseUrl.IsEmpty())
	{
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("WhisperSpeechToText: WhisperServerUrl is empty; cannot transcribe"));
		return;
	}
	if (BaseUrl.EndsWith(TEXT("/")))
	{
		BaseUrl.LeftChopInline(1);
	}

	// A fixed boundary is fine — it just needs to be a token that does not appear in the
	// payload; a WAV header never contains this ASCII string.
	const FString Boundary = TEXT("----MetaHumanLLMWhisperBoundary7a1b");
	const TArray<uint8> Body = BuildMultipartFormData(WavBytes, Boundary, TEXT("audio.wav"));

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(BaseUrl + TEXT("/inference"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), MultipartContentType(Boundary));
	Request->SetContent(Body);
	Request->OnProcessRequestComplete().BindUObject(this, &UWhisperSpeechToText::HandleTranscriptionComplete);

	if (!Request->ProcessRequest())
	{
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("WhisperSpeechToText: failed to start transcription request"));
	}
}

void UWhisperSpeechToText::HandleTranscriptionComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!bWasSuccessful || !Response.IsValid())
	{
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("WhisperSpeechToText: transcription request failed"));
		return;
	}

	FString Text;
	if (ParseTranscriptionResponse(Response->GetContentAsString(), Text))
	{
		OnTranscribed.Broadcast(Text);
	}
	else
	{
		UE_LOG(LogMetaHumanLLM, Verbose, TEXT("WhisperSpeechToText: empty or unparseable transcription"));
	}
}
