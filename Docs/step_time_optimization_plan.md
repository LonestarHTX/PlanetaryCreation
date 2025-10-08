# Step-Time Optimization Plan

## Overview
Now that the simulation pipeline is stable, the next step is reducing **step-time** to make the tectonic and GPU passes more efficient. This document outlines the next optimization targets and implementation plans.

---

## 1. Stop Recomputing Ridge Directions Every Step

### Objective
Cache ridge directions to eliminate redundant calculations in steady-state steps. Only refresh when topological changes occur (after re-tessellation or plate reassignment).

### Implementation
- **Status (2025-10-07):** Mainline now routes Stage B through `RefreshRidgeDirectionsIfNeeded()`, which checks topology/dirty state before calling `ComputeRidgeDirections()` and feeds render-space tangents from `RenderVertexBoundaryCache`.
- **Dirty Ring Update (Next):** Track a small ring of vertices near active boundaries using a bitset. Recompute ridge directions for those vertices only, allowing localized kinematic adjustments without full cache rebuild.
- **Parity Test:** Add an automation test that recomputes ridge directions after 10 steps without re-tessellation and verifies RMS angular error < 2°.

### Expected Impact
- Removes one of the heaviest loops in the steady-state path.
- Maintains fidelity by localizing updates to dynamic regions only.

---

## 2. Throttle Re-Tessellation Frequency

### Objective
Reduce unnecessary mesh rebuilds by introducing hysteresis and smarter triggers.

### Implementation
- **Check Cadence:** Evaluate drift every 5–10 steps (configurable) instead of every step.
- **Hysteresis:**
  - Re-tessellate when `max_drift > 45°` and `bad_tri_ratio > 2%`.
  - Prevent further re-tessellation until `max_drift < 30°`.
- **Quality-Aware Trigger:** Use both drift and triangle quality metrics to determine rebuild necessity.
- **Performance Metrics:**
  - Log % of steps that trigger re-tessellation.
  - Measure `avg re-tess time` and `avg steady-state step time`.
  - Use `TRACE_CPUPROFILER_EVENT_SCOPE("ReTess")` for Insights profiling.

### Expected Impact
- Smoother performance with fewer mesh rebuild spikes.
- Step-time variance significantly reduced.

---

## 3. Keep GPU Results On-Device for Visualization Only

### Objective
Eliminate costly GPU readbacks by rendering directly from GPU buffers.

### Implementation Options
#### Option A – Height Texture + WPO Material
- Write the final elevation field to a **PF_R16F equirectangular FRDGTexture**.
- Bind texture to the planet’s **World Position Offset (WPO)** material using precomputed UVs.
- Recompute normals in the material (via finite differences) or reuse CPU normals if acceptable.

**Pros:** Simple integration; no plugin changes.  
**Cons:** Requires texture quantization and seam handling.

#### Option B – GPU Buffer → Vertex Factory
- Retain compute output in a `StructuredBuffer<float>` SRV.
- Extend RMC or a custom vertex factory to sample displacement directly in the vertex shader.
- Optional: Add a second GPU pass for normals.

**Pros:** Bit-exact to CPU results; no seams.  
**Cons:** Requires custom vertex factory integration.

### Implementation Notes
- Label this as **Preview Mode**—collision and picking remain CPU-side.
- Ensure `FRHIGPUBufferReadback` is no longer called during visualization.

### Expected Impact
- Removes the readback stall from the step loop.
- Achieves real-time planetary updates directly on GPU.

---

## 4. Stage B Steady-State Mesh Refresh

### Objective
Avoid rebuilding realtime-mesh sections when topology is unchanged so Stage B updates stay in the sub-10 ms window even under continuous playback.

### Implementation
- **Status (2025-10-07):** `UTectonicSimulationController` now pushes positions/tangents/colors in place when the render topology stamp matches, reusing the shared post-update path and bypassing the expensive rebuild.
- **Next:** Instrument Insights markers around the in-place path vs. full rebuild to quantify savings at L6/L7 and surface the delta in `Docs/Performance_M6.md`.
- **Parity Test:** Extend the GPU parity suite to cover the in-place refresh path (toggle amplification twice without triggering a topology change) and confirm render buffers stay in sync.

### Expected Impact
- Removes redundant mesh rebuilds during steady-state Stage B playback.
- Keeps the render thread budget stable once amplification settles, tightening variance for automation captures.

---

## 5. Suggested Order of Implementation
1. Implement **Re-Tessellation Throttling** (immediate, low risk).
2. Add **Ridge Direction Caching** with topology stamping.
3. Develop **GPU-Only Preview Path** (WPO method first, then vertex factory approach).

---

## 6. Risks and Guardrails
- **Ridge Cache Drift:** Regular parity tests ensure scientific accuracy.
- **Visual Mismatch:** When switching to WPO preview, verify shading parity with CPU normals.
- **Async GPU Sync:** Continue using per-job `FRenderCommandFence` to ensure render-thread submission without blocking the GPU.

---

## 7. Summary for Agents
- **Step-Time Focus Areas:** Ridge caching, re-tessellation throttling, GPU preview.
- **Acceptance Criteria:**
  - Ridge cache reduces per-step cost by ≥20%.
  - Re-tess frequency <10% of total steps.
  - Preview mode eliminates readback and maintains <0.1 m elevation delta at L7.

Once these optimizations land, we can benchmark new steady-state step times and begin fine-tuning GPU amplification parameters for even tighter frame budgets.
