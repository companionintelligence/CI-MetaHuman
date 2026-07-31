#include "CICompanionLog.h"

// One definition each, for the whole module.
DEFINE_LOG_CATEGORY(LogCompanionFace);
DEFINE_LOG_CATEGORY(LogCompanionRuntime);
// Scene dressing that is bolted onto a headlessly-authored map at BeginPlay (tape reels today).
// UTapeReelSpinner logged to LogCompanionScene without anything ever declaring it, so the module
// did not compile at all -- the file was committed after the last packaging run, so no build had
// yet touched it.
DEFINE_LOG_CATEGORY(LogCompanionScene);
DEFINE_LOG_CATEGORY(LogCompanionStage);
DEFINE_LOG_CATEGORY(LogCompanionVoice);
