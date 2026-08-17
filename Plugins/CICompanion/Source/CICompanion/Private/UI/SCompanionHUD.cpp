#include "UI/SCompanionHUD.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Layout/WidgetPath.h"
#include "UI/CIBrandStyle.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
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

		// Status reads as a PILL rather than loose text. It is the only always-on chrome, so it has
		// to be legible against whatever the camera happens to be pointing at, and bare glyphs over
		// a lit 3D set are not.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(12.f, 10.f, 12.f, 4.f)
		[
			SNew(SBorder)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.BorderImage(CIBrand::PillBrush())
			.Padding(FMargin(9.f, 4.f))
			[
				SAssignNew(StatusTextBlock, STextBlock)
				.Visibility(EVisibility::SelfHitTestInvisible)
				.Font(CIBrand::StatusFont())
				.ColorAndOpacity(FSlateColor(CIBrand::MutedFg()))
				.Text(FText::FromString(TEXT("Disconnected")))
			]
		]

		// The transcript is width-capped rather than full-bleed. She is centre-frame in a portrait
		// composition, and a transcript spanning the whole viewport runs text straight across her
		// face; 620px keeps a readable measure and leaves the subject clear.
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.HAlign(HAlign_Left)
		.Padding(12.f, 2.f, 12.f, 4.f)
		[
			SNew(SBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.WidthOverride(620.f)
			[
				SNew(SBorder)
				.Visibility(EVisibility::SelfHitTestInvisible)
				.BorderImage(CIBrand::ShellBrush())
				.Padding(FMargin(8.f, 6.f))
				[
					SAssignNew(ChatScrollBox, SScrollBox)
					// Hit-testable ON PURPOSE, unlike its parents: the wheel has to reach it or there
					// is no scrollback through the transcript. It is a defined region well clear of
					// the input row, so it cannot intercept a click aimed at the text box.
					.Visibility(EVisibility::Visible)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.f, 4.f, 12.f, 12.f)
		[
			SNew(SBorder)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.BorderImage(CIBrand::InputShellBrush())
			.Padding(FMargin(8.f, 6.f))
			[
			SNew(SHorizontalBox)
			.Visibility(EVisibility::SelfHitTestInvisible)

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(InputBox, SEditableTextBox)
				.Font(CIBrand::InputFont())
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
				// Fire on PRESS, not on press-then-release-while-still-captured.
				//
				// A streamed tap is not a local click. Epic's FakeTouchController turns one tap into
				// MouseEnter/MouseDown/MouseUp/MouseLeave with no MouseMove between them, and
				// FPixelStreaming2InputHandler drains its whole queue inside a single Tick -- so all
				// four land in ONE frame, and the default DownAndUp method (which needs the button to
				// still hold pointer capture on the release) does not fire. Measured on core-2: a
				// tap on Send delivered MOUSE_DOWN at exactly (657,1269) and produced no click.
				// MouseDown needs only the press, which is the one event that carries a position.
				.ClickMethod(EButtonClickMethod::MouseDown)
				.TouchMethod(EButtonTouchMethod::Down)
				.Text(FText::FromString(TEXT("Send")))
				.OnClicked(this, &SCompanionHUD::HandleSendClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Visibility(EVisibility::Visible)
				.ClickMethod(EButtonClickMethod::MouseDown)
				.TouchMethod(EButtonTouchMethod::Down)
				.Text(FText::FromString(TEXT("Mic")))
				.OnClicked(this, &SCompanionHUD::HandleMicClicked)
			]
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
	// ALL users, not just user 0. Not because the stream owns a different one -- it does not, see
	// the header -- but because this is a strict superset and costs nothing.
	FSlateApplication::Get().SetAllUserFocus(PathToInput, EFocusCause::SetDirectly);
}

bool SCompanionHUD::IsInputFocused() const
{
	// ...OrFocusedDescendants, because Slate parks the caret on the inner SEditableText: asking the
	// SEditableTextBox wrapper alone reports "not focused" for a box that is being typed into.
	return InputBox.IsValid() && InputBox->HasAnyUserFocusOrFocusedDescendants();
}

void SCompanionHUD::RestoreInputFocusIfLost()
{
	if (!InputBox.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	// A hidden HUD must stay hidden AND silent: grabbing the keyboard back for a collapsed widget
	// would take it away from whatever replaced it on screen.
	const EVisibility Current = GetVisibility();
	if (Current == EVisibility::Collapsed || Current == EVisibility::Hidden)
	{
		return;
	}
	if (IsInputFocused())
	{
		return;
	}

	// Do NOT fight another text field for the caret. The operator console carries its own editable
	// boxes (model name, voice, face profile); yanking focus out of one every quarter second would
	// make them impossible to type into. Anything else holding focus -- the game viewport above all
	// -- is not a typing target and has no claim on the keyboard.
	bool bOtherTextFieldFocused = false;
	FSlateApplication::Get().ForEachUser([&bOtherTextFieldFocused](FSlateUser& User)
	{
		const TSharedPtr<SWidget> Focused = User.GetFocusedWidget();
		if (!Focused.IsValid())
		{
			return;
		}
		const FName FocusedType = Focused->GetType();
		static const FName EditableTextType(TEXT("SEditableText"));
		static const FName MultiLineEditableTextType(TEXT("SMultiLineEditableText"));
		if (FocusedType == EditableTextType || FocusedType == MultiLineEditableTextType)
		{
			bOtherTextFieldFocused = true;
		}
	});
	if (bOtherTextFieldFocused)
	{
		return;
	}

	FocusInputBox();
}

void SCompanionHUD::AppendChatEntry(const FString& Speaker, const FString& Text)
{
	ChatEntries.Add(FCompanionChatEntry{ Speaker, Text });

	if (ChatScrollBox.IsValid())
	{
		// Compared against the VISITOR label rather than the companion's, because "You" is the only
		// one of the two that is fixed. Hers is the persona name and moves with MHLLM_COMPANION, so
		// testing for it positively would mislabel every line the moment somebody renamed her.
		const bool bIsVisitor = Speaker.Equals(TEXT("You"), ESearchCase::IgnoreCase);

		// ChatEntryWidgets keeps holding the TEXT block rather than the card, so TrimChatLog and
		// every existing spec that reads entry text keep working untouched.
		const TSharedRef<STextBlock> EntryWidget = SNew(STextBlock)
			.AutoWrapText(true)
			.Font(CIBrand::BodyFont())
			.ColorAndOpacity(FSlateColor(bIsVisitor ? CIBrand::Foreground() : CIBrand::SecondaryFg()))
			.Text(FText::FromString(Text));

		ChatScrollBox->AddSlot()
			.Padding(0.f, 3.f)
			[
				SNew(SBorder)
				.Visibility(EVisibility::SelfHitTestInvisible)
				// Her lines carry the teal hairline; the visitor's do not. One accent, used once, is
				// what the guide means by "surfaces are neutral, teal carries identity" -- outlining
				// both would spend the brand colour to say nothing.
				.BorderImage(bIsVisitor ? CIBrand::EntryCardBrush() : CIBrand::CompanionCardBrush())
				.Padding(FMargin(10.f, 7.f))
				[
					SNew(SVerticalBox)
					.Visibility(EVisibility::SelfHitTestInvisible)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 0.f, 0.f, 3.f)
					[
						SNew(STextBlock)
						.Visibility(EVisibility::SelfHitTestInvisible)
						.Font(CIBrand::SpeakerFont())
						// Teal for her, muted for the visitor: the transcript should be scannable by
						// colour alone, before a single word is read.
						.ColorAndOpacity(FSlateColor(bIsVisitor ? CIBrand::MutedFg() : CIBrand::Primary()))
						.Text(FText::FromString(Speaker.ToUpper()))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						EntryWidget
					]
				]
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
