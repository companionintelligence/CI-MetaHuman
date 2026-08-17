#include "Misc/AutomationTest.h"
#include "Voice/WhisperSpeechToText.h"

namespace
{
	// Searches a byte buffer for an ASCII pattern. Used instead of reconstructing an FString
	// across the whole multipart body, which would truncate at the embedded WAV's null bytes.
	bool ContainsAscii(const TArray<uint8>& Bytes, const char* Pattern)
	{
		const int32 PatLen = FCStringAnsi::Strlen(Pattern);
		if (PatLen == 0 || Bytes.Num() < PatLen)
		{
			return false;
		}
		for (int32 i = 0; i + PatLen <= Bytes.Num(); ++i)
		{
			bool bMatch = true;
			for (int32 j = 0; j < PatLen; ++j)
			{
				if (Bytes[i + j] != static_cast<uint8>(Pattern[j])) { bMatch = false; break; }
			}
			if (bMatch) { return true; }
		}
		return false;
	}
}

BEGIN_DEFINE_SPEC(FWhisperSpeechToTextSpec, "MetaHumanLocalLLM.Backend.WhisperSpeechToText", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FWhisperSpeechToTextSpec)

// Covers the pure, HTTP-free units of UWhisperSpeechToText: multipart request assembly and
// transcription-response parsing. The live mic capture and network POST are runtime-only.
void FWhisperSpeechToTextSpec::Define()
{
	Describe("BuildMultipartFormData", [this]()
	{
		It("wraps the wav bytes with the boundary, filename and content type", [this]()
		{
			// Include a 0x00 byte in the payload to prove the framing checks don't rely on
			// C-string termination (an earlier version reconstructed the whole body as an
			// FString and silently truncated at this null).
			TArray<uint8> Wav = { 'R', 'I', 'F', 'F', 0x00, 0x11, 0x22 };
			const FString Boundary = TEXT("BOUNDARY123");
			const TArray<uint8> Body = UWhisperSpeechToText::BuildMultipartFormData(Wav, Boundary, TEXT("audio.wav"));

			TestTrue("opens with boundary", ContainsAscii(Body, "--BOUNDARY123\r\n"));
			TestTrue("declares the file field", ContainsAscii(Body, "name=\"file\"; filename=\"audio.wav\""));
			TestTrue("declares wav content type", ContainsAscii(Body, "Content-Type: audio/wav"));
			TestTrue("includes response_format field", ContainsAscii(Body, "name=\"response_format\""));
			TestTrue("ends with closing boundary", ContainsAscii(Body, "--BOUNDARY123--\r\n"));
			// No model configured: the field must be absent ENTIRELY, not empty. whisper.cpp hosts
			// one model and an empty-valued part is not the same thing as no part at all.
			TestFalse("omits the model field when none is configured", ContainsAscii(Body, "name=\"model\""));
		});

		It("emits the model field only when one is configured", [this]()
		{
			// The OpenAI shape that Lemonade serves REQUIRES this field and rejects the request
			// without it, so omitting it made the entire STT path fail against the very server the
			// deployment points at -- with the resulting 4xx logged at Verbose, i.e. invisible.
			TArray<uint8> Wav = { 'R', 'I', 'F', 'F' };
			const TArray<uint8> Body = UWhisperSpeechToText::BuildMultipartFormData(
				Wav, TEXT("BOUNDARY123"), TEXT("audio.wav"), TEXT("Whisper-Base"));

			TestTrue("declares the model field", ContainsAscii(Body, "name=\"model\""));
			TestTrue("carries the configured value", ContainsAscii(Body, "Whisper-Base\r\n"));
			// Still well-formed: the new part sits before the closing boundary, not after it.
			TestTrue("still ends with the closing boundary", ContainsAscii(Body, "--BOUNDARY123--\r\n"));
		});

		It("embeds the exact wav payload bytes", [this]()
		{
			TArray<uint8> Wav = { 0xDE, 0xAD, 0xBE, 0xEF };
			const TArray<uint8> Body = UWhisperSpeechToText::BuildMultipartFormData(Wav, TEXT("B"), TEXT("a.wav"));

			// The payload must appear contiguously somewhere in the body.
			bool bFound = false;
			for (int32 i = 0; i + 3 < Body.Num(); ++i)
			{
				if (Body[i] == 0xDE && Body[i + 1] == 0xAD && Body[i + 2] == 0xBE && Body[i + 3] == 0xEF)
				{
					bFound = true;
					break;
				}
			}
			TestTrue("wav bytes present unmodified", bFound);
		});
	});

	Describe("MultipartContentType", [this]()
	{
		It("names the boundary", [this]()
		{
			TestEqual("content type", UWhisperSpeechToText::MultipartContentType(TEXT("XYZ")),
				FString(TEXT("multipart/form-data; boundary=XYZ")));
		});
	});

	Describe("ParseTranscriptionResponse", [this]()
	{
		It("extracts the text field", [this]()
		{
			FString Out;
			const bool bOk = UWhisperSpeechToText::ParseTranscriptionResponse(TEXT("{\"text\":\"hello world\"}"), Out);
			TestTrue("parsed", bOk);
			TestEqual("text", Out, TEXT("hello world"));
		});

		It("trims surrounding whitespace (whisper often pads with a leading space)", [this]()
		{
			FString Out;
			const bool bOk = UWhisperSpeechToText::ParseTranscriptionResponse(TEXT("{\"text\":\"  hi there \"}"), Out);
			TestTrue("parsed", bOk);
			TestEqual("trimmed text", Out, TEXT("hi there"));
		});

		It("fails on an empty text field", [this]()
		{
			FString Out;
			TestFalse("empty is a failure", UWhisperSpeechToText::ParseTranscriptionResponse(TEXT("{\"text\":\"   \"}"), Out));
		});

		It("fails on malformed json", [this]()
		{
			FString Out;
			TestFalse("garbage is a failure", UWhisperSpeechToText::ParseTranscriptionResponse(TEXT("not json"), Out));
		});

		It("fails when the text field is absent", [this]()
		{
			FString Out;
			TestFalse("missing field is a failure", UWhisperSpeechToText::ParseTranscriptionResponse(TEXT("{\"other\":1}"), Out));
		});
	});
}
