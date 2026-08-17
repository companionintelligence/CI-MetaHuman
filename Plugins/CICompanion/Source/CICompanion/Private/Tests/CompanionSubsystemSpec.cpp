#include "Misc/AutomationTest.h"

#include "Avatar/CompanionFaceComponent.h"
#include "Avatar/CompanionFaceSubsystem.h"
#include "Avatar/CompanionRuntimeSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Voice/KokoroTextToSpeech.h"

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

	// The mouth when the VOICE FAILS.
	//
	// With a TTS component attached, HandleCompanionSpoke deliberately does not plan a viseme
	// track: it arms the utterance and hands the mouth to the audio callback, because planning in
	// both places would play the utterance twice at two different speeds. The consequence nobody
	// covered is that when synthesis never produces audio -- no server URL, a non-2xx, an
	// undecodable body -- that callback never comes and the face simply stays shut for the whole
	// reply, with the text on screen and no error anywhere near the face.
	//
	// These drive the handlers through reflection rather than through a live Kokoro. They are
	// UFUNCTIONs precisely because they are dynamic-delegate targets, so this is the same entry
	// point the delegate uses, and it keeps the cases free of an HTTP server.
	Describe("UCompanionRuntimeSubsystem speech failure", [this]()
	{
		// Binds a face to a resident the way the real BeginPlay does, and returns a runtime
		// subsystem holding a TTS component -- the state in which the mouth is deferred.
		auto MakeArmedSubsystem = [this](UCompanionFaceComponent*& OutFace) -> UCompanionRuntimeSubsystem*
		{
			AActor* Resident = World->SpawnActor<AActor>();
			Resident->Tags.Add(UCompanionFaceSubsystem::ResidentTag);
			USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(Resident);
			Mesh->RegisterComponent();
			Resident->SetRootComponent(Mesh);

			// The REGISTERED subsystem, not a fresh NewObject: ResolveFace looks the face driver up
			// through World->GetSubsystem, so a hand-built instance would be invisible to it.
			UCompanionFaceSubsystem* FaceSubsystem = World->GetSubsystem<UCompanionFaceSubsystem>();
			if (!FaceSubsystem)
			{
				AddError(TEXT("world has no CompanionFaceSubsystem"));
				return nullptr;
			}
			FaceSubsystem->OnWorldBeginPlay(*World);
			OutFace = FaceSubsystem->GetFace();

			UCompanionRuntimeSubsystem* Subsystem = NewObject<UCompanionRuntimeSubsystem>(World);
			// Stands in for the character's own component. Set by reflection because Tts is private
			// and is otherwise only ever adopted from a spawned AMetaHumanCompanionCharacter; the
			// point of these cases is the mouth, not the adoption.
			FObjectProperty* TtsProperty = FindFProperty<FObjectProperty>(
				UCompanionRuntimeSubsystem::StaticClass(), TEXT("Tts"));
			if (!TtsProperty)
			{
				AddError(TEXT("UCompanionRuntimeSubsystem has no reflected Tts property"));
				return nullptr;
			}
			TtsProperty->SetObjectPropertyValue_InContainer(Subsystem,
				NewObject<UKokoroTextToSpeech>(Resident));
			return Subsystem;
		};

		auto Invoke = [](UObject* Target, const TCHAR* FunctionName, const FString& Text)
		{
			struct FTextParam { FString Text; } Params{ Text };
			Target->ProcessEvent(Target->FindFunctionChecked(FName(FunctionName)), &Params);
		};

		It("defers the mouth to the audio callback while a voice is attached", [this, MakeArmedSubsystem, Invoke]()
		{
			UCompanionFaceComponent* Face = nullptr;
			UCompanionRuntimeSubsystem* Subsystem = MakeArmedSubsystem(Face);
			if (!Subsystem || !Face) { return; }

			Invoke(Subsystem, TEXT("HandleCompanionSpoke"), TEXT("Hello there."));
			// Not a bug -- this is the deferral working, and it is the precondition that makes the
			// next case matter. Asserted so that if the deferral is ever removed, the failure test
			// below cannot start passing for the wrong reason.
			TestFalse("mouth waits for audio", Face->IsSpeaking());
		});

		It("mouths the line anyway when synthesis produced no audio", [this, MakeArmedSubsystem, Invoke]()
		{
			UCompanionFaceComponent* Face = nullptr;
			UCompanionRuntimeSubsystem* Subsystem = MakeArmedSubsystem(Face);
			if (!Subsystem || !Face) { return; }

			Invoke(Subsystem, TEXT("HandleCompanionSpoke"), TEXT("Hello there."));
			// Every Kokoro failure exit broadcasts OnSpeechFinished without ever having broadcast
			// OnSpeechAudioReady. That combination is the failure signal.
			Invoke(Subsystem, TEXT("HandleSpeechFinished"), TEXT("Hello there."));
			TestTrue("mouth falls back rather than staying shut", Face->IsSpeaking());
		});

		It("ignores a finish belonging to a superseded reply", [this, MakeArmedSubsystem, Invoke]()
		{
			UCompanionFaceComponent* Face = nullptr;
			UCompanionRuntimeSubsystem* Subsystem = MakeArmedSubsystem(Face);
			if (!Subsystem || !Face) { return; }

			Invoke(Subsystem, TEXT("HandleCompanionSpoke"), TEXT("The reply now in flight."));
			Invoke(Subsystem, TEXT("HandleSpeechFinished"), TEXT("A reply from before the barge-in."));
			// Driving here would mouth the CURRENT text on the previous utterance's cue, which is
			// the stutter the identity guards elsewhere in this class exist to prevent.
			TestFalse("stale finish does not drive the mouth", Face->IsSpeaking());
		});

		It("mouths a failed line that never passed through the reply handler", [this, MakeArmedSubsystem, Invoke]()
		{
			UCompanionFaceComponent* Face = nullptr;
			UCompanionRuntimeSubsystem* Subsystem = MakeArmedSubsystem(Face);
			if (!Subsystem || !Face) { return; }

			// No HandleCompanionSpoke first, so nothing is armed. A line can still reach the voice
			// this way, and the mouth should follow the text it was handed rather than the state
			// it was not.
			Invoke(Subsystem, TEXT("HandleSpeechFinished"), TEXT("Unprompted."));
			TestTrue("adopts the delegate's text", Face->IsSpeaking());
		});

		It("ignores a finish carrying no text at all", [this, MakeArmedSubsystem, Invoke]()
		{
			UCompanionFaceComponent* Face = nullptr;
			UCompanionRuntimeSubsystem* Subsystem = MakeArmedSubsystem(Face);
			if (!Subsystem || !Face) { return; }

			// StopSpeaking clears Kokoro's PendingText, so a cancelled request that still calls
			// back reports an empty string. There is nothing to mouth and nothing to infer.
			Invoke(Subsystem, TEXT("HandleSpeechFinished"), FString());
			TestFalse("no text, no fallback", Face->IsSpeaking());
		});
	});
}
