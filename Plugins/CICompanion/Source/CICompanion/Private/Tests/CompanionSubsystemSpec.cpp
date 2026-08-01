#include "Misc/AutomationTest.h"

#include "Avatar/CompanionFaceSubsystem.h"
#include "Avatar/CompanionRuntimeSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

BEGIN_DEFINE_SPEC(FCompanionSubsystemSpec, "MetaHumanLocalLLM.Backend.CompanionSubsystems", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	UWorld* World = nullptr;
END_DEFINE_SPEC(FCompanionSubsystemSpec)

// The two world subsystems had NO coverage at all, and they are the load-bearing glue: one binds
// the face to the resident, the other constructs the companion and routes every reply. That gap
// is not academic -- the worst bug this project shipped was "nothing ever constructed the
// companion", and it lived exactly here while 200-odd tests stayed green.
//
// They resisted testing because they need a UWorld. A transient one is cheap enough to build per
// case, which covers the selection and guard logic without a cooked map or a running game.
void FCompanionSubsystemSpec::Define()
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

	Describe("UCompanionFaceSubsystem::FindResident", [this]()
	{
		It("finds nothing in an empty world rather than crashing", [this]()
		{
			UCompanionFaceSubsystem* Subsystem = NewObject<UCompanionFaceSubsystem>(World);
			TestNull("no resident", Subsystem->FindResident(*World));
		});

		It("selects the actor carrying the resident tag", [this]()
		{
			AActor* Tagged = World->SpawnActor<AActor>();
			Tagged->Tags.Add(UCompanionFaceSubsystem::ResidentTag);
			World->SpawnActor<AActor>();	// an untagged bystander

			UCompanionFaceSubsystem* Subsystem = NewObject<UCompanionFaceSubsystem>(World);
			TestEqual("tagged actor wins", Subsystem->FindResident(*World), Tagged);
		});

		It("ignores actors carrying some other tag", [this]()
		{
			AActor* Other = World->SpawnActor<AActor>();
			Other->Tags.Add(FName(TEXT("SomethingElse")));

			UCompanionFaceSubsystem* Subsystem = NewObject<UCompanionFaceSubsystem>(World);
			TestNull("no false positive", Subsystem->FindResident(*World));
		});

		It("uses the tag name the authoring script actually stamps", [this]()
		{
			// author_diorama.py writes this literal. If the two ever disagree the face silently
			// never binds -- there is no error, the mouth simply does not move.
			TestEqual("tag", UCompanionFaceSubsystem::ResidentTag.ToString(), FString(TEXT("CompanionFace")));
		});
	});

	Describe("UCompanionFaceSubsystem::ShouldCreateSubsystem", [this]()
	{
		It("declines a non-world outer", [this]()
		{
			UCompanionFaceSubsystem* Subsystem = NewObject<UCompanionFaceSubsystem>(World);
			TestFalse("not for a plain object", Subsystem->ShouldCreateSubsystem(GetTransientPackage()));
		});

		It("accepts a game world", [this]()
		{
			UCompanionFaceSubsystem* Subsystem = NewObject<UCompanionFaceSubsystem>(World);
			TestTrue("game world", Subsystem->ShouldCreateSubsystem(World));
		});
	});

	Describe("UCompanionRuntimeSubsystem guards", [this]()
	{
		It("refuses an empty user turn", [this]()
		{
			UCompanionRuntimeSubsystem* Subsystem = NewObject<UCompanionRuntimeSubsystem>(World);
			TestFalse("empty text rejected", Subsystem->SubmitUserText(FString()));
		});

		It("refuses a turn when no companion was constructed", [this]()
		{
			// The exact condition that shipped: the subsystem exists, the map is fine, and there
			// is no companion behind it. It must report failure rather than silently do nothing.
			UCompanionRuntimeSubsystem* Subsystem = NewObject<UCompanionRuntimeSubsystem>(World);
			TestNull("no companion yet", Subsystem->GetCompanion());
			TestFalse("submit fails", Subsystem->SubmitUserText(TEXT("hello")));
		});

		It("refuses an empty scripted line", [this]()
		{
			UCompanionRuntimeSubsystem* Subsystem = NewObject<UCompanionRuntimeSubsystem>(World);
			TestFalse("empty literal rejected", Subsystem->SpeakLiteral(FString()));
		});

		It("declines a non-world outer", [this]()
		{
			UCompanionRuntimeSubsystem* Subsystem = NewObject<UCompanionRuntimeSubsystem>(World);
			TestFalse("not for a plain object", Subsystem->ShouldCreateSubsystem(GetTransientPackage()));
		});
	});
}
