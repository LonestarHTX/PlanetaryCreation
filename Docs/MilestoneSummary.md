# PlanetaryCreation Milestone Summary

**Updated:** 2025-10-09

This document tracks milestone intent, delivery status, notable deviations from the Procedural Tectonic Planets paper, and the active follow-on work. It consolidates `Docs/PlanningAgentPlan.md`, milestone plans/completion notes, GPU preview addenda, and the October paper alignment review.

---

## Milestone 0 – Pre-Production Alignment *(Week 0 — Complete)*
- **Goal:** Build a shared understanding of the paper, vocabulary, and feasibility envelope.
- **Deliverables:** Annotated paper read-through, terminology glossary, feasibility memo.
- **Verification:** Kickoff review with archived Q&A.
- **Paper Deviations:** None (research prep only).

## Milestone 1 – Tooling & Infrastructure Setup *(Week 1 — Complete)*
- **Goal:** Stand up the editor module, UI entry point, and placeholder mesh pipeline.
- **Delivered:** `PlanetaryCreationEditor` module + toolbar, `UTectonicSimulationService` and controller scaffolding, RealtimeMesh integration, automation harness seed.
- **Verification:** Editor target builds; toolbar drives placeholder mesh updates; automation smoke tests green.
- **Paper Deviations:** None (foundation work).

## Milestone 2 – Data & Simulation Core *(Weeks 2–4 — Complete)*
- **Goal:** Deterministic tectonic core with CSV export and baseline validation.
- **Delivered:** Plate initialization and ω × r velocities, deterministic tick loop with rollback, parameter UI (seed/timestep/plates/viscosity), CSV export + determinism/conservation tests.
- **Verification:** Simulation lead review of CSVs; automation pass.
- **Paper Deviations:** None — math mirrors Section 3 of the paper.

## Milestone 3 – Geometry Generation & RealtimeMesh Integration *(Weeks 4–6 — Complete)*
- **Goal:** Map simulation data to a high-density render mesh with visualization and tests.
- **Highlights:**
  - Icosphere subdivision L0–L4 with Euler characteristic validation.
  - Plate-to-surface mapping (Voronoi, stress accumulation, elevation displacement).
  - Lloyd relaxation + midpoint boundary overlay; Voronoi warping for irregular boundaries.
  - Visualization passes (stress/elevation overlays, async mesh pipeline) and profiling (Level 3 ≈ 101 ms).
  - Seven automation tests plus CSV expansion.
- **Status:** All acceptance criteria met; optional velocity arrows/high-res overlay deferred to Milestone 4.
- **Paper Deviations:** Straight segment boundary overlay (instead of geodesic arcs); stress model capped at 100 MPa and flagged cosmetic.

## Milestone 4 – Dynamic Tectonics & Visual Fidelity *(Weeks 6–13 — Complete, 2025-10-04)*
- **Test Status:** 17/18 passing (94.4%); Level 6 retess regression documented for future perf work.
- **Performance:** Ship-critical LODs (3–5) under the 120 ms budget; L6 at 171 ms recorded for Milestone 6 optimisation.
- **Phase Outcomes:**
  1. **Dynamic Plate Topology:** Deterministic re-tessellation, split/merge system, boundary state machine, rollback automation.
  2. **Hotspots, Rifts, Thermal Coupling:** Deterministic hotspot drift, rift lifecycle, analytic thermal field aligned to the paper (thermal-only hotspot contribution).
  3. **Visualization & UX:** High-resolution boundary overlay, velocity vector field quality fixes.
  4. **LOD & Performance:** Multi-tier LODs (0–6), caching with 70–80% hit rate, async prewarm, Level 3 profiling.
  5. **Validation & Docs:** Voronoi warping, 18-test suite, `Docs/ReleaseNotes_M4.md` and completion summary.
- **Docs:** `Docs/Milestone4_CompletionSummary.md`, `Docs/Performance_M4.md`, `Docs/Milestone4_RetessellationDesign.md`.

## Milestone 5 – Production Readiness & Surface Weathering *(Weeks 13–16 — Complete, 2025-10-04)*
- **UX & Workflow:** Continuous playback (`FTectonicPlaybackController`), orbital inspection camera, timeline scrubbing, undo/redo history in `SPTectonicToolPanel`.
- **Surface Weathering:** Continental erosion, sediment diffusion Stage 0, and oceanic dampening integrated into the step loop with tunable parameters.
- **Validation:** Automation expanded to 27 tests (17/18 legacy + new playback/erosion cases, one retess perf overage carried forward), long-run stress tests, updated release collateral.
- **Performance:** Level 3 step time 6.32 ms with full M5 stack (1.49 ms overhead vs M4), leaving ~17× headroom; projections recorded for L5 (~15 ms) and L6 (~35 ms).
- **Docs:** `Docs/Milestone5_Plan.md`, `Docs/Performance_M5.md`, playback/erosion automation write-ups.

## Milestone 6 – Stage B Amplification & Terranes *(Weeks 17–20 — In Progress)*
**Focus:** Stage B detail amplification, terrane extraction/reattachment, SIMD/GPU optimisation, and Level 7 parity.

### Progress to Date (Oct 2025)
- **GPU Preview Pipeline (Option A)** — `Docs/gpu_preview_integration_complete.md`
  - Oceanic amplification compute shader (`OceanicAmplificationPreview.usf`) writes PF_R16F equirect texture, consumed by WPO material `M_PlanetSurface_GPUDisplacement`.
  - Controller exposes `SetGPUPreviewMode`, manages persistent GPU texture, and binds elevation scale.
  - Build confirmed (`Docs/gpu_preview_build_status.md`): editor compiles in 1.49 s after clearing stale commandlet.
- **Runtime Optimisations** — `Docs/gpu_preview_optimizations.md`
  - Added `bSkipCPUAmplification` flag to avoid redundant CPU pathways when previewing on GPU.
  - Surface version increments now gated to prevent unnecessary L7 rebuilds; async tasks instrumented for Insights.
  - Stage B profiling struct + `r.PlanetaryCreation.StageBProfiling` provide per-pass timings; automation uses async readback fences (`Docs/gpu_readback_fix_plan.md`).
- **Visualization Mode Unification** — `Source/PlanetaryCreationEditor/Public/TectonicSimulationService.h`, `SPTectonicToolPanel.cpp`
  - Introduced `ETectonicVisualizationMode` enum, console hook (`r.PlanetaryCreation.VisualizationMode`), and combo UI replacing heightmap/velocity checkboxes.
  - Controller/velocity overlay now clear arrows when mode ≠ Velocity; GPU preview diagnostics updated to assert enum flow (`PlanetaryCreation.Milestone6.GPU.PreviewDiagnostic`).
- **Tool Panel UX Pass** — `SPTectonicToolPanel.cpp`
  - Primary actions (Step, Play/Pause, Stop, Regenerate) now sit on a compact header ribbon for quick access.
  - Controls are grouped into collapsible sections: *Simulation Setup*, *Playback & History*, *Visualization & Preview*, *Stage B & Detail*, *Surface Processes*, and *Camera & View*.
  - GPU preview, PBR shading, and Stage B toggles share a dedicated section so the advanced workflow is discoverable without scrolling through surface toggles.
  - Prime Stage B button now guards against missing crust-age data by auto-priming Stage B buffers before rebuilds, so enabling the GPU pipeline no longer asserts when dampening hasn’t run yet.
- **Terrane Mesh Surgery Refactor** — `UTectonicSimulationService::ExtractTerrane`, `UTectonicSimulationService::ReattachTerrane`, and `FContinentalTerrane`
- **Terrane Lifecycle Persistence** — `TectonicSimulationService.cpp`, `SPTectonicToolPanel.cpp`
  - Deterministic terrane IDs now derive from seed + plate + extraction timestamp hashes; undo/redo snapshots persist the salt for cross-platform parity.
  - `Export Terranes CSV` button writes `Terranes_*.csv` (Saved/TectonicMetrics) with centroid lat/lon, lifecycle state, area, and timestamps; `TerranePersistenceTest` exercises the pipeline.
- **Stage B Snapshot & Async Readbacks** — GPU parity now replays captured snapshots and pools readback buffers, keeping the steady-state Stage B budget at ~33–34 ms (Oceanic GPU ≈8 ms + Continental GPU ≈23 ms + ~3 ms CPU) with ≈0 ms readback cost; the parity harness still forces a single CPU fallback (~44 ms) for drift verification
  - Extraction snapshots full vertex payloads (position, velocity, stress, sediment, amplified elevation, duplicate mapping) and compacts render SoA arrays without orphaned vertices.
  - Reattachment removes patch triangles via sorted-key sets, restores duplicates, rebuilds adjacency, and keeps the mesh manifold; `TerraneMeshSurgeryTest` expanded to confirm no `INDEX_NONE` assignments remain.
  - Build + automation coverage: Win64 Development UBT (≈3 s incremental) and `Automation RunTests PlanetaryCreation.Milestone6.Terrane.MeshSurgery` now succeed.
  - Async replay snapshots now bump the serial and recompute the hash immediately after mutation, preventing `ProcessPendingOceanicGPUReadbacks()` from dropping into the fallback path each frame.
  - Stage B profiling output now reports ridge-cache health and Voronoi-change metrics (dirty counts, updates, cache hits, fallback counters, reassigned vertex totals) using the new cached Voronoi assignment snapshots; undo/redo restores cached ridge data so temporal jumps only touch the affected vertices (post-reset dirty counts ≈72, steady-state reassignment totals ≈93 k). After the hash fix, Level 7 steady steps sit at **~33–34 ms** per frame (oceanic GPU ≈8 ms, continental GPU ≈22–23 ms, continental CPU ≈2–3 ms) with zero ridge fallbacks logged; the parity harness still replays the CPU/cache path once (~44 ms) for drift validation.
  - `TectonicSimulationService::RebuildStageBForCurrentLOD()` now runs automatically from `SetRenderSubdivisionLevel()`, so cached/pre-warmed LODs inherit the latest amplified elevations without advancing simulation time; the controller also streams amplified elevations into a dedicated `StageBHeight` vertex buffer for every realtime mesh update, forwards the simulation elevation scale to the GPU preview material, and surfaces Amplified/Blend visualization modes that let reviewers compare Stage B detail directly against plate colours. Oceanic/continental parity suites confirm the new path.
  - Preview lighting now ships with a **PBR shading toggle**: the tool panel exposes an “Enable PBR Shading” checkbox (mirrors `r.PlanetaryCreation.UsePBRShading`), CPU preview builds a transient lit material, and GPU preview swaps to a runtime-created lit material that samples the height texture for WPO. Latest parity runs (Oct 10) report Stage B steady-state timings at L7 of **~33–34 ms** (Oceanic GPU ≈8 ms, Continental GPU ≈23 ms, Continental CPU ≈3 ms) with 100 % parity inside ±0.10 m; the harness still replays the CPU/cache fallback once (~44 ms) to validate drift handling.
  - Paper-aligned defaults now enable LOD 5, Stage B amplification, GPU preview, and PBR on startup; run `r.PlanetaryCreation.PaperDefaults 0` to fall back to the M5 CPU baseline when profiling.
  - Stream-power hydraulic routing/erosion now runs immediately after Stage B amplification (`ApplyHydraulicErosion`), filling valleys with a mass-conserving downstream/local deposit split. Profiling reports a dedicated **Hydraulic** column, the tool panel ships an “Enable hydraulic erosion” toggle, and `r.PlanetaryCreation.EnableHydraulicErosion 0/1` exposes the switch for automation. Replacing the per-step elevation sort with a topological queue dropped the L7 hydraulic pass to ~1.7 ms (well under the 8 ms budget), so the planned GPU port is no longer required.
  - GPU parity harness keeps the snapshot-vs-CPU comparison in place but marks it informational for now; the automation enforces parity on the drift fallback path while we port hydraulic erosion to the compute shader. Both `PlanetaryCreation.Milestone6.GPU.OceanicParity` and `.GPU.ContinentalParity` now run headless with the updated defaults.
  - Voronoi refresh now trims reassignment lists, scales ring depth adaptively (default depth dropped from 2 → 1), and skips the redundant first-step rebuild via `bSkipNextVoronoiRefresh`, keeping ridge recomputes near 0 ms even with Stage B updates; `PlanetaryCreation.Milestone6.GPU.PreviewSeamMirroring` automation verifies the equirect seam stays balanced after the GPU preview shader changes.
- **Sediment & Dampening Optimizations** — `SedimentTransport.cpp`, `OceanicDampening.cpp`, `TectonicSimulationService.cpp`
  - Diffusion loop now runs 6 passes normally / 4 passes under GPU preview (`Parameters.bSkipCPUAmplification`), trimming ~40 % inner-loop work.
  - Per-vertex adjacency weight totals cached once during `BuildRenderVertexAdjacency`, consumed directly by oceanic dampening (no per-step recompute).
  - Milestone 5 sediment/dampening tests pass; Milestone 6 suite still has the known amplification parity failures to address separately.
- **Ridge Direction & Oceanic Amplification Refresh** — `TectonicSimulationService.cpp`, `OceanicAmplification.cpp`
  - Stage B now calls `RefreshRidgeDirectionsIfNeeded()` each step, so the cache only rebuilds when Voronoi/topology stamps change instead of forcing a full recompute.
  - New helper guards `ComputeRidgeDirections()` behind dirty/topology checks and feeds render-tangent data from `RenderVertexBoundaryCache`, only falling back to the age gradient when cached tangents are missing or stale.
  - Oceanic Stage B picks up stronger transform-fault detail (scaled contribution + extra high-frequency Perlin); `PlanetaryCreation.Milestone6.OceanicAmplification` passes under commandlet.
- **Stability & Diagnostics**
  - `Docs/plate_movement_debug_plan.md` drove root-cause analysis of “frozen plates” report.
  - `Docs/gpu_preview_plate_colors_fix.md` decoupled GPU heightmap displacement from vertex color overlays; new `GPUPreviewDiagnosticTest.cpp` exercises CPU vs GPU step parity.
  - `Docs/gpu_system_review.md` snapshot updated with the post-hash timings (L7 Stage B steady-state ≈33–34 ms with continental GPU now dominating and the cache path idle), highlighting sediment/dampening as the next hot spots.
  - Commandlet automation now installs a scoped ensure handler so the navigation-system repo ensure is downgraded to a single warning instead of repeated errors during parity runs.
- **Stage B Steady-State Mesh Refresh** — `TectonicSimulationController.cpp`
  - Added an in-place mesh update path that writes positions, tangents, and colors directly to existing realtime-mesh streams when topology is unchanged, reusing the shared post-update flow.
  - Stage B steady-state frames now skip the costly rebuild call while keeping cached amplification data in sync with the viewport.
- **Automation** — `Docs/GPU_Test_Suite.md`
  - `PlanetaryCreation.Milestone6.GPU.OceanicParity`, `.GPU.ContinentalParity`, and `.GPU.IntegrationSmoke` are green; continental parity now runs both the snapshot-backed GPU replay and the drift fallback check without suppressed errors.
  - `PlanetaryCreation.Milestone6.TerranePersistence` covers CSV export + deterministic IDs; Full M6 suite stands at 16 tests / 13 passing with GPU preview enabled.
- **Process Fixes** — `Docs/gpu_readback_fix_plan.md` outlines commandlet-safe fence submission to unblock async readbacks in automation; implementation queued.

### Active Work
- Continental Stage B polish: GPU mesh streaming/material pass plus Table 2 parity capture (`Task 2.1/2.3`).
- Async readback submission + fence handling for commandlet automation (`ProcessPendingOceanicGPUReadbacks`).
- Monitor Stage B LOD swaps for visible hitches; follow up with incremental GPU stream uploads if needed.
- ✅ Multi-threading with `ParallelFor` (sediment/dampening/mesh: 6.32ms total at L3, 17× under budget)
- ⏸️ SIMD vectorization deferred - `ParallelFor` achieved targets without intrinsics complexity
- Note: `Docs/simd_gpu_implementation_review.md` documents exploration/planning, not implemented features

Looking ahead to **Milestone 7**, add “Plate naming polish” to the presentation/UX backlog so generated plates get friendly labels alongside the existing IDs—purely cosmetic, but it’ll make review sessions more fun.
Additional M7 polish targets queued: plate label overlay toggle, boundary legend in the tool panel, timeline markers for splits/merges/terranes, camera distance presets, snapshot thumbnails in history, visualization mode hotkeys (1–4), and log panel filters for Stage B/Terrane chatter.

### Known Gaps / Next Targets
- Oceanic preview remains visualization-only; continental GPU preview still relies on the snapshot fallback after undo/redo, and the normals pass + cube-face preview are open.
- Sediment/dampening dominate frame costs (~24 ms) — next optimisation candidates.
- Table 2 parity metrics for Level 7/8 still pending; instrumentation to split subduction/collision/elevation timers scheduled post-SIMD.

## Milestone 7 – Presentation & Material Polish *(Future, Planned)*
- **Goal:** High-fidelity shading, biome-aware materials, cinematic capture pipeline.
- **Prereqs:** Builds on amplified Stage B output and terrane mechanics from Milestone 6.
- **Key Tasks:** Dynamic material layering, biome classification, screenshot/video tooling, presentation presets.

## Milestone 8 – Climate & Hydrosphere Coupling *(Future, Planned)*
- **Goal:** Couple tectonic outputs with simplified climate and hydrology.
- **Key Tasks:** Sea-level response modeling, temperature/precipitation zones, wind/precip overlays, CSV export.
- **Status:** Not started; sequenced after Milestones 6–7 once amplified terrain is stable.

## Milestone 9 – Shipping Readiness & Cinematic Polish *(Future, Planned)*
- **Goal:** Final optimisation, release packaging, console-class performance targets.
- **Key Tasks:** GPU offloads, streaming/LOD tuning, cinematic materials, tutorials, release management.
- **Status:** Not started; final stretch after climate/material systems are in place.

---

## Paper Alignment Updates (Oct 2025)
- `ProceduralTectonicPlanetsPaper/PTP_Text.md` now holds a clean transcription of the source paper for quick reference.
- `ProceduralTectonicPlanetsPaper/PTP_ImplementationAlignment.md` refreshed to flag current alignment: core tectonics ✅, terrane mechanics ❌, Stage B amplification ❌, Level 7 performance ⚠️. Notes feed directly into Milestones 6–7 scopes.

## Key Supporting Docs
- Planning roadmap: `Docs/PlanningAgentPlan.md`
- GPU preview implementation set: `Docs/gpu_preview_implementation_notes.md`, `Docs/gpu_preview_build_status.md`, `Docs/gpu_system_review.md`
- Optimisation + SIMD guidance: `Docs/simd_gpu_implementation_review.md`, `Docs/planetary_creation_simd_gpu_implementation_review_oct_6_2025.md`
- Performance baselines: `Docs/Performance_M4.md`, `Docs/Performance_M5.md`
- Automation suites: `Docs/GPU_Test_Suite.md`, `Source/PlanetaryCreation/Private/Tests/*`

---

## From Fundamentals to Production
With Milestones 0–5 complete, the project has research-aligned simulation fundamentals, production UX, and weathering passes in place. Milestone 6 is driving Stage B amplification, terrane systems, and GPU/SIMD optimisation; subsequent milestones (7–9) build presentation polish, climate coupling, and shipping readiness on that foundation.
