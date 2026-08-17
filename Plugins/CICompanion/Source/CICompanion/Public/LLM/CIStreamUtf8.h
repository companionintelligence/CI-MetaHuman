#pragma once

#include "CoreMinimal.h"
// FUTF8ToTCHAR. Reachable transitively through CoreMinimal, but named explicitly because it is the
// one type this header cannot do its job without.
#include "Containers/StringConv.h"

// A byte cursor over a streaming HTTP response body that never splits a UTF-8 character.
//
// THE DEFECT THIS REPLACES.
// Unreal's HTTP module has no first-class SSE/NDJSON support, so both chat clients read a stream by
// re-reading the whole in-flight response on every progress tick and taking whatever is new. The
// original form of that was:
//
//     const FString All = Response->GetContentAsString();      // decodes the WHOLE buffer, again
//     const FString New = All.Mid(LastProcessedLength);        // diff by TCHAR count
//     LastProcessedLength = All.Len();
//
// GetContentAsString() decodes the entire byte buffer as UTF-8 each time it is called. A tick can
// land with the buffer ending in the MIDDLE of a multi-byte sequence -- which is not rare, it is
// simply wherever TCP happened to cut the packet -- and the decoder turns that dangling fragment
// into a replacement character. LastProcessedLength is then advanced PAST it. On the next tick the
// sequence is complete, so the same bytes now decode to the real character and every index after
// them shifts; the cursor is pointing into the wrong place in a string that changed underneath it.
//
// The visible result is dropped or doubled characters on any reply containing a non-ASCII byte --
// an em dash, a curly quote, an accent, an emoji, any CJK at all. And because the corrupted text is
// what gets accumulated AND what gets handed to the TTS, she then says it out loud.
//
// THE FIX.
// Diff RAW BYTES, and hold back a trailing incomplete sequence until the rest of it arrives. Byte
// offsets into an append-only buffer are stable in a way TCHAR offsets into a re-decoded string are
// not, which is the actual invariant the old code assumed and did not have.
//
// Defined once, in a header, as inline free functions -- NOT in an anonymous namespace in each .cpp.
// Unreal's unity build concatenates translation units, at which point per-file anonymous namespaces
// merge and duplicate definitions collide; CICompanionEnv.h documents the same lesson, which this
// project learned the hard way ("error C2084: function already has a body", Linux/clang grouped the
// files apart and never noticed, Windows/MSVC grouped them together and failed).
namespace CIStreamUtf8
{
	// How many bytes at the END of [Data, Data+Num) form an INCOMPLETE UTF-8 sequence.
	//
	// Returns 0 when the buffer ends on a character boundary, and 0 for malformed input as well:
	// holding bytes back forever because they will never become valid would stall the stream
	// permanently, which is a far worse failure than passing one bad character through.
	inline int32 IncompleteTrailingBytes(const uint8* Data, int32 Num)
	{
		if (!Data || Num <= 0)
		{
			return 0;
		}

		// A UTF-8 sequence is at most 4 bytes, so only the final 3 can be a partial tail -- but the
		// lead byte itself may be the 4th from the end, so look back at most 4.
		const int32 MaxLookback = FMath::Min(Num, 4);
		for (int32 Back = 1; Back <= MaxLookback; ++Back)
		{
			const uint8 Byte = Data[Num - Back];
			if ((Byte & 0xC0) == 0x80)
			{
				continue;	// continuation byte -- keep walking back toward the lead
			}

			// This is the lead byte (or plain ASCII), and `Back` is how many bytes of its sequence
			// the buffer currently holds.
			int32 Expected;
			if ((Byte & 0x80) == 0x00)      { Expected = 1; }
			else if ((Byte & 0xE0) == 0xC0) { Expected = 2; }
			else if ((Byte & 0xF0) == 0xE0) { Expected = 3; }
			else if ((Byte & 0xF8) == 0xF0) { Expected = 4; }
			else                            { return 0; }	// malformed lead: let it through

			return (Back < Expected) ? Back : 0;
		}

		// Four continuation bytes and no lead in sight: malformed. Hold nothing back, for the same
		// reason as above -- the stream must keep moving.
		return 0;
	}

	// Decodes everything from InOutOffset up to the last complete character, advances InOutOffset by
	// the number of bytes actually consumed, and returns the decoded text. Returns empty when there
	// is nothing new or only a partial character is available.
	inline FString ConsumeCompleteUtf8(const TArray<uint8>& Payload, int32& InOutOffset)
	{
		const int32 Total = Payload.Num();
		InOutOffset = FMath::Clamp(InOutOffset, 0, Total);
		if (InOutOffset >= Total)
		{
			return FString();
		}

		const uint8* Start = Payload.GetData() + InOutOffset;
		const int32 Available = Total - InOutOffset;
		const int32 Usable = Available - IncompleteTrailingBytes(Start, Available);
		if (Usable <= 0)
		{
			// The only thing new this tick was the first byte or two of a character. Consume
			// nothing and look again next tick.
			return FString();
		}

		InOutOffset += Usable;

		// LENGTH-EXPLICIT, and that is load-bearing: FUTF8ToTCHAR does NOT null-terminate its output
		// when constructed with an explicit source length.
		//
		// TStringConversion's (Source, SourceLen) constructor only sets ENullTerminatedString::Yes
		// when the SOURCE buffer's own last byte is already 0 (StringConv.h) -- otherwise NullOffset
		// is 0 and the destination buffer is allocated at exactly StringLength with no terminator.
		// A slice of an HTTP payload never ends in a NUL, so reading the result as a C string runs
		// off the end of the allocation: garbage tokens at best, a crash at worst, and neither would
		// be caught by a compiler or by any test that only feeds it ASCII.
		//
		// ConstructFromPtrSize rather than FString(Len, Ptr) because that constructor is deprecated
		// from 5.4 onward; the static exists in both 5.5 and 5.8, which this module must both build
		// against.
		FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Start), Usable);
		return FString::ConstructFromPtrSize(Converted.Get(), Converted.Length());
	}
}
