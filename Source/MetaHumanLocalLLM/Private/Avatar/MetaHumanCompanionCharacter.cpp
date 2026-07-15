#include "Avatar/MetaHumanCompanionCharacter.h"
#include "LLM/OllamaConversation.h"
#include "LLM/OllamaClient.h"
#include "LLM/OpenAIChatClient.h"
#include "Voice/NullSpeechToText.h"
#include "Voice/NullTextToSpeech.h"
#include "Settings/MetaHumanLLMSettings.h"
#include "MetaHumanLocalLLM.h"

AMetaHumanCompanionCharacter::AMetaHumanCompanionCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	Conversation = CreateDefaultSubobject<UOllamaConversation>(TEXT("Conversation"));
	SpeechToText = CreateDefaultSubobject<UNullSpeechToText>(TEXT("SpeechToText"));
	TextToSpeech = CreateDefaultSubobject<UNullTextToSpeech>(TEXT("TextToSpeech"));
}

void AMetaHumanCompanionCharacter::BeginPlay()
{
	Super::BeginPlay();

	const UMetaHumanLLMSettings* Settings = GetDefault<UMetaHumanLLMSettings>();
	Conversation->Initialize(Settings->SystemPrompt);

	if (SpeechToText)
	{
		SpeechToText->OnTranscribed.AddDynamic(this, &AMetaHumanCompanionCharacter::HandleTranscribed);
	}
}

IChatBackend* AMetaHumanCompanionCharacter::GetBackend() const
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return nullptr;
	}

	// Pick the backend the user selected in Project Settings. Both are game-instance
	// subsystems that implement IChatBackend, so the rest of the orchestrator is agnostic.
	const UMetaHumanLLMSettings* Settings = GetDefault<UMetaHumanLLMSettings>();
	switch (Settings->BackendType)
	{
	case EChatBackendType::OpenAICompatible:
		return GameInstance->GetSubsystem<UOpenAIChatClient>();
	case EChatBackendType::OllamaNative:
	default:
		return GameInstance->GetSubsystem<UOllamaClient>();
	}
}

void AMetaHumanCompanionCharacter::HandleTranscribed(const FString& Text)
{
	SubmitUserText(Text);
}

void AMetaHumanCompanionCharacter::SubmitUserText(const FString& Text)
{
	if (Text.IsEmpty())
	{
		return;
	}

	IChatBackend* Backend = GetBackend();
	if (!Backend)
	{
		UE_LOG(LogMetaHumanLLM, Error, TEXT("SubmitUserText: no OllamaClient subsystem available"));
		return;
	}

	// Barge-in: a new user turn cancels whatever response was still in flight.
	if (CurrentRequestHandle != InvalidOllamaRequestHandle)
	{
		Backend->CancelRequest(CurrentRequestHandle);
		CurrentRequestHandle = InvalidOllamaRequestHandle;
	}

	Conversation->AppendUserMessage(Text);

	CurrentRequestHandle = Backend->SendChat(
		Conversation->GetHistory(),
		FOnOllamaToken::CreateUObject(this, &AMetaHumanCompanionCharacter::HandleToken),
		FOnOllamaComplete::CreateUObject(this, &AMetaHumanCompanionCharacter::HandleComplete),
		FOnOllamaError::CreateUObject(this, &AMetaHumanCompanionCharacter::HandleError));
}

void AMetaHumanCompanionCharacter::ResetConversation()
{
	IChatBackend* Backend = GetBackend();
	if (Backend && CurrentRequestHandle != InvalidOllamaRequestHandle)
	{
		Backend->CancelRequest(CurrentRequestHandle);
	}
	CurrentRequestHandle = InvalidOllamaRequestHandle;

	const UMetaHumanLLMSettings* Settings = GetDefault<UMetaHumanLLMSettings>();
	Conversation->Reset(Settings->SystemPrompt);
}

void AMetaHumanCompanionCharacter::HandleToken(const FString& Token)
{
	OnPartialResponse(Token);
}

void AMetaHumanCompanionCharacter::HandleComplete(const FString& FullText)
{
	CurrentRequestHandle = InvalidOllamaRequestHandle;
	Conversation->AppendAssistantMessage(FullText);

	const EEmotionType Emotion = UEmotionMapper::DetectEmotionFromText(FullText);
	OnSetEmotion(Emotion);
	OnSpeak(FullText);
	OnCompanionSpoke.Broadcast(FullText);

	if (TextToSpeech)
	{
		TextToSpeech->Speak(FullText);
	}
}

void AMetaHumanCompanionCharacter::HandleError(const FString& ErrorMessage)
{
	CurrentRequestHandle = InvalidOllamaRequestHandle;
	UE_LOG(LogMetaHumanLLM, Warning, TEXT("Ollama chat error: %s"), *ErrorMessage);
	OnConversationError(ErrorMessage);
}
