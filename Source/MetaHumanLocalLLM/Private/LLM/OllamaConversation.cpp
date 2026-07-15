#include "LLM/OllamaConversation.h"

void UOllamaConversation::Initialize(const FString& InSystemPrompt)
{
	History.Reset();
	if (!InSystemPrompt.IsEmpty())
	{
		History.Add(FOllamaMessage(EOllamaRole::System, InSystemPrompt));
	}
}

void UOllamaConversation::AppendUserMessage(const FString& Text)
{
	History.Add(FOllamaMessage(EOllamaRole::User, Text));
}

void UOllamaConversation::AppendAssistantMessage(const FString& Text)
{
	History.Add(FOllamaMessage(EOllamaRole::Assistant, Text));
}

void UOllamaConversation::Reset(const FString& InSystemPrompt)
{
	Initialize(InSystemPrompt);
}

bool UOllamaConversation::HasSystemPrompt() const
{
	return History.Num() > 0 && History[0].Role == EOllamaRole::System;
}
