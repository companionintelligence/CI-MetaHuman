#include "Misc/AutomationTest.h"
#include "Avatar/CompanionStageDirector.h"

BEGIN_DEFINE_SPEC(FCompanionStageSpec, "MetaHumanLocalLLM.Frontend.CompanionStage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FCompanionStageSpec)

// Covers the camera policy only -- whether the shot moves, and whether the pawn may move it.
//
// The rest of UCompanionStageDirector needs a game world, a tagged resident and a player
// controller, none of which exist under -nullrhi, so the decision is factored out into pure
// statics that take an env lookup. Same pattern as EnvConfigSpec: an injected lookup rather than
// real process-environment mutation, so tests cannot leak state into each other or into the
// editor running them.
void FCompanionStageSpec::Define()
{
	auto MakeLookup = [](const TMap<FString, FString>& Env)
	{
		return [Env](const TCHAR* Name) -> FString
		{
			const FString* Found = Env.Find(FString(Name));
			return Found ? *Found : FString();
		};
	};

	// The regression: the opening flythrough played on EVERY run because it was opt-out, so the
	// only way to get the static framing the set was solved for was to know MHLLM_OPENING_SHOT=0.
	// A camera that moves is the special case, so it is the one that has to be asked for.
	Describe("the opening shot", [this, MakeLookup]()
	{
		It("does not play when nothing is configured", [this, MakeLookup]()
		{
			TMap<FString, FString> Env;
			TestFalse("static by default", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));
		});

		It("plays only when explicitly asked for", [this, MakeLookup]()
		{
			TMap<FString, FString> Env;
			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("1"));
			TestTrue("opted in with 1", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("true"));
			TestTrue("opted in with true", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("TRUE"));
			TestTrue("case insensitive", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("yes"));
			TestTrue("opted in with yes", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));
		});

		It("stays static for the old opt-out value, an empty string, or a typo", [this, MakeLookup]()
		{
			// "0" used to be the ONLY way to get a static shot; it must keep meaning static rather
			// than becoming an accidental opt-in now that the default flipped.
			TMap<FString, FString> Env;
			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("0"));
			TestFalse("zero", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("false"));
			TestFalse("false", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT(""));
			TestFalse("empty", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));

			// An unparseable value must fail SAFE. Compose files interpolate blanks and typos
			// happen; neither should start a camera move nobody asked for.
			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("ture"));
			TestFalse("typo", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));
		});

		It("tolerates the whitespace a compose file leaves behind", [this, MakeLookup]()
		{
			TMap<FString, FString> Env;
			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT(" 1 "));
			TestTrue("padded 1 still opts in", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));
		});
	});

	// The other half of "the camera must not move": even with a static shot, the GameModeBase
	// DefaultPawn's default bindings turn mouse movement into camera yaw/pitch.
	Describe("free-look", [this, MakeLookup]()
	{
		It("is locked when nothing is configured", [this, MakeLookup]()
		{
			TMap<FString, FString> Env;
			TestFalse("locked by default", UCompanionStageDirector::ShouldAllowFreeLook(MakeLookup(Env)));
		});

		It("is handed back only on an explicit opt-in", [this, MakeLookup]()
		{
			TMap<FString, FString> Env;
			Env.Add(TEXT("MHLLM_FREE_LOOK"), TEXT("1"));
			TestTrue("opted in", UCompanionStageDirector::ShouldAllowFreeLook(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_FREE_LOOK"), TEXT("0"));
			TestFalse("explicitly off", UCompanionStageDirector::ShouldAllowFreeLook(MakeLookup(Env)));
		});

		It("is independent of the opening shot", [this, MakeLookup]()
		{
			// Two separate decisions: a launch stream may want the flythrough AND a locked camera
			// afterwards, and a developer may want to fly around a static scene.
			TMap<FString, FString> Env;
			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("1"));
			TestTrue("shot on", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));
			TestFalse("look still locked", UCompanionStageDirector::ShouldAllowFreeLook(MakeLookup(Env)));
		});
	});
}
