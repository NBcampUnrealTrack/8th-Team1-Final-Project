using UnrealBuildTool;

public class MDMultiUser : ModuleRules
{
    public MDMultiUser(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "UnrealEd", "Slate", "SlateCore",
            "LevelEditor", "EditorFramework", "MeshDescription", "ToolMenus", "ApplicationCore", "InputCore", "MultiUserClient", "Concert", "ConcertClient", "ConcertSyncClient"
        });
    }
}
