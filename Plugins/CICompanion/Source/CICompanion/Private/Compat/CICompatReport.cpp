#include "Compat/CICompatReport.h"

#include "Misc/EngineVersion.h"

FCICompatReport FCICompatReport::Compiled()
{
	FCICompatReport R;
	R.EngineMajor = ENGINE_MAJOR_VERSION;
	R.EngineMinor = ENGINE_MINOR_VERSION;

	// The ONLY place the macros are read. Everything downstream is data, which is what lets the rest
	// of the compatibility layer be tested without a preprocessor in the loop.
	R.bMetaHumanCharacter          = (CI_WITH_METAHUMAN_CHARACTER != 0);
	R.bUnrealMcp                   = (CI_WITH_UNREAL_MCP != 0);
	R.bRigLogicIsEpic13            = (CI_RIGLOGIC_IS_EPIC_13 != 0);
	R.bSkelMeshCompCurveAccessors  = (CI_WITH_SKELMESHCOMP_CURVE_ACCESSORS != 0);
	R.bLegacySmartNameCurveApi     = (CI_WITH_LEGACY_SMARTNAME_CURVE_API != 0);
	R.bPs2SplitModules             = (CI_WITH_PS2_SPLIT_MODULES != 0);
	R.bPs2AudioProducerUInterface  = (CI_WITH_PS2_AUDIO_PRODUCER_UINTERFACE != 0);
	R.bPs2WeakAudioSinks           = (CI_WITH_PS2_WEAK_AUDIO_SINKS != 0);
	R.bPixelStreaming2             = true;	// present and non-beta in every supported version
	return R;
}

FCICompatReport FCICompatReport::ExpectedFor(int32 Major, int32 Minor)
{
	// Deliberately free of #if. This is the compatibility TABLE, and it must evaluate identically no
	// matter which engine compiled it -- that property is what lets a spec running on 5.8 assert the
	// correct answers for 5.5.
	const auto AtLeast = [Major, Minor](int32 ReqMajor, int32 ReqMinor)
	{
		return Major > ReqMajor || (Major == ReqMajor && Minor >= ReqMinor);
	};

	FCICompatReport R;
	R.EngineMajor = Major;
	R.EngineMinor = Minor;

	// Boundaries and their evidence are documented in CICompanionCompat.h. In short: 5.5.4 and 5.8.1
	// are measured; the 5.6 boundaries are inferred, mostly from the engine's own UE_DEPRECATED(5.6)
	// and UE_DEPRECATED(5.7) markers on the superseded APIs.
	R.bMetaHumanCharacter         = AtLeast(5, 6);
	R.bUnrealMcp                  = AtLeast(5, 6);
	R.bRigLogicIsEpic13           = AtLeast(5, 6);
	R.bSkelMeshCompCurveAccessors = AtLeast(5, 8);
	R.bLegacySmartNameCurveApi    = !AtLeast(5, 6);
	R.bPs2SplitModules            = AtLeast(5, 8);
	R.bPs2AudioProducerUInterface = AtLeast(5, 6);
	R.bPs2WeakAudioSinks          = AtLeast(5, 6);
	R.bPixelStreaming2            = true;
	return R;
}

FCICompatReport FCICompatReport::ExpectedForRunningEngine()
{
	const FEngineVersion& Current = FEngineVersion::Current();
	return ExpectedFor(static_cast<int32>(Current.GetMajor()), static_cast<int32>(Current.GetMinor()));
}

TArray<FString> FCICompatReport::Diff(const FCICompatReport& Other) const
{
	TArray<FString> Out;

	const auto CompareInt = [&Out](const TCHAR* Name, int32 A, int32 B)
	{
		if (A != B)
		{
			Out.Add(FString::Printf(TEXT("%s: expected=%d actual=%d"), Name, B, A));
		}
	};
	const auto CompareBool = [&Out](const TCHAR* Name, bool A, bool B)
	{
		if (A != B)
		{
			Out.Add(FString::Printf(TEXT("%s: expected=%d actual=%d"), Name, B ? 1 : 0, A ? 1 : 0));
		}
	};

	CompareInt(TEXT("EngineMajor"), EngineMajor, Other.EngineMajor);
	CompareInt(TEXT("EngineMinor"), EngineMinor, Other.EngineMinor);
	CompareBool(TEXT("MetaHumanCharacter"), bMetaHumanCharacter, Other.bMetaHumanCharacter);
	CompareBool(TEXT("UnrealMcp"), bUnrealMcp, Other.bUnrealMcp);
	CompareBool(TEXT("RigLogicIsEpic13"), bRigLogicIsEpic13, Other.bRigLogicIsEpic13);
	CompareBool(TEXT("SkelMeshCompCurveAccessors"), bSkelMeshCompCurveAccessors, Other.bSkelMeshCompCurveAccessors);
	CompareBool(TEXT("LegacySmartNameCurveApi"), bLegacySmartNameCurveApi, Other.bLegacySmartNameCurveApi);
	CompareBool(TEXT("Ps2SplitModules"), bPs2SplitModules, Other.bPs2SplitModules);
	CompareBool(TEXT("Ps2AudioProducerUInterface"), bPs2AudioProducerUInterface, Other.bPs2AudioProducerUInterface);
	CompareBool(TEXT("Ps2WeakAudioSinks"), bPs2WeakAudioSinks, Other.bPs2WeakAudioSinks);
	CompareBool(TEXT("PixelStreaming2"), bPixelStreaming2, Other.bPixelStreaming2);
	return Out;
}

FString FCICompatReport::Describe() const
{
	return FString::Printf(
		TEXT("UE %d.%d | MetaHumanCharacter=%d MCP=%d RigLogic13=%d CurveAccessors=%d ")
		TEXT("LegacyCurveApi=%d PS2=%d PS2Split=%d PS2AudioUInterface=%d PS2WeakSinks=%d"),
		EngineMajor, EngineMinor,
		bMetaHumanCharacter ? 1 : 0, bUnrealMcp ? 1 : 0, bRigLogicIsEpic13 ? 1 : 0,
		bSkelMeshCompCurveAccessors ? 1 : 0, bLegacySmartNameCurveApi ? 1 : 0,
		bPixelStreaming2 ? 1 : 0, bPs2SplitModules ? 1 : 0,
		bPs2AudioProducerUInterface ? 1 : 0, bPs2WeakAudioSinks ? 1 : 0);
}
