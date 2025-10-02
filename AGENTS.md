# Repository Guidelines

## Project Structure & Module Organization
Core gameplay code lives in `Source/PlanetaryCreation`, following Unreal's primary module layout (`PlanetaryCreation.cpp`/`.h`, module rules in `PlanetaryCreation.Build.cs`). Runtime meshing features reside in `Plugins/RealtimeMeshComponent`, which ships with its own documentation and source; keep plugin additions isolated there. Game assets stay under `Content`. Research artifacts for the procedural tectonic workflow are archived in `ProceduralTectonicPlanetsPaper`; reference these when implementing simulation logic, but do not commit generated screenshots back into `Content`.

## Build, Test, and Development Commands
Use the Unreal Editor to iterate: `"<UE5>\Engine\Binaries\Win64\UnrealEditor.exe" PlanetaryCreation.uproject -game` launches the playable build. Rebuild the module from the command line with `"<UE5>\Engine\Build\BatchFiles\Build.bat" PlanetaryCreationEditor Win64 Development -project="%CD%\PlanetaryCreation.uproject"`. Regenerate Visual Studio project files after adding modules via `"<UE5>\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" -projectfiles`. Keep the `RealtimeMeshComponent_HowTo.md` open to cross-check API usage while coding.

## Coding Style & Naming Conventions
Adopt Unreal Engine C++ guidelines: 4-space indentation, no tabs; classes prefixed with `A`, `U`, `F`, or `I` as appropriate; functions in PascalCase; locals in camelCase; constants in ALL_CAPS. Keep headers lean and include the matching `.generated.h` last. Place new runtime mesh helpers beneath `Plugins/RealtimeMeshComponent/Source/...` and wrap experimental features with `UE_BUILD_DEVELOPMENT` guards until stable.

## Testing Guidelines
Author automation tests in `Source/PlanetaryCreation/Private/Tests` (create the folder if missing) using Unreal's Automation Testing framework. Name tests `FPlanetaryCreation<Feature>Test` and register via `IMPLEMENT_SIMPLE_AUTOMATION_TEST`. Run headless with `"<UE5>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" PlanetaryCreation.uproject -ExecCmds="Automation RunTests PlanetaryCreation" -TestExit="Automation Test Queue Empty"`. Capture benchmark data for mesh generation using `stat unit` and attach screenshots when filing regressions.

## Commit & Pull Request Guidelines
Follow concise, imperative commit messages similar to the existing history (`Initial project import`): first line <= 72 chars, body optional but explain rationale and testing. For pull requests, link Jira or Trello tasks, summarise tectonic simulation goals, list verification steps (editor launch, automation suite, performance captures), and include before/after imagery when modifying rendering. Request review from both gameplay and rendering peers whenever touching shared plugin code.

## Plugin & Paper Notes
When extending the real-time mesh pipeline, mirror the terminology from `ProceduralTectonicPlanetsPaper` (plates, ridges, hotspots) to keep research alignment clear. Store reusable math utilities in the plugin module so Blueprint-accessible components stay lightweight. Document any departures from the paper in code comments and update the paper folder with a short README addendum instead of editing the original PDF.

## Planning Resources
Consult `Docs/PlanningAgentPlan.md` for the milestone breakdown, owner assignments, and validation checkpoints that align the editor tool with the Procedural Tectonic Planets paper. Use it when sequencing sprint work or onboarding new collaborators.

