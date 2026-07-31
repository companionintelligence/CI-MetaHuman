#pragma once

#include "CoreMinimal.h"
#include "Compat/CICompanionCompat.h"

// Makes the compile-time compatibility flags testable.
//
// THE PROBLEM THIS SOLVES
// A polyfill's substance is #if branches resolved at compile time. A runtime automation spec running
// on 5.8 can never execute the 5.5 branch, so the obvious test ("call the shim, check the answer")
// only ever covers half the code and silently reports a pass. Testing the shim by running it is a
// tautology: it tests the branch the compiler already picked.
//
// THE SPLIT THAT MAKES IT TESTABLE
// The decision -- "which capabilities does engine X.Y have?" -- is separated from the #if that acts
// on it, and expressed as a PURE FUNCTION of a version number:
//
//   ExpectedFor(Major, Minor)  ->  FCICompatReport      pure, no #if, callable for ANY version
//   Compiled()                 ->  FCICompatReport      what this binary was actually built with
//
// That yields two independent kinds of test, and together they cover both branches:
//
//   1. TABLE TESTS run on ANY engine. ExpectedFor(5,5) and ExpectedFor(5,8) are evaluated on
//      whichever engine happens to be running, so the 5.5 row is fully exercised even when only 5.8
//      is installed. This is the part that would otherwise be untestable.
//
//   2. A BINDING TEST runs per engine: Compiled() == ExpectedFor(running engine). It proves the
//      preprocessor actually selected the row the table says it should have. Run it on 5.5 and it
//      validates the 5.5 #if branches; run it on 5.8 and it validates the 5.8 ones. This is the only
//      check that needs both engines, and it is one assertion rather than a whole suite.
//
// A wrong or contradictory -D override in CICompanion.Build.cs shows up as a Diff() entry naming the
// exact flag, on whichever engine the suite is run.

struct CICOMPANION_API FCICompatReport
{
	// The engine this report describes. For Compiled() these come from the Version.h the translation
	// unit saw; for ExpectedFor they are the requested version.
	int32 EngineMajor = 0;
	int32 EngineMinor = 0;

	bool bMetaHumanCharacter = false;			// UMetaHumanCharacter asset type + the MetaHuman Creator plugin set
	bool bUnrealMcp = false;					// ModelContextProtocol / MCPClientToolset / AllToolsets / AIAssistant
	bool bRigLogicIsEpic13 = false;				// RigLogic 13.2.5 (Epic) vs 10.2.5 (3Lateral, beta)
	bool bSkelMeshCompCurveAccessors = false;	// USkeletalMeshComponent::GetCurveValue / GetAnimCurves
	bool bLegacySmartNameCurveApi = false;		// UAnimInstance::AddCurveValue(UID) etc. still present
	bool bPs2SplitModules = false;				// PixelStreaming2Settings + PixelStreaming2RTC modules exist
	bool bPs2AudioProducerUInterface = false;	// IPixelStreaming2AudioProducer is a UINTERFACE
	bool bPs2WeakAudioSinks = false;			// audio sink getters return TWeakPtr, not a raw pointer

	// PixelStreaming2 itself ships in BOTH 5.5.4 and 5.8.1, non-beta. Recorded as a field rather than
	// omitted because "is Pixel Streaming available?" is the question people actually ask, and the
	// answer -- yes, on both -- is the single most commonly mis-stated fact about this project.
	bool bPixelStreaming2 = true;

	// What the flags in CICompanionCompat.h resolved to in THIS binary.
	static FCICompatReport Compiled();

	// What they SHOULD resolve to for an arbitrary engine version. Pure: no preprocessor, no engine
	// state, no I/O. This is the compatibility table, and it is the thing worth testing.
	static FCICompatReport ExpectedFor(int32 Major, int32 Minor);

	// ExpectedFor() applied to FEngineVersion::Current().
	static FCICompatReport ExpectedForRunningEngine();

	// Flags that differ, as human-readable "Name: expected=X actual=Y" lines. Empty when identical,
	// so a spec can assert emptiness and get a useful message on failure instead of "false != true".
	// Engine version fields are compared too, since a mismatch there means Compiled() was built
	// against a different engine than the one now running -- a stale-binary situation worth catching.
	TArray<FString> Diff(const FCICompatReport& Other) const;

	bool operator==(const FCICompatReport& Other) const { return Diff(Other).Num() == 0; }
	bool operator!=(const FCICompatReport& Other) const { return !(*this == Other); }

	// One-line summary for startup logs, e.g.
	//   "UE 5.8 | MetaHumanCharacter=1 MCP=1 RigLogic13=1 CurveAccessors=1 LegacyCurveApi=0 ..."
	FString Describe() const;
};
