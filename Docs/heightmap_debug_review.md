Michael — here’s a no‑sugar‑coating, engineering‑grade review of why your exported “heightmap” doesn’t match the example and how to fix it. I’m prioritizing by severity and calling out concrete code changes.

---

## 2025-10-10 Alignment Addendum

- Run the **Stage B heartbeat diagnostic** before any exporter work each session. Confirm `[StageBDiag] Ready=1`, amplified counts match render vertices, and finite/non-zero samples appear; pause integration if any check fails.
- Deliver a Day 1 **isotropic Stage B spike** (noise blended by age/ridge distance) to prove the pipeline end-to-end while `ALG-03` finishes the sampler-driven exporter. Replace the spike with anisotropic kernels once `STG-05‒07` land.
- Keep **hydraulic erosion disabled** during the alignment phase (`STG-04`); reintroduce tuned erosion only after anisotropic amplification passes validation (`STG-08`).
- Require **inline validation artifacts** for ridge tangents, fold directions, and anisotropic kernels (population stats + Day 5 export PNG) before marking the corresponding tasks complete.
- Treat palette/doc polish as **Phase 4 parallel work.** VIS tasks should not gate the shape fix; merge them after the Stage B/ALG critical path is green.

These adjustments keep the focus on restoring Stage B fidelity quickly while preserving tight feedback loops.

---

## Executive summary (root causes — ranked)

1. **You’re splatting *vertices* into a 2 M‑pixel image and then “filling the holes” by dilation.**  
   That can’t reconstruct the surface. It produces blocky coastlines, smeared detail, and visible artifacts at seams/poles. Your exporter writes a single pixel per vertex (“nearest pixel”) and *never* rasterizes triangles or samples the surface per pixel. The big image vs. tiny vertex count (e.g., LOD5 ≈10 k verts → 2,097,152 pixels) guarantees huge gaps that dilation won’t realistically fix.

2. **Projection & seam/pole handling are fragile.**  
   You do equirectangular (lon/lat) projection, but only mirror the *outermost* columns after the fact and you don’t duplicate coverage near U≈0/1 during the write stage. That leaves visible discontinuities and filtering bleed at the dateline.

3. **Color mapping is inconsistent with the exporter header and expectations.**  
   The header says “gradient from min to max,” but you actually color using a fixed **absolute** hypsometric palette (−6 km … +6 km). Min/max are computed and then ignored. If your simulated elevations don’t span that physical range (common early in the sim), the map compresses into a narrow band of colors that won’t match the demo.

4. **Performance pathologies in the “gap fill” are masking the real problem.**  
   You run up to 50 full‑image dilation passes on a 2 k×1 k buffer every export. Even though you now reuse a scratch buffer and early‑out when nothing fills, it’s still O(W×H×passes). The right fix is to stop needing dilation by sampling the surface properly.

5. **CPU vs GPU spherical UV conventions are inconsistent.**  
   CPU exporter uses `atan2(N.Y,N.X)` / `asin(N.Z)`; your GPU path uses `atan2(z,x)` / `asin(y)`. Even if the exporter is CPU‑only, this mismatch creates verification headaches and visual flips when you compare exports to GPU previews.

6. **Architectural mismatch with the paper.**  
   The paper renders detail at ~100 m using dense sampling and amplification. Exporting an equirectangular image by vertex‑nearest splats at LOD5 (≈35–100 km vertex spacing) is fundamentally off‑spec.

---

## What to change (precise, shippable plan)

### A. Replace “vertex splat + dilation” with **pixel‑driven sampling + triangle interpolation** (Critical)

**Goal:** For every output pixel (u,v), evaluate elevation by barycentric interpolation over the *containing spherical triangle*.

**How:**
1. Loop over pixels, not vertices. Convert (u,v) → 3D unit vector → sphere point.  
   You already have `VertexToEquirectangularUV` (inverse math is trivial).
2. Find the triangle on your icosphere that contains that direction. Use your existing **SphericalKDTree** (it’s in the public API) for nearest‑vertex seed and walk adjacency to locate the containing face. The file list shows `SphericalKDTree.h` on purpose.
3. Interpolate elevation from the triangle’s three vertices (use `VertexAmplifiedElevation` when available, else base elevation).
4. Write the color once—**no dilation needed**.

**Why this matters:** It eliminates the fundamental aliasing that makes your export look nothing like the example. It also scales predictably and keeps coastlines crisp because you’re sampling the true surface, not propagating nearest points.

**Complexity:** O(W×H×logN) with a KD‑tree (or ~constant with a face lookup cache). For 2048×1024 and ~40 k–160 k faces, this is fine in editor with multithreading.

**Sanity tests to add:**
- Pixel‑vs‑CPU vertex sampling RMS error over random pixels < 0.5 m when sampling the same triangles.
- Coastline continuity across U=0/1: count color deltas between last/first columns; assert below a threshold.

### B. Fix seam & pole handling at write‑time (High)

- **Duplicate coverage near seams:** When `U < threshold` also write to `x=0`; when `U > 1−threshold` also write to `x=width−1` (for *pixel‑driven* sampling, this becomes trivial: just sample with wrapped U or set the texture addressing to wrap). Your current “mirror final column if A==0” is a band‑aid that doesn’t ensure *both* columns received semantically equivalent samples.  
- **Poles:** Clamp V exactly to [ε, 1−ε] to avoid multiple triangles mapping to a single pixel row.

You’ve already written a seam‑coverage test for the GPU preview—carry the same logic to the exporter.

### C. Offer **two color modes** and make the default explicit (Medium)

- **Hypsometric (absolute):** current behavior using `HeightmapColorPalette.h`.
- **Normalized (min→max):** use your Min/Max (which you compute and never use) to scale 0..1 and then remap to a perceptual gradient. Update the exporter docstring (it currently claims “from min to max,” which is wrong).

Why: the example image looks like a hypsometric palette; others will expect normalized range during debugging. A toggle avoids confusion and matches both use cases.

### D. Unify spherical UV conventions across CPU & GPU (Medium)

- Pick one axis convention and stick with it:
  - CPU: `atan2(Y,X)`, `asin(Z)`
  - GPU: `atan2(z,x)`, `asin(y)`

If you keep equirectangular, align both. Otherwise, consider cube‑face exports (6 faces) to eliminate polar distortion completely (fits your long‑term doc recommendations).

### E. Clean up exporter performance (Nice‑to‑have once A is done)

If you *temporarily* keep dilation while implementing A:

- Keep a single scratch buffer (you already do) and **detect convergence** (you already early‑out)—that’s good.
- But the real win is removing dilation entirely via triangle sampling (A). Your own audit flags this as a hotspot.

---

## Detailed issue list (with severity & fixes)

### Critical

1. **Vertex‑nearest rasterization (fundamental algorithmic error).**  
   **Where:** Heightmap export writes one pixel per vertex using equirectangular UV, then dilates.  
   **Why it breaks fidelity:** You’re undersampling by ≥200× at 2048×1024. Dilation can’t reconstruct high‑frequency structure (shorelines, ridges).  
   **Fix:** Pixel‑driven barycentric sampling over triangulated icosphere (see A).

2. **Seam continuity is post‑hoc and lossy.**  
   **Where:** You only copy the *outermost* col if the other is empty.  
   **Fix:** Treat U as periodic during sampling or explicitly duplicate writes within a threshold of U=0/1; verify with a seam‑coverage test (you already did this for preview).

### High

3. **Exporter claim vs reality (color mapping).**  
   **Where:** Header says “gradient from min to max,” code uses fixed hypsometric stops; min/max unused.  
   **Impact:** Expected dynamic range vs fixed physical scale mismatch → “doesn’t look like the example.”  
   **Fix:** Add a `bUseNormalizedPalette` flag; if on, normalize to [Min, Max] and lerp a perceptual gradient.

4. **CPU/GPU spherical mapping mismatch.**  
   **Where:** CPU exporter vs GPU shader use different axes in `atan2/asin`.  
   **Impact:** Export doesn’t align with GPU preview snapshots/screenshots, so comparisons are misleading.  
   **Fix:** Standardize convention; add a unit test that a known 3D direction maps to the same (U,V) on both code paths.

### Medium

5. **Ambiguity in elevation source selection.**  
   **Where:** You choose “amplified” elevation if `VertexAmplifiedElevation.Num()==RenderVertices.Num()`.  
   **Risk:** This only checks *array length*, not that the buffer is actually populated. In early steps, Stage B may be partial or stale; the test is weak.  
   **Fix:** Gate on an explicit “amplification valid” latch set when the Stage B pass finishes (you have parity/readback plumbing for GPU elsewhere). Fallback to base elevation if not latched.

6. **Many‑to‑one writes (last‑writer wins).**  
   **Where:** When multiple vertices hit the same pixel, the latter overwrites the former.  
   **Fix (if you stay vertex‑driven temporarily):** Keep a small z‑buffer or “min angular distance” buffer per pixel to keep the nearest sample; but again, move to pixel‑driven sampling.

### Low / Maintenance

7. **Doc clarity.**  
   Update the exporter docstring to reflect “hypsometric absolute palette” vs “normalized” modes to avoid the current contradiction.

8. **Projection choice.**  
   Consider exporting **cube‑face** (6× textures) as your default for analysis/ML. Your own docs recommend moving away from equirectangular because of polar stretch.

---

## Why this diverges from the paper (and why it matters)

- The paper’s workflow computes coarse tectonics and then **amplifies** to a high‑resolution relief; published examples use ~500 k samples and exemplar amplification to reach ~100 m detail in the final heightfield.  
- Your exporter is **not** sampling that high‑resolution surface. It’s exporting a sparse vertex paint and smearing it. That’s why it doesn’t resemble the reference visualization (crisp coastlines, clean ridge/plateau transitions).

---

## Suggested implementation sketch (triangle‑correct export)

1. **Build a query structure once** (per export): array of triangle planes + per‑vertex elevations; optional `SphericalKDTree` over triangle centroids (or reuse your existing vertex KD‑tree and neighbor lists).
2. **ParallelFor over rows** (or tiles):  
   - For each pixel center (u,v):
     - Wrap U into [0,1) to guarantee seam continuity.  
     - Convert to unit vector d.  
     - Find seed vertex (KD‑tree). Walk incident triangles to locate the face whose spherical barycentric coords are all ≥0 (or project to planar tangent and test).  
     - Interpolate elevation; write color using chosen palette.
3. **No dilation, no post seam mirror**; you get continuity by construction.

---

## Quick checks you can run today

- **Log unfilled‑pixel percentage** before dilation (you already do) — it will be huge with vertex splats. After switching to pixel‑sampling it should be 0% without any dilation.
- **Turn on the seam continuity test expectations for the exporter** mirroring your GPU test (counts on column 0 and last column should both be >0, and left/right colors should match within a small delta).
- **Render the same frame with GPU preview** and compare the colorized export after unifying the UV convention; the continents should align visually once CPU/GPU axes match.

---

## Concrete diffs to consider (high‑level)

- **`HeightmapExporter.cpp`**
  - Replace the vertex loop with a pixel loop; delete the dilation block.
  - Add `bUseNormalizedPalette` param (or read from service). If enabled, normalize against the computed `MinElevation/MaxElevation` you already calculate.
  - Make U periodic: `U = frac(U);` before sampling; set V clamp with a tiny epsilon.
- **`HeightmapColorPalette.h`**
  - Keep hypsometric stops (current behavior); add a normalized gradient (e.g., Cividis/Viridis) for diagnostics.
- **`ContinentalAmplification.usf` & any CPU preview**  
  - Standardize axis mapping so CPU exports and GPU previews line up.
- **`UTectonicSimulationService`**
  - Replace `bUseAmplified = (VertexAmplifiedElevation.Num() == RenderVertices.Num())` with a latched “StageBReady” flag set only after a successful amplification pass.

---

## If you want the exported image to match the attached example **now**

- Temporarily set **Normalized** color mode to stretch your current min/max to the full palette. That alone will often make the result *look* closer to the example, because early steps don’t span −6 km..+6 km. But this is a cosmetic fix; do the algorithmic change above to actually match shapes.

---

## Closing

The mismatch isn’t a small bug; it’s an algorithmic choice that can’t produce the same visual result. Move from vertex splatting to per‑pixel triangle sampling, fix the seam handling at write‑time, and make the color mode explicit. That will bring the export in line with your preview and with the paper’s intent, and the output should visually match the example you attached.

If you want, I can write a drop‑in `SampleElevationAtUV(u,v)` helper for the exporter and a minimal KD‑tree‑backed `FindContainingTriangle()` sketch to de‑risk the change.
