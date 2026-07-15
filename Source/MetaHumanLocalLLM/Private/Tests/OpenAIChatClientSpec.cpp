#include "Misc/AutomationTest.h"
#include "LLM/OpenAIStreamParser.h"
#include "LLM/OpenAIChatClient.h"
#include "LLM/OllamaTypes.h"

BEGIN_DEFINE_SPEC(FOpenAIChatClientSpec, "MetaHumanLocalLLM.Backend.OpenAIChatClient", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FOpenAIChatClientSpec)

// Covers the two pure, HTTP-free units of UOpenAIChatClient: request-body construction
// (OpenAI /v1/chat/completions shape) and SSE stream parsing. Neither touches the network,
// so this suite runs headlessly in CI alongside the Ollama equivalents.
void FOpenAIChatClientSpec::Define()
{
	Describe("BuildRequestBody", [this]()
	{
		It("includes the model name and stream flag", [this]()
		{
			TArray<FOllamaMessage> History;
			History.Add(FOllamaMessage(EOllamaRole::System, TEXT("Be nice")));
			History.Add(FOllamaMessage(EOllamaRole::User, TEXT("Hello")));

			const FString Body = UOpenAIChatClient::BuildRequestBody(History, TEXT("gpt-4o-mini"));

			TestTrue("contains model", Body.Contains(TEXT("\"model\":\"gpt-4o-mini\"")));
			TestTrue("contains stream:true", Body.Contains(TEXT("\"stream\":true")));
			TestTrue("contains system role", Body.Contains(TEXT("\"role\":\"system\"")));
			TestTrue("contains user content", Body.Contains(TEXT("\"content\":\"Hello\"")));
		});

		It("preserves message order", [this]()
		{
			TArray<FOllamaMessage> History;
			History.Add(FOllamaMessage(EOllamaRole::User, TEXT("first")));
			History.Add(FOllamaMessage(EOllamaRole::Assistant, TEXT("second")));

			const FString Body = UOpenAIChatClient::BuildRequestBody(History, TEXT("m"));
			const int32 FirstIdx = Body.Find(TEXT("first"));
			const int32 SecondIdx = Body.Find(TEXT("second"));

			TestTrue("first appears before second", FirstIdx >= 0 && SecondIdx > FirstIdx);
		});

		It("produces an empty messages array for empty history", [this]()
		{
			const FString Body = UOpenAIChatClient::BuildRequestBody(TArray<FOllamaMessage>{}, TEXT("m"));
			TestTrue("empty messages array", Body.Contains(TEXT("\"messages\":[]")));
		});
	});

	Describe("FOpenAIStreamParser", [this]()
	{
		It("parses a single SSE data line", [this]()
		{
			FOpenAIStreamParser Parser;
			const TArray<FOpenAIStreamParser::FParsedEvent> Events = Parser.Feed(
				TEXT("data: {\"choices\":[{\"delta\":{\"content\":\"Hi\"},\"finish_reason\":null}]}\n"));

			TestEqual("one event parsed", Events.Num(), 1);
			if (Events.Num() == 1)
			{
				TestEqual("content", Events[0].Content, TEXT("Hi"));
				TestFalse("not done", Events[0].bDone);
			}
		});

		It("treats the [DONE] sentinel as a done event", [this]()
		{
			FOpenAIStreamParser Parser;
			const TArray<FOpenAIStreamParser::FParsedEvent> Events = Parser.Feed(TEXT("data: [DONE]\n"));

			TestEqual("one event parsed", Events.Num(), 1);
			if (Events.Num() == 1)
			{
				TestTrue("marked done", Events[0].bDone);
				TestEqual("no content", Events[0].Content, FString());
			}
		});

		It("marks a non-null finish_reason as done", [this]()
		{
			FOpenAIStreamParser Parser;
			const TArray<FOpenAIStreamParser::FParsedEvent> Events = Parser.Feed(
				TEXT("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n"));

			TestEqual("one event parsed", Events.Num(), 1);
			if (Events.Num() == 1)
			{
				TestTrue("marked done via finish_reason", Events[0].bDone);
			}
		});

		It("buffers a partial line split across two Feed calls", [this]()
		{
			FOpenAIStreamParser Parser;
			const TArray<FOpenAIStreamParser::FParsedEvent> FirstResult = Parser.Feed(TEXT("data: {\"choices\":[{\"delta\":{\"content\":\"He"));
			TestEqual("no complete events yet", FirstResult.Num(), 0);

			const TArray<FOpenAIStreamParser::FParsedEvent> SecondResult = Parser.Feed(TEXT("llo\"},\"finish_reason\":null}]}\n"));
			TestEqual("one event now complete", SecondResult.Num(), 1);
			if (SecondResult.Num() == 1)
			{
				TestEqual("content reassembled across chunks", SecondResult[0].Content, TEXT("Hello"));
			}
		});

		It("parses multiple events delivered in one chunk", [this]()
		{
			FOpenAIStreamParser Parser;
			const FString Chunk =
				TEXT("data: {\"choices\":[{\"delta\":{\"content\":\"A\"},\"finish_reason\":null}]}\n")
				TEXT("data: {\"choices\":[{\"delta\":{\"content\":\"B\"},\"finish_reason\":null}]}\n")
				TEXT("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n")
				TEXT("data: [DONE]\n");

			const TArray<FOpenAIStreamParser::FParsedEvent> Results = Parser.Feed(Chunk);

			TestEqual("four events parsed", Results.Num(), 4);
			if (Results.Num() == 4)
			{
				TestEqual("first token", Results[0].Content, TEXT("A"));
				TestEqual("second token", Results[1].Content, TEXT("B"));
				TestTrue("third marked done via finish_reason", Results[2].bDone);
				TestTrue("fourth is the DONE sentinel", Results[3].bDone);
			}
		});

		It("skips non-data lines and blank separators", [this]()
		{
			FOpenAIStreamParser Parser;
			const FString Chunk =
				TEXT(": openai keep-alive comment\n")
				TEXT("event: message\n")
				TEXT("\n")
				TEXT("data: {\"choices\":[{\"delta\":{\"content\":\"ok\"},\"finish_reason\":null}]}\n");

			const TArray<FOpenAIStreamParser::FParsedEvent> Results = Parser.Feed(Chunk);

			TestEqual("only the data line survives", Results.Num(), 1);
			if (Results.Num() == 1)
			{
				TestEqual("content", Results[0].Content, TEXT("ok"));
			}
		});

		It("skips a malformed data payload without losing subsequent valid events", [this]()
		{
			FOpenAIStreamParser Parser;
			const FString Chunk =
				TEXT("data: not valid json\n")
				TEXT("data: {\"choices\":[{\"delta\":{\"content\":\"ok\"},\"finish_reason\":null}]}\n");

			const TArray<FOpenAIStreamParser::FParsedEvent> Results = Parser.Feed(Chunk);

			TestEqual("only the valid event survives", Results.Num(), 1);
			if (Results.Num() == 1)
			{
				TestEqual("content", Results[0].Content, TEXT("ok"));
			}
		});

		It("Reset clears any buffered partial line", [this]()
		{
			FOpenAIStreamParser Parser;
			Parser.Feed(TEXT("data: {\"choices\":[{\"delta\":{\"content\":\"partial"));
			Parser.Reset();

			const TArray<FOpenAIStreamParser::FParsedEvent> Results = Parser.Feed(
				TEXT("data: {\"choices\":[{\"delta\":{\"content\":\"fresh\"},\"finish_reason\":null}]}\n"));

			TestEqual("only the post-reset event is parsed", Results.Num(), 1);
			if (Results.Num() == 1)
			{
				TestEqual("content is not glued to the discarded partial", Results[0].Content, TEXT("fresh"));
			}
		});
	});
}
