#include "Settings/MetaHumanLLMEnvConfig.h"
#include "Settings/MetaHumanLLMSettings.h"
#include "Misc/Parse.h"

int32 FMetaHumanLLMEnvConfig::Apply(UMetaHumanLLMSettings& Settings, FEnvLookup EnvLookup)
{
	int32 Applied = 0;

	auto TryString = [&EnvLookup, &Applied](const TCHAR* Name, FString& Target)
	{
		const FString Value = EnvLookup(Name);
		if (!Value.IsEmpty())
		{
			Target = Value;
			++Applied;
		}
	};

	// Explicit backend selection first, so CI_HUB_URL (below) can still force OpenAI mode.
	const FString Backend = EnvLookup(TEXT("MHLLM_BACKEND"));
	if (!Backend.IsEmpty())
	{
		if (Backend.Equals(TEXT("openai"), ESearchCase::IgnoreCase))
		{
			Settings.BackendType = EChatBackendType::OpenAICompatible;
			++Applied;
		}
		else if (Backend.Equals(TEXT("ollama"), ESearchCase::IgnoreCase))
		{
			Settings.BackendType = EChatBackendType::OllamaNative;
			++Applied;
		}
	}

	// CI-hub: presence of a hub URL implies the OpenAI-compatible path.
	const FString HubUrl = EnvLookup(TEXT("CI_HUB_URL"));
	if (!HubUrl.IsEmpty())
	{
		Settings.OpenAIBaseUrl = HubUrl;
		Settings.BackendType = EChatBackendType::OpenAICompatible;
		++Applied;
	}
	TryString(TEXT("CI_HUB_TOKEN"), Settings.OpenAIApiKey);
	TryString(TEXT("CI_HUB_MODEL"), Settings.OpenAIModel);

	// Direct Ollama overrides (for a sidecar Ollama rather than the hub).
	TryString(TEXT("OLLAMA_BASE_URL"), Settings.OllamaBaseUrl);
	TryString(TEXT("OLLAMA_MODEL"), Settings.OllamaModel);

	// Voice services.
	TryString(TEXT("WHISPER_SERVER_URL"), Settings.WhisperServerUrl);
	TryString(TEXT("PIPER_SERVER_URL"), Settings.PiperServerUrl);

	// Persona.
	TryString(TEXT("MHLLM_SYSTEM_PROMPT"), Settings.SystemPrompt);

	return Applied;
}

int32 FMetaHumanLLMEnvConfig::ApplyFromProcessEnv(UMetaHumanLLMSettings& Settings)
{
	return Apply(Settings, [](const TCHAR* Name)
	{
		return FPlatformMisc::GetEnvironmentVariable(Name);
	});
}
