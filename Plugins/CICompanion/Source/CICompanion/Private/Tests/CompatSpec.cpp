#include "Misc/AutomationTest.h"
#include "Compat/CICompatReport.h"
#include "Compat/CICompanionCompat.h"
#include "Misc/EngineVersion.h"

BEGIN_DEFINE_SPEC(FCompatSpec, "MetaHumanLocalLLM.Backend.Compat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FCompatSpec)

// Tests for the UE 5.5 <-> 5.8 compatibility layer.
//
// The awkward part of testing a polyfill is that its substance is #if branches the compiler already
// resolved: a spec running on 5.8 physically cannot execute the 5.5 branch. So the coverage is split
// in two (see the long comment in CICompatReport.h):
//
//   "compatibility table" -- pure ExpectedFor(major, minor) queries. These run identically on ANY
//       engine, so the 5.5 answers are fully exercised even on a machine with only 5.8 installed.
//       This is where most of the real assertions live.
//
//   "compiled binding"    -- one check that the preprocessor picked the row the table specifies for
//       the engine actually running. Run the suite on 5.5 and it validates the 5.5 #ifs; run it on
//       5.8 and it validates the 5.8 ones.
//
// Together those cover both branches without needing a mock engine or a second process.
void FCompatSpec::Define()
{
	Describe("compatibility table", [this]()
	{
		It("gives UE 5.5 no MetaHuman Character asset type", [this]()
		{
			// Measured: UE 5.5.4 ships only MetaHumanSDK (2 modules) plus a zero-module
			// MetaHumanRuntime redirect stub. There is no UMetaHumanCharacter anywhere in its tree.
			TestFalse("5.5 MetaHumanCharacter", FCICompatReport::ExpectedFor(5, 5).bMetaHumanCharacter);
		});

		It("gives UE 5.8 the MetaHuman Character asset type", [this]()
		{
			TestTrue("5.8 MetaHumanCharacter", FCICompatReport::ExpectedFor(5, 8).bMetaHumanCharacter);
		});

		It("gives UE 5.5 no Unreal MCP surface", [this]()
		{
			// ModelContextProtocol, MCPClientToolset, AllToolsets, ToolsetRegistry, AIAssistant and
			// the ~25 *Toolset plugins are all absent from 5.5.4.
			TestFalse("5.5 MCP", FCICompatReport::ExpectedFor(5, 5).bUnrealMcp);
			TestTrue("5.8 MCP", FCICompatReport::ExpectedFor(5, 8).bUnrealMcp);
		});

		It("reports Pixel Streaming 2 available on BOTH engines", [this]()
		{
			// The single most commonly mis-stated fact about this project. PixelStreaming2 ships
			// non-beta at Engine/Plugins/Media/PixelStreaming2 in 5.5.4 AND 5.8.1. Choosing 5.5 buys
			// no Pixel Streaming capability that 5.8 lacks; the AMD render wall documented in
			// Docs/FOLLOW-UP.md is a Mesa/RADV driver bug, not an engine-version difference.
			TestTrue("5.5 PS2", FCICompatReport::ExpectedFor(5, 5).bPixelStreaming2);
			TestTrue("5.8 PS2", FCICompatReport::ExpectedFor(5, 8).bPixelStreaming2);
		});

		It("puts the component curve accessors on 5.8 only", [this]()
		{
			// USkeletalMeshComponent::GetCurveValue / GetAnimCurves exist at
			// SkeletalMeshComponent.h:664/:667 in 5.8 and nowhere in 5.5's copy.
			TestFalse("5.5 curve accessors", FCICompatReport::ExpectedFor(5, 5).bSkelMeshCompCurveAccessors);
			TestFalse("5.6 curve accessors", FCICompatReport::ExpectedFor(5, 6).bSkelMeshCompCurveAccessors);
			TestTrue("5.8 curve accessors", FCICompatReport::ExpectedFor(5, 8).bSkelMeshCompCurveAccessors);
		});

		It("keeps the legacy SmartName curve API on pre-5.6 only", [this]()
		{
			// The one flag that runs the other way: 5.8 DELETED (not merely deprecated) the
			// UID-based AddCurveValue overloads and the FSmartName FAnimationCurveIdentifier
			// constructors that still compile in 5.5.4. Code written against 5.5 breaks forward.
			TestTrue("5.5 legacy curve API", FCICompatReport::ExpectedFor(5, 5).bLegacySmartNameCurveApi);
			TestFalse("5.8 legacy curve API", FCICompatReport::ExpectedFor(5, 8).bLegacySmartNameCurveApi);
		});

		It("marks the legacy curve API and MetaHuman Character as mutually exclusive at every version", [this]()
		{
			// Mirrors the static_assert in CICompanionCompat.h. Asserted across a range rather than
			// at one point so that a future edit to the table cannot make them overlap somewhere in
			// the middle without a test noticing.
			for (int32 Minor = 5; Minor <= 12; ++Minor)
			{
				const FCICompatReport Report = FCICompatReport::ExpectedFor(5, Minor);
				TestFalse(*FString::Printf(TEXT("5.%d not both"), Minor),
					Report.bMetaHumanCharacter && Report.bLegacySmartNameCurveApi);
			}
		});

		It("is monotonic: capabilities appear once and never disappear", [this]()
		{
			// Every flag here describes a capability the engine GAINED. If a future table edit makes
			// one flicker off at a later version, that is almost certainly a typo in a comparison,
			// and it would otherwise only surface as a mis-selected #if on one engine.
			bool bPrevMetaHuman = false;
			bool bPrevMcp = false;
			bool bPrevAccessors = false;
			for (int32 Minor = 5; Minor <= 12; ++Minor)
			{
				const FCICompatReport Report = FCICompatReport::ExpectedFor(5, Minor);
				TestFalse(*FString::Printf(TEXT("5.%d MetaHumanCharacter regressed"), Minor), bPrevMetaHuman && !Report.bMetaHumanCharacter);
				TestFalse(*FString::Printf(TEXT("5.%d MCP regressed"), Minor), bPrevMcp && !Report.bUnrealMcp);
				TestFalse(*FString::Printf(TEXT("5.%d curve accessors regressed"), Minor), bPrevAccessors && !Report.bSkelMeshCompCurveAccessors);
				bPrevMetaHuman = Report.bMetaHumanCharacter;
				bPrevMcp = Report.bUnrealMcp;
				bPrevAccessors = Report.bSkelMeshCompCurveAccessors;
			}
		});

		It("treats a major version bump as newer than any 5.x", [this]()
		{
			// Guards the comparison itself: a naive `Minor >= 6` test would wrongly report UE 6.0 as
			// lacking every 5.6+ capability.
			const FCICompatReport Six = FCICompatReport::ExpectedFor(6, 0);
			TestTrue("6.0 MetaHumanCharacter", Six.bMetaHumanCharacter);
			TestTrue("6.0 curve accessors", Six.bSkelMeshCompCurveAccessors);
		});
	});

	Describe("compiled binding", [this]()
	{
		It("matches the table row for the engine actually running", [this]()
		{
			// The only assertion here that needs both engines to be fully exercised. On 5.5 it proves
			// the 5.5 #if branches were selected; on 5.8, the 5.8 ones. A contradictory or stale
			// -D override in CICompanion.Build.cs shows up as a named flag in the diff rather than as
			// a bare "false != true".
			const FCICompatReport Compiled = FCICompatReport::Compiled();
			const FCICompatReport Expected = FCICompatReport::ExpectedForRunningEngine();
			const TArray<FString> Differences = Compiled.Diff(Expected);

			TestEqual(
				*FString::Printf(TEXT("compiled flags match running engine (%s): %s"),
					*Expected.Describe(), *FString::Join(Differences, TEXT("; "))),
				Differences.Num(), 0);
		});

		It("was compiled against the engine version now running", [this]()
		{
			// Catches a stale binary: the plugin .dll built against one engine and loaded by another.
			// That produces flag values that look self-consistent but describe the wrong engine, and
			// it is otherwise very hard to spot.
			const FCICompatReport Compiled = FCICompatReport::Compiled();
			const FEngineVersion& Running = FEngineVersion::Current();
			TestEqual("major", Compiled.EngineMajor, static_cast<int32>(Running.GetMajor()));
			TestEqual("minor", Compiled.EngineMinor, static_cast<int32>(Running.GetMinor()));
		});

		It("agrees with the raw macros it mirrors", [this]()
		{
			// Guards against the mirror in Compiled() drifting from the macros in CICompanionCompat.h
			// -- e.g. someone adding a flag to the header and forgetting to copy it across, which
			// would leave the report quietly reporting a default.
			const FCICompatReport Compiled = FCICompatReport::Compiled();
			TestEqual("MetaHumanCharacter", Compiled.bMetaHumanCharacter, CI_WITH_METAHUMAN_CHARACTER != 0);
			TestEqual("UnrealMcp", Compiled.bUnrealMcp, CI_WITH_UNREAL_MCP != 0);
			TestEqual("RigLogicIsEpic13", Compiled.bRigLogicIsEpic13, CI_RIGLOGIC_IS_EPIC_13 != 0);
			TestEqual("SkelMeshCompCurveAccessors", Compiled.bSkelMeshCompCurveAccessors, CI_WITH_SKELMESHCOMP_CURVE_ACCESSORS != 0);
			TestEqual("LegacySmartNameCurveApi", Compiled.bLegacySmartNameCurveApi, CI_WITH_LEGACY_SMARTNAME_CURVE_API != 0);
			TestEqual("Ps2SplitModules", Compiled.bPs2SplitModules, CI_WITH_PS2_SPLIT_MODULES != 0);
			TestEqual("Ps2AudioProducerUInterface", Compiled.bPs2AudioProducerUInterface, CI_WITH_PS2_AUDIO_PRODUCER_UINTERFACE != 0);
			TestEqual("Ps2WeakAudioSinks", Compiled.bPs2WeakAudioSinks, CI_WITH_PS2_WEAK_AUDIO_SINKS != 0);
		});
	});

	Describe("diff reporting", [this]()
	{
		It("reports no differences between identical reports", [this]()
		{
			const FCICompatReport A = FCICompatReport::ExpectedFor(5, 8);
			TestEqual("identical", A.Diff(A).Num(), 0);
			TestTrue("operator==", A == A);
		});

		It("names the flags that differ", [this]()
		{
			const FCICompatReport Five = FCICompatReport::ExpectedFor(5, 5);
			const FCICompatReport Eight = FCICompatReport::ExpectedFor(5, 8);
			const TArray<FString> Differences = Five.Diff(Eight);

			TestTrue("5.5 differs from 5.8", Differences.Num() > 0);
			TestTrue("operator!=", Five != Eight);

			// The message has to be actionable on its own, since a CI log is often all anyone sees.
			const FString Joined = FString::Join(Differences, TEXT("; "));
			TestTrue("names MetaHumanCharacter", Joined.Contains(TEXT("MetaHumanCharacter")));
			TestTrue("names the minor version", Joined.Contains(TEXT("EngineMinor")));
		});
	});

	Describe("Describe()", [this]()
	{
		It("includes the engine version and the flag values", [this]()
		{
			const FString Text = FCICompatReport::ExpectedFor(5, 8).Describe();
			TestTrue("has version", Text.Contains(TEXT("UE 5.8")));
			TestTrue("has a flag", Text.Contains(TEXT("MetaHumanCharacter=1")));
			TestTrue("has PS2", Text.Contains(TEXT("PS2=1")));
		});
	});
}
