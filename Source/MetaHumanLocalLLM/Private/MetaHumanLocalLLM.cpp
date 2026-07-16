#include "Modules/ModuleManager.h"

// Thin primary game module — exists only because a code-based UE project requires one.
// All companion functionality lives in the CICompanion plugin.
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, MetaHumanLocalLLM, "MetaHumanLocalLLM");
