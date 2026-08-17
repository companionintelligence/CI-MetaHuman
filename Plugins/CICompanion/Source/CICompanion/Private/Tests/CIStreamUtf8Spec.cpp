#include "Misc/AutomationTest.h"
#include "LLM/CIStreamUtf8.h"

#include <initializer_list>

BEGIN_DEFINE_SPEC(FCIStreamUtf8Spec, "MetaHumanLocalLLM.Backend.StreamUtf8", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FCIStreamUtf8Spec)

// The invariant the streaming clients actually need, stated once: THE DECODED TEXT MUST NOT DEPEND
// ON HOW THE BYTES WERE CHUNKED.
//
// That is precisely what the old TCHAR-diffing code got wrong, and precisely what no existing test
// could see. OllamaClientSpec and OpenAIChatClientSpec both feed their parsers whole, well-formed
// FString chunks -- so they exercise the parser and never the cursor in front of it. The cursor only
// misbehaves when a chunk boundary lands inside a multi-byte character, which is a property of where
// TCP happened to split a packet and therefore something a test has to construct deliberately.
//
// Byte arrays are built from explicit code-unit values rather than from source literals on purpose:
// a non-ASCII literal in a .cpp depends on the file's encoding and on the compiler's -finput-charset
// (MSVC and clang disagree by default), so a test written with a literal em dash can fail for a
// reason that has nothing to do with the code under test.
void FCIStreamUtf8Spec::Define()
{
	// "Hi " + U+00E9 (2 bytes) + " " + U+4E16 (3 bytes) + " " + U+1F600 (4 bytes) + "!"
	// One of every UTF-8 sequence length, so every partial-tail case is reachable.
	auto MixedBytes = []() -> TArray<uint8>
	{
		return TArray<uint8>({
			0x48, 0x69, 0x20,				// "Hi "
			0xC3, 0xA9,						// U+00E9  2-byte
			0x20,							// " "
			0xE4, 0xB8, 0x96,				// U+4E16  3-byte
			0x20,							// " "
			0xF0, 0x9F, 0x98, 0x80,			// U+1F600 4-byte
			0x21							// "!"
		});
	};

	// Replays the byte stream in fixed-size instalments through exactly the cursor the HTTP progress
	// delegate drives: an append-only buffer plus a persistent byte offset.
	auto DecodeInChunks = [](const TArray<uint8>& Bytes, int32 ChunkSize) -> FString
	{
		TArray<uint8> Buffer;
		int32 Offset = 0;
		FString Out;
		for (int32 i = 0; i < Bytes.Num(); i += ChunkSize)
		{
			const int32 Take = FMath::Min(ChunkSize, Bytes.Num() - i);
			Buffer.Append(Bytes.GetData() + i, Take);
			Out += CIStreamUtf8::ConsumeCompleteUtf8(Buffer, Offset);
		}
		return Out;
	};

	Describe("chunk independence", [this, MixedBytes, DecodeInChunks]()
	{
		It("decodes identically at every chunk size, including one byte at a time", [this, MixedBytes, DecodeInChunks]()
		{
			// THE REGRESSION TEST. One byte at a time is the worst case and it is not hypothetical:
			// it is what the old code faced whenever a packet boundary fell mid-character, and it
			// produced dropped or doubled glyphs that were then accumulated AND spoken aloud.
			const TArray<uint8> Bytes = MixedBytes();
			const FString Whole = DecodeInChunks(Bytes, Bytes.Num());

			// AN ABSOLUTE ORACLE FIRST. Comparing every chunk size against Whole proves only that
			// the decoder is CONSISTENT — a bug uniform across chunk sizes (dropping the last
			// character, say, or emitting a replacement glyph every time) would satisfy every
			// comparison below and still be wrong. So the reference itself is pinned to known code
			// points before it is used as one.
			//
			// Checked by code point rather than against a source literal: a non-ASCII literal in a
			// .cpp depends on the file encoding and the compiler's input charset, so the test could
			// fail for reasons unrelated to the code. The two BMP characters are single TCHARs on
			// every platform, which keeps this portable.
			TestTrue(TEXT("decodes the leading ASCII"), Whole.StartsWith(TEXT("Hi ")));
			TestEqual(TEXT("decodes U+00E9 at index 3"), static_cast<uint32>(Whole[3]), 0x00E9u);
			TestEqual(TEXT("decodes U+4E16 at index 5"), static_cast<uint32>(Whole[5]), 0x4E16u);
			TestEqual(TEXT("ends with the trailing ASCII"), Whole[Whole.Len() - 1], TEXT('!'));
			// U+1F600 is outside the BMP: one TCHAR where TCHAR is 4 bytes (Linux), a surrogate
			// PAIR where it is 2 (Windows). Both are correct, so the length pins that the character
			// survived at all without asserting a platform.
			TestTrue(TEXT("the astral character survived"), Whole.Len() == 9 || Whole.Len() == 10);

			for (int32 ChunkSize = 1; ChunkSize <= Bytes.Num(); ++ChunkSize)
			{
				const FString Chunked = DecodeInChunks(Bytes, ChunkSize);
				TestEqual(FString::Printf(TEXT("chunk size %d decodes identically"), ChunkSize), Chunked, Whole);
			}
		});

		It("emits nothing at all when only part of a character has arrived", [this]()
		{
			// The cursor must not advance over a fragment. If it does, the bytes are gone: the next
			// tick sees the completed character but the offset is already past it.
			TArray<uint8> Buffer({ 0xF0, 0x9F });	// first half of a 4-byte emoji
			int32 Offset = 0;
			TestEqual(TEXT("a partial character yields nothing"),
				CIStreamUtf8::ConsumeCompleteUtf8(Buffer, Offset), FString());
			TestEqual(TEXT("and the cursor does not move"), Offset, 0);

			Buffer.Append(TArray<uint8>({ 0x98, 0x80 }));
			const FString Complete = CIStreamUtf8::ConsumeCompleteUtf8(Buffer, Offset);
			TestFalse(TEXT("the completed character is emitted once the rest arrives"), Complete.IsEmpty());
			TestEqual(TEXT("and the cursor consumed all four bytes"), Offset, 4);
		});

		It("hands back the ASCII prefix while holding the partial tail", [this]()
		{
			// The common real case: a chunk of readable text ending mid-character. The text must not
			// be delayed just because the last glyph is incomplete -- that would stutter the stream.
			TArray<uint8> Buffer({ 0x68, 0x69, 0xE4, 0xB8 });	// "hi" + 2 of 3 bytes
			int32 Offset = 0;
			TestEqual(TEXT("the complete prefix is emitted"), CIStreamUtf8::ConsumeCompleteUtf8(Buffer, Offset), FString(TEXT("hi")));
			TestEqual(TEXT("only the complete bytes were consumed"), Offset, 2);
		});
	});

	Describe("IncompleteTrailingBytes", [this]()
	{
		auto Tail = [](std::initializer_list<uint8> Bytes) -> int32
		{
			const TArray<uint8> Buffer(Bytes);
			return CIStreamUtf8::IncompleteTrailingBytes(Buffer.GetData(), Buffer.Num());
		};

		It("reports zero on a boundary", [this, Tail]()
		{
			TestEqual(TEXT("ascii"), Tail({ 0x41, 0x42 }), 0);
			TestEqual(TEXT("complete 2-byte"), Tail({ 0xC3, 0xA9 }), 0);
			TestEqual(TEXT("complete 3-byte"), Tail({ 0xE4, 0xB8, 0x96 }), 0);
			TestEqual(TEXT("complete 4-byte"), Tail({ 0xF0, 0x9F, 0x98, 0x80 }), 0);
			TestEqual(TEXT("ascii after a complete sequence"), Tail({ 0xC3, 0xA9, 0x41 }), 0);
		});

		It("reports the fragment length on every partial sequence", [this, Tail]()
		{
			TestEqual(TEXT("2-byte missing 1"), Tail({ 0xC3 }), 1);
			TestEqual(TEXT("3-byte missing 2"), Tail({ 0xE4 }), 1);
			TestEqual(TEXT("3-byte missing 1"), Tail({ 0xE4, 0xB8 }), 2);
			TestEqual(TEXT("4-byte missing 3"), Tail({ 0xF0 }), 1);
			TestEqual(TEXT("4-byte missing 2"), Tail({ 0xF0, 0x9F }), 2);
			TestEqual(TEXT("4-byte missing 1"), Tail({ 0xF0, 0x9F, 0x98 }), 3);
			TestEqual(TEXT("partial after complete text"), Tail({ 0x41, 0x42, 0xF0, 0x9F }), 2);
		});

		It("never stalls the stream on malformed input", [this, Tail]()
		{
			// Holding bytes back until they become valid would wait forever on input that is never
			// going to be valid, which turns a single bad byte into a permanently frozen reply.
			// Passing it through costs one wrong glyph and keeps the conversation alive.
			TestEqual(TEXT("a lone continuation byte"), Tail({ 0x80 }), 0);
			TestEqual(TEXT("continuation bytes with no lead"), Tail({ 0x80, 0x80, 0x80, 0x80 }), 0);
			TestEqual(TEXT("an invalid lead byte"), Tail({ 0xFF }), 0);
			TestEqual(TEXT("empty"), CIStreamUtf8::IncompleteTrailingBytes(nullptr, 0), 0);
		});
	});

	Describe("cursor bookkeeping", [this]()
	{
		It("returns empty and holds still once the buffer is exhausted", [this]()
		{
			const TArray<uint8> Buffer({ 0x41, 0x42 });
			int32 Offset = 0;
			TestEqual(TEXT("first read takes everything"), CIStreamUtf8::ConsumeCompleteUtf8(Buffer, Offset), FString(TEXT("AB")));
			TestEqual(TEXT("cursor at the end"), Offset, 2);
			// The progress delegate fires repeatedly with no new bytes; that must be free and silent
			// rather than re-emitting the tail.
			TestEqual(TEXT("a second read yields nothing"), CIStreamUtf8::ConsumeCompleteUtf8(Buffer, Offset), FString());
			TestEqual(TEXT("cursor unchanged"), Offset, 2);
		});

		It("survives an offset past the end of a buffer", [this]()
		{
			// HandleRequestComplete re-reads the whole response after the last progress tick. A
			// cancelled or truncated request can leave the cursor beyond what the buffer now holds.
			const TArray<uint8> Buffer({ 0x41 });
			int32 Offset = 99;
			TestEqual(TEXT("no read"), CIStreamUtf8::ConsumeCompleteUtf8(Buffer, Offset), FString());
			TestEqual(TEXT("clamped rather than left out of range"), Offset, 1);
		});

		It("handles an empty buffer", [this]()
		{
			const TArray<uint8> Empty;
			int32 Offset = 0;
			TestEqual(TEXT("nothing decoded"), CIStreamUtf8::ConsumeCompleteUtf8(Empty, Offset), FString());
			TestEqual(TEXT("cursor still zero"), Offset, 0);
		});
	});
}
