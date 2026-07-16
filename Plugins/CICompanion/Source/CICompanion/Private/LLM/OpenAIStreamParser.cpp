#include "LLM/OpenAIStreamParser.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

TArray<FOpenAIStreamParser::FParsedEvent> FOpenAIStreamParser::Feed(const FString& NewData)
{
	TArray<FParsedEvent> Results;
	Buffer += NewData;

	int32 NewlineIndex;
	while (Buffer.FindChar(TEXT('\n'), NewlineIndex))
	{
		FString Line = Buffer.Left(NewlineIndex);
		Buffer = Buffer.Mid(NewlineIndex + 1);

		Line.TrimStartAndEndInline();
		if (Line.IsEmpty())
		{
			continue;
		}

		// Only `data:` lines carry payloads; skip `event:`/`id:`/`:comment` lines.
		if (!Line.StartsWith(TEXT("data:")))
		{
			continue;
		}

		FString Payload = Line.RightChop(5); // strip "data:"
		Payload.TrimStartAndEndInline();

		if (Payload == TEXT("[DONE]"))
		{
			FParsedEvent Done;
			Done.bDone = true;
			Results.Add(MoveTemp(Done));
			continue;
		}

		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Payload);
		if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
		if (!JsonObject->TryGetArrayField(TEXT("choices"), Choices) || Choices == nullptr || Choices->Num() == 0)
		{
			continue;
		}

		const TSharedPtr<FJsonObject>& FirstChoice = (*Choices)[0]->AsObject();
		if (!FirstChoice.IsValid())
		{
			continue;
		}

		FParsedEvent Parsed;

		const TSharedPtr<FJsonObject>* DeltaObject = nullptr;
		if (FirstChoice->TryGetObjectField(TEXT("delta"), DeltaObject) && DeltaObject != nullptr)
		{
			(*DeltaObject)->TryGetStringField(TEXT("content"), Parsed.Content);
		}

		// A non-null finish_reason ("stop", "length", ...) marks the last content event.
		FString FinishReason;
		if (FirstChoice->TryGetStringField(TEXT("finish_reason"), FinishReason) && !FinishReason.IsEmpty())
		{
			Parsed.bDone = true;
		}

		Results.Add(MoveTemp(Parsed));
	}

	return Results;
}

void FOpenAIStreamParser::Reset()
{
	Buffer.Empty();
}
