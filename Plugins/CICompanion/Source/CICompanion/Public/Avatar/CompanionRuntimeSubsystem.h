#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Voice/VisemePlanner.h"
#include "CompanionRuntimeSubsystem.generated.h"

class AMetaHumanCompanionCharacter;
class UCompanionFaceComponent;
class UKokoroPhonemeAligner;
class UKokoroTextToSpeech;

// Instantiates the companion and connects its replies to the face.
//
// This closes the gap that made everything else moot: AMetaHumanCompanionCharacter, the Ollama
// client, the stream parsers and the emotion mapper were all implemented and unit-tested, but
// NOTHING IN THE SHIPPED GAME EVER CONSTRUCTED ONE. The cooked build contained the entire
// conversational stack as unreachable code, so the diorama could only ever be a diorama.
//
// It is a subsystem for the same reason the face driver is: the map is authored headlessly by
// Python in a content-only project that cannot reference plugin C++ or embed a Blueprint
// (HANDOFF.md Lesson 6), so the character cannot be placed at author time. Spawning it at
// BeginPlay is the one route that survives that constraint.
//
// Speech reaches the face via the OnCompanionSpoke multicast delegate rather than the
// OnSpeak/OnSetVisemeWeights BlueprintImplementableEvents, which do nothing without a
// hand-authored Blueprint subclass that this pipeline cannot produce.
UCLASS()
class CICOMPANION_API UCompanionRuntimeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// Tears down everything OnWorldBeginPlay attached to something that OUTLIVES this subsystem.
	//
	// There was no Deinitialize at all, and two things leaked across a world teardown. The chat
	// focus guard is a 4 Hz LOOPING timer bound to this object, and the two Slate widgets were added
	// to GEngine->GameViewport -- which is owned by the engine, not the world, and therefore keeps
	// holding them after the world that made them is gone. In the editor that means every PIE
	// session leaves another HUD stacked on the viewport; in a packaged build it matters on any
	// level transition.
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM")
	AMetaHumanCompanionCharacter* GetCompanion() const { return Companion; }

	// Routes a user turn to the companion. The single entry point any future input path (Pixel
	// Streaming data channel, HUD, STT) should call, so none of them need to find the actor.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM")
	bool SubmitUserText(const FString& Text);

	// A turn that arrived as SPEECH rather than as keystrokes.
	//
	// Distinct from SubmitUserText only in that it does the things the chat box does for itself:
	// echo the words into the log so the viewer can see what was heard, put the HUD into
	// "thinking...", and cut the scripted opening short. Without this the STT path reached the model
	// but left the UI showing nothing at all — and a transcription the user cannot see is a
	// transcription they cannot tell was WRONG, which is the first thing anyone needs from a mic.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM")
	bool SubmitTranscribedText(const FString& Text);

	// Speaks an exact line WITHOUT consulting the LLM. The opening line is a brand statement: it
	// must be word-for-word identical every run, and must not wait on a model round trip while the
	// camera holds on her face. Drives the same face/TTS path a generated reply would.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM")
	bool SpeakLiteral(const FString& Text);

	// Shows/hides the on-screen chat box. Bound to the same entry point every other input path
	// uses, so native and Pixel Streaming cannot diverge in behaviour.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM")
	void SetHudVisible(bool bVisible);

	// Puts the caret back in the chat box. Public so anything that steals focus (a future
	// Blueprint overlay, a Pixel Streaming reconnect) can hand it back.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM")
	void FocusChatInput();

private:
	void CreateHud();
	void HandleHudTextSubmitted(const FString& Text);

	// The presentation half of "a user turn arrived", shared by every input path so typed and spoken
	// turns cannot drift into looking different to the viewer.
	void AnnounceUserTurn(const FString& Text);

	// Connects the HUD's mic button to the STT backend.
	//
	// Everything either side of this handler already existed and was tested: SCompanionHUD has
	// published OnMicToggled since it was written, AMetaHumanCompanionCharacter has StartListening /
	// StopListening, UWhisperSpeechToText captures the host device and POSTs the WAV, and the
	// character binds OnTranscribed straight into SubmitUserText. NOTHING BOUND THE TWO ENDS — the
	// delegate was published to no subscriber, so the button toggled its own colour and did
	// literally nothing else. That is why the gap survived a 200-spec suite: every piece passed its
	// own test and the wire between them was never a piece.
	//
	// It is also why this rolls the button back on failure rather than trusting it. The mic toggle is
	// optimistic — SCompanionHUD flips bMicListening before anyone has checked whether a capture
	// device exists — so a UI that is not corrected claims she is listening while the null backend
	// quietly discards the turn.
	void HandleMicToggled(bool bListening);

	// Switches the player controller into a mode where Slate sees the keyboard and mouse, shows a
	// cursor, and focuses the text box. Without it a packaged game starts in
	// FInputModeGameOnly: the viewport captures the mouse, the cursor is hidden, and every
	// keystroke is routed to the pawn -- so the chat box renders perfectly and cannot be used.
	//
	// Retries: the local player controller is not guaranteed to exist when a world subsystem's
	// BeginPlay runs.
	void ApplyChatInputMode();

	// Focusing the box ONCE is not enough over Pixel Streaming.
	//
	// Focus is not sticky: any click that misses the HUD lands on the game viewport, which takes
	// keyboard focus and never gives it back, so every later keystroke is discarded. Locally that is
	// a nuisance you fix by clicking the box again. Over the stream it is fatal, because with the
	// stock player page the pointer is locked and a click carries a virtual cursor position rather
	// than the one the viewer aimed at -- the click cannot be aimed back at the box. This ticks a
	// quarter-second guard that puts the caret back whenever nothing else that can type holds it.
	void RearmChatFocus();

	FTimerHandle ChatFocusGuardHandle;
	// Logged once, not every quarter second: the fact that focus was recovered at all is the
	// interesting event, and a per-tick line would bury a multi-hour kiosk log.
	bool bLoggedChatFocusRecovery = false;

	int32 ChatInputModeAttempts = 0;
	static constexpr int32 MaxChatInputModeAttempts = 20;

	// Slate, not UMG: UMG needs a WidgetBlueprint asset, and the map is authored headlessly in a
	// content-only project that cannot carry one (HANDOFF.md Lesson 6). Slate is pure C++ and so
	// survives that constraint -- the same reason the face driver and stage director are C++.
	TSharedPtr<class SCompanionHUD> Hud;
	TSharedPtr<class SCompanionSettingsPanel> SettingsPanel;

	// The SWeakWidget wrappers actually handed to the viewport.
	//
	// Kept because RemoveViewportWidgetContent matches by IDENTITY: the viewport holds the wrapper,
	// not the widget inside it, so removing Hud directly matches nothing and silently succeeds.
	// Resetting the inner pointers alone does blank the widgets (that is the point of SWeakWidget),
	// but it leaves an empty placeholder slot in the viewport for every world that ever ran.
	TSharedPtr<class SWidget> HudSlot;
	TSharedPtr<class SWidget> SettingsPanelSlot;

	// Pushes changed settings to the systems that own them. Only the subsystems the diff marks
	// dirty are touched: rebinding the face or restarting audio on every edit would hitch a live
	// stream, and re-resolving morph curves is not free.
	void ApplySettings(const struct FCompanionSettingsModel& NewSettings);

	// Receives the finished reply and drives the mouth from it. Must be a UFUNCTION: the delegate
	// is dynamic, so a lambda or raw member cannot bind to it.
	UFUNCTION()
	void HandleCompanionSpoke(const FString& Text);

	// The failure path out of the same actor. Must be a UFUNCTION for the same reason its sibling
	// is: the delegate is dynamic, so a lambda or raw member cannot bind to it.
	UFUNCTION()
	void HandleCompanionError(const FString& Message);

	// Receives synthesized speech: the text that is now playing and the clip's true length. Starts
	// the mouth, or — if the phoneme aligner has not answered yet — starts the short wait for it.
	UFUNCTION()
	void HandleSpeechAudioReady(const FString& Text, float DurationSeconds);

	// Adopts the character's own TTS component and binds the audio-ready callback to it. Idempotent
	// and called from two places, because the component is not there yet when this subsystem starts:
	// the character swaps its default UNullTextToSpeech for the real one in its own BeginPlay, which
	// runs a frame after the SpawnActor that created it.
	void ResolveVoice();

	// The voice-resolution outcome is stated once, not once per reply.
	bool bVoiceResolutionReported = false;

	// Resets the per-utterance timing state. Called at the moment a reply is handed to the voice,
	// so a barge-in cannot leave the previous reply's alignment attached to the new one.
	void BeginUtterance(const FString& Text);

	// Tears down an utterance that is being INTERRUPTED, across all three things that outlive it.
	//
	// Barge-in was previously only half-implemented, and the half that was missing is the half a
	// viewer sees. The character cancels the LLM request and stops the audio — so she goes quiet —
	// but nothing ever called UCompanionFaceComponent::StopSpeaking, which had zero callers in the
	// whole repo. The viseme track is on its own clock and does not care that the audio stopped, so
	// she carried on MOUTHING the abandoned reply in silence until the track ran out.
	//
	// The alignment grace timer is the third piece and the least obvious: it can still be armed when
	// the interrupt lands, and it fires into DriveMouthFromBestSource, which would start a fresh
	// viseme track for the DEAD utterance — a mouth moving to a sentence that was cancelled a second
	// earlier and whose audio was never played.
	void CancelUtterance();

	// Receives the aligner's answer, successful or not (empty Spans == not available).
	void HandlePhonemeAlignmentReady(const FString& Text, const TArray<FPhonemeSpan>& Spans, float AlignedSeconds);

	// Builds and plays the viseme track from the best timing that has arrived, exactly once per
	// utterance. Reached from three directions — audio ready, alignment ready, grace expired —
	// whichever gets there first, which is why it is idempotent.
	void DriveMouthFromBestSource();

	// How long the mouth will wait for real alignment after the audio has already started.
	//
	// Measured on core-2 for the opening line: Lemonade returns audio in ~0.70s and the aligner
	// answers in ~0.95s, so alignment lands roughly a quarter second after playback begins, and
	// both scale together with utterance length. Kokoro also pads ~0.48s of leading silence, so
	// that wait is spent where the mouth would be shut anyway. A second is comfortably past the
	// measured gap and still short enough that a dead aligner is a beat, not a stall.
	static constexpr float AlignmentGraceSeconds = 1.0f;

	UPROPERTY(Transient)
	TObjectPtr<UKokoroTextToSpeech> Tts;

	// Real per-phoneme timing from the kokoro-align sidecar. Null when no aligner URL is
	// configured, which is a precision loss and not a failure.
	UPROPERTY(Transient)
	TObjectPtr<UKokoroPhonemeAligner> Aligner;

	FString PendingUtteranceText;
	TArray<FPhonemeSpan> PendingAlignment;
	float PendingAlignedSeconds = 0.0f;
	float PendingAudioSeconds = 0.0f;
	double AudioStartRealTime = 0.0;
	bool bAlignmentResolved = true;
	bool bAudioReady = false;
	bool bMouthDriven = false;
	FTimerHandle AlignmentGraceHandle;

	UPROPERTY(Transient)
	TObjectPtr<AMetaHumanCompanionCharacter> Companion;

	// Resolved lazily from UCompanionFaceSubsystem, whose OnWorldBeginPlay ordering relative to
	// this one is not guaranteed.
	UCompanionFaceComponent* ResolveFace() const;
};
