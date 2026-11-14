#include "Misc/AutomationTest.h"

#include "Avatar/CompanionLiveLinkFaceSource.h"
#include "Avatar/FaceProfile.h"
#include "Containers/Ticker.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"

BEGIN_DEFINE_SPEC(FLiveLinkFaceSourceSpec, "MetaHumanLocalLLM.Backend.LiveLinkFaceSource", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TSharedPtr<FCompanionLiveLinkFaceSource> Source;
	FTSTicker::FDelegateHandle Tick;
	int32 Frames = 0;
END_DEFINE_SPEC(FLiveLinkFaceSourceSpec)

// The MetaHuman resident's face is driven by publishing the composed frame as a Live Link subject
// (Eliza, Phase M). This proves the publish half end to end through the real Live Link client: push
// the ARKitMH curve set, let the client tick, and read the same values back as a subject frame. The
// other half -- ABP_MH_LiveLink remapping them onto RigLogic -- needs her assembled face and is
// checked in the cooked build by the FACESURVIVE log line.
void FLiveLinkFaceSourceSpec::Define()
{
	AfterEach([this]()
	{
		if (Tick.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(Tick); Tick.Reset(); }
		if (Source.IsValid()) { Source->Shutdown(); Source.Reset(); }
		Frames = 0;
	});

	LatentIt("publishes every profile curve and reads the frame back", EAsyncExecution::TaskGraphMainThread,
		FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
		{
			AddInfo(TEXT("No Live Link client in this process (LiveLink plugin disabled); skipping."));
			Done.Execute();
			return;
		}
		const TArray<FName> Curves = FFaceProfile::ARKitMH().AllCurves();
		Source = FCompanionLiveLinkFaceSource::Create(TEXT("CompanionFaceSpecSubject"), Curves);
		if (!TestTrue("source registered", Source.IsValid() && Source->IsRegistered()))
		{
			Done.Execute();
			return;
		}
		TMap<FName, float> Frame;
		Frame.Add(TEXT("jawOpen"), 0.6f);
		Frame.Add(TEXT("eyeBlinkLeft"), 1.0f);
		Source->PushFrame(Frame);
		Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, Done, Curves, Frame](float)
		{
			// Live Link applies pushed frames on its own tick; give it a few.
			if (++Frames < 4)
			{
				Source->PushFrame(Frame);
				return true;
			}
			TMap<FName, float> Back;
			if (TestTrue("subject evaluates", Source->Evaluate(Back)))
			{
				TestEqual("every curve published", Back.Num(), Curves.Num());
				TestEqual("jawOpen round-trips", Back.FindRef(TEXT("jawOpen")), 0.6f, 1e-4f);
				TestEqual("blink round-trips", Back.FindRef(TEXT("eyeBlinkLeft")), 1.0f, 1e-4f);
				TestEqual("unsent curves are published as zero", Back.FindRef(TEXT("mouthPucker")), 0.0f, 1e-4f);
				TestFalse("mouthClose is not part of ARKitMH", Back.Contains(TEXT("mouthClose")));
			}
			Done.Execute();
			return false;
		}));
	});
}
