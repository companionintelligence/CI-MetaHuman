#pragma once

#include "CoreMinimal.h"
#include "OllamaTypes.generated.h"

UENUM(BlueprintType)
enum class EOllamaRole : uint8
{
	System,
	User,
	Assistant
};

USTRUCT(BlueprintType)
struct CICOMPANION_API FOllamaMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ollama")
	EOllamaRole Role = EOllamaRole::User;

	UPROPERTY(BlueprintReadOnly, Category = "Ollama")
	FString Content;

	FOllamaMessage() = default;
	FOllamaMessage(EOllamaRole InRole, const FString& InContent)
		: Role(InRole)
		, Content(InContent)
	{
	}

	static FString RoleToString(EOllamaRole InRole)
	{
		switch (InRole)
		{
		case EOllamaRole::System: return TEXT("system");
		case EOllamaRole::Assistant: return TEXT("assistant");
		case EOllamaRole::User:
		default: return TEXT("user");
		}
	}

	// The inverse, for reading a persisted transcript back off disk.
	//
	// Defaults to User on anything unrecognised rather than failing the load. A saved conversation
	// is a convenience, not a system of record: one unreadable role should cost the exact turn it
	// names, not the whole memory of a companion. Case-insensitive because the value survives a
	// round trip through a hand-editable JSON file.
	static EOllamaRole RoleFromString(const FString& InRole)
	{
		if (InRole.Equals(TEXT("system"), ESearchCase::IgnoreCase))    { return EOllamaRole::System; }
		if (InRole.Equals(TEXT("assistant"), ESearchCase::IgnoreCase)) { return EOllamaRole::Assistant; }
		return EOllamaRole::User;
	}
};
