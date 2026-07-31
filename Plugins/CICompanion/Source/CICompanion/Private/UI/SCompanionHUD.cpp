#include "UI/SCompanionHUD.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"

void SCompanionHUD::Construct(const FArguments& InArgs)
{
	OnTextSubmitted = InArgs._OnTextSubmitted;
	OnMicToggled = InArgs._OnMicToggled;

	// The HUD is added as viewport content, so it is stretched across the WHOLE screen. A
	// hit-testable root therefore turns the entire viewport into a click sink and, worse, swallows
	// the click that was meant for the text box on its way past the transparent area above it.
	// SelfHitTestInvisible means "draw me, but let the pointer through -- my children still hit
	// test", which is exactly right for an overlay: only the actual controls take clicks.
	SetVisibility(EVisibility::SelfHitTestInvisible);

	ChildSlot
	[
		SNew(SVerticalBox)
		.Visibility(EVisibility::SelfHitTestInvisible)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f)
		[
			SAssignNew(StatusTextBlock, STextBlock)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.Text(FText::FromString(TEXT("Disconnected")))
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(4.f)
		[
			SAssignNew(ChatScrollBox, SScrollBox)
			// Hit-testable ON PURPOSE, unlike its parents: the wheel has to reach it or there is no
			// scrollback through the transcript. It is a defined region well clear of the input row,
			// so it cannot intercept a click aimed at the text box.
			.Visibility(EVisibility::Visible)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f)
		[
			SNew(SHorizontalBox)
			.Visibility(EVisibility::SelfHitTestInvisible)

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SAssignNew(InputBox, SEditableTextBox)
				// Explicit, not inherited: this is the one widget in the HUD that MUST take the
				// pointer, and it sits inside three SelfHitTestInvisible parents.
				.Visibility(EVisibility::Visible)
				.HintText(FText::FromString(TEXT("Type your message here...")))
				// Slate's DEFAULT is to drop keyboard focus the moment a commit fires. That makes
				// the box work exactly once: the first Enter sends, focus goes nowhere, and every
				// subsequent keystroke falls through to the game. Keeping focus is what makes this
				// a chat box rather than a single-shot prompt.
				.ClearKeyboardFocusOnCommit(false)
				// Clicking back into a box that select-all'd its contents means the next character
				// silently wipes what is there.
				.SelectAllTextWhenFocused(false)
				.RevertTextOnEscape(false)
				.OnTextChanged(this, &SCompanionHUD::HandleTextChanged)
				.OnTextCommitted(this, &SCompanionHUD::HandleTextCommitted)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 0.f, 0.f, 0.f)
			[
				// A pointer-only route to the same submit path. Over Pixel Streaming a phone or a
				// tablet has no Enter key at all, so an Enter-only box is unusable there even once
				// focus is correct.
				SNew(SButton)
				.Visibility(EVisibility::Visible)
				.Text(FText::FromString(TEXT("Send")))
				.OnClicked(this, &SCompanionHUD::HandleSendClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Visibility(EVisibility::Visible)
				.Text(FText::FromString(TEXT("Mic")))
				.OnClicked(this, &SCompanionHUD::HandleMicClicked)
			]
		]
	];
}

FReply SCompanionHUD::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (InputBox.IsValid())
	{
		return FReply::Handled().SetUserFocus(InputBox.ToSharedRef(), InFocusEvent.GetCause());
	}
	return FReply::Unhandled();
}

void SCompanionHUD::FocusInputBox()
{
	if (!InputBox.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	// Resolve a real widget path first. Focusing a widget that is not yet parented into a live
	// window is meaningless and logs, which is the state this is called in under -nullrhi tests and
	// during the frame the HUD is added to the viewport.
	FWidgetPath PathToInput;
	if (!FSlateApplication::Get().FindPathToWidget(InputBox.ToSharedRef(), PathToInput))
	{
		return;
	}
	// ALL users, not just user 0 -- see the header: Pixel Streaming's keyboard rides a different
	// Slate user than the local one.
	FSlateApplication::Get().SetAllUserFocus(PathToInput, EFocusCause::SetDirectly);
}

void SCompanionHUD::AppendChatEntry(const FString& Speaker, const FString& Text)
{
	ChatEntries.Add(FCompanionChatEntry{ Speaker, Text });

	if (ChatScrollBox.IsValid())
	{
		const TSharedRef<STextBlock> EntryWidget = SNew(STextBlock)
			.AutoWrapText(true)
			.Text(FText::FromString(FString::Printf(TEXT("%s: %s"), *Speaker, *Text)));

		ChatScrollBox->AddSlot()
			.Padding(0.f, 2.f)
			[
				EntryWidget
			];
		ChatEntryWidgets.Add(EntryWidget);
	}

	TrimChatLog();

	// After the trim, not before: eviction shortens the content and would otherwise leave the view
	// parked short of the line that was just added.
	if (ChatScrollBox.IsValid())
	{
		ChatScrollBox->ScrollToEnd();
	}
}

void SCompanionHUD::TrimChatLog()
{
	const int32 NumToDrop = ChatEntries.Num() - MaxRetainedChatEntries;
	if (NumToDrop <= 0)
	{
		return;
	}

	ChatEntries.RemoveAt(0, NumToDrop);

	// ChatEntryWidgets can be shorter than ChatEntries if entries were appended before Construct
	// ran, so drop against its own length rather than assuming the two arrays are the same size.
	const int32 NumWidgetsToDrop = FMath::Min(NumToDrop, ChatEntryWidgets.Num());
	if (ChatScrollBox.IsValid())
	{
		for (int32 Index = 0; Index < NumWidgetsToDrop; ++Index)
		{
			ChatScrollBox->RemoveSlot(ChatEntryWidgets[Index]);
		}
	}
	ChatEntryWidgets.RemoveAt(0, NumWidgetsToDrop);
}

void SCompanionHUD::SetStatusText(const FString& Status)
{
	if (StatusTextBlock.IsValid())
	{
		StatusTextBlock->SetText(FText::FromString(Status));
	}
}

void SCompanionHUD::ClearChatLog()
{
	ChatEntries.Reset();
	ChatEntryWidgets.Reset();
	if (ChatScrollBox.IsValid())
	{
		ChatScrollBox->ClearChildren();
	}
}

void SCompanionHUD::SetMicListening(bool bListening)
{
	bMicListening = bListening;
}

FReply SCompanionHUD::HandleMicClicked()
{
	bMicListening = !bMicListening;
	OnMicToggled.ExecuteIfBound(bMicListening);
	return FReply::Handled();
}

FReply SCompanionHUD::HandleSendClicked()
{
	// Reads the BOX, not a commit payload: a button click carries no text of its own.
	SubmitText(GetInputText());
	// Clicking a button moves focus to the button, which would leave the next keystroke homeless.
	FocusInputBox();
	return FReply::Handled();
}

void SCompanionHUD::HandleTextCommitted(const FText& InText, ETextCommit::Type CommitType)
{
	// Enter ONLY. Slate also fires this for OnUserMovedFocus, OnCleared and Default -- clicking
	// away from a half-typed line, or the box being cleared programmatically, must not send a turn
	// to the model.
	if (CommitType != ETextCommit::OnEnter)
	{
		return;
	}
	SubmitText(InText.ToString());
}

void SCompanionHUD::SubmitText(const FString& RawText)
{
	// Trimmed, so a stray space or a newline pasted in over the stream is not a "message". An empty
	// turn costs a model round trip and puts a blank bubble in the transcript.
	const FString Text = RawText.TrimStartAndEnd();
	if (Text.IsEmpty())
	{
		return;
	}

	OnTextSubmitted.ExecuteIfBound(Text);

	if (InputBox.IsValid())
	{
		InputBox->SetText(FText::GetEmpty());
	}
	InputText = FText::GetEmpty();
}

FString SCompanionHUD::GetInputText() const
{
	return InputBox.IsValid() ? InputBox->GetText().ToString() : InputText.ToString();
}

void SCompanionHUD::SimulateTyping(const FString& Text)
{
	if (InputBox.IsValid())
	{
		InputBox->SetText(FText::FromString(Text));
	}
	// SEditableTextBox::SetText does NOT fire OnTextChanged (Slate suppresses the callback for
	// programmatic sets), so mirror it by hand or the tracked text never moves.
	HandleTextChanged(FText::FromString(Text));
}

void SCompanionHUD::SimulateSubmitText(const FString& Text)
{
	SimulateTextCommit(Text, ETextCommit::OnEnter);
}

void SCompanionHUD::SimulateTextCommit(const FString& Text, ETextCommit::Type CommitType)
{
	SimulateTyping(Text);
	HandleTextCommitted(FText::FromString(Text), CommitType);
}

void SCompanionHUD::SimulateSendButtonClick()
{
	HandleSendClicked();
}

void SCompanionHUD::SimulateMicToggleClick()
{
	HandleMicClicked();
}
