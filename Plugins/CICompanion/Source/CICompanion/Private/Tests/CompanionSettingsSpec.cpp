#include "Misc/AutomationTest.h"
#include "UI/CompanionSettingsModel.h"
#include "UI/SCompanionSettingsPanel.h"

BEGIN_DEFINE_SPEC(FCompanionSettingsSpec, "MetaHumanLocalLLM.Backend.Settings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FCompanionSettingsSpec)

// The model is pure, and the panel's Simulate* surface drives the SAME code the real widgets call,
// so both run headlessly with no viewport.
void FCompanionSettingsSpec::Define()
{
	Describe("Validate", [this]()
	{
		It("accepts the defaults unchanged", [this]()
		{
			FCompanionSettingsModel M;
			TestTrue("defaults are valid", M.Validate());
		});

		It("clamps music volume into range", [this]()
		{
			FCompanionSettingsModel M; M.MusicVolume = 3.0f;
			TestFalse("reports correction", M.Validate());
			TestEqual("clamped to 1", M.MusicVolume, 1.0f);

			M.MusicVolume = -2.0f; M.Validate();
			TestEqual("clamped to 0", M.MusicVolume, 0.0f);
		});

		It("replaces a NaN volume rather than passing it to the audio component", [this]()
		{
			// A NaN reaching SetVolumeMultiplier is not recoverable, and a text box on the far end
			// of a data channel can deliver one from a malformed parse.
			FCompanionSettingsModel M; M.MusicVolume = FMath::Sqrt(-1.0f);
			TestFalse("reports correction", M.Validate());
			TestTrue("finite afterwards", FMath::IsFinite(M.MusicVolume));
		});

		It("keeps the chat log within useful bounds", [this]()
		{
			FCompanionSettingsModel M; M.ChatLogLines = 0;
			M.Validate();
			TestTrue("at least one line", M.ChatLogLines >= 1);

			M.ChatLogLines = 100000; M.Validate();
			TestTrue("bounded above", M.ChatLogLines <= 50);
		});

		It("rejects a face profile that does not exist", [this]()
		{
			// An unknown profile resolves zero curves and leaves the face frozen with no error,
			// which is indistinguishable from the rig being broken.
			FCompanionSettingsModel M; M.FaceProfile = TEXT("Nonsense");
			TestFalse("reports correction", M.Validate());
			TestEqual("falls back to CC", M.FaceProfile, FString(TEXT("CC")));
		});

		It("accepts face profiles case-insensitively", [this]()
		{
			FCompanionSettingsModel M; M.FaceProfile = TEXT("arkit");
			TestTrue("accepted", M.Validate());
			TestEqual("preserved", M.FaceProfile, FString(TEXT("arkit")));
		});

		It("ACCEPTS an unlisted voice, because the server may know voices this build does not", [this]()
		{
			FCompanionSettingsModel M; M.TtsVoice = TEXT("af_future");
			TestTrue("not rejected", M.Validate());
			TestEqual("kept verbatim", M.TtsVoice, FString(TEXT("af_future")));
		});

		It("refuses empty identifiers, which would silently disable what they name", [this]()
		{
			FCompanionSettingsModel M;
			M.AvatarId = TEXT("  "); M.TtsVoice = TEXT(""); M.LlmModel = TEXT("");
			TestFalse("reports correction", M.Validate());
			TestTrue("avatar restored", !M.AvatarId.TrimStartAndEnd().IsEmpty());
			TestTrue("voice restored", !M.TtsVoice.IsEmpty());
			TestTrue("llm restored", !M.LlmModel.IsEmpty());
		});
	});

	Describe("DiffAgainst", [this]()
	{
		It("reports nothing for identical models", [this]()
		{
			FCompanionSettingsModel A, B;
			TestTrue("clean", A.DiffAgainst(B) == ECompanionSettingDirty::None);
		});

		It("routes each change to the right subsystem", [this]()
		{
			// The whole reason Diff exists: rebinding the face or restarting audio on every edit
			// would hitch the stream, so callers need to know what ACTUALLY changed.
			FCompanionSettingsModel Base;

			FCompanionSettingsModel V = Base; V.TtsVoice = TEXT("bm_george");
			TestTrue("voice dirty", EnumHasAnyFlags(V.DiffAgainst(Base), ECompanionSettingDirty::Voice));
			TestFalse("avatar clean", EnumHasAnyFlags(V.DiffAgainst(Base), ECompanionSettingDirty::Avatar));

			FCompanionSettingsModel A = Base; A.FaceProfile = TEXT("ARKit");
			TestTrue("avatar dirty", EnumHasAnyFlags(A.DiffAgainst(Base), ECompanionSettingDirty::Avatar));

			FCompanionSettingsModel Mu = Base; Mu.MusicVolume = 0.1f;
			TestTrue("audio dirty", EnumHasAnyFlags(Mu.DiffAgainst(Base), ECompanionSettingDirty::Audio));
			TestFalse("voice clean", EnumHasAnyFlags(Mu.DiffAgainst(Base), ECompanionSettingDirty::Voice));

			FCompanionSettingsModel C = Base; C.bChatVisible = !Base.bChatVisible;
			TestTrue("chat dirty", EnumHasAnyFlags(C.DiffAgainst(Base), ECompanionSettingDirty::Chat));

			FCompanionSettingsModel Br = Base; Br.bBrandingVisible = !Base.bBrandingVisible;
			TestTrue("branding dirty", EnumHasAnyFlags(Br.DiffAgainst(Base), ECompanionSettingDirty::Branding));

			FCompanionSettingsModel D = Base; D.bFaceSelfTest = !Base.bFaceSelfTest;
			TestTrue("debug dirty", EnumHasAnyFlags(D.DiffAgainst(Base), ECompanionSettingDirty::Debug));
		});

		It("can report several dirty subsystems at once", [this]()
		{
			FCompanionSettingsModel Base, Both;
			Both.TtsVoice = TEXT("bf_emma");
			Both.MusicVolume = 0.2f;
			const ECompanionSettingDirty D = Both.DiffAgainst(Base);
			TestTrue("voice", EnumHasAnyFlags(D, ECompanionSettingDirty::Voice));
			TestTrue("audio", EnumHasAnyFlags(D, ECompanionSettingDirty::Audio));
		});
	});

	Describe("ToCompactString", [this]()
	{
		It("carries the values an operator needs to confirm what is live", [this]()
		{
			FCompanionSettingsModel M;
			M.TtsVoice = TEXT("bm_lewis"); M.AvatarId = TEXT("ada");
			const FString S = M.ToCompactString();
			TestTrue("has voice", S.Contains(TEXT("bm_lewis")));
			TestTrue("has avatar", S.Contains(TEXT("ada")));
			TestTrue("has music", S.Contains(TEXT("music=")));
		});
	});

	Describe("panel notification", [this]()
	{
		It("notifies once per real change", [this]()
		{
			int32 Seen = 0;
			FCompanionSettingsModel Last;
			TSharedRef<SCompanionSettingsPanel> P = SNew(SCompanionSettingsPanel)
				.OnSettingsChanged(FOnCompanionSettingsChanged::CreateLambda(
					[&Seen, &Last](const FCompanionSettingsModel& M) { ++Seen; Last = M; }));

			P->SimulateSelectVoice(TEXT("bf_emma"));
			TestEqual("one notification", Seen, 1);
			TestEqual("carried the value", Last.TtsVoice, FString(TEXT("bf_emma")));
		});

		It("does NOT notify when a set changes nothing", [this]()
		{
			// Sliders fire continuously while dragged. Re-pushing an identical value would rebind
			// audio every frame of the drag.
			int32 Seen = 0;
			TSharedRef<SCompanionSettingsPanel> P = SNew(SCompanionSettingsPanel)
				.OnSettingsChanged(FOnCompanionSettingsChanged::CreateLambda(
					[&Seen](const FCompanionSettingsModel&) { ++Seen; }));

			const FString Current = P->GetSettings().TtsVoice;
			P->SimulateSelectVoice(Current);
			TestEqual("silent", Seen, 0);
		});

		It("does not notify when an invalid value clamps back to the current one", [this]()
		{
			int32 Seen = 0;
			TSharedRef<SCompanionSettingsPanel> P = SNew(SCompanionSettingsPanel)
				.OnSettingsChanged(FOnCompanionSettingsChanged::CreateLambda(
					[&Seen](const FCompanionSettingsModel&) { ++Seen; }));

			P->SimulateSetMusicVolume(1.0f);      // a real change
			const int32 AfterReal = Seen;
			P->SimulateSetMusicVolume(4.0f);      // clamps to 1.0 == unchanged
			TestEqual("no extra notification", Seen, AfterReal);
			TestEqual("still clamped", P->GetSettings().MusicVolume, 1.0f);
		});

		It("toggles are their own inverse", [this]()
		{
			TSharedRef<SCompanionSettingsPanel> P = SNew(SCompanionSettingsPanel);
			const bool Before = P->GetSettings().bMusicEnabled;
			P->SimulateToggleMusic();
			TestNotEqual("flipped", P->GetSettings().bMusicEnabled, Before);
			P->SimulateToggleMusic();
			TestEqual("restored", P->GetSettings().bMusicEnabled, Before);
			TestEqual("two changes", P->GetChangeCount(), 2);
		});

		It("sanitises a bad profile typed into the panel", [this]()
		{
			TSharedRef<SCompanionSettingsPanel> P = SNew(SCompanionSettingsPanel);
			P->SimulateSelectFaceProfile(TEXT("Bogus"));
			TestEqual("fell back", P->GetSettings().FaceProfile, FString(TEXT("CC")));
		});

		It("starts hidden so the opening shot is not a settings screen", [this]()
		{
			TSharedRef<SCompanionSettingsPanel> P = SNew(SCompanionSettingsPanel);
			TestFalse("hidden", P->IsPanelVisible());
			P->SetVisible(true);
			TestTrue("shown", P->IsPanelVisible());
		});

		It("SetSettingsQuiet does not re-broadcast", [this]()
		{
			// Applying settings back into the panel (e.g. from a registry) must not loop into
			// another change notification.
			int32 Seen = 0;
			TSharedRef<SCompanionSettingsPanel> P = SNew(SCompanionSettingsPanel)
				.OnSettingsChanged(FOnCompanionSettingsChanged::CreateLambda(
					[&Seen](const FCompanionSettingsModel&) { ++Seen; }));

			FCompanionSettingsModel M; M.TtsVoice = TEXT("am_adam");
			P->SetSettingsQuiet(M);
			TestEqual("silent", Seen, 0);
			TestEqual("applied", P->GetSettings().TtsVoice, FString(TEXT("am_adam")));
		});
	});
}
