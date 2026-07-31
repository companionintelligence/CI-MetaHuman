#include "Avatar/CompanionRuntimeSubsystem.h"
#include "CICompanionEnv.h"
#include "CICompanionLog.h"

#include "Avatar/CompanionFaceComponent.h"
#include "Avatar/CompanionFaceSubsystem.h"
#include "Avatar/CompanionStageDirector.h"
#include "Avatar/MetaHumanCompanionCharacter.h"
#include "Voice/KokoroTextToSpeech.h"
#include "Sound/AmbientSound.h"
#include "Components/AudioComponent.h"
#include "Engine/TextRenderActor.h"
#include "EngineUtils.h"
#include "Settings/MetaHumanLLMSettings.h"
#include "Avatar/FaceProfile.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Voice/VisemePlanner.h"
#include "UI/SCompanionHUD.h"
#include "UI/SCompanionSettingsPanel.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
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
	// NOTE: deliberately NOT re-binding here. Tts above is the same component this lookup finds,
	// so binding twice drove the face twice per clip -- visible in the log as every "Re-timed to
	// audio" line appearing in duplicate. Harmless-looking, but it means two viseme tracks racing
	// on one mouth.
	if (Tts)
	{
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

	// The console rides in the SAME viewport layer as the chat box, for the same reason: it is
	// captured into the streamed frame, so one implementation serves the browser and a native
	// window, and clicks come back over the input channel that already exists.
	FCompanionSettingsModel Initial;
	Initial.TtsVoice = CICompanionEnv::Get(TEXT("KOKORO_VOICE"), Initial.TtsVoice);
	Initial.TtsModel = CICompanionEnv::Get(TEXT("KOKORO_MODEL"), Initial.TtsModel);
	Initial.LlmModel = CICompanionEnv::Get(TEXT("OLLAMA_MODEL"), Initial.LlmModel);
	Initial.FaceProfile = CICompanionEnv::Get(TEXT("MHLLM_FACE_PROFILE"), Initial.FaceProfile);
	Initial.bVoiceEnabled = !CICompanionEnv::Get(TEXT("KOKORO_SERVER_URL"), FString()).IsEmpty();
	Initial.Validate();

	SettingsPanel = SNew(SCompanionSettingsPanel)
		.InitialSettings(Initial)
		.OnSettingsChanged(FOnCompanionSettingsChanged::CreateUObject(
			this, &UCompanionRuntimeSubsystem::ApplySettings));

	GEngine->GameViewport->AddViewportWidgetContent(
		SNew(SWeakWidget).PossiblyNullContent(SettingsPanel.ToSharedRef()), 10001);

	// The console is hidden by default -- the opening is a brand moment, not a settings screen --
	// but it then needs a way IN. Two, because they fail differently: MHLLM_CONSOLE=1 starts it
	// open (no input path required, which is what makes it screenshottable and what a kiosk
	// operator wants), and "/console" typed in the chat box toggles it live. A key binding was the
	// obvious third option and is the one that does NOT work here: the streamed client's keystrokes
	// arrive through the HUD's focused text box, so a raw key handler would never see them.
	if (CICompanionEnv::IsEnabled(TEXT("MHLLM_CONSOLE")))
	{
		SettingsPanel->SetVisible(true);
		UE_LOG(LogCompanionRuntime, Log, TEXT("Console opened by MHLLM_CONSOLE=1."));
	}

	Hud->SetStatusText(TEXT("Type to Eliza and press Enter"));
	UE_LOG(LogCompanionRuntime, Log, TEXT("Chat box + console attached to viewport. Settings: %s"),
		*Initial.ToCompactString());

	// Rendering the box is not the same as being able to USE it. See ApplyChatInputMode.
	ApplyChatInputMode();
}

void UCompanionRuntimeSubsystem::ApplyChatInputMode()
{
	UWorld* World = GetWorld();
	if (!World || !Hud.IsValid())
	{
		return;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		if (ChatInputModeAttempts++ < MaxChatInputModeAttempts)
		{
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle,
				FTimerDelegate::CreateUObject(this, &UCompanionRuntimeSubsystem::ApplyChatInputMode), 0.25f, false);
		}
		else
		{
			UE_LOG(LogCompanionRuntime, Warning, TEXT("No player controller; chat box will not receive input."));
		}
		return;
	}

	// A cursor is not cosmetic here: without one there is nothing to aim at the text box with, in a
	// native window OR in a browser watching the stream (Pixel Streaming renders the SOFTWARE
	// cursor into the frame -- with bShowMouseCursor false the remote viewer's pointer is invisible
	// and clicking is guesswork).
	PC->bShowMouseCursor = true;
	PC->bEnableClickEvents = true;
	PC->bEnableMouseOverEvents = true;

	const TSharedPtr<SWidget> FocusTarget = Hud->GetInputWidget();

	if (UCompanionStageDirector::ShouldAllowFreeLook())
	{
		// Opt-in: the pawn keeps its input, and the UI shares it. Typing still works because the
		// text box holds keyboard focus, but a mouse drag on the scene will move the camera.
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(FocusTarget);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}
	else
	{
		// DEFAULT. UI-only is what makes the keyboard belong to Slate: in the default
		// FInputModeGameOnly the viewport captures input and the pawn consumes every key, which is
		// precisely why the box could be seen but never typed into. It also removes the second half
		// of the camera-drift problem for free -- input the pawn never receives cannot move it.
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(FocusTarget);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}

	// SetInputMode focuses the widget for the CURRENT slate user; this covers the rest (Pixel
	// Streaming's keyboard rides its own).
	Hud->FocusInputBox();

	// ...and again once the viewport has actually laid the HUD out. SetInputMode's focus is a
	// DEFERRED Slate operation -- it rides the local player's FReply and is only flushed when input
	// next arrives -- and FocusInputBox above can find no widget path at all on the frame the HUD is
	// added. Either way the box would sit unfocused until the user clicked it, which is
	// indistinguishable from the bug being fixed. Half a second is after the first frame and long
	// before anyone has typed.
	FTimerHandle FocusHandle;
	World->GetTimerManager().SetTimer(FocusHandle,
		FTimerDelegate::CreateUObject(this, &UCompanionRuntimeSubsystem::FocusChatInput), 0.5f, false);

	// ...and keep it there. See RearmChatFocus: one focus grab at load survives exactly until the
	// first click that misses the HUD, which over Pixel Streaming is the FIRST click, because the
	// stock player page locks the pointer and sends a drifted virtual cursor rather than the point
	// the viewer clicked.
	World->GetTimerManager().SetTimer(ChatFocusGuardHandle,
		FTimerDelegate::CreateUObject(this, &UCompanionRuntimeSubsystem::RearmChatFocus),
		0.25f, /*bLoop*/ true, /*InFirstDelay*/ 1.0f);

	UE_LOG(LogCompanionRuntime, Log, TEXT("Chat input armed: cursor shown, keyboard focus on the text box, focus guard running (free-look %s)."),
		UCompanionStageDirector::ShouldAllowFreeLook() ? TEXT("ON") : TEXT("locked"));
}

void UCompanionRuntimeSubsystem::RearmChatFocus()
{
	if (!Hud.IsValid())
	{
		return;
	}
	if (Hud->IsInputFocused())
	{
		return;
	}
	Hud->RestoreInputFocusIfLost();
	if (!bLoggedChatFocusRecovery && Hud->IsInputFocused())
	{
		bLoggedChatFocusRecovery = true;
		UE_LOG(LogCompanionRuntime, Log, TEXT("Chat focus guard recovered the caret after something else took it."));
	}
}

void UCompanionRuntimeSubsystem::FocusChatInput()
{
	if (Hud.IsValid())
	{
		Hud->FocusInputBox();
	}
}

void UCompanionRuntimeSubsystem::SetHudVisible(bool bVisible)
{
	if (!Hud.IsValid())
	{
		return;
	}
	// SelfHitTestInvisible on the ROOT only: it is a full-screen overlay, so a hit-testable root
	// would eat every click in the game. The controls inside it are explicitly Visible.
	Hud->SetVisibility(bVisible ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
	if (bVisible)
	{
		// Hiding the HUD drops keyboard focus; showing it again has to take it back or the box
		// looks live and is not.
		Hud->FocusInputBox();
	}
}

void UCompanionRuntimeSubsystem::HandleHudTextSubmitted(const FString& Text)
{
	if (Text.IsEmpty())
	{
		return;
	}
	// Logged at the boundary: this line is the proof that a real keystroke crossed from Slate into
	// the conversation stack, which is the exact link that was broken.
	UE_LOG(LogCompanionRuntime, Log, TEXT("Chat box submitted (%d chars): %s"), Text.Len(), *Text.Left(160));
	// Chat commands are intercepted BEFORE the model sees them. Routing "/console" to the LLM
	// would make her answer a question about consoles instead of opening one.
	const FString Trimmed = Text.TrimStartAndEnd();
	if (Trimmed.Equals(TEXT("/console"), ESearchCase::IgnoreCase)
		|| Trimmed.Equals(TEXT("/settings"), ESearchCase::IgnoreCase))
	{
		if (SettingsPanel.IsValid())
		{
			const bool bNowVisible = !SettingsPanel->IsPanelVisible();
			SettingsPanel->SetVisible(bNowVisible);
			if (Hud.IsValid())
			{
				Hud->SetStatusText(bNowVisible ? TEXT("console open — /console to close")
				                               : TEXT("Type to Eliza and press Enter"));
			}
			UE_LOG(LogCompanionRuntime, Log, TEXT("Console toggled -> %s"),
				bNowVisible ? TEXT("open") : TEXT("closed"));
		}
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
		UE_LOG(LogCompanionRuntime, Warning, TEXT("SubmitUserText dropped (%s)."),
			Companion ? TEXT("empty text") : TEXT("no companion spawned"));
		return false;
	}
	UE_LOG(LogCompanionRuntime, Log, TEXT("SubmitUserText -> companion: %s"), *Text.Left(160));
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

void UCompanionRuntimeSubsystem::ApplySettings(const FCompanionSettingsModel& NewSettings)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// VOICE. Cheapest of the lot: Kokoro takes the voice per request, so a change costs nothing
	// and lands on the next utterance rather than needing a reconnect.
	if (UMetaHumanLLMSettings* Cfg = GetMutableDefault<UMetaHumanLLMSettings>())
	{
		Cfg->KokoroVoice = NewSettings.TtsVoice;
		Cfg->KokoroModel = NewSettings.TtsModel;
		Cfg->OllamaModel = NewSettings.LlmModel;
	}

	// FACE. Swapping the profile re-resolves every curve against the mesh, and the resolved count
	// is logged because an unknown rig silently no-ops -- SetMorphTarget on a missing name does
	// nothing at all, so a wrong profile looks exactly like a frozen face.
	if (UCompanionFaceComponent* Face = ResolveFace())
	{
		Face->SetProfile(FFaceProfile::ByName(NewSettings.FaceProfile));
		UE_LOG(LogCompanionRuntime, Log, TEXT("Face profile '%s': %d/%d curves resolved."),
			*NewSettings.FaceProfile, Face->GetResolvedCurveCount(), Face->GetProfileCurveCount());

		if (NewSettings.bFaceSelfTest && !Face->IsSelfTesting())      { Face->StartSelfTest(); }
		else if (!NewSettings.bFaceSelfTest && Face->IsSelfTesting()) { Face->StopSelfTest(); }
	}

	// MUSIC. The ambience is an authored AmbientSound actor, found by label rather than held as a
	// pointer, because the map is authored headlessly and the subsystem never spawned it.
	for (TActorIterator<AAmbientSound> It(World); It; ++It)
	{
		if (UAudioComponent* AC = It->GetAudioComponent())
		{
			AC->SetVolumeMultiplier(NewSettings.bMusicEnabled ? NewSettings.MusicVolume : 0.0f);
		}
	}

	// BRANDING. Same story: an authored TextRenderActor, toggled rather than destroyed so it can
	// come back without re-authoring the level.
	for (TActorIterator<ATextRenderActor> It(World); It; ++It)
	{
		It->SetActorHiddenInGame(!NewSettings.bBrandingVisible);
	}

	// CHAT.
	SetHudVisible(NewSettings.bChatVisible);
}
