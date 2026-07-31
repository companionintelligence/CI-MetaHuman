#include "Misc/AutomationTest.h"
#include "Voice/VisemePlanner.h"

BEGIN_DEFINE_SPEC(FVisemePlannerSpec, "MetaHumanLocalLLM.Backend.VisemePlanner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FVisemePlannerSpec)

// Pure text/arithmetic — no rig, no audio, no world — so this runs headlessly with the rest of
// the backend suite.
void FVisemePlannerSpec::Define()
{
	Describe("PlanFromText", [this]()
	{
		It("produces nothing for empty text", [this]()
		{
			TestEqual("no spans", FVisemePlanner::PlanFromText(TEXT("")).Num(), 0);
		});

		It("produces a track for ordinary speech", [this]()
		{
			const TArray<FVisemeSpan> Track = FVisemePlanner::PlanFromText(TEXT("Hello there"));
			TestTrue("has spans", Track.Num() > 0);
			TestTrue("has duration", FVisemePlanner::TrackDuration(Track) > 0.0f);
		});

		It("lays spans end to end with no gaps or overlaps", [this]()
		{
			const TArray<FVisemeSpan> Track = FVisemePlanner::PlanFromText(TEXT("A quick brown fox."));
			for (int32 i = 1; i < Track.Num(); ++i)
			{
				TestTrue(FString::Printf(TEXT("span %d starts where %d ended"), i, i - 1),
					FMath::IsNearlyEqual(Track[i].StartSeconds, Track[i - 1].EndSeconds(), 0.001f));
			}
		});

		It("ends closed so the face returns to rest", [this]()
		{
			const TArray<FVisemeSpan> Track = FVisemePlanner::PlanFromText(TEXT("Hello"));
			TestTrue("has spans", Track.Num() > 0);
			TestEqual("last span is silence", Track.Last().Viseme, EViseme::Sil);
		});

		It("speaks longer text for longer", [this]()
		{
			const float Short = FVisemePlanner::TrackDuration(FVisemePlanner::PlanFromText(TEXT("Hi.")));
			const float Long = FVisemePlanner::TrackDuration(
				FVisemePlanner::PlanFromText(TEXT("Hi, this is a considerably longer sentence.")));
			TestTrue("longer text takes longer", Long > Short);
		});

		It("slows down for a faster speaking rate and vice versa", [this]()
		{
			const float Fast = FVisemePlanner::TrackDuration(FVisemePlanner::PlanFromText(TEXT("Hello there"), 300.0f));
			const float Slow = FVisemePlanner::TrackDuration(FVisemePlanner::PlanFromText(TEXT("Hello there"), 100.0f));
			TestTrue("slower rate yields a longer track", Slow > Fast);
		});

		It("collapses digraphs into one shape rather than two", [this]()
		{
			// "th" must not read as t-then-h; that is the difference between a mouth that looks
			// like speech and one that looks like it is chewing letters.
			const TArray<FVisemeSpan> Track = FVisemePlanner::PlanFromText(TEXT("th"));
			bool bFoundTH = false;
			for (const FVisemeSpan& S : Track)
			{
				bFoundTH |= (S.Viseme == EViseme::TH);
			}
			TestTrue("TH viseme emitted", bFoundTH);
		});

		It("pauses longer at a full stop than at a space", [this]()
		{
			const float WithStop = FVisemePlanner::TrackDuration(FVisemePlanner::PlanFromText(TEXT("ab. cd")));
			const float WithSpace = FVisemePlanner::TrackDuration(FVisemePlanner::PlanFromText(TEXT("ab  cd")));
			TestTrue("sentence end pauses longer", WithStop > WithSpace);
		});
	});

	Describe("PlanForDuration", [this]()
	{
		It("rescales to the measured audio length", [this]()
		{
			const TArray<FVisemeSpan> Track = FVisemePlanner::PlanForDuration(TEXT("Hello there, visitor."), 3.0f);
			TestTrue("has spans", Track.Num() > 0);
			TestTrue("matches target", FMath::IsNearlyEqual(FVisemePlanner::TrackDuration(Track), 3.0f, 0.01f));
		});

		It("tolerates a zero target rather than dividing by it", [this]()
		{
			const TArray<FVisemeSpan> Track = FVisemePlanner::PlanForDuration(TEXT("Hello"), 0.0f);
			TestTrue("still returns the unscaled plan", Track.Num() > 0);
		});
	});

	Describe("SampleAt", [this]()
	{
		It("is silent before and after the track", [this]()
		{
			const TArray<FVisemeSpan> Track = FVisemePlanner::PlanFromText(TEXT("Hello"));
			TestEqual("before", FVisemePlanner::SampleAt(Track, -1.0f).Num(), 0);
			TestEqual("after", FVisemePlanner::SampleAt(Track, FVisemePlanner::TrackDuration(Track) + 1.0f).Num(), 0);
		});

		It("returns weights within range across the whole utterance", [this]()
		{
			const TArray<FVisemeSpan> Track = FVisemePlanner::PlanFromText(TEXT("Hello there, visitor."));
			const float Duration = FVisemePlanner::TrackDuration(Track);
			for (float T = 0.0f; T < Duration; T += Duration / 40.0f)
			{
				for (const TPair<EViseme, float>& Pair : FVisemePlanner::SampleAt(Track, T))
				{
					TestTrue("weight in [0,1]", Pair.Value >= 0.0f && Pair.Value <= 1.0f);
				}
			}
		});

		It("blends between neighbours instead of snapping", [this]()
		{
			// Two shapes active at once somewhere in the track is what stops the mouth popping
			// between discrete poses.
			const TArray<FVisemeSpan> Track = FVisemePlanner::PlanFromText(TEXT("aeiou"));
			const float Duration = FVisemePlanner::TrackDuration(Track);
			bool bSawBlend = false;
			for (float T = 0.0f; T < Duration; T += 0.005f)
			{
				bSawBlend |= (FVisemePlanner::SampleAt(Track, T).Num() > 1);
			}
			TestTrue("some frame blends two visemes", bSawBlend);
		});
	});

	Describe("VisemeForChar", [this]()
	{
		It("maps bilabials to the closed shape", [this]()
		{
			TestEqual("p", FVisemePlanner::VisemeForChar(TEXT('p')), EViseme::PP);
			TestEqual("b", FVisemePlanner::VisemeForChar(TEXT('b')), EViseme::PP);
			TestEqual("m", FVisemePlanner::VisemeForChar(TEXT('m')), EViseme::PP);
		});

		It("maps vowels to open shapes", [this]()
		{
			TestEqual("a", FVisemePlanner::VisemeForChar(TEXT('a')), EViseme::AA);
			TestEqual("o", FVisemePlanner::VisemeForChar(TEXT('o')), EViseme::OH);
		});

		It("treats punctuation as silence", [this]()
		{
			TestEqual("period", FVisemePlanner::VisemeForChar(TEXT('.')), EViseme::Sil);
			TestEqual("space", FVisemePlanner::VisemeForChar(TEXT(' ')), EViseme::Sil);
		});

		It("is case insensitive", [this]()
		{
			TestEqual("P == p", FVisemePlanner::VisemeForChar(TEXT('P')), FVisemePlanner::VisemeForChar(TEXT('p')));
		});
	});

	Describe("PlanFromAlignment", [this]()
	{
		auto Span = [](const TCHAR* Sym, float Start, float Dur)
		{
			FPhonemeSpan S; S.Symbol = Sym; S.StartSeconds = Start; S.DurationSeconds = Dur; return S;
		};

		It("returns nothing for no spans", [this]()
		{
			TestEqual("empty", FVisemePlanner::PlanFromAlignment({}).Num(), 0);
		});

		It("keeps the aligner's timing instead of re-estimating it", [this, Span]()
		{
			// The whole point: a long phoneme stays long. PlanFromText would have given these
			// two equal time because they are both one character.
			const TArray<FVisemeSpan> T = FVisemePlanner::PlanFromAlignment(
				{ Span(TEXT("m"), 0.0f, 0.40f), Span(TEXT("s"), 0.40f, 0.05f) });
			TestTrue("has spans", T.Num() >= 2);
			TestTrue("first is the long one", T[0].DurationSeconds > T[1].DurationSeconds * 4.0f);
		});

		It("treats misaki capitals as DIPHTHONGS, not spelled-out letters", [this]()
		{
			// This one is here because I misread it myself: hˌW is /haʊ/, not the letter W.
			TestEqual("I is /aɪ/", FVisemePlanner::VisemeForIpa(TEXT("I")), EViseme::AA);
			TestEqual("W is /aʊ/", FVisemePlanner::VisemeForIpa(TEXT("W")), EViseme::AA);
			TestEqual("A is /eɪ/", FVisemePlanner::VisemeForIpa(TEXT("A")), EViseme::E);
			TestEqual("Y is /ɔɪ/", FVisemePlanner::VisemeForIpa(TEXT("Y")), EViseme::OH);
		});

		It("does not punch silence into a stressed syllable", [this, Span]()
		{
			// Stress marks carry real frames from the model. Emitting them as Sil would close
			// the mouth mid-vowel on every stressed syllable in the language.
			const TArray<FVisemeSpan> T = FVisemePlanner::PlanFromAlignment(
				{ Span(TEXT("w"), 0.0f, 0.05f), Span(TEXT("ˈ"), 0.05f, 0.05f), Span(TEXT("ɛ"), 0.10f, 0.05f) });
			for (const FVisemeSpan& S : T)
			{
				const bool bHole = (S.Viseme == EViseme::Sil && S.StartSeconds < 0.10f);
				TestFalse("no silence inside the syllable", bHole);
			}
		});

		It("stays contiguous so sampling never falls in a hole", [this, Span]()
		{
			const TArray<FVisemeSpan> T = FVisemePlanner::PlanFromAlignment(
				{ Span(TEXT("k"), 0.0f, 0.05f), Span(TEXT("ˈ"), 0.05f, 0.03f),
				  Span(TEXT("æ"), 0.08f, 0.09f),
				  Span(TEXT("t"), 0.17f, 0.04f) });
			for (int32 i = 0; i + 1 < T.Num(); ++i)
			{
				TestTrue("no gap", FMath::IsNearlyEqual(T[i].EndSeconds(), T[i + 1].StartSeconds, 0.002f));
			}
		});

		It("maps the consonant families a viewer can actually see", [this]()
		{
			TestEqual("m closes", FVisemePlanner::VisemeForIpa(TEXT("m")), EViseme::PP);
			TestEqual("v is labiodental", FVisemePlanner::VisemeForIpa(TEXT("v")), EViseme::FF);
			TestEqual("ʤ is postalveolar", FVisemePlanner::VisemeForIpa(TEXT("ʤ")), EViseme::CH);
			TestEqual("schwa is mid", FVisemePlanner::VisemeForIpa(TEXT("ə")), EViseme::E);
		});

		It("ends closed", [this, Span]()
		{
			const TArray<FVisemeSpan> T = FVisemePlanner::PlanFromAlignment({ Span(TEXT("a"), 0.0f, 0.2f) });
			TestTrue("has spans", T.Num() > 0);
			TestEqual("last is silence", T.Last().Viseme, EViseme::Sil);
		});
	});

	Describe("PlanBest", [this]()
	{
		auto Span = [](const TCHAR* Sym, float Start, float Dur)
		{
			FPhonemeSpan S; S.Symbol = Sym; S.StartSeconds = Start; S.DurationSeconds = Dur; return S;
		};

		It("prefers real alignment over the measured clip length", [this, Span]()
		{
			// Both are available. Alignment must win: the measured length only fixes the TOTAL,
			// leaving each phoneme's share of it decided by spelling.
			EVisemeTimingSource Source = EVisemeTimingSource::TextEstimate;
			const TArray<FVisemeSpan> Track = FVisemePlanner::PlanBest(
				TEXT("mass"), { Span(TEXT("m"), 0.0f, 0.40f), Span(TEXT("s"), 0.40f, 0.05f) }, 3.0f, Source);

			TestEqual("chose alignment", Source, EVisemeTimingSource::Alignment);
			TestTrue("has spans", Track.Num() > 0);
			// Proof it is the aligner's track and not a rescale of the estimate: the aligned
			// version runs 0.45s, nowhere near the 3.0s a rescale would have produced.
			TestTrue("kept the aligner's clock", FVisemePlanner::TrackDuration(Track) < 1.0f);
		});

		It("falls back to the measured clip length when there is no alignment", [this]()
		{
			EVisemeTimingSource Source = EVisemeTimingSource::Alignment;
			const TArray<FVisemeSpan> Track = FVisemePlanner::PlanBest(TEXT("Hello there, visitor."), {}, 3.0f, Source);

			TestEqual("chose the measured total", Source, EVisemeTimingSource::MeasuredDuration);
			TestTrue("matches the clip", FMath::IsNearlyEqual(FVisemePlanner::TrackDuration(Track), 3.0f, 0.01f));
		});

		It("falls back to the pure estimate when there is no audio either", [this]()
		{
			EVisemeTimingSource Source = EVisemeTimingSource::Alignment;
			const TArray<FVisemeSpan> Track = FVisemePlanner::PlanBest(TEXT("Hello there"), {}, 0.0f, Source);

			TestEqual("chose the text estimate", Source, EVisemeTimingSource::TextEstimate);
			TestTrue("still speaks", Track.Num() > 0);
		});

		It("steps past an alignment that produced no usable track", [this, Span]()
		{
			// Spans arrived, but every one of them is zero-length, so PlanFromAlignment drops the
			// lot. Reporting the accurate path here would hold the mouth shut for the whole reply
			// while the log claimed real timing -- the exact shape of a silent failure.
			EVisemeTimingSource Source = EVisemeTimingSource::Alignment;
			const TArray<FVisemeSpan> Track = FVisemePlanner::PlanBest(
				TEXT("Hello there"), { Span(TEXT("m"), 0.0f, 0.0f) }, 2.0f, Source);

			TestEqual("degraded to the measured total", Source, EVisemeTimingSource::MeasuredDuration);
			TestTrue("still speaks", Track.Num() > 0);
			TestTrue("over the measured length", FMath::IsNearlyEqual(FVisemePlanner::TrackDuration(Track), 2.0f, 0.01f));
		});

		It("names each source distinctly, since the log line is the only way to tell them apart", [this]()
		{
			const FString Aligned = FVisemePlanner::TimingSourceName(EVisemeTimingSource::Alignment);
			const FString Measured = FVisemePlanner::TimingSourceName(EVisemeTimingSource::MeasuredDuration);
			const FString Estimated = FVisemePlanner::TimingSourceName(EVisemeTimingSource::TextEstimate);

			TestFalse("alignment vs measured", Aligned.Equals(Measured));
			TestFalse("measured vs estimate", Measured.Equals(Estimated));
			TestTrue("the estimates say so", Measured.Contains(TEXT("ESTIMATE")) && Estimated.Contains(TEXT("ESTIMATE")));
			TestFalse("and the real one does not", Aligned.Contains(TEXT("ESTIMATE")));
		});
	});
}
