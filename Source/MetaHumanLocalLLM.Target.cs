using UnrealBuildTool;
using System.Collections.Generic;

public class MetaHumanLocalLLMTarget : TargetRules
{
	public MetaHumanLocalLLMTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		// Latest (not a pinned V7/Unreal5_8) so this project builds unchanged on any engine
		// from 5.5 through 5.8 — the AMD container can target either. Enum values like V7 don't
		// even exist in 5.5's UnrealBuildTool, so a pinned value would fail to compile there.
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange(new string[] { "MetaHumanLocalLLM" });
	}
}
