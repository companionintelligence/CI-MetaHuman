#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EmotionMapper.generated.h"

// Mirrors CI-Spatial-Companion-WebXR's EmotionType vocabulary so the same emotional
// palette carries across the org's web and Unreal companions.
UENUM(BlueprintType)
enum class EEmotionType : uint8
{
	Neutral,
	Happy,
	Angry,
	Sad,
	Fear,
	Love,
	Alert
};

// Body-animation category an emotion resolves to (mirrors WebXR's EMOTION_TO_CATEGORY).
UENUM(BlueprintType)
enum class EAnimationCategory : uint8
{
	Idle,
	Communication,
	Gesture,
	Expression,
	Action
};

UCLASS()
class CICOMPANION_API UEmotionMapper : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Keyword/heuristic emotion detection over LLM response text. Pure and
	// table-driven so it's fully unit-testable without any LLM/TTS in the loop.
	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Emotion")
	static EEmotionType DetectEmotionFromText(const FString& Text);

	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Emotion")
	static EAnimationCategory EmotionToAnimCategory(EEmotionType Emotion);
};
