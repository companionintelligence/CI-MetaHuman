#include "Misc/AutomationTest.h"
#include "LLM/OllamaStreamParser.h"
#include "LLM/OllamaClient.h"
#include "LLM/OllamaTypes.h"

BEGIN_DEFINE_SPEC(FOllamaClientSpec, "MetaHumanLocalLLM.Backend.OllamaClient", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FOllamaClientSpec)

// Covers the two pure, HTTP-free units of UOllamaClient: request-body construction and
// NDJSON stream parsing. Neither test touches the network or requires a live Ollama
// server, so this suite runs headlessly in CI.
void FOllamaClientSpec::Define()
{
	Describe("BuildRequestBody", [this]()
	{
		It("includes the model name and stream flag", [this]()
		{
			TArray<FOllamaMessage> History;
			History.Add(FOllamaMessage(EOllamaRole::System, TEXT("Be nice")));
			History.Add(FOllamaMessage(EOllamaRole::User, TEXT("Hello")));

			const FString Body = UOllamaClient::BuildRequestBody(History, TEXT("llama3.2"));

			TestTrue("contains model", Body.Contains(TEXT("\"model\":\"llama3.2\"")));
			TestTrue("contains stream:true", Body.Contains(TEXT("\"stream\":true")));
			TestTrue("contains system role", Body.Contains(TEXT("\"role\":\"system\"")));
			TestTrue("contains user content", Body.Contains(TEXT("\"content\":\"Hello\"")));
		});

		It("preserves message order", [this]()
		{
			TArray<FOllamaMessage> History;
			History.Add(FOllamaMessage(EOllamaRole::User, TEXT("first")));
			History.Add(FOllamaMessage(EOllamaRole::Assistant, TEXT("second")));

			const FString Body = UOllamaClient::BuildRequestBody(History, TEXT("m"));
			const int32 FirstIdx = Body.Find(TEXT("first"));
			const int32 SecondIdx = Body.Find(TEXT("second"));

			TestTrue("first appears before second", FirstIdx >= 0 && SecondIdx > FirstIdx);
		});

		It("produces an empty messages array for empty history", [this]()
		{
			const FString Body = UOllamaClient::BuildRequestBody(TArray<FOllamaMessage>{}, TEXT("m"));
			TestTrue("empty messages array", Body.Contains(TEXT("\"messages\":[]")));
		});
	});

	Describe("FOllamaStreamParser", [this]()
	{
		It("parses a single complete NDJSON line", [this]()
		{
			FOllamaStreamParser Parser;
			const TArray<FOllamaStreamParser::FParsedLine> Lines = Parser.Feed(
				TEXT("{\"message\":{\"role\":\"assistant\",\"content\":\"Hi\"},\"done\":false}\n"));

			TestEqual("one line parsed", Lines.Num(), 1);
			if (Lines.Num() == 1)
			{
				TestEqual("content", Lines[0].Content, TEXT("Hi"));
				TestFalse("not done", Lines[0].bDone);
			}
		});

		It("buffers a partial line split across two Feed calls", [this]()
		{
			FOllamaStreamParser Parser;
			const TArray<FOllamaStreamParser::FParsedLine> FirstResult = Parser.Feed(TEXT("{\"message\":{\"content\":\"He"));
			TestEqual("no complete lines yet", FirstResult.Num(), 0);

			const TArray<FOllamaStreamParser::FParsedLine> SecondResult = Parser.Feed(TEXT("llo\"},\"done\":false}\n"));
			TestEqual("one line now complete", SecondResult.Num(), 1);
			if (SecondResult.Num() == 1)
			{
				TestEqual("content reassembled across chunks", SecondResult[0].Content, TEXT("Hello"));
			}
		});

		It("parses multiple lines delivered in one chunk", [this]()
		{
			FOllamaStreamParser Parser;
			const FString Chunk =
				TEXT("{\"message\":{\"content\":\"A\"},\"done\":false}\n")
				TEXT("{\"message\":{\"content\":\"B\"},\"done\":false}\n")
				TEXT("{\"message\":{\"content\":\"\"},\"done\":true}\n");

			const TArray<FOllamaStreamParser::FParsedLine> Results = Parser.Feed(Chunk);

			TestEqual("three lines parsed", Results.Num(), 3);
			if (Results.Num() == 3)
			{
				TestEqual("first token", Results[0].Content, TEXT("A"));
				TestEqual("second token", Results[1].Content, TEXT("B"));
				TestTrue("final line marked done", Results[2].bDone);
			}
		});

		It("skips a malformed line without losing subsequent valid lines", [this]()
		{
			FOllamaStreamParser Parser;
			const FString Chunk =
				TEXT("not valid json\n")
				TEXT("{\"message\":{\"content\":\"ok\"},\"done\":false}\n");

			const TArray<FOllamaStreamParser::FParsedLine> Results = Parser.Feed(Chunk);

			TestEqual("only the valid line survives", Results.Num(), 1);
			if (Results.Num() == 1)
			{
				TestEqual("content", Results[0].Content, TEXT("ok"));
			}
		});

		It("Reset clears any buffered partial line", [this]()
		{
			FOllamaStreamParser Parser;
			Parser.Feed(TEXT("{\"message\":{\"content\":\"partial"));
			Parser.Reset();

			const TArray<FOllamaStreamParser::FParsedLine> Results = Parser.Feed(
				TEXT("{\"message\":{\"content\":\"fresh\"},\"done\":true}\n"));

			TestEqual("only the post-reset line is parsed", Results.Num(), 1);
			if (Results.Num() == 1)
			{
				TestEqual("content is not glued to the discarded partial", Results[0].Content, TEXT("fresh"));
			}
		});
	});

	Describe("thinking suppression", [this]()
	{
		It("disables thinking by default, because it is pure latency for a companion", [this]()
		{
			// Measured on beta-1: qwen3.6:27b answers a one-line greeting in ~814ms with thinking
			// off and 16-21s with it on. Ollama returns the reasoning in a separate `thinking`
			// field that never reaches the mouth, so the wait buys nothing.
			TArray<FOllamaMessage> History;
			History.Emplace(EOllamaRole::User, TEXT("hi"));
			const FString Body = UOllamaClient::BuildRequestBody(History, TEXT("qwen3.6:27b"));
			TestTrue("sends think:false", Body.Contains(TEXT("\"think\": false")) || Body.Contains(TEXT("\"think\":false")));
		});

		It("can opt back in when deliberation is wanted", [this]()
		{
			TArray<FOllamaMessage> History;
			History.Emplace(EOllamaRole::User, TEXT("hi"));
			const FString Body = UOllamaClient::BuildRequestBody(History, TEXT("qwen3.6:27b"), true);
			TestTrue("sends think:true", Body.Contains(TEXT("\"think\": true")) || Body.Contains(TEXT("\"think\":true")));
		});
	});
}
