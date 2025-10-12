# Make the shape match the paper — concrete changes

## 0) Pipeline order (one-line change)
**Order must be:** Stage A (coarse crust) → Stage B (amplification) → light erosion/dampening → preview/export.  
If erosion runs before amplification anywhere, **move it after**.
- Runtime safeguard (`[StageOrder]`) now logs and blocks hydraulic/dampening passes if they trigger before Stage B completes; keep the warnings clean when touching the loop.

---

## 1) Ridge tangent cache at **render-vertex** granularity (oceanic shape driver)
**Goal:** every ridge-side render vertex has a stable tangent `r̂` aligned **along** the ridge.

- **Add:** `TArray<FVector3f> RidgeTangent; // size = RenderVertices.Num()`
- **Where:** `TectonicSimulationService` (same module as your existing render-vertex fields).
- **Compute:**
  1. Build a list of ridge edges (divergent boundaries) at render resolution.
  2. For each render vertex on a ridge, estimate tangent by averaging incident ridge-edge directions (projected to tangent plane).
  3. Smooth with 1–2 neighborhood rings (Laplace or bilateral on sphere).
  4. Normalize; store in `RidgeTangent[i]`.  
- **Refuse fallback:** delete/disable the “age gradient” fallback near ridges. If `r̂` missing in a ridge mask, **assert** (that’s a shape bug).

**Definition of done:** `%MissingRidgeTangent == 0` within ridge mask.

---

## 2) Fold direction & orogeny class at render-vertex (continental belts)
**Goal:** every convergent continental vertex has fold direction `f̂` and class `σ ∈ {Himalayan, Andean, Old}`.

- **Add:**  
  - `TArray<FVector3f> FoldDir;`
  - `TArray<uint8> OrogenyClass; // 0=Old,1=Andean,2=Himalayan`
- **Compute:**  
  - `f̂` = principal axis of compressive strain / convergence; take tangential component and normalize.  
  - Classify `σ` via magnitude of convergence, crustal thickness, and distance to boundary (thresholds you already log—turn into a simple 3-way decision).
- **Smooth:** 2–3 ring smoothing to avoid noisy belt zig-zag.

**Definition of done:** In continental collision zones, `|f̂|≈1` and class is stable across belt widths (no salt-and-pepper).

---

## 3) Anisotropic amplification kernels (the actual “mold”)
**Oceanic amplification** (banded abyssal hills aligned to ridge):
```cpp
float OceanicAmp(const X& p) {
  float age = Age[p];
  float dr  = DistToRidge[p];         // signed/unsigned ok; use unsigned for decay
  float alongλ = Lerp(5e3f, 12e3f, clamp(age/A0,0,1));  // meters
  float crossλ = 800.0f;                                  // meters
  Mat2 S = Stretch(RidgeTangent[p], alongλ, crossλ);      // build local 2D frame on sphere
  float bands = fbm(Noise(S * LocalCoords(p)), 4);        // oriented FBM
  float G = Gaussian(dr, Sigma=15e3f);                    // strongest near ridge
  float A = Amp0 * G * (1 - smoothstep(0,A0,age));        // youngest = strongest
  return A * bands;                                       // ±300 m typical
}
```

**Continental amplification** (oriented ridges aligned to fold direction):
```cpp
float ContinentalAmp(const X& p) {
  Params P = ParamsFor(OrogenyClass[p]); // {\u03bb_along, \u03bb_cross, Amp, Rough}
  Mat2 S = Stretch(FoldDir[p], P.\u03bb_along, P.\u03bb_cross);
  float chains = RidgeChainFBM(Noise(S * LocalCoords(p)), P.Rough, /*ridge_bias=*/high);
  return P.Amp * RidgeEnvelope(chains);
}
```

**Blend:** use soft masks from signed distance to boundaries and plate types:
```cpp
float h_amp = mix( OceanicAmp(p), ContinentalAmp(p), ContinentMask[p] );
Height[p]  += h_amp;
```

**Definition of done (visual/metrics):**
- Mid-ocean transect: band spacing increases with age; amplitude decays with distance.
- Continental belts: long, continuous ridge chains; gradient mostly ⟂ `f̂`.

---

## 4) Sampling surface **where you render/export**
This is about shape fidelity at the consumer. No color, no dilation.

- **Replace “nearest-vertex + dilation” everywhere** with **triangle interpolation** at the sampling resolution (pixels for export; vertices for mesh).
- **Add helper:**  
  - `bool FindContainingTriangle(dir3 d, FaceID& outFace)`
  - `float SampleElevation(dir3 d)` → barycentric interp of the 3 face vertices (post-amplification).
- **Acceleration:** seed with your vertex KD-tree; walk adjacency to face hit.

**Definition of done:** 0% “empty” samples; coastlines are crisp without any post-process.

---

## 5) Coordinate convention unification (preview↔export)
- Pick one mapping for lon/lat (`atan2(y,x)` / `asin(z)` or your GPU version) and use it *everywhere*.
- Add a tiny test: known unit vectors → same (u,v) on CPU and GPU.

**Definition of done:** “Preview vs Export” overlay lines up to within one pixel at 2k width.

---

## 6) Light erosion **after** amplification (just shape polish)
- 1–2 thermal or shallow hydraulic passes with conservative params.
- Don’t destroy anisotropy; kernel radii ≪ wavelengths used above.

**Definition of done:** High-frequency stair-steps gone; belts and bands persist.

---

# Tests (shape-only, fast to run)

1. **Ridge transect:** sample a great-circle crossing a MOR.  
   - Expect: central crest, then banded relief with increasing wavelength vs age; amplitude decays to ~0 by ~100–150 km.
2. **Belt alignment:** in continental belts, `mean(|dot(tangent_of_ridges, FoldDir)|) > 0.8`.  
   (Approximate tangent via structure tensor on height.)
3. **Seam continuity:** mean absolute height difference between U=0 and U=1 columns < 1 m (or < one LSB of your export format).

---

# Files to touch (typical placement)
- `Source/.../TectonicSimulationService.*` — add `RidgeTangent`, `FoldDir`, `OrogenyClass`, and their builders.
- `Source/.../Amplification.*` — add `OceanicAmp()`, `ContinentalAmp()`, and blending masks.
- `Source/.../SphericalSampling.*` — add `FindContainingTriangle()` + `SampleElevation()`.
- `Shaders/...` (if you GPU this later) — mirror anisotropic sampling with same params.
- Tests: `.../Tests/GeoShapeTests.cpp` — ridge transect, belt alignment, seam continuity.

---

# What we’re **not** doing here
- No palette work, no normalization, no post-hoc seam copying, no dilation.  
- No refactor of tectonics Stage A itself—this is purely the **amplification + correct sampling** to produce the **mold**.

---

If you want, I’ll draft `SampleElevation(dir3)` and a minimal `FindContainingTriangle()` (KD-seed + adjacency walk) you can paste into `SphericalSampling.*`.
