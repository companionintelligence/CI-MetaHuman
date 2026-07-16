using UnrealBuildTool;
using System.Collections.Generic;

public class MetaHumanLocalLLMEditorTarget : TargetRules
{
	public MetaHumanLocalLLMEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		// Latest so the editor target builds unchanged on 5.5-5.8 (V7/Unreal5_8 don't exist in
		// 5.5's UBT). Dev happens in the 5.8 editor (with the Unreal MCP); deploy can be 5.5.
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange(new string[] { "MetaHumanLocalLLM" });
	}
}
