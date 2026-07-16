#include "Avatar/EmotionMapper.h"

namespace
{
	struct FEmotionKeywords
	{
		EEmotionType Emotion;
		TArray<FString> Keywords;
	};

	// Checked in order; the first emotion with a matching keyword wins. Order matters:
	// more specific/strong emotions are listed before milder ones so e.g. "amazing" wins
	// over an incidental "ok" elsewhere in the same sentence.
	const TArray<FEmotionKeywords>& GetEmotionKeywordTable()
	{
		static const TArray<FEmotionKeywords> Table = {
			{ EEmotionType::Love,  { TEXT("love"), TEXT("adore"), TEXT("<3"), TEXT("❤")} },
			{ EEmotionType::Angry, { TEXT("angry"), TEXT("furious"), TEXT("annoyed"), TEXT("mad at") } },
			{ EEmotionType::Fear,  { TEXT("afraid"), TEXT("scared"), TEXT("worried"), TEXT("nervous") } },
			{ EEmotionType::Alert, { TEXT("watch out"), TEXT("careful"), TEXT("warning"), TEXT("danger") } },
			{ EEmotionType::Sad,   { TEXT("sad"), TEXT("sorry"), TEXT("unfortunately"), TEXT("😢") } },
			{ EEmotionType::Happy, { TEXT("great"), TEXT("amazing"), TEXT("awesome"), TEXT("happy"), TEXT("glad"), TEXT("😊") } },
		};
		return Table;
	}
}

EEmotionType UEmotionMapper::DetectEmotionFromText(const FString& Text)
{
	for (const FEmotionKeywords& Entry : GetEmotionKeywordTable())
	{
		for (const FString& Keyword : Entry.Keywords)
		{
			if (Text.Contains(Keyword, ESearchCase::IgnoreCase))
			{
				return Entry.Emotion;
			}
		}
	}
	return EEmotionType::Neutral;
}

EAnimationCategory UEmotionMapper::EmotionToAnimCategory(EEmotionType Emotion)
{
	switch (Emotion)
	{
	case EEmotionType::Happy:
	case EEmotionType::Love:
		return EAnimationCategory::Expression;
	case EEmotionType::Angry:
	case EEmotionType::Alert:
		return EAnimationCategory::Action;
	case EEmotionType::Sad:
	case EEmotionType::Fear:
		return EAnimationCategory::Gesture;
	case EEmotionType::Neutral:
	default:
		return EAnimationCategory::Idle;
	}
}
