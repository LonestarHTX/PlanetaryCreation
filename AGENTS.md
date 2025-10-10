# Repository Guidelines

## Project Structure & Module Organization
- Repository overview and quick-start live in `README.md`; scan it before new sessions.
- Core gameplay lives in `Source/PlanetaryCreation`; keep module headers lean with `.generated.h` last.
- Runtime meshing extensions belong under `Plugins/RealtimeMeshComponent`; isolate experiments here and prefer plugin math helpers for reuse.
- Game assets stay in `Content`; do not commit generated screenshots.
- Research references are archived in `ProceduralTectonicPlanetsPaper`; add addenda rather than editing the paper itself.
- Place automation tests in `Source/PlanetaryCreation/Private/Tests` once created.

## Build, Test, and Development Commands
- The editor now boots with the full paper pipeline (LOD 5 + Stage B/GPU/PBR). Run `r.PlanetaryCreation.PaperDefaults 0` when you need the lean M5 baseline (CPU profiling, older automation).
- Stream-power hydraulic erosion is part of the default Stage B stack and now costs ~1.7 ms at L7. Leave it enabled for parity/profiling runs; use `r.PlanetaryCreation.EnableHydraulicErosion 0` only when you explicitly need the lean M5 baseline.
- The toggle `r.PlanetaryCreation.UseGPUHydraulic` remains available (default `1`), but the pass still runs on the optimised CPU path—no GPU compute dispatch is required.
- Launch playable build: `"<UE5>\Engine\Binaries\Win64\UnrealEditor.exe" PlanetaryCreation.uproject -game`.
- Rebuild editor target: `"<UE5>\Engine\Build\BatchFiles\Build.bat" PlanetaryCreationEditor Win64 Development -project="%CD%\PlanetaryCreation.uproject"`.
- Regenerate project files after module changes with `"<UE5>\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" -projectfiles`.
- Run Milestone 3 automation suite: `powershell -ExecutionPolicy Bypass -File .\Scripts\RunMilestone3Tests.ps1 [-ArchiveLogs]`.
- CPU-only automation (Milestone 3/5 regressions, etc.) should prepend `r.PlanetaryCreation.PaperDefaults 0` in `-ExecCmds` so the run matches the lean M5 configuration.
- GPU preview parity suites (Milestone 6): launch from Windows PowerShell via `Automation RunTests PlanetaryCreation.Milestone6.GPU`.
- Running GPU automation from WSL **must** hop out to a native Windows shell (PowerShell or `cmd.exe`). Launching `UnrealEditor-Cmd.exe` directly inside WSL fails with `UtilBindVsockAnyPort` and the command times out after ~10 s.
- Typical PowerShell pattern (swap the test name as needed):
  ```powershell
  & 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
    'C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject' `
    -SetCVar='r.PlanetaryCreation.StageBProfiling=1' `
    -ExecCmds='Automation RunTests PlanetaryCreation.Milestone6.GPU.OceanicParity' `
    -TestExit='Automation Test Queue Empty' -unattended -nop4 -nosplash -log
  ```
  - Swap the test name for `PlanetaryCreation.Milestone6.GPU.ContinentalParity` to capture the complementary Stage B tables; both suites now emit `[StageB][Profile]`/`[StageB][CacheProfile]` as long as the command is launched this way.
  - If you previously ran `r.PlanetaryCreation.PaperDefaults 0`, remember to re-enable (`r.PlanetaryCreation.PaperDefaults 1`) before GPU suites so Stage B/GPU preview are active.
  ```
  - **Do not** append `Quit` inside `-ExecCmds`; the automation controller exits on its own when `-TestExit="Automation Test Queue Empty"` is present. Adding `Quit` or manually terminating the process prevents the queue from running, which was the root cause of earlier “nothing happens” sessions.
  - Let the command run to completion—the Stage B `[StageB][Profile]` / `[StageB][CacheProfile]` lines appear once the parity suite reaches the replay phase.

## Coding Style & Naming Conventions
- Follow Unreal C++ style: 4-space indentation, no tabs.
- Prefix classes with `A`, `U`, `F`, or `I`; use PascalCase for functions, camelCase for locals, ALL_CAPS for constants.
- Wrap experimental runtime mesh code in `#if UE_BUILD_DEVELOPMENT` guards until production-ready.

## Testing Guidelines
- Quick smoke after environment changes:
  1. Launch the editor (defaults land in LOD 5 + Stage B/GPU/PBR).
  2. Step the simulation once and confirm plate colours + amplification blend render (no flat grey fallback).
  3. Toggle `PaperDefaults 0/1` to ensure the switch still works before running heavy automation.
  4. When running the Milestone 6 parity suites, confirm `[StageB][Profile]` logs include `Hydraulic ≈ 1.6–1.8 ms` to verify the optimised pass is active.
- Use Unreal's Automation Testing framework (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`) and name suites `FPlanetaryCreation<Feature>Test`.
- Run targeted tests headless: `"<UE5>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" PlanetaryCreation.uproject -ExecCmds="Automation RunTests PlanetaryCreation" -TestExit="Automation Test Queue Empty"`.
- Inspect logs with `powershell -Command "Get-Content 'Saved/Logs/PlanetaryCreation.log' | Select-String 'Result={Success}'"`.
- If automation appears to hang, check for stale `UnrealEditor-Cmd.exe` processes holding DLL locks and terminate them before rebuilding (see troubleshooting below).
- GPU compute suites (e.g., Milestone 6 preview/parity) must run with a real graphics RHI. Launch them from Windows PowerShell, for example:
  ```powershell
  & 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' \
    'C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject' \
    -ExecCmds "Automation RunTests PlanetaryCreation.Milestone6.GPU.PreviewVertexParity; Quit" \
    -unattended -nop4 -nosplash
  ```
  Running the Windows binary directly inside WSL fails with `UtilBindVsockAnyPort`; always shell out through PowerShell or `cmd.exe` on Windows when you need GPU automation.
- Guard GPU-reliant automation with a NullRHI check so tests auto-skip when `GDynamicRHI` reports `NullDrv`.
- After GPU preview runs, verify the log for `[OceanicGPUPreview] ... SeamLeft=...`/`SeamRight=...` (and the parity test summaries) to confirm height-map coverage.

## Build Expectations
- After **any** change to Unreal C++ or Slate, run the WSL-friendly build command from `CLAUDE.md` (see below).
  ```bash
  "/mnt/c/Program Files/Epic Games/UE_5.5/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" \
    PlanetaryCreationEditor Win64 Development \
    -project="C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject" \
    -WaitMutex -FromMsBuild
  ```
- When invoking that command from the Codex CLI, set `with_escalated_permissions=true` on the shell call; otherwise WSL denies it with `UtilBindVsockAnyPort`. Expect build logs under `%LOCALAPPDATA%\UnrealBuildTool\Log.txt`.
- Confirm the build **succeeds** before proceeding. If the command fails in the sandbox, rerun it immediately and include the failure reason in your summary.

## Commit & Pull Request Guidelines
- Write concise, imperative commit subjects ≤72 chars (e.g., `Add plate hotspot sampling`); include rationale/test notes in the body when helpful.
- PRs should link Jira/Trello tasks, outline tectonic simulation impact, list validation steps (editor launch, automation, perf captures), and attach before/after renders for visual changes.

## Planning & Collaboration
- Skim `README.md` and `Docs/MilestoneSummary.md` at the start of any working session for current scope and status cues.
- Review `Docs/PlanningAgentPlan.md` when sequencing work to stay aligned with milestone checkpoints.
- Keep `RealtimeMeshComponent_HowTo.md` open while extending runtime mesh features to maintain API consistency.
- GPU preview investigations: consult `Docs/gpu_preview_implementation_notes.md`, `Docs/gpu_preview_optimizations.md`, and `Docs/gpu_system_review.md` before diving into shader/controller changes.
- Terrane mesh surgery spike: run `planetary.TerraneMeshSurgery` in the editor console (Development builds only) to generate logs with Euler/non-manifold metrics while the extraction work is still in progress.

## Automation Troubleshooting
- **Automation tests hang or DLL lock errors**
  - Symptom: build fails with `... cannot access the file` after a test run; root cause is a stale `UnrealEditor-Cmd.exe` still running.
  - Diagnose with `cmd.exe /c "tasklist | findstr /i Unreal"`; terminate stuck processes via `cmd.exe /c "taskkill /F /PID <PID>"` before rebuilding.
  - Prevention: after long-running automation, confirm no `UnrealEditor-Cmd.exe` remains in the process list.
- **Reading automation test logs**
  - Logs live under `Saved/Logs/PlanetaryCreation.log`; console output may omit detailed failures.
  - Quickly locate latest log: `find "<ProjectPath>/Saved/Logs" -name "*.log" -type f -printf '%T@ %p\n' | sort -n | tail -1` (WSL).
  - Useful patterns: `grep -E "Test Completed|Result=|Error:" PlanetaryCreation.log`; scope to a single test with `grep -A 30 "TestName" PlanetaryCreation.log`.
  - Check overall result via `grep "EXIT CODE:" PlanetaryCreation.log` (0 = success, -1 = failures).
