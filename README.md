# PlanetaryCreation

PlanetaryCreation is an Unreal Engine 5.5 editor tool that recreates the tectonic-plate simulation from *Procedural Tectonic Planets*. It couples a deterministic plate solver with real-time visualization, automation, and export tooling so researchers can iterate on terrain pipelines without leaving the editor.

> **Active focus (Milestone 6 – Stage B Amplification & Terranes):** Stage B GPU preview is functional, unified CPU/GPU amplification is nearing parity, and terrane metrics are under validation. The remaining scope tracks in `Docs/Milestones/Milestone6_Plan.md`.

---

## What You Get
- Deterministic tectonic simulation with rollback support and CSV export hooks.
- Realtime mesh streaming (LODs 0–6) via `RealtimeMeshComponent` plus async rebuild cache.
- Rich visualization suite: elevation/thermal overlays, stress fields, high-res boundary tracing, velocity vectors, Stage B GPU preview.
- Stage B pipeline: oceanic & continental amplification, exemplar sampling, hydraulic tuning hooks, profiling telemetry.
- Automation harness: headless tests, parity captures, heightmap exporter metrics, scripted exemplar processing.
- Research-friendly UX: playback timeline, parameter presets, plate surgery tools, documented pipelines.

---

## Environment Checklist
- **Unreal Engine 5.5.4** installed to `C:\Program Files\Epic Games\UE_5.5`.
- **Visual Studio 2022** with Desktop C++ workload.
- **Windows 11 + WSL2 (Ubuntu)** for cross-platform build/test commands.
- **Shader Model 5.0 GPU** (falls back to CPU when unavailable).
- `RealtimeMeshComponent` plugin (already included under `Plugins/`).

---

## Quick Start
1. Clone and enter the repo:
   ```powershell
   git clone https://github.com/<org>/PlanetaryCreation.git
   cd PlanetaryCreation
   ```
2. Review onboarding handbooks: `Handbooks/AGENTS.md`, `Handbooks/CLAUDE.md`, `Handbooks/QUICKSTART.md`.
3. Skim planning context: `Docs/Plans/PlanningAgentPlan.md`, `Docs/Milestones/MilestoneSummary.md`.
4. Ensure the Milestone 6 exemplar data (StageB_SRTM90) is downloaded if you plan to touch Stage B tooling.

---

## Building
- **Preferred (WSL)**  
  ```bash
  "/mnt/c/Program Files/Epic Games/UE_5.5/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" \
    PlanetaryCreationEditor Win64 Development \
    -project="C:\\Users\\Michael\\Documents\\Unreal Projects\\PlanetaryCreation\\PlanetaryCreation.uproject" \
    -WaitMutex -FromMsBuild
  ```
- **Windows shell**  
  ```powershell
  "C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat" `
    PlanetaryCreationEditor Win64 Development `
    -project="%CD%\PlanetaryCreation.uproject"
  ```
- **Regenerate project files**  
  ```powershell
  "C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" -projectfiles
  ```
- If a build fails after automation, kill stale `UnrealEditor-Cmd.exe` (see `Handbooks/QUICKSTART.md#build-recovery`).

---

## Running & Development Loops
- Launch editor:
  ```powershell
  "C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor.exe" `
    "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject"
  ```
- Switch between paper defaults and lean baseline:  
  `r.PlanetaryCreation.PaperDefaults 1` (full Stage B) / `r.PlanetaryCreation.PaperDefaults 0` (Milestone 5 baseline).
- Stage B throttling during GPU automation: `-SetCVar=r.PlanetaryCreation.StageBThrottleMs=50`.
- Heightmap exports use `Scripts/ExportHeightmap1024.py` (safe) and `Scripts/ExportHeightmap4096Force.py` (unsafe; follow crash guidance in `Docs/Heightmap/heightmap_export_review.md`).

---

## Automation & Testing
- **Milestone 3 regression:**  
  `powershell.exe -ExecutionPolicy Bypass -File ".\Scripts\RunMilestone3Tests.ps1" -ArchiveLogs`
- **General automation suite:**  
  ```powershell
  powershell.exe -Command "& 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
    'C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject' `
    -ExecCmds='Automation RunTests PlanetaryCreation' `
    -TestExit='Automation Test Queue Empty' -unattended -nop4 -nosplash -log"
  ```
- **GPU parity suites:**  
  Require real RHI, throttle ≥25 ms. Suites live under `PlanetaryCreation.Milestone6.GPU.*`.
- **Log inspection:**  
  `powershell -Command "Get-Content 'Saved/Logs/PlanetaryCreation.log' | Select-String 'Result={Success}'"`
- Automation outputs (metrics, parity figures) now write under `Docs/Automation/Validation/`.

---

## Project Layout (Top Level)
- `Source/PlanetaryCreation` – runtime module scaffolding plus shared automation.
- `Source/PlanetaryCreationEditor` – editor module organized by concern (`Private/StageB`, `Private/Simulation`, `Private/Export`, `Private/UI`, `Private/Visualization`, etc.).
- `Plugins/RealtimeMeshComponent` – upstream plugin dependency.
- `Content` – editor assets (materials, tool UI content, exemplar placeholders).
- `Scripts` – build, automation, exemplar-processing scripts (Python/PowerShell).
- `StageB_SRTM90` – exemplar source tiles (`raw/`, `cropped/`, `metadata/`).
- `Handbooks` – contributor guides, quickstart, implementation summary.
- `Docs` – topic-based documentation hub (see below).

---

## Documentation Map
- **Handbooks:** `Handbooks/` (roles, quickstart, style guidance).
- **Plans & Milestones:** `Docs/Plans/`, `Docs/Milestones/` (roadmaps, retros, release notes).
- **Stage B & Heightmaps:** `Docs/Heightmap/` (export overhaul, parity plan, exemplar guides).
- **GPU Preview & Compute:** `Docs/GPU/` (implementation notes, parity investigations, optimization logs).
- **Automation:** `Docs/Automation/` (test quickstart, validation outputs, parity captures).
- **Performance:** `Docs/Performance/` (milestone metrics, profiling notes).
- **Reviews & Research:** `Docs/Reviews/` and `Docs/Research/` for audit findings and paper alignment.
- **Reference:** `Docs/Reference/` (parameter cheat sheets, licenses).

---

## Troubleshooting Snapshot
- **Blank GPU preview:** confirm Stage B ready latch (`LogPlanetaryCreation`, `[StageB][Profile]`) and see `Docs/GPU/gpu_preview_plate_colors_fix.md`.
- **Automation hang:** verify no lingering `UnrealEditor-Cmd.exe`; rerun with `-TestExit="Automation Test Queue Empty"`.
- **Heightmap export crash:** never exceed 512×256 without explicit approval; documented incidents in `Docs/Heightmap/heightmap_overhaul_relaunch.md`.
- **GPU automation TDRs:** use `-AllowGPUAutomation` only on certified hardware and throttle ≥25 ms (50 ms for full parity suites).

---

## Ownership & Support
- Simulation lead: Michael (tectonic modeling & validation).
- Tooling/rendering owners per milestone: see `Docs/Milestones/MilestoneSummary.md`.
- Planning cadence and workstream checkpoints: `Docs/Plans/PlanningAgentPlan.md`.
- Pair reviews, automation expectations, and build etiquette: `Handbooks/AGENTS.md`.

When proposing changes, include validation commands (build + relevant automation), Stage B profiling snippets, and doc updates for any new workflows.
