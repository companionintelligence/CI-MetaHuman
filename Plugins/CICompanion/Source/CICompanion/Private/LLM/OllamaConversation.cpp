#include "LLM/OllamaConversation.h"

#include "CICompanion.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

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
	TrimToCap();
}

void UOllamaConversation::AppendAssistantMessage(const FString& Text)
{
	History.Add(FOllamaMessage(EOllamaRole::Assistant, Text));
	TrimToCap();
}

void UOllamaConversation::Reset(const FString& InSystemPrompt)
{
	Initialize(InSystemPrompt);
}

bool UOllamaConversation::HasSystemPrompt() const
{
	return History.Num() > 0 && History[0].Role == EOllamaRole::System;
}

void UOllamaConversation::SetMaxRetainedTurns(int32 InMaxTurns)
{
	MaxRetainedTurns = InMaxTurns;
	// Applied immediately rather than only from the next append: lowering the cap is something an
	// operator does BECAUSE the history is already too long, and a setting that waits for two more
	// turns to take effect looks like it did not work.
	TrimToCap();
}

void UOllamaConversation::TrimToCap()
{
	if (MaxRetainedTurns <= 0)
	{
		return;	// unlimited, by explicit configuration
	}

	// Index 0 is the system prompt when there is one, and it is never a trim candidate. Losing it
	// is exactly the failure this cap exists to prevent, so it must not be the thing the cap
	// removes.
	const int32 First = HasSystemPrompt() ? 1 : 0;

	// Clamped before the multiply. MHLLM_MAX_TURNS reaches this straight from Atoi, and a value
	// above MAX_int32/2 wraps the product NEGATIVE -- which makes Excess enormous, clamps to the
	// whole history, and silently erases the entire conversation on every append. An absurdly large
	// cap has to mean "effectively unlimited", not "delete everything".
	const int32 MaxMessages = (MaxRetainedTurns > (MAX_int32 / 2)) ? MAX_int32 : MaxRetainedTurns * 2;

	int32 Excess = (History.Num() - First) - MaxMessages;
	if (Excess <= 0)
	{
		return;
	}

	// EXTEND the cut until the first surviving message is a User turn.
	//
	// This replaces an earlier "round the removal up to an even number", which was only correct
	// while the transcript alternated strictly user/assistant -- and two routine paths break that.
	// Barge-in cancels the in-flight request, so HandleComplete never fires and no assistant turn is
	// ever appended for it, leaving two consecutive user turns; an errored turn does the same. Once
	// the sequence is not alternating, removing an even count preserves whatever role happened to be
	// first, so the transcript can end up opening on an assistant turn -- which reads to the model
	// as though it had spoken unprompted, and it answers by continuing its own imagined turn instead
	// of addressing the user. Asking the roles directly is correct for any sequence.
	while (First + Excess < History.Num() && History[First + Excess].Role != EOllamaRole::User)
	{
		++Excess;
	}
	Excess = FMath::Min(Excess, History.Num() - First);

	History.RemoveAt(First, Excess);

	UE_LOG(LogMetaHumanLLM, Verbose,
		TEXT("Conversation trimmed: dropped %d message(s), %d retained (cap %d turns)."),
		Excess, History.Num(), MaxRetainedTurns);
}

void UOllamaConversation::DropLeadingNonUserTurns()
{
	const int32 First = HasSystemPrompt() ? 1 : 0;
	int32 Drop = 0;
	while (First + Drop < History.Num() && History[First + Drop].Role != EOllamaRole::User)
	{
		++Drop;
	}
	if (Drop > 0)
	{
		History.RemoveAt(First, Drop);
		UE_LOG(LogMetaHumanLLM, Verbose,
			TEXT("Dropped %d leading non-user message(s) so the transcript opens on a user turn."), Drop);
	}
}

FString UOllamaConversation::DefaultMemoryPath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Companion"), TEXT("Conversation.json"));
}

bool UOllamaConversation::SaveToJson(const FString& AbsPath) const
{
	TArray<TSharedPtr<FJsonValue>> Messages;
	Messages.Reserve(History.Num());
	for (const FOllamaMessage& Msg : History)
	{
		// Skipped, not merely unused on load: see the header. The persona is configuration and must
		// stay changeable after a conversation has already happened.
		if (Msg.Role == EOllamaRole::System)
		{
			continue;
		}
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("role"), FOllamaMessage::RoleToString(Msg.Role));
		Obj->SetStringField(TEXT("content"), Msg.Content);
		Messages.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	// Stamped so a later format change can migrate rather than crash on, or silently misread, a
	// file written by an older build.
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetArrayField(TEXT("messages"), Messages);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("Could not serialize conversation memory."));
		return false;
	}

	// Written to a temp file and MOVED into place, rather than truncating the live file.
	//
	// This is saved after every completed reply precisely because a kiosk is more likely to be
	// killed or lose power than to exit cleanly — which is exactly the moment an in-place truncating
	// write is destroyed mid-stream. LoadFromJson would then reject the half-written JSON and the
	// entire memory would be gone, so the cadence chosen to protect the data was the thing putting
	// it at risk. A rename is atomic enough that the previous good file survives any failure here.
	const FString TempPath = AbsPath + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(Output, *TempPath))
	{
		// Warning, not Error: a companion that cannot write her memory should still be able to talk.
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("Could not write conversation memory to '%s'."), *TempPath);
		return false;
	}

	if (!IFileManager::Get().Move(*AbsPath, *TempPath, /*bReplace*/ true))
	{
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("Could not move conversation memory into place at '%s'."), *AbsPath);
		IFileManager::Get().Delete(*TempPath, false, true, true);
		return false;
	}

	UE_LOG(LogMetaHumanLLM, Verbose, TEXT("Conversation memory saved (%d message(s)) to '%s'."),
		Messages.Num(), *AbsPath);
	return true;
}

bool UOllamaConversation::LoadFromJson(const FString& AbsPath)
{
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *AbsPath))
	{
		// The ordinary first-run case. Deliberately quiet at Log level -- an absent memory file is
		// not a problem, and warning about it every launch would train operators to ignore warnings.
		UE_LOG(LogMetaHumanLLM, Log, TEXT("No conversation memory at '%s'; starting fresh."), *AbsPath);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("Conversation memory at '%s' is not valid JSON; ignoring it."), *AbsPath);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
	if (!Root->TryGetArrayField(TEXT("messages"), Messages) || !Messages)
	{
		UE_LOG(LogMetaHumanLLM, Warning, TEXT("Conversation memory at '%s' has no messages array; ignoring it."), *AbsPath);
		return false;
	}

	// The currently-seeded system prompt is preserved and everything else replaced, so a load can
	// never duplicate the persona or adopt a stale one.
	const bool bKeepSystem = HasSystemPrompt();
	const FOllamaMessage SystemMessage = bKeepSystem ? History[0] : FOllamaMessage();
	History.Reset();
	if (bKeepSystem)
	{
		History.Add(SystemMessage);
	}

	for (const TSharedPtr<FJsonValue>& Value : *Messages)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Obj) || !Obj)
		{
			continue;
		}
		FString Role, Content;
		(*Obj)->TryGetStringField(TEXT("role"), Role);
		if (!(*Obj)->TryGetStringField(TEXT("content"), Content) || Content.IsEmpty())
		{
			continue;	// a turn with no words is not a turn
		}
		const EOllamaRole Parsed = FOllamaMessage::RoleFromString(Role);
		if (Parsed == EOllamaRole::System)
		{
			continue;	// belt and braces: a hand-edited file must not inject a second persona
		}
		History.Add(FOllamaMessage(Parsed, Content));
	}

	// A file can open mid-exchange: written by a build that trimmed on an even count, hand-edited,
	// or saved from a session whose first retained turn was an assistant reply. TrimToCap cannot fix
	// it, because it does nothing at all when the history is already under the cap.
	DropLeadingNonUserTurns();

	// Applied on load as well as on append, so shrinking the cap between runs actually shrinks the
	// history rather than being defeated by a large file written under the old one.
	TrimToCap();

	UE_LOG(LogMetaHumanLLM, Log, TEXT("Conversation memory loaded: %d message(s) recalled from '%s'."),
		bKeepSystem ? History.Num() - 1 : History.Num(), *AbsPath);
	return true;
}
