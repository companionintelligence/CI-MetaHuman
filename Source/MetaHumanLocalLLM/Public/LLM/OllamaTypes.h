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
struct METAHUMANLOCALLLM_API FOllamaMessage
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
};
