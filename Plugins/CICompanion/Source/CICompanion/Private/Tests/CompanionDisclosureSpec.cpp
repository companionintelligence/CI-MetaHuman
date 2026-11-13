#include "Misc/AutomationTest.h"

#include "CICompanionEnv.h"
#include "Avatar/CompanionRuntimeSubsystem.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Settings/MetaHumanLLMSettings.h"
#include "UI/SCompanionAiNotice.h"

// Two launch conditions of the streamed companion's privacy policy, held in code.
//
// She says she is an AI, in the frame and where people type, from the first frame a viewer gets
// (EU AI Act Art. 50(1)). And conversation memory stays off unless an operator turns it on: with it
// on she remembers people across sessions, which New York GBL Art. 47 and California SB 243 tie to a
// crisis protocol this product does not have yet, and the policy says memory is off.
BEGIN_DEFINE_SPEC(FCompanionAiNoticeSpec, "MetaHumanLocalLLM.Frontend.AiNotice", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FCompanionAiNoticeSpec)

void FCompanionAiNoticeSpec::Define()
{
	It("says she is an AI and that her replies can be wrong", [this]()
	{
		const TSharedRef<SCompanionAiNotice> Notice = SNew(SCompanionAiNotice);
		const FString Shown = Notice->GetDisplayedText();
		TestEqual("shows the notice", Shown, FString(SCompanionAiNotice::NoticeText));
		TestTrue("names her an AI first", Shown.StartsWith(TEXT("Eliza is an AI.")));
		TestTrue("says her replies can be wrong", Shown.Contains(TEXT("can be wrong")));
	});

	It("never takes a click meant for the scene or the chat box", [this]()
	{
		const TSharedRef<SCompanionAiNotice> Notice = SNew(SCompanionAiNotice);
		TestTrue("hit-test invisible, children included", Notice->GetVisibility() == EVisibility::HitTestInvisible);
	});

	It("names her an AI in the chat box's idle status", [this]()
	{
		TestTrue("the prompt says so", FString(SCompanionAiNotice::ChatPrompt).Contains(TEXT("Eliza (an AI)")));
	});

	It("uses the player page's words", [this]()
	{
		const FString Script = FPaths::Combine(FPaths::ProjectDir(), TEXT("Deployment/patch_pixelstreaming_player.py"));
		FString Source;
		if (!FFileHelper::LoadFileToString(Source, *Script))
		{
			AddInfo(FString::Printf(TEXT("%s is not here; checks.yml compares the two instead."), *Script));
			return;
		}
		TestTrue("the page and the frame say the same thing",
			Source.Contains(FString::Printf(TEXT("AI_NOTICE_TEXT = \"%s\""), SCompanionAiNotice::NoticeText)));
	});
}

BEGIN_DEFINE_SPEC(FCompanionDisclosureSpec, "MetaHumanLocalLLM.Backend.Disclosure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	UWorld* World = nullptr;
END_DEFINE_SPEC(FCompanionDisclosureSpec)

void FCompanionDisclosureSpec::Define()
{
	BeforeEach([this]()
	{
		World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	});

	AfterEach([this]()
	{
		if (World)
		{
			World->DestroyWorld(/*bInformEngineOfWorld*/ false);
			World = nullptr;
		}
	});

	Describe("the AI notice in the frame", [this]()
	{
		// Skips rather than mutating the process: MHLLM_COMPANION=0 means no companion and nothing to
		// disclose, and with MHLLM_MEMORY=1 a reset overwrites the developer's saved transcript.
		auto Unsuitable = [this]() -> bool
		{
			if (CICompanionEnv::IsDisabled(TEXT("MHLLM_COMPANION")))
			{
				AddInfo(TEXT("MHLLM_COMPANION=0 in this process, so no companion is spawned; skipping."));
				return true;
			}
			if (GetDefault<UMetaHumanLLMSettings>()->bPersistConversation)
			{
				AddInfo(TEXT("Conversation memory is on (MHLLM_MEMORY=1), and a reset would overwrite the saved transcript; skipping."));
				return true;
			}
			return false;
		};

		It("is built with the companion, even where no HUD can be", [this, Unsuitable]()
		{
			if (Unsuitable()) { return; }

			UCompanionRuntimeSubsystem* Runtime = World->GetSubsystem<UCompanionRuntimeSubsystem>();
			if (!TestNotNull("the world has a runtime subsystem", Runtime)) { return; }
			TestEqual("nothing before the companion", Runtime->GetAiNoticeText(), FString());

			// A headless editor has no game viewport, so CreateHud gives up here; the notice must not.
			Runtime->OnWorldBeginPlay(*World);
			TestNotNull("a companion was spawned", Runtime->GetCompanion());
			TestEqual("the notice is there", Runtime->GetAiNoticeText(), FString(SCompanionAiNotice::NoticeText));
		});

		It("stays for the next viewer", [this, Unsuitable]()
		{
			if (Unsuitable()) { return; }

			UCompanionRuntimeSubsystem* Runtime = World->GetSubsystem<UCompanionRuntimeSubsystem>();
			if (!TestNotNull("the world has a runtime subsystem", Runtime)) { return; }
			Runtime->OnWorldBeginPlay(*World);
			Runtime->ResetForNextViewer();
			TestEqual("still there after the reset", Runtime->GetAiNoticeText(), FString(SCompanionAiNotice::NoticeText));
		});
	});

	Describe("conversation memory", [this]()
	{
		It("is off unless the operator turns it on", [this]()
		{
			if (!CICompanionEnv::Get(TEXT("MHLLM_MEMORY"), FString()).IsEmpty())
			{
				AddInfo(TEXT("MHLLM_MEMORY is set in this process, and it wins by design; skipping."));
				return;
			}
			// The class default as compiled and as DefaultGame.ini leaves it.
			TestFalse("off by default", GetDefault<UMetaHumanLLMSettings>()->bPersistConversation);
		});
	});
}
