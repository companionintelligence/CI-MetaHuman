#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SEditableTextBox.h"

// A single line in the chat log (mirrors CI-Spatial-Companion-WebXR's You/Assistant bubbles).
struct FCompanionChatEntry
{
	FString Speaker; // "You" or "Assistant"
	FString Text;
};

DECLARE_DELEGATE_OneParam(FOnCompanionTextSubmitted, const FString& /*Text*/);
DECLARE_DELEGATE_OneParam(FOnCompanionMicToggled, bool /*bListening*/);

// Native (code-only, no Widget Blueprint asset needed) HUD: chat log, text input, mic
// toggle, connection status. Bound to a controller (typically
// AMetaHumanCompanionCharacter) purely through
// delegates/setters, so it can be constructed and driven in isolation by tests with no
// Actor/World required.
class CICOMPANION_API SCompanionHUD : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCompanionHUD)
		{
		}
		SLATE_EVENT(FOnCompanionTextSubmitted, OnTextSubmitted)
		SLATE_EVENT(FOnCompanionMicToggled, OnMicToggled)
	SLATE_END_ARGS()

	// Upper bound on retained chat lines, enforced by dropping the oldest.
	//
	// The HUD is an overlay drawn on top of the companion, and a streamed kiosk session runs for
	// hours without anyone present to clear it -- so an uncapped log does not merely grow, it
	// eventually covers the character the viewer came to watch. (Observed: ~60 lines filling the
	// frame edge to edge.) 50 keeps roughly the last few exchanges in scrollback, which is all the
	// backscroll this UI is for.
	static constexpr int32 MaxRetainedChatEntries = 50;

	void Construct(const FArguments& InArgs);

	// Slate will not hand the keyboard to a widget that does not ASK for it. Without this the HUD
	// is never a focus candidate, so every keystroke falls through to the player pawn and the text
	// box can be clicked but never typed into.
	virtual bool SupportsKeyboardFocus() const override { return true; }

	// ...and when focus does land on the HUD (a click on the panel, a Slate focus restore, or the
	// FInputModeUIOnly widget-to-focus handoff), forward it to the caret rather than parking it on
	// a container that does nothing with key events.
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;

	// Puts the caret in the text box for EVERY Slate user, not just user 0.
	//
	// MEASURED (core-2, packaged 5.5 + PixelStreaming2, 2026-07-31): a streamed session creates NO
	// extra Slate user -- the log shows exactly one ("User Index 0"), and FPixelStreaming2InputHandler
	// hands key events to FSlateApplication under GetUserIndexForKeyboard(), i.e. user 0, the same
	// one a local keyboard uses. The earlier "Pixel Streaming rides a different user index" reasoning
	// was never observed and is not what was broken. SetAllUserFocus is kept anyway because it is a
	// strict superset of focusing user 0 and costs nothing; it is not load-bearing.
	// No-ops when the widget is not yet parented into a live window (headless tests, or before the
	// viewport has laid the HUD out).
	void FocusInputBox();

	// True when the caret is in the chat box -- i.e. the next keystroke will be typed rather than
	// dropped. Tests focus on the inner SEditableText, not the SEditableTextBox wrapper, so this
	// deliberately asks about descendants too.
	bool IsInputFocused() const;

	// Takes the caret back after something else stole it, and does nothing when it did not.
	//
	// Over Pixel Streaming a click is not aimed the way a local click is: with the stock player page
	// the pointer is LOCKED, so the streamer sends a virtual cursor driven by mouse DELTAS and the
	// first click lands wherever that cursor drifted -- measured at (0,640) and (720,1280) on a
	// 720x1280 stream when the browser clicked (300,1269) and (657,1269). A click that misses the
	// HUD reaches the game viewport, which takes keyboard focus, and from then on every keystroke is
	// silently discarded. Nothing gave it back, so ONE stray click bricked the chat box for the rest
	// of the session. Driven on a timer by UCompanionRuntimeSubsystem.
	void RestoreInputFocusIfLost();

	// The widget FInputModeUIOnly/FInputModeGameAndUI should focus. Exposed because the input mode
	// is owned by the player controller, which lives on the other side of the runtime subsystem.
	TSharedPtr<SEditableTextBox> GetInputWidget() const { return InputBox; }

	// Appends a chat bubble, evicts anything past MaxRetainedChatEntries, and scrolls the log to
	// the bottom so the newest line is the one on screen.
	void AppendChatEntry(const FString& Speaker, const FString& Text);

	void SetStatusText(const FString& Status);

	void ClearChatLog();

	// Used by the UMG host to roll back an optimistic mic-button toggle when the configured
	// STT backend cannot acquire a real capture device.
	void SetMicListening(bool bListening);

	bool IsMicListening() const { return bMicListening; }

	int32 NumChatEntries() const { return ChatEntries.Num(); }
	const TArray<FCompanionChatEntry>& GetChatEntries() const { return ChatEntries; }

	// --- Test/automation hooks: drive the widget without real mouse/keyboard input. ---

	// Types into the box exactly as a keystroke would: this is what OnTextChanged does, and it is
	// the only thing that makes the Send button (which reads the box, not a commit payload) have
	// anything to send.
	void SimulateTyping(const FString& Text);

	// The Enter path.
	void SimulateSubmitText(const FString& Text);

	// The general commit path, so a test can prove the NON-Enter commit types are ignored. Slate
	// fires OnTextCommitted for OnCleared, OnUserMovedFocus and Default as well as OnEnter -- and
	// "user clicked somewhere else" must not send a half-typed message to the model.
	void SimulateTextCommit(const FString& Text, ETextCommit::Type CommitType);

	void SimulateSendButtonClick();
	void SimulateMicToggleClick();

	// What is in the box right now. Used by tests to prove the box is cleared after a send and
	// left alone after an ignored commit.
	FString GetInputText() const;

	// Rows actually parented to the scroll box. Distinct from NumChatEntries() on purpose: the bug
	// this guards is on-screen, so a test has to be able to see that the *widgets* were evicted and
	// not just the tracked array.
	int32 NumVisibleChatEntries() const { return ChatEntryWidgets.Num(); }

private:
	FReply HandleMicClicked();
	FReply HandleSendClicked();
	void HandleTextCommitted(const FText& InText, ETextCommit::Type CommitType);
	void HandleTextChanged(const FText& InText) { InputText = InText; }

	// The one place a turn is actually dispatched. Both the Enter commit and the Send button land
	// here, so the two cannot drift into treating empty or whitespace-only input differently.
	void SubmitText(const FString& RawText);

	// Evicts oldest-first until both the tracked entries and the scroll box rows are within cap.
	void TrimChatLog();

	TSharedPtr<SEditableTextBox> InputBox;
	TSharedPtr<class SScrollBox> ChatScrollBox;
	TSharedPtr<class STextBlock> StatusTextBlock;

	// Parallel to ChatEntries, held only so an evicted line can be unparented from the scroll box:
	// SScrollBox::RemoveSlot needs the widget, and it exposes no index-based removal.
	TArray<TSharedRef<SWidget>> ChatEntryWidgets;

	FOnCompanionTextSubmitted OnTextSubmitted;
	FOnCompanionMicToggled OnMicToggled;

	TArray<FCompanionChatEntry> ChatEntries;
	FText InputText;
	bool bMicListening = false;
};
