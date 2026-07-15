#include "MetaHumanLocalLLM.h"
#include "Settings/MetaHumanLLMSettings.h"
#include "Settings/MetaHumanLLMEnvConfig.h"

DEFINE_LOG_CATEGORY(LogMetaHumanLLM);

void FMetaHumanLocalLLMModule::StartupModule()
{
	// Apply container/environment overrides onto the developer settings so a packaged build
	// can be pointed at CI-hub's Ollama (and the voice services) purely via env vars.
	if (UMetaHumanLLMSettings* Settings = GetMutableDefault<UMetaHumanLLMSettings>())
	{
		const int32 Applied = FMetaHumanLLMEnvConfig::ApplyFromProcessEnv(*Settings);
		if (Applied > 0)
		{
			UE_LOG(LogMetaHumanLLM, Log, TEXT("MetaHumanLocalLLM: applied %d environment override(s) to settings"), Applied);
		}
	}

	UE_LOG(LogMetaHumanLLM, Log, TEXT("MetaHumanLocalLLM module started"));
}

void FMetaHumanLocalLLMModule::ShutdownModule()
{
}

IMPLEMENT_PRIMARY_GAME_MODULE(FMetaHumanLocalLLMModule, MetaHumanLocalLLM, "MetaHumanLocalLLM");
