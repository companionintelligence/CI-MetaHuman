#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MetaHumanLLMSettings.generated.h"

// Which chat backend AMetaHumanCompanionCharacter talks to. The original planning doc
// ("Existing Tech on RPM / Whisper / Piper") calls for a pluggable AI backend "compatible
// with ... our companion intelligence servers or OpenAI or Claude or anything else" via an
// OpenAI-style API. OllamaNative uses Ollama's own /api/chat (NDJSON stream); OpenAICompatible
// uses the OpenAI /v1/chat/completions shape (SSE stream) and therefore also works against
// Ollama's built-in /v1 endpoint, the CI server, OpenAI, or any OpenAI-compatible gateway.
UENUM(BlueprintType)
enum class EChatBackendType : uint8
{
	OllamaNative       UMETA(DisplayName = "Ollama (native /api/chat)"),
	OpenAICompatible   UMETA(DisplayName = "OpenAI-compatible (/v1/chat/completions)")
};

// Project Settings > Plugins > MetaHuman Local LLM. Mirrors the environment-profile
// config pattern from the CI-Spatial-Companion-Unity sibling (server IP/port/model as
// editable fields) but as native UE developer settings instead of a ScriptableObject.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "MetaHuman Local LLM"))
class CICOMPANION_API UMetaHumanLLMSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMetaHumanLLMSettings();

	// Selects which IChatBackend implementation the companion uses at runtime.
	UPROPERTY(Config, EditAnywhere, Category = "Backend")
	EChatBackendType BackendType = EChatBackendType::OllamaNative;

	// Seconds before a chat request is abandoned. 0 keeps the engine default.
	//
	// Neither client set a timeout at all, which meant a backend that accepted the connection and
	// then stopped answering -- an Ollama mid-model-load, a dead SSH tunnel still holding its
	// listener open, a machine that went to sleep -- left the request in flight forever. Nothing
	// ever fired, so the companion sat on "thinking..." for the rest of the session with no error
	// and no way back short of a restart. A timeout is what converts that into a reportable failure.
	//
	// Generous, because a COLD model load is legitimately slow: a 27B model paging in from disk can
	// take well over a minute before it emits its first token, and cutting that off would break the
	// first turn after every deploy.
	UPROPERTY(Config, EditAnywhere, Category = "Backend", meta = (ClampMin = "0.0"))
	float ChatRequestTimeoutSeconds = 180.0f;

	// --- Ollama (native) backend ---

	// Base URL of the local Ollama server.
	UPROPERTY(Config, EditAnywhere, Category = "Ollama", meta = (EditCondition = "BackendType == EChatBackendType::OllamaNative"))
	FString OllamaBaseUrl = TEXT("http://127.0.0.1:11434");

	// Model name as known to `ollama list`, e.g. "llama3.2".
	UPROPERTY(Config, EditAnywhere, Category = "Ollama", meta = (EditCondition = "BackendType == EChatBackendType::OllamaNative"))
	FString OllamaModel = TEXT("llama3.2");

	// Reasoning models (qwen3.x, deepseek-r1, ...) emit a chain of thought before answering.
	// For a companion that is pure latency cost: measured on beta-1, qwen3.6:27b answers a
	// one-line greeting in ~814ms with thinking off and 16-21s with it on -- a ~20x penalty for
	// reasoning nobody reads, since Ollama returns it in a separate `thinking` field that never
	// reaches the mouth. Off by default; turn it on only where answer quality needs deliberation.
	// Sent as "think": false, which Ollama ignores for non-reasoning models.
	UPROPERTY(Config, EditAnywhere, Category = "Ollama", meta = (EditCondition = "BackendType == EChatBackendType::OllamaNative"))
	bool bEnableThinking = false;

	// --- OpenAI-compatible backend ---

	// Base URL up to and including the version segment, no trailing slash — the client
	// appends "/chat/completions". For Ollama's built-in OpenAI shim use
	// "http://127.0.0.1:11434/v1"; for OpenAI proper, "https://api.openai.com/v1".
	UPROPERTY(Config, EditAnywhere, Category = "OpenAI-compatible", meta = (EditCondition = "BackendType == EChatBackendType::OpenAICompatible"))
	FString OpenAIBaseUrl = TEXT("http://127.0.0.1:11434/v1");

	// Sent as "Authorization: Bearer <key>". Optional for local servers (Ollama ignores
	// it); required for OpenAI/Claude gateways. Stored in DefaultGame.ini — do not commit a
	// real key to source control.
	UPROPERTY(Config, EditAnywhere, Category = "OpenAI-compatible", meta = (EditCondition = "BackendType == EChatBackendType::OpenAICompatible"))
	FString OpenAIApiKey;

	// Model id as the gateway expects it, e.g. "llama3.2" (Ollama), "gpt-4o-mini" (OpenAI).
	UPROPERTY(Config, EditAnywhere, Category = "OpenAI-compatible", meta = (EditCondition = "BackendType == EChatBackendType::OpenAICompatible"))
	FString OpenAIModel = TEXT("llama3.2");

	// --- Shared ---

	// Seeded as the first (system-role) message of every new conversation.
	UPROPERTY(Config, EditAnywhere, Category = "Prompt", meta = (MultiLine = true))
	FString SystemPrompt = TEXT(
		"You are a helpful, friendly companion speaking out loud through a MetaHuman avatar. "
		"Keep replies short and conversational, since they will be spoken aloud.");

	// Upper bound on retained user+assistant PAIRS, excluding the system prompt. 0 = unlimited.
	//
	// /api/chat has no server-side session, so the whole transcript is re-sent every turn: without a
	// cap each turn is slower and costlier than the last until the request overruns the model's
	// context window, at which point the server truncates from the FRONT and the first casualty is
	// the system prompt. She does not error when that happens, she just stops being Eliza.
	UPROPERTY(Config, EditAnywhere, Category = "Prompt", meta = (ClampMin = "0"))
	int32 MaxRetainedTurns = 24;

	// Remember the conversation across sessions. OPT-IN.
	//
	// Off by default deliberately, and not for technical reasons: turning it on writes everything
	// anyone says to this companion to disk in plain text, indefinitely. That is a reasonable thing
	// for an operator to choose and an unreasonable thing to choose on their behalf, so the default
	// is the one that keeps a kiosk from quietly accumulating transcripts of its visitors.
	UPROPERTY(Config, EditAnywhere, Category = "Prompt")
	bool bPersistConversation = false;

	// Where that transcript lives. Blank uses UOllamaConversation::DefaultMemoryPath(), which is
	// under Saved/ so it survives a cook and is never packaged into one.
	UPROPERTY(Config, EditAnywhere, Category = "Prompt", meta = (EditCondition = "bPersistConversation"))
	FString ConversationMemoryPath;

	// --- Voice ---

	// Base URL of a local whisper.cpp server (built with `whisper-server`). The Whisper STT
	// component POSTs captured audio to "<url>/inference". Intentionally blank by default so
	// host services must be configured explicitly rather than guessed from local ports.
	// --- AMD Lemonade Server (one ROCm-accelerated service for chat + STT + TTS) ---
	// Base URL of a Lemonade instance, e.g. http://127.0.0.1:13305. Empty disables Kokoro TTS.
	// NOTE the API lives under /api/v1/..., not /v1/... — verified by probe, not assumed.
	UPROPERTY(Config, EditAnywhere, Category = "Voice")
	FString KokoroServerUrl;

	UPROPERTY(Config, EditAnywhere, Category = "Voice")
	FString KokoroModel = TEXT("Kokoro-82M-ONNX");

	UPROPERTY(Config, EditAnywhere, Category = "Voice")
	FString KokoroVoice = TEXT("af_sky");

	// Base URL of the kokoro-align sidecar (Deployment/kokoro-align), e.g. http://127.0.0.1:13400.
	// It returns the per-phoneme durations Kokoro's duration predictor already computed, which the
	// OpenAI /audio/speech schema structurally discards — so this is the difference between a mouth
	// following what was said and one following how it was spelled.
	//
	// Blank by default, like the other service URLs: an unset aligner is a precision loss, never a
	// failure, and lip-sync falls back to rescaling the spelling estimate to the clip length.
	UPROPERTY(Config, EditAnywhere, Category = "Voice")
	FString KokoroAlignerUrl;

	// Path appended to WhisperServerUrl. whisper.cpp's own server serves "/inference"; Lemonade
	// serves the OpenAI shape at "/api/v1/audio/transcriptions". Both return {"text": ...}, so
	// only the path differs and one setting covers both rather than needing a second client.
	UPROPERTY(Config, EditAnywhere, Category = "Voice")
	FString WhisperEndpointPath = TEXT("/inference");

	UPROPERTY(Config, EditAnywhere, Category = "Voice")
	FString WhisperServerUrl;

	// Model id sent as the multipart "model" field. Blank omits the field entirely.
	//
	// whisper.cpp's own /inference serves exactly one model and ignores this. The OpenAI shape does
	// NOT: Lemonade's /api/v1/audio/transcriptions requires it and rejects a request without one, so
	// omitting the field made the whole STT path fail against the very server it was pointed at.
	// Blank-by-default keeps the whisper.cpp case byte-identical to what it was.
	UPROPERTY(Config, EditAnywhere, Category = "Voice")
	FString WhisperModel;

	// Base URL of a local Piper HTTP server. The Piper TTS component POSTs text and expects
	// WAV audio back. Intentionally blank by default for the same reason as Whisper above.
	UPROPERTY(Config, EditAnywhere, Category = "Voice")
	FString PiperServerUrl;

	// Filesystem path to a local speech-to-text model (e.g. a whisper.cpp GGML file), for
	// in-process backends that load the model directly rather than calling a server.
	UPROPERTY(Config, EditAnywhere, Category = "Voice")
	FString WhisperModelPath;

	// Filesystem path to a local text-to-speech voice file/config, for in-process TTS.
	UPROPERTY(Config, EditAnywhere, Category = "Voice")
	FString TTSVoicePath;
};
