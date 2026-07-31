#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Voice/VisemePlanner.h"
#include "KokoroPhonemeAligner.generated.h"

// Fires exactly once per request, whether it succeeded or not. On failure Spans is EMPTY and
// AlignedSeconds is 0 — that is the signal a caller uses to fall back, and it is deliberately not
// a separate delegate, because a caller that forgets to bind the failure case would leave the
// mouth waiting forever on a service that is simply not there.
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnPhonemeAlignmentReady,
	const FString& /*Text*/, const TArray<FPhonemeSpan>& /*Spans*/, float /*AlignedSeconds*/);

// Client for the kokoro-align sidecar (Deployment/kokoro-align/app.py).
//
// WHY THIS EXISTS AT ALL. Kokoro is StyleTTS2: a duration predictor decides how many frames each
// phoneme occupies, and the length regulator expands the phoneme encodings by exactly those
// durations before the vocoder runs. The model therefore KNOWS where every phoneme lands in the
// waveform it just produced. OpenAI's /v1/audio/speech schema returns audio bytes and nothing
// else, so serving Kokoro through an OpenAI-compatible endpoint (AMD Lemonade, here) discards
// that tensor at the HTTP boundary. The sidecar runs the same model and hands the durations back.
//
// The alternative — inferring timing from the audio afterwards — is strictly worse: a forced
// aligner has to re-derive, from a waveform, a fact the synthesizer had in hand and threw away,
// and it can only ever approximate it. Owning the TTS means the timing is a MEASUREMENT rather
// than an inference.
//
// This is a sibling of UKokoroTextToSpeech rather than part of it because the two answer different
// questions of different services: Lemonade produces the audio the listener hears, this produces
// the timing the face follows. Folding them together would tie the mouth's correctness to the
// speaker's availability, and vice versa.
UCLASS()
class CICOMPANION_API UKokoroPhonemeAligner : public UObject
{
	GENERATED_BODY()

public:
	// Asks the sidecar for the phoneme spans of Text. Always answers via OnAlignmentReady, even
	// when it cannot ask at all (no URL configured, empty text, request refused), so the caller
	// never has to distinguish "failed" from "still thinking".
	void RequestAlignment(const FString& Text);

	// Drops an in-flight request without answering for it. Used on barge-in: a superseded reply's
	// alignment must not arrive and re-time the mouth against audio nobody is playing.
	void CancelPending();

	FOnPhonemeAlignmentReady OnAlignmentReady;

	// Pure helpers, exposed for unit testing without a server.
	static FString BuildEndpointUrl(const FString& BaseUrl);
	static FString BuildRequestBody(const FString& Text, const FString& Voice);

	// Returns false — leaving OutSpans empty — for anything that is not a usable alignment:
	// unparseable JSON, the sidecar's {"error": ...} shape, a missing/!array `spans`, or an array
	// that contains no span with a positive duration. "Parsed successfully but says nothing" must
	// not reach the planner, because a zero-span track is a shut mouth for the whole reply and
	// looks exactly like a rig failure.
	static bool ParseAlignmentResponse(const FString& ResponseBody, TArray<FPhonemeSpan>& OutSpans, float& OutAudioSeconds);

private:
	void HandleAlignmentComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	// Bounded so a hung sidecar costs a fraction of a reply rather than the reply. The caller
	// gives up sooner than this anyway (see UCompanionRuntimeSubsystem's grace window); the
	// timeout exists so the request itself cannot leak.
	static constexpr float RequestTimeoutSeconds = 10.0f;

	FHttpRequestPtr ActiveRequest;
	FString PendingText;
};
