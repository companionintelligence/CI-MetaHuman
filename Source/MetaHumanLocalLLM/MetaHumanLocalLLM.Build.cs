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

		// The licence and attributions travel with every package, staged as loose (NonUFS) files
		// beside the paks: <archive>/MetaHumanLocalLLM/LICENSE, NOTICE.md and CREDITS.md. The cooked
		// machine room contains eight CC BY 4.0 Sketchfab models, and CC BY requires their attribution
		// wherever the set is shared (NOTICE.md, "3D models under CC BY 4.0"). PolyForm Noncommercial
		// requires its terms and Required Notice to go with every copy. Staged here rather than copied
		// by each build script, so every packaging path gets them: Scripts/BuildNative.ps1 and both
		// Dockerfiles only check that the package came out with all three.
		foreach (string LegalFile in new string[] { "LICENSE", "NOTICE.md", "CREDITS.md" })
		{
			RuntimeDependencies.Add("$(ProjectDir)/" + LegalFile, StagedFileType.NonUFS);
		}

		// The Montserrat font faces ship too (Config/DefaultGame.ini always-cooks /Game/Fonts), and
		// the SIL Open Font License wants its text with every copy of the fonts, so NOTICE.md asks
		// every packaged build to carry it: <archive>/MetaHumanLocalLLM/Content/Fonts/OFL-Montserrat.txt.
		RuntimeDependencies.Add("$(ProjectDir)/Content/Fonts/OFL-Montserrat.txt", StagedFileType.NonUFS);
	}
}
