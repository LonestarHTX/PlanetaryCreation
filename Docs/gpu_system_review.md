# PlanetaryCreation GPU System Review

## Summary
The GPU preview implementation in *PlanetaryCreation* is performing extremely well. Logs indicate that GPU preview mode has cleanly taken over Stage B amplification, the CPU path is skipped, and all relevant compute stages are now running efficiently with LOD caching in effect.

---

## ✅ Current Observations

### GPU Preview
```
[GPUPreview] GPU preview mode ENABLED (CPU amplification SKIPPED)
[LOD Cache] Using cached L7: 164224 verts, 327680 tris (cache hit)
[OceanicGPUPreview] Height texture written (2048x1024, 163842 vertices)
```
- GPU preview mode is correctly enabled and bypasses CPU amplification.
- LOD caching is functional, with stable topology reuse.
- The height texture path is producing correct data for preview rendering.

### Performance Metrics
```
Step 1 | Total 185.68 ms | StageB 0.11 ms | Erosion 6.00 ms | Sediment 14.48 ms | Dampening 23.95 ms
Step 2 | Total 199.81 ms | StageB 0.12 ms | Erosion 6.37 ms | Sediment 19.00 ms | Dampening 24.53 ms
```
- Stage B is effectively free (~0.1 ms).
- Major compute costs are now in **Sediment (~14–19 ms)** and **Dampening (~24–25 ms)**.
- Stable timing and consistent LOD cache hits confirm system stability.

---

## ⚙️ Issues & Misses

1. **Vertex count mismatch**  
   - LOD cache shows 164,224 verts, preview log reports 163,842 processed.
   - Likely causes: seam/pole omission, equirectangular raster mismatch, or masking of continental vertices.
   - Fix: reconcile counts or clarify logging terminology (e.g., “pixels written”).

2. **Equirectangular distortion**  
   - 2048x1024 maps stretch near poles and cause seams at 180°.
   - Recommendation: move to cube-sphere faces (6 textures) for uniform sampling.

3. **Normals not matching displaced geometry**  
   - Displaced surface via GPU heightmap may misalign with CPU normals.
   - Solution: compute normals in material (finite-diff) or via a GPU post-pass.

4. **Texture wrap / seam handling**  
   - Add a duplicated 1-pixel border or set `AddressMode=Clamp` to prevent MIP bleeding at 180°.

---

## 🚀 Next Steps (Ranked by Impact)

### 1. Optimize Sediment and Dampening (Hot Path)

**CPU Phase (fastest iteration)**
- Switch to **SoA layout** and **ParallelFor**.
- Use CSR-style neighbor lists: `NeighborsIndex`, `NeighborsStart`, `NeighborsCount`.
- Two-pass pattern: compute outflows (no atomics) then gather inflows.

**GPU Phase (future optimization)**
- Port Sediment/Dampening to compute shaders using same CSR structure.
- Maintain async readback via per-job `FRenderCommandFence`.

### 2. Ridge Direction Caching
- Cache by `TopologyStamp` (LOD + plate + boundary versions).
- Rebuild only when stamp changes; refresh a thin boundary ring otherwise.
- Add test: recompute ridges after 10 steps, assert RMS angular error < 2°.

### 3. Retessellation Hysteresis
- Check drift every 5 steps.
- Trigger at >45° with bad_tri_ratio >2%, lockout until <30°.
- Log frequency and wall time for data-driven tuning.

### 4. Cube-Face Preview
- Convert preview to 6 PF_R16F face textures.
- Removes pole stretch, simplifies normal calc, prepares for adaptive LOD.

---

## 🧠 Code Patterns

**CSR Neighbor Storage**
```cpp
TArray<int32> NeighborsIndex;
TArray<int32> NeighborsStart;
TArray<uint8> NeighborsCount; // <= 6
```

**Two-Pass Gather Example**
```cpp
// Pass A: Outflows
ParallelFor(N, [&](int32 v){ ComputeOutflows(v); });
// Pass B: Inflows
ParallelFor(N, [&](int32 v){ GatherInflows(v); });
```

---

## 🧪 Validation & Tests
- **Preview parity**: compare GPU vs CPU elevation samples (<1e-3 tolerance).
- **Continents unchanged**: assert non-oceanic vertices unchanged.
- **Timing regression**: fail test if Erosion/Sediment/Dampening >2x baseline.

---

## 🗺️ Long-Term Roadmap
- Implement **render-only adaptive LOD** (CDLOD cube-sphere tiles).
- Integrate **Nanite displacement** for 1 m surface detail.
- Combine GPU displacement preview + adaptive LOD for real-time fidelity.

---

## ✅ Summary for Team
> GPU Preview is stable. Stage B cost is down to ~0.12 ms at L7 with consistent cache hits.  
> Key issues: vertex-count mismatch, equirect seams, and normal alignment.  
> Next: focus on Sediment/Dampening optimization, ridge caching, re-tess hysteresis, and cube-face preview.  
> Goal: push step time under 100 ms at L7, then progress to adaptive LOD + Nanite displacement.