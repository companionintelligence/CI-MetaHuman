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
// StartListening/StopListening capture the host's default audio device, convert the recorded
// mono buffer to WAV, and submit it to the server. Remote Pixel Streaming microphone audio
// still needs a WebRTC-to-UE bridge; UAudioCapture only sees the host device.
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

	// True only while the local capture device is actively recording.
	virtual bool IsListening() const override { return bIsListening; }

	// --- Pure helpers, exposed for unit testing without a live server ---

	// Assembles a multipart/form-data body carrying the WAV bytes under the "file" field,
	// a "response_format=json" field, and — when Model is non-empty — a "model" field.
	//
	// Model is optional because the two servers this one client is meant to cover disagree about it:
	// whisper.cpp's /inference hosts a single model and ignores the field, while the OpenAI shape
	// Lemonade serves REQUIRES it. Defaulted to empty so the whisper.cpp body is unchanged.
	static TArray<uint8> BuildMultipartFormData(const TArray<uint8>& WavBytes, const FString& Boundary, const FString& FileName, const FString& Model = FString());

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
	bool bCaptureStreamOpened = false;
	bool bCaptureDelegateRegistered = false;
	TArray<float> CapturedFloats;
	FCriticalSection CapturedFloatsMutex;
};
