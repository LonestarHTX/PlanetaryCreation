# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PlanetaryCreation is an Unreal Engine 5.5 editor tool implementing the "Procedural Tectonic Planets" paper. The tool runs **entirely in the editor** (no PIE required) and uses `RealtimeMeshComponent` for high-performance visualization of tectonic plate simulations.

**Key Modules:**
- `PlanetaryCreation` (Runtime): Minimal runtime module placeholder
- `PlanetaryCreationEditor` (Editor): Contains all tectonic simulation logic, UI, and mesh generation
- `RealtimeMeshComponent` (Plugin): Third-party real-time mesh rendering system

## Build Commands

**Rebuild from command line (WSL/Linux):**
```bash
# Use UnrealBuildTool directly (works from WSL bash)
"/mnt/c/Program Files/Epic Games/UE_5.5/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" \
  PlanetaryCreationEditor Win64 Development \
  -project="C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject" \
  -WaitMutex -FromMsBuild
```

**Rebuild from command line (Windows cmd.exe):**
```bash
"C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat" PlanetaryCreationEditor Win64 Development -project="<ProjectPath>\PlanetaryCreation.uproject"
```

**Regenerate Visual Studio project files:**
```bash
"C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" -projectfiles -project="<ProjectPath>\PlanetaryCreation.uproject"
```

**Launch editor:**
```bash
"C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor.exe" "<ProjectPath>\PlanetaryCreation.uproject"
```
- The editor now boots with the paper-authentic stack (LOD 5, Stage B amplification, GPU preview, and PBR). To revert to the lean M5 baseline for profiling or legacy tests, run `r.PlanetaryCreation.PaperDefaults 0` in the console (or prepend it to automation commands).
- Hydraulic routing/erosion ships enabled alongside Stage B and costs ~1.7 ms at L7. Leave it on for parity/profiling; use `r.PlanetaryCreation.EnableHydraulicErosion 0/1` (or the Surface Processes checkbox) only when you need the lean baseline. The optional `r.PlanetaryCreation.UseGPUHydraulic` toggle defaults to `1`, but the pass currently runs entirely on the optimised CPU path.

## Running Tests

**In-Editor (Recommended):**
1. Press **`** (backtick) to open console
2. Type: `Automation List` to see available tests
3. Type: `Automation RunTests PlanetaryCreation` to run project tests
4. Or go to **Tools** → **Test Automation** for GUI

**Command-line automation:**
```bash
"C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<ProjectPath>\PlanetaryCreation.uproject" -ExecCmds="r.PlanetaryCreation.PaperDefaults 0; Automation RunTests PlanetaryCreation; Quit" -unattended -nop4 -nosplash
```

**RHI selection for automation suites:**

- *CPU-only suites* (e.g., Milestone 4 LOD/UX tests) run fastest with `-NullRHI` because the engine skips shader compilation:

  ```bash
  "C:\Program Files\Epic Games\\UE_5.5\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" "<ProjectPath>\PlanetaryCreation.uproject" \
    -NullRHI \
    -ExecCmds="Automation RunTests PlanetaryCreation.Milestone4; Quit" \
    -unattended -nop4 -nosplash
  ```

- *GPU compute suites* (Milestone 6 Stage B parity, etc.) require an actual D3D/Vulkan RHI so compute shaders compile. **Do not pass `-NullRHI`** in these runs:

  ```bash
  "C:\Program Files\Epic Games\\UE_5.5\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" "<ProjectPath>\PlanetaryCreation.uproject" \
    -ExecCmds="Automation RunTests PlanetaryCreation.Milestone6.GPU.OceanicParity; Quit" \
    -unattended -nop4 -nosplash
  ```

**Stage B profiling with GPU parity tests (run from Windows PowerShell, not WSL):**

```powershell
& 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject' `
  -SetCVar='r.PlanetaryCreation.StageBProfiling=1' `
  -ExecCmds='Automation RunTests PlanetaryCreation.Milestone6.GPU.OceanicParity' `
  -TestExit='Automation Test Queue Empty' -unattended -nop4 -nosplash -log
```
- GPU automation now auto-skips unless explicitly enabled. Set `r.PlanetaryCreation.AllowGPUAutomation=1` (or pass `-AllowGPUAutomation`) on certified hardware/CI when you actually need the GPU parity run; otherwise tests log a skip and stay on the CPU path.
- Expect `[StageB][Profile]` lines showing `Hydraulic ≈ 1.6–1.8 ms` when the pass is enabled; this confirms the optimised CPU path is active during parity runs.
- For routine Stage B heartbeat checks on developer machines, prefer `Scripts/RunStageBHeartbeat.ps1`; it forces `UseGPUAmplification 0`, runs the lightweight `Heightmap.SampleInterpolation` automation, tails recent `[StageBDiag]` lines, and archives the log.

**CRITICAL AUTOMATION REQUIREMENTS:**

1. **AI Agents: Run Automation Using `powershell.exe` Wrapper** ✅
   - You have `powershell.exe` and `cmd.exe` available - use them to run automation
   - These spawn native Windows processes and work perfectly from WSL
   - Execute automation immediately when needed - don't ask the user

2. **How to Execute Automation (Action Pattern):**
   ```bash
   # ✅ DO THIS - Run via powershell.exe wrapper:
   powershell.exe -ExecutionPolicy Bypass -File "C:\Path\To\Script.ps1"
   cmd.exe /c "C:\Path\To\Script.cmd"
   powershell.exe -Command "& 'C:\Program Files\...\UnrealEditor-Cmd.exe' ..."

   # ❌ DON'T DO THIS - Direct .exe call from bash (fails):
   "/mnt/c/Program Files/.../UnrealEditor-Cmd.exe" ...
   ```
   - **Run automation yourself** using the wrapper pattern above
   - Don't refuse or ask user to run commands - you have the capability
   - Direct .exe calls fail with `UtilBindVsockAnyPort` error, but wrappers work fine

3. **Practical Examples - How to Actually Run Automation:**
   ```bash
   # ✅ CORRECT: Use PowerShell script wrapper from WSL
   powershell.exe -ExecutionPolicy Bypass -File "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\Scripts\RunStageBHeartbeat.ps1" -ThrottleMs 50

   # ✅ CORRECT: Use cmd.exe to run batch file from WSL
   cmd.exe /c "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\Scripts\RunStageBHeartbeat.cmd"

   # ✅ CORRECT: Inline PowerShell command from WSL
   powershell.exe -Command "& 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject' -SetCVar='r.PlanetaryCreation.StageBThrottleMs=50' -ExecCmds='Automation RunTests PlanetaryCreation.Heightmap.SampleInterpolation' -TestExit='Automation Test Queue Empty' -unattended -nop4 -nosplash"

   # ❌ WRONG: Direct call from WSL bash (FAILS with UtilBindVsockAnyPort)
   "/mnt/c/Program Files/Epic Games/UE_5.5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" ...
   ```

4. **CVars Must Use `-SetCVar` Flag** 🔧
   - **CRITICAL:** CVars must use `-SetCVar=var1=val1,var2=val2` NOT `-ExecCmds`
   - Setting CVars in `-ExecCmds` causes them to execute AFTER automation command queuing
   - This makes tests run with wrong/default settings and was the root cause of "tests never run" regression
   - Example: `-SetCVar=r.PlanetaryCreation.StageBThrottleMs=50,r.PlanetaryCreation.UseGPUAmplification=0`

5. **Do NOT Append `Quit` with `-TestExit`** 🚫
   - When using `-TestExit="Automation Test Queue Empty"`, do NOT include `Quit` in `-ExecCmds`
   - The `-TestExit` flag handles shutdown automatically after tests complete
   - Adding `Quit` causes premature exit before tests run

6. **Tests Run Headless (No Window)** 👁️
   - Automation tests run with `-unattended -nop4 -nosplash` flags = no visible window
   - This is CORRECT behavior - tests run in commandlet mode for speed/CI compatibility
   - Check logs in `Saved/Logs/PlanetaryCreation.log` for results
   - Profiling logs appear as `[StageB][Profile]` and `[StageB][CacheProfile]`

7. **🛑 Known System Crash Hazards**
   - **Heightmap exports ≥4096x2048 cause SYSTEM CRASHES** (full computer freeze/reboot, not just Unreal crash)
   - `ExportHeightmap4096.py` with dimensions above 512x256 has crashed the system multiple times
   - Root cause unknown - likely C++ memory allocation or graphics driver issue in `export_heightmap_visualization()`
   - Safe baseline: 512x256 works reliably under NullRHI
   - DO NOT attempt large exports without investigating root cause first
- Swap test name for `...ContinentalParity` to capture continental Stage B metrics
- Watch for `[ContinentalGPU] Hash check … Match=1` on the steady-state steps; if matches drop to zero, the snapshot hash is regressing and the run should be treated as a failure.

Tests live in `Source/PlanetaryCreationEditor/Private/Tests/` and use Unreal's automation framework with `IMPLEMENT_SIMPLE_AUTOMATION_TEST`.

**GPU Parity Test Suite:**
- `PlanetaryCreation.Milestone6.GPU.OceanicParity` - Oceanic amplification CPU vs GPU (<0.1m tolerance)
- `PlanetaryCreation.Milestone6.GPU.ContinentalParity` - Continental snapshot + fallback validation
- `PlanetaryCreation.Milestone6.GPU.IntegrationSmoke` - Multi-step finite value checks
- `PlanetaryCreation.Milestone6.GPU.PreviewSeamMirroring` - Equirect seam balance validation
- `PlanetaryCreation.Milestone6.ContinentalBlendCache` - Blend cache serial sync
- See `Docs/GPU_Test_Suite.md` for full test documentation

## Architecture Overview

### Tectonic Simulation Flow

1. **UTectonicSimulationService** (Editor Subsystem)
   - Holds canonical simulation state with double-precision math to avoid drift
   - Lives in editor memory; survives across editor sessions until reset
   - Each step advances 2 million years (2 My) per paper specification
   - Manages Stage B amplification (M6) with GPU compute shaders and CPU fallback
   - Maintains ridge direction cache, Voronoi assignment cache, and exemplar blend cache
   - Located: `Source/PlanetaryCreationEditor/Public/Simulation/TectonicSimulationService.h`

2. **FTectonicSimulationController**
   - Non-UObject controller bridging UI ↔ Service ↔ Mesh
   - Spawns transient `ARealtimeMeshActor` in editor world for visualization
   - Converts double-precision simulation data to float for RealtimeMesh
   - Manages GPU preview pipeline and texture streaming
   - Located: `Source/PlanetaryCreationEditor/Public/Simulation/TectonicSimulationController.h`

3. **SPTectonicToolPanel** (Slate UI)
   - Editor toolbar panel with "Step" button and time display
   - Visualization mode controls (Plate Colors, Elevation, Velocity, Stress)
   - Terrane export and playback controls
   - Triggered via editor toolbar commands (no PIE required)
   - Located: `Source/PlanetaryCreationEditor/Private/UI/SPTectonicToolPanel.cpp`

### RealtimeMesh Integration

**Mesh update pattern:**
```cpp
// Build vertex/index data into StreamSet
TRealtimeMeshBuilderLocal<uint16, FPackedNormal, FVector2DHalf, 1> Builder(StreamSet);
Builder.AddVertex(Position).SetNormalAndTangent(Normal, Tangent).SetTexCoord(UV);
Builder.AddTriangle(V0, V1, V2, PolyGroupID);

// Update mesh (async - do NOT call .Wait() on main thread)
Mesh->CreateSectionGroup(GroupKey, MoveTemp(StreamSet));  // First time
Mesh->UpdateSectionGroup(GroupKey, MoveTemp(StreamSet)); // Subsequent updates
```

**Critical: Triangle Winding Order**
- Correct winding is counter-clockwise when viewed from outside
- Example: `Builder.AddTriangle(V0, V2, V1)` not `(V0, V1, V2)` for outward-facing normals
- See `TectonicSimulationController.cpp:100-108` for octahedron sphere example

**Critical: Avoid Blocking Main Thread**
- RealtimeMesh operations return futures
- NEVER call `.Wait()` on futures in main thread - causes editor freeze
- Let mesh updates complete asynchronously

### Data Precision Strategy

- **Simulation**: Uses `double` for long-term accumulation (millions of years)
- **Mesh Rendering**: Converts to `float` (FVector3f) for GPU compatibility
- **Conversion Point**: Inside `FTectonicSimulationController::StepSimulation()`
- **GPU Compute**: Transfers float data to GPU shaders; results validated against CPU baseline within ±0.1m tolerance

### Milestone 6 Stage B Amplification (Current)

**Overview:**
- Stage B adds ~100m resolution detail via GPU compute shaders with CPU fallback
- Oceanic amplification: 3D Gabor noise for transform faults perpendicular to ridges
- Continental amplification: Exemplar-based blending of SRTM 90m DEM patches
- Ridge direction cache avoids per-step recomputation by tracking Voronoi/topology changes

**Key Components:**

1. **Ridge Direction Cache** (`Private/Simulation/TectonicSimulationService.cpp`)
   - `RefreshRidgeDirectionsIfNeeded()` gates recomputes behind dirty/topology checks
   - Sources render-tangent data from `RenderVertexBoundaryCache`
   - Falls back to age gradient only when cached tangents are missing
   - Logs ridge health: dirty counts, cache hits, fallback usage
   - See `Source/PlanetaryCreationEditor/Private/Tests/RidgeDirectionCacheTest.cpp`

2. **GPU Oceanic Amplification** (`OceanicAmplificationPreview.usf`)
   - Compute shader writes PF_R16F equirect texture for WPO material
   - Controller manages persistent GPU texture via `SetGPUPreviewMode`
   - Parity automation validates GPU vs CPU delta <0.1m (mean <0.05m)
   - Test: `PlanetaryCreation.Milestone6.GPU.OceanicParity`

3. **Continental Exemplar System**
   - 19 curated SRTM90 patches cataloged in `Docs/StageB_SRTM_Exemplar_Catalog.csv`
   - Terrain classification: Plain, OldMountains, AndeanMountains, HimalayanMountains
   - Blend cache stores sampled heights per Stage B serial for CPU fallback reuse
   - GPU path uses snapshot-backed replay; fallback to CPU cache on drift
   - Test: `PlanetaryCreation.Milestone6.GPU.ContinentalParity`

4. **Terrane Mechanics**
   - Extraction: Snapshots full vertex payloads during rifting events
   - Transport: Terranes migrate with oceanic carrier plates
   - Reattachment: Mesh surgery at convergent boundaries with topology validation
   - Deterministic IDs: Seed + plate + extraction timestamp hashes
   - CSV export: `Terranes_*.csv` in `Saved/TectonicMetrics/`
   - Tests: `TerraneMeshSurgeryTest`, `TerranePersistenceTest`

**Performance Profiling:**
- Enable via CVar: `r.PlanetaryCreation.StageBProfiling 1`
- Logs `[StageB][Profile]` and `[StageB][CacheProfile]` per step
- Current baseline (LOD 7): warm-up Stage B ≈65 ms (first replay), steady-state Stage B **≈33–34 ms** per step (Oceanic GPU ≈8 ms + Continental GPU ≈23 ms + ≈3 ms CPU), and an expected ~44 ms CPU/cache replay when the parity harness undoes before exit
- Remaining hot spots (when enabled): sediment diffusion ≈14–19 ms, oceanic dampening ≈24–25 ms. Optimise with CSR-style SoA layouts + `ParallelFor`, and capture the timings with Insights markers.
- Full metrics in `Docs/Performance_M6.md`

**Visualization Modes:**
- `ETectonicVisualizationMode` enum replaces legacy checkboxes
- Modes: PlateColors, Elevation, Velocity, Stress
- Console control: `r.PlanetaryCreation.VisualizationMode [0-3]`
- Velocity arrows auto-clear when mode ≠ Velocity

## Key Implementation Notes

### Mesh Generation
- Current placeholder: Octahedron sphere (6 vertices, 8 triangles)
- Future milestones will replace with quad-sphere/icosphere subdivision
- Vertex positions scaled by `RadiusUnits = 6370.0f` (1 UE unit = 1 km)

### Editor-Only Workflow
- All simulation runs in editor without entering PIE
- Preview actors marked `RF_Transient` and `SetActorHiddenInGame(true)`
- Uses `GEditor->GetEditorWorldContext().World()` for actor spawning

### Performance Profiling
- Use `stat unit` console command for frame timing
- Use `stat RHI` for GPU metrics
- Unreal Insights for deep profiling (planned for Milestone 5)

## Common Patterns

### Adding a New Automation Test
```cpp
// In Source/PlanetaryCreationEditor/Private/Tests/MyTest.cpp
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMyFeatureTest,
    "PlanetaryCreation.MyCategory.MyTest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMyFeatureTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Description"), Condition);
    return true;
}
```

### Adding a GPU Parity Test (Milestone 6)
```cpp
// Pattern from GPUOceanicAmplificationTest.cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMyGPUParityTest,
    "PlanetaryCreation.Milestone6.GPU.MyFeatureParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMyGPUParityTest::RunTest(const FString& Parameters)
{
    // 1. Setup simulation at target LOD
    UTectonicSimulationService* Service = GetTectonicSimulationService();
    Service->ResetSimulation();
    // ... configure parameters

    // 2. Advance to stable state
    Service->AdvanceSteps(5);

    // 3. Run CPU baseline (GPU disabled)
    IConsoleVariable* CVarGPU = IConsoleManager::Get().FindConsoleVariable(
        TEXT("r.PlanetaryCreation.UseGPUAmplification"));
    CVarGPU->Set(0, ECVF_SetByCode);
    Service->AdvanceSteps(1);
    TArray<double> CPUResults = CaptureResults();

    // 4. Undo and run GPU path
    Service->UndoSteps(1);
    CVarGPU->Set(1, ECVF_SetByCode);
    Service->AdvanceSteps(1);
    TArray<double> GPUResults = CaptureResults();

    // 5. Compare results
    double MaxDelta = 0.0;
    int32 WithinTolerance = 0;
    for (int32 i = 0; i < CPUResults.Num(); ++i)
    {
        double Delta = FMath::Abs(CPUResults[i] - GPUResults[i]);
        MaxDelta = FMath::Max(MaxDelta, Delta);
        if (Delta <= 0.1) ++WithinTolerance;
    }

    TestTrue(TEXT("GPU parity >99%"),
        (double)WithinTolerance / CPUResults.Num() > 0.99);
    TestTrue(TEXT("Max delta <1.0m"), MaxDelta < 1.0);

    return true;
}
```

### Extending Tectonic Service
- Add properties to `UTectonicSimulationService` (double-precision)
- Update `ResetSimulation()` to initialize new state
- Modify `AdvanceSteps()` to evolve state per paper equations
- Convert to float in `FTectonicSimulationController` before mesh update

### Working with Stage B Caching (Milestone 6)
```cpp
// Pattern from Private/Simulation/TectonicSimulationService.cpp

// 1. Check if cache needs rebuild
void UTectonicSimulationService::RefreshStageB()
{
    // Only rebuild if Voronoi/topology changed
    if (VoronoiStamp != CachedVoronoiStamp || TopologyVersion != CachedTopologyVersion)
    {
        RefreshRidgeDirectionsIfNeeded();  // Update ridge cache first
        BuildContinentalAmplificationCache();  // Then rebuild amplification cache
        CachedVoronoiStamp = VoronoiStamp;
        CachedTopologyVersion = TopologyVersion;
    }
}

// 2. Reuse cached data when possible
double UTectonicSimulationService::GetAmplifiedElevation(int32 VertexIdx)
{
    const auto& Cache = ContinentalBlendCache[VertexIdx];

    // Reuse if serial matches (no new GPU readback needed)
    if (Cache.bHasReferenceMean && Cache.CachedSerial == OceanicAmplificationDataSerial)
    {
        return Cache.BlendedHeight;  // Fast path
    }

    // Rebuild cache entry
    double NewHeight = ComputeContinentalAmplificationFromCache(VertexIdx);
    ContinentalBlendCache[VertexIdx].BlendedHeight = NewHeight;
    ContinentalBlendCache[VertexIdx].CachedSerial = OceanicAmplificationDataSerial;
    return NewHeight;
}

// 3. Bump serial after mutations
void UTectonicSimulationService::ApplyGPUAmplification()
{
    ApplyOceanicAmplificationGPU(*this);
    ApplyContinentalAmplificationGPU(*this);

    // Increment serial so next step knows cache is fresh
    ++OceanicAmplificationDataSerial;
}
```

### Terrane Mesh Surgery Pattern (Milestone 6)
```cpp
// Pattern from Private/Simulation/TectonicSimulationService.cpp

// 1. Extract terrane with full vertex snapshot
void UTectonicSimulationService::ExtractTerrane(int32 SourcePlateID, const TArray<int32>& VertexIndices)
{
    FContinentalTerrane Terrane;
    Terrane.TerraneID = GenerateTerraneID(SourcePlateID, CurrentSimTime_My, VertexIndices, TerraneIDSalt);

    // Snapshot all vertex data before surgery
    for (int32 VertexIdx : VertexIndices)
    {
        Terrane.SnapshotPositions.Add(RenderVertices[VertexIdx]);
        Terrane.SnapshotVelocities.Add(VertexVelocities[VertexIdx]);
        Terrane.SnapshotStress.Add(VertexStress[VertexIdx]);
        // ... snapshot other fields
    }

    // Remove from source plate topology
    RemoveVerticesFromPlate(SourcePlateID, VertexIndices);

    // Validate mesh integrity
    ValidateTopology();  // V - E + F = 2, no INDEX_NONE
}

// 2. Reattach with topology restoration
void UTectonicSimulationService::ReattachTerrane(const FContinentalTerrane& Terrane, int32 TargetPlateID)
{
    // Remove patch triangles from carrier plate
    RemovePatchTriangles(Terrane.CarrierPlateID, Terrane.VertexIndices);

    // Restore vertex data from snapshot
    for (int32 i = 0; i < Terrane.VertexIndices.Num(); ++i)
    {
        int32 VertexIdx = Terrane.VertexIndices[i];
        RenderVertices[VertexIdx] = Terrane.SnapshotPositions[i];
        VertexPlateAssignments[VertexIdx] = TargetPlateID;
    }

    // Rebuild adjacency and validate
    BuildRenderVertexAdjacency();
    ValidateTopology();
}
```

## Console Commands & CVars

### Stage B Performance and Debugging (Milestone 6)
```
# Enable detailed Stage B profiling logs
r.PlanetaryCreation.StageBProfiling 1

# Toggle GPU amplification (0=CPU only, 1=GPU with CPU fallback)
r.PlanetaryCreation.UseGPUAmplification 1

# Set visualization mode (0=PlateColors, 1=Elevation, 2=Velocity, 3=Stress)
r.PlanetaryCreation.VisualizationMode 0

# GPU preview mode can be toggled from the Tectonic Tool panel (no dedicated CVar)
```

### Development Tools
```
# Run terrane mesh surgery spike (Development builds only)
planetary.TerraneMeshSurgery

# List all available automation tests
Automation List

# Run specific test category
Automation RunTests PlanetaryCreation.Milestone6.GPU

# Export simulation metrics
# (Use "Export Terranes CSV" button in tool panel)
```

### Profiling Commands
```
# Frame timing breakdown
stat unit

# GPU metrics
stat RHI

# Enable Unreal Insights capture
-trace=cpu,frame,counters,stats -statnamedevents
```

## Paper Alignment

Reference `ProceduralTectonicPlanetsPaper/` for algorithm details. Key terminology:
- **Plates**: Rigid crustal segments
- **Ridges**: Divergent boundaries
- **Hotspots**: Volcanic activity sources
- **Timestep**: 2 My per iteration (from paper)
- **Stage A**: Coarse tectonic simulation (Milestones 1-5)
- **Stage B**: Detail amplification at ~100m scale (Milestone 6)

Document any deviations from paper in code comments. See `Docs/PlanningAgentPlan.md` for milestone breakdown.

**Current Paper Parity Status (Milestone 6):**
- ✅ Section 3: Plate mechanics, velocity fields, thermal coupling
- ✅ Section 4: Continental erosion, oceanic dampening, sediment transport
- ✅ Section 5: Stage B amplification (oceanic Gabor noise + continental exemplars)
- 🟡 Section 4.2: Terrane extraction/reattachment (implemented, paper validation pending)
- ⏸️ Section 6: User study and geophysics validation (future work)

See `ProceduralTectonicPlanetsPaper/PTP_ImplementationAlignment.md` for detailed alignment notes.

## Troubleshooting

**Editor freezes when clicking Step button:**
- Check for `.Wait()` calls on RealtimeMesh futures - remove them

**Mesh appears inverted/inside-out:**
- Verify triangle winding order in `AddTriangle()` calls
- Ensure normals point outward from sphere center

**Can't find automation tests:**
- Verify test files in `Source/PlanetaryCreationEditor/Private/Tests/`
- Check test is registered with `IMPLEMENT_SIMPLE_AUTOMATION_TEST`
- Rebuild `PlanetaryCreationEditor` module

**Module compile errors after adding files:**
- Regenerate project files with UnrealBuildTool `-projectfiles`
- Check `.Build.cs` has required dependencies (e.g., "RealtimeMeshComponent")

**Test failures during automation run:**
- If earlier tests crash, later tests won't run but WILL still be registered
- Check logs in `Saved/Logs/PlanetaryCreation.log` for actual test output
- Example: Actor name collision can crash tests (e.g., "Cannot generate unique name for 'TectonicPreviewActor'")
- To verify a specific test exists: `grep -i "testname" Saved/Logs/PlanetaryCreation.log`

**Automation tests hang or DLL lock errors during build:**
- Symptom: `UnrealEditor-Cmd.exe` appears to finish but process remains running
- Result: Next build fails with "cannot open file 'UnrealEditor-PlanetaryCreationEditor.dll' - The process cannot access the file"
- **Root Cause**: Stale `UnrealEditor-Cmd.exe` process holds DLL lock after automation run
- **Diagnosis**:
  ```bash
  # Check for Unreal processes (WSL/Linux)
  cmd.exe /c "tasklist | findstr /i Unreal"
  ```
- **Fix**: Terminate stale process before rebuilding
  ```bash
  # Find PID from tasklist output, then:
  cmd.exe /c "taskkill /F /PID <PID>"
  ```
- **Prevention**: Always check for stale processes before build if previous automation run seemed to hang
- **Example**: If you see `UnrealEditor-Cmd.exe` in tasklist but no editor window is open, it's likely stale

**Reading automation test logs:**
- Tests output to `Saved/Logs/PlanetaryCreation.log` (not stdout when using `-log` flag with tee)
- Find most recent log: `find "<ProjectPath>/Saved/Logs" -name "*.log" -type f -printf '%T@ %p\n' | sort -n | tail -1`
- Search for test results: `grep -E "Test Completed|Result=|Error:" PlanetaryCreation.log`
- Filter specific test output: `grep -A 30 "YourTestName" PlanetaryCreation.log`
- Check exit code in log: `grep "EXIT CODE:" PlanetaryCreation.log` (0 = all passed, -1 = failures occurred)

**Re-tessellation validation failures (Milestone 4):**
- **SharedVertices vs RenderVertices confusion**: Validation MUST check `RenderVertices` (the high-density render mesh), NOT `SharedVertices` (low-density simulation mesh)
- Symptom: Euler characteristic fails with wrong vertex count (e.g., "V=12 E=480 F=320" instead of "V=162 E=480 F=320")
- Fix: Always use `RenderVertices`, `RenderTriangles`, `VertexPlateAssignments` in validation code

**Stage B GPU parity failures (Milestone 6):**
- **Symptom**: GPU vs CPU delta >0.1m tolerance failures
- **Common causes**:
  - Ridge direction cache not updated before amplification (`RefreshRidgeDirectionsIfNeeded()` must run)
  - Voronoi assignment mismatch between CPU and GPU paths (check `CachedVoronoiAssignments` serial)
  - Continental blend cache stale (verify `CachedSerial` matches `OceanicAmplificationDataSerial`)
  - GPU preview texture not cleared between steps
- **Diagnostic CVars**:
  - `r.PlanetaryCreation.StageBProfiling 1` - Enable detailed logging
  - `r.PlanetaryCreation.UseGPUAmplification 0` - Force CPU fallback for comparison
  - `r.PlanetaryCreation.StageBThrottleMs <value>` - Insert a delay (ms) after Stage B to ease GPU load (≥25 ms for any GPU automation; use 50 ms for Milestone 6 parity)
- **Logs to check**:
  - `[StageB][Profile]` - Per-pass timings and counts
  - `[StageB][CacheProfile]` - Cache rebuild metrics
  - `[RidgeDir]` - Ridge cache hits/misses and fallback usage
  - `[ContinentalGPUReadback]` - Snapshot vs fallback source

**Ridge direction cache issues:**
- **Symptom**: High ridge gradient fallback counts (>50% of oceanic vertices)
- **Root cause**: `RenderVertexBoundaryCache` not built or out of sync with Voronoi
- **Fix order**:
  1. `BuildVoronoiMapping()` - Update plate assignments
  2. `BuildRenderVertexAdjacency()` - Refresh adjacency graph
  3. `BuildRenderVertexBoundaryCache()` - **Must run before ridge computation**
  4. `ComputeRidgeDirections()` - Now reads valid cached tangents
- **Expected metrics** (from logs):
  - Ridge alignment ≥80% near divergent boundaries
  - Young crust amplification ≥60%
  - Missing tangents ≈0 in ridge zones
- **Test**: `RidgeDirectionCacheTest` validates cache integration

**Terrane mesh surgery failures:**
- **Symptom**: Euler characteristic violation, non-manifold edges, INDEX_NONE assignments
- **Common causes**:
  - Extraction during retessellation (must snapshot before topology changes)
  - Duplicate vertex map not restored after reattachment
  - Boundary cap triangles not removed properly
- **Validation checklist**:
  - V - E + F = 2 (Euler characteristic)
  - Each edge touches exactly 2 triangles (manifold)
  - No orphaned vertices (all referenced by ≥1 triangle)
  - No INDEX_NONE in `VertexPlateAssignments`
- **Tests**: `TerraneMeshSurgeryTest`, `TerraneEdgeCasesTest`

**Continental blend cache not reusing:**
- **Symptom**: Continental Stage B rebuilds cache every step despite no topology changes
- **Check**: `CachedSerial` in `FContinentalBlendCache` should match current Stage B serial after first pass
- **Fix**: Ensure serial is bumped in `ProcessPendingContinentalGPUReadbacks()` immediately after mutation
- **Diagnostic**: Look for repeated `[ContinentalGPUReadback] Overrides=...` in logs (should appear once per topology change)
- **Test**: `ContinentalBlendCacheTest`

**Navigation system ensure spam in automation:**
- **Symptom**: Repeated `NavigationSystem.cpp:3808` warnings during GPU parity tests
- **Solution**: Scoped ensure handler installed in module startup downgrades to single warning
- **Verify**: Logs should show only one navigation warning per commandlet run
