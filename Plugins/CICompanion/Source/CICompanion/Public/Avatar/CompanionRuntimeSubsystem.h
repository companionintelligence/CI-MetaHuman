#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CompanionRuntimeSubsystem.generated.h"

class AMetaHumanCompanionCharacter;
class UCompanionFaceComponent;

// Instantiates the companion and connects its replies to the face.
//
// This closes the gap that made everything else moot: AMetaHumanCompanionCharacter, the Ollama
// client, the stream parsers and the emotion mapper were all implemented and unit-tested, but
// NOTHING IN THE SHIPPED GAME EVER CONSTRUCTED ONE. The cooked build contained the entire
// conversational stack as unreachable code, so the diorama could only ever be a diorama.
//
// It is a subsystem for the same reason the face driver is: the map is authored headlessly by
// Python in a content-only project that cannot reference plugin C++ or embed a Blueprint
// (HANDOFF.md Lesson 6), so the character cannot be placed at author time. Spawning it at
// BeginPlay is the one route that survives that constraint.
//
// Speech reaches the face via the OnCompanionSpoke multicast delegate rather than the
// OnSpeak/OnSetVisemeWeights BlueprintImplementableEvents, which do nothing without a
// hand-authored Blueprint subclass that this pipeline cannot produce.
UCLASS()
class CICOMPANION_API UCompanionRuntimeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM")
	AMetaHumanCompanionCharacter* GetCompanion() const { return Companion; }

	// Routes a user turn to the companion. The single entry point any future input path (Pixel
	// Streaming data channel, HUD, STT) should call, so none of them need to find the actor.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM")
	bool SubmitUserText(const FString& Text);

private:
	// Receives the finished reply and drives the mouth from it. Must be a UFUNCTION: the delegate
	// is dynamic, so a lambda or raw member cannot bind to it.
	UFUNCTION()
	void HandleCompanionSpoke(const FString& Text);

	UPROPERTY(Transient)
	TObjectPtr<AMetaHumanCompanionCharacter> Companion;

	// Resolved lazily from UCompanionFaceSubsystem, whose OnWorldBeginPlay ordering relative to
	// this one is not guaranteed.
	UCompanionFaceComponent* ResolveFace() const;
};
