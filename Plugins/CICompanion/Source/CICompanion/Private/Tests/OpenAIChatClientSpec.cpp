#include "Misc/AutomationTest.h"
#include "LLM/OpenAIStreamParser.h"
#include "LLM/OpenAIChatClient.h"
#include "LLM/OllamaTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

BEGIN_DEFINE_SPEC(FOpenAIChatClientSpec, "MetaHumanLocalLLM.Backend.OpenAIChatClient", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FOpenAIChatClientSpec)

namespace
{
	// PARSE the body; do not grep it. The older assertions in this file use Body.Contains(), which is
	// adequate for "is this substring somewhere in the payload" but is the wrong instrument for the
	// thinking-suppression field, because it cannot distinguish any of the ways that field can be
	// wrong from the way it must be right: it passes on output that is not valid JSON at all, it
	// cannot tell a boolean false from the string "false", and it cannot tell a TOP-LEVEL
	// chat_template_kwargs -- the only place a server looks -- from the same key nested inside a
	// message. A server would reject or ignore all three; a substring test would call them green.
	TSharedPtr<FJsonObject> ParseJsonBody(const FString& Body)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
		if (!FJsonSerializer::Deserialize(Reader, Root))
		{
			return nullptr;
		}
		return Root;
	}
}

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

	// The OpenAI-compatible sibling of OllamaClientSpec's "thinking suppression" block, but guarding a
	// different failure. On Ollama, thinking costs latency (814ms -> 16-21s, measured on beta-1) and
	// the answer still arrives. Against the vLLM server CI-Hub on core-2 hands out as hermes-agent's
	// backend (cyankiwi/Qwen3.5-35B-A3B-AWQ-4bit), measured this session with the prompt "Say hello in
	// one short sentence." and stream:false, the reasoning consumed the whole token budget and
	// `content` came back NULL with finish_reason=length -- 28.1s and 2118 reasoning characters for an
	// empty reply at max_tokens 600. The same request with chat_template_kwargs {enable_thinking:false}
	// returned content "Hello!", zero reasoning characters, finish_reason=stop, in 0.335s. So what is
	// under test here is a companion that speaks versus one that is silent.
	Describe("thinking suppression", [this]()
	{
		It("sends a top-level chat_template_kwargs with a BOOLEAN enable_thinking:false when thinking is off", [this]()
		{
			TArray<FOllamaMessage> History;
			History.Emplace(EOllamaRole::User, TEXT("hi"));
			const FString Body = UOpenAIChatClient::BuildRequestBody(
				History, TEXT("cyankiwi/Qwen3.5-35B-A3B-AWQ-4bit"), /*bEnableThinking=*/false);

			const TSharedPtr<FJsonObject> Root = ParseJsonBody(Body);
			if (!TestTrue("body parses as a JSON object", Root.IsValid()))
			{
				return;
			}

			// TOP-LEVEL and an OBJECT. The server reads this next to "model"/"messages"; the same key
			// buried anywhere else is inert, and a stringified "{...}" would be too.
			const TSharedPtr<FJsonObject>* Kwargs = nullptr;
			if (!TestTrue("chat_template_kwargs is a top-level object",
				Root->TryGetObjectField(TEXT("chat_template_kwargs"), Kwargs) && Kwargs != nullptr && Kwargs->IsValid()))
			{
				return;
			}

			// TryGetBoolField fails rather than coercing, so this also rejects "false" as a string.
			bool bEnableThinkingOnWire = true;
			if (TestTrue("enable_thinking is a JSON boolean",
				(*Kwargs)->TryGetBoolField(TEXT("enable_thinking"), bEnableThinkingOnWire)))
			{
				TestFalse("enable_thinking is false", bEnableThinkingOnWire);
			}

			// The suppression field must not have cost the payload its actual content.
			FString WireModel;
			TestTrue("model survives alongside it", Root->TryGetStringField(TEXT("model"), WireModel));
			TestEqual("model unchanged", WireModel, TEXT("cyankiwi/Qwen3.5-35B-A3B-AWQ-4bit"));
			const TArray<TSharedPtr<FJsonValue>>* WireMessages = nullptr;
			if (TestTrue("messages survive alongside it", Root->TryGetArrayField(TEXT("messages"), WireMessages)))
			{
				TestEqual("one message", WireMessages->Num(), 1);
			}
		});

		It("suppresses by DEFAULT, since the caller that omits the flag is the one that most needs it", [this]()
		{
			// Guards the default argument specifically: BuildRequestBody's two-argument form is what
			// every pre-existing call site in this file uses, and thinking-off is the default state of
			// UMetaHumanLLMSettings::bEnableThinking.
			const FString Body = UOpenAIChatClient::BuildRequestBody(TArray<FOllamaMessage>{}, TEXT("m"));

			const TSharedPtr<FJsonObject> Root = ParseJsonBody(Body);
			if (!TestTrue("body parses as a JSON object", Root.IsValid()))
			{
				return;
			}
			TestTrue("chat_template_kwargs present without being asked for", Root->HasField(TEXT("chat_template_kwargs")));
		});

		It("omits the field entirely when thinking is ENABLED, because it is a hint and not a toggle", [this]()
		{
			// Deliberately NOT {"enable_thinking": true}. Every server that understands the key already
			// reasons by default, so sending true changes nothing on those; on a server that does not
			// understand it, it is an unrecognised top-level field with no upside at all.
			TArray<FOllamaMessage> History;
			History.Emplace(EOllamaRole::User, TEXT("hi"));
			const FString Body = UOpenAIChatClient::BuildRequestBody(
				History, TEXT("cyankiwi/Qwen3.5-35B-A3B-AWQ-4bit"), /*bEnableThinking=*/true);

			const TSharedPtr<FJsonObject> Root = ParseJsonBody(Body);
			if (!TestTrue("body parses as a JSON object", Root.IsValid()))
			{
				return;
			}
			TestFalse("no chat_template_kwargs at all", Root->HasField(TEXT("chat_template_kwargs")));
		});

		It("omits the field when the escape hatch is closed, WITHOUT re-enabling thinking", [this]()
		{
			// The whole point of the fourth parameter. An operator whose gateway 400s on an unknown
			// top-level body field must be able to drop the field while thinking stays off -- so this
			// asserts both halves: the field is gone AND bEnableThinking is still false, i.e. nothing
			// here quietly bought silence back by turning reasoning on. Whether api.openai.com is such
			// a gateway is UNTESTED; this exists because it might be.
			TArray<FOllamaMessage> History;
			History.Emplace(EOllamaRole::User, TEXT("hi"));
			const FString Body = UOpenAIChatClient::BuildRequestBody(
				History, TEXT("gpt-4o-mini"), /*bEnableThinking=*/false, /*bSendThinkingSuppression=*/false);

			const TSharedPtr<FJsonObject> Root = ParseJsonBody(Body);
			if (!TestTrue("body parses as a JSON object", Root.IsValid()))
			{
				return;
			}
			TestFalse("no chat_template_kwargs", Root->HasField(TEXT("chat_template_kwargs")));

			// Nothing else may have appeared in its place -- an escaped request has to be a PLAIN
			// OpenAI request, or the 400 it was avoiding just comes back under a different field name.
			TestFalse("no stray top-level enable_thinking either", Root->HasField(TEXT("enable_thinking")));
			TestFalse("no stray top-level think either", Root->HasField(TEXT("think")));
			bool bStreamOnWire = false;
			TestTrue("still a normal streaming request", Root->TryGetBoolField(TEXT("stream"), bStreamOnWire) && bStreamOnWire);
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
