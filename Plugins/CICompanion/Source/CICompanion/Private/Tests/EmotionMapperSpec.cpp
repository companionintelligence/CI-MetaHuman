#include "Misc/AutomationTest.h"
#include "Avatar/EmotionMapper.h"

BEGIN_DEFINE_SPEC(FEmotionMapperSpec, "MetaHumanLocalLLM.Backend.EmotionMapper", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FEmotionMapperSpec)

// Table-driven text->emotion->animation-category mapping. Pure functions, no LLM/TTS
// in the loop, so this suite runs headlessly in CI.
void FEmotionMapperSpec::Define()
{
	Describe("DetectEmotionFromText", [this]()
	{
		It("detects happy from positive keywords", [this]()
		{
			TestEqual("happy", static_cast<uint8>(UEmotionMapper::DetectEmotionFromText(TEXT("That's amazing news!"))), static_cast<uint8>(EEmotionType::Happy));
		});

		It("detects sad from apologetic language", [this]()
		{
			TestEqual("sad", static_cast<uint8>(UEmotionMapper::DetectEmotionFromText(TEXT("I'm sorry, that didn't work."))), static_cast<uint8>(EEmotionType::Sad));
		});

		It("detects love", [this]()
		{
			TestEqual("love", static_cast<uint8>(UEmotionMapper::DetectEmotionFromText(TEXT("I love this idea."))), static_cast<uint8>(EEmotionType::Love));
		});

		It("detects alert from warning language", [this]()
		{
			TestEqual("alert", static_cast<uint8>(UEmotionMapper::DetectEmotionFromText(TEXT("Careful, watch out for that."))), static_cast<uint8>(EEmotionType::Alert));
		});

		It("falls back to neutral when no keyword matches", [this]()
		{
			TestEqual("neutral", static_cast<uint8>(UEmotionMapper::DetectEmotionFromText(TEXT("The weather today is cloudy."))), static_cast<uint8>(EEmotionType::Neutral));
		});

		It("falls back to neutral for empty text", [this]()
		{
			TestEqual("neutral for empty", static_cast<uint8>(UEmotionMapper::DetectEmotionFromText(TEXT(""))), static_cast<uint8>(EEmotionType::Neutral));
		});

		It("is case-insensitive", [this]()
		{
			TestEqual("case-insensitive happy", static_cast<uint8>(UEmotionMapper::DetectEmotionFromText(TEXT("AMAZING!"))), static_cast<uint8>(EEmotionType::Happy));
		});
	});

	Describe("EmotionToAnimCategory", [this]()
	{
		It("maps Happy to Expression", [this]()
		{
			TestEqual("happy->expression", static_cast<uint8>(UEmotionMapper::EmotionToAnimCategory(EEmotionType::Happy)), static_cast<uint8>(EAnimationCategory::Expression));
		});

		It("maps Neutral to Idle", [this]()
		{
			TestEqual("neutral->idle", static_cast<uint8>(UEmotionMapper::EmotionToAnimCategory(EEmotionType::Neutral)), static_cast<uint8>(EAnimationCategory::Idle));
		});

		It("maps Angry to Action", [this]()
		{
			TestEqual("angry->action", static_cast<uint8>(UEmotionMapper::EmotionToAnimCategory(EEmotionType::Angry)), static_cast<uint8>(EAnimationCategory::Action));
		});

		It("maps Sad to Gesture", [this]()
		{
			TestEqual("sad->gesture", static_cast<uint8>(UEmotionMapper::EmotionToAnimCategory(EEmotionType::Sad)), static_cast<uint8>(EAnimationCategory::Gesture));
		});
	});
}
