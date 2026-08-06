#include "Misc/AutomationTest.h"
#include "Voice/KokoroTextToSpeech.h"

BEGIN_DEFINE_SPEC(FKokoroSpec, "MetaHumanLocalLLM.Backend.Kokoro", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FKokoroSpec)

// Pure string/JSON construction — no server, no audio device.
void FKokoroSpec::Define()
{
	Describe("BuildEndpointUrl", [this]()
	{
		It("appends the Lemonade API path to a bare base url", [this]()
		{
			TestEqual("bare host", UKokoroTextToSpeech::BuildEndpointUrl(TEXT("http://h:13305")),
				FString(TEXT("http://h:13305/api/v1/audio/speech")));
		});

		It("does not double up when the base already ends at the api root", [this]()
		{
			// Operators paste whichever URL Lemonade printed at them; silently producing
			// /api/v1/api/v1/... would 404 with no clue why.
			TestEqual("api root", UKokoroTextToSpeech::BuildEndpointUrl(TEXT("http://h:13305/api/v1")),
				FString(TEXT("http://h:13305/api/v1/audio/speech")));
		});

		It("tolerates trailing slashes", [this]()
		{
			TestEqual("trailing", UKokoroTextToSpeech::BuildEndpointUrl(TEXT("http://h:13305///")),
				FString(TEXT("http://h:13305/api/v1/audio/speech")));
		});
	});

	Describe("BuildRequestBody", [this]()
	{
		It("uses the OpenAI audio/speech field names", [this]()
		{
			const FString Body = UKokoroTextToSpeech::BuildRequestBody(TEXT("hello"), TEXT("m"), TEXT("v"));
			TestTrue("input field", Body.Contains(TEXT("\"input\"")));
			TestTrue("model field", Body.Contains(TEXT("\"model\"")));
			TestTrue("voice field", Body.Contains(TEXT("\"voice\"")));
			TestTrue("carries the text", Body.Contains(TEXT("hello")));
		});

		It("pins response_format to wav, the only format we can decode", [this]()
		{
			// The KEY AND THE VALUE together. Asserting on a bare "wav" substring passes if the
			// field is renamed, dropped, or if "wav" appears anywhere else in the body at all --
			// including inside the model or voice name a caller passed in. The failure that would
			// slip through is the expensive one: the server happily returns mp3, FWavAudioReader
			// declines to decode it, and she goes silent with a well-formed request in the log.
			const FString Body = UKokoroTextToSpeech::BuildRequestBody(TEXT("hi"), TEXT("m"), TEXT("v"));

			// Whitespace stripped before matching, because BuildRequestBody uses the DEFAULT
			// TJsonWriterFactory policy, which PRETTY-prints -- so the literal on the wire is
			// `"response_format": "wav"` with a space that a naive `"key":"value"` assertion misses.
			// Normalising here pins the key/value pair without also pinning the print policy.
			const FString Compact = Body
				.Replace(TEXT(" "), TEXT(""))
				.Replace(TEXT("\t"), TEXT(""))
				.Replace(TEXT("\r"), TEXT(""))
				.Replace(TEXT("\n"), TEXT(""));

			TestTrue("response_format is pinned to wav",
				Compact.Contains(TEXT("\"response_format\":\"wav\"")));
		});

		It("escapes quotes rather than emitting broken json", [this]()
		{
			const FString Body = UKokoroTextToSpeech::BuildRequestBody(TEXT("say \"hi\""), TEXT("m"), TEXT("v"));
			TestTrue("escaped", Body.Contains(TEXT("\\\"")));
		});
	});
}
