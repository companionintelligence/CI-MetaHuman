#include "Misc/AutomationTest.h"
#include "Voice/CIVisemeInterop.h"

BEGIN_DEFINE_SPEC(FVisemeInteropSpec, "MetaHumanLocalLLM.Backend.VisemeInterop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FVisemeInteropSpec)

// Tests the bridge between this puppet and the org's other embodied agents.
//
// The contracts under test were not invented here -- they were read out of
// CI-Spatial-Companion-WebXR, CI-Spatial-Companion-Unity and the archived WebSpatial client, which
// independently converged on the Oculus/OVR-15 viseme names and a { words, wtimes, wdurations }
// millisecond timing envelope. Keeping those byte-exact is what lets a timing source written for any
// sibling drive the MetaHuman with no adapter, so the exact spellings are asserted rather than
// assumed.
void FVisemeInteropSpec::Define()
{
	Describe("OVR-15 names", [this]()
	{
		It("has one name per EViseme, in enum order", [this]()
		{
			TestEqual("count", CIOvr::Names15().Num(), static_cast<int32>(EViseme::OU) + 1);
		});

		It("spells the names exactly as the sibling repos do", [this]()
		{
			// Compared as STRINGS, not FNames. Case is not incidental here: Oculus mixes uppercase
			// consonant classes with lowercase vowel and nasal ones, and that exact spelling is baked
			// into every Ready Player Me avatar's blendshape list -- verified in the Unity sibling's
			// CI_Brad.glb targetNames at mesh indices 1-15.
			//
			// An FName comparison would pass here even if the capitalisation were wrong, because
			// FName ignores case. The consumers that matter (JavaScript and C# siblings) do not, so
			// the assertion has to go through ToString() to mean anything.
			TestEqual("sil", CIOvr::ToOvrName(EViseme::Sil).ToString(), FString(TEXT("viseme_sil")));
			TestEqual("PP uppercase", CIOvr::ToOvrName(EViseme::PP).ToString(), FString(TEXT("viseme_PP")));
			TestEqual("kk lowercase", CIOvr::ToOvrName(EViseme::KK).ToString(), FString(TEXT("viseme_kk")));
			TestEqual("nn lowercase", CIOvr::ToOvrName(EViseme::NN).ToString(), FString(TEXT("viseme_nn")));
			TestEqual("aa lowercase", CIOvr::ToOvrName(EViseme::AA).ToString(), FString(TEXT("viseme_aa")));
			TestEqual("CH uppercase", CIOvr::ToOvrName(EViseme::CH).ToString(), FString(TEXT("viseme_CH")));
		});

		It("maps the three renamed vowel classes", [this]()
		{
			// The whole divergence between UE's EViseme and the sibling vocabulary. UE spells these
			// phonetically; the siblings use the bare Oculus letter.
			TestEqual("IH -> I", CIOvr::ToOvrName(EViseme::IH), FName(TEXT("viseme_I")));
			TestEqual("OH -> O", CIOvr::ToOvrName(EViseme::OH), FName(TEXT("viseme_O")));
			TestEqual("OU -> U", CIOvr::ToOvrName(EViseme::OU), FName(TEXT("viseme_U")));
		});

		It("round-trips every viseme", [this]()
		{
			for (int32 Index = 0; Index <= static_cast<int32>(EViseme::OU); ++Index)
			{
				const EViseme Original = static_cast<EViseme>(Index);
				EViseme RoundTripped = EViseme::Sil;
				TestTrue(*FString::Printf(TEXT("viseme %d resolves"), Index),
					CIOvr::FromOvrName(CIOvr::ToOvrName(Original), RoundTripped));
				TestEqual(*FString::Printf(TEXT("viseme %d round-trips"), Index),
					static_cast<uint8>(RoundTripped), static_cast<uint8>(Original));
			}
		});

		It("rejects unknown names instead of guessing", [this]()
		{
			// An unrecognised name from an upstream service must surface, not silently become
			// silence -- otherwise a renamed viseme upstream looks like a mouth that stopped moving.
			EViseme Unused = EViseme::AA;
			TestFalse("empty", CIOvr::FromOvrName(FName(), Unused));
			TestFalse("not a viseme", CIOvr::FromOvrName(TEXT("jawOpen"), Unused));
			TestFalse("truncated", CIOvr::FromOvrName(TEXT("viseme_"), Unused));
			TestFalse("unprefixed", CIOvr::FromOvrName(TEXT("PP"), Unused));
		});

		It("accepts differently-cased input, because FName does", [this]()
		{
			// Documented rather than fought. FromOvrName resolves through FName, whose comparison
			// ignores case, so "viseme_KK" maps to EViseme::KK. That is a useful leniency on the
			// INBOUND edge -- a sibling that capitalises differently still drives the face -- and it
			// costs nothing, because the OUTBOUND spelling is fixed by Names15() and asserted above.
			EViseme Parsed = EViseme::Sil;
			TestTrue("viseme_KK resolves", CIOvr::FromOvrName(TEXT("viseme_KK"), Parsed));
			TestEqual("to KK", static_cast<uint8>(Parsed), static_cast<uint8>(EViseme::KK));
		});
	});

	Describe("FCIWordTimings::FromJson", [this]()
	{
		It("parses the sibling envelope", [this]()
		{
			FCIWordTimings Timings;
			const bool bParsed = FCIWordTimings::FromJson(
				TEXT(R"({"words":["hello","world"],"wtimes":[0,500],"wdurations":[400,600]})"), Timings);

			TestTrue("parsed", bParsed);
			TestTrue("valid", Timings.IsValid());
			TestEqual("word count", Timings.Num(), 2);
			TestEqual("first word", Timings.Words[0], FString(TEXT("hello")));
			TestEqual("second start", Timings.WTimesMs[1], 500);
			TestEqual("second duration", Timings.WDurationsMs[1], 600);
		});

		It("accepts fractional milliseconds", [this]()
		{
			// The WebSpatial sibling fabricates timings from a 150 WPM estimate and emits fractional
			// milliseconds; an integer-typed read would reject perfectly good data.
			FCIWordTimings Timings;
			TestTrue("parsed", FCIWordTimings::FromJson(
				TEXT(R"({"words":["hi"],"wtimes":[12.7],"wdurations":[333.4]})"), Timings));
			TestEqual("rounded start", Timings.WTimesMs[0], 13);
			TestEqual("rounded duration", Timings.WDurationsMs[0], 333);
		});

		It("ignores unknown fields", [this]()
		{
			FCIWordTimings Timings;
			TestTrue("parsed", FCIWordTimings::FromJson(
				TEXT(R"({"words":["hi"],"wtimes":[0],"wdurations":[100],"voice":"marius","sampleRate":22050})"), Timings));
			TestEqual("count", Timings.Num(), 1);
		});

		It("accepts an empty utterance", [this]()
		{
			FCIWordTimings Timings;
			TestTrue("parsed", FCIWordTimings::FromJson(TEXT(R"({"words":[],"wtimes":[],"wdurations":[]})"), Timings));
			TestEqual("count", Timings.Num(), 0);
			TestEqual("total", Timings.TotalSeconds(), 0.0f);
		});

		It("rejects mismatched array lengths", [this]()
		{
			// Deliberately a rejection rather than a truncation: silently using the shortest array
			// would desynchronise the mouth from the audio and read as a lip-sync bug rather than the
			// data bug it is.
			FCIWordTimings Timings;
			TestFalse("short wdurations", FCIWordTimings::FromJson(
				TEXT(R"({"words":["a","b"],"wtimes":[0,10],"wdurations":[5]})"), Timings));
		});

		It("rejects malformed and incomplete payloads", [this]()
		{
			FCIWordTimings Timings;
			TestFalse("not json", FCIWordTimings::FromJson(TEXT("not json at all"), Timings));
			TestFalse("empty string", FCIWordTimings::FromJson(TEXT(""), Timings));
			TestFalse("missing wdurations", FCIWordTimings::FromJson(
				TEXT(R"({"words":["a"],"wtimes":[0]})"), Timings));
			TestFalse("wrong value type", FCIWordTimings::FromJson(
				TEXT(R"({"words":[5],"wtimes":[0],"wdurations":[1]})"), Timings));
		});

		It("leaves the output untouched when parsing fails", [this]()
		{
			FCIWordTimings Timings;
			TestTrue("seed parse", FCIWordTimings::FromJson(
				TEXT(R"({"words":["keep"],"wtimes":[0],"wdurations":[100]})"), Timings));
			TestFalse("bad parse", FCIWordTimings::FromJson(TEXT("{"), Timings));
			TestEqual("previous value survives", Timings.Words[0], FString(TEXT("keep")));
		});
	});

	Describe("FCIWordTimings round-trip", [this]()
	{
		It("survives ToJson then FromJson", [this]()
		{
			FCIWordTimings Original;
			Original.Words = { TEXT("the"), TEXT("quick"), TEXT("fox") };
			Original.WTimesMs = { 0, 300, 900 };
			Original.WDurationsMs = { 250, 550, 400 };

			FCIWordTimings Restored;
			TestTrue("parsed", FCIWordTimings::FromJson(Original.ToJson(), Restored));
			TestEqual("count", Restored.Num(), Original.Num());
			for (int32 Index = 0; Index < Original.Num(); ++Index)
			{
				TestEqual("word", Restored.Words[Index], Original.Words[Index]);
				TestEqual("time", Restored.WTimesMs[Index], Original.WTimesMs[Index]);
				TestEqual("duration", Restored.WDurationsMs[Index], Original.WDurationsMs[Index]);
			}
		});
	});

	Describe("FCIWordTimings::TotalSeconds", [this]()
	{
		It("measures to the end of the last word, including gaps", [this]()
		{
			// Not the sum of durations: senders leave gaps between words, and the utterance length
			// has to include them or a track will be rescaled short.
			FCIWordTimings Timings;
			Timings.Words = { TEXT("a"), TEXT("b") };
			Timings.WTimesMs = { 0, 1000 };
			Timings.WDurationsMs = { 200, 500 };
			TestEqual("total", Timings.TotalSeconds(), 1.5f, 0.0001f);
		});
	});

	Describe("FCIWordTimings::ToVisemeTrack", [this]()
	{
		It("produces an empty track for empty timings", [this]()
		{
			TestEqual("empty", FCIWordTimings().ToVisemeTrack().Num(), 0);
		});

		It("fills each word's measured window", [this]()
		{
			FCIWordTimings Timings;
			Timings.Words = { TEXT("hi") };
			Timings.WTimesMs = { 0 };
			Timings.WDurationsMs = { 400 };

			const TArray<FVisemeSpan> Track = Timings.ToVisemeTrack();
			TestTrue("has spans", Track.Num() > 0);
			TestEqual("starts at zero", Track[0].StartSeconds, 0.0f, 0.0001f);
			TestEqual("ends at the word end", Track.Last().EndSeconds(), 0.4f, 0.0001f);
		});

		It("inserts silence in the gaps between words", [this]()
		{
			// The behaviour that makes externally-timed speech read correctly. Without silence in the
			// pauses the mouth keeps moving between words and the whole utterance looks like chewing.
			FCIWordTimings Timings;
			Timings.Words = { TEXT("a"), TEXT("b") };
			Timings.WTimesMs = { 0, 1000 };
			Timings.WDurationsMs = { 200, 200 };

			const TArray<FVisemeSpan> Track = Timings.ToVisemeTrack();

			bool bFoundSilenceInGap = false;
			for (const FVisemeSpan& Span : Track)
			{
				if (Span.Viseme == EViseme::Sil && Span.StartSeconds >= 0.2f - 0.0001f && Span.EndSeconds() <= 1.0f + 0.0001f)
				{
					bFoundSilenceInGap = true;
				}
			}
			TestTrue("silence covers the 0.2s-1.0s gap", bFoundSilenceInGap);
		});

		It("produces a monotonic, non-overlapping track", [this]()
		{
			// FVisemePlanner::SampleAt assumes a sorted, non-overlapping track; violating that makes
			// the mouth jump backwards mid-utterance.
			FCIWordTimings Timings;
			Timings.Words = { TEXT("hello"), TEXT("there"), TEXT("world") };
			Timings.WTimesMs = { 0, 500, 1200 };
			Timings.WDurationsMs = { 400, 600, 500 };

			const TArray<FVisemeSpan> Track = Timings.ToVisemeTrack();
			TestTrue("has spans", Track.Num() > 1);
			for (int32 Index = 1; Index < Track.Num(); ++Index)
			{
				TestTrue(*FString::Printf(TEXT("span %d starts at or after span %d ends"), Index, Index - 1),
					Track[Index].StartSeconds >= Track[Index - 1].EndSeconds() - 0.0001f);
				TestTrue(*FString::Printf(TEXT("span %d has non-negative duration"), Index),
					Track[Index].DurationSeconds >= 0.0f);
			}
		});

		It("does not drift against the source timing", [this]()
		{
			// The point of using measured word times instead of FVisemePlanner's 165 WPM estimate:
			// the track cannot accumulate error, because every word is pinned to its own window.
			FCIWordTimings Timings;
			Timings.Words = { TEXT("one"), TEXT("two"), TEXT("three"), TEXT("four"), TEXT("five") };
			Timings.WTimesMs = { 0, 400, 800, 1200, 1600 };
			Timings.WDurationsMs = { 350, 350, 350, 350, 350 };

			const TArray<FVisemeSpan> Track = Timings.ToVisemeTrack();
			TestEqual("track ends exactly at the last word's end", Track.Last().EndSeconds(),
				Timings.TotalSeconds(), 0.001f);
		});

		It("collapses doubled letters into one held shape", [this]()
		{
			// "ll" in "hello" is one mouth shape held, not two identical adjacent spans; emitting
			// both makes the mouth stutter on a shape it should simply sustain.
			FCIWordTimings Timings;
			Timings.Words = { TEXT("hello") };
			Timings.WTimesMs = { 0 };
			Timings.WDurationsMs = { 500 };

			const TArray<FVisemeSpan> Track = Timings.ToVisemeTrack();
			for (int32 Index = 1; Index < Track.Num(); ++Index)
			{
				TestTrue(*FString::Printf(TEXT("span %d differs from its predecessor"), Index),
					Track[Index].Viseme != Track[Index - 1].Viseme);
			}
		});

		It("treats punctuation-only words as silence without losing their time", [this]()
		{
			FCIWordTimings Timings;
			Timings.Words = { TEXT("ok"), TEXT("--"), TEXT("go") };
			Timings.WTimesMs = { 0, 300, 600 };
			Timings.WDurationsMs = { 250, 250, 250 };

			const TArray<FVisemeSpan> Track = Timings.ToVisemeTrack();
			TestEqual("track still spans the whole utterance", Track.Last().EndSeconds(), 0.85f, 0.001f);
		});
	});
}
