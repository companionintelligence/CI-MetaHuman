#pragma once

#include "CoreMinimal.h"
#include "UI/CompanionSettingsModel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SEditableTextBox;
class SVerticalBox;

DECLARE_DELEGATE_OneParam(FOnCompanionSettingsChanged, const FCompanionSettingsModel& /*New*/);

// The operator console, drawn INTO THE VIEWPORT alongside the chat box.
//
// Viewport content rather than a browser-side DOM panel, for the same reason the chat box is: Pixel
// Streaming captures the back buffer, so a Slate panel reaches the streamed client AND a native
// window with one implementation, and clicks travel back over the input channel that already
// exists. A DOM overlay would work for the stream and leave a native build with no console at all.
//
// Everything here is live: changing the voice affects the next utterance, muting the music takes
// effect immediately. That is the point -- the alternative is a 25-minute cook to hear a different
// voice.
class CICOMPANION_API SCompanionSettingsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCompanionSettingsPanel) {}
		SLATE_EVENT(FOnCompanionSettingsChanged, OnSettingsChanged)
		SLATE_ARGUMENT(FCompanionSettingsModel, InitialSettings)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	const FCompanionSettingsModel& GetSettings() const { return Settings; }

	// Replaces the whole model (e.g. from a registry load) without re-broadcasting, so applying
	// settings cannot loop back into another change notification.
	void SetSettingsQuiet(const FCompanionSettingsModel& InSettings);

	void SetVisible(bool bVisible);
	bool IsPanelVisible() const;

	// --- test surface -------------------------------------------------------------------------
	// Mirrors SCompanionHUD's Simulate* pattern. These drive the SAME code paths the real widgets
	// call, so a headless test exercises validation and notification rather than a parallel stub.
	void SimulateSelectVoice(const FString& Voice);
	void SimulateSelectFaceProfile(const FString& Profile);
	void SimulateSetMusicVolume(float Volume);
	void SimulateToggleMusic();
	void SimulateToggleChat();
	void SimulateToggleBranding();
	void SimulateToggleSelfTest();
	void SimulateToggleDiagnostics();
	void SimulateSetLlmModel(const FString& Model);
	void SimulateSetTtsModel(const FString& Model);
	void SimulateSetSttModel(const FString& Model);
	void SimulateSetAvatar(const FString& AvatarId);

	// How many times a change has been broadcast. Lets a test assert that an idempotent set does
	// NOT notify, which is what stops a slider redundantly rebinding the face every frame.
	int32 GetChangeCount() const { return ChangeCount; }

private:
	// Validates, diffs, and notifies only when something actually changed.
	void CommitChange();

	TSharedRef<class SWidget> BuildAvatarSection();
	TSharedRef<class SWidget> BuildVoiceSection();
	TSharedRef<class SWidget> BuildAudioSection();
	TSharedRef<class SWidget> BuildChatSection();
	TSharedRef<class SWidget> BuildDebugSection();

	FCompanionSettingsModel Settings;

	// The last state actually broadcast. Diffing against THIS rather than against the pre-edit
	// Settings is what makes a rejected value silent: clamping 3.0 back to 1.0 leaves the model
	// identical to what listeners already have, so nothing is re-pushed.
	FCompanionSettingsModel LastNotified;

	FOnCompanionSettingsChanged OnSettingsChanged;
	int32 ChangeCount = 0;
	bool bPanelVisible = false;
};
