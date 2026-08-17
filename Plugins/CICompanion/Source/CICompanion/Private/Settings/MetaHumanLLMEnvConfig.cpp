#include "Settings/MetaHumanLLMEnvConfig.h"
#include "Settings/MetaHumanLLMSettings.h"
#include "CICompanion.h"
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

	// Trimmed and validated rather than silently coerced: FCString::Atoi returns 0 for "banana",
	// and 0 means "unlimited" for the turn cap -- so a typo would remove the very bound it was
	// trying to set. Refusing the value and saying so is the only safe reading.
	auto TryInt = [&EnvLookup, &Applied](const TCHAR* Name, int32& Target)
	{
		const FString Value = EnvLookup(Name).TrimStartAndEnd();
		if (Value.IsEmpty())
		{
			return;
		}
		if (Value.IsNumeric())
		{
			const int32 Parsed = FCString::Atoi(*Value);
			// NEGATIVE is rejected; zero is not. Zero legitimately means "unlimited" (see above),
			// but FString::IsNumeric() accepts a leading '-', so "-1" sailed straight through this
			// guard -- the one written to stop a bad value silently removing the cap -- and did
			// something strictly worse than removing it. UOllamaConversation::Trim computes
			// MaxMessages = MaxRetainedTurns * 2, so a negative cap makes Excess exceed the history
			// itself and every append erases the WHOLE conversation, persona included. The bound
			// the operator was tightening instead deletes everything, and nothing says so.
			if (Parsed < 0)
			{
				UE_LOG(LogMetaHumanLLM, Warning,
					TEXT("%s='%s' is negative, which would erase the conversation on every append; keeping %d."),
					Name, *Value, Target);
				return;
			}
			Target = Parsed;
			++Applied;
		}
		else
		{
			UE_LOG(LogMetaHumanLLM, Warning, TEXT("%s='%s' is not a number; keeping %d."), Name, *Value, Target);
		}
	};

	auto TryFloat = [&EnvLookup, &Applied](const TCHAR* Name, float& Target)
	{
		const FString Value = EnvLookup(Name).TrimStartAndEnd();
		if (Value.IsEmpty())
		{
			return;
		}
		if (Value.IsNumeric())
		{
			Target = FCString::Atof(*Value);
			++Applied;
		}
		else
		{
			UE_LOG(LogMetaHumanLLM, Warning, TEXT("%s='%s' is not a number; keeping %.1f."), Name, *Value, Target);
		}
	};

	// Recognises the usual truthy/falsy vocabulary and REFUSES anything else loudly, matching TryInt
	// above. Accepting only "1"/"true" and silently reading everything else as false is the same
	// trap in a nastier form: MHLLM_MEMORY=yes would DISABLE persistence on a deploy that plainly
	// asked for it, while the startup log still reported the override as applied.
	auto TryBool = [&EnvLookup, &Applied](const TCHAR* Name, bool& Target)
	{
		const FString Value = EnvLookup(Name).TrimStartAndEnd();
		if (Value.IsEmpty())
		{
			return;
		}
		if (Value == TEXT("1") || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("yes"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("on"), ESearchCase::IgnoreCase))
		{
			Target = true;
			++Applied;
		}
		else if (Value == TEXT("0") || Value.Equals(TEXT("false"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("no"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			Target = false;
			++Applied;
		}
		else
		{
			UE_LOG(LogMetaHumanLLM, Warning,
				TEXT("%s='%s' is not a recognised boolean (1/true/yes/on or 0/false/no/off); keeping %s."),
				Name, *Value, Target ? TEXT("true") : TEXT("false"));
		}
	};

	// An explicit MHLLM_BACKEND is AUTHORITATIVE and outranks the CI_HUB_URL inference below.
	// It used to be the other way round, and the result was that MHLLM_BACKEND did nothing
	// whenever CI_HUB_URL happened to be set -- which the base compose always sets. A deploy
	// asking plainly for Ollama silently talked to the hub instead, with a placeholder token,
	// and every turn died on HTTP 405. A specific, deliberate signal must beat an incidental one.
	//
	// TRIMMED, because a compose file that interpolates an unset variable leaves whitespace behind
	// and " ollama " is unmistakably a request for Ollama. Untrimmed it matched neither branch and
	// fell through to exactly the silent CI_HUB_URL takeover described above.
	const FString Backend = EnvLookup(TEXT("MHLLM_BACKEND")).TrimStartAndEnd();
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
		else
		{
			// LOUD. Both variables look correct in isolation and the wrong one quietly wins, which
			// is precisely the failure mode EnvConfigSpec exists to guard: a typo here used to be
			// indistinguishable in every log from not having set the variable at all.
			UE_LOG(LogMetaHumanLLM, Warning,
				TEXT("MHLLM_BACKEND='%s' is not recognised (expected 'ollama' or 'openai'); ")
				TEXT("ignoring it, so the backend will be decided by CI_HUB_URL/OPENAI_* or the project default."),
				*Backend);
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
	TryString(TEXT("KOKORO_ALIGNER_URL"), Settings.KokoroAlignerUrl);
	TryString(TEXT("WHISPER_ENDPOINT_PATH"), Settings.WhisperEndpointPath);
	TryString(TEXT("WHISPER_SERVER_URL"), Settings.WhisperServerUrl);
	TryString(TEXT("WHISPER_MODEL"), Settings.WhisperModel);
	TryString(TEXT("PIPER_SERVER_URL"), Settings.PiperServerUrl);

	// Persona.
	TryString(TEXT("MHLLM_SYSTEM_PROMPT"), Settings.SystemPrompt);

	// Conversation shape and memory.
	TryInt(TEXT("MHLLM_MAX_TURNS"), Settings.MaxRetainedTurns);
	TryBool(TEXT("MHLLM_MEMORY"), Settings.bPersistConversation);
	TryString(TEXT("MHLLM_MEMORY_PATH"), Settings.ConversationMemoryPath);

	// Request budget.
	TryFloat(TEXT("MHLLM_CHAT_TIMEOUT"), Settings.ChatRequestTimeoutSeconds);

	return Applied;
}

int32 FMetaHumanLLMEnvConfig::ApplyFromProcessEnv(UMetaHumanLLMSettings& Settings)
{
	return Apply(Settings, [](const TCHAR* Name)
	{
		return FPlatformMisc::GetEnvironmentVariable(Name);
	});
}
