#include "Misc/AutomationTest.h"
#include "Settings/MetaHumanLLMEnvConfig.h"
#include "Settings/MetaHumanLLMSettings.h"
#include "UObject/Package.h"

BEGIN_DEFINE_SPEC(FEnvConfigSpec, "MetaHumanLocalLLM.Backend.EnvConfig", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FEnvConfigSpec)

// Verifies the container/env-var configuration resolution (the "ci-hub ollama support" seam)
// using an injected lookup, so no real process-environment mutation is needed.
//
// NOTE: UMetaHumanLLMSettings is a UObject and must be heap-allocated via NewObject — never
// stack-allocated (that trips a fatal error in UObject construction). Each test gets a fresh
// instance so mutations don't leak between tests.
void FEnvConfigSpec::Define()
{
	// Builds a lookup backed by an in-memory map.
	auto MakeLookup = [](const TMap<FString, FString>& Env)
	{
		return [Env](const TCHAR* Name) -> FString
		{
			const FString* Found = Env.Find(FString(Name));
			return Found ? *Found : FString();
		};
	};

	It("leaves defaults untouched when no variables are set", [this, MakeLookup]()
	{
		UMetaHumanLLMSettings* Settings = NewObject<UMetaHumanLLMSettings>(GetTransientPackage());
		const EChatBackendType DefaultBackend = Settings->BackendType;
		const FString DefaultOllama = Settings->OllamaBaseUrl;

		TMap<FString, FString> Env;
		const int32 Applied = FMetaHumanLLMEnvConfig::Apply(*Settings, MakeLookup(Env));

		TestEqual("nothing applied", Applied, 0);
		TestEqual("backend unchanged", Settings->BackendType, DefaultBackend);
		TestEqual("ollama url unchanged", Settings->OllamaBaseUrl, DefaultOllama);
		TestTrue("whisper remains opt-in", Settings->WhisperServerUrl.IsEmpty());
		TestTrue("piper remains opt-in", Settings->PiperServerUrl.IsEmpty());
	});

	It("points the agent at CI-hub and flips to the OpenAI-compatible backend", [this, MakeLookup]()
	{
		UMetaHumanLLMSettings* Settings = NewObject<UMetaHumanLLMSettings>(GetTransientPackage());
		Settings->BackendType = EChatBackendType::OllamaNative;

		TMap<FString, FString> Env;
		Env.Add(TEXT("CI_HUB_URL"), TEXT("https://hub.ci.internal/v1"));
		Env.Add(TEXT("CI_HUB_TOKEN"), TEXT("sk-hub-abc"));
		Env.Add(TEXT("CI_HUB_MODEL"), TEXT("llama3.2"));

		const int32 Applied = FMetaHumanLLMEnvConfig::Apply(*Settings, MakeLookup(Env));

		TestEqual("three applied", Applied, 3);
		TestEqual("backend flipped to OpenAI-compatible", Settings->BackendType, EChatBackendType::OpenAICompatible);
		TestEqual("hub url", Settings->OpenAIBaseUrl, TEXT("https://hub.ci.internal/v1"));
		TestEqual("hub token", Settings->OpenAIApiKey, TEXT("sk-hub-abc"));
		TestEqual("hub model", Settings->OpenAIModel, TEXT("llama3.2"));
	});

	It("honors an explicit MHLLM_BACKEND=ollama", [this, MakeLookup]()
	{
		UMetaHumanLLMSettings* Settings = NewObject<UMetaHumanLLMSettings>(GetTransientPackage());
		Settings->BackendType = EChatBackendType::OpenAICompatible;

		TMap<FString, FString> Env;
		Env.Add(TEXT("MHLLM_BACKEND"), TEXT("ollama"));

		FMetaHumanLLMEnvConfig::Apply(*Settings, MakeLookup(Env));
		TestEqual("backend set to ollama", Settings->BackendType, EChatBackendType::OllamaNative);
	});

	It("overrides the direct Ollama endpoint and model", [this, MakeLookup]()
	{
		UMetaHumanLLMSettings* Settings = NewObject<UMetaHumanLLMSettings>(GetTransientPackage());

		TMap<FString, FString> Env;
		Env.Add(TEXT("OLLAMA_BASE_URL"), TEXT("http://ollama:11434"));
		Env.Add(TEXT("OLLAMA_MODEL"), TEXT("qwen2.5"));

		FMetaHumanLLMEnvConfig::Apply(*Settings, MakeLookup(Env));
		TestEqual("ollama url", Settings->OllamaBaseUrl, TEXT("http://ollama:11434"));
		TestEqual("ollama model", Settings->OllamaModel, TEXT("qwen2.5"));
	});

	It("overrides the voice service URLs and system prompt", [this, MakeLookup]()
	{
		UMetaHumanLLMSettings* Settings = NewObject<UMetaHumanLLMSettings>(GetTransientPackage());

		TMap<FString, FString> Env;
		Env.Add(TEXT("WHISPER_SERVER_URL"), TEXT("http://whisper:8080"));
		Env.Add(TEXT("PIPER_SERVER_URL"), TEXT("http://piper:5000"));
		Env.Add(TEXT("MHLLM_SYSTEM_PROMPT"), TEXT("You are the CI kiosk agent."));

		const int32 Applied = FMetaHumanLLMEnvConfig::Apply(*Settings, MakeLookup(Env));
		TestEqual("three applied", Applied, 3);
		TestEqual("whisper", Settings->WhisperServerUrl, TEXT("http://whisper:8080"));
		TestEqual("piper", Settings->PiperServerUrl, TEXT("http://piper:5000"));
		TestEqual("prompt", Settings->SystemPrompt, TEXT("You are the CI kiosk agent."));
	});

	It("ignores an unrecognized backend value", [this, MakeLookup]()
	{
		UMetaHumanLLMSettings* Settings = NewObject<UMetaHumanLLMSettings>(GetTransientPackage());
		const EChatBackendType DefaultBackend = Settings->BackendType;

		TMap<FString, FString> Env;
		Env.Add(TEXT("MHLLM_BACKEND"), TEXT("nonsense"));

		FMetaHumanLLMEnvConfig::Apply(*Settings, MakeLookup(Env));
		TestEqual("backend unchanged for bad value", Settings->BackendType, DefaultBackend);
	});

	Describe("backend precedence", [this, MakeLookup]()
	{
		It("lets an explicit MHLLM_BACKEND=ollama beat an incidental CI_HUB_URL", [this, MakeLookup]()
		{
			// The regression this guards: the base compose ALWAYS sets CI_HUB_URL, so while its
			// presence overrode MHLLM_BACKEND, a deploy that plainly asked for Ollama silently
			// talked to the hub with a placeholder token and every turn failed with HTTP 405.
			UMetaHumanLLMSettings* Settings = NewObject<UMetaHumanLLMSettings>(GetTransientPackage());
			TMap<FString, FString> Env;
			Env.Add(TEXT("MHLLM_BACKEND"), TEXT("ollama"));
			Env.Add(TEXT("CI_HUB_URL"), TEXT("https://hub.example.com/v1"));
			FMetaHumanLLMEnvConfig::Apply(*Settings, MakeLookup(Env));

			TestEqual("stays on native Ollama", Settings->BackendType, EChatBackendType::OllamaNative);
			TestEqual("but still records the hub base url", Settings->OpenAIBaseUrl, FString(TEXT("https://hub.example.com/v1")));
		});

		It("still infers OpenAI from CI_HUB_URL when no backend is stated", [this, MakeLookup]()
		{
			UMetaHumanLLMSettings* Settings = NewObject<UMetaHumanLLMSettings>(GetTransientPackage());
			TMap<FString, FString> Env;
			Env.Add(TEXT("CI_HUB_URL"), TEXT("https://hub.example.com/v1"));
			FMetaHumanLLMEnvConfig::Apply(*Settings, MakeLookup(Env));

			TestEqual("infers OpenAI-compatible", Settings->BackendType, EChatBackendType::OpenAICompatible);
		});

		It("honours an explicit MHLLM_BACKEND=openai as well", [this, MakeLookup]()
		{
			UMetaHumanLLMSettings* Settings = NewObject<UMetaHumanLLMSettings>(GetTransientPackage());
			TMap<FString, FString> Env;
			Env.Add(TEXT("MHLLM_BACKEND"), TEXT("openai"));
			FMetaHumanLLMEnvConfig::Apply(*Settings, MakeLookup(Env));

			TestEqual("selects OpenAI-compatible", Settings->BackendType, EChatBackendType::OpenAICompatible);
		});
	});
}