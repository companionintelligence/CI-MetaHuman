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
// AMetaHumanCompanionCharacter, via UCompanionHUDWidget) purely through
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
	void SimulateSubmitText(const FString& Text);
	void SimulateMicToggleClick();

	// Rows actually parented to the scroll box. Distinct from NumChatEntries() on purpose: the bug
	// this guards is on-screen, so a test has to be able to see that the *widgets* were evicted and
	// not just the tracked array.
	int32 NumVisibleChatEntries() const { return ChatEntryWidgets.Num(); }

private:
	FReply HandleMicClicked();
	void HandleTextCommitted(const FText& InText, ETextCommit::Type CommitType);
	void HandleTextChanged(const FText& InText) { InputText = InText; }

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
