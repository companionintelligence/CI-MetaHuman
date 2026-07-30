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
}
