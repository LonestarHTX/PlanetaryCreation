# Repository Guidelines

## Project Structure & Module Organization
- Core gameplay lives in `Source/PlanetaryCreation`; keep module headers lean with `.generated.h` last.
- Runtime meshing extensions belong under `Plugins/RealtimeMeshComponent`; isolate experiments here and prefer plugin math helpers for reuse.
- Game assets stay in `Content`; do not commit generated screenshots.
- Research references are archived in `ProceduralTectonicPlanetsPaper`; add addenda rather than editing the paper itself.
- Place automation tests in `Source/PlanetaryCreation/Private/Tests` once created.

## Build, Test, and Development Commands
- Launch playable build: `"<UE5>\Engine\Binaries\Win64\UnrealEditor.exe" PlanetaryCreation.uproject -game`.
- Rebuild editor target: `"<UE5>\Engine\Build\BatchFiles\Build.bat" PlanetaryCreationEditor Win64 Development -project="%CD%\PlanetaryCreation.uproject"`.
- Regenerate project files after module changes with `"<UE5>\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" -projectfiles`.
- Run Milestone 3 automation suite: `powershell -ExecutionPolicy Bypass -File .\Scripts\RunMilestone3Tests.ps1 [-ArchiveLogs]`.

## Coding Style & Naming Conventions
- Follow Unreal C++ style: 4-space indentation, no tabs.
- Prefix classes with `A`, `U`, `F`, or `I`; use PascalCase for functions, camelCase for locals, ALL_CAPS for constants.
- Wrap experimental runtime mesh code in `#if UE_BUILD_DEVELOPMENT` guards until production-ready.

## Testing Guidelines
- Use Unreal's Automation Testing framework (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`) and name suites `FPlanetaryCreation<Feature>Test`.
- Run targeted tests headless: `"<UE5>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" PlanetaryCreation.uproject -ExecCmds="Automation RunTests PlanetaryCreation" -TestExit="Automation Test Queue Empty"`.
- Inspect logs with `powershell -Command "Get-Content 'Saved/Logs/PlanetaryCreation.log' | Select-String 'Result={Success}'"`.

## Build Expectations
- After **any** change to Unreal C++ or Slate, run the WSL-friendly build command from `CLAUDE.md` (see below).
  ```bash
  "/mnt/c/Program Files/Epic Games/UE_5.5/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" \
    PlanetaryCreationEditor Win64 Development \
    -project="C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject" \
    -WaitMutex -FromMsBuild
  ```
- Confirm the build **succeeds** before proceeding. If the command fails in the sandbox, rerun it immediately and include the failure reason in your summary.

## Commit & Pull Request Guidelines
- Write concise, imperative commit subjects ≤72 chars (e.g., `Add plate hotspot sampling`); include rationale/test notes in the body when helpful.
- PRs should link Jira/Trello tasks, outline tectonic simulation impact, list validation steps (editor launch, automation, perf captures), and attach before/after renders for visual changes.

## Planning & Collaboration
- Review `Docs/PlanningAgentPlan.md` when sequencing work to stay aligned with milestone checkpoints.
- Keep `RealtimeMeshComponent_HowTo.md` open while extending runtime mesh features to maintain API consistency.
