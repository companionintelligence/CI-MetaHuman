using UnrealBuildTool;
using System;

public class CICompanion : ModuleRules
{
	public CICompanion(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"HTTP",
			"Json",
			"JsonUtilities",
			"UMG",
			"Slate",
			"SlateCore",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AudioCapture",
			"AudioCaptureCore",
			"AudioMixer"
		});

		// ---------------------------------------------------------------------------------------
		// Engine version window
		// ---------------------------------------------------------------------------------------
		//
		// Target.Version is a ReadOnlyBuildVersion exposing MajorVersion / MinorVersion /
		// PatchVersion. Verified present and identical in both installed UBTs:
		//   5.5.4  Configuration/ReadOnlyTargetRules.cs:67   public ReadOnlyBuildVersion Version => Inner.Version;
		//   5.8.1  Configuration/ReadOnlyTargetRules.cs:68   (same)
		// with the three int members at BuildVersion.cs:294/296/298 in both.
		//
		// Fail here rather than let an unsupported engine surface as a wall of template errors from
		// some header that moved. The feature flags in Public/Compat/CICompanionCompat.h were reasoned
		// about inside this window and nowhere else.
		int major = Target.Version.MajorVersion;
		int minor = Target.Version.MinorVersion;

		if (major != 5 || minor < 5)
		{
			throw new BuildException(
				"CICompanion supports Unreal Engine 5.5 through 5.8; this engine reports {0}.{1}. " +
				"If you are intentionally moving the support window, update the feature flags and their " +
				"evidence in Public/Compat/CICompanionCompat.h and the compatibility table in " +
				"Private/Compat/CICompatReport.cpp together -- they are asserted against each other by " +
				"MetaHumanLocalLLM.Backend.Compat.", major, minor);
		}

		if (minor > 8)
		{
			// Not fatal: a newer engine most likely still works, since every flag is written as
			// "5.x and newer". But nobody has measured it, so say so at build time instead of letting
			// a silently-wrong flag reach a running avatar.
			//
			// System.Console, not UnrealBuildTool's Log: `Log` is not in scope inside ModuleRules in
			// either 5.5.4 or 5.8.1 (CS0103), and a Build.cs that fails to compile takes the whole
			// rules assembly with it -- which then reports as the far more confusing "Expecting to
			// find a type to be declared in a target rules named ...".
			Console.WriteLine(
				"CICompanion: engine {0}.{1} is newer than the measured support window (5.5-5.8). " +
				"Compatibility flags are extrapolated, not verified.", major, minor);
		}

		// ---------------------------------------------------------------------------------------
		// Overriding a compatibility flag
		// ---------------------------------------------------------------------------------------
		//
		// Every CI_WITH_* flag in Public/Compat/CICompanionCompat.h is wrapped in #ifndef, so any of
		// them can be forced from here, e.g.
		//
		//     PublicDefinitions.Add("CI_WITH_METAHUMAN_CHARACTER=0");
		//
		// Deliberately NOT done by default. The header derives each flag from the same engine version
		// UBT would report, so defining them here as well would create two sources of truth that could
		// drift apart. Override only for a real exception -- a custom engine build that backported or
		// removed a feature -- and expect MetaHumanLocalLLM.Backend.Compat's "compiled binding" test to
		// fail until the compatibility table is taught about it, which is the intended behaviour.

		// ---------------------------------------------------------------------------------------
		// Pixel Streaming (not yet a dependency, on purpose)
		// ---------------------------------------------------------------------------------------
		//
		// Nothing in this module references Pixel Streaming today -- a search for "PixelStreaming"
		// across the whole plugin Source tree returns no files -- so adding the modules now would be a
		// dependency with no consumer.
		//
		// When the browser -> C++ text path lands (Docs/GAP-ANALYSIS.md gap #2, "nobody can address
		// her"), add:
		//
		//     PrivateDependencyModuleNames.AddRange(new string[] { "PixelStreaming2", "PixelStreaming2Input" });
		//     if (minor >= 8)
		//     {
		//         // 5.8 split several public headers into these modules; they do not exist in 5.5.
		//         PrivateDependencyModuleNames.AddRange(new string[] { "PixelStreaming2Core", "PixelStreaming2Settings", "PixelStreaming2RTC" });
		//     }
		//
		// The INPUT api itself needs no shim -- IPixelStreaming2InputHandler::RegisterMessageHandler
		// and IPixelStreaming2DataProtocol::Add are declared identically in 5.5.4 and 5.8.1. The AUDIO
		// producer API does differ, and is gated by CI_WITH_PS2_AUDIO_PRODUCER_UINTERFACE.
		//
		// Note for anyone globbing engine build files while working on this: several PixelStreaming2
		// modules use a lowercase ".build.cs" extension in BOTH engines, so a case-sensitive
		// "*.Build.cs" search silently misses them.
	}
}
