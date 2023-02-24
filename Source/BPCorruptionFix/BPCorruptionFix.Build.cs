using UnrealBuildTool;

public class BPCorruptionFix : ModuleRules
{
	public BPCorruptionFix(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });

		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "EditorStyle", "PropertyEditor", "Slate", "SlateCore", "SubobjectEditor", "SubobjectDataInterface", "ToolMenus", "Kismet" });
	}
}
