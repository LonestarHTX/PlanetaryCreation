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

**Decision:** Focus GPU compute efforts on Stage B amplification (the actual bottleneck at ~31 ms combined), not thermal/velocity fields (only 0.6 ms total).

**Implementation:**
- `OceanicAmplification.usf` - Perlin 3D noise, age-based fault amplitude, ridge-perpendicular transform faults
- `ContinentalAmplification.usf` - Texture2DArray exemplar sampling, terrain classification, weighted blending
- `OceanicAmplificationPreview.usf` - Equirect PF_R16F texture for WPO material visualization
- `OceanicAmplificationGPU.cpp` - RDG integration, async readback pooling, snapshot hashing

**Results:**
- Oceanic amplification: **~10.9 ms** (GPU pass) with <0.1 m CPU parity
- Continental amplification: **~19.6 ms** (GPU snapshot → CPU fallback replay) while we keep drift detection enabled
- Async readback: ~0 ms blocking cost (pooled `FRHIGPUBufferReadback`)
- Stage B elevations stream into the realtime mesh as a dedicated `StageBHeight` vertex buffer, so cached LODs and GPU preview materials consume the exact amplified heights with no CPU displacement step; the preview MID now takes the live elevation scale from simulation parameters.
- Visualization modes expanded with **Amplified Stage B** (delta heatmap) and **Amplification Blend** (plate colours tinted by Stage B deltas) so perf captures can highlight how much detail Stage B contributes without swapping materials mid-run.
- GPU parity tests: `GPUOceanicParity`, `GPUContinentalParity`, `GPUPreviewSeamMirroring`
- Paper defaults ship with Stage B/GPU/PBR enabled (LOD 5, CPU skip). Use `r.PlanetaryCreation.PaperDefaults 0` before profiling CPU-only baselines.
- Preview lighting toggle (UI + `r.PlanetaryCreation.UsePBRShading`) builds a transient lit material for CPU preview and an on-the-fly lit WPO material for GPU preview; 2025‑10‑09 parity runs logged Stage B = **11.3 ms** (Oceanic GPU) and **16.9 ms CPU + 11.5 ms cache** (Continental snapshot replay) with 100 % vertices inside ±0.10 m.

**Why Stage B Over Thermal/Velocity:**
- Stage B: **≈30.5 ms** combined (dominant share of M6 budget) → High-value GPU target
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
+ Stage B Oceanic (GPU):       10.90 ms  ✅ (compute shader pass, 0.1 m parity)
+ Stage B Continental (GPU snapshot + CPU fallback):   19.60 ms  ✅ (snapshot write, drift replay)
+ Hydraulic erosion:            8.00 ms  🔴 (planned, not implemented)
- GPU async readback:          -0.00 ms  ✅ (pooled, non-blocking)
= M6 Current Total:            38.82 ms  (target: <90ms, 57% headroom)

With hydraulic erosion:        46.82 ms  (still 48% headroom)
```

**Notes:**
- ParallelFor savings already baked into 6.32ms M5 baseline (not a separate line item)
- SIMD exploration planned -2.1ms savings not implemented (ParallelFor superseded)
- GPU thermal/velocity planned -0.45ms savings deferred (low priority)
- Stage B GPU compute is the major M6 optimization win (~31 ms across oceanic + continental)

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

**Last Updated:** 2025-10-10
