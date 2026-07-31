#include "Misc/AutomationTest.h"
#include "Voice/KokoroPhonemeAligner.h"

BEGIN_DEFINE_SPEC(FKokoroPhonemeAlignerSpec, "MetaHumanLocalLLM.Backend.KokoroAligner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FKokoroPhonemeAlignerSpec)

// Pure string/JSON work — no server is contacted. The response fixtures below are trimmed copies
// of a REAL reply from the sidecar on beta-1 (Deployment/kokoro-align/app.py), not invented shapes,
// because a parser tested against an imagined schema passes and then finds nothing in production.
void FKokoroPhonemeAlignerSpec::Define()
{
	Describe("BuildEndpointUrl", [this]()
	{
		It("appends the sidecar's endpoint to a bare base url", [this]()
		{
			TestEqual("bare host", UKokoroPhonemeAligner::BuildEndpointUrl(TEXT("http://127.0.0.1:13400")),
				FString(TEXT("http://127.0.0.1:13400/speak")));
		});

		It("does not double up when the base already names the endpoint", [this]()
		{
			TestEqual("already pathed", UKokoroPhonemeAligner::BuildEndpointUrl(TEXT("http://h:13400/speak")),
				FString(TEXT("http://h:13400/speak")));
		});

		It("tolerates trailing slashes", [this]()
		{
			TestEqual("trailing", UKokoroPhonemeAligner::BuildEndpointUrl(TEXT("http://h:13400///")),
				FString(TEXT("http://h:13400/speak")));
		});
	});

	Describe("BuildRequestBody", [this]()
	{
		It("sends the sidecar's field names, not the OpenAI ones", [this]()
		{
			// The TTS endpoint takes {"input": ...}; this one takes {"text": ...}. Sending the
			// wrong name yields a 422 that looks like the service being down.
			const FString Body = UKokoroPhonemeAligner::BuildRequestBody(TEXT("hello"), TEXT("af_sky"));
			TestTrue("text field", Body.Contains(TEXT("\"text\"")));
			TestFalse("not the openai field", Body.Contains(TEXT("\"input\"")));
			TestTrue("carries the text", Body.Contains(TEXT("hello")));
		});

		It("carries the voice, so the timing describes the audio being played", [this]()
		{
			const FString Body = UKokoroPhonemeAligner::BuildRequestBody(TEXT("hi"), TEXT("af_sky"));
			TestTrue("voice field", Body.Contains(TEXT("\"voice\"")));
			TestTrue("the configured voice", Body.Contains(TEXT("af_sky")));
		});

		It("escapes quotes rather than emitting broken json", [this]()
		{
			const FString Body = UKokoroPhonemeAligner::BuildRequestBody(TEXT("say \"hi\""), TEXT("af_sky"));
			TestTrue("escaped", Body.Contains(TEXT("\\\"")));
		});
	});

	Describe("ParseAlignmentResponse", [this]()
	{
		// A real response, shortened: the leading entry is Kokoro's pad token, which carries no
		// symbol and half a second of silence.
		const FString Valid =
			FString(TEXT(R"({"sample_rate": 24000, "audio_seconds": 4.75, "frame_rate": 40.0, "phonemes": "wˈɛlkəm", "spans": [)"))
			+ TEXT(R"({"symbol": "", "start": 0.0, "duration": 0.4773, "frames": 19.09},)")
			+ TEXT(R"({"symbol": "w", "start": 0.4773, "duration": 0.0451, "frames": 1.80},)")
			+ TEXT(R"({"symbol": "ˈ", "start": 0.5224, "duration": 0.05, "frames": 2.00},)")
			+ TEXT(R"({"symbol": "ɛ", "start": 0.5724, "duration": 0.05, "frames": 1.99}]})");

		It("reads the spans and the clip length out of a real response", [this, Valid]()
		{
			TArray<FPhonemeSpan> Spans;
			float Seconds = 0.0f;
			TestTrue("parsed", UKokoroPhonemeAligner::ParseAlignmentResponse(Valid, Spans, Seconds));
			TestEqual("span count", Spans.Num(), 4);
			TestTrue("audio length", FMath::IsNearlyEqual(Seconds, 4.75f, 0.001f));
			TestTrue("first start", FMath::IsNearlyEqual(Spans[0].StartSeconds, 0.0f, 0.001f));
			TestTrue("second start", FMath::IsNearlyEqual(Spans[1].StartSeconds, 0.4773f, 0.001f));
			TestTrue("second duration", FMath::IsNearlyEqual(Spans[1].DurationSeconds, 0.0451f, 0.001f));
			TestEqual("symbol survives", Spans[1].Symbol, FString(TEXT("w")));
		});

		It("keeps the symbol-less pad span rather than dropping it", [this, Valid]()
		{
			// It is half a second of leading silence. Dropping it would start every reply's mouth
			// half a second early, which is worse than the estimate this path replaces.
			TArray<FPhonemeSpan> Spans;
			float Seconds = 0.0f;
			UKokoroPhonemeAligner::ParseAlignmentResponse(Valid, Spans, Seconds);
			TestTrue("pad kept", Spans.Num() > 0 && Spans[0].Symbol.IsEmpty());
			TestTrue("pad has real duration", Spans[0].DurationSeconds > 0.4f);
		});

		It("preserves the unicode IPA symbols the viseme mapper keys on", [this, Valid]()
		{
			// FVisemePlanner::VisemeForIpa compares against literal IPA. A parser that mangled the
			// encoding would silently map every vowel to silence.
			TArray<FPhonemeSpan> Spans;
			float Seconds = 0.0f;
			UKokoroPhonemeAligner::ParseAlignmentResponse(Valid, Spans, Seconds);
			TestEqual("open-e survived", Spans.Last().Symbol, FString(TEXT("ɛ")));
		});

		It("rejects text that is not json at all", [this]()
		{
			TArray<FPhonemeSpan> Spans;
			float Seconds = 0.0f;
			TestFalse("rejected", UKokoroPhonemeAligner::ParseAlignmentResponse(
				TEXT("<html>502 Bad Gateway</html>"), Spans, Seconds));
			TestEqual("nothing produced", Spans.Num(), 0);
		});

		It("rejects truncated json", [this]()
		{
			TArray<FPhonemeSpan> Spans;
			float Seconds = 0.0f;
			TestFalse("rejected", UKokoroPhonemeAligner::ParseAlignmentResponse(
				TEXT("{\"spans\": [{\"symbol\": \"w\", \"start\": 0.0,"), Spans, Seconds));
		});

		It("rejects an empty span list", [this]()
		{
			TArray<FPhonemeSpan> Spans;
			float Seconds = 0.0f;
			TestFalse("rejected", UKokoroPhonemeAligner::ParseAlignmentResponse(
				TEXT("{\"audio_seconds\": 1.0, \"spans\": []}"), Spans, Seconds));
			TestEqual("nothing produced", Spans.Num(), 0);
		});

		It("rejects a response with no spans field", [this]()
		{
			TArray<FPhonemeSpan> Spans;
			float Seconds = 0.0f;
			TestFalse("rejected", UKokoroPhonemeAligner::ParseAlignmentResponse(
				TEXT("{\"audio_seconds\": 1.0}"), Spans, Seconds));
		});

		It("rejects the sidecar's 200-with-an-error shape", [this]()
		{
			// app.py answers an unpronounceable input with HTTP 200 and {"error": ...}, so a
			// status-code check alone would treat it as a successful alignment of nothing.
			TArray<FPhonemeSpan> Spans;
			float Seconds = 0.0f;
			TestFalse("rejected", UKokoroPhonemeAligner::ParseAlignmentResponse(
				TEXT("{\"error\": \"no pronounceable tokens\", \"text\": \"...\"}"), Spans, Seconds));
		});

		It("rejects an alignment whose spans all have zero duration", [this]()
		{
			// Parseable and empty of information. Passing it on would report the accurate path and
			// then hold the mouth shut for the whole reply.
			TArray<FPhonemeSpan> Spans;
			float Seconds = 0.0f;
			TestFalse("rejected", UKokoroPhonemeAligner::ParseAlignmentResponse(
				TEXT("{\"spans\": [{\"symbol\": \"w\", \"start\": 0.0, \"duration\": 0.0}]}"), Spans, Seconds));
			TestEqual("nothing produced", Spans.Num(), 0);
		});

		It("skips malformed entries but keeps the usable ones", [this]()
		{
			TArray<FPhonemeSpan> Spans;
			float Seconds = 0.0f;
			const bool bOk = UKokoroPhonemeAligner::ParseAlignmentResponse(
				TEXT(R"({"spans": ["not an object", {"symbol": "w"}, {"symbol": "a", "start": 0.1, "duration": 0.2}]})"),
				Spans, Seconds);
			TestTrue("parsed", bOk);
			TestEqual("only the well-formed span", Spans.Num(), 1);
			TestEqual("and it is the right one", Spans[0].Symbol, FString(TEXT("a")));
		});

		It("tolerates a missing audio_seconds", [this]()
		{
			// Only used for the agreement log line, so its absence must not cost the alignment.
			TArray<FPhonemeSpan> Spans;
			float Seconds = -1.0f;
			TestTrue("parsed", UKokoroPhonemeAligner::ParseAlignmentResponse(
				TEXT("{\"spans\": [{\"symbol\": \"a\", \"start\": 0.0, \"duration\": 0.2}]}"), Spans, Seconds));
			TestEqual("defaulted to zero", Seconds, 0.0f);
		});
	});
}
