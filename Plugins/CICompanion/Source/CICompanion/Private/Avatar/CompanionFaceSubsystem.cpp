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


const FName UCompanionFaceSubsystem::ResidentTag(TEXT("CompanionFace"));


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
	Face->SetProfile(FFaceProfile::ByName(CICompanionEnv::Get(TEXT("MHLLM_FACE_PROFILE"), TEXT("CC"))));
	Face->SetTargetMesh(Mesh);

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

	UE_LOG(LogCompanionFace, Log, TEXT("Face driver bound to '%s': %d/%d profile curves resolved."),
		*Resident->GetName(), Face->GetResolvedCurveCount(), Face->GetProfileCurveCount());

	// Opt-in, because it visibly overrides the face: MHLLM_FACE_SELFTEST=1 (or -FaceSelfTest)
	// cycles every viseme so a captured frame proves the path works in the cooked build.
	if (CICompanionEnv::IsEnabled(TEXT("MHLLM_FACE_SELFTEST")) || FParse::Param(FCommandLine::Get(), TEXT("FaceSelfTest")))
	{
		Face->StartSelfTest();
		UE_LOG(LogCompanionFace, Log, TEXT("Face self-test running."));
	}
}
