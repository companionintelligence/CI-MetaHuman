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
	});
}
