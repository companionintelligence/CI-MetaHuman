#include "Voice/NullSpeechToText.h"
#include "MetaHumanLocalLLM.h"

void UNullSpeechToText::StartListening()
{
	UE_LOG(LogMetaHumanLLM, Verbose, TEXT("[NullSTT] StartListening (no-op, no STT plugin installed)"));
}

void UNullSpeechToText::StopListening()
{
	UE_LOG(LogMetaHumanLLM, Verbose, TEXT("[NullSTT] StopListening (no-op, no STT plugin installed)"));
}
