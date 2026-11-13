#include "Avatar/CompanionFaceSubsystem.h"
#include "CICompanionEnv.h"
#include "CICompanionLog.h"

#include "Avatar/CompanionFaceComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/CommandLine.h"
#include "Animation/AnimInstance.h"
#include "Avatar/CompanionLiveLinkFaceSource.h"
#include "UObject/UnrealType.h"


const FName UCompanionFaceSubsystem::ResidentTag(TEXT("CompanionFace"));
const FString UCompanionFaceSubsystem::ProfileTagPrefix(TEXT("FaceProfile="));
const FString UCompanionFaceSubsystem::GainTagPrefix(TEXT("FaceGain="));
const FString UCompanionFaceSubsystem::PoseCapTagPrefix(TEXT("FacePoseCap="));
const FString UCompanionFaceSubsystem::SinkTagPrefix(TEXT("FaceSink="));
const FString UCompanionFaceSubsystem::SubjectTagPrefix(TEXT("LiveLinkSubject="));
const FName UCompanionFaceSubsystem::LeaderPoseTag(TEXT("LeaderPose=Body"));
const TCHAR* UCompanionFaceSubsystem::LiveLinkFaceAnimClassPath = TEXT("/MetaHumanCharacter/Animation/ABP_MH_LiveLink.ABP_MH_LiveLink_C");
const TCHAR* UCompanionFaceSubsystem::ArkitMappingPoseAssetPath = TEXT("/MetaHumanCharacter/Face/ARKit/PA_MetaHuman_ARKit_Mapping.PA_MetaHuman_ARKit_Mapping");

FString UCompanionFaceSubsystem::ResolveStringTag(const AActor* Resident, const FString& Prefix)
{
	if (Resident)
	{
		for (const FName& Tag : Resident->Tags)
		{
			const FString TagString = Tag.ToString();
			if (TagString.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				return TagString.Mid(Prefix.Len()).TrimStartAndEnd();
			}
		}
	}
	return FString();
}

namespace
{
	// Sets a Name/String/Text property, or a bool, on an object by reflection. The MetaHuman
	// blueprint's Live Link variables are blueprint members, which C++ cannot name at compile time.
	bool SetReflected(UObject* Obj, const TCHAR* PropName, const FString& Value, bool bBool, bool bBoolValue)
	{
		if (!Obj)
		{
			return false;
		}
		FProperty* Prop = Obj->GetClass()->FindPropertyByName(FName(PropName));
		if (!Prop)
		{
			return false;
		}
		void* Ptr = Prop->ContainerPtrToValuePtr<void>(Obj);
		if (bBool)
		{
			if (FBoolProperty* B = CastField<FBoolProperty>(Prop)) { B->SetPropertyValue(Ptr, bBoolValue); return true; }
			return false;
		}
		if (FNameProperty* N = CastField<FNameProperty>(Prop)) { N->SetPropertyValue(Ptr, FName(*Value)); return true; }
		if (FStrProperty* S = CastField<FStrProperty>(Prop)) { S->SetPropertyValue(Ptr, Value); return true; }
		if (FTextProperty* T = CastField<FTextProperty>(Prop)) { T->SetPropertyValue(Ptr, FText::FromString(Value)); return true; }
		// FLiveLinkSubjectName is a struct wrapping one FName.
		if (FStructProperty* St = CastField<FStructProperty>(Prop))
		{
			if (FNameProperty* Inner = CastField<FNameProperty>(St->Struct->FindPropertyByName(TEXT("Name"))))
			{
				Inner->SetPropertyValue(Inner->ContainerPtrToValuePtr<void>(Ptr), FName(*Value));
				return true;
			}
		}
		return false;
	}

	USkeletalMeshComponent* FindNamedMesh(AActor& Actor, const TCHAR* Name)
	{
		TArray<USkeletalMeshComponent*> Meshes;
		Actor.GetComponents(Meshes);
		for (USkeletalMeshComponent* M : Meshes)
		{
			if (M && M->GetName().Equals(Name, ESearchCase::IgnoreCase)) { return M; }
		}
		return nullptr;
	}
}

FString UCompanionFaceSubsystem::BindLiveLinkFace(AActor& Resident)
{
	if (!ResolveStringTag(&Resident, SinkTagPrefix).Equals(TEXT("LiveLink"), ESearchCase::IgnoreCase))
	{
		return FString();
	}
	FString Subject = ResolveStringTag(&Resident, SubjectTagPrefix);
	if (Subject.IsEmpty()) { Subject = Resident.GetName(); }

	USkeletalMeshComponent* FaceMesh = FindNamedMesh(Resident, TEXT("Face"));
	USkeletalMeshComponent* Body = FindNamedMesh(Resident, TEXT("Body"));
	if (!FaceMesh)
	{
		UE_LOG(LogCompanionFace, Warning, TEXT("Resident '%s' asks for FaceSink=LiveLink but has no 'Face' component."), *Resident.GetName());
		return TEXT("LiveLink requested, no Face component");
	}

	// The face must run ABP_MH_LiveLink. Set here rather than trusted from the map, because a
	// MetaHuman blueprint's construction script may reset the face's anim class.
	if (UClass* LiveLinkAnim = LoadClass<UAnimInstance>(nullptr, LiveLinkFaceAnimClassPath))
	{
		if (FaceMesh->GetAnimClass() != LiveLinkAnim)
		{
			FaceMesh->SetAnimInstanceClass(LiveLinkAnim);
		}
	}
	else
	{
		UE_LOG(LogCompanionFace, Warning, TEXT("Live Link sink: %s did not load (is it cooked? see /Game/Eliza/PAL_Eliza)."), LiveLinkFaceAnimClassPath);
	}

	// Subject and head-rotation switch, on whichever of the blueprint or the face's anim instance
	// declares them. Head rotation stays off: the body's idle owns the head.
	TArray<FString> Set;
	TArray<UObject*> Targets;
	Targets.Add(&Resident);
	Targets.Add(FaceMesh->GetAnimInstance());
	for (UObject* Obj : Targets)
	{
		if (!Obj) { continue; }
		if (SetReflected(Obj, TEXT("LLink_Face_Subj"), Subject, false, false)) { Set.Add(Obj->GetClass()->GetName() + TEXT(".LLink_Face_Subj")); }
		if (SetReflected(Obj, TEXT("LLink_Face_Head"), FString(), true, false)) { Set.Add(Obj->GetClass()->GetName() + TEXT(".LLink_Face_Head")); }
	}

	// Garments follow the body pose rather than carrying their own animation.
	int32 Garments = 0;
	int32 Led = 0;
	TArray<AActor*> Attached;
	Resident.GetAttachedActors(Attached, true, true);
	for (AActor* G : Attached)
	{
		if (!G || !G->ActorHasTag(LeaderPoseTag))
		{
			continue;
		}
		++Garments;
		TArray<USkeletalMeshComponent*> Meshes;
		G->GetComponents(Meshes);
		for (USkeletalMeshComponent* M : Meshes)
		{
			if (M && Body) { M->SetLeaderPoseComponent(Body); ++Led; }
		}
	}

	TSharedPtr<FCompanionLiveLinkFaceSource> Source = FCompanionLiveLinkFaceSource::Create(FName(*Subject), Face->GetProfileCurves());
	Face->SetLiveLinkSink(Source, FaceMesh, ArkitMappingPoseAssetPath);
	return FString::Printf(TEXT("LiveLink '%s' -> %s; set %s; garments %d (%d meshes led by Body)"),
		*Subject, *Face->GetLiveLinkPoseAssetName(), Set.Num() ? *FString::Join(Set, TEXT(", ")) : TEXT("no LLink variables"),
		Garments, Led);
}


bool UCompanionFaceSubsystem::ResolveFloatTag(const AActor* Resident, const FString& Prefix, float& OutValue)
{
	if (!Resident)
	{
		return false;
	}
	for (const FName& Tag : Resident->Tags)
	{
		const FString TagString = Tag.ToString();
		if (!TagString.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			continue;
		}
		const FString Value = TagString.Mid(Prefix.Len()).TrimStartAndEnd();
		if (Value.IsEmpty() || !Value.IsNumeric())
		{
			UE_LOG(LogCompanionFace, Warning, TEXT("Resident '%s': tag '%s' is not a number; ignored."),
				*Resident->GetName(), *TagString);
			return false;
		}
		const float Parsed = FCString::Atof(*Value);
		if (Parsed <= 0.0f)
		{
			UE_LOG(LogCompanionFace, Warning, TEXT("Resident '%s': tag '%s' is not positive; ignored."),
				*Resident->GetName(), *TagString);
			return false;
		}
		OutValue = Parsed;
		return true;
	}
	return false;
}

FString UCompanionFaceSubsystem::ApplyTuningTags(const AActor* Resident, UCompanionFaceComponent* InFace)
{
	if (!InFace)
	{
		return FString();
	}
	auto One = [Resident, InFace](const TCHAR* EnvName, const FString& Prefix, bool bGain) -> FString
	{
		if (!FPlatformMisc::GetEnvironmentVariable(EnvName).IsEmpty())
		{
			return FString::Printf(TEXT("%s"), EnvName);	// the constructor already read it; env wins
		}
		float Value = 0.0f;
		if (ResolveFloatTag(Resident, Prefix, Value))
		{
			if (bGain) { InFace->SetFaceGain(Value); } else { InFace->SetVisemePoseCap(Value); }
			return FString::Printf(TEXT("tag %s%g"), *Prefix, Value);
		}
		return TEXT("default");
	};
	const FString GainSource = One(TEXT("MHLLM_FACE_GAIN"), GainTagPrefix, true);
	const FString CapSource = One(TEXT("MHLLM_FACE_POSE_CAP"), PoseCapTagPrefix, false);
	return FString::Printf(TEXT("gain %.2f (%s), pose cap %.2f (%s)"),
		InFace->GetFaceGain(), *GainSource, InFace->GetVisemePoseCap(), *CapSource);
}


FString UCompanionFaceSubsystem::ResolveProfileName(const AActor* Resident)
{
	const FString FromEnv = CICompanionEnv::Get(TEXT("MHLLM_FACE_PROFILE"), FString());
	if (!FromEnv.IsEmpty())
	{
		return FromEnv;
	}

	if (Resident)
	{
		for (const FName& Tag : Resident->Tags)
		{
			const FString TagString = Tag.ToString();
			if (TagString.StartsWith(ProfileTagPrefix, ESearchCase::IgnoreCase))
			{
				const FString Name = TagString.Mid(ProfileTagPrefix.Len()).TrimStartAndEnd();
				if (!Name.IsEmpty())
				{
					return Name;
				}
			}
		}
	}

	return TEXT("CC");
}

bool UCompanionFaceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game and PIE worlds only — an editor preview or an inspector world has no business
	// getting a driven face.
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

AActor* UCompanionFaceSubsystem::FindResident(UWorld& InWorld) const
{
	AActor* BestFallback = nullptr;
	int32 BestMorphCount = 0;

	for (TActorIterator<AActor> It(&InWorld); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		if (Actor->ActorHasTag(ResidentTag))
		{
			return Actor;
		}

		// Fallback so a map authored before the tag existed still animates: pick the skeletal
		// mesh with the most morph targets. Props have none, so this resolves to the character.
		if (const USkeletalMeshComponent* Mesh = Actor->FindComponentByClass<USkeletalMeshComponent>())
		{
			if (const USkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset())
			{
				const int32 MorphCount = Asset->GetMorphTargets().Num();
				if (MorphCount > BestMorphCount)
				{
					BestMorphCount = MorphCount;
					BestFallback = Actor;
				}
			}
		}
	}

	if (BestFallback)
	{
		UE_LOG(LogCompanionFace, Warning,
			TEXT("No actor tagged '%s'; falling back to '%s' (%d morph targets)"),
			*ResidentTag.ToString(), *BestFallback->GetName(), BestMorphCount);
	}
	return BestFallback;
}

void UCompanionFaceSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	AActor* Resident = FindResident(InWorld);
	if (!Resident)
	{
		UE_LOG(LogCompanionFace, Warning, TEXT("No resident found; face driver inactive."));
		return;
	}

	USkeletalMeshComponent* Mesh = Resident->FindComponentByClass<USkeletalMeshComponent>();
	if (!Mesh)
	{
		UE_LOG(LogCompanionFace, Warning, TEXT("Resident '%s' has no skeletal mesh."), *Resident->GetName());
		return;
	}

	Face = NewObject<UCompanionFaceComponent>(Resident, TEXT("CompanionFace"));
	Face->RegisterComponent();
	// Env first, then the rig the map declares on the resident, then CC. A Nia map resolves to CC
	// exactly as before; the MetaHuman resident declares ARKit, which a hard-coded CC default would
	// have overridden with a profile that shares no curve names with its face.
	Face->SetProfile(FFaceProfile::ByName(ResolveProfileName(Resident)));
	Face->SetTargetMesh(Mesh);
	UE_LOG(LogCompanionFace, Log, TEXT("Face tuning for '%s': %s"), *Resident->GetName(),
		*ApplyTuningTags(Resident, Face));

	// TICK AFTER THE MESH'S ANIMATION, or none of this is visible.
	//
	// UCompanionFaceComponent writes weights with USkeletalMeshComponent::SetMorphTarget from its
	// TickComponent. Without an ordering constraint both components tick in the same group in
	// registration order, so the face driver generally runs BEFORE the mesh evaluates its own
	// animation -- and that evaluation rebuilds the component's curve/morph state from the anim
	// graph, discarding whatever was set beforehand. The resident plays MF_Idle through a single
	// node instance, so this happens every single frame.
	//
	// The failure is completely silent and reads as a dead rig: SetMorphTarget on a missing name
	// is a no-op, so the existing "42/42 curves resolved" log looks identical whether the writes
	// survive or are thrown away a millisecond later. Measured before this line existed: driving
	// all 15 visemes through the self-test moved the mouth region no more than a CONTROL patch of
	// cheek (2.25 vs 2.05 mean frame delta) -- i.e. the only thing moving was the whole head, under
	// the very idle animation that was clobbering the morphs.
	//
	// AddTickPrerequisiteComponent is the narrow fix: it constrains order without moving either
	// component to another tick group, so physics and rendering see the same schedule they did.
	Face->AddTickPrerequisiteComponent(Mesh);

	const FString LiveLink = BindLiveLinkFace(*Resident);
	if (LiveLink.IsEmpty())
	{
		UE_LOG(LogCompanionFace, Log, TEXT("Face driver bound to '%s': %d/%d profile curves resolved."),
			*Resident->GetName(), Face->GetResolvedCurveCount(), Face->GetProfileCurveCount());
	}
	else
	{
		UE_LOG(LogCompanionFace, Log, TEXT("Face driver bound to '%s': %d/%d profile curves resolved (%s)"),
			*Resident->GetName(), Face->GetResolvedCurveCount(), Face->GetProfileCurveCount(), *LiveLink);
	}

	// Opt-in, because it visibly overrides the face: MHLLM_FACE_SELFTEST=1 (or -FaceSelfTest)
	// cycles every viseme so a captured frame proves the path works in the cooked build.
	if (CICompanionEnv::IsEnabled(TEXT("MHLLM_FACE_SELFTEST")) || FParse::Param(FCommandLine::Get(), TEXT("FaceSelfTest")))
	{
		Face->StartSelfTest();
		UE_LOG(LogCompanionFace, Log, TEXT("Face self-test running."));
	}
}
