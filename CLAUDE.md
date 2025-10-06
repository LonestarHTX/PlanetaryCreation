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

## Running Tests

**In-Editor (Recommended):**
1. Press **`** (backtick) to open console
2. Type: `Automation List` to see available tests
3. Type: `Automation RunTests PlanetaryCreation` to run project tests
4. Or go to **Tools** → **Test Automation** for GUI

**Command-line automation:**
```bash
"C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<ProjectPath>\PlanetaryCreation.uproject" -ExecCmds="Automation RunTests PlanetaryCreation; Quit" -unattended -nop4 -nosplash
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


Tests live in `Source/PlanetaryCreationEditor/Private/Tests/` and use Unreal's automation framework with `IMPLEMENT_SIMPLE_AUTOMATION_TEST`.

## Architecture Overview

### Tectonic Simulation Flow

1. **UTectonicSimulationService** (Editor Subsystem)
   - Holds canonical simulation state with double-precision math to avoid drift
   - Lives in editor memory; survives across editor sessions until reset
   - Each step advances 2 million years (2 My) per paper specification
   - Located: `Source/PlanetaryCreationEditor/Public/TectonicSimulationService.h`

2. **FTectonicSimulationController**
   - Non-UObject controller bridging UI ↔ Service ↔ Mesh
   - Spawns transient `ARealtimeMeshActor` in editor world for visualization
   - Converts double-precision simulation data to float for RealtimeMesh
   - Located: `Source/PlanetaryCreationEditor/Public/TectonicSimulationController.h`

3. **SPTectonicToolPanel** (Slate UI)
   - Editor toolbar panel with "Step" button and time display
   - Triggered via editor toolbar commands (no PIE required)
   - Located: `Source/PlanetaryCreationEditor/Private/SPTectonicToolPanel.cpp`

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

### Extending Tectonic Service
- Add properties to `UTectonicSimulationService` (double-precision)
- Update `ResetSimulation()` to initialize new state
- Modify `AdvanceSteps()` to evolve state per paper equations
- Convert to float in `FTectonicSimulationController` before mesh update

## Paper Alignment

Reference `ProceduralTectonicPlanetsPaper/` for algorithm details. Key terminology:
- **Plates**: Rigid crustal segments
- **Ridges**: Divergent boundaries
- **Hotspots**: Volcanic activity sources
- **Timestep**: 2 My per iteration (from paper)

Document any deviations from paper in code comments. See `Docs/PlanningAgentPlan.md` for milestone breakdown.

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

**Re-tessellation validation failures (Milestone 4):**
- **SharedVertices vs RenderVertices confusion**: Validation MUST check `RenderVertices` (the high-density render mesh), NOT `SharedVertices` (low-density simulation mesh)
- Symptom: Euler characteristic fails with wrong vertex count (e.g., "V=12 E=480 F=320" instead of "V=162 E=480 F=320")
- Fix: Always use `RenderVertices`, `RenderTriangles`, `VertexPlateAssignments` in validation code
