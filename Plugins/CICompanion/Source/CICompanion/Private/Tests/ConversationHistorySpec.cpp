#include "Misc/AutomationTest.h"
#include "LLM/OllamaConversation.h"

BEGIN_DEFINE_SPEC(FConversationHistorySpec, "MetaHumanLocalLLM.Backend.ConversationHistory", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	UOllamaConversation* Conversation = nullptr;
END_DEFINE_SPEC(FConversationHistorySpec)

// Ollama's /api/chat has no server-side session: the full history round-trips on every
// request, so UOllamaConversation is the single source of truth for turn ordering and
// system-prompt seeding. Runs headlessly (plain UObject, no HTTP/rendering involved).
void FConversationHistorySpec::Define()
{
	BeforeEach([this]()
	{
		Conversation = NewObject<UOllamaConversation>();
		Conversation->AddToRoot();
	});

	AfterEach([this]()
	{
		if (Conversation)
		{
			Conversation->RemoveFromRoot();
			Conversation = nullptr;
		}
	});

	It("seeds the system prompt as the first message", [this]()
	{
		Conversation->Initialize(TEXT("Be helpful"));

		TestEqual("one message", Conversation->Num(), 1);
		TestTrue("has system prompt", Conversation->HasSystemPrompt());
		TestEqual("system content", Conversation->GetHistory()[0].Content, TEXT("Be helpful"));
	});

	It("skips seeding when the system prompt is empty", [this]()
	{
		Conversation->Initialize(TEXT(""));

		TestEqual("no messages", Conversation->Num(), 0);
		TestFalse("no system prompt", Conversation->HasSystemPrompt());
	});

	It("appends user and assistant turns in order", [this]()
	{
		Conversation->Initialize(TEXT("System"));
		Conversation->AppendUserMessage(TEXT("Hi"));
		Conversation->AppendAssistantMessage(TEXT("Hello!"));

		TestEqual("three messages", Conversation->Num(), 3);
		const TArray<FOllamaMessage>& History = Conversation->GetHistory();
		TestEqual("msg1 role", static_cast<uint8>(History[0].Role), static_cast<uint8>(EOllamaRole::System));
		TestEqual("msg2 role", static_cast<uint8>(History[1].Role), static_cast<uint8>(EOllamaRole::User));
		TestEqual("msg3 role", static_cast<uint8>(History[2].Role), static_cast<uint8>(EOllamaRole::Assistant));
	});

	It("Reset clears prior history and reseeds the system prompt", [this]()
	{
		Conversation->Initialize(TEXT("Old system"));
		Conversation->AppendUserMessage(TEXT("something"));

		Conversation->Reset(TEXT("New system"));

		TestEqual("only the new system message remains", Conversation->Num(), 1);
		TestEqual("new system content", Conversation->GetHistory()[0].Content, TEXT("New system"));
	});
}
