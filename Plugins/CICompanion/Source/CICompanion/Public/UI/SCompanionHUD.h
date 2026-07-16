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

	void Construct(const FArguments& InArgs);

	// Appends a chat bubble and scrolls the log to the bottom.
	void AppendChatEntry(const FString& Speaker, const FString& Text);

	void SetStatusText(const FString& Status);

	void ClearChatLog();

	bool IsMicListening() const { return bMicListening; }

	int32 NumChatEntries() const { return ChatEntries.Num(); }
	const TArray<FCompanionChatEntry>& GetChatEntries() const { return ChatEntries; }

	// --- Test/automation hooks: drive the widget without real mouse/keyboard input. ---
	void SimulateSubmitText(const FString& Text);
	void SimulateMicToggleClick();

private:
	FReply HandleMicClicked();
	void HandleTextCommitted(const FText& InText, ETextCommit::Type CommitType);
	void HandleTextChanged(const FText& InText) { InputText = InText; }

	TSharedPtr<SEditableTextBox> InputBox;
	TSharedPtr<class SScrollBox> ChatScrollBox;
	TSharedPtr<class STextBlock> StatusTextBlock;

	FOnCompanionTextSubmitted OnTextSubmitted;
	FOnCompanionMicToggled OnMicToggled;

	TArray<FCompanionChatEntry> ChatEntries;
	FText InputText;
	bool bMicListening = false;
};
