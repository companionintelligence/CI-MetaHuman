#include "Avatar/CompanionRuntimeSubsystem.h"
#include "CICompanionEnv.h"
#include "CICompanionLog.h"

#include "Avatar/CompanionFaceComponent.h"
#include "Avatar/CompanionFaceSubsystem.h"
#include "Avatar/CompanionStageDirector.h"
#include "Avatar/MetaHumanCompanionCharacter.h"
#include "Voice/KokoroTextToSpeech.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Voice/KokoroTextToSpeech.h"
#include "Voice/VisemePlanner.h"
#include "UI/SCompanionHUD.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SWeakWidget.h"



bool UCompanionRuntimeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UCompanionRuntimeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (CICompanionEnv::IsDisabled(TEXT("MHLLM_COMPANION")))
	{
		UE_LOG(LogCompanionRuntime, Log, TEXT("Companion disabled by MHLLM_COMPANION=0."));
		return;
	}

	// Spawned far below the floor and never rendered: this actor is the conversational brain, not
	// the body. The body is the authored SkeletalMeshActor the face driver binds to. Keeping them
	// separate means the map stays a plain visual asset with no dependency on plugin C++.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Name = TEXT("CompanionBrain");
	Companion = InWorld.SpawnActor<AMetaHumanCompanionCharacter>(
		AMetaHumanCompanionCharacter::StaticClass(), FVector(0.0f, 0.0f, -10000.0f), FRotator::ZeroRotator, Params);

	if (!Companion)
	{
		UE_LOG(LogCompanionRuntime, Error, TEXT("Failed to spawn companion; conversation unavailable."));
		return;
	}
	Companion->SetActorHiddenInGame(true);
	Companion->OnCompanionSpoke.AddDynamic(this, &UCompanionRuntimeSubsystem::HandleCompanionSpoke);

	// Voice is opt-in on configuration alone: with no Kokoro URL the mouth still moves from planned
	// text, so a missing speech service degrades to a silent talker rather than a dead face.
	if (!CICompanionEnv::Get(TEXT("KOKORO_SERVER_URL"), FString()).IsEmpty())
	{
		Tts = NewObject<UKokoroTextToSpeech>(Companion, TEXT("CompanionVoice"));
		Tts->RegisterComponent();
		Tts->OnSpeechAudioReady.AddDynamic(this, &UCompanionRuntimeSubsystem::HandleSpeechAudioReady);
		UE_LOG(LogCompanionRuntime, Log, TEXT("Voice enabled (Kokoro)."));
	}
	else
	{
		UE_LOG(LogCompanionRuntime, Log, TEXT("No KOKORO_SERVER_URL; speaking silently from planned text."));
	}

	// Prefer MEASURED audio length over an estimated speaking rate when TTS is live. PlanFromText
	// guesses at 165wpm; the E2E harness measured real replies between 131 and 184wpm, so on a
	// long sentence the estimate drifts far enough to see. Binding here keeps the mouth locked to
	// the clip that is actually playing.
	if (UKokoroTextToSpeech* Kokoro = Companion->FindComponentByClass<UKokoroTextToSpeech>())
	{
		Kokoro->OnSpeechAudioReady.AddDynamic(this, &UCompanionRuntimeSubsystem::HandleSpeechAudioReady);
		UE_LOG(LogCompanionRuntime, Log, TEXT("Viseme timing bound to measured Kokoro audio duration."));
	}

	CreateHud();

	UE_LOG(LogCompanionRuntime, Log, TEXT("Companion spawned and bound (backend=%s model=%s)."),
		*CICompanionEnv::Get(TEXT("MHLLM_BACKEND"), TEXT("<unset>")), *CICompanionEnv::Get(TEXT("OLLAMA_MODEL"), TEXT("<unset>")));

	// An opening line, because there is still no input path: without it a correct build is
	// indistinguishable from a broken one. Deferred rather than sent inline, since the LLM call is
	// slow enough to stall level load and the streamer has not attached yet at BeginPlay.
	// Default is now "none": the opening line is scripted and delivered by
	// UCompanionStageDirector at the end of the camera move. Two greetings would collide -- the
	// LLM one would fire on a 5s timer while the camera was still flying.
	const FString Greeting = CICompanionEnv::Get(TEXT("MHLLM_GREETING"), TEXT("none"));
	if (!Greeting.IsEmpty() && !Greeting.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		FTimerHandle Handle;
		InWorld.GetTimerManager().SetTimer(Handle, [this, Greeting]()
		{
			UE_LOG(LogCompanionRuntime, Log, TEXT("Sending opening turn."));
			SubmitUserText(Greeting);
		}, 5.0f, false);
	}
}

void UCompanionRuntimeSubsystem::CreateHud()
{
	if (CICompanionEnv::IsDisabled(TEXT("MHLLM_HUD")))
	{
		UE_LOG(LogCompanionRuntime, Log, TEXT("HUD disabled by MHLLM_HUD=0."));
		return;
	}
	if (!GEngine || !GEngine->GameViewport)
	{
		UE_LOG(LogCompanionRuntime, Warning, TEXT("No game viewport; chat box unavailable."));
		return;
	}

	Hud = SNew(SCompanionHUD)
		.OnTextSubmitted(FOnCompanionTextSubmitted::CreateUObject(this, &UCompanionRuntimeSubsystem::HandleHudTextSubmitted));

	// Added as viewport content so it renders into the SAME back buffer Pixel Streaming captures.
	// That is what makes one implementation serve both targets: a native window draws it directly,
	// and the streamed client sees it because it is part of the frame, with keystrokes travelling
	// back over the existing input channel. A browser-only DOM overlay would have worked for the
	// stream and left the native build with no way to type at all.
	GEngine->GameViewport->AddViewportWidgetContent(
		SNew(SWeakWidget).PossiblyNullContent(Hud.ToSharedRef()), 10000);

	Hud->SetStatusText(TEXT("Type to Eliza and press Enter"));
	UE_LOG(LogCompanionRuntime, Log, TEXT("Chat box attached to viewport."));
}

void UCompanionRuntimeSubsystem::SetHudVisible(bool bVisible)
{
	if (Hud.IsValid())
	{
		Hud->SetVisibility(bVisible ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
	}
}

void UCompanionRuntimeSubsystem::HandleHudTextSubmitted(const FString& Text)
{
	if (Text.IsEmpty())
	{
		return;
	}
	if (Hud.IsValid())
	{
		Hud->AppendChatEntry(TEXT("You"), Text);
		Hud->SetStatusText(TEXT("thinking..."));
	}
	// A typed turn must cut the scripted opening short: making someone wait out a camera move
	// after they have spoken is the rudest thing an assistant can do.
	if (UWorld* World = GetWorld())
	{
		if (UCompanionStageDirector* Stage = World->GetSubsystem<UCompanionStageDirector>())
		{
			Stage->SkipToLive();
		}
	}
	SubmitUserText(Text);
}

bool UCompanionRuntimeSubsystem::SubmitUserText(const FString& Text)
{
	if (!Companion || Text.IsEmpty())
	{
		return false;
	}
	Companion->SubmitUserText(Text);
	return true;
}

UCompanionFaceComponent* UCompanionRuntimeSubsystem::ResolveFace() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	const UCompanionFaceSubsystem* FaceSubsystem = World->GetSubsystem<UCompanionFaceSubsystem>();
	return FaceSubsystem ? FaceSubsystem->GetFace() : nullptr;
}

bool UCompanionRuntimeSubsystem::SpeakLiteral(const FString& Text)
{
	if (Text.IsEmpty())
	{
		return false;
	}
	// Route through the CHARACTER, not straight at the face. Calling HandleCompanionSpoke here
	// looked equivalent -- it drives the same visemes -- but it skipped TextToSpeech->Speak
	// entirely, so the scripted opening mouthed the brand line while the Pixel Streaming audio
	// track carried digital silence. "Same handler" was true of the face and false of the voice.
	if (Companion)
	{
		Companion->SpeakWithoutLLM(Text);
		return true;
	}
	HandleCompanionSpoke(Text);   // face-only fallback if the brain failed to spawn
	return true;
}

void UCompanionRuntimeSubsystem::HandleSpeechAudioReady(const FString& Text, float DurationSeconds)
{
	UCompanionFaceComponent* Face = ResolveFace();
	if (!Face || DurationSeconds <= 0.0f)
	{
		return;
	}
	// The mouth has been waiting for this: with a voice service configured, HandleCompanionSpoke
	// deliberately queues NOTHING, because planning from text and then re-timing plays the same
	// utterance twice at two different speeds. FVisemePlanner assumes ~165wpm while the E2E run
	// measured Kokoro between 131 and 184wpm, so over a long reply the estimate drifts far enough
	// for the lips to stop while the voice is still going.
	const TArray<FVisemeSpan> Track = FVisemePlanner::PlanForDuration(Text, DurationSeconds);
	Face->PlayVisemeTrack(Track);
	UE_LOG(LogCompanionRuntime, Log, TEXT("Re-timed to audio: %d spans over %.2fs."),
		Track.Num(), DurationSeconds);
}

void UCompanionRuntimeSubsystem::HandleCompanionSpoke(const FString& Text)
{
	UE_LOG(LogCompanionRuntime, Log, TEXT("Companion replied (%d chars): %s"), Text.Len(), *Text.Left(160));

	if (Hud.IsValid())
	{
		Hud->AppendChatEntry(TEXT("Eliza"), Text);
		Hud->SetStatusText(TEXT("Type to Eliza and press Enter"));
	}

	UCompanionFaceComponent* Face = ResolveFace();
	if (!Face)
	{
		UE_LOG(LogCompanionRuntime, Warning, TEXT("Reply received but no face bound; mouth will not move."));
		return;
	}

	if (Tts)
	{
		// Defer the mouth until the audio exists. Planning now and again on arrival would play the
		// utterance twice at two different speeds.
		Tts->Speak(Text);
		return;
	}

	// No voice service: fall back to timing estimated from a speaking rate. Visibly fine on its
	// own, and only wrong once there is real audio to drift against.
	const TArray<FVisemeSpan> Track = FVisemePlanner::PlanFromText(Text);
	Face->PlayVisemeTrack(Track);

	UE_LOG(LogCompanionRuntime, Log, TEXT("Speaking (planned): %d viseme spans over %.2fs."),
		Track.Num(), FVisemePlanner::TrackDuration(Track));
}
