#include "Avatar/MetaHumanCompanionCharacter.h"
#include "LLM/OllamaConversation.h"
#include "LLM/OllamaClient.h"
#include "LLM/OpenAIChatClient.h"
#include "Voice/NullSpeechToText.h"
#include "Voice/NullTextToSpeech.h"
#include "Voice/WhisperSpeechToText.h"
#include "Voice/PiperTextToSpeech.h"
#include "Voice/SpeechToTextComponent.h"
#include "Voice/TextToSpeechComponent.h"
#include "Settings/MetaHumanLLMSettings.h"
#include "CICompanion.h"

AMetaHumanCompanionCharacter::AMetaHumanCompanionCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Conversation = CreateDefaultSubobject<UOllamaConversation>(TEXT("Conversation"));
	SpeechToText = CreateDefaultSubobject<UNullSpeechToText>(TEXT("SpeechToText"));
	TextToSpeech = CreateDefaultSubobject<UNullTextToSpeech>(TEXT("TextToSpeech"));
}

void AMetaHumanCompanionCharacter::BeginPlay()
{
	Super::BeginPlay();

	const UMetaHumanLLMSettings* Settings = GetDefault<UMetaHumanLLMSettings>();
	Conversation->Initialize(Settings->SystemPrompt);

	// Dynamically instantiate real voice components if server URLs are configured
	if (!Settings->WhisperServerUrl.IsEmpty())
	{
		UWhisperSpeechToText* RealSTT = NewObject<UWhisperSpeechToText>(this, TEXT("RealSpeechToText"));
		if (RealSTT)
		{
			if (SpeechToText)
			{
				SpeechToText->DestroyComponent();
			}
			SpeechToText = RealSTT;
			SpeechToText->RegisterComponent();
		}
	}

	if (!Settings->PiperServerUrl.IsEmpty())
	{
		UPiperTextToSpeech* RealTTS = NewObject<UPiperTextToSpeech>(this, TEXT("RealTextToSpeech"));
		if (RealTTS)
		{
			if (TextToSpeech)
			{
				TextToSpeech->DestroyComponent();
			}
			TextToSpeech = RealTTS;
			TextToSpeech->RegisterComponent();
		}
	}

	if (SpeechToText)
	{
		SpeechToText->OnTranscribed.AddDynamic(this, &AMetaHumanCompanionCharacter::HandleTranscribed);
	}

	UPiperTextToSpeech* PiperTTS = Cast<UPiperTextToSpeech>(TextToSpeech);
	if (PiperTTS)
	{
		PiperTTS->OnVisemeEnvelopeReady.AddDynamic(this, &AMetaHumanCompanionCharacter::HandleVisemesReady);
	}

	if (TextToSpeech)
	{
		TextToSpeech->OnSpeechFinished.AddDynamic(this, &AMetaHumanCompanionCharacter::HandleSpeechFinished);
	}
}

void AMetaHumanCompanionCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bPlayingVisemes)
	{
		return;
	}

	if (ActiveVisemeKeys.IsEmpty() || !GetWorld())
	{
		StopVisemePlayback();
		return;
	}

	const float Elapsed = GetWorld()->GetTimeSeconds() - SpeechStartTime;
	float CurrentJawOpen = 0.0f;
	const FVisemeKey& FirstKey = ActiveVisemeKeys[0];
	const FVisemeKey& LastKey = ActiveVisemeKeys.Last();

	if (Elapsed <= FirstKey.TimeSeconds)
	{
		CurrentJawOpen = FirstKey.JawOpen;
	}
	else if (Elapsed >= LastKey.TimeSeconds)
	{
		CurrentJawOpen = LastKey.JawOpen;
		bPlayingVisemes = false;
		SetActorTickEnabled(false);
	}
	else
	{
		int32 KeyIndex = 0;
		for (int32 Index = 0; Index < ActiveVisemeKeys.Num() - 1; ++Index)
		{
			if (Elapsed >= ActiveVisemeKeys[Index].TimeSeconds && Elapsed <= ActiveVisemeKeys[Index + 1].TimeSeconds)
			{
				KeyIndex = Index;
				break;
			}
		}

		const FVisemeKey& KeyA = ActiveVisemeKeys[KeyIndex];
		const FVisemeKey& KeyB = ActiveVisemeKeys[KeyIndex + 1];
		const float TimeDelta = KeyB.TimeSeconds - KeyA.TimeSeconds;
		const float Alpha = TimeDelta > KINDA_SMALL_NUMBER
			? FMath::Clamp((Elapsed - KeyA.TimeSeconds) / TimeDelta, 0.0f, 1.0f)
			: 0.0f;
		CurrentJawOpen = FMath::Lerp(KeyA.JawOpen, KeyB.JawOpen, Alpha);
	}

	TMap<FName, float> Weights;
	Weights.Add(TEXT("jawOpen"), CurrentJawOpen);
	OnSetVisemeWeights(Weights);
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

bool AMetaHumanCompanionCharacter::StartListening()
{
	if (!SpeechToText)
	{
		return false;
	}

	SpeechToText->StartListening();
	return SpeechToText->IsListening();
}

void AMetaHumanCompanionCharacter::StopListening()
{
	if (SpeechToText)
	{
		SpeechToText->StopListening();
	}
}

bool AMetaHumanCompanionCharacter::IsListening() const
{
	return SpeechToText && SpeechToText->IsListening();
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

	if (TextToSpeech)
	{
		TextToSpeech->StopSpeaking();
	}
	StopVisemePlayback();

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

	if (TextToSpeech)
	{
		TextToSpeech->StopSpeaking();
	}
	StopVisemePlayback();

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

void AMetaHumanCompanionCharacter::HandleVisemesReady(const TArray<FVisemeKey>& Keys)
{
	ActiveVisemeKeys = Keys;
	bPlayingVisemes = ActiveVisemeKeys.Num() > 0 && GetWorld();
	if (bPlayingVisemes)
	{
		SpeechStartTime = GetWorld()->GetTimeSeconds();
		SetActorTickEnabled(true);
	}
	else
	{
		StopVisemePlayback();
	}
}

void AMetaHumanCompanionCharacter::HandleSpeechFinished(const FString& SpokenText)
{
	StopVisemePlayback();
}

void AMetaHumanCompanionCharacter::StopVisemePlayback()
{
	bPlayingVisemes = false;
	ActiveVisemeKeys.Empty();
	SetActorTickEnabled(false);

	TMap<FName, float> Weights;
	Weights.Add(TEXT("jawOpen"), 0.0f);
	OnSetVisemeWeights(Weights);
}
