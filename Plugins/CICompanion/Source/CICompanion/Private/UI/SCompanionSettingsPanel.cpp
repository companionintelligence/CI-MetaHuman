#include "UI/SCompanionSettingsPanel.h"

#include "CICompanionLog.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CompanionSettings"

namespace
{
	// Dark translucent plate. The panel sits over a photoreal render, so it needs enough contrast
	// to be readable against both the orange wall and the pale floor without being opaque enough
	// to hide the character it is describing.
	FSlateColor PanelBg()   { return FSlateColor(FLinearColor(0.02f, 0.03f, 0.04f, 0.86f)); }
	FSlateColor LabelCol()  { return FSlateColor(FLinearColor(0.62f, 0.72f, 0.78f, 1.0f)); }
	FSlateColor ValueCol()  { return FSlateColor(FLinearColor(0.92f, 0.94f, 0.95f, 1.0f)); }
	FSlateColor AccentCol() { return FSlateColor(FLinearColor(0.85f, 0.35f, 0.13f, 1.0f)); }	// wall orange

	TSharedRef<SWidget> SectionHeader(const FString& Title)
	{
		return SNew(STextBlock)
			.Text(FText::FromString(Title.ToUpper()))
			.ColorAndOpacity(AccentCol())
			.Margin(FMargin(0.0f, 8.0f, 0.0f, 3.0f));
	}

	TSharedRef<SWidget> Label(const FString& Text)
	{
		return SNew(STextBlock).Text(FText::FromString(Text)).ColorAndOpacity(LabelCol());
	}
}

void SCompanionSettingsPanel::Construct(const FArguments& InArgs)
{
	Settings = InArgs._InitialSettings;
	Settings.Validate();
	LastNotified = Settings;	// baseline, so the first real edit diffs against what listeners have
	OnSettingsChanged = InArgs._OnSettingsChanged;

	// The panel must OCCUPY only its own corner. Previously the SBorder was the whole ChildSlot,
	// so it stretched to the viewport and its 0.86-alpha plate dimmed the entire scene -- the
	// console worked, but it turned the shot it is meant to sit over into a murky silhouette.
	// Constraining the width and pinning to the top-left keeps the character lit.
	ChildSlot
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	[
		SNew(SBox)
		.WidthOverride(430.0f)
		.Padding(FMargin(8.0f, 8.0f, 0.0f, 0.0f))
		[
			SNew(SBorder)
			.BorderBackgroundColor(PanelBg())
			.Padding(FMargin(14.0f, 10.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "COMPANION INTELLIGENCE — CONSOLE"))
					.ColorAndOpacity(ValueCol())
				]
				+ SVerticalBox::Slot().AutoHeight()[ BuildAvatarSection() ]
				+ SVerticalBox::Slot().AutoHeight()[ BuildVoiceSection()  ]
				+ SVerticalBox::Slot().AutoHeight()[ BuildAudioSection()  ]
				+ SVerticalBox::Slot().AutoHeight()[ BuildChatSection()   ]
				+ SVerticalBox::Slot().AutoHeight()[ BuildDebugSection()  ]
			]
		]
	];

	SetVisible(false);	// starts hidden: the opening shot is a brand moment, not a settings screen
}

TSharedRef<SWidget> SCompanionSettingsPanel::BuildAvatarSection()
{
	TSharedRef<SHorizontalBox> Profiles = SNew(SHorizontalBox);
	for (const FString& P : FCompanionSettingsModel::AvailableFaceProfiles())
	{
		Profiles->AddSlot().AutoWidth().Padding(2.0f, 0.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(P))
			.OnClicked_Lambda([this, P]() { SimulateSelectFaceProfile(P); return FReply::Handled(); })
		];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ SectionHeader(TEXT("Avatar")) ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ Label(TEXT("id  ")) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(Settings.AvatarId); })
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type)
					{ SimulateSetAvatar(T.ToString()); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ Label(TEXT("rig ")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Profiles ]
		];
}

TSharedRef<SWidget> SCompanionSettingsPanel::BuildVoiceSection()
{
	// Voices as buttons rather than a combo box: SComboBox needs a backing TArray<TSharedPtr<>>
	// kept alive for the widget's lifetime, and a flat row of ten short names is both simpler and
	// faster to hit with a mouse travelling over a video stream.
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(2.0f));
	const TArray<FString>& Voices = FCompanionSettingsModel::AvailableVoices();
	for (int32 i = 0; i < Voices.Num(); ++i)
	{
		const FString V = Voices[i];
		Grid->AddSlot(i % 5, i / 5)
		[
			SNew(SButton)
			.Text(FText::FromString(V))
			.OnClicked_Lambda([this, V]() { SimulateSelectVoice(V); return FReply::Handled(); })
		];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ SectionHeader(TEXT("Voice / Speech")) ]
		+ SVerticalBox::Slot().AutoHeight()[ Grid ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]()
					{ return Settings.bVoiceEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
					{ Settings.bVoiceEnabled = (S == ECheckBoxState::Checked); CommitChange(); })
				[ Label(TEXT("speak")) ]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(10.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]()
					{ return Settings.bSttEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
					{ Settings.bSttEnabled = (S == ECheckBoxState::Checked); CommitChange(); })
				[ Label(TEXT("listen")) ]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ Label(TEXT("tts ")) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(Settings.TtsModel); })
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type)
					{ SimulateSetTtsModel(T.ToString()); })
			]
			+ SHorizontalBox::Slot().AutoWidth()[ Label(TEXT(" stt ")) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(Settings.SttModel); })
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type)
					{ SimulateSetSttModel(T.ToString()); })
			]
		];
}

TSharedRef<SWidget> SCompanionSettingsPanel::BuildAudioSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ SectionHeader(TEXT("Music")) ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]()
					{ return Settings.bMusicEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { SimulateToggleMusic(); })
				[ Label(TEXT("on")) ]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(8.0f, 0.0f)
			[
				SNew(SSlider)
				.Value_Lambda([this]() { return Settings.MusicVolume; })
				.OnValueChanged_Lambda([this](float V) { SimulateSetMusicVolume(V); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
					{ return FText::FromString(FString::Printf(TEXT("%.0f%%"), Settings.MusicVolume * 100.0f)); })
				.ColorAndOpacity(ValueCol())
			]
		];
}

TSharedRef<SWidget> SCompanionSettingsPanel::BuildChatSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ SectionHeader(TEXT("Chat / Branding")) ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]()
					{ return Settings.bChatVisible ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { SimulateToggleChat(); })
				[ Label(TEXT("chat")) ]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(10.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]()
					{ return Settings.bBrandingVisible ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { SimulateToggleBranding(); })
				[ Label(TEXT("wordmark")) ]
			]
		];
}

TSharedRef<SWidget> SCompanionSettingsPanel::BuildDebugSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ SectionHeader(TEXT("Debug")) ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]()
					{ return Settings.bFaceSelfTest ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { SimulateToggleSelfTest(); })
				[ Label(TEXT("viseme self-test")) ]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(10.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]()
					{ return Settings.bShowDiagnostics ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { SimulateToggleDiagnostics(); })
				[ Label(TEXT("diagnostics")) ]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ Label(TEXT("llm ")) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(Settings.LlmModel); })
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type)
					{ SimulateSetLlmModel(T.ToString()); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			// The live state, on screen. This is the line that answers "is it actually using what
			// the panel says?" without shelling into the container to read a log.
			SNew(STextBlock)
			.Text_Lambda([this]() { return FText::FromString(Settings.ToCompactString()); })
			.ColorAndOpacity(LabelCol())
			.AutoWrapText(true)
			.Visibility_Lambda([this]()
				{ return Settings.bShowDiagnostics ? EVisibility::Visible : EVisibility::Collapsed; })
		];
}

void SCompanionSettingsPanel::CommitChange()
{
	FCompanionSettingsModel Candidate = Settings;
	Candidate.Validate();

	// Compare AFTER validation, so a rejected value (say a volume of 3.0 clamped back to the
	// current 1.0) does not count as a change and does not re-push audio settings.
	const ECompanionSettingDirty Dirty = Candidate.DiffAgainst(LastNotified);
	Settings = Candidate;
	if (Dirty == ECompanionSettingDirty::None)
	{
		return;
	}

	LastNotified = Settings;
	++ChangeCount;
	UE_LOG(LogCompanionUI, Log, TEXT("Settings changed: %s"), *Settings.ToCompactString());
	OnSettingsChanged.ExecuteIfBound(Settings);
}

void SCompanionSettingsPanel::SetSettingsQuiet(const FCompanionSettingsModel& InSettings)
{
	Settings = InSettings;
	Settings.Validate();
	LastNotified = Settings;
}

void SCompanionSettingsPanel::SetVisible(bool bVisible)
{
	bPanelVisible = bVisible;
	SetVisibility(bVisible ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
}

bool SCompanionSettingsPanel::IsPanelVisible() const
{
	return bPanelVisible;
}

// --- test surface: these are what the real widgets call, not parallel implementations ----------
void SCompanionSettingsPanel::SimulateSelectVoice(const FString& Voice)       { Settings.TtsVoice = Voice; CommitChange(); }
void SCompanionSettingsPanel::SimulateSelectFaceProfile(const FString& P)     { Settings.FaceProfile = P; CommitChange(); }
void SCompanionSettingsPanel::SimulateSetMusicVolume(float Volume)            { Settings.MusicVolume = Volume; CommitChange(); }
void SCompanionSettingsPanel::SimulateToggleMusic()                           { Settings.bMusicEnabled = !Settings.bMusicEnabled; CommitChange(); }
void SCompanionSettingsPanel::SimulateToggleChat()                            { Settings.bChatVisible = !Settings.bChatVisible; CommitChange(); }
void SCompanionSettingsPanel::SimulateToggleBranding()                        { Settings.bBrandingVisible = !Settings.bBrandingVisible; CommitChange(); }
void SCompanionSettingsPanel::SimulateToggleSelfTest()                        { Settings.bFaceSelfTest = !Settings.bFaceSelfTest; CommitChange(); }
void SCompanionSettingsPanel::SimulateToggleDiagnostics()                     { Settings.bShowDiagnostics = !Settings.bShowDiagnostics; CommitChange(); }
void SCompanionSettingsPanel::SimulateSetLlmModel(const FString& Model)       { Settings.LlmModel = Model; CommitChange(); }
void SCompanionSettingsPanel::SimulateSetTtsModel(const FString& Model)       { Settings.TtsModel = Model; CommitChange(); }
void SCompanionSettingsPanel::SimulateSetSttModel(const FString& Model)       { Settings.SttModel = Model; CommitChange(); }
void SCompanionSettingsPanel::SimulateSetAvatar(const FString& AvatarId)      { Settings.AvatarId = AvatarId; CommitChange(); }

#undef LOCTEXT_NAMESPACE
