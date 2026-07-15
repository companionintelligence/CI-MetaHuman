#include "Voice/NullTextToSpeech.h"
#include "MetaHumanLocalLLM.h"

void UNullTextToSpeech::Speak(const FString& Text)
{
	UE_LOG(LogMetaHumanLLM, Verbose, TEXT("[NullTTS] Speak (no audio plugin installed): %s"), *Text);
	OnSpeechFinished.Broadcast(Text);
}

void UNullTextToSpeech::StopSpeaking()
{
}
