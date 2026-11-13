#pragma once

#include "CoreMinimal.h"
#include "ILiveLinkSource.h"

class ILiveLinkClient;

// The face driver's Live Link sink, for a MetaHuman face (the resident "Eliza", Phase M).
//
// WHY LIVE LINK AND NOT SetMorphTarget. A MetaHuman face is moved by RigLogic joints driven from
// CTRL_expressions_* curves; its morph targets are only the correctives RigLogic layers on top, so
// writing ARKit weights with SetMorphTarget would move the correctives and leave the face itself
// still. The engine already ships the bridge: ABP_MH_LiveLink reads a Live Link subject's ARKit
// curves and remaps them through PA_MetaHuman_ARKit_Mapping into the CTRL_expressions_* controls
// that ABP_Face_PostProcess hands to RigLogic. So the composed face frame is published as a Live
// Link subject and the MetaHuman's own anim blueprint does the rest.
//
// ROLE. The subject is published in the ANIMATION role with no bones and the curves as
// properties -- the shape Live Link Face (ARKit) subjects have -- because the Live Link Pose node in
// an anim blueprint evaluates the animation role, and an animation-role subject also evaluates as
// the basic role (ULiveLinkAnimationRole derives from ULiveLinkBasicRole). A basic-role subject
// could not be read by the pose node at all.
//
// EVERY CURVE, EVERY FRAME. A frame lists all of the profile's curves, zeros included, in the
// static data's order. Live Link holds the last value of a property it is not sent, so sending only
// the non-zero curves would latch a blink shut the first time it closed.
class CICOMPANION_API FCompanionLiveLinkFaceSource : public ILiveLinkSource
{
public:
	virtual ~FCompanionLiveLinkFaceSource() override = default;

	// Registers a new source with the engine's Live Link client (IModularFeatures), sets it to the
	// Latest evaluation mode, and publishes the subject's static data. nullptr when no Live Link
	// client exists in this process (the LiveLink plugin is disabled), which callers log and treat as
	// "face not driven" rather than crash.
	static TSharedPtr<FCompanionLiveLinkFaceSource> Create(FName InSubject, const TArray<FName>& InCurveNames);

	// Publishes one frame. Curves missing from Values are sent as 0.
	void PushFrame(const TMap<FName, float>& Values);

	// Reads the subject back through the client (any thread), for the specs and the bind log.
	// False when the subject cannot be evaluated.
	bool Evaluate(TMap<FName, float>& OutValues) const;

	// Removes the source from the client. Safe to call twice.
	void Shutdown();

	FName GetSubjectName() const { return SubjectName; }
	const TArray<FName>& GetCurveNames() const { return CurveNames; }
	bool IsRegistered() const { return Client != nullptr; }

	// ILiveLinkSource
	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;
	virtual bool IsSourceStillValid() const override { return Client != nullptr; }
	virtual bool RequestSourceShutdown() override;
	virtual FText GetSourceType() const override;
	virtual FText GetSourceMachineName() const override;
	virtual FText GetSourceStatus() const override;

private:
	void PushStaticData();

	ILiveLinkClient* Client = nullptr;
	FGuid SourceGuid;
	FName SubjectName;
	TArray<FName> CurveNames;
	uint64 FramesPushed = 0;
};
