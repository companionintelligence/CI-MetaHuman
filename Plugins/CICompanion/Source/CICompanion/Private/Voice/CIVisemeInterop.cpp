#include "Voice/CIVisemeInterop.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace CIArkit
{
	const TArray<FName>& Names52()
	{
		// Order is EARFaceBlendShape declaration order (ARTrackable.h), lowerCamelCase.
		static const TArray<FName> Names = {
			TEXT("eyeBlinkLeft"), TEXT("eyeLookDownLeft"), TEXT("eyeLookInLeft"), TEXT("eyeLookOutLeft"),
			TEXT("eyeLookUpLeft"), TEXT("eyeSquintLeft"), TEXT("eyeWideLeft"), TEXT("eyeBlinkRight"),
			TEXT("eyeLookDownRight"), TEXT("eyeLookInRight"), TEXT("eyeLookOutRight"), TEXT("eyeLookUpRight"),
			TEXT("eyeSquintRight"), TEXT("eyeWideRight"), TEXT("jawForward"), TEXT("jawLeft"),
			TEXT("jawRight"), TEXT("jawOpen"), TEXT("mouthClose"), TEXT("mouthFunnel"),
			TEXT("mouthPucker"), TEXT("mouthLeft"), TEXT("mouthRight"), TEXT("mouthSmileLeft"),
			TEXT("mouthSmileRight"), TEXT("mouthFrownLeft"), TEXT("mouthFrownRight"), TEXT("mouthDimpleLeft"),
			TEXT("mouthDimpleRight"), TEXT("mouthStretchLeft"), TEXT("mouthStretchRight"), TEXT("mouthRollLower"),
			TEXT("mouthRollUpper"), TEXT("mouthShrugLower"), TEXT("mouthShrugUpper"), TEXT("mouthPressLeft"),
			TEXT("mouthPressRight"), TEXT("mouthLowerDownLeft"), TEXT("mouthLowerDownRight"), TEXT("mouthUpperUpLeft"),
			TEXT("mouthUpperUpRight"), TEXT("browDownLeft"), TEXT("browDownRight"), TEXT("browInnerUp"),
			TEXT("browOuterUpLeft"), TEXT("browOuterUpRight"), TEXT("cheekPuff"), TEXT("cheekSquintLeft"),
			TEXT("cheekSquintRight"), TEXT("noseSneerLeft"), TEXT("noseSneerRight"), TEXT("tongueOut")
		};
		return Names;
	}

	bool IsCanonical(FName Name)
	{
		static const TSet<FName> Lookup(Names52());
		return Lookup.Contains(Name);
	}
}

namespace CIOvr
{
	const TArray<FName>& Names15()
	{
		// Index order matches EViseme's declaration order exactly, which is what lets ToOvrName be a
		// direct index rather than a switch that could drift out of sync with the enum.
		static const TArray<FName> Names = {
			TEXT("viseme_sil"), TEXT("viseme_PP"), TEXT("viseme_FF"), TEXT("viseme_TH"),
			TEXT("viseme_DD"), TEXT("viseme_kk"), TEXT("viseme_CH"), TEXT("viseme_SS"),
			TEXT("viseme_nn"), TEXT("viseme_RR"), TEXT("viseme_aa"), TEXT("viseme_E"),
			TEXT("viseme_I"), TEXT("viseme_O"), TEXT("viseme_U")
		};
		return Names;
	}

	FName ToOvrName(EViseme Viseme)
	{
		const int32 Index = static_cast<int32>(Viseme);
		return Names15().IsValidIndex(Index) ? Names15()[Index] : Names15()[0];
	}

	bool FromOvrName(FName OvrName, EViseme& OutViseme)
	{
		const int32 Index = Names15().IndexOfByKey(OvrName);
		if (Index == INDEX_NONE)
		{
			return false;
		}
		OutViseme = static_cast<EViseme>(Index);
		return true;
	}
}

float FCIWordTimings::TotalSeconds() const
{
	if (!IsValid() || Words.Num() == 0)
	{
		return 0.0f;
	}
	// Last word's end, not the sum of durations: senders may leave gaps between words, and the
	// utterance length has to include them.
	const int32 Last = Words.Num() - 1;
	return (WTimesMs[Last] + WDurationsMs[Last]) / 1000.0f;
}

bool FCIWordTimings::FromJson(const FString& Json, FCIWordTimings& OutTimings)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* WordsArray = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* TimesArray = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* DursArray = nullptr;
	if (!Root->TryGetArrayField(TEXT("words"), WordsArray)
		|| !Root->TryGetArrayField(TEXT("wtimes"), TimesArray)
		|| !Root->TryGetArrayField(TEXT("wdurations"), DursArray))
	{
		return false;
	}

	// Reject rather than truncate. Mismatched lengths mean the producer is broken, and silently
	// using the shortest array would desynchronise the mouth from the audio in a way that looks like
	// a lip-sync bug rather than a data bug.
	if (WordsArray->Num() != TimesArray->Num() || WordsArray->Num() != DursArray->Num())
	{
		return false;
	}

	FCIWordTimings Parsed;
	Parsed.Words.Reserve(WordsArray->Num());
	Parsed.WTimesMs.Reserve(WordsArray->Num());
	Parsed.WDurationsMs.Reserve(WordsArray->Num());

	for (int32 Index = 0; Index < WordsArray->Num(); ++Index)
	{
		// Check the JSON type before reading. FJsonValue::TryGetString SUCCEEDS on a number --
		// {"words":[5]} would silently yield the word "5" -- so a type check is the only way to
		// reject a malformed payload rather than lip-syncing someone's array index.
		if ((*WordsArray)[Index]->Type != EJson::String)
		{
			return false;
		}

		FString Word;
		if (!(*WordsArray)[Index]->TryGetString(Word))
		{
			return false;
		}

		// Read as double then round: the WebSpatial sibling's approximator emits fractional
		// milliseconds, and TryGetNumber on a float value would fail an integer-typed read.
		double TimeMs = 0.0;
		double DurationMs = 0.0;
		if (!(*TimesArray)[Index]->TryGetNumber(TimeMs) || !(*DursArray)[Index]->TryGetNumber(DurationMs))
		{
			return false;
		}

		Parsed.Words.Add(MoveTemp(Word));
		Parsed.WTimesMs.Add(FMath::RoundToInt(TimeMs));
		Parsed.WDurationsMs.Add(FMath::RoundToInt(DurationMs));
	}

	OutTimings = MoveTemp(Parsed);
	return true;
}

FString FCIWordTimings::ToJson() const
{
	TArray<TSharedPtr<FJsonValue>> WordValues;
	TArray<TSharedPtr<FJsonValue>> TimeValues;
	TArray<TSharedPtr<FJsonValue>> DurationValues;
	for (int32 Index = 0; Index < Words.Num(); ++Index)
	{
		WordValues.Add(MakeShared<FJsonValueString>(Words[Index]));
		TimeValues.Add(MakeShared<FJsonValueNumber>(WTimesMs.IsValidIndex(Index) ? WTimesMs[Index] : 0));
		DurationValues.Add(MakeShared<FJsonValueNumber>(WDurationsMs.IsValidIndex(Index) ? WDurationsMs[Index] : 0));
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("words"), WordValues);
	Root->SetArrayField(TEXT("wtimes"), TimeValues);
	Root->SetArrayField(TEXT("wdurations"), DurationValues);

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}

TArray<FVisemeSpan> FCIWordTimings::ToVisemeTrack() const
{
	TArray<FVisemeSpan> Track;
	if (!IsValid() || Words.Num() == 0)
	{
		return Track;
	}

	float Cursor = 0.0f;	// seconds; end of the last span emitted

	for (int32 WordIndex = 0; WordIndex < Words.Num(); ++WordIndex)
	{
		const float WordStart = WTimesMs[WordIndex] / 1000.0f;
		const float WordDuration = FMath::Max(0.0f, WDurationsMs[WordIndex] / 1000.0f);

		// Silence for any gap before this word. This is what stops the mouth running on between
		// words; without it externally-timed speech reads as continuous chewing.
		if (WordStart > Cursor + UE_KINDA_SMALL_NUMBER)
		{
			Track.Emplace(Cursor, WordStart - Cursor, EViseme::Sil);
		}

		// Letters that produce a visible mouth shape. Non-alphabetic characters are dropped rather
		// than mapped, so punctuation inside a word ("don't", "well-known") does not consume a slot.
		TArray<EViseme> Shapes;
		for (const TCHAR Char : Words[WordIndex])
		{
			if (FChar::IsAlpha(Char))
			{
				const EViseme Viseme = FVisemePlanner::VisemeForChar(Char);
				// Collapse immediate repeats: "ll" in "hello" is one shape held, not two identical
				// spans, and doubling them makes the mouth stutter.
				if (Shapes.Num() == 0 || Shapes.Last() != Viseme)
				{
					Shapes.Add(Viseme);
				}
			}
		}

		if (Shapes.Num() == 0 || WordDuration <= UE_KINDA_SMALL_NUMBER)
		{
			// A word with no pronounceable letters (or zero measured duration) still occupies its
			// slot in time; emit silence so the following word starts where the data says it does.
			if (WordDuration > UE_KINDA_SMALL_NUMBER)
			{
				Track.Emplace(WordStart, WordDuration, EViseme::Sil);
			}
			Cursor = FMath::Max(Cursor, WordStart + WordDuration);
			continue;
		}

		// Distribute the word's MEASURED window evenly across its shapes. Even distribution is a
		// simplification -- real phoneme durations differ -- but the word boundaries are exact, so
		// error cannot accumulate across the utterance the way it does with a WPM estimate.
		const float PerShape = WordDuration / Shapes.Num();
		for (int32 ShapeIndex = 0; ShapeIndex < Shapes.Num(); ++ShapeIndex)
		{
			Track.Emplace(WordStart + (ShapeIndex * PerShape), PerShape, Shapes[ShapeIndex]);
		}

		Cursor = WordStart + WordDuration;
	}

	return Track;
}
