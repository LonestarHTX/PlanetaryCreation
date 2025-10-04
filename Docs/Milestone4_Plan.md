# Milestone 4: Dynamic Tectonics & Visual Fidelity

## Overview
Translate the Milestone 3 scaffolding into believable tectonic activity by enabling plate re-topology, hotspot/rift events, and responsive visualization. This milestone focuses on dynamic geometry updates, higher-fidelity boundary rendering, and Level of Detail (LOD) support so the experience can scale to production-quality planets.

## Goals (Aligned with Procedural Tectonic Planets Paper)
1. Activate plate lifecycle events (rift creation, hotspot-driven growth, subduction recycling).
2. Implement dynamic re-tessellation to keep render meshes aligned with drifting plate boundaries.
3. Add tectonic activity cues (volcanic hotspots, rift valleys, transform shear) with gameplay-friendly analytics.
4. Introduce multi-tier LOD and streaming so dense meshes render efficiently.
5. Expand automated validation with comparative renders and extended stress/temperature metrics.

## Scope Constraints
- **In Scope:** Hotspot spawning, boundary state machines, dynamic mesh rebuilds, LOD tiers, shader-driven overlays, CSV + screenshot validation.
- **Out of Scope:** Full mantle convection, long-term climate coupling, multiplayer sync, GPU-based stress solver, cinematic materials.
- **Deferred:** Non-spherical planet shapes, ocean simulation, erosion/weathering passes.

---

## Phase 1: Dynamic Plate Topology

### Task 1.1: Re-tessellation Engine
**Owner:** Simulation Engineer  
**Effort:** 4-5 days  
**Description:**
- Replace the M3 warning-only stub with actionable re-tessellation.
- When centroid drift > threshold, split render mesh edges along the boundary and rebuild `SharedVertices` + render buffers.
- Preserve determinism via seed-stable edge processing.
- Cache boundary adjacency so repeated rebuilds reuse shared work.
- Support incremental rebuild (only plates crossing the threshold).

**Acceptance Criteria:**
- Drifted plates trigger automatic mesh rebuild within one simulation step.
- Plate counts remain constant unless a split/merge is requested.
- Automation test verifies re-tessellation occurs when drift > threshold and remains stable otherwise.

### Task 1.2: Plate Split & Merge Pipeline
**Owner:** Simulation Engineer  
**Effort:** 3 days  
**Description:**
- Implement rules from the paper for rift-induced splitting and subduction merging.
- Preserve plate metadata (velocity, stress history) across operations.
- Emit events for UI/logging/analytics when topology changes.

**Acceptance Criteria:**
- Rifts can split a plate into two children with deterministic ID assignment.
- Subduction merges collapse minor plates and redistribute vertices.
- Tests confirm conservation of total area within tolerance.

### Task 1.3: Boundary State Machine
**Owner:** Simulation Engineer  
**Effort:** 2 days  
**Description:**
- Track boundary lifecycle (nascent, active, dormant) with thresholds based on relative velocity & stress.
- Drive downstream visuals (volcanic plume intensity, trench depth).

**Acceptance Criteria:**
- Boundaries transition correctly according to thresholds.
- CSV export includes boundary states and timestamps.

---

## Phase 2: Hotspots, Rifts, and Thermal Fields

### Task 2.1: Hotspot Generation & Drift
**Owner:** Simulation Engineer  
**Effort:** 3 days  
**Description:**
- Sample hotspot seeds from mantle plume distribution.
- Allow hotspots to migrate with underlying mantle conveyor (paper Section 4).
- Couple hotspot heat to nearby plate vertices and elevate stress accordingly.

**Acceptance Criteria:**
- Hotspot count configurable; default matches paper (3 major, 5 minor).
- Heat field influences stress accumulation and elevation displacement.
- Tests validate deterministic hotspot placement per seed.

### Task 2.2: Rift Propagation Model
**Owner:** Simulation Engineer  
**Effort:** 3 days  
**Description:**
- Trigger rifts when divergent boundaries exceed velocity threshold.
- Animate rift widening, spawn new crust along ridge.
- Interface with re-tessellation to split plates when rifts extend past critical angle.

**Acceptance Criteria:**
- Divergent boundaries with sustained separation spawn rift events.
- Rift width grows over time and impacts vertex displacement/elevation.
- Test ensures rifts do not trigger under convergent/transform conditions.

### Task 2.3: Thermal & Stress Coupling
**Owner:** Simulation Engineer  
**Effort:** 2 days  
**Description:**
- Add simplified thermal field decaying with distance from hotspots and subduction zones.
- Feed temperature into stress/elevation modifiers (e.g., hotter crust = softer, lower elevation).

**Acceptance Criteria:**
- Temperature recorded per render vertex and exported to CSV.
- Thermal field influences elevation as described in documentation.
- Automated test checks temperature falloff curve.

---

## Phase 3: Visualization & UX Enhancements

### Task 3.1: High-Resolution Boundary Overlay
**Owner:** Rendering Engineer  
**Effort:** 3 days  
**Description:**
- Upgrade overlay to follow render-level plate seams by tracing triangle edges where plate IDs change.
- Generate polylines (or ribbon mesh) with vertex density matching render mesh.
- Fade color based on boundary state (Task 1.3) and stress magnitude.

**Acceptance Criteria:**
- Overlay aligns within one vertex of visual seam at all subdivision levels.
- Boundary colors and widths respond to state & stress thresholds.
- Test compares sampled overlay points against plate ID transitions.

### Task 3.2: Velocity Vector Field (Deferred Task from M3)
**Owner:** Rendering Engineer  
**Effort:** 1 day  
**Description:**
- Render velocity arrows at centroids with magnitude-based scaling and optional screen-space fade.
- Integrate with UI toggle and legend.

**Acceptance Criteria:**
- Arrows display correct direction/magnitude and toggle without hitching.
- Automated screenshot validation checks arrow presence when enabled.

### Task 3.3: Material & Lighting Pass
**Owner:** Rendering Engineer  
**Effort:** 2 days  
**Description:**
- Introduce physically-inspired material layering: crust, oceanic, volcanic.
- Use material parameter collections to feed stress/temperature into emissive/displacement.
- Add night/day preview (directional light orbit toggle).

**Acceptance Criteria:**
- Three material variants selectable in UI.
- Hotspots emit glow proportional to temperature.
- Lighting toggle updates without full mesh rebuild.

---

## Phase 4: Level of Detail & Performance

### Task 4.1: Multi-Tier LOD System
**Owner:** Rendering Engineer  
**Effort:** 4 days  
**Description:**
- Implement three LOD tiers (High, Medium, Low) tied to render subdivision levels.
- Seamlessly transition between LODs based on camera distance and performance budget.
- Maintain plate ID/color continuity during transitions.

**Acceptance Criteria:**
- LOD switches without popping; hysteresis prevents rapid oscillation.
- Performance at Low LOD < 16ms/frame on reference hardware (level 2 mesh).
- Tests verify LOD selection logic given distance inputs.

### Task 4.2: Streaming & Caching
**Owner:** Rendering Engineer  
**Effort:** 2 days  
**Description:**
- Cache precomputed LOD meshes and share buffers across frames.
- Stream textures/material parameters asynchronously to avoid frame spikes.

**Acceptance Criteria:**
- Cache hits logged when revisiting previously generated LODs.
- No blocking loads during camera transitions.

### Task 4.3: Profiling & Optimization Pass
**Owner:** Performance Engineer  
**Effort:** 3 days  
**Description:**
- Re-run Insights captures post-M4 features.
- Optimize hot loops (Voronoi update, stress interpolation) identified in M3 report.
- Document CPU/GPU metrics for each LOD.

**Acceptance Criteria:**
- Level 3 step time pulled back under 90ms despite new features.
- Report updated in `Docs/Performance_M4.md` with before/after comparisons.

---

## Phase 5: Validation & Documentation

### Task 5.1: Expanded Automation Suite
**Owner:** QA Engineer  
**Effort:** 3 days  
**Description:**
- Add tests for hotspot determinism, plate splitting, boundary state transitions, and LOD integrity.
- Integrate screenshot-based validation (new baseline images).

**Acceptance Criteria:**
- 6 new M4 tests added; all pass on CI.
- Screenshot diff tooling flags deviations >5% pixel variance.

### Task 5.2: Visual Comparison & Paper Parity
**Owner:** QA Engineer  
**Effort:** 2 days  
**Description:**
- Recreate paper figures showing hotspots, rifts, and transform boundaries.
- Document alignment/deviations in `Docs/Validation_M4/README.md`.

**Acceptance Criteria:**
- Updated gallery with before/after images.
- Deviations explained and approved by simulation lead.

### Task 5.3: Documentation & Release Notes
**Owner:** Technical Writer  
**Effort:** 2 days  
**Description:**
- Summarize M4 in `Docs/Milestone4_Features.md` and update `CLAUDE.md` architecture notes.
- Draft release notes, including known limitations (e.g., lack of erosion, climate coupling).

**Acceptance Criteria:**
- Documentation reviewed and merged.
- Release notes include verification checklist and profiling results.

---

## Acceptance Criteria (Milestone Level)
- Re-tessellation automatically corrects boundary drift; plate area conserved within 1%.
- Hotspots/rifts influence stress, elevation, and visualization overlays.
- High-resolution boundary overlay aligns with render seam.
- LOD system hits performance targets and transitions gracefully.
- All M4 automation tests green; screenshot comparisons approved.
- Documentation updated; CSV exports include new thermal/boundary state metrics.

---

## Risk Register
| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Re-tessellation introduces instability | Medium | High | Start with small drift thresholds, add rollback if rebuild fails |
| Plate split/merge causes determinism loss | Medium | Medium | Seed-based ordering, regression tests |
| High-res overlay expensive at level 6 | Medium | Medium | Allow downsampled overlay option; profile early |
| Hotspot heat overwhelms elevation | Low | Medium | Clamp temperature contributions; expose tuning parameters |
| LOD transitions visible seams | Medium | High | Blend vertex colors across seams; add hysteresis |

---

## Estimated Timeline
- **Phase 1:** 2 weeks (re-tessellation + split/merge + state machine)
- **Phase 2:** 2 weeks (hotspots, rifts, thermal coupling)
- **Phase 3:** 1.5 weeks (overlay, vectors, materials)
- **Phase 4:** 2 weeks (LOD system, streaming, optimization)
- **Phase 5:** 1 week (validation, docs)

Total: ~6.5 weeks (buffer included for integration & QA).

