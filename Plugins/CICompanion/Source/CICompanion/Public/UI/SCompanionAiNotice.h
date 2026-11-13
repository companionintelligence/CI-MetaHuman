#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

// The line in the frame itself that says she is an AI.
//
// The resident introduces herself with a human name, and the EU AI Act (Art. 50(1), applicable
// since 2 August 2026) asks that people talking with an AI system are told so, clearly, by the
// first interaction at the latest. The player page says it above the video
// (Deployment/patch_pixelstreaming_player.py, with the privacy link), but a page is only one way to
// watch: this is part of every streamed frame and of a native window, from the first frame a viewer
// receives, and a recording or another player keeps it.
//
// Its own viewport layer rather than a row of SCompanionHUD, on purpose: the HUD can be switched off
// (MHLLM_HUD=0) or hidden from the operator console, and neither may take the notice with it. Top
// right, the one corner nothing else uses: the status pill and the console sit top left, the
// transcript down the left, the input row along the bottom, and the page's own links top centre.
// Hit-test invisible, so it never takes a click meant for the scene or the chat box.
class CICOMPANION_API SCompanionAiNotice : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCompanionAiNotice)
		{
		}
	SLATE_END_ARGS()

	// Word for word the player page's notice; CI fails if the two drift apart (checks.yml).
	static constexpr const TCHAR* NoticeText = TEXT("Eliza is an AI. Her replies are generated and can be wrong.");

	// The chat box's idle status, which says it again where people type.
	static constexpr const TCHAR* ChatPrompt = TEXT("Type to Eliza (an AI) and press Enter");

	void Construct(const FArguments& InArgs);

	// The text on screen, for the specs.
	FString GetDisplayedText() const;

private:
	TSharedPtr<class STextBlock> NoticeTextBlock;
};
