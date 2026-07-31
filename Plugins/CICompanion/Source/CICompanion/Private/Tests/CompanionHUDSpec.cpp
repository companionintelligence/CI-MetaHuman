#include "Misc/AutomationTest.h"
#include "UI/SCompanionHUD.h"

BEGIN_DEFINE_SPEC(FCompanionHUDSpec, "MetaHumanLocalLLM.Frontend.CompanionHUD", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TSharedPtr<SCompanionHUD> HUD;
	FString LastSubmittedText;
	bool bLastMicState = false;
	int32 SubmitCount = 0;
	int32 MicToggleCount = 0;
END_DEFINE_SPEC(FCompanionHUDSpec)

// Drives the native Slate HUD via its Simulate* test hooks rather than real
// mouse/keyboard input, so this runs headlessly (-nullrhi, no window needed).
void FCompanionHUDSpec::Define()
{
	BeforeEach([this]()
	{
		LastSubmittedText.Empty();
		bLastMicState = false;
		SubmitCount = 0;
		MicToggleCount = 0;

		SAssignNew(HUD, SCompanionHUD)
			.OnTextSubmitted(FOnCompanionTextSubmitted::CreateLambda([this](const FString& Text)
			{
				LastSubmittedText = Text;
				++SubmitCount;
			}))
			.OnMicToggled(FOnCompanionMicToggled::CreateLambda([this](bool bListening)
			{
				bLastMicState = bListening;
				++MicToggleCount;
			}));
	});

	AfterEach([this]()
	{
		HUD.Reset();
	});

	It("fires OnTextSubmitted with the typed text when Enter is simulated", [this]()
	{
		HUD->SimulateSubmitText(TEXT("Hello there"));

		TestEqual("submit fired once", SubmitCount, 1);
		TestEqual("submitted text", LastSubmittedText, TEXT("Hello there"));
	});

	It("ignores an empty submission", [this]()
	{
		HUD->SimulateSubmitText(TEXT(""));

		TestEqual("submit not fired", SubmitCount, 0);
	});

	It("toggles mic state and fires OnMicToggled on each click", [this]()
	{
		TestFalse("starts not listening", HUD->IsMicListening());

		HUD->SimulateMicToggleClick();
		TestTrue("now listening", HUD->IsMicListening());
		TestEqual("toggle fired once", MicToggleCount, 1);
		TestTrue("callback received true", bLastMicState);

		HUD->SimulateMicToggleClick();
		TestFalse("now not listening", HUD->IsMicListening());
		TestEqual("toggle fired twice", MicToggleCount, 2);
		TestFalse("callback received false", bLastMicState);
	});

	It("allows the host to roll back a mic toggle when capture cannot start", [this]()
	{
		HUD->SimulateMicToggleClick();
		TestTrue("optimistically listening", HUD->IsMicListening());

		HUD->SetMicListening(false);
		TestFalse("rolled back", HUD->IsMicListening());
	});

	It("appends chat entries and tracks them", [this]()
	{
		HUD->AppendChatEntry(TEXT("You"), TEXT("Hi"));
		HUD->AppendChatEntry(TEXT("Assistant"), TEXT("Hello!"));

		TestEqual("two entries", HUD->NumChatEntries(), 2);
		TestEqual("first speaker", HUD->GetChatEntries()[0].Speaker, TEXT("You"));
		TestEqual("second text", HUD->GetChatEntries()[1].Text, TEXT("Hello!"));
	});

	It("ClearChatLog empties the tracked entries", [this]()
	{
		HUD->AppendChatEntry(TEXT("You"), TEXT("Hi"));
		HUD->ClearChatLog();

		TestEqual("no entries after clear", HUD->NumChatEntries(), 0);
		TestEqual("no rows after clear", HUD->NumVisibleChatEntries(), 0);
	});

	// The regression these three cover: a kiosk session that runs for hours grew the log without
	// limit until it covered the companion. Overshooting the cap deliberately, because the real
	// failure was gradual accumulation rather than one oversized burst.
	It("stops growing once the chat log reaches the cap", [this]()
	{
		const int32 Cap = SCompanionHUD::MaxRetainedChatEntries;

		for (int32 Index = 0; Index < Cap * 2; ++Index)
		{
			HUD->AppendChatEntry(TEXT("Assistant"), FString::Printf(TEXT("Line %d"), Index));
		}

		TestEqual("tracked entries held at the cap", HUD->NumChatEntries(), Cap);
		TestEqual("scroll box rows held at the cap", HUD->NumVisibleChatEntries(), Cap);
	});

	It("drops the oldest entries and keeps the newest when over the cap", [this]()
	{
		const int32 Cap = SCompanionHUD::MaxRetainedChatEntries;
		const int32 Overflow = 7;

		for (int32 Index = 0; Index < Cap + Overflow; ++Index)
		{
			HUD->AppendChatEntry(TEXT("Assistant"), FString::Printf(TEXT("Line %d"), Index));
		}

		const TArray<FCompanionChatEntry>& Entries = HUD->GetChatEntries();
		TestEqual("kept exactly the cap", Entries.Num(), Cap);
		TestEqual("oldest survivor is the first line past the overflow", Entries[0].Text, FString::Printf(TEXT("Line %d"), Overflow));
		TestEqual("newest line survives", Entries.Last().Text, FString::Printf(TEXT("Line %d"), Cap + Overflow - 1));
	});

	It("keeps accepting entries after a clear that followed an overflow", [this]()
	{
		const int32 Cap = SCompanionHUD::MaxRetainedChatEntries;

		for (int32 Index = 0; Index < Cap + 10; ++Index)
		{
			HUD->AppendChatEntry(TEXT("Assistant"), FString::Printf(TEXT("Line %d"), Index));
		}
		HUD->ClearChatLog();
		HUD->AppendChatEntry(TEXT("You"), TEXT("Still here"));

		TestEqual("one entry after clear and refill", HUD->NumChatEntries(), 1);
		TestEqual("one row after clear and refill", HUD->NumVisibleChatEntries(), 1);
		TestEqual("and it is the new one", HUD->GetChatEntries()[0].Text, TEXT("Still here"));
	});
}
