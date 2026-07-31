#include "Avatar/CompanionFaceSubsystem.h"
#include "CICompanionLog.h"

#include "Avatar/CompanionFaceComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/CommandLine.h"


const FName UCompanionFaceSubsystem::ResidentTag(TEXT("CompanionFace"));

namespace
{
	// Env vars follow the FMetaHumanLLMEnvConfig convention already used for the LLM/voice
	// endpoints, so the container can configure this the same way as everything else.
	FString GetEnv(const TCHAR* Name, const FString& Default)
	{
		const FString Value = FPlatformMisc::GetEnvironmentVariable(Name);
		return Value.IsEmpty() ? Default : Value;
	}

	bool EnvEnabled(const TCHAR* Name)
	{
		const FString Value = FPlatformMisc::GetEnvironmentVariable(Name);
		return Value == TEXT("1") || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase);
	}
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
	Face->SetProfile(FFaceProfile::ByName(GetEnv(TEXT("MHLLM_FACE_PROFILE"), TEXT("CC"))));
	Face->SetTargetMesh(Mesh);

	UE_LOG(LogCompanionFace, Log, TEXT("Face driver bound to '%s': %d/%d profile curves resolved."),
		*Resident->GetName(), Face->GetResolvedCurveCount(), Face->GetProfileCurveCount());

	// Opt-in, because it visibly overrides the face: MHLLM_FACE_SELFTEST=1 (or -FaceSelfTest)
	// cycles every viseme so a captured frame proves the path works in the cooked build.
	if (EnvEnabled(TEXT("MHLLM_FACE_SELFTEST")) || FParse::Param(FCommandLine::Get(), TEXT("FaceSelfTest")))
	{
		Face->StartSelfTest();
		UE_LOG(LogCompanionFace, Log, TEXT("Face self-test running."));
	}
}
