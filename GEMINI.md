# GEMINI.md

This file provides guidance to Gemini when working with code in this repository. It is based on the conventions and workflows defined in `CLAUDE.md` and `AGENTS.md`.

## Project Overview

`PlanetaryCreation` is an Unreal Engine 5.5 editor tool that implements the concepts from the "Procedural Tectonic Planets" paper. The tool runs **entirely in the editor** (no Play-In-Editor/PIE session required) and uses the `RealtimeMeshComponent` plugin for high-performance, real-time visualization of tectonic plate simulations.

### Key Modules

*   **`PlanetaryCreation` (Runtime):** A minimal runtime module that serves as a placeholder.
*   **`PlanetaryCreationEditor` (Editor):** The core of the project. It contains all the logic for the tectonic simulation, the Slate UI, and the mesh generation.
*   **`RealtimeMeshComponent` (Plugin):** A third-party plugin used for efficient, real-time rendering of the procedurally generated planetary mesh.

### Tectonic Simulation Flow

1.  **`UTectonicSimulationService` (Editor Subsystem):** This is the heart of the simulation. It holds the canonical simulation state using **double-precision** floating-point numbers to prevent numerical drift over long simulation times. The service persists in editor memory across sessions. Each step of the simulation advances time by 2 million years, as specified in the paper.
2.  **`FTectonicSimulationController`:** This non-`UObject` controller acts as a bridge between the UI, the simulation service, and the rendered mesh. It spawns a transient `ARealtimeMeshActor` in the editor world for visualization and converts the double-precision simulation data to single-precision floats for the `RealtimeMeshComponent`.
3.  **`SPTectonicToolPanel` (Slate UI):** This is the user-facing editor panel, which includes a "Step" button and a time display. It uses editor toolbar commands to trigger the simulation steps.

## Build, Test, and Development Commands

### Building the Project

**Windows (`cmd.exe`):**
```batch
"C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat" PlanetaryCreationEditor Win64 Development -project="<AbsolutePathToProject>\PlanetaryCreation.uproject"
```

**WSL/Linux (`bash`):**
```bash
"/mnt/c/Program Files/Epic Games/UE_5.5/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" \
  PlanetaryCreationEditor Win64 Development \
  -project="C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject" \
  -WaitMutex -FromMsBuild
```

### Running Tests

**From the Command Line:**
```batch
"C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<AbsolutePathToProject>\PlanetaryCreation.uproject" -ExecCmds="Automation RunTests PlanetaryCreation; Quit" -unattended -nop4 -nosplash
```

**In the Editor:**
1.  Open the console by pressing the backtick key (`` ` ``).
2.  Type `Automation List` to see all available tests.
3.  Type `Automation RunTests PlanetaryCreation` to run all tests for this project.
4.  Alternatively, you can use the Test Automation window by navigating to **Tools > Test Automation**.

### Other Development Commands

**Regenerate Visual Studio Project Files:**
```batch
"C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" -projectfiles -project="<AbsolutePathToProject>\PlanetaryCreation.uproject"
```

**Launch the Editor:**
```batch
"C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor.exe" "<AbsolutePathToProject>\PlanetaryCreation.uproject"
```

## Development Conventions

### Coding Style and Naming

*   Follow the standard Unreal Engine C++ coding style (4-space indentation, no tabs).
*   Use the standard Unreal Engine class prefixes: `A` for Actors, `U` for UObjects, `F` for structs, and `I` for interfaces.
*   Use `PascalCase` for function names and `camelCase` for local variables.
*   Constants should be in `ALL_CAPS`.

### `RealtimeMeshComponent` Integration

*   **Asynchronous Operations:** `RealtimeMeshComponent` operations are asynchronous and return futures. **NEVER** call `.Wait()` on these futures from the main thread, as this will freeze the editor.
*   **Triangle Winding Order:** Ensure the correct triangle winding order (counter-clockwise when viewed from the outside) to prevent inverted or inside-out meshes.
*   **Data Precision:** The simulation uses `double` for precision, but the data must be converted to `float` (`FVector3f`) before being passed to the `RealtimeMeshComponent` for rendering. This conversion happens in `FTectonicSimulationController::StepSimulation()`.

### Testing

*   Tests are created using Unreal's Automation Testing framework with `IMPLEMENT_SIMPLE_AUTOMATION_TEST`.
*   Test suites should be named `FPlanetaryCreation<Feature>Test`.
*   Test files are located in `Source/PlanetaryCreationEditor/Private/Tests/`.

## Troubleshooting

*   **Editor freezes when clicking the "Step" button:** This is likely caused by a `.Wait()` call on a `RealtimeMesh` future from the main thread. Remove these calls and let the mesh updates complete asynchronously.
*   **Mesh appears inverted or inside-out:** Check the triangle winding order in your `AddTriangle()` calls. It should be counter-clockwise. Also, ensure the mesh normals are pointing outward from the sphere's center.
*   **Automation tests not found:**
    *   Ensure the test files are in the correct directory (`Source/PlanetaryCreationEditor/Private/Tests/`).
    *   Verify that the test is registered with `IMPLEMENT_SIMPLE_AUTOMATION_TEST`.
    *   Rebuild the `PlanetaryCreationEditor` module.
*   **Module compile errors after adding new files:** Regenerate the project files using the `-projectfiles` command. Also, check that the `.Build.cs` file has all the required dependencies (e.g., `"RealtimeMeshComponent"`).