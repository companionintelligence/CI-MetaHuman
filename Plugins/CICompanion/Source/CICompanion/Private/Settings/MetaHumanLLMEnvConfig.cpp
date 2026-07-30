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

	// An explicit MHLLM_BACKEND is AUTHORITATIVE and outranks the CI_HUB_URL inference below.
	// It used to be the other way round, and the result was that MHLLM_BACKEND did nothing
	// whenever CI_HUB_URL happened to be set -- which the base compose always sets. A deploy
	// asking plainly for Ollama silently talked to the hub instead, with a placeholder token,
	// and every turn died on HTTP 405. A specific, deliberate signal must beat an incidental one.
	const FString Backend = EnvLookup(TEXT("MHLLM_BACKEND"));
	bool bBackendExplicit = false;
	if (!Backend.IsEmpty())
	{
		if (Backend.Equals(TEXT("openai"), ESearchCase::IgnoreCase))
		{
			Settings.BackendType = EChatBackendType::OpenAICompatible;
			bBackendExplicit = true;
			++Applied;
		}
		else if (Backend.Equals(TEXT("ollama"), ESearchCase::IgnoreCase))
		{
			Settings.BackendType = EChatBackendType::OllamaNative;
			bBackendExplicit = true;
			++Applied;
		}
	}

	// CI-hub: a hub URL still IMPLIES the OpenAI-compatible path, but only as a default. It
	// always supplies the base URL, so switching back to openai later needs no extra config.
	const FString HubUrl = EnvLookup(TEXT("CI_HUB_URL"));
	if (!HubUrl.IsEmpty())
	{
		Settings.OpenAIBaseUrl = HubUrl;
		if (!bBackendExplicit)
		{
			Settings.BackendType = EChatBackendType::OpenAICompatible;
		}
		++Applied;
	}
	TryString(TEXT("CI_HUB_TOKEN"), Settings.OpenAIApiKey);
	TryString(TEXT("CI_HUB_MODEL"), Settings.OpenAIModel);

	// Vendor-neutral aliases, so this build is a THIN CLIENT for any OpenAI-compatible server --
	// LM Studio, llama.cpp, vLLM, Lemonade -- with no code change and no CI-hub naming. Applied
	// after the hub vars so an explicit OPENAI_* wins over an inherited CI_HUB_*.
	//
	// LM Studio serves /v1 on :1234 by default and ignores the key, but sends 401 without a
	// header, hence the placeholder default rather than empty.
	//   MHLLM_BACKEND=openai OPENAI_BASE_URL=http://host:1234/v1 OPENAI_MODEL=<id>
	TryString(TEXT("OPENAI_BASE_URL"), Settings.OpenAIBaseUrl);
	TryString(TEXT("OPENAI_MODEL"), Settings.OpenAIModel);
	TryString(TEXT("OPENAI_API_KEY"), Settings.OpenAIApiKey);

	// Convenience: naming the LM Studio endpoint alone is enough to select it, since nobody sets
	// it while wanting a different backend.
	const FString LmStudio = EnvLookup(TEXT("LMSTUDIO_BASE_URL"));
	if (!LmStudio.IsEmpty())
	{
		Settings.OpenAIBaseUrl = LmStudio;
		if (!bBackendExplicit)
		{
			Settings.BackendType = EChatBackendType::OpenAICompatible;
		}
		if (Settings.OpenAIApiKey.IsEmpty())
		{
			Settings.OpenAIApiKey = TEXT("lm-studio");
		}
		++Applied;
	}
	TryString(TEXT("LMSTUDIO_MODEL"), Settings.OpenAIModel);

	// Direct Ollama overrides (for a sidecar Ollama rather than the hub).
	TryString(TEXT("OLLAMA_BASE_URL"), Settings.OllamaBaseUrl);
	TryString(TEXT("OLLAMA_MODEL"), Settings.OllamaModel);
	{
		// MHLLM_THINKING=1 re-enables chain-of-thought on reasoning models. Costs ~20x latency
		// (qwen3.6:27b: 814ms -> 16-21s), so it is off unless deliberately asked for.
		const FString Thinking = EnvLookup(TEXT("MHLLM_THINKING"));
		if (!Thinking.IsEmpty())
		{
			Settings.bEnableThinking = (Thinking == TEXT("1") || Thinking.Equals(TEXT("true"), ESearchCase::IgnoreCase));
			++Applied;
		}
	}

	// Voice services.
	TryString(TEXT("KOKORO_SERVER_URL"), Settings.KokoroServerUrl);
	TryString(TEXT("KOKORO_MODEL"), Settings.KokoroModel);
	TryString(TEXT("KOKORO_VOICE"), Settings.KokoroVoice);
	TryString(TEXT("WHISPER_ENDPOINT_PATH"), Settings.WhisperEndpointPath);
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
