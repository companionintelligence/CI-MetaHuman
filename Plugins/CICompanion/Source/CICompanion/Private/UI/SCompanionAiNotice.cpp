#include "UI/SCompanionAiNotice.h"
#include "UI/CIBrandStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

void SCompanionAiNotice::Construct(const FArguments& InArgs)
{
	// Added as viewport content like the HUD, so it spans the whole screen: the root must let every
	// click through, and so must its children, since nothing in it takes input at all.
	SetVisibility(EVisibility::HitTestInvisible);

	ChildSlot
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Top)
	// The status pill's margins, mirrored into the opposite corner.
	.Padding(12.f, 10.f, 12.f, 4.f)
	[
		SNew(SBorder)
		.BorderImage(CIBrand::PillBrush())
		.Padding(FMargin(9.f, 4.f))
		[
			// Brighter and a size up from the status text: it has to be read, not just be there.
			SAssignNew(NoticeTextBlock, STextBlock)
			.Font(CIBrand::SpeakerFont())
			.ColorAndOpacity(FSlateColor(CIBrand::SecondaryFg()))
			.Text(FText::FromString(NoticeText))
		]
	];
}

FString SCompanionAiNotice::GetDisplayedText() const
{
	return NoticeTextBlock.IsValid() ? NoticeTextBlock->GetText().ToString() : FString();
}
