#include "Voice/KokoroPhonemeAligner.h"
#include "CICompanionLog.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Settings/MetaHumanLLMSettings.h"

FString UKokoroPhonemeAligner::BuildEndpointUrl(const FString& BaseUrl)
{
	FString Base = BaseUrl;
	while (Base.EndsWith(TEXT("/")))
	{
		Base.LeftChopInline(1);
	}
	// Accept the URL either as the service root or already pointed at the endpoint, for the same
	// reason UKokoroTextToSpeech::BuildEndpointUrl does: operators paste whatever was printed at
	// them, and silently producing /speak/speak would 404 with no clue why.
	if (Base.EndsWith(TEXT("/speak")))
	{
		return Base;
	}
	return Base + TEXT("/speak");
}

FString UKokoroPhonemeAligner::BuildRequestBody(const FString& Text, const FString& Voice)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("text"), Text);
	// The VOICE has to match the one the audio service is using. Kokoro's style vector is
	// per-voice and per-length, so two voices produce different durations for the same sentence —
	// aligning against a voice nobody is listening to would hand the face confidently precise
	// timing for the wrong waveform.
	Root->SetStringField(TEXT("voice"), Voice);
	Root->SetNumberField(TEXT("speed"), 1.0);

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}

bool UKokoroPhonemeAligner::ParseAlignmentResponse(const FString& ResponseBody, TArray<FPhonemeSpan>& OutSpans, float& OutAudioSeconds)
{
	OutSpans.Reset();
	OutAudioSeconds = 0.0f;

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	// The sidecar reports an unpronounceable input as HTTP 200 with an {"error": ...} body rather
	// than a status code, so the failure is invisible unless this field is checked. Reported by
	// the caller rather than logged here, so this stays a pure function the tests can hammer.
	FString Error;
	if (Root->TryGetStringField(TEXT("error"), Error))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Spans = nullptr;
	if (!Root->TryGetArrayField(TEXT("spans"), Spans) || Spans == nullptr)
	{
		return false;
	}

	double AudioSeconds = 0.0;
	Root->TryGetNumberField(TEXT("audio_seconds"), AudioSeconds);
	OutAudioSeconds = static_cast<float>(AudioSeconds);

	OutSpans.Reserve(Spans->Num());
	for (const TSharedPtr<FJsonValue>& Value : *Spans)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr || !Object->IsValid())
		{
			continue;
		}

		double Start = 0.0;
		double Duration = 0.0;
		if (!(*Object)->TryGetNumberField(TEXT("start"), Start)
			|| !(*Object)->TryGetNumberField(TEXT("duration"), Duration))
		{
			continue;	// a span with no timing is not a span
		}

		FPhonemeSpan Span;
		// Symbol is legitimately ABSENT/empty for the pad tokens Kokoro puts at each end. Those
		// carry real frames — the leading pad measured 0.48s on our own opening line — and the
		// face wants them explicitly so it stays shut until the voice actually starts.
		(*Object)->TryGetStringField(TEXT("symbol"), Span.Symbol);
		Span.StartSeconds = static_cast<float>(Start);
		Span.DurationSeconds = static_cast<float>(Duration);
		OutSpans.Add(MoveTemp(Span));
	}

	for (const FPhonemeSpan& Span : OutSpans)
	{
		if (Span.DurationSeconds > 0.0f)
		{
			return true;
		}
	}

	OutSpans.Reset();
	OutAudioSeconds = 0.0f;
	return false;
}

void UKokoroPhonemeAligner::RequestAlignment(const FString& Text)
{
	const UMetaHumanLLMSettings* Settings = GetDefault<UMetaHumanLLMSettings>();
	if (Text.IsEmpty() || !Settings || Settings->KokoroAlignerUrl.IsEmpty())
	{
		OnAlignmentReady.Broadcast(Text, TArray<FPhonemeSpan>(), 0.0f);
		return;
	}

	CancelPending();
	PendingText = Text;

	ActiveRequest = FHttpModule::Get().CreateRequest();
	ActiveRequest->SetURL(BuildEndpointUrl(Settings->KokoroAlignerUrl));
	ActiveRequest->SetVerb(TEXT("POST"));
	ActiveRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ActiveRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	ActiveRequest->SetTimeout(RequestTimeoutSeconds);
	ActiveRequest->SetContentAsString(BuildRequestBody(Text, Settings->KokoroVoice));
	ActiveRequest->OnProcessRequestComplete().BindUObject(this, &UKokoroPhonemeAligner::HandleAlignmentComplete);

	if (!ActiveRequest->ProcessRequest())
	{
		ActiveRequest.Reset();
		PendingText.Empty();
		UE_LOG(LogCompanionVoice, Warning, TEXT("Aligner: could not start the request; falling back to estimated timing."));
		OnAlignmentReady.Broadcast(Text, TArray<FPhonemeSpan>(), 0.0f);
	}
}

void UKokoroPhonemeAligner::CancelPending()
{
	if (ActiveRequest.IsValid())
	{
		// Cancelled requests still call back. HandleAlignmentComplete drops any completion that is
		// not the request we are currently waiting on, so nothing is broadcast for this one.
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
	PendingText.Empty();
}

void UKokoroPhonemeAligner::HandleAlignmentComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!ActiveRequest.IsValid() || Request.Get() != ActiveRequest.Get())
	{
		return;	// cancelled or superseded; its utterance is no longer the one being spoken
	}
	ActiveRequest.Reset();

	const FString AlignedText = PendingText;
	PendingText.Empty();

	const int32 Code = Response.IsValid() ? Response->GetResponseCode() : 0;
	if (!bWasSuccessful || !Response.IsValid() || Code < 200 || Code >= 300)
	{
		// Not an error worth stopping speech for: the voice is unaffected, only its precision is.
		UE_LOG(LogCompanionVoice, Warning,
			TEXT("Aligner: request failed (HTTP %d); lip-sync falls back to estimated timing."), Code);
		OnAlignmentReady.Broadcast(AlignedText, TArray<FPhonemeSpan>(), 0.0f);
		return;
	}

	TArray<FPhonemeSpan> Spans;
	float AudioSeconds = 0.0f;
	if (!ParseAlignmentResponse(Response->GetContentAsString(), Spans, AudioSeconds))
	{
		UE_LOG(LogCompanionVoice, Warning,
			TEXT("Aligner: response carried no usable alignment (%d bytes); falling back to estimated timing."),
			Response->GetContent().Num());
		OnAlignmentReady.Broadcast(AlignedText, TArray<FPhonemeSpan>(), 0.0f);
		return;
	}

	UE_LOG(LogCompanionVoice, Log, TEXT("Aligner: %d phoneme spans covering %.2fs for %d chars."),
		Spans.Num(), AudioSeconds, AlignedText.Len());
	OnAlignmentReady.Broadcast(AlignedText, Spans, AudioSeconds);
}
