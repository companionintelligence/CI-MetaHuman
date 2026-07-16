#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CompanionHUDWidget.generated.h"

class SCompanionHUD;
class AMetaHumanCompanionCharacter;

// Thin UUserWidget host for the native SCompanionHUD, so the HUD can be added to the
// viewport (AddToViewport) from a level Blueprint without needing the Widget Blueprint
// designer — SCompanionHUD itself is pure C++/Slate.
UCLASS()
class CICOMPANION_API UCompanionHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Binds the HUD's text/mic events to the given companion character and appends its
	// replies to the chat log.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM")
	void BindToCompanion(AMetaHumanCompanionCharacter* Companion);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void HandleTextSubmitted(const FString& Text);
	void HandleMicToggled(bool bListening);

	UFUNCTION()
	void HandleCompanionSpoke(const FString& Text);

	TSharedPtr<SCompanionHUD> HUD;

	UPROPERTY()
	TObjectPtr<AMetaHumanCompanionCharacter> BoundCompanion;
};
