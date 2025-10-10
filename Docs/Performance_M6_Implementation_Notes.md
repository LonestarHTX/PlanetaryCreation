# Milestone 6 Performance Implementation Notes

**Date:** October 2025
**Purpose:** Clarify actual performance optimizations implemented vs exploration documents

---

## What Was Actually Implemented

### ✅ Multi-Threading with ParallelFor (Supersedes SIMD)

**Decision:** Instead of manual SIMD vectorization (AVX2/NEON intrinsics), we implemented Unreal's built-in `ParallelFor` across performance-critical paths.

**Implementation Locations:**
- `SedimentTransport.cpp:82` - Multi-threaded diffusion passes (6 iterations normal, 4 when GPU preview active)
- `OceanicDampening.cpp:70` - Parallel vertex smoothing with cached adjacency weights
- `TectonicSimulationController.cpp:1256, 2004` - Parallel mesh vertex processing

**Results:**
- M5 baseline: **6.32ms** at LOD Level 3 (vs 110ms target = 17× headroom)
- M5 feature overhead: **1.49ms** (vs 14ms budgeted = 89% under budget)
- Sediment transport: 0.30ms
- Oceanic dampening: 0.32ms
- Erosion: 0.34ms

**Why ParallelFor Over SIMD:**
1. **Better scalability:** 4-16 CPU cores (8-32× parallelism) vs SIMD 2-8 lanes
2. **Platform agnostic:** No AVX2/SSE4.2/NEON conditional compilation
3. **Cache friendly:** Task-based work stealing optimizes cache utilization automatically
4. **Maintainable:** Standard Unreal pattern, no intrinsics debugging
5. **Proven results:** Achieved 17× performance headroom without complexity

---

### ✅ GPU Compute Shaders (Stage B Amplification)

**Decision:** Focus GPU compute efforts on Stage B amplification (the actual bottleneck at ~33 ms combined), not thermal/velocity fields (only 0.6 ms total).

**Implementation:**
- `OceanicAmplification.usf` - Perlin 3D noise, age-based fault amplitude, ridge-perpendicular transform faults
- `ContinentalAmplification.usf` - Texture2DArray exemplar sampling, terrain classification, weighted blending
- `OceanicAmplificationPreview.usf` - Equirect PF_R16F texture for WPO material visualization
- `OceanicAmplificationGPU.cpp` - RDG integration, async readback pooling, snapshot hashing

**Results:**
- Oceanic amplification: **~8.2 ms** GPU in steady-state (0 ms readback; when the suite runs solo it reports ~1.7 ms because the shared displacement buffer already holds the result)
- Continental amplification: **~22–23 ms** GPU replay with **≈2–3 ms** CPU overhead once the snapshot hash stabilises (first warm-up step still pays ≈26 ms CPU + 27 ms GPU before the cache hits)
- Async readback: ~0 ms blocking cost (pooled `FRHIGPUBufferReadback`)
- Stage B elevations stream into the realtime mesh as a dedicated `StageBHeight` vertex buffer, so cached LODs and GPU preview materials consume the exact amplified heights with no CPU displacement step; the preview MID now takes the live elevation scale from simulation parameters.
- Visualization modes expanded with **Amplified Stage B** (delta heatmap) and **Amplification Blend** (plate colours tinted by Stage B deltas) so perf captures can highlight how much detail Stage B contributes without swapping materials mid-run.
- GPU parity tests: `GPUOceanicParity`, `GPUContinentalParity`, `GPUPreviewSeamMirroring`
- Paper defaults ship with Stage B/GPU/PBR enabled (LOD 5, CPU skip). Use `r.PlanetaryCreation.PaperDefaults 0` before profiling CPU-only baselines.
- Preview lighting toggle (UI + `r.PlanetaryCreation.UsePBRShading`) builds a transient lit material for CPU preview and an on-the-fly lit WPO material for GPU preview; 2025‑10‑10 parity runs logged Stage B steady-state at **~33–34 ms** (Oceanic GPU ≈8 ms, Continental GPU ≈23 ms, Continental CPU ≈3 ms) with 100 % vertices inside ±0.10 m. The parity harness still replays the CPU/cache fallback once (~44 ms) for drift validation.

**Why Stage B Over Thermal/Velocity:**
- Stage B: **≈33–34 ms** steady-state (dominant share of M6 budget) → High-value GPU target
- Thermal/Velocity: **0.6ms** (1.2% of M6 budget) → Low ROI
- GPU transfer overhead would likely exceed savings for small fields
- CPU thermal/velocity already well-optimized

---

## What Was NOT Implemented (Exploration Only)

### ⏸️ SIMD Vectorization (AVX2/NEON intrinsics)

**Status:** Exploration documents exist (`simd_gpu_implementation_review.md`) but feature was NOT implemented.

**Why Deferred:**
- ParallelFor achieved performance targets (17× headroom) without SIMD complexity
- SIMD would require:
  - Platform-specific code paths (AVX2 x64, NEON ARM, SSE fallback)
  - SoA (Structure of Arrays) data layout refactor
  - Intrinsics debugging and maintenance burden
  - Determinism validation across SIMD widths
- Estimated gains (2.1ms from Voronoi + stress) not needed given current 6.32ms baseline
- Can revisit if Level 7/8 profiling reveals CPU bottlenecks

**Documents:**
- `Docs/simd_gpu_implementation_review.md` - Planning/exploration
- `Docs/planetary_creation_simd_gpu_implementation_review_oct_6_2025.md` - Planning/exploration
- These documents describe **what we considered**, not what we built

---

### ✅ Hydraulic Erosion CPU Optimization (Topological Queue)

**Decision:** Replace O(N log N) per-step elevation sorting with O(N) topological queue using Kahn's algorithm for flow accumulation.

**Implementation:**
- `HydraulicErosion.cpp:146-201` - Topological traversal with upstream counting and cycle detection
- Persistent buffers: `HydraulicUpstreamCount`, `HydraulicProcessingQueue`, `HydraulicFlowAccumulation`
- Kahn's algorithm: Seed queue with vertices having no upstream contributors, propagate flow in dependency order

**Results @ LOD 7 (163,842 vertices):**

| Metric | Pre-Optimization<br>(Elevation Sort) | Post-Optimization<br>(Topological Queue) | Improvement |
|--------|--------------------------------------|------------------------------------------|-------------|
| **Average Time** | 18.29 ms | 1.65 ms | **90.98% faster** |
| **Min Time** | 17.77 ms | 1.60 ms | 11.11× speedup |
| **Max Time** | 18.77 ms | 1.69 ms | 11.10× speedup |
| **Algorithm Complexity** | O(N log N) | O(N) | Linear scaling ✅ |

**Data Sources:**
- **Pre-optimization:** `PlanetaryCreation-backup-2025.10.09-22.38.32.log` (Steps 1,2,4,8,10)
- **Post-optimization:** `PlanetaryCreation-backup-2025.10.09-23.08.44.log` (Steps 1-8)

**Key Implementation Changes:**
1. **Upstream counting** (Lines 146-154): Count incoming edges in O(N)
2. **Source seeding** (Lines 156-162): Queue vertices with no upstream contributors
3. **Topological traversal** (Lines 164-193): Process vertices in dependency order
4. **Cycle detection** (Lines 195-201): Validate complete graph coverage

**Validation:**
- ✅ Mass conservation: Erosion/deposition totals match within floating-point tolerance
- ✅ Flow accumulation: Matches previous elevation-sorted results (parity tests pass)
- ✅ Determinism: Same vertex order given same input elevations
- ✅ Automation: `HydraulicRouting` and `HydraulicErosionCoupling` tests green

**Why This Optimization:**
The 16.6ms savings brings hydraulic erosion **well under the 8ms M6 budget target**, eliminating the need for GPU compute shader port (which was the original plan). The CPU path is now fast enough for real-time preview at LOD 7 with <2ms overhead.

**Note:** Intermediate logs at `22.50.27` showed ~15.6ms because they ran on the **old binary** before the rebuild completed. Always verify binary timestamp vs log timestamp when profiling post-build.

---

### ⏸️ GPU Thermal/Velocity Field Compute Shaders

**Status:** Planned in M6 Task 4.2 but deferred as low-priority.

**Why Deferred:**
- CPU implementation already fast: Thermal 0.2ms + Velocity 0.4ms = **0.6ms total**
- Represents only **1.2%** of M6 performance budget
- GPU transfer overhead (upload plate data, download fields) would likely exceed savings
- Not a bottleneck - other areas (Stage B, hydraulic erosion) have higher ROI
- Can implement if visualization-only GPU path becomes useful (no readback needed)

---

## M6 Actual Performance Budget (October 2025)

```
M5 Baseline (L3):               6.32 ms  ✅ (includes ParallelFor)
+ Terrane extraction/tracking:  2.00 ms  (amortized, rare events)
+ Stage B Oceanic (GPU steady-state):        8.20 ms  🟢 (compute shader pass, shared buffer keeps readback at 0 ms)
+ Stage B Continental (GPU replay + CPU assist):   25.00 ms  🟢 (GPU 22.6 ms + CPU 2.4 ms once the snapshot hash matches; warm-up step hits ~65 ms before stabilising)
+ Hydraulic erosion:            1.70 ms  🟢 (topological queue, was 18.3 ms with sorting)
- GPU async readback:          -0.00 ms  ✅ (pooled, non-blocking)
= M6 Total (with hydraulic):   43.22 ms  (target: <90ms, ~52% headroom)
```

**Notes:**
- ParallelFor savings already baked into 6.32ms M5 baseline (not a separate line item)
- SIMD exploration planned -2.1ms savings not implemented (ParallelFor superseded)
- GPU thermal/velocity planned -0.45ms savings deferred (low priority)
- Stage B GPU compute is the major M6 optimization win (~33 ms across oceanic + continental in steady-state)

---

## Key Takeaways for Onboarding

1. **When you see SIMD docs:** These are exploration/planning, not implemented features
2. **Actual optimization:** Multi-threading via `ParallelFor` (simple, effective, maintainable)
3. **GPU focus:** Stage B amplification (high value), not thermal/velocity (low value)
4. **Performance status:** 6.32ms at L3 = 17× under budget, M6 on track
5. **Future work:** SIMD can be revisited if L7/8 profiling shows CPU bottlenecks

---

## References

**Implemented Code:**
- `Source/PlanetaryCreationEditor/Private/SedimentTransport.cpp`
- `Source/PlanetaryCreationEditor/Private/OceanicDampening.cpp`
- `Source/PlanetaryCreationEditor/Private/HydraulicErosion.cpp` - Topological queue optimization
- `Source/PlanetaryCreationEditor/Private/TectonicSimulationController.cpp`
- `Source/PlanetaryCreationEditor/Shaders/Private/OceanicAmplification.usf`
- `Source/PlanetaryCreationEditor/Shaders/Private/ContinentalAmplification.usf`
- `Source/PlanetaryCreationEditor/Private/OceanicAmplificationGPU.cpp`

**Exploration Docs (NOT Implemented):**
- `Docs/simd_gpu_implementation_review.md`
- `Docs/planetary_creation_simd_gpu_implementation_review_oct_6_2025.md`

**Performance Reports:**
- `Docs/Performance_M5.md` - 6.32ms baseline measurements
- `Docs/Performance_M6.md` - Stage B profiling
- `Docs/GPU_Test_Suite.md` - GPU parity validation

---

**Last Updated:** 2025-10-09
