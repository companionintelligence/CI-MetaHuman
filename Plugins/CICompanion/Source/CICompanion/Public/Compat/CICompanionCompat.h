#pragma once

// Engine feature drift between UE 5.5 and UE 5.8, in one place.
//
// WHY THIS FILE EXISTS
// The project cooks from one tree onto two engines (Deployment/Dockerfile.amd --build-arg
// RELEASE=5.5|5.8). Until now that was handled entirely at DEPLOY time -- BuildSettingsVersion.Latest,
// "Optional": true plugin markers, and a Dockerfile that deletes plugins and content when the minor
// version is < 6. There was no COMPILE-time layer at all: a repository-wide search for
// ENGINE_MAJOR_VERSION / ENGINE_MINOR_VERSION / Version.h returned zero hits. This header is that
// layer.
//
// EVIDENCE DISCIPLINE
// Every flag below was measured against the two engines installed on the authoring machine:
//   UE 5.5.4 (CL 40574608)   C:\Program Files\Epic Games\UE_5.5
//   UE 5.8.1 (CL 56057345)   C:\Program Files\Epic Games\UE_5.8   <- note: 5.8.1, not 5.8.0
// Only those two points are VERIFIED. 5.6 and 5.7 were not available to test, so every boundary
// placed at 5.6 is an INFERENCE, and each one says so and cites the signal it was inferred from
// (usually the engine's own UE_DEPRECATED(5.x) macros, which date a change precisely).
//
// USAGE RULE
// Prefer a named CI_WITH_* feature flag over a raw version test at the call site. The version is an
// implementation detail of the flag; the call site cares about the capability. When a flag turns out
// to be wrong for some engine, it is fixed here once rather than at N call sites.

#include "Runtime/Launch/Resources/Version.h"
#include "Misc/EngineVersionComparison.h"

// ---------------------------------------------------------------------------------------------
// Version gate
// ---------------------------------------------------------------------------------------------
//
// UE_VERSION_NEWER_THAN / UE_VERSION_OLDER_THAN are the idiomatic engine gates and BOTH exist in
// 5.5.4 (EngineVersionComparison.h:13,:18) and 5.8.1 (:19,:24).
//
// UE_VERSION_NEWER_THAN_OR_EQUAL DELIBERATELY IS NOT USED. It was added in 5.8
// (EngineVersionComparison.h:14) and does NOT exist in 5.5.4 -- using it here would make the
// compatibility header itself fail to compile on the older of the two engines it exists to support.
// That is the exact class of bug this file is meant to prevent, so it is called out rather than
// silently avoided.

#define CI_UE_AT_LEAST(Major, Minor) (!UE_VERSION_OLDER_THAN(Major, Minor, 0))
#define CI_UE_BEFORE(Major, Minor)   (UE_VERSION_OLDER_THAN(Major, Minor, 0))

// ---------------------------------------------------------------------------------------------
// Feature flags
// ---------------------------------------------------------------------------------------------
//
// Each may be overridden from CICompanion.Build.cs via PublicDefinitions, which is the only place
// that can probe for a plugin/module actually being present rather than inferring it from a version
// number. The #ifndef guards make the version-derived value a DEFAULT, not a mandate.

// -- MetaHuman ---------------------------------------------------------------------------------
//
// VERIFIED: UE 5.5.4 ships exactly one MetaHuman plugin, MetaHumanSDK (2 modules), plus a
// MetaHumanRuntime.uplugin whose own Description reads "Deprecated plugin now redirected to
// MetaHumanSDK" and which declares zero modules. `grep -rn UMetaHumanCharacter` over the whole 5.5
// MetaHuman and RigLogic trees returns nothing.
// VERIFIED: UE 5.8.1 adds a top-level Engine/Plugins/MetaHuman/ directory that does not exist in 5.5
// at all -- MetaHumanAnimator (28 modules), MetaHumanCharacter, MetaHumanCoreTechLib,
// MetaHumanLiveLink, MetaHumanCrowd, MetaHumanCoreML, MetaHumanCalibrationProcessing,
// MetaHumanAnimationTools -- with UMetaHumanCharacter at
// Plugins/MetaHuman/MetaHumanCharacter/Source/MetaHumanCharacter/Public/MetaHumanCharacter.h:206.
//
// INFERRED boundary: 5.6. This matches the boundary Deployment/Dockerfile.amd already uses
// (`if [ "${MINOR:-8}" -lt 6 ]`), so the compile-time flag and the cook-time strip agree by
// construction. If 5.6/5.7 ever get tested and disagree, fix it here and the Dockerfile together.
#ifndef CI_WITH_METAHUMAN_CHARACTER
	#define CI_WITH_METAHUMAN_CHARACTER CI_UE_AT_LEAST(5, 6)
#endif

// VERIFIED: the entire MCP / Toolsets surface (ModelContextProtocol, MCPClientToolset, AllToolsets,
// ToolsetRegistry, AIAssistant and ~25 *Toolset plugins) exists only under UE 5.8.1's
// Engine/Plugins/Experimental. UE 5.5.4 has none of it.
#ifndef CI_WITH_UNREAL_MCP
	#define CI_WITH_UNREAL_MCP CI_UE_AT_LEAST(5, 6)
#endif

// VERIFIED: RigLogic ships in both, at the same path with the same five modules, but it was
// re-versioned AND re-authored: 5.5 RigLogic.uplugin is Version 10.2 / "10.2.5" / CreatedBy
// "3Lateral" / IsBetaVersion true; 5.8 is Version 13.2 / "13.2.5" / CreatedBy "Epic Games, Inc." /
// IsBetaVersion false. FAnimNode_RigLogic lives in the same module and header in both but its
// private API was rewritten (5 methods -> 13, three new nested structs), and its export style
// flipped from whole-struct RIGLOGICMODULE_API to MinimalAPI + per-method UE_API -- so the TYPE is
// no longer dllexported in 5.8 even though every method still is.
//
// This flag exists so that a path-or-module-name probe cannot report "no change" and be badly wrong,
// which is exactly what would happen here: the layout is identical across a three-major-version jump.
#ifndef CI_RIGLOGIC_IS_EPIC_13
	#define CI_RIGLOGIC_IS_EPIC_13 CI_UE_AT_LEAST(5, 6)
#endif

// -- Skeletal mesh / animation curves ----------------------------------------------------------
//
// VERIFIED: USkeletalMeshComponent::GetCurveValue(FName, float, float&) and GetAnimCurves() are
// 5.8-ONLY additions on the component -- they do not exist in 5.5.4's SkeletalMeshComponent.h. In
// 5.5 the equivalent lives only on UAnimInstance. See CISkeletalMeshCompat.h for the shim.
#ifndef CI_WITH_SKELMESHCOMP_CURVE_ACCESSORS
	#define CI_WITH_SKELMESHCOMP_CURVE_ACCESSORS CI_UE_AT_LEAST(5, 8)
#endif

// VERIFIED: 5.8 DELETED (did not merely deprecate) overloads that still compile in 5.5.4:
// UAnimInstance::AddCurveValue(SmartName::UID_Type, float) and (const FSmartNameMapping&, const
// FName&, float) [5.5 AnimInstance.h:1502-1506], FAnimInstanceProxy::AddCurveValue(const
// FSmartNameMapping&, ...) [5.5 AnimInstanceProxy.h:963-964], and both FSmartName/UID
// FAnimationCurveIdentifier constructors [5.5 CurveIdentifier.h:42-54].
//
// This is a FORWARD-compatibility trap, the opposite direction to everything else here: code written
// against 5.5 compiles there with only a deprecation warning and then hard-fails on 5.8. Nothing in
// CICompanion uses these today; the flag exists so that if anyone reaches for them the guard is
// already named.
#ifndef CI_WITH_LEGACY_SMARTNAME_CURVE_API
	#define CI_WITH_LEGACY_SMARTNAME_CURVE_API CI_UE_BEFORE(5, 6)
#endif

// -- Pixel Streaming ---------------------------------------------------------------------------
//
// VERIFIED: PixelStreaming2 ships NON-BETA in BOTH engines at Engine/Plugins/Media/PixelStreaming2.
// This corrects a widely-held assumption in the project's own docs that 5.5 is the "better Pixel
// Streaming" engine -- for plugin availability the two are equivalent. (The AMD/RADV render crash
// documented in Docs/FOLLOW-UP.md is a Mesa driver bug, not an engine-version difference.)
//
// VERIFIED: the browser -> C++ data-channel recipe is unchanged across both engines:
//   IPixelStreaming2Module::Get().FindStreamer(...) -> GetInputHandler().Pin()
//     -> GetToStreamerProtocol()->Add(Name, {EPixelStreaming2MessageTypes::String})
//     -> RegisterMessageHandler(Name, [](FString SourceId, FMemoryReader Ar){ ... })
// MessageHandlerFn is declared identically in both (5.5 IPixelStreaming2InputHandler.h:87,
// 5.8 :89). So the INPUT path needs no shim -- which matters, because that path is the fix for
// GAP-ANALYSIS.md's gap #2 ("nobody can address her").

// VERIFIED: 5.5's PixelStreaming2.uplugin declares 6 modules; 5.8's declares 8, adding
// PixelStreaming2Settings and PixelStreaming2RTC. Several public headers moved between modules in
// 5.8 (IPixelStreaming2Streamer.h and the audio/video sink+producer headers moved from
// PixelStreaming2 to PixelStreaming2Core; stats headers moved to PixelStreaming2RTC). Includes still
// resolve from a module that depends only on "PixelStreaming2" because 5.8 lists PixelStreaming2Core
// as a PUBLIC dependency -- but the module list in Build.cs differs, so it is gated.
#ifndef CI_WITH_PS2_SPLIT_MODULES
	#define CI_WITH_PS2_SPLIT_MODULES CI_UE_AT_LEAST(5, 8)
#endif

// VERIFIED: the audio PRODUCER API is genuinely incompatible, and this is the one Pixel Streaming
// area that actually needs a shim.
//   5.5: IPixelStreaming2AudioProducer is a plain C++ class with
//        `virtual void PushAudio(const float*, int32, int32, int32) = 0`, obtained from
//        IPixelStreaming2Module::CreateAudioProducer().
//   5.8: IPixelStreaming2AudioProducer is a UINTERFACE. PushAudio is `final` and merely broadcasts
//        FOnAudioPushed. You implement the interface and register it with
//        IPixelStreaming2Streamer::AddAudioProducer (which does not exist in 5.5).
//        IPixelStreaming2Module::CreateAudioProducer is UE_DEPRECATED(5.7).
//
// INFERRED boundary: 5.6, dated from the engine's own UE_DEPRECATED(5.6) on the old
// AddAudioConsumer overloads. That is a stronger signal than a guess, but it is still not a direct
// 5.6 measurement.
//
// This matters operationally: pushing Piper TTS audio into the WebRTC stream is on the critical path
// twice (audio out, and the phoneme timing that drives the mouth), and per Docs/GAP-ANALYSIS.md
// "audio over Pixel Streaming is unverified".
#ifndef CI_WITH_PS2_AUDIO_PRODUCER_UINTERFACE
	#define CI_WITH_PS2_AUDIO_PRODUCER_UINTERFACE CI_UE_AT_LEAST(5, 6)
#endif

// VERIFIED: IPixelStreaming2Streamer::GetPeerAudioSink / GetUnlistenedAudioSink return a raw
// IPixelStreaming2AudioSink* in 5.5 and a TWeakPtr<IPixelStreaming2AudioSink> in 5.8.
#ifndef CI_WITH_PS2_WEAK_AUDIO_SINKS
	#define CI_WITH_PS2_WEAK_AUDIO_SINKS CI_UE_AT_LEAST(5, 6)
#endif

// ---------------------------------------------------------------------------------------------
// Build-envelope note (not a flag -- there is nothing to switch on, but it explains real failures)
// ---------------------------------------------------------------------------------------------
//
// Source/MetaHumanLocalLLM.Target.cs sets DefaultBuildSettings = BuildSettingsVersion.Latest with a
// comment implying that is the safe portable choice. It compiles on both, but it is NOT semantically
// neutral, and the risk runs the OPPOSITE way to what the comment suggests:
//
//   BuildSettingsVersion.Latest  == V5 on 5.5.4  ->  V7 on 5.8.1
//     V6 turns on WindowsPlatform.bStrictInlineConformance and UndefinedIdentifierWarningLevel=Error
//     V7 turns on ReturnTypeWarningLevel, DanglingWarningLevel, UnreachableCodeWarningLevel = Error
//
// So identical source is held to five extra warnings-as-errors on 5.8. 5.5.4 is the PERMISSIVE
// engine; 5.8 is the strict one. Code that builds clean on 5.5 can still fail on 5.8 with zero
// source differences. Pin DefaultBuildSettings = BuildSettingsVersion.V5 (which exists in both) if an
// identical compile envelope is ever wanted.
//
// EngineIncludeOrderVersion is the reverse and has no safe pin: 5.5.4 tops out at Unreal5_5, while
// 5.8 marks Unreal5_1..Unreal5_5 [Obsolete("unsupported")] and sets Oldest = Unreal5_6 (which does
// not exist in 5.5.4). There is NO value pinnable across both installs, so `Latest` is genuinely
// correct there -- an unusual case where the seemingly risky choice is the only working one.

// ---------------------------------------------------------------------------------------------
// Invariants
// ---------------------------------------------------------------------------------------------

static_assert(ENGINE_MAJOR_VERSION == 5, "CICompanion targets Unreal Engine 5 only.");

// The plugin's own supported window, per CICompanion.uplugin's description ("Unreal 5.5-5.8").
// Compiling outside it is not necessarily broken, but every flag above was reasoned about within it,
// so stepping outside should be a deliberate act rather than a surprise at runtime.
static_assert(CI_UE_AT_LEAST(5, 5), "CICompanion supports UE 5.5 and newer.");

// A version cannot simultaneously have and not have the MetaHuman Character asset type. This catches
// a contradictory Build.cs override, which is the most likely way these flags go wrong.
static_assert(!(CI_WITH_METAHUMAN_CHARACTER && CI_WITH_LEGACY_SMARTNAME_CURVE_API),
	"CI_WITH_METAHUMAN_CHARACTER (5.6+) and CI_WITH_LEGACY_SMARTNAME_CURVE_API (pre-5.6) are mutually "
	"exclusive; check the PublicDefinitions overrides in CICompanion.Build.cs.");

// The 5.8-only component curve accessors imply the 5.6+ world; the reverse need not hold.
static_assert(!CI_WITH_SKELMESHCOMP_CURVE_ACCESSORS || CI_WITH_METAHUMAN_CHARACTER,
	"CI_WITH_SKELMESHCOMP_CURVE_ACCESSORS is 5.8+, which is necessarily 5.6+.");
