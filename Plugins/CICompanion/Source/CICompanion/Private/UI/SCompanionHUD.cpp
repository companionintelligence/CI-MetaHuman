#include "UI/SCompanionHUD.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"

void SCompanionHUD::Construct(const FArguments& InArgs)
{
	OnTextSubmitted = InArgs._OnTextSubmitted;
	OnMicToggled = InArgs._OnMicToggled;

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f)
		[
			SAssignNew(StatusTextBlock, STextBlock)
			.Text(FText::FromString(TEXT("Disconnected")))
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(4.f)
		[
			SAssignNew(ChatScrollBox, SScrollBox)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SAssignNew(InputBox, SEditableTextBox)
				.HintText(FText::FromString(TEXT("Type your message here...")))
				.OnTextChanged(this, &SCompanionHUD::HandleTextChanged)
				.OnTextCommitted(this, &SCompanionHUD::HandleTextCommitted)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Mic")))
				.OnClicked(this, &SCompanionHUD::HandleMicClicked)
			]
		]
	];
}

void SCompanionHUD::AppendChatEntry(const FString& Speaker, const FString& Text)
{
	ChatEntries.Add(FCompanionChatEntry{ Speaker, Text });

	if (ChatScrollBox.IsValid())
	{
		ChatScrollBox->AddSlot()
			.Padding(0.f, 2.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(FText::FromString(FString::Printf(TEXT("%s: %s"), *Speaker, *Text)))
			];
		ChatScrollBox->ScrollToEnd();
	}
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

void SCompanionHUD::HandleTextCommitted(const FText& InText, ETextCommit::Type CommitType)
{
	if (CommitType != ETextCommit::OnEnter)
	{
		return;
	}

	const FString Text = InText.ToString();
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

void SCompanionHUD::SimulateSubmitText(const FString& Text)
{
	if (InputBox.IsValid())
	{
		InputBox->SetText(FText::FromString(Text));
	}
	HandleTextCommitted(FText::FromString(Text), ETextCommit::OnEnter);
}

void SCompanionHUD::SimulateMicToggleClick()
{
	HandleMicClicked();
}
