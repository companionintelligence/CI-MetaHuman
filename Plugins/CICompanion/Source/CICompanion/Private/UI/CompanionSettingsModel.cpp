#include "UI/CompanionSettingsModel.h"

const TArray<FString>& FCompanionSettingsModel::AvailableVoices()
{
	// Kokoro's stock voice set. af_* are US female, am_* US male, bf_/bm_ British.
	static const TArray<FString> Voices = {
		TEXT("af_sky"), TEXT("af_bella"), TEXT("af_nicole"), TEXT("af_sarah"),
		TEXT("am_adam"), TEXT("am_michael"),
		TEXT("bf_emma"), TEXT("bf_isabella"), TEXT("bm_george"), TEXT("bm_lewis"),
	};
	return Voices;
}

const TArray<FString>& FCompanionSettingsModel::AvailableFaceProfiles()
{
	// Exactly the two FFaceProfile ships. Listing a third here would offer the operator a profile
	// that resolves zero curves and leaves the face frozen with no error.
	static const TArray<FString> Profiles = { TEXT("CC"), TEXT("ARKit") };
	return Profiles;
}

bool FCompanionSettingsModel::Validate()
{
	bool bWasValid = true;

	// Volume: a Pixel Streaming text box can deliver anything, including NaN from a malformed
	// float parse. Clamp rather than trust; a NaN reaching the audio component is not recoverable.
	if (FMath::IsNaN(MusicVolume) || !FMath::IsFinite(MusicVolume))
	{
		MusicVolume = 0.55f;
		bWasValid = false;
	}
	const float ClampedVolume = FMath::Clamp(MusicVolume, 0.0f, 1.0f);
	if (!FMath::IsNearlyEqual(ClampedVolume, MusicVolume))
	{
		MusicVolume = ClampedVolume;
		bWasValid = false;
	}

	// Chat log length. Zero would hide the transcript entirely, which reads as a broken HUD rather
	// than a setting; an unbounded value grows the Slate tree until the stream hitches.
	const int32 ClampedLines = FMath::Clamp(ChatLogLines, 1, 50);
	if (ClampedLines != ChatLogLines)
	{
		ChatLogLines = ClampedLines;
		bWasValid = false;
	}

	// Face profile must be one that exists. Case-insensitive because the operator types it.
	if (!AvailableFaceProfiles().ContainsByPredicate(
			[this](const FString& P) { return P.Equals(FaceProfile, ESearchCase::IgnoreCase); }))
	{
		FaceProfile = TEXT("CC");
		bWasValid = false;
	}

	// An unknown voice is ACCEPTED but flagged: Kokoro adds voices independently of this build, and
	// hard-rejecting one would make a newer server's voice unusable. The panel offers the known
	// list; typing past it is allowed.
	if (TtsVoice.TrimStartAndEnd().IsEmpty())
	{
		TtsVoice = TEXT("af_sky");
		bWasValid = false;
	}

	// Empty identifiers would silently disable the thing they name.
	if (AvatarId.TrimStartAndEnd().IsEmpty()) { AvatarId = TEXT("eliza"); bWasValid = false; }
	if (TtsModel.TrimStartAndEnd().IsEmpty()) { TtsModel = TEXT("kokoro-v1"); bWasValid = false; }
	if (LlmModel.TrimStartAndEnd().IsEmpty()) { LlmModel = TEXT("qwen3.8:27b"); bWasValid = false; }
	if (SttModel.TrimStartAndEnd().IsEmpty()) { SttModel = TEXT("whisper-base"); bWasValid = false; }

	return bWasValid;
}

ECompanionSettingDirty FCompanionSettingsModel::DiffAgainst(const FCompanionSettingsModel& Other) const
{
	ECompanionSettingDirty Dirty = ECompanionSettingDirty::None;

	if (AvatarId != Other.AvatarId || FaceProfile != Other.FaceProfile)
	{
		Dirty |= ECompanionSettingDirty::Avatar;
	}
	if (TtsVoice != Other.TtsVoice || TtsModel != Other.TtsModel || bVoiceEnabled != Other.bVoiceEnabled)
	{
		Dirty |= ECompanionSettingDirty::Voice;
	}
	if (SttModel != Other.SttModel || bSttEnabled != Other.bSttEnabled)
	{
		Dirty |= ECompanionSettingDirty::Speech;
	}
	if (bMusicEnabled != Other.bMusicEnabled || !FMath::IsNearlyEqual(MusicVolume, Other.MusicVolume))
	{
		Dirty |= ECompanionSettingDirty::Audio;
	}
	if (bChatVisible != Other.bChatVisible || ChatLogLines != Other.ChatLogLines)
	{
		Dirty |= ECompanionSettingDirty::Chat;
	}
	if (bBrandingVisible != Other.bBrandingVisible)
	{
		Dirty |= ECompanionSettingDirty::Branding;
	}
	if (bFaceSelfTest != Other.bFaceSelfTest || bShowDiagnostics != Other.bShowDiagnostics
		|| bOpeningShot != Other.bOpeningShot || bThinking != Other.bThinking
		|| LlmModel != Other.LlmModel)
	{
		Dirty |= ECompanionSettingDirty::Debug;
	}
	return Dirty;
}

FString FCompanionSettingsModel::ToCompactString() const
{
	return FString::Printf(
		TEXT("avatar=%s face=%s voice=%s(%s) tts=%s stt=%s(%s) llm=%s think=%d music=%d/%.2f chat=%d/%d brand=%d selftest=%d"),
		*AvatarId, *FaceProfile, *TtsVoice, bVoiceEnabled ? TEXT("on") : TEXT("off"),
		*TtsModel, *SttModel, bSttEnabled ? TEXT("on") : TEXT("off"),
		*LlmModel, bThinking ? 1 : 0,
		bMusicEnabled ? 1 : 0, MusicVolume,
		bChatVisible ? 1 : 0, ChatLogLines,
		bBrandingVisible ? 1 : 0, bFaceSelfTest ? 1 : 0);
}
