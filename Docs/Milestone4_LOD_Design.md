### Milestone 4 – Paper-Aligned Global LOD Design

Revision note: This document has been revised to align with the paper’s Section 4. The earlier per-plate LOD, seam stitching, morphing, and SSE-heavy design are superseded. The active design is the following simple global LOD approach. The previous content remains archived below for reference.

---

### Goals (Paper-Aligned)

- Render a single, uniform spherical mesh at one subdivision level at a time, matching the paper’s methodology.
- Adjust the global subdivision level based on camera distance to planet center (coarse when far, fine when close).
- Avoid complex seam handling and morphing by changing the LOD globally for the entire planet mesh.
- Cache a small set of global meshes (e.g., L4–L7; optionally L8 for tests) and rebuild only on demand or topology change.
- Stream global mesh builds asynchronously to eliminate hitches.

---

### Simple Global LOD Selection

We use a single subdivision level for the entire planet. Thresholds are expressed in terms of d/R (camera distance over planet radius):

- Close: d/R < 3 → L7 (≈327,680 tris)
- Medium: 3 ≤ d/R < 10 → L5 (≈20,480 tris)
- Far: d/R ≥ 10 → L4 (≈5,120 tris)

Notes:

- These thresholds are seed values consistent with the paper’s figures; we can tune later based on profiling.
- No per-plate LOD, no morphing, no seam stitching. Because the mesh is global, level switches do not introduce cracks.
- Optional mild hysteresis (e.g., 10%) around boundaries is allowed to reduce thrashing, but switches are instantaneous at the mesh level.
- Distance thresholds are seed values and will be tuned empirically; align to [CR11] when available.
- We will test at L7 first. If profiling indicates the need for more detail, evaluate L8 (≈1,310,720 tris).

Reference selector outline:

```cpp
int32 SelectGlobalLOD(double cameraDistanceMeters, double planetRadiusMeters)
{
    const double dOverR = cameraDistanceMeters / planetRadiusMeters;
    if (dOverR < 3.0) return 7;   // Close: ~327K tris
    if (dOverR < 10.0) return 5;  // Medium: ~20K tris
    return 4;                     // Far: ~5K tris
}
```

---

### Global Mesh Caching and Invalidation

Cache key: `(LODLevel, TopologyVersion, SurfaceDataVersion)`

- LODLevel: integer level (e.g., 4, 5, 6, 7).
- TopologyVersion: increments when connectivity/plate topology changes.
- SurfaceDataVersion: increments when surface/heightfield/material inputs change.

Artifacts stored per LOD:

- Global vertex buffers (positions, normals/tangents), index buffers, and any derived data needed by materials.

Invalidation:

- On topology change: invalidate all cached LOD meshes.
- On surface change: rebuild only the vertex-dependent data; index buffers can be reused if subdivision topology is unchanged.

Because there are only 3–4 global LODs in memory, eviction is trivial; keep them all resident when possible.

---

### Streaming and Swap Policy

- Build or reload the requested global LOD asynchronously (UE Task Graph / ThreadPool).
- Keep the current global mesh visible until the new LOD is fully ready.
- On completion, swap the mesh in a single frame (instant switch). No per-vertex morphing required.
- Optionally pre-warm neighboring LODs (e.g., keep L4–L6 in memory) to make switches instantaneous.

---

### Culling

- Back-face culling eliminates ~50% of triangles at planetary scale when the camera is outside the sphere.
- Frustum culling by the renderer is sufficient; no custom occlusion needed at this stage.

---

### Implementation Outline

1) Add a global LOD manager to compute `TargetLOD` from camera distance each tick (or on camera move events).
2) If `TargetLOD != CurrentLOD` and not already building, request async build of `GenerateRenderMesh(TargetLOD)`.
3) When build completes, push buffers to the global `RealtimeMeshComponent` and set `CurrentLOD = TargetLOD`.
4) Cache results keyed by `(LODLevel, TopologyVersion, SurfaceDataVersion)`.
5) Invalidate cache on topology changes; rebuild on next request.

Integration notes:

- Use a single `RealtimeMeshComponent` for the whole planet mesh and swap its buffers per LOD.
- Follow the plugin how-to for thread-safe updates to sections and RHI resources.

---

### Profiling Plan (Targeting Realism)

- Static LOD baselines: lock to L4, L5, L6 and capture tri count, FPS, and frame breakdown (Game/Render/RHI).
- Dynamic run: traverse camera path from far→medium→close and back; verify that switches are hitch-free and sustained FPS ≥ 60 at close (L6) with 20–40 plates.
- Artifacts: CSV metrics, UE Insights traces, and an automation test that runs the camera path and asserts FPS and tri budgets.

---

### Deliverables and Task Mapping (Revised)

- 4.1 Implement global distance-based LOD selector and full mesh rebuild.
- 4.2 Global mesh caching by LOD; invalidate on topology change; async streaming.
- 4.2 Enable back-face/standard frustum culling. No seam stitching or morphing.
- 4.3 Profiling harness and automation to validate FPS and tri counts along a recorded camera path.

---

### Archived: Previous Per-Plate LOD Design (Superseded)

This document defines the Level-of-Detail (LOD) system for the Planetary Creation project targeting high visual fidelity (Levels 5–7, 20,480–327,680 triangles for the full sphere) while sustaining performance across zoom levels. It focuses on deterministic, crack-free rendering tied to the existing plate topology and re-tessellation workflow.

---

### Goals

- Deliver realistic visual detail at close range (Levels 6–7) while keeping far/medium views efficient.
- Ensure deterministic results across runs and between LOD levels.
- Avoid cracks and popping at cross-LOD seams; provide smooth transitions.
- Minimize rebuild work: cache tessellated meshes by plate and LOD; invalidate only on topology change.
- Stream mesh generation on background threads to keep frame time stable.

---

### Background and Constraints

- Base topology is plate-driven with deterministic split/merge mechanics.
- Re-tessellation is full-rebuild (per current design), but we will vastly reduce rebuild frequency via caching and invalidate only when the plate topology version changes.
- Visual target levels (icosphere counts for reference):
  - L3: 1,280 tris
  - L4: 5,120 tris
  - L5: 20,480 tris
  - L6: 81,920 tris
  - L7: 327,680 tris
  Per-plate triangle budgets roughly track plate surface area fraction of the sphere.

---

### LOD Selection Strategy

We use a screen-space error (SSE) driven approach with distance fallbacks to maintain predictable budgets:

- Compute a per-plate LOD based on projected geometric error:
  - SSE ≈ K · GeometricError(LOD) / DistanceToCamera.
  - K = ViewportHeight / (2 · tan(FOV/2)).
  - GeometricError(LOD) ≈ C / 2^LOD for geodesic subdivision (C calibrated empirically on a unit radius sphere).
- Pick the lowest LOD whose SSE ≤ τ, with τ in pixels (near τ ≈ 1.5 px, far τ ≈ 3.0 px).
- Add hysteresis (10–15% of threshold) to avoid thrashing.
- For initial tuning and predictability we also define radius-relative distance bands:
  - L7 when d/R ≤ 3
  - L6 when 3 < d/R ≤ 6
  - L5 when 6 < d/R ≤ 12
  - L4 when 12 < d/R ≤ 24
  - L3 when d/R > 24
  These are seed values; the SSE check refines them per device/FOV.

Transition policy:

- Cross-fade in a morph window spanning 15% of the distance band width around the threshold.
- Use per-vertex morph from coarse-to-fine derived via deterministic subdivision correspondence (explained below).
- Gate LOD changes by hysteresis and a time-since-last-switch cooldown (e.g., 0.25s) to stabilize.

---

### Mesh Organization and Deterministic Correspondence

- Each plate owns one `RealtimeMeshComponent` LOD group. The visual LOD index maps to subdivision level.
- Subdivision scheme: geodesic/icosphere-style triangle subdivision consistent with current re-tessellation. Each parent triangle subdivides into 4 children with stable vertex ordering. This guarantees a deterministic vertex correspondence chain between levels.
- Per-plate LOD = max of that plate’s triangle patches intersecting its bounds. For simplicity in Phase 1 of LOD, we choose a single LOD per plate; future work may support per-patch LOD within large plates.
- Adjacency map: For every plate neighbor, store shared edge vertex indices at each LOD to support crack stitching and synchronized morphing.

---

### Crack-Free Cross-LOD Seams

To prevent cracks when adjacent plates render at different LODs:

1) Seam Stitching (primary):
   - Constrain the higher-LOD edge vertices to the coarse edge by projecting fine-edge vertices onto the coarse edge segment during morph windows.
   - Build stitched index buffers for the seam that collapse fine edge topology down to the coarse segment using deterministic remapping.

2) Skirts (fallback):
   - Generate short inward-facing skirt triangles along edges (depth bias + small normal offset) to hide residual sub-pixel gaps.
   - Enable skirts only when a neighbor plate’s LOD differs by ≥2 levels or while a neighbor’s mesh is still streaming.

Both methods use deterministic adjacency and shared-edge vertex order to avoid temporal artifacts.

---

### Smooth Transitions (Morphing)

- Maintain a per-vertex morph factor µ ∈ [0,1] over the transition window. Interpolate positions/normals/UVs from parent (coarser) to child (finer) derived vertices.
- Deterministic subdivision provides a unique mapping from coarse vertices and mid-edge points to fine vertices.
- Morph windows are short (100–200 ms) and eased (smoothstep) to hide pops.

---

### Caching Strategy

Cache key: `(PlateId, LOD, TopologyVersion, SurfaceDataVersion)`

- `PlateId`: stable identifier of the plate.
- `LOD`: subdivision level.
- `TopologyVersion`: increments only on split/merge or other connectivity changes.
- `SurfaceDataVersion`: increments when heightfield/material inputs change.

Artifacts stored:

- Vertex buffers (positions, normals/tangents), index buffers, adjacency seam data per neighbor, and optional morph helper data (parent-to-child maps).
- Optional compressed payload on disk for cold cache (future; Phase 4 can target memory-only cache first).

Invalidation:

- On topology change: invalidate all LOD entries for affected plates and any adjacency seam data with their neighbors.
- On surface data change without topology change: invalidate only the vertex-dependent buffers; retain index/seam buffers.

Eviction policy:

- LRU with plate- and LOD-aware budgets. Keep L5–L7 hot; evict far-view L3–L4 first.

---

### Streaming and Update Pipeline

- Build meshes on background threads (UE Task Graph or `Async(EAsyncExecution::ThreadPool, ...)`).
- Stage results in a thread-safe cache, then enqueue render-thread updates to `RealtimeMeshComponent`.
- Double-buffer per-plate LOD to avoid stalls: keep the current LOD visible until the next LOD is fully ready, then cross-fade and swap.
- Prefer batched updates to amortize RHI cost.

Realtime Mesh Integration:

- Use per-plate component with multiple LOD levels; each LOD contains one or more sections.
- Update calls must respect render thread constraints; use the plugin’s async-safe APIs and enqueue on the game/render thread as required.
- Morphing can be implemented either as CPU morph (two sections blended by weights) or single section with CPU-updated vertex positions during the morph window; choose based on measured cost.

---

### Distance/Screen-Space Thresholds and Initial Budgets

Initial device-agnostic targets (seed values):

- Far view (whole planet on screen): target ~5–10k tris total (L3–L4 equivalent). Skirts permitted if any seam disparity.
- Medium view (hemisphere): target ~60–100k tris (L5–L6 equivalent).
- Close view (regional): target ~250–350k tris (L6–L7 equivalent).

Transition thresholds (relative to radius R):

- L7 if d/R ≤ 3
- L6 if 3 < d/R ≤ 6
- L5 if 6 < d/R ≤ 12
- L4 if 12 < d/R ≤ 24
- L3 if d/R > 24

Hysteresis: ±15% around each boundary. SSE thresholds τnear ≈ 1.5 px, τfar ≈ 3.0 px, refined during profiling by calibrating C.

---

### Profiling Plan (Phase 4.3)

Two complementary passes:

1) System Overhead Baseline:
   - Lock rendering to L3 and measure CPU (LOD selection, caching, streaming) and GPU (draw overhead) to quantify LOD system tax at trivial geometry.

2) Realism Budget Pass:
   - Enable dynamic LODs targeting L6–L7 near, L5–L6 medium, L3–L4 far. Measure:
     - Frame time breakdown (Game/Render/RHI)
     - Tri counts per frame, draw call counts, vertex/memory bandwidth
     - Streaming times and cache hit/miss rates
   - Acceptance: ≥60 FPS on target hardware with 20–40 plates in view, camera path covering far→medium→near.

Artifacts: CSV traces, UE Insights captures, automation test that validates FPS and tri budgets along a recorded camera spline.

---

### Implementation Notes and Risks

- Deterministic mapping between LODs is critical; we will codify a stable vertex indexing scheme during subdivision.
- Seam stitching is preferred; skirts are a safety net during streaming or large LOD deltas.
- Per-plate uniform LOD keeps seams manageable for Phase 4; per-patch LOD is a future optimization.
- Memory budget must be enforced; keep at most two LODs per plate in memory unless in morph/transition.
- Validate transitions under extreme FOVs and editor scalings.

---

### Deliverables and Task Mapping

- 4.0 LOD design (this document) with thresholds and budgets.
- 4.1 Distance-based LOD selector with hysteresis and morphing.
- 4.2 Mesh caching keyed by plate+LOD with topology-aware invalidation; async streaming.
- 4.0 Seam handling: stitching + skirts fallback.
- 4.0 Integration with `RealtimeMeshComponent` for multi-LOD updates.
- 4.3 Profiling harness and automation test path.

---

### Appendix: Calibrating SSE

Given viewport height H and FOV, K = H / (2 · tan(FOV/2)). For an icosphere-like subdivision, GeometricError(LOD) = C / 2^LOD on a unit sphere. Choose τ (in px), solve for distance thresholds d(LOD) = K · C / (τ · 2^LOD). Calibrate C once from the measured maximum screen error of L3 at a known distance on the unit sphere; scale to planet radius.


