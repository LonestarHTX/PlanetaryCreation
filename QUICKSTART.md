# PlanetaryCreation Quick Start Guide

**Last Updated:** October 2025
**For:** New developers, onboarding, project handoff

---

## What Is This Project?

An Unreal Engine 5.5 **editor tool** (not a game) that implements the "Procedural Tectonic Planets" paper. Simulates realistic plate tectonics with continental drift, mountain formation, oceanic trenches, and terrain amplification—all running in the editor without PIE.

**Key Features:**
- ✅ Deterministic tectonic simulation (double-precision, seed-stable)
- ✅ Real-time visualization with RealtimeMeshComponent
- ✅ Stage B terrain amplification (~100m detail via GPU compute shaders)
- ✅ Terrane extraction/reattachment mechanics
- ✅ Undo/redo, timeline scrubbing, continuous playback
- ✅ CSV export for analysis

---

## Quick Build & Run

### Prerequisites
- Windows 10/11 (WSL2 for bash commands)
- Unreal Engine 5.5
- Visual Studio 2022
- Git

### Build from Command Line (WSL)
```bash
# Rebuild editor module
"/mnt/c/Program Files/Epic Games/UE_5.5/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" \
  PlanetaryCreationEditor Win64 Development \
  -project="C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject" \
  -WaitMutex -FromMsBuild
```

### Launch Editor
```bash
"C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor.exe" \
  "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject"
```

### Run Automation Tests
```bash
# All tests
"C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" \
  "<ProjectPath>\PlanetaryCreation.uproject" \
  -ExecCmds="Automation RunTests PlanetaryCreation; Quit" \
  -unattended -nop4 -nosplash

# GPU parity tests (must run from native Windows shell, not WSL)
# PowerShell:
& 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject' `
  -ExecCmds='Automation RunTests PlanetaryCreation.Milestone6.GPU.OceanicParity' `
  -TestExit='Automation Test Queue Empty' -unattended -nop4 -nosplash
```

---

## Architecture Overview

### Core Components

1. **`UTectonicSimulationService`** (Editor Subsystem)
   - Canonical simulation state (double-precision)
   - Advances in 2 My (million year) steps per paper spec
   - Manages Stage B amplification, terrane lifecycle, surface processes
   - Location: `Source/PlanetaryCreationEditor/Public/Simulation/TectonicSimulationService.h`

2. **`FTectonicSimulationController`** (Non-UObject Bridge)
   - Converts double → float for rendering
   - Spawns/updates RealtimeMeshActor in editor world
   - Manages GPU preview pipeline
   - Location: `Source/PlanetaryCreationEditor/Public/Simulation/TectonicSimulationController.h`

3. **`SPTectonicToolPanel`** (Slate UI)
   - Editor toolbar panel with Step button, visualization controls
   - Terrane export, undo/redo, playback controls
   - Location: `Source/PlanetaryCreationEditor/Private/UI/SPTectonicToolPanel.cpp`

### Data Flow
```
User clicks "Step"
  → Service advances simulation (double-precision math)
  → Controller converts to float, updates mesh
  → RealtimeMeshComponent renders in viewport
  (No PIE required!)
```

---

## Current Milestone Status (M6)

**Focus:** Stage B amplification, terrane mechanics, performance optimization

### ✅ Completed
- Terrane extraction/transport/reattachment with mesh surgery
- Oceanic amplification (GPU compute: Perlin noise, transform faults)
- Continental amplification (GPU compute: exemplar blending)
- Multi-threading via `ParallelFor` (sediment, dampening, mesh build)
- GPU preview pipeline (equirect texture for WPO material)
- Visualization mode unification (PlateColors, Elevation, Velocity, Stress)
- Boundary overlay simplification (clean single-line seams)
- 57 automation tests covering M1-M6 features

### 🟡 In Progress
- Hydraulic erosion on amplified mesh
- Level 7/8 profiling for paper Table 2 parity
- LOD integration for amplification

### 🔴 Planned (M7+)
- Advanced materials (PBR, biome-specific)
- Climate coupling (temperature/precipitation zones)
- Final performance polish (target <60ms L3, <90ms L6)

---

## Performance Snapshot (October 2025)

| LOD | Vertices | Step Time | Budget | Status |
|-----|----------|-----------|--------|--------|
| L3  | 642      | 6.32 ms   | 90 ms  | ✅ 17× under budget |
| L5  | 10,242   | ~15 ms    | 120 ms | ✅ 8× under budget |
| L6  | 40,962   | ~35 ms    | 120 ms | ✅ 3× under budget |
| L7  | 163,842  | TBD       | 200 ms | 📊 Baseline pending |

**M6 Features (Paper defaults, L7):**
- Terrane mechanics: ~2 ms (amortized)
- Stage B warm-up: ~65 ms on the first replay while the GPU snapshot seeds
- Stage B steady-state: **~33–34 ms** (Oceanic GPU ≈8 ms + Continental GPU ≈23 ms + ≈3 ms CPU)
- Parity undo: ~44 ms (expected CPU/cache fallback before the suite exits)
- Hydraulic erosion: ~1.7 ms (topological queue)
- **Current steady-state total:** ≈43 ms at L7 (≤90 ms budget) with 52 % headroom; L3 remains 6.32 ms (17× under its 90 ms budget)

---

## Key Performance Optimizations

### ✅ Multi-Threading (ParallelFor)
**What:** Unreal's built-in `ParallelFor` across hot paths (sediment, dampening, mesh)
**Why:** Better than SIMD (4-16 cores vs 2-8 lanes), platform-agnostic, maintainable
**Result:** 6.32ms baseline (17× under 110ms target)

**Locations:**
- `SedimentTransport.cpp:82` - Diffusion passes
- `OceanicDampening.cpp:70` - Vertex smoothing
- `TectonicSimulationController.cpp:1256, 2004` - Mesh processing

### ✅ GPU Compute Shaders (Stage B)
**What:** Offload Stage B amplification to GPU compute shaders
**Why:** Stage B dominates the M6 budget (~31 ms combined), high ROI
**Result:** Warm-up ~65 ms (one-time), then steady-state Stage B ≈33–34 ms per step (Oceanic GPU ≈8 ms, Continental GPU ≈23 ms, Continental CPU ≈3 ms) with <0.1 m parity and a final CPU/cache replay (~44 ms) when the parity harness undoes

**Shaders:**
- `OceanicAmplification.usf` - Perlin noise, transform faults
- `ContinentalAmplification.usf` - Exemplar blending
- `OceanicAmplificationPreview.usf` - Equirect preview texture

### ⏸️ NOT Implemented: SIMD Vectorization
**Why deferred:** ParallelFor achieved targets without intrinsics complexity
**Note:** `Docs/simd_gpu_implementation_review.md` documents exploration, not implementation

### ⏸️ NOT Implemented: GPU Thermal/Velocity Fields
**Why deferred:** Only 0.6ms CPU cost, not worth GPU transfer overhead
**Note:** Stage B was higher priority (~31 ms → GPU), whereas thermal/velocity together cost only 0.6 ms on CPU

---

## Important Console Commands

```cpp
// Stage B profiling
r.PlanetaryCreation.StageBProfiling 1

// GPU amplification toggle
r.PlanetaryCreation.UseGPUAmplification 1

// Visualization modes (0=PlateColors, 1=Elevation, 2=Velocity, 3=Stress)
r.PlanetaryCreation.VisualizationMode 0

// GPU preview mode is toggled from the Tectonic Tool panel checkbox (no console command)
```

**In-Editor:**
```
# List tests
Automation List

# Run all PlanetaryCreation tests
Automation RunTests PlanetaryCreation

# Run specific milestone
Automation RunTests PlanetaryCreation.Milestone6
```

---

## Common Gotchas

### 1. "Stale UnrealEditor-Cmd.exe Process"
**Symptom:** Build fails with "cannot access DLL" after automation run
**Fix:**
```bash
# Check for stale processes (WSL)
cmd.exe /c "tasklist | findstr /i Unreal"

# Kill stale process
cmd.exe /c "taskkill /F /PID <PID>"
```

### 2. "GPU Parity Tests Fail from WSL"
**Symptom:** `UtilBindVsockAnyPort` error when running GPU tests from WSL
**Fix:** Run GPU tests from native Windows shell (PowerShell/cmd), not WSL

### 3. "Navigation System Warnings Spam Logs"
**Status:** Expected - ensure handler installed at module startup downgrades to single warning
**Check:** Should only see one navigation warning per commandlet run

### 4. "Mesh Appears Inside-Out"
**Cause:** Incorrect triangle winding order
**Fix:** Ensure counter-clockwise winding when viewed from outside sphere

---

## Key Documentation

### Getting Started
- `CLAUDE.md` - Primary reference for Claude Code
- `README.md` - Project overview
- `Docs/PlanningAgentPlan.md` - Milestone roadmap

### Current Milestone
- `Docs/Milestone6_Plan.md` - Task breakdown, acceptance criteria
- `Docs/Performance_M6_Implementation_Notes.md` - **What was actually implemented vs planned**
- `Docs/MilestoneSummary.md` - High-level progress tracker

### Performance & Testing
- `Docs/Performance_M5.md` - 6.32ms baseline measurements
- `Docs/GPU_Test_Suite.md` - GPU parity validation
- `Source/PlanetaryCreationEditor/Private/Tests/` - 57 automation tests

### Paper Alignment
- `ProceduralTectonicPlanetsPaper/PTP_Text.md` - Clean paper transcription
- `ProceduralTectonicPlanetsPaper/PTP_ImplementationAlignment.md` - Implementation status

---

## FAQ for New Developers

**Q: Why editor-only, not PIE?**
A: Simulation is a research/content-creation tool, not a game. Editor workflow is faster for iteration.

**Q: Why double-precision in service?**
A: Accumulating millions of years of plate motion; float drift would compound errors.

**Q: What's the difference between SharedVertices and RenderVertices?**
A: SharedVertices = low-density simulation mesh; RenderVertices = high-density render mesh (subdivided)

**Q: Why were SIMD docs written if not implemented?**
A: Exploration phase; decided ParallelFor was simpler and more effective.

**Q: Can I run this on Mac/Linux?**
A: Not tested, but ParallelFor and GPU shaders are cross-platform. Build may need adjustments.

**Q: How do I add a new automation test?**
A: See pattern in `Tests/` folder; use `IMPLEMENT_SIMPLE_AUTOMATION_TEST` macro.

---

## Getting Help

- Check `CLAUDE.md` for troubleshooting patterns
- Review `Docs/Milestone6_Plan.md` for task context
- Search automation tests for usage examples
- Check git history for implementation notes

**Pro Tip:** When diving into code, start with:
1. `TectonicSimulationService.h` - Core simulation API
2. `TectonicSimulationController.cpp` - Rendering integration
3. Automation tests matching your area of interest

---

**Welcome to the project! 🌍**
