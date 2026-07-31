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

EViseme FVisemePlanner::VisemeForIpa(const FString& Symbol)
{
	if (Symbol.IsEmpty())
	{
		return EViseme::Sil;
	}

	// CASE-SENSITIVE comparison, deliberately and load-bearingly. FString::operator== compares
	// case-INSENSITIVELY in Unreal, which silently collides misaki's capital diphthongs with the
	// lowercase consonants and vowels they sit beside: W (/aʊ/) matched "w" (/w/), I (/aɪ/)
	// matched "i", A (/eɪ/) matched "a". The mouth would have rounded on "how" instead of
	// opening. Caught by a unit test, not by reading the code -- nothing about `S == TEXT("w")`
	// looks wrong.
	auto Is = [&Symbol](const TCHAR* Literal)
	{
		return Symbol.Equals(Literal, ESearchCase::CaseSensitive);
	};

	// --- consonants, grouped by the closure a viewer can actually see ---
	if (Is(TEXT("p")) || Is(TEXT("b")) || Is(TEXT("m"))) return EViseme::PP;
	if (Is(TEXT("f")) || Is(TEXT("v"))) return EViseme::FF;
	if (Is(TEXT("θ")) || Is(TEXT("ð"))) return EViseme::TH;
	if (Is(TEXT("t")) || Is(TEXT("d"))) return EViseme::DD;
	if (Is(TEXT("k")) || Is(TEXT("g")) || Is(TEXT("ŋ"))) return EViseme::KK;
	if (Is(TEXT("ʃ")) || Is(TEXT("ʒ")) || Is(TEXT("ʧ")) || Is(TEXT("ʤ"))
		|| Is(TEXT("tʃ")) || Is(TEXT("dʒ"))) return EViseme::CH;
	if (Is(TEXT("s")) || Is(TEXT("z"))) return EViseme::SS;
	if (Is(TEXT("n")) || Is(TEXT("l"))) return EViseme::NN;
	if (Is(TEXT("ɹ")) || Is(TEXT("r")) || Is(TEXT("ɚ")) || Is(TEXT("ɜ")) || Is(TEXT("ɝ"))) return EViseme::RR;
	if (Is(TEXT("w"))) return EViseme::OU;	// rounded glide
	if (Is(TEXT("j"))) return EViseme::IH;	// palatal glide
	if (Is(TEXT("h"))) return EViseme::AA;	// breathy, mouth already open

	// --- monophthongs ---
	if (Is(TEXT("ɑ")) || Is(TEXT("ɒ")) || Is(TEXT("æ")) || Is(TEXT("ɐ"))
		|| Is(TEXT("a")) || Is(TEXT("ʌ"))) return EViseme::AA;
	if (Is(TEXT("ɛ")) || Is(TEXT("e")) || Is(TEXT("ə")) || Is(TEXT("ᵊ"))) return EViseme::E;
	if (Is(TEXT("ɪ")) || Is(TEXT("i"))) return EViseme::IH;
	if (Is(TEXT("ɔ")) || Is(TEXT("o")) || Is(TEXT("ʊ"))) return EViseme::OH;
	if (Is(TEXT("u")) || Is(TEXT("ʉ"))) return EViseme::OU;

	// --- diphthongs, which misaki writes as CAPITALS ---
	// Mapped to the shape the mouth OPENS into, since that is held longest and is the one a
	// viewer reads. These look like spelled-out letters and are not.
	if (Is(TEXT("A"))) return EViseme::E;	// /eɪ/
	if (Is(TEXT("I"))) return EViseme::AA;	// /aɪ/
	if (Is(TEXT("W"))) return EViseme::AA;	// /aʊ/
	if (Is(TEXT("Y"))) return EViseme::OH;	// /ɔɪ/
	if (Is(TEXT("O"))) return EViseme::OH;	// /oʊ/

	// Stress marks, spaces and punctuation carry duration but no shape of their own.
	return EViseme::Sil;
}

TArray<FVisemeSpan> FVisemePlanner::PlanFromAlignment(const TArray<FPhonemeSpan>& Spans)
{
	TArray<FVisemeSpan> Track;
	Track.Reserve(Spans.Num());

	for (const FPhonemeSpan& In : Spans)
	{
		if (In.DurationSeconds <= 0.0f)
		{
			continue;
		}
		const FString Sym = In.Symbol;

		// Stress marks (ˈ ˌ) are voiced AS PART OF the vowel that follows and get their own
		// frames from the model. Emitting them as silence would punch a hole in the middle of
		// every stressed syllable, so their time is donated forward to the next real shape.
		const bool bStress = (Sym == TEXT("ˈ") || Sym == TEXT("ˌ"));
		if (bStress)
		{
			continue;	// duration is absorbed by the gap-filling below
		}

		const EViseme V = VisemeForIpa(Sym);

		// Merge consecutive identical shapes into one sustained hold rather than emitting a
		// redundant span, which is what stops "ll" or a stress+vowel pair from stuttering.
		if (Track.Num() > 0 && Track.Last().Viseme == V)
		{
			Track.Last().DurationSeconds = (In.StartSeconds + In.DurationSeconds) - Track.Last().StartSeconds;
			continue;
		}
		Track.Emplace(In.StartSeconds, In.DurationSeconds, V);
	}

	// Close any gaps left by dropped stress marks by extending each span to meet the next, so
	// the track stays contiguous and SampleAt never lands in a hole.
	for (int32 i = 0; i + 1 < Track.Num(); ++i)
	{
		Track[i].DurationSeconds = FMath::Max(0.0f, Track[i + 1].StartSeconds - Track[i].StartSeconds);
	}

	// Land closed, as PlanFromText does, so the mouth returns to rest deterministically.
	if (Track.Num() > 0 && Track.Last().Viseme != EViseme::Sil)
	{
		Track.Emplace(Track.Last().EndSeconds(), 0.08f, EViseme::Sil);
	}
	return Track;
}
