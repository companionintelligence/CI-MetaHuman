#pragma once

#include "CoreMinimal.h"
#include "CompanionSettingsModel.generated.h"

// Which subsystem a changed setting has to be pushed to. Returned by Apply() so callers only touch
// what actually changed -- rebinding the face or restarting audio on every keystroke would hitch
// the stream, and re-resolving morph curves is not free.
enum class ECompanionSettingDirty : uint8
{
	None      = 0,
	Avatar    = 1 << 0,	// active resident changed: rebind face driver
	Voice     = 1 << 1,	// TTS voice/model: next utterance only, no rebind
	Speech    = 1 << 2,	// STT endpoint
	Audio     = 1 << 3,	// music volume / mute
	Chat      = 1 << 4,	// HUD visibility, log length
	Branding  = 1 << 5,	// wall wordmark visibility
	Debug     = 1 << 6,	// self-test, diagnostics overlay
};
ENUM_CLASS_FLAGS(ECompanionSettingDirty);

// Live-tunable runtime settings, separated from UMetaHumanLLMSettings on purpose.
//
// UMetaHumanLLMSettings is a UDeveloperSettings config object: it is where DEPLOYMENT decides the
// endpoints, and it is read once. This is the operator's console for a RUNNING stream -- swap the
// voice, mute the music, flip the face self-test on to see whether the rig is alive, all without a
// 25-minute cook. Everything here is deliberately validated rather than trusted, because the values
// arrive from a text box travelling over a Pixel Streaming data channel.
USTRUCT(BlueprintType)
struct CICOMPANION_API FCompanionSettingsModel
{
	GENERATED_BODY()

	// --- avatar ---
	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	FString AvatarId = TEXT("eliza");

	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	FString FaceProfile = TEXT("CC");	// "CC" | "ARKit"

	// --- voice out ---
	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	FString TtsVoice = TEXT("af_sky");

	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	FString TtsModel = TEXT("kokoro-v1");

	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	bool bVoiceEnabled = true;

	// --- speech in ---
	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	FString SttModel = TEXT("whisper-base");

	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	bool bSttEnabled = false;	// no capture path yet; off so the UI does not imply one

	// --- cognition ---
	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	FString LlmModel = TEXT("qwen3.8:27b");

	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	bool bThinking = false;

	// --- music / ambience ---
	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	bool bMusicEnabled = true;

	// 0..1. Default matches the authored AmbientSound multiplier so opening the panel does not
	// silently change the mix.
	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	float MusicVolume = 0.55f;

	// --- chat ---
	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	bool bChatVisible = true;

	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	int32 ChatLogLines = 8;

	// --- branding ---
	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	bool bBrandingVisible = true;

	// --- debug ---
	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	bool bFaceSelfTest = false;

	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	bool bShowDiagnostics = false;

	UPROPERTY(BlueprintReadWrite, Category = "MetaHumanLLM|Settings")
	bool bOpeningShot = true;

	// Clamps and normalises in place, returning false if anything had to be corrected. Called on
	// every mutation rather than at read time, so an out-of-range value can never reach a subsystem.
	bool Validate();

	// Field-by-field comparison, reporting WHICH subsystems need re-pushing.
	ECompanionSettingDirty DiffAgainst(const FCompanionSettingsModel& Other) const;

	// One-line summary for the log and the diagnostics overlay.
	FString ToCompactString() const;

	// The voices Kokoro ships. Kept as data rather than a hard-coded combo box so the panel and the
	// validator cannot disagree about what is selectable.
	static const TArray<FString>& AvailableVoices();
	static const TArray<FString>& AvailableFaceProfiles();
};
