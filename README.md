# PlanetaryCreation

PlanetaryCreation is an Unreal Engine 5.5 editor tool that recreates the tectonic-plate driven world synthesis described in *Procedural Tectonic Planets* (Cordial et al.). It delivers a deterministic tectonic simulation, real-time mesh visualization via `RealtimeMeshComponent`, and researcher-friendly tooling for exploring plate dynamics step-by-step or in continuous playback.

> **Current status (Milestone 6 – Stage B Amplification & Terranes):** Oceanic GPU preview is live with parity automation, CPU/GPU amplification plumbing is in place, and Stage B detail + terrane mechanics are in active development. See [Roadmap](#roadmap--milestones) for per-milestone details.

---

## Feature Highlights
- **Deterministic tectonic simulation:** Double-precision plate state, repeatable split/merge, rollback history, CSV exports.
- **Realtime mesh pipeline:** Icosphere LODs (0–6) streamed through `RealtimeMeshComponent` with async rebuilds and caching.
- **Visualization suite:** Stress, elevation, thermal overlays; high-resolution boundary tracing; velocity vectors; GPU World Position Offset preview.
- **Production UX:** Continuous playback, orbital camera, timeline scrubber, undo/redo history, parameter presets, automation harness.
- **Surface weathering (Milestone 5):** Continental erosion, sediment diffusion stage 0, oceanic dampening integrated into the step loop.
- **GPU acceleration groundwork:** Oceanic Stage B preview compute shader, parity diagnostics, async playlist for future continental + normals passes.

---

## Repository Layout
| Path | Purpose |
| --- | --- |
| `Source/PlanetaryCreation` | Runtime simulation core (`UTectonicSimulationService`, automation tests). |
| `Source/PlanetaryCreationEditor` | Editor module, organized into `Private/Simulation`, `Private/StageB`, `Private/Export`, `Private/UI`, plus shared utilities and Slate tooling. |
| `Plugins/RealtimeMeshComponent` | Third-party mesh streaming plugin (API reference in `RealtimeMeshComponent_HowTo.md`). |
| `Content/` | Editor assets (materials, tool UI assets, exemplar placeholders). |
| `Docs/` | Milestone plans, performance reports, GPU preview notes, roadmap (`PlanningAgentPlan.md`). |
| `ProceduralTectonicPlanetsPaper/` | Paper transcription (`PTP_Text.md`) and implementation alignment log. |
| `Scripts/` | PowerShell automation wrappers (e.g., `RunMilestone3Tests.ps1`). |

> **Directory refactor (2025-10-15):** Stage B amplification, sampling/export helpers, and simulation controllers now live in their own subfolders under `Source/PlanetaryCreationEditor`. Public headers mirror this layout (`Public/StageB`, `Public/Simulation`, etc.) to make ownership clearer when onboarding new contributors.

---

## Prerequisites
- **Unreal Engine 5.5.4** (Windows install at `C:\Program Files\Epic Games\UE_5.5`).
- **Visual Studio 2022** with C++ workload (Windows builds).
- **Windows 11 with WSL2 (Ubuntu)** for the provided cross-platform build commands.
- **RealtimeMeshComponent** plugin (already included under `Plugins/`).
- GPU with Shader Model 5.0 support for Stage B preview (falls back to CPU if unavailable).

---

## Initial Setup
1. Clone the repository:
   ```powershell
   git clone https://github.com/<org>/PlanetaryCreation.git
   cd PlanetaryCreation
   ```
2. (Optional) Initialize submodules if additional plugins are added later:
   ```powershell
   git submodule update --init --recursive
   ```
3. Open `AGENTS.md` and `CLAUDE.md` for role expectations and build etiquette.
4. Review `Docs/PlanningAgentPlan.md` and `Docs/MilestoneSummary.md` to understand scope & current milestones.

---

## Build Instructions
### WSL-friendly (recommended for C++/Slate changes)
```bash
"/mnt/c/Program Files/Epic Games/UE_5.5/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" \
  PlanetaryCreationEditor Win64 Development \
  -project="C:\\Users\\Michael\\Documents\\Unreal Projects\\PlanetaryCreation\\PlanetaryCreation.uproject" \
  -WaitMutex -FromMsBuild
```

### Windows (VS Developer Command Prompt or PowerShell)
```powershell
"C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat" \
  PlanetaryCreationEditor Win64 Development \
  -project="%CD%\PlanetaryCreation.uproject"
```

### Regenerate Project Files after module changes
```powershell
"C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" -projectfiles
```

> **Tip:** If builds fail with DLL lock errors after automation runs, terminate stale commandlets: `cmd.exe /c "tasklist | findstr /i Unreal"` then `taskkill /F /PID <PID>` before rebuilding.

---

## Running the Editor Tool
```powershell
"C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor.exe" \
  "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject"
```
- The **Planetary Creation** toolbar exposes playback, stepping, and GPU preview toggles.
- GPU Preview Mode (Milestone 6) applies Stage B displacement through a WPO material while keeping CPU topology intact.

---

## Automation & Testing
| Purpose | Command |
| --- | --- |
| Milestone 3 regression + quantitative metrics (CSV in `Saved/Metrics`) | `powershell -ExecutionPolicy Bypass -File .\Scripts\RunMilestone3Tests.ps1 -ArchiveLogs` |
| Targeted automation (headless) | `"C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" PlanetaryCreation.uproject -ExecCmds="Automation RunTests PlanetaryCreation" -TestExit="Automation Test Queue Empty" -unattended -nop4 -nosplash` |
| GPU parity smoke | `Automation RunTests PlanetaryCreation.Milestone6.GPU` (from in-editor console or commandlet) |

Logs live under `Saved/Logs/PlanetaryCreation.log`. Tail/search patterns from WSL:
```bash
find "Saved/Logs" -name "*.log" -type f -printf '%T@ %p\n' | sort -n | tail -1
```
```bash
grep -E "Test Completed|Result=|Error:" Saved/Logs/PlanetaryCreation.log
```

Automation suites auto-skip GPU-reliant tests when `GDynamicRHI` reports `NullDrv`.

---

## Documentation Index
- **Roadmap & milestones:** `Docs/MilestoneSummary.md`, `Docs/PlanningAgentPlan.md`
- **Performance baselines:** `Docs/Performance_M4.md`, `Docs/Performance_M5.md`
- **GPU preview stack:** `Docs/gpu_preview_implementation_notes.md`, `Docs/gpu_preview_integration_complete.md`, `Docs/gpu_preview_optimizations.md`, `Docs/gpu_system_review.md`
- **Simulation/paper alignment:** `ProceduralTectonicPlanetsPaper/PTP_Text.md` (transcribed paper) and `PTP_ImplementationAlignment.md`
- **Testing:** `Docs/GPU_Test_Suite.md`, plus automation sources under `Source/PlanetaryCreation/Private/Tests`
- **How-to references:** `RealtimeMeshComponent_HowTo.md`, `Docs/ParameterQuickReference.md`

---

## Roadmap & Milestones
| Milestone | Status | Highlights |
| --- | --- | --- |
| 0 – Pre-Production | ✅ Complete | Paper review, glossary, feasibility analysis. |
| 1 – Tooling & Infrastructure | ✅ Complete | Editor module scaffold, toolbar, automation seed. |
| 2 – Simulation Core | ✅ Complete | Deterministic plate simulation, CSV exports. |
| 3 – Geometry Integration | ✅ Complete | Icosphere LODs, stress/elevation overlays, async mesh pipeline. |
| 4 – Dynamic Tectonics & Visual Fidelity | ✅ Complete (2025-10-04) | Re-tess engine, hotspot/rift lifecycle, LOD caching, 17/18 tests green. |
| 5 – Production Readiness & Weathering | ✅ Complete (2025-10-04) | Continuous playback, orbital camera, erosion/sediment, 6.32 ms L3 step. |
| 6 – Stage B Amplification & Terranes | 🚧 In Progress | GPU preview pipeline, terrane extraction & Stage B detail, SIMD/GPU optimisation, Level 7 parity. |
| 7 – Presentation & Material Polish | 📝 Planned | Biome-aware materials, cinematic capture, advanced camera flows. |
| 8 – Climate & Hydrosphere Coupling | 📝 Planned | Sea-level response, climate overlays, hydrology.
| 9 – Shipping & Cinematic Polish | 📝 Planned | Final optimisation, console-class budgets, release packaging. |

Milestone retrospectives and detailed plans live in `Docs/Milestone*_Plan.md` / `Docs/Milestone*_CompletionSummary.md`.

---

## Troubleshooting
- **Plates appear frozen in GPU preview:** See `Docs/gpu_preview_plate_colors_fix.md`; verify vertex colors aren’t forced to elevation mode.
- **Automation hangs on GPU readback:** Apply the fence strategy in `Docs/gpu_readback_fix_plan.md` and ensure commandlets tick `ProcessPendingOceanicGPUReadbacks(true)`.
- **Perf spikes after enabling GPU preview:** Confirm `bSkipCPUAmplification` is true (Milestone 6 optimisation) and check `Docs/gpu_system_review.md` for sediment/dampening hotspots.
- **Stale UnrealEditor-Cmd.exe locks:** Kill with `taskkill /F` before relaunching builds.

---

## Contributing & Workflow
- Follow role guidance in `AGENTS.md`, coding standards in `CLAUDE.md`, and Unreal C++ style (4 spaces, prefix classes with A/U/F/I, `.generated.h` last in headers).
- New runtime mesh experiments belong under `Plugins/RealtimeMeshComponent`; wrap experimental GPU paths in `#if UE_BUILD_DEVELOPMENT` until production-ready.
- Place automation tests in `Source/PlanetaryCreation/Private/Tests`.
- Update relevant docs (milestone plan/summary, performance logs) alongside code changes.
- Use deterministic seeds and capture performance metrics when touching simulation or GPU code paths.

---

## Credits & Ownership
- **Simulation Lead:** Michael (tectonic modeling, validation)
- **Tooling & Rendering Engineers:** See `Docs/MilestoneSummary.md` for per-milestone ownership.
- **Automated documentation:** Maintained alongside milestone completion artifacts.

For project coordination, reference `Docs/PlanningAgentPlan.md` and associated milestone documents. Issues, PRs, and feature ideas should link the relevant milestone scope and include validation steps (editor launch, automation suites, perf captures).
