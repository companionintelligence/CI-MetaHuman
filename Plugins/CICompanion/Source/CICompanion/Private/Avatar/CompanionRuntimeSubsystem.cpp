#include "Avatar/CompanionRuntimeSubsystem.h"
#include "CICompanionEnv.h"
#include "CICompanionLog.h"

#include "Avatar/CompanionFaceComponent.h"
#include "Avatar/CompanionFaceSubsystem.h"
#include "Avatar/CompanionStageDirector.h"
#include "Avatar/EmotionMapper.h"
#include "Avatar/MetaHumanCompanionCharacter.h"
#include "Voice/KokoroPhonemeAligner.h"
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
#include "UObject/Class.h"



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
	//
	// ADOPTED from the character, never constructed here. This subsystem used to build its own
	// UKokoroTextToSpeech on the companion actor, and the note that used to sit below claimed it was
	// "the same component" the character had. It was not -- there were two, "CompanionVoice" here
	// and "RealTextToSpeech" on the character -- and both were driven from a single reply, because
	// AMetaHumanCompanionCharacter broadcasts OnCompanionSpoke (reaching HandleCompanionSpoke, which
	// spoke) and then calls Speak on its own component. Two syntheses, two SpawnSound2D calls, and
	// the listener heard every line overlapping itself ~700ms later once the server had served the
	// second request. It read in the log as a duplicate "Kokoro: N.NNs of speech" per reply.
	//
	// The character owns Speak. This subsystem only listens, so there is exactly one voice to hear
	// and exactly one clip to time the mouth against.
	//
	// Resolved on the NEXT TICK, not on this line. The component to adopt does not exist yet: the
	// character starts with its default UNullTextToSpeech and only swaps in the real Kokoro one in
	// its own BeginPlay, which UE runs AFTER this function returns even though SpawnActor is called
	// inside it. Reading it here got exactly what that ordering implies, and shipped it:
	//   19.57.40:903  Error: KOKORO_SERVER_URL is set but the companion's TTS is NullTextToSpeech
	//   19.57.40:905  TTS backend: KokoroTextToSpeech
	// Two milliseconds and one frame apart. Nothing was broken except the order of two lines, and
	// the visible symptom was a mouth silently timed to spelling while she spoke perfectly.
	InWorld.GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(this, &UCompanionRuntimeSubsystem::ResolveVoice));

	// REAL per-phoneme timing, if the kokoro-align sidecar is configured. The measured length above
	// only fixes the TOTAL: it rescales a spelling-based estimate, so "though" and "throw" still get
	// identical time despite sounding nothing alike. The aligner returns the durations Kokoro's own
	// duration predictor produced for the waveform, which is the difference between a mouth timed to
	// what was said and one timed to how it was spelled.
	//
	// Read from settings rather than the environment directly so the editor and the container agree:
	// FMetaHumanLLMEnvConfig has already folded KOKORO_ALIGNER_URL into settings by this point, and
	// the client itself reads settings — gating on a different source could enable a client that
	// then finds nothing to call.
	//
	// Gated on the URL ALONE, and deliberately not also on Tts: the voice resolves a frame later
	// than this, so an `&& Tts` here would have reported "no KOKORO_ALIGNER_URL" for a variable
	// that was plainly set — a log line that sends whoever reads it to the wrong file. Whether the
	// aligner is ever ASKED anything is decided at speaking time, where Tts is known.
	const UMetaHumanLLMSettings* VoiceCfg = GetDefault<UMetaHumanLLMSettings>();
	if (VoiceCfg && !VoiceCfg->KokoroAlignerUrl.IsEmpty())
	{
		Aligner = NewObject<UKokoroPhonemeAligner>(this, TEXT("CompanionAligner"));
		Aligner->OnAlignmentReady.AddUObject(this, &UCompanionRuntimeSubsystem::HandlePhonemeAlignmentReady);
		UE_LOG(LogCompanionRuntime, Log,
			TEXT("Phoneme alignment ENABLED (kokoro-align at %s): lip-sync will follow measured phonemes."),
			*VoiceCfg->KokoroAlignerUrl);
	}
	else
	{
		UE_LOG(LogCompanionRuntime, Log,
			TEXT("Phoneme alignment DISABLED (no KOKORO_ALIGNER_URL): lip-sync will use a spelling ESTIMATE."));
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
	// More so now: the character is the ONLY caller of Speak, so HandleCompanionSpoke is a
	// face-timing path exclusively and cannot make a sound on its own.
	if (Companion)
	{
		Companion->SpeakWithoutLLM(Text);
		return true;
	}
	HandleCompanionSpoke(Text);   // face-only fallback if the brain failed to spawn
	return true;
}

void UCompanionRuntimeSubsystem::ResolveVoice()
{
	if (Tts || !Companion)
	{
		return;	// idempotent: called from a next-tick timer AND from every reply, whichever lands first
	}

	Tts = Cast<UKokoroTextToSpeech>(Companion->GetTextToSpeech());
	if (Tts)
	{
		// Prefer MEASURED audio length over an estimated speaking rate when TTS is live. PlanFromText
		// guesses at 165wpm; the E2E harness measured real replies between 131 and 184wpm, so on a
		// long sentence the estimate drifts far enough to see. Binding here keeps the mouth locked to
		// the clip that is actually playing.
		Tts->OnSpeechAudioReady.AddDynamic(this, &UCompanionRuntimeSubsystem::HandleSpeechAudioReady);
		UE_LOG(LogCompanionRuntime, Log,
			TEXT("Voice enabled (Kokoro, shared with the companion); viseme timing bound to measured audio duration."));
		bVoiceResolutionReported = true;
		return;
	}

	if (bVoiceResolutionReported)
	{
		return;	// said once; this runs per reply and a repeated line would bury a kiosk log
	}
	bVoiceResolutionReported = true;

	if (!CICompanionEnv::Get(TEXT("KOKORO_SERVER_URL"), FString()).IsEmpty())
	{
		// Configured for Kokoro and the character did not end up holding one. She will still be heard
		// through whatever backend it did select, but nothing here can measure the clip, so the mouth
		// silently drops to the spelling estimate. Logged loudly because that failure is otherwise
		// invisible: a face timed to an estimate looks like a face, just slightly wrong.
		const UTextToSpeechComponent* Actual = Companion->GetTextToSpeech();
		UE_LOG(LogCompanionRuntime, Error,
			TEXT("KOKORO_SERVER_URL is set but the companion's TTS is %s; lip-sync falls back to estimated timing."),
			Actual ? *Actual->GetClass()->GetName() : TEXT("null"));
	}
	else
	{
		UE_LOG(LogCompanionRuntime, Log, TEXT("No KOKORO_SERVER_URL; speaking silently from planned text."));
	}
}

void UCompanionRuntimeSubsystem::BeginUtterance(const FString& Text)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AlignmentGraceHandle);
	}
	PendingUtteranceText = Text;
	PendingAlignment.Reset();
	PendingAlignedSeconds = 0.0f;
	PendingAudioSeconds = 0.0f;
	AudioStartRealTime = 0.0;
	// With no aligner there is nothing to wait for, so the question is already settled and the
	// mouth starts the instant audio does.
	bAlignmentResolved = (Aligner == nullptr);
	bAudioReady = false;
	bMouthDriven = false;
}

void UCompanionRuntimeSubsystem::HandleSpeechAudioReady(const FString& Text, float DurationSeconds)
{
	if (DurationSeconds <= 0.0f)
	{
		return;
	}
	// Trust the delegate's text over the stored one: the opening line is spoken through a path that
	// does not go via HandleCompanionSpoke on every build, and planning against the wrong string
	// would be worse than planning against no alignment at all.
	if (!Text.Equals(PendingUtteranceText))
	{
		BeginUtterance(Text);
	}
	PendingAudioSeconds = DurationSeconds;
	AudioStartRealTime = FPlatformTime::Seconds();
	bAudioReady = true;

	if (bAlignmentResolved)
	{
		DriveMouthFromBestSource();
		return;
	}

	// The aligner is still working. Wait a beat rather than committing to the estimate — see
	// AlignmentGraceSeconds for the measurements. The wait is bounded from both ends: this timer,
	// and the client's own request timeout.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(AlignmentGraceHandle,
			FTimerDelegate::CreateUObject(this, &UCompanionRuntimeSubsystem::DriveMouthFromBestSource),
			AlignmentGraceSeconds, false);
	}
	else
	{
		DriveMouthFromBestSource();
	}
}

void UCompanionRuntimeSubsystem::HandlePhonemeAlignmentReady(const FString& Text, const TArray<FPhonemeSpan>& Spans, float AlignedSeconds)
{
	if (!Text.Equals(PendingUtteranceText))
	{
		return;	// belongs to a reply that has already been superseded
	}
	PendingAlignment = Spans;
	PendingAlignedSeconds = AlignedSeconds;
	bAlignmentResolved = true;

	if (bAudioReady)
	{
		DriveMouthFromBestSource();
	}
}

void UCompanionRuntimeSubsystem::DriveMouthFromBestSource()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AlignmentGraceHandle);
	}
	// Three callers race to get here and only the first should win; a second track would restart
	// the mouth mid-sentence, which reads as a stutter.
	if (bMouthDriven || !bAudioReady)
	{
		return;
	}
	bMouthDriven = true;

	UCompanionFaceComponent* Face = ResolveFace();
	if (!Face)
	{
		return;
	}

	EVisemeTimingSource Source = EVisemeTimingSource::TextEstimate;
	const TArray<FVisemeSpan> Track = FVisemePlanner::PlanBest(
		PendingUtteranceText, PendingAlignment, PendingAudioSeconds, Source);
	if (Track.Num() == 0)
	{
		return;
	}

	// The clip has been playing for however long the alignment took to arrive, so enter the track
	// at that point instead of at zero — otherwise every phoneme runs late by exactly the time
	// spent acquiring better timing, which would give back the drift this whole path removes.
	const float Elapsed = AudioStartRealTime > 0.0
		? FMath::Max(0.0f, static_cast<float>(FPlatformTime::Seconds() - AudioStartRealTime))
		: 0.0f;
	Face->PlayVisemeTrack(Track, Elapsed);

	if (Source == EVisemeTimingSource::Alignment)
	{
		// The ratio is the size of the correction PlanBest just applied, and a live check on the
		// two services: the sidecar's Kokoro and the voice service's Kokoro run about 7% apart on
		// this deployment, and anything much wider than that means they are configured with
		// different voices or models -- at which point the phoneme ORDER is still right but its
		// proportions describe a waveform nobody is listening to, and no amount of rescaling helps.
		const float Ratio = PendingAudioSeconds > 0.0f ? 100.0f * PendingAlignedSeconds / PendingAudioSeconds : 0.0f;
		UE_LOG(LogCompanionRuntime, Log,
			TEXT("Lip-sync source=%s: %d phonemes -> %d spans; aligned %.2fs stretched onto measured %.2fs (%.0f%%), entered %.0fms in."),
			FVisemePlanner::TimingSourceName(Source), PendingAlignment.Num(), Track.Num(),
			PendingAlignedSeconds, PendingAudioSeconds, Ratio, Elapsed * 1000.0f);
	}
	else
	{
		UE_LOG(LogCompanionRuntime, Log,
			TEXT("Lip-sync source=%s: %d spans over %.2fs, entered %.0fms in."),
			FVisemePlanner::TimingSourceName(Source), Track.Num(), PendingAudioSeconds, Elapsed * 1000.0f);
	}
}

void UCompanionRuntimeSubsystem::HandleCompanionSpoke(const FString& Text)
{
	UE_LOG(LogCompanionRuntime, Log, TEXT("Companion replied (%d chars): %s"), Text.Len(), *Text.Left(160));

	if (Hud.IsValid())
	{
		Hud->AppendChatEntry(TEXT("Eliza"), Text);
		Hud->SetStatusText(TEXT("Type to Eliza and press Enter"));
	}

	// Safety net for the adoption above: this handler runs from the broadcast the character fires
	// immediately BEFORE it speaks, so binding here is still in time for the very first reply even
	// if the next-tick resolve never ran.
	ResolveVoice();

	UCompanionFaceComponent* Face = ResolveFace();
	if (!Face)
	{
		UE_LOG(LogCompanionRuntime, Warning, TEXT("Reply received but no face bound; mouth will not move."));
		return;
	}

	// EXPRESSION FIRST, and deliberately ABOVE the TTS branch.
	//
	// UEmotionMapper has been implemented and unit-tested since the beginning and nothing ever
	// called it in the shipped game: AMetaHumanCompanionCharacter fires OnSetEmotion, which is a
	// BlueprintImplementableEvent with no Blueprint behind it, so the derived emotion was computed
	// and thrown away every single reply. This is the line that connects it to the face.
	//
	// It is set ONCE per reply rather than per viseme because an emotion is a SUSTAINED bias held
	// across the whole utterance — the face component blends it in over ~0.4s, holds it, and decays
	// it when the utterance ends. And it is set here rather than in HandleSpeechAudioReady because
	// with a voice service configured the mouth is deferred to that callback, a network round trip
	// later; her expression should already be right by the time she opens her mouth, not arrive
	// after the first syllable.
	const EEmotionType Emotion = UEmotionMapper::DetectEmotionFromText(Text);
	Face->SetEmotion(Emotion, 1.0f);
	UE_LOG(LogCompanionRuntime, Log, TEXT("Emotion for reply: %s (face emotion %s)."),
		*UEnum::GetValueAsString(Emotion),
		Face->IsEmotionEnabled() ? TEXT("on") : TEXT("OFF by MHLLM_EMOTION=0"));

	if (Tts)
	{
		// Defer the mouth until the audio exists. Planning now and again on arrival would play the
		// utterance twice at two different speeds.
		BeginUtterance(Text);
		// Fired ALONGSIDE the voice, not before it. Sequencing them would add the aligner's whole
		// inference (~0.95s on core-2 for a five-second reply) to time-to-first-audio, nearly
		// doubling how long she takes to answer, to buy timing that arrives only a quarter second
		// late if both are asked at once.
		//
		// "Alongside" is now literal rather than arranged, and there is deliberately no Speak call
		// here. This handler runs from the OnCompanionSpoke broadcast the character fires
		// immediately BEFORE it speaks, so the alignment request goes out microseconds ahead of the
		// synthesis request without this code having to start either. Speaking here as well is what
		// made every reply audible twice; the state above is set in time either way, because the
		// broadcast is synchronous and the audio is a network round trip behind it.
		if (Aligner)
		{
			Aligner->RequestAlignment(Text);
		}
		return;
	}

	// No voice service: fall back to timing estimated from a speaking rate. Visibly fine on its
	// own, and only wrong once there is real audio to drift against.
	EVisemeTimingSource Source = EVisemeTimingSource::TextEstimate;
	const TArray<FVisemeSpan> Track = FVisemePlanner::PlanBest(Text, {}, 0.0f, Source);
	Face->PlayVisemeTrack(Track);

	UE_LOG(LogCompanionRuntime, Log, TEXT("Lip-sync source=%s (no voice service): %d spans over %.2fs."),
		FVisemePlanner::TimingSourceName(Source), Track.Num(), FVisemePlanner::TrackDuration(Track));
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
