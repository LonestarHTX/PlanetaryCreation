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
- Run Milestone 3 automation suite (captures `PlanetaryCreation.QuantitativeMetrics.Export` and refreshes `Saved/Metrics/heightmap_export_metrics.csv` + timestamped copy under `Docs/Validation`): `powershell -ExecutionPolicy Bypass -File .\Scripts\RunMilestone3Tests.ps1 [-ArchiveLogs]`.
- **CRITICAL AUTOMATION FIX:** CVars must use `-SetCVar=var1=val1,var2=val2` NOT `-ExecCmds`. Setting CVars in `-ExecCmds` causes them to execute AFTER test queuing, making tests run with wrong/default settings. This was the root cause of the "tests never run" regression.
- CPU-only automation (Milestone 3/5 regressions, etc.) should use `-SetCVar=r.PlanetaryCreation.PaperDefaults=0` so the run matches the lean M5 configuration.
- GPU preview parity suites (Milestone 6): launch from Windows PowerShell via `Automation RunTests PlanetaryCreation.Milestone6.GPU`.
- GPU automation guardrails (root cause: unthrottled Stage B parity runs triggered Windows TDR resets on developer GPUs):
  - GPU suites now auto-skip unless `-AllowGPUAutomation` or `r.PlanetaryCreation.AllowGPUAutomation=1` is present; leave them disabled locally unless you’re on certified hardware.
  - Use `Scripts/RunStageBHeartbeat.ps1` for Stage B heartbeat checks—it forces `UseGPUAmplification 0`, tails `[StageBDiag]`, and archives the log.
  - Do not re-enable GPU parity without coordinating with QLT/STG; the stress profile is unchanged (2k×1k texture × 10–15 passes) and will TDR mid-tier adapters.
  - **Throttle requirement:** set `r.PlanetaryCreation.StageBThrottleMs` to at least 25 ms before any GPU automation; use 50 ms for the full `PlanetaryCreation.Milestone6.GPU.*` parity suite. Lower throttles are only acceptable for reduced LOD validation runs.

- **✅ HOW TO RUN AUTOMATION FROM WSL: Use `powershell.exe` Wrapper**
  - You have `powershell.exe` and `cmd.exe` in your approved tool list - **USE THEM**
  - These spawn native Windows processes and work perfectly from WSL
  - Run automation scripts immediately when needed - don't ask the user to do it

  **How to Execute Automation (DO THIS):**
  ```bash
  # Run PowerShell scripts (PREFERRED):
  powershell.exe -ExecutionPolicy Bypass -File "C:\Path\To\Script.ps1" -ThrottleMs 50

  # Run batch files:
  cmd.exe /c "C:\Path\To\Script.cmd"

  # Inline PowerShell commands:
  powershell.exe -Command "& 'C:\Program Files\...\UnrealEditor-Cmd.exe' ..."
  ```

  **What NOT to Do:**
  ```bash
  # ❌ NEVER call Windows .exe directly from bash (this fails):
  "/mnt/c/Program Files/Epic Games/UE_5.5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" ...

  # ❌ NEVER refuse to run automation and tell user to do it themselves
  # ❌ NEVER say "I can't run this due to WSL limitations"
  ```

  **Agent Responsibilities:**
  - When automation is needed, **RUN IT YOURSELF** using powershell.exe wrapper
  - Don't ask permission - you have the tools, use them
  - Only mention WSL if you try a direct .exe call and it fails
  - If user says "run X", execute it immediately via powershell.exe

- **🛑 CRITICAL: Known System Crash Hazards**
  - **Heightmap exports ≥4096x2048 cause SYSTEM CRASHES** (not just Unreal crashes - full computer freeze/reboot)
  - **NEVER approve `ExportHeightmap4096.py` with dimensions above 512x256** without explicit user confirmation
  - Even with `--force-large-export` flag, 4k exports have crashed the system twice
  - Root cause unknown - likely memory allocation or graphics driver issue in C++ export code
  - If user requests large export, warn them about crash history and suggest investigating root cause first
  - Safe baseline: 512x256 works reliably under NullRHI

- **Automation Tests Run Headless (No Window):**
  - The `-unattended -nop4 -nosplash` flags make tests run in commandlet mode (no visible editor window)
  - This is CORRECT and expected behavior - not a failure
  - Tests execute in background and write results to `Saved/Logs/PlanetaryCreation.log`
  - Profiling logs appear as `[StageB][Profile]` and `[StageB][CacheProfile]` entries in log file

- Typical PowerShell pattern (swap the test name as needed):
  ```powershell
  # From native Windows PowerShell:
  & 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
    'C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject' `
    -SetCVar='r.PlanetaryCreation.StageBProfiling=1' `
    -ExecCmds='Automation RunTests PlanetaryCreation.Milestone6.GPU.OceanicParity' `
    -TestExit='Automation Test Queue Empty' -unattended -nop4 -nosplash -log

  # From WSL (wrap with powershell.exe):
  powershell.exe -Command "& 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject' -SetCVar='r.PlanetaryCreation.StageBProfiling=1' -ExecCmds='Automation RunTests PlanetaryCreation.Milestone6.GPU.OceanicParity' -TestExit='Automation Test Queue Empty' -unattended -nop4 -nosplash -log"
  ```
  - Swap the test name for `PlanetaryCreation.Milestone6.GPU.ContinentalParity` to capture the complementary Stage B tables; both suites now emit `[StageB][Profile]`/`[StageB][CacheProfile]` as long as the command is launched this way.
  - If you previously ran `r.PlanetaryCreation.PaperDefaults 0`, remember to re-enable (`r.PlanetaryCreation.PaperDefaults 1`) before GPU suites so Stage B/GPU preview are active.
  - **Do not** append `Quit` inside `-ExecCmds`; the automation controller exits on its own when `-TestExit="Automation Test Queue Empty"` is present. Adding `Quit` or manually terminating the process prevents the queue from running, which was the root cause of earlier "nothing happens" sessions.
  - Let the command run to completion—the Stage B `[StageB][Profile]` / `[StageB][CacheProfile]` lines appear once the parity suite reaches the replay phase.

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

## Heightmap Export Overhaul – Agent Spin-Up Plan

### Executive Context
- Focus: align CPU heightmap exports with Stage B visuals, address seam issues, clarify palettes, and tighten CPU/GPU parity.
- Agents: deploy in parallel across five workstreams (Algorithm Rewrite, Stage B State Hygiene, Visualization Modes, Fidelity Verification, Tooling & Docs).
- Coordination: weekly sync via `Docs/PlanningAgentPlan.md`; each agent maintains a running log under `Docs/agent_logs/<agent>.md`.

### Objectives & Streams
- **Algorithm Rewrite (ALG-Agent)**: replace vertex splat + dilation with per-pixel triangle sampling; guarantee seam/pole fidelity.
- **Stage B Hygiene (STG-Agent)**: manage amplification readiness, ridge/fold field generation, anisotropic kernels, telemetry, and pipeline ordering.
- **Visualization Modes (VIS-Agent)**: deliver palette refactor, configuration UX, and documentation.
- **Fidelity Verification (QLT-Agent)**: expand automation, CPU↔GPU comparison, orientation metrics, and performance benchmarking.
- **Tooling & Docs (OPS-Agent)**: update scripts, design notes, checklists, and rollout communication.

### Staffing & Onboarding
- Each agent clones latest `main`, reviews `Docs/heightmap_debug_review.md`, this plan, and relevant code (`HeightmapExporter.cpp`, `TectonicSimulationService.cpp`, Stage B shaders).
- Agents register themselves in `Docs/agent_registry.md` with start date, owner, and slack handle.
- Kick-off checklist:
  1. Review acceptance criteria per task table below.
  2. Confirm build & test commands available on workstation.
  3. Create tracking Jira ticket referencing Task ID.
  4. Schedule pair-review slot with peer agent before first merge.

### Phase Roadmap
| Phase | Window | Owner Agents | Goals | Exit Criteria |
| --- | --- | --- | --- | --- |
| Phase 1 – Analysis | Day 0–1 | ALG, STG, VIS | Capture exporter baseline metrics and Stage B state behaviour | Metrics recorded in shared sheet, issues logged |
| Phase 2 – Implementation | Day 1–5 | ALG, STG, VIS | Ship sampler rewrite, Stage B latch, ridge/fold caches, anisotropic kernels, palette toggle | Feature branches include unit coverage and local perf capture |
| Phase 3 – Validation | Day 5–7 | QLT | Land automation, CPU/GPU parity harness, ridge transect & belt metrics, benchmark suite | Milestone 6 automation green; perf logs published |
| Phase 4 – Documentation | Day 7–8 | OPS | Update docs, scripts, release checklist | README + Docs PR merged; ops checklist circulated |

### Detailed Task Matrix
| Task ID | Scope Summary | Assigned Agent | Prerequisites | Deliverables | Target ETA |
| --- | --- | --- | --- | --- | --- |
| MNG-00 | Stage B heartbeat diagnostic & finite sample check | STG-Agent | None | `[StageBDiag]` logs showing Ready=1, finite/non-zero samples | Day 0 |
| ALG-01 | Baseline metrics for exporter coverage & seam gaps | ALG-Agent | None | Metrics log + before screenshots | Day 0 (Done 2025-10-09) |
| ALG-02 | `SampleElevationAtUV` using KD-tree face lookup | ALG-Agent | ALG-01 | Reusable sampler function + unit tests | Day 1 (Done 2025-10-09) |
| STG-00 | Isotropic Stage B amplification spike (pipeline proof) | STG-Agent | MNG-00 | Noise-blended amplification running end-to-end + exported PNG | Day 1 |
| ALG-03 | Replace vertex splat with sampler-driven ParallelFor loop | ALG-Agent | ALG-02 | Exporter uses sampler exclusively; vertex splat removed | Day 1 |
| ALG-04 | Seam/pole stabilization + UV wrapping | ALG-Agent | ALG-03 | Verified seam continuity, epsilon clamps | Day 2 |
| ALG-05 | Performance profiling and regression guardrails | ALG-Agent | ALG-03 | Insights capture + log reporting hook | Day 3 |
| ALG-06 | Centralize equirectangular helper & CPU/GPU parity stub | ALG-Agent | ALG-03 | Shared conversion utility + unit coverage | Day 4 |
| STG-01 | Stage B readiness latch (CPU/GPU parity) | STG-Agent | None | `bStageBAmplificationReady` + state machine | Day 1 (Done 2025-10-09) |
| STG-02 | Exporter gating on amplification readiness | STG-Agent | STG-01 | Warning paths + fallback to baseline | Day 1 |
| STG-03 | Stage B telemetry extensions | STG-Agent | STG-01 | `[StageB][Profile]` updates | Day 2 |
| STG-04 | Pipeline order enforcement + hydraulic erosion disable | STG-Agent | STG-00 | Stage B runs before erosion; erosion temporarily gated off | Day 2 (Updated) |
| STG-05 | Per-render-vertex ridge tangent cache with inline validation | STG-Agent | STG-04 | `RidgeTangent` array ≥99% populated, validation log attached | Day 3 |
| STG-06 | Fold direction & orogeny class generation + inline check | STG-Agent | STG-05 | `FoldDir`, `OrogenyClass` populated; log classification stats | Day 4 |
| STG-07 | Anisotropic amplification kernels (oceanic & continental) | STG-Agent | STG-06 | Updated Stage B kernels + validation export/PNG | Day 5 |
| STG-08 | Post-amplification light erosion validation (conservative) | STG-Agent | STG-07 | Tuned passes, metrics logged, no anisotropy loss | Day 5 |
| VIS-01 | Palette API split (absolute vs normalized) | VIS-Agent | ALG-03 | Refactored `HeightmapColorPalette` | Day 7 (Parallel) |
| VIS-02 | Exporter toggle + CLI exposure | VIS-Agent | VIS-01 | Config flag, script updates, editor toggle | Day 7 (Parallel) |
| VIS-03 | Documentation refresh (README, docs) | VIS-Agent | VIS-02 | Updated wording, screenshots, tooltips | Day 8 (Parallel) |
| QLT-01 | Seam delta automation test | QLT-Agent | ALG-04 | Extended `FHeightmapSeamContinuityTest` | Day 3 |
| QLT-02 | Pixel coverage verification tests | QLT-Agent | ALG-03 | New automation suite entries | Day 3 |
| QLT-03 | CPU/GPU parity comparison harness | QLT-Agent | ALG-03 | Test executes GPU preview + compare | Day 5 |
| QLT-04 | Export performance benchmark | QLT-Agent | ALG-05 | Automated perf capture + threshold | Day 5 |
| QLT-05 | Mid-ocean ridge transect metric | QLT-Agent | STG-07 | Automation asserting band spacing & decay | Day 6 |
| QLT-06 | Orogeny belt alignment metric | QLT-Agent | STG-06 | Test ensuring ridge chains align with fold dir | Day 6 |
| QLT-07 | Coordinate convention parity test | QLT-Agent | ALG-06 | CPU/GPU UV match automation (unit vectors) | Day 4 |
| OPS-01 | CLI script enhancement (`ExportHeightmap4096.py`) | OPS-Agent | VIS-02 | Arg-parsing, logging, docs | Day 4 |
| OPS-02 | Design note & checklist publication | OPS-Agent | All tasks | `Docs/heightmap_export_design.md`, release checklist | Day 5 |

### Coordination & Review
- Daily async updates in `#planetary-heightmap` channel using template: `Status | Blockers | Next`.
- Pair reviews mandated: ALG ↔ STG for shared code, VIS ↔ OPS for docs & UX, QLT ↔ ALG for parity thresholds.
- Merge gates:
  - ✅ All local tests run (document command + result).
  - ✅ Relevant automation suite executed or justified skip.
  - ✅ Stage B profile logs attached for changes touching amplification.

### Risk Management
- **Sampler Performance**: if ParallelFor + KD lookup breaches budget, escalate to ALG-Agent to prototype face-cache; notify QLT-Agent to adjust benchmarks.
- **Async GPU Readiness**: STG-Agent verifies latch integrates with GPU callbacks; if async failure occurs, block exporter and log guidance.
- **Palette UX Drift**: VIS-Agent coordinates with art/design leads before flipping defaults.
- **Orientation Field Gaps**: STG-Agent treats missing `RidgeTangent`/`FoldDir` data as blockers; asserts remain enabled in development builds.

### Exit Criteria
- CPU exports visually align with Stage B previews (document with before/after PNGs).
- Seam delta tests < configured threshold; pixel coverage 100% without dilation.
- Ridge transect and belt alignment metrics pass with thresholds defined in QLT-05/06.
- Coordinate convention parity automation passes; helper used across CPU/GPU code paths.
- Stage B readiness logging present in `[StageB][Profile]`; exporter blocks unsafe amplification access.
- Documentation, scripts, and release checklist updated and reviewed by OPS-Agent.
- Post-mortem recorded in `Docs/postmortems/heightmap_export_overhaul.md` outlining lessons learned for future agent deployments.
