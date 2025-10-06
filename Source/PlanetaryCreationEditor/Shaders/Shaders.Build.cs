using UnrealBuildTool;

public class PlanetaryCreationEditorShaders : ModuleRules
{
    public PlanetaryCreationEditorShaders(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "RHI",
            "RenderCore"
        });
    }
}
