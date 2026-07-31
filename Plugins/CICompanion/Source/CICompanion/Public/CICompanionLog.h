#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

// Shared log categories.
//
// These live here because DEFINE_LOG_CATEGORY_STATIC is per-TRANSLATION-UNIT, and Unreal's unity
// build concatenates several .cpp files into one. Two files each defining LogCompanionFace
// compiled fine on Linux/clang (which happened to group them apart) and failed on Windows/MSVC
// with "error C2011: type redefinition", cascading into a dozen unrelated-looking C2664s at
// every use site. The build that catches this is the one that groups the files together, which
// is a property of the toolchain and file ordering rather than of the code -- so the only robust
// fix is to declare once and define once.
CICOMPANION_API DECLARE_LOG_CATEGORY_EXTERN(LogCompanionFace, Log, All);
CICOMPANION_API DECLARE_LOG_CATEGORY_EXTERN(LogCompanionRuntime, Log, All);
CICOMPANION_API DECLARE_LOG_CATEGORY_EXTERN(LogCompanionScene, Log, All);
CICOMPANION_API DECLARE_LOG_CATEGORY_EXTERN(LogCompanionStage, Log, All);
CICOMPANION_API DECLARE_LOG_CATEGORY_EXTERN(LogCompanionVoice, Log, All);
