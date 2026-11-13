#include "Avatar/CompanionLiveLinkFaceSource.h"
#include "CICompanionLog.h"

#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "LiveLinkSourceSettings.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Roles/LiveLinkBasicRole.h"
#include "Roles/LiveLinkBasicTypes.h"

#define LOCTEXT_NAMESPACE "CompanionLiveLinkFaceSource"

TSharedPtr<FCompanionLiveLinkFaceSource> FCompanionLiveLinkFaceSource::Create(FName InSubject, const TArray<FName>& InCurveNames)
{
	IModularFeatures& Features = IModularFeatures::Get();
	if (!Features.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		UE_LOG(LogCompanionFace, Warning,
			TEXT("Live Link sink: no Live Link client in this process (is the LiveLink plugin enabled?); subject '%s' not published."),
			*InSubject.ToString());
		return nullptr;
	}
	ILiveLinkClient& LiveLink = Features.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	TSharedPtr<FCompanionLiveLinkFaceSource> Source = MakeShared<FCompanionLiveLinkFaceSource>();
	Source->SubjectName = InSubject;
	Source->CurveNames = InCurveNames;
	const FGuid Guid = LiveLink.AddSource(Source);	// calls ReceiveClient
	if (!Guid.IsValid() || !Source->Client)
	{
		UE_LOG(LogCompanionFace, Warning, TEXT("Live Link sink: the client refused the source for '%s'."), *InSubject.ToString());
		return nullptr;
	}
	// LATEST, not the default interpolated/timecode modes: the frames are produced on this machine
	// at render rate, and buffering them for interpolation only adds a frame of mouth latency.
	if (ULiveLinkSourceSettings* Settings = LiveLink.GetSourceSettings(Guid))
	{
		Settings->Mode = ELiveLinkSourceMode::Latest;
	}
	Source->PushStaticData();
	return Source;
}

void FCompanionLiveLinkFaceSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
	Client = InClient;
	SourceGuid = InSourceGuid;
}

void FCompanionLiveLinkFaceSource::PushStaticData()
{
	if (!Client)
	{
		return;
	}
	FLiveLinkStaticDataStruct StaticData(FLiveLinkSkeletonStaticData::StaticStruct());
	FLiveLinkSkeletonStaticData& Skeleton = *StaticData.Cast<FLiveLinkSkeletonStaticData>();
	Skeleton.PropertyNames = CurveNames;	// no bones: a curves-only subject, as Live Link Face publishes
	Client->PushSubjectStaticData_AnyThread(FLiveLinkSubjectKey(SourceGuid, SubjectName),
		ULiveLinkAnimationRole::StaticClass(), MoveTemp(StaticData));
}

void FCompanionLiveLinkFaceSource::PushFrame(const TMap<FName, float>& Values)
{
	if (!Client)
	{
		return;
	}
	FLiveLinkFrameDataStruct FrameData(FLiveLinkAnimationFrameData::StaticStruct());
	FLiveLinkAnimationFrameData& Frame = *FrameData.Cast<FLiveLinkAnimationFrameData>();
	Frame.WorldTime = FLiveLinkWorldTime(FPlatformTime::Seconds());
	Frame.PropertyValues.SetNumZeroed(CurveNames.Num());
	for (int32 I = 0; I < CurveNames.Num(); ++I)
	{
		if (const float* V = Values.Find(CurveNames[I]))
		{
			Frame.PropertyValues[I] = *V;
		}
	}
	Client->PushSubjectFrameData_AnyThread(FLiveLinkSubjectKey(SourceGuid, SubjectName), MoveTemp(FrameData));
	++FramesPushed;
}

bool FCompanionLiveLinkFaceSource::Evaluate(TMap<FName, float>& OutValues) const
{
	OutValues.Reset();
	if (!Client)
	{
		return false;
	}
	FLiveLinkSubjectFrameData Out;
	if (!Client->EvaluateFrame_AnyThread(FLiveLinkSubjectName(SubjectName), ULiveLinkBasicRole::StaticClass(), Out))
	{
		return false;
	}
	const FLiveLinkBaseStaticData* Static = Out.StaticData.Cast<FLiveLinkBaseStaticData>();
	const FLiveLinkBaseFrameData* Frame = Out.FrameData.Cast<FLiveLinkBaseFrameData>();
	if (!Static || !Frame)
	{
		return false;
	}
	const int32 N = FMath::Min(Static->PropertyNames.Num(), Frame->PropertyValues.Num());
	for (int32 I = 0; I < N; ++I)
	{
		OutValues.Add(Static->PropertyNames[I], Frame->PropertyValues[I]);
	}
	return true;
}

void FCompanionLiveLinkFaceSource::Shutdown()
{
	if (Client && SourceGuid.IsValid())
	{
		ILiveLinkClient* Was = Client;
		Client = nullptr;
		Was->RemoveSource(SourceGuid);
	}
	Client = nullptr;
}

bool FCompanionLiveLinkFaceSource::RequestSourceShutdown()
{
	Client = nullptr;
	return true;
}

FText FCompanionLiveLinkFaceSource::GetSourceType() const
{
	return LOCTEXT("SourceType", "Companion face driver");
}

FText FCompanionLiveLinkFaceSource::GetSourceMachineName() const
{
	return FText::FromString(FPlatformProcess::ComputerName());
}

FText FCompanionLiveLinkFaceSource::GetSourceStatus() const
{
	return FText::Format(LOCTEXT("SourceStatus", "{0} curves, {1} frames"),
		FText::AsNumber(CurveNames.Num()), FText::AsNumber(FramesPushed));
}

#undef LOCTEXT_NAMESPACE
