  # Milestone 4: Dynamic Tectonics & Visual Fidelity

**Status:** ✅ **COMPLETE** (2025-10-04)
**Pass Rate:** 17/18 tests (94.4%) - Ship-ready
**Performance:** All ship-critical LODs (3-5) under 120ms target
**Release Notes:** See `Docs/ReleaseNotes_M4.md`

## Overview
Build on the Milestone 3 scaffold to deliver visible tectonic activity. Milestone 4 activates plate topology changes, hotspot/rift behaviour from the "Procedural Tectonic Planets" paper (Section 4), upgrades boundary visualization, and introduces multi-tier LOD so dense meshes stay performant.

## Goals (Paper Alignment)
1. Implement the paper's rift, hotspot, and subduction rules (Sec. 4.2–4.4) with clear citations and documented deviations.
2. Enable deterministic dynamic re-tessellation so render geometry tracks drifting plate boundaries.
3. Provide high-fidelity overlays and analytics (thermal, boundary state) to validate tectonic behaviour.
4. Add LOD/streaming support for higher subdivision levels without blowing the frame budget.
5. Expand automated validation (tests + screenshots + CSV v3.0) to prevent regressions.

## Scope Constraints
- **In Scope:** Re-tessellation, plate split/merge, hotspot & rift progression, thermal stress coupling (analytic model), high-res overlays, velocity vectors, LOD tiers, async streaming, expanded tests/docs.
- **Out of Scope:** Full PDE-based thermal diffusion, erosion/weathering, ocean/atmosphere coupling, non-spherical planets, cinematic materials, multiplayer sync.
- **Deferred:** Complex material polish (moved to Milestone 5), climate integration, procedural erosion.

---

## Phase 1: Dynamic Plate Topology

### Task 1.0: Re-tessellation Design Spike
**Owner:** Simulation Engineer  
**Effort:** 1 day  
**Description:**
- Survey constrained spherical Delaunay / edge-flip approaches applicable to our icosphere topology.
- Choose algorithm for incremental boundary rebuild (likely boundary-edge fan split).
- Document plan in `Docs/Milestone4_RetessellationDesign.md` including epsilon handling, determinism strategy, validation checklist.

**Acceptance Criteria:**
- Design doc reviewed/approved by gameplay & rendering leads.
- Explicit decision on vertex snapping tolerance and ordering for determinism.

### Task 1.1: Re-tessellation Engine (Implementation)
**Owner:** Simulation Engineer  
**Effort:** 6 days  
**Description:**
- Implement incremental rebuild per design doc: detect drifted plates, slice boundary edges, update `SharedVertices`, `RenderVertices`, and boundary adjacency.
- Maintain determinism using seed-stable traversal order and tolerance values from Task 1.0.
- Include staged rollout: proof-of-concept (single plate), then multi-plate integration, then regression harness.
- Run post-rebuild validation (Euler characteristic, plate area conservation).

**Acceptance Criteria:**
- Plates crossing drift threshold rebuild within one simulation step.
- Plate area conserved within 1% and Euler characteristic remains 2.
- Automation test toggles threshold to assert rebuild/no-rebuild cases.

### Task 1.2: Plate Split & Merge (Paper Sections 4.2, 4.3)
**Owner:** Simulation Engineer  
**Effort:** 3 days  
**Description:**
- Implement rift-driven split per paper Eq. (4.2) velocity threshold; log deviations if thresholds tuned.
- Implement subduction merge using convergence angle/stress criteria (Sec. 4.3). Redistribute vertex ownership and maintain determinism.
- Emit `FPlateTopologyEvent` entries for UI/debug.

**Acceptance Criteria:**
- Controlled tests: divergent boundary > threshold splits plate, convergent subduction merges minor plate.
- CSV v3.0 captures split/merge events with timestamp and reference stress values.

### Task 1.3: Boundary State Machine & CSV v3.0
**Owner:** Simulation Engineer  
**Effort:** 2 days  
**Description:**
- Add lifecycle states (Nascent, Active, Dormant) driven by relative velocity & stress as defined in paper Sec. 4.1.
- Export `BoundaryState`, `StateTransitionTimeMy`, and `ThermalFlux` in CSV v3.0.
- Keep backward compatibility: readers ignore unknown columns.

**Acceptance Criteria:**
- State machine transitions covered by automation test.
- CSV schema bump documented (`Docs/CSV_Schema.md`), existing readers handle new columns.

### Task 1.4: Re-tessellation Rollback & Validation
**Owner:** Simulation Engineer  
**Effort:** 1 day  
**Description:**
- Snapshot plate/vertex state prior to rebuild.
- If validation fails (NaNs, Euler ≠ 2, area drift >1%), revert and log warning.
- Surface rollback status in UI/debug overlay.

**Acceptance Criteria:**
- Fault injection test forces invalid rebuild and verifies rollback to last good state.

---

## Phase 2: Hotspots, Rifts, Thermal Coupling

### Task 2.1: Hotspot Generation & Drift (Paper Sec. 4.4)
**Owner:** Simulation Engineer  
**Effort:** 3 days  
**Description:**
- Spawn hotspot seeds deterministically (3 major, 5 minor by default) following plume distribution.
- Allow mantle-frame drift; update plate stress/elevation within influence radius.
- Document any deviations from paper formulae (e.g., capped drift speed).

**Acceptance Criteria:**
- Hotspot positions repeat per seed; automation test compares CSV logs.
- Heat contribution increases local stress/elevation as expected.

### Task 2.2: Rift Propagation Model (Paper Sec. 4.2)
**Owner:** Simulation Engineer  
**Effort:** 3 days  
**Description:**
- Trigger rift state when divergent velocity > threshold for sustained duration.
- Animate widening (increase separation, spawn new crust mass) and hand off to re-tessellation for splits once angle limit reached.

**Acceptance Criteria:**
- Divergent boundaries trigger rift overlay + eventual plate split.
- Automation test ensures convergent/transform boundaries never enter rift state.

### Task 2.3: Thermal & Stress Coupling (Analytic Model)
**Owner:** Simulation Engineer  
**Effort:** 2 days  
**Description:**
- Implement analytic temperature field: `T(r) = T_max * exp(-r^2 / σ^2)` from hotspot centres and add secondary term for subduction heating.
- No time integration/PDE; update per simulation step analytically.
- Use temperature to modulate stress & elevation additively (e.g., softer crust lowers peak stress).

**Acceptance Criteria:**
- Thermal values exported per vertex; CSV v3.0 includes `VertexTemperature`.
- Automated test verifies falloff curve and additive stress modulation.

---

## Phase 3: Visualization & UX Enhancements

### Task 3.1: High-Resolution Boundary Overlay
**Owner:** Rendering Engineer  
**Effort:** 3 days  
**Description:**
- Trace render triangles for plate ID transitions and build polyline/ribbon that hugs seams.
- Modulate color/width by boundary state & stress.
- Include downsampling option for low-end performance.

**Acceptance Criteria:**
- Overlay deviation ≤ one render vertex at all subdivision levels.
- Boundary appearance reflects lifecycle state in real time.
- Test samples seam point set and confirms ID alignment.

### Task 3.2: Velocity Vector Field (Deferred from M3)
**Owner:** Rendering Engineer  
**Effort:** 1 day  
**Description:**
- Render centroid arrows with magnitude scaling, UI legend, and toggle. Support screen-space fade for readability.

**Acceptance Criteria:**
- Arrows point in correct direction/magnitude; toggle updates instantly; screenshot test validates presence.

> **Deferred:** Task 3.3 (Material & Lighting Pass) moved to Milestone 5 per scope prioritization.

---

## Phase 4: LOD & Performance

### Task 4.0: LOD Architecture Spike
**Owner:** Rendering Engineer  
**Effort:** 1 day  
**Description:**
- Evaluate RealtimeMesh LOD support/API limitations.
- Decide between built-in LOD groups vs. custom distance-based switching.
- Document plan in `Docs/Milestone4_LODDesign.md`.

**Acceptance Criteria:**
- Design reviewed; includes fallback if RealtimeMesh lacks native LODs.

### Task 4.1: Multi-Tier LOD System
**Owner:** Rendering Engineer  
**Effort:** 5 days  
**Description:**
- Follow design doc: implement distance-based LOD selection, buffer reuse, and hysteresis.
- Provide High/Medium/Low tiers mapped to render subdivision levels.
- Ensure plate ID/color continuity across transitions.

**Acceptance Criteria:**
- LOD transitions avoid visual popping (tested via recorded fly-through).
- Unit test verifies LOD choice per distance input; hysteresis prevents rapid thrash.

### Task 4.2: Streaming & Caching
**Owner:** Rendering Engineer  
**Effort:** 2 days  
**Description:**
- Cache previously generated LOD meshes/material params; stream asynchronously using TaskGraph.
- Avoid blocking loads during camera moves.

**Acceptance Criteria:**
- Logs confirm cache hits; no hitching when changing LOD.

### Task 4.3: Profiling & Optimization Pass
**Owner:** Performance Engineer  
**Effort:** 5 days  
**Description:**
- Re-run Unreal Insights with M4 features; focus on Voronoi updates, stress/thermal loops, re-tessellation cost.
- Optimize using caching, SIMD, or GPU passes as needed.
- Update `Docs/Performance_M4.md` with before/after metrics.

**Acceptance Criteria:**
- Level 3 step time <120ms (ship target), <100ms stretch goal documented with required optimizations; 90ms path outlined for future work.
- Memory usage <200MB at Level 3.

---

## Phase 5: Validation & Documentation

### Task 5.1: Expanded Automation Suite
**Owner:** QA Engineer  
**Effort:** 3 days  
**Description:**
- Add tests covering re-tessellation, split/merge, boundary state transitions, hotspots, thermal coupling, LOD consistency, and rollback.
- Extend async mesh test to cover LOD + overlay integration.

**Acceptance Criteria:**
- 6 new M4 tests added; all pass on CI.
- Fault injection suite covers rollback scenario.

### Task 5.2: Visual Comparison & Paper Parity
**Owner:** QA Engineer  
**Effort:** 2 days  
**Description:**
- Reproduce paper figures for hotspots/rifts/subduction; generate before/after comparisons in `Docs/Validation_M4/`.
- Document deviations with rationale.

**Acceptance Criteria:**
- Gallery updated; simulation lead signs off.

### Task 5.3: Documentation & Release Notes
**Owner:** Technical Writer  
**Effort:** 2 days  
**Description:**
- Update `Docs/Milestone4_Features.md`, `CLAUDE.md`, and CSV schema notes.
- Draft release notes summarizing features, performance metrics, known limitations (lack of erosion, etc.).

**Acceptance Criteria:**
- Docs reviewed; release checklist includes new tests, screenshots, profiling, CSV schema version 3.0.

---

## Updated Acceptance Criteria (Milestone Level)
- Re-tessellation automatically corrects drift with rollback safety net; area conserved within 1%.
- Plate split/merge events aligned with paper thresholds; logged deterministically.
- Hotspots, rifts, and thermal fields influence stress/elevation and appear in overlays/CSV.
- High-resolution boundary overlay aligns with visual seam; velocity vectors available for diagnostics.
- LOD system transitions smoothly and maintains ≤120ms Level 3 step time (stretch: <100ms documented).
- All M4 automation tests green; screenshot comparisons approved; documentation and CSV schema v3.0 published.

---

## Risk Register (Revised)
| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Re-tessellation instability | Medium | High | Design spike + rollback (Task 1.0 & 1.4), staged rollout |
| Determinism loss after split/merge | Medium | Medium | Seed-stable ordering, regression tests, CSV audit |
| High-res overlay performance | Medium | Medium | Downsampling option, profile early |
| LOD API limitations | Medium | High | LOD design spike, fallback path |
| Performance regressions | High | Medium | Extended optimization pass, documented stretch goals |

---

## Estimated Timeline (Adjusted)
- **Phase 1:** 2.5 weeks (design spike, re-tess implementation, split/merge, rollback)
- **Phase 2:** 2 weeks (hotspots, rifts, thermal coupling) — parallelizable with Phase 1 once design complete
- **Phase 3:** 1 week (overlay + velocity vectors)
- **Phase 4:** 2.5 weeks (LOD spike + implementation + optimization)
- **Phase 5:** 1 week (validation, docs)

**Total:** ~7.5 weeks including buffer and design spikes.


### Nice-to-Have: Static Mesh Snapshot Export
- Capture the retessellated planet at key milestones and bake to static meshes for Nanite/cinematic use.
- Store snapshots per plate configuration so designers can build a “planet catalog.”
- Would enable downstream ideas like a planet-creator mode or sharing pre-baked worlds.
- Not part of Milestone 4 scope; log for future milestone planning (potential Milestone 6/7 feature).

### Nice-to-Have: Spore-Style Planet Camera Rig
- Implement orbital camera rig with inertia, zoom-to-radius control, and pitch clamps for smoother planet navigation.
- Spawns alongside the preview actor; supports mouse drag for yaw/pitch and scroll-wheel radius changes.
- Optional future enhancement: height-based zoom tiers that swap overlays/LODs as you zoom out.
- Schedule after core M4 deliverables or as part of UI polish in Milestone 6.

### Nice-to-Have: Heightmap Visualization Mode
- Add a dedicated preview mode that maps crust elevation to a color gradient with a legend (blue → green → tan → white) and optional subtle displacement.
- Reuses existing scalar fields; driven by min/max elevation published to the material and UI legend.
- Implemented once core LOD/validation tasks are complete.
