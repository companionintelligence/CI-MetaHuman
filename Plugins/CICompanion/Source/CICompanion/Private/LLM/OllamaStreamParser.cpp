#include "LLM/OllamaStreamParser.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

TArray<FOllamaStreamParser::FParsedLine> FOllamaStreamParser::Feed(const FString& NewData)
{
	TArray<FParsedLine> Results;
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

		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
		if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
		{
			continue;
		}

		FParsedLine Parsed;
		const TSharedPtr<FJsonObject>* MessageObject = nullptr;
		if (JsonObject->TryGetObjectField(TEXT("message"), MessageObject) && MessageObject != nullptr)
		{
			(*MessageObject)->TryGetStringField(TEXT("content"), Parsed.Content);
		}
		JsonObject->TryGetBoolField(TEXT("done"), Parsed.bDone);

		Results.Add(MoveTemp(Parsed));
	}

	return Results;
}

void FOllamaStreamParser::Reset()
{
	Buffer.Empty();
}
