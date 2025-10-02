using UnrealBuildTool;

public class PlanetaryCreationEditor : ModuleRules
{
    public PlanetaryCreationEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "UnrealEd",
            "EditorSubsystem",
            "EditorFramework"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "UnrealEd",
            "LevelEditor",
            "Projects",
            "RealtimeMeshComponent",
            "Blutility",
            "EditorStyle",
            "UMG",
            "WorkspaceMenuStructure"
        });
    }
}
