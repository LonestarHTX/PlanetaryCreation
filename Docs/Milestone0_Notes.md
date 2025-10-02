# Milestone 0 – Procedural Tectonic Planets Overview

## Core Concept Summary
- The paper defines a fully procedural, editor-controllable model for generating tectonic planets by approximating interactions between tectonic plates (subduction, continental collision, oceanic crust formation, plate rifting).
- The workflow produces a coarse crust model `C` via simulation, then amplifies it into a detailed relief `T` using procedural noise or exemplar-based terrain blending.
- Plates are represented as spherical Voronoi cells sampled on a global triangulation; user events can trigger rifting and movement changes mid-simulation.
- Simulation runs on discrete time steps (`δt = 2 My`), updating plate attributes: type (`oceanic`/`continental`), elevation `z`, thickness `e`, age, local ridge direction `r`, fold direction `f`, and orogeny type `o`.
- Validation highlights real-time performance goals (37–145 FPS) on GPU for rendering, with CPU processing for plate interactions and crust amplification.

## Phenomena & Parameters
- **Subduction (Sec. 4.1):** Occurs when oceanic plates collide with oceanic or continental plates, causing the older/denser plate to dive beneath; produces uplift via distance, speed, and elevation transfer curves.
- **Continental Collision (Sec. 4.2):** No subduction; instead the terrane reconnects, producing localized uplift; uses collision radius `r = r_c √(|q|/V₀)` with adjustment factors for plate area.
- **Oceanic Crust Generation (Sec. 4.3):** Diverging plates create ridges; elevation interpolated via template profile `z_T`, blended using distances to ridge/plate.
- **Plate Rifting (Sec. 4.4):** Splits plates via Poisson-driven events; new Voronoi partitions form and plates drift apart.
- **Dampening & Erosion (Sec. 4.5):** Continental erosion, oceanic dampening, sediment filling applied each time step using coefficients `ε_c`, `ε_o`, `ε_s`.
- **Amplification (Sec. 5):** Adds procedural noise (orientable ridge noise) or exemplar-based heightfield primitives aligned with local fold directions.

## Unreal/RealtimeMesh Mapping
- **Plate Data:** Store as structs within an Unreal `UTectonicSimulationService`; leverage `TArray` of plate samples (spherical triangulation points) to map attributes to mesh vertices.
- **Time Step Processing:** Implement deterministic editor tick using a custom `FTectonicSimulationController` invoked via editor subsystem (no PIE).
- **Geometry Representation:** Use RealtimeMesh Section Groups to maintain the spherical surface; apply attribute-driven vertex stream updates (`FRealtimeMeshStreamSet` with elevation, color, normals).
- **Amplification Stage:** Either precompute noise via material functions or CPU-side displacement baked into mesh LODs; support exemplar-based lookup via texture assets.

## Terminology Alignment
- **Plate** → `FTectonicPlate` (containing samples, type, movement vectors).
- **Terrane** → `FTectonicTerrane` data referencing subsets of plate samples for collision attachment.
- **Crust Model C** → Base RealtimeMesh stream/LOD0.
- **Relief T** → Amplified mesh or material displacement textures.
- **User-triggered events** → Editor UI actions (buttons, timeline markers) invoking simulation commands.
- **Ridge direction r** → Vector stored per sample; drives material tangents and amplification orientation.
- **Fold direction f** → Vector used for mountain alignment and noise orientation.

## Key Constants (Appendix A)
| Symbol | Description | Value |
|--------|-------------|-------|
| δt | Time step | 2 My |
| R | Planet radius | 6370 km |
| z_T | Highest oceanic ridge elevation | 1 km |
| z_A | Abyssal plain elevation | -6 km |
| z_o | Oceanic trench elevation | -10 km |
| z_c | Highest continental altitude | 10 km |
| r_s | Subduction distance | 1800 km |
| r_c | Collision distance | 4200 km |
| Δ_c | Collision coefficient | 1.3 × 10^-5 km^-1 |
| v₀ | Max plate speed | 100 mm/yr |
| ε_o | Oceanic dampening | 4 × 10^-7 m^-1 |
| ε_c | Continental erosion | 3 × 10^-7 m^-1 |
| ε_s | Sediment accretion | 3 × 10^-7 m^-1 |
| u₀ | Subduction uplift | 6 × 10^-6 mm^-1 |


## Version Control Strategy
- Maintain long-lived branch `planetary-tool` as the primary development line; push as soon as the branch is created to keep GitHub in sync.
- After each milestone, commit with descriptive message and tag (e.g., `milestone-1-complete`) to provide rollback anchors.
- Use short-lived feature branches (e.g., `feature/rifting-topology`) for risky spikes; delete them if experiments fail.
- Document PR rollbacks by noting the `git revert <merge-sha>` command in each description; merge using non-fast-forward strategies so reverts stay simple.
- Only merge into `main` once a milestone is release-ready; tag releases (`release-m1-prototype`) and revert the merge commit if a hotfix is needed.

**ELI5:** Keep a big coloring book page (`planetary-tool`) where we draw carefully, save a copy after each finished section (tag), use extra scratch paper for experiments, and if a drawing goes wrong we just go back to the last saved copy.


## Time Step Mapping
- Each simulation iteration in the paper advances tectonic time by `δt = 2 My`; CPU processing per step ranges roughly 0.08–1.5 seconds depending on resolution (Sec. 7.4).
- Editor `Step` button should execute a single iteration (2 My) to keep interactions predictable and responsive.
- Provide optional batched stepping (default 5 iterations / 10 My) as an async operation so the editor UI stays fluid; add fast-forward presets later as needed.
- Display cumulative tectonic time in the UI (e.g., “Current Time: 124 My”) so users see the +2 My increment per step.
- Expose planet resolution presets and warn if estimated step cost exceeds ~300 ms to maintain real-time authoring feel.


## Heightfield Exemplar Strategy
- Store exemplar terrains as `Texture2D` assets encoded in 16-bit grayscale (`PF_G16`, `SRGB=false`) under `Content/PlanetaryCreation/Heightfields/`. This keeps them editor-friendly, streamable to materials, and CPU-readable via `Texture->Source.LockMip`.
- Author a companion `UDataTable` (struct `FHeightfieldExemplarMeta`) in `Content/PlanetaryCreation/Data/` containing source name, scaling factors, ridge alignment, and recommended usage (oceanic vs continental). The data table references texture soft object paths.
- Provide a `UHeightfieldExemplarLibrary` helper (C++) that loads the texture, caches a linear `TArray<float>` for CPU amplification, and exposes Blueprint sampling for visualization.
- Preprocess raw DEM files externally (e.g., GDAL) to match power-of-two resolutions (1024², 2048²) before import; keep raw source files in version control-friendly `Docs/Assets/` or fetch-on-demand instructions to avoid repo bloat.
- Keep exemplar usage optional so the procedural noise pipeline still works when textures are missing (falls back to deterministic noise amplification).

## Dataset Licensing Notes
- USGS elevation products (e.g., SRTM, NED) are U.S. Government works and typically public domain, but require acknowledgement; include a LICENSE.txt excerpt in `Content/PlanetaryCreation/Heightfields/USGS/`.
- Document data provenance (dataset name, download URL, access date) in `Docs/DataSources.md`; note any redistribution constraints per dataset readme.
- For any third-party, non-USGS exemplars, confirm license compatibility before import and record terms beside the asset.
- When distributing builds, ensure credits screen and project README cite USGS data per guidance (e.g., “Contains public domain data courtesy of the U.S. Geological Survey”).


## RealtimeMesh Topology Updates
- RMC supports dynamic topology by replacing an entire section group's stream set using `UpdateSectionGroup`. Tests like `RealtimeMeshStressTestActor` rebuild vertex/index arrays each frame without destroying the group.
- After updating the streams, call `UpdateSectionRange` for each section to match new vertex/triangle offsets; rifting can detach or merge sections by adjusting ranges without recreating the component.
- When rifting introduces new plates, spawn additional section groups (one per plate) so updates stay localized; outdated groups can be cleared with `RemoveSectionGroup` once transfers complete.
- Collision data updates asynchronously, so expect a short delay before physics reflects the new topology—queue updates only after visual data is committed.


## Precision Strategy
**Float (32-bit)**
- Pros: Native to Unreal math types (`FVector3f`/`FVector`), fastest, lower memory bandwidth, aligns with RealtimeMesh stream formats.
- Cons: ~7 decimal digits of precision; mapping a 6,370 km radius planet into centimeter units yields large coordinate magnitudes and visible jitter when authoring fine relief (<1 m).

**Double (64-bit)**
- Pros: ~15 decimal digits, enough to store tectonic samples in planetary kilometers without drift across thousands of iterations; stable accumulation for velocity integration and erosion calculations.
- Cons: Heavier CPU cost, larger memory, not directly supported by GPU streams or most UE containers; requires down-converting to float for rendering.

**Recommendation**
- Run the simulation core in double precision (`FVector3d`, `double`) with positions expressed in kilometers (unit sphere radius = 1). Keep plate centers, velocities, and accumulated elevations as doubles to avoid drift during long interactive sessions.
- When emitting geometry to RealtimeMesh, convert to floats by scaling the normalized sphere to the chosen world radius (e.g., 1 Unreal unit = 1 km) and subtracting a local origin if needed to stay within float range.
- Cache the float data only for the current frame; never feed floats back into the simulation state. This hybrid approach keeps the editor stable while satisfying RealtimeMesh’s float-based buffers.



## Performance & Profiling Plan
- Use in-editor `stat unit`, `stat RHI`, and `stat GPU` during manual authoring sessions to catch frame-rate spikes quickly.
- For automated validation, script Unreal Insights captures with `UnrealEditor-Cmd.exe` and `-tracehost=localhost -trace=default`. Store `.utrace` files alongside build artifacts for regression comparison.
- Add a commandlet or automation test hook that runs the simulation for N steps, gathers profiling markers (CPU/GPU times, memory), and emits a JSON summary to `Saved/PerfReports/`.
- During CI, parse the JSON reports and fail the build if step time or memory exceeds set thresholds; keep raw traces for deep analysis when needed.
- Document the workflow in `Docs/Performance.md` (to create in Milestone 5) so we have step-by-step guides for taking manual traces and analyzing them.

## Next Steps for Milestone 1
- Translate plate/terrane structs into UE data definitions.
- Design editor module structure accommodating simulation controller and UI widgets.
- Prototype single-step elevation update on placeholder mesh to validate RealtimeMesh stream updates.

_Reference figures available in `ProceduralTectonicPlanetsPaper/` screenshots._

## Milestone 1 Task Breakdown

### Milestone 1 – Progress Notes
- PlanetaryCreationEditor module boots with dedicated tab, toolbar buttons, and a 2 My step command for the tectonic tool.
- Simulation scaffolding uses double precision and spawns a hidden RealtimeMesh preview actor to exercise `UpdateSectionGroup`/`UpdateSectionRange` on each step.
- Heightfield infrastructure seeded with stub library and content folders (`Content/PlanetaryCreation/Heightfields`, `.../Data`).
- Automation smoke test added to `Source/PlanetaryCreationEditor/Private/Tests` with TODO hook for future Insights capture.

1. **Editor Module Bootstrapping**
   - Add `PlanetaryCreationEditor.Target.cs` and module Build.cs entries.
   - Register the module in `.uproject`; ensure it loads editor-only.
2. **UI Entry Point**
   - Implement toolbar/menu extender creating a "Tectonic Tool" tab.
   - Scaffold Slate widget with `Step (2 My)` button and disabled multi-step controls.
3. **Simulation Scaffolding**
   - Create `UTectonicSimulationService` (double-based state) and `FTectonicSimulationController` skeleton with `Advance(int32 Steps)` placeholder.
   - Add float conversion helper returning `FRealtimeMeshStreamSet` for the placeholder mesh.
4. **RealtimeMesh Prototype**
   - Initialize a simple section group on startup and wire `Step` button to update it via `UpdateSectionGroup`/`UpdateSectionRange`.
5. **Heightfield Infrastructure**
   - Generate content folders (`Content/PlanetaryCreation/Heightfields`, `.../Data`) and stub `UHeightfieldExemplarLibrary`.
   - Create empty `UDataTable` asset + struct for exemplar metadata.
6. **Automation Harness**
   - Add editor automation test module, stub test case logging TODO perf capture.
   - Script CLI command to run tests and leave placeholders for Insights/JSON export.
7. **Documentation & Validation**
   - Update `Docs/Milestone0_Notes.md` with status notes.
   - Record Milestone 1 checklist in `Docs/PlanningAgentPlan.md` once complete.

