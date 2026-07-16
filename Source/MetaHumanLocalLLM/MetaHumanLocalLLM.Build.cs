using UnrealBuildTool;

// Thin primary game module. A code-based UE project needs one primary game module; all the
// actual companion functionality lives in the reusable CICompanion plugin, which a second
// Unreal project in the monorepo can enable independently.
public class MetaHumanLocalLLM : ModuleRules
{
	public MetaHumanLocalLLM(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"CICompanion"
		});
	}
}
