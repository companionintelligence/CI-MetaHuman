#pragma once

#include "CoreMinimal.h"
#include "Voice/SpeechToTextComponent.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Generators/AudioGenerator.h"
#include "WhisperSpeechToText.generated.h"

class UAudioCapture;

// Speech-to-text against a local whisper.cpp HTTP server (`whisper-server`), matching the
// planning doc's Whisper reference (chat/stt-whisper.js in CI-Embodied-Companion-Web).
// It POSTs captured audio as multipart/form-data to "<WhisperServerUrl>/inference" and
// parses the JSON transcription, firing OnTranscribed.
//
// This component is a *server client*: capture is decoupled from transcription. Feed it a
// finished WAV buffer via TranscribeWav() from whatever capture source you have (push-to-talk
// buffer, UAudioCapture submix recording, an imported clip). StartListening/StopListening
// track intent and are the documented seam where a live mic-capture source hands its buffered
// WAV to TranscribeWav — the actual platform mic binding is left to a subclass/Blueprint so
// this class stays free of the AudioCapture module and unit-testable.
UCLASS(ClassGroup = (MetaHumanLLM), meta = (BlueprintSpawnableComponent))
class CICOMPANION_API UWhisperSpeechToText : public USpeechToTextComponent
{
	GENERATED_BODY()

public:
	virtual void StartListening() override;
	virtual void StopListening() override;

	// Transcribes a complete WAV buffer by POSTing it to the configured whisper server.
	// On success fires OnTranscribed with the recognized text.
	UFUNCTION(BlueprintCallable, Category = "MetaHumanLLM|Voice")
	void TranscribeWav(const TArray<uint8>& WavBytes);

	// True between StartListening and StopListening.
	UFUNCTION(BlueprintPure, Category = "MetaHumanLLM|Voice")
	bool IsListening() const { return bIsListening; }

	// --- Pure helpers, exposed for unit testing without a live server ---

	// Assembles a multipart/form-data body carrying the WAV bytes under the "file" field
	// plus a "response_format=json" field, using the given boundary token.
	static TArray<uint8> BuildMultipartFormData(const TArray<uint8>& WavBytes, const FString& Boundary, const FString& FileName);

	// The matching Content-Type header value for a multipart body with this boundary.
	static FString MultipartContentType(const FString& Boundary);

	// Extracts the transcription text from a whisper-server JSON response ({"text": "..."}).
	// Returns false if the body is not valid JSON or has no non-empty "text" field.
	static bool ParseTranscriptionResponse(const FString& ResponseBody, FString& OutText);

private:
	void HandleTranscriptionComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	TArray<uint8> ConvertToWav(const TArray<float>& FloatSamples, int32 InputSampleRate);

	bool bIsListening = false;

	UPROPERTY(Transient)
	TObjectPtr<UAudioCapture> AudioCapture;

	FAudioGeneratorHandle AudioCaptureHandle;
	TArray<float> CapturedFloats;
	FCriticalSection CapturedFloatsMutex;
};
