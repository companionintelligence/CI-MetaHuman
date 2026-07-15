#pragma once

#include "CoreMinimal.h"

class UMetaHumanLLMSettings;

// Applies environment-variable overrides onto UMetaHumanLLMSettings at startup, so the same
// packaged build can be pointed at CI-hub's Ollama (or any endpoint) purely through container
// config — no rebuild, no baked-in URLs. This is the "ci-hub ollama support" seam: a
// containerized deployment sets CI_HUB_URL / CI_HUB_TOKEN / CI_HUB_MODEL and the agent talks
// to the hub's OpenAI-compatible endpoint via UOpenAIChatClient.
//
// Recognized variables (all optional; unset variables leave the configured/default value):
//   MHLLM_BACKEND        "ollama" | "openai"  -> BackendType
//   CI_HUB_URL           OpenAI-compatible base URL (implies BackendType = OpenAICompatible)
//   CI_HUB_TOKEN         Bearer token for the hub -> OpenAIApiKey
//   CI_HUB_MODEL         model id at the hub      -> OpenAIModel
//   OLLAMA_BASE_URL      -> OllamaBaseUrl
//   OLLAMA_MODEL         -> OllamaModel
//   WHISPER_SERVER_URL   -> WhisperServerUrl
//   PIPER_SERVER_URL     -> PiperServerUrl
//   MHLLM_SYSTEM_PROMPT  -> SystemPrompt
class METAHUMANLOCALLLM_API FMetaHumanLLMEnvConfig
{
public:
	// Injectable lookup so the resolution logic is unit-testable without touching the real
	// process environment. Should return an empty string for an unset variable.
	using FEnvLookup = TFunctionRef<FString(const TCHAR* /*Name*/)>;

	// Applies recognized env overrides onto Settings in place. Returns the number of
	// variables that were present and applied (for logging).
	static int32 Apply(UMetaHumanLLMSettings& Settings, FEnvLookup EnvLookup);

	// Convenience wrapper that reads the real process environment via FPlatformMisc.
	static int32 ApplyFromProcessEnv(UMetaHumanLLMSettings& Settings);
};
