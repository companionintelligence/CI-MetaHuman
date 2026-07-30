#include "Voice/VisemePlanner.h"

namespace
{
	// Average letters per word in English prose, used to convert a words-per-minute rate into a
	// per-letter duration without needing a dictionary.
	constexpr float LettersPerWord = 4.7f;

	// Vowels hold longer than consonants; this is the single biggest cue that makes estimated
	// timing read as speech instead of a metronome.
	constexpr float VowelDurationScale = 1.45f;
	constexpr float ConsonantDurationScale = 0.8f;

	bool IsVowelViseme(EViseme V)
	{
		return V == EViseme::AA || V == EViseme::E || V == EViseme::IH
			|| V == EViseme::OH || V == EViseme::OU;
	}
}

EViseme FVisemePlanner::VisemeForChar(TCHAR RawChar)
{
	const TCHAR C = FChar::ToLower(RawChar);
	switch (C)
	{
		// Vowels — the shapes a viewer actually reads.
		case 'a': return EViseme::AA;
		case 'e': return EViseme::E;
		case 'i': case 'y': return EViseme::IH;
		case 'o': return EViseme::OH;
		case 'u': case 'w': return EViseme::OU;

		// Consonants, grouped by the closure that is visible from outside the mouth.
		case 'p': case 'b': case 'm': return EViseme::PP;
		case 'f': case 'v': return EViseme::FF;
		case 't': case 'd': return EViseme::DD;
		case 'k': case 'g': case 'c': case 'q': case 'x': return EViseme::KK;
		case 'j': return EViseme::CH;
		case 's': case 'z': return EViseme::SS;
		case 'n': case 'l': return EViseme::NN;
		case 'r': return EViseme::RR;
		case 'h': return EViseme::AA;	// breathy, mouth already open

		default: return EViseme::Sil;	// spaces, digits, punctuation
	}
}

TArray<FVisemeSpan> FVisemePlanner::PlanFromText(const FString& Text, float WordsPerMinute)
{
	TArray<FVisemeSpan> Track;
	if (Text.IsEmpty() || WordsPerMinute <= 0.0f)
	{
		return Track;
	}

	const float SecondsPerLetter = 60.0f / (WordsPerMinute * LettersPerWord);
	float Cursor = 0.0f;

	// 'th', 'ch', 'sh' are single mouth shapes, not two — handled by lookahead so "the" does not
	// render as a t followed by an h.
	const int32 Len = Text.Len();
	for (int32 i = 0; i < Len; ++i)
	{
		const TCHAR C = FChar::ToLower(Text[i]);
		const TCHAR Next = (i + 1 < Len) ? FChar::ToLower(Text[i + 1]) : TEXT('\0');

		EViseme V;
		int32 Consumed = 1;
		if (C == 't' && Next == 'h') { V = EViseme::TH; Consumed = 2; }
		else if ((C == 'c' || C == 's') && Next == 'h') { V = EViseme::CH; Consumed = 2; }
		else if (C == 'p' && Next == 'h') { V = EViseme::FF; Consumed = 2; }
		else if (C == 'o' && Next == 'o') { V = EViseme::OU; Consumed = 2; }
		else if (C == 'e' && Next == 'e') { V = EViseme::IH; Consumed = 2; }
		else { V = VisemeForChar(C); }

		// Punctuation becomes a real pause; that rhythm is most of what sells it as speech.
		float Duration;
		if (V == EViseme::Sil)
		{
			const bool bSentenceEnd = (C == '.' || C == '!' || C == '?');
			const bool bClause = (C == ',' || C == ';' || C == ':');
			Duration = bSentenceEnd ? SecondsPerLetter * 6.0f
					 : bClause     ? SecondsPerLetter * 3.0f
					 :               SecondsPerLetter * 1.2f;	// plain space
		}
		else
		{
			Duration = SecondsPerLetter * (IsVowelViseme(V) ? VowelDurationScale : ConsonantDurationScale);
		}

		// Collapse a repeat into a longer hold rather than emitting a redundant span, so "ll"
		// or "ss" reads as one sustained shape.
		if (Track.Num() > 0 && Track.Last().Viseme == V)
		{
			Track.Last().DurationSeconds += Duration;
		}
		else
		{
			Track.Emplace(Cursor, Duration, V);
		}
		Cursor += Duration;
		i += (Consumed - 1);
	}

	// Always land closed, so the face returns to rest deterministically.
	if (Track.Num() > 0 && Track.Last().Viseme != EViseme::Sil)
	{
		Track.Emplace(Cursor, SecondsPerLetter * 2.0f, EViseme::Sil);
	}
	return Track;
}

TArray<FVisemeSpan> FVisemePlanner::PlanForDuration(const FString& Text, float TargetSeconds)
{
	TArray<FVisemeSpan> Track = PlanFromText(Text);
	const float Planned = TrackDuration(Track);
	if (Planned <= KINDA_SMALL_NUMBER || TargetSeconds <= 0.0f)
	{
		return Track;
	}

	const float Scale = TargetSeconds / Planned;
	for (FVisemeSpan& Span : Track)
	{
		Span.StartSeconds *= Scale;
		Span.DurationSeconds *= Scale;
	}
	return Track;
}

float FVisemePlanner::TrackDuration(const TArray<FVisemeSpan>& Track)
{
	return Track.Num() > 0 ? Track.Last().EndSeconds() : 0.0f;
}

TMap<EViseme, float> FVisemePlanner::SampleAt(const TArray<FVisemeSpan>& Track, float TimeSeconds, float BlendSeconds)
{
	TMap<EViseme, float> Out;
	if (Track.Num() == 0 || TimeSeconds < 0.0f || TimeSeconds >= TrackDuration(Track))
	{
		return Out;	// empty == rest face
	}

	// Locate the active span. Tracks are short (one utterance), so a linear scan is fine and
	// keeps this trivially testable.
	int32 Index = INDEX_NONE;
	for (int32 i = 0; i < Track.Num(); ++i)
	{
		if (TimeSeconds >= Track[i].StartSeconds && TimeSeconds < Track[i].EndSeconds())
		{
			Index = i;
			break;
		}
	}
	if (Index == INDEX_NONE)
	{
		return Out;
	}

	const FVisemeSpan& Active = Track[Index];
	if (Active.Viseme != EViseme::Sil)
	{
		Out.Add(Active.Viseme, 1.0f);
	}

	// Cross-fade into the next shape over the tail of this span. Without this the mouth pops
	// between discrete shapes and reads as a puppet.
	const float Blend = FMath::Min(BlendSeconds, Active.DurationSeconds * 0.5f);
	if (Blend > KINDA_SMALL_NUMBER && Index + 1 < Track.Num())
	{
		const float TailStart = Active.EndSeconds() - Blend;
		if (TimeSeconds > TailStart)
		{
			const float Alpha = FMath::Clamp((TimeSeconds - TailStart) / Blend, 0.0f, 1.0f);
			const EViseme NextV = Track[Index + 1].Viseme;
			if (Active.Viseme != EViseme::Sil)
			{
				Out.Add(Active.Viseme, 1.0f - Alpha);
			}
			if (NextV != EViseme::Sil)
			{
				Out.Add(NextV, Alpha);
			}
		}
	}
	return Out;
}
