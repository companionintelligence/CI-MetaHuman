#include "Avatar/CompanionRuntimeSubsystem.h"

#include "Avatar/CompanionFaceComponent.h"
#include "Avatar/CompanionFaceSubsystem.h"
#include "Avatar/MetaHumanCompanionCharacter.h"
#include "Voice/KokoroTextToSpeech.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Voice/VisemePlanner.h"

DEFINE_LOG_CATEGORY_STATIC(LogCompanionRuntime, Log, All);

namespace
{
	FString GetEnv(const TCHAR* Name, const FString& Default)
	{
		const FString Value = FPlatformMisc::GetEnvironmentVariable(Name);
		return Value.IsEmpty() ? Default : Value;
	}

	bool EnvDisabled(const TCHAR* Name)
	{
		const FString Value = FPlatformMisc::GetEnvironmentVariable(Name);
		return Value == TEXT("0") || Value.Equals(TEXT("false"), ESearchCase::IgnoreCase);
	}
}

bool UCompanionRuntimeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UCompanionRuntimeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (EnvDisabled(TEXT("MHLLM_COMPANION")))
	{
		UE_LOG(LogCompanionRuntime, Log, TEXT("Companion disabled by MHLLM_COMPANION=0."));
		return;
	}

	// Spawned far below the floor and never rendered: this actor is the conversational brain, not
	// the body. The body is the authored SkeletalMeshActor the face driver binds to. Keeping them
	// separate means the map stays a plain visual asset with no dependency on plugin C++.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Name = TEXT("CompanionBrain");
	Companion = InWorld.SpawnActor<AMetaHumanCompanionCharacter>(
		AMetaHumanCompanionCharacter::StaticClass(), FVector(0.0f, 0.0f, -10000.0f), FRotator::ZeroRotator, Params);

	if (!Companion)
	{
		UE_LOG(LogCompanionRuntime, Error, TEXT("Failed to spawn companion; conversation unavailable."));
		return;
	}
	Companion->SetActorHiddenInGame(true);
	Companion->OnCompanionSpoke.AddDynamic(this, &UCompanionRuntimeSubsystem::HandleCompanionSpoke);

	// Prefer MEASURED audio length over an estimated speaking rate when TTS is live. PlanFromText
	// guesses at 165wpm; the E2E harness measured real replies between 131 and 184wpm, so on a
	// long sentence the estimate drifts far enough to see. Binding here keeps the mouth locked to
	// the clip that is actually playing.
	if (UKokoroTextToSpeech* Kokoro = Companion->FindComponentByClass<UKokoroTextToSpeech>())
	{
		Kokoro->OnSpeechAudioReady.AddDynamic(this, &UCompanionRuntimeSubsystem::HandleSpeechAudioReady);
		UE_LOG(LogCompanionRuntime, Log, TEXT("Viseme timing bound to measured Kokoro audio duration."));
	}

	UE_LOG(LogCompanionRuntime, Log, TEXT("Companion spawned and bound (backend=%s model=%s)."),
		*GetEnv(TEXT("MHLLM_BACKEND"), TEXT("<unset>")), *GetEnv(TEXT("OLLAMA_MODEL"), TEXT("<unset>")));

	// An opening line, because there is still no input path: without it a correct build is
	// indistinguishable from a broken one. Deferred rather than sent inline, since the LLM call is
	// slow enough to stall level load and the streamer has not attached yet at BeginPlay.
	// Default is now "none": the opening line is scripted and delivered by
	// UCompanionStageDirector at the end of the camera move. Two greetings would collide -- the
	// LLM one would fire on a 5s timer while the camera was still flying.
	const FString Greeting = GetEnv(TEXT("MHLLM_GREETING"), TEXT("none"));
	if (!Greeting.IsEmpty() && !Greeting.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		FTimerHandle Handle;
		InWorld.GetTimerManager().SetTimer(Handle, [this, Greeting]()
		{
			UE_LOG(LogCompanionRuntime, Log, TEXT("Sending opening turn."));
			SubmitUserText(Greeting);
		}, 5.0f, false);
	}
}

bool UCompanionRuntimeSubsystem::SubmitUserText(const FString& Text)
{
	if (!Companion || Text.IsEmpty())
	{
		return false;
	}
	Companion->SubmitUserText(Text);
	return true;
}

UCompanionFaceComponent* UCompanionRuntimeSubsystem::ResolveFace() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	const UCompanionFaceSubsystem* FaceSubsystem = World->GetSubsystem<UCompanionFaceSubsystem>();
	return FaceSubsystem ? FaceSubsystem->GetFace() : nullptr;
}

bool UCompanionRuntimeSubsystem::SpeakLiteral(const FString& Text)
{
	if (Text.IsEmpty())
	{
		return false;
	}
	// Deliberately routed through the same handler a generated reply uses, so scripted and live
	// speech can never drift apart in how they drive the face.
	HandleCompanionSpoke(Text);
	return true;
}

void UCompanionRuntimeSubsystem::HandleSpeechAudioReady(const FString& Text, float DurationSeconds)
{
	UCompanionFaceComponent* Face = ResolveFace();
	if (!Face || DurationSeconds <= 0.0f)
	{
		return;
	}
	// Replaces the estimated track queued by HandleCompanionSpoke with one rescaled to the clip.
	const TArray<FVisemeSpan> Track = FVisemePlanner::PlanForDuration(Text, DurationSeconds);
	Face->PlayVisemeTrack(Track);
	UE_LOG(LogCompanionRuntime, Log, TEXT("Re-timed to audio: %d spans over %.2fs."),
		Track.Num(), DurationSeconds);
}

void UCompanionRuntimeSubsystem::HandleCompanionSpoke(const FString& Text)
{
	UE_LOG(LogCompanionRuntime, Log, TEXT("Companion replied (%d chars): %s"), Text.Len(), *Text.Left(160));

	UCompanionFaceComponent* Face = ResolveFace();
	if (!Face)
	{
		UE_LOG(LogCompanionRuntime, Warning, TEXT("Reply received but no face bound; mouth will not move."));
		return;
	}

	// Text-planned timing, not audio-aligned: there is no TTS clip to measure yet, so the track is
	// estimated from a speaking rate. When TTS lands, switch to PlanForDuration with the clip's
	// real length so the mouth cannot drift against the audio.
	const TArray<FVisemeSpan> Track = FVisemePlanner::PlanFromText(Text);
	Face->PlayVisemeTrack(Track);

	UE_LOG(LogCompanionRuntime, Log, TEXT("Speaking: %d viseme spans over %.2fs."),
		Track.Num(), FVisemePlanner::TrackDuration(Track));
}
