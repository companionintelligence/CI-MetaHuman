#pragma once

#include "Modules/ModuleManager.h"

// CICompanion — reusable UE plugin (5.5-5.8) with the LLM backends, voice/viseme, avatar
// orchestrator, emotion mapping, HUD, settings, and env-config. Drop it into any Unreal
// project. The log category keeps its LogMetaHumanLLM name for continuity.
DECLARE_LOG_CATEGORY_EXTERN(LogMetaHumanLLM, Log, All);

class FCICompanionModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
