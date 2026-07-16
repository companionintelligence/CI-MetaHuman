#include "UI/CompanionHUDWidget.h"
#include "UI/SCompanionHUD.h"
#include "Avatar/MetaHumanCompanionCharacter.h"

TSharedRef<SWidget> UCompanionHUDWidget::RebuildWidget()
{
	SAssignNew(HUD, SCompanionHUD)
		.OnTextSubmitted(FOnCompanionTextSubmitted::CreateUObject(this, &UCompanionHUDWidget::HandleTextSubmitted))
		.OnMicToggled(FOnCompanionMicToggled::CreateUObject(this, &UCompanionHUDWidget::HandleMicToggled));

	return HUD.ToSharedRef();
}

void UCompanionHUDWidget::BindToCompanion(AMetaHumanCompanionCharacter* Companion)
{
	if (BoundCompanion)
	{
		BoundCompanion->OnCompanionSpoke.RemoveDynamic(this, &UCompanionHUDWidget::HandleCompanionSpoke);
	}

	BoundCompanion = Companion;

	if (BoundCompanion)
	{
		BoundCompanion->OnCompanionSpoke.AddDynamic(this, &UCompanionHUDWidget::HandleCompanionSpoke);
	}
}

void UCompanionHUDWidget::HandleTextSubmitted(const FString& Text)
{
	if (HUD.IsValid())
	{
		HUD->AppendChatEntry(TEXT("You"), Text);
	}

	if (BoundCompanion)
	{
		BoundCompanion->SubmitUserText(Text);
	}
}

void UCompanionHUDWidget::HandleMicToggled(bool bListening)
{
	if (!BoundCompanion)
	{
		if (HUD.IsValid())
		{
			HUD->SetMicListening(false);
			HUD->SetStatusText(TEXT("No companion"));
		}
		return;
	}

	if (bListening)
	{
		if (BoundCompanion->StartListening())
		{
			if (HUD.IsValid())
			{
				HUD->SetStatusText(TEXT("Listening"));
			}
		}
		else if (HUD.IsValid())
		{
			HUD->SetMicListening(false);
			HUD->SetStatusText(TEXT("Microphone unavailable"));
		}
	}
	else
	{
		BoundCompanion->StopListening();
		if (HUD.IsValid())
		{
			HUD->SetMicListening(false);
			HUD->SetStatusText(TEXT("Ready"));
		}
	}
}

void UCompanionHUDWidget::HandleCompanionSpoke(const FString& Text)
{
	if (HUD.IsValid())
	{
		HUD->AppendChatEntry(TEXT("Assistant"), Text);
	}
}
