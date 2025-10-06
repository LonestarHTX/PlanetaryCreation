using UnrealBuildTool;

public class PlanetaryCreationEditor : ModuleRules
{
    public PlanetaryCreationEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        if (Target.Type != TargetType.Editor)
        {
            throw new BuildException("PlanetaryCreationEditor module only supports editor targets.");
        }

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "UnrealEd",
            "EditorFramework"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "UnrealEd",
            "LevelEditor",
            "Projects",
            "RealtimeMeshComponent",
            "InputCore",
            "Blutility",
            "EditorStyle",
            "UMG",
            "WorkspaceMenuStructure",
            "ImageWrapper",
            "Json",
            "JsonUtilities",
            "RHI",                  // Milestone 6: GPU compute shader dependencies
            "RenderCore",
            "Renderer"
        });
    }
}
