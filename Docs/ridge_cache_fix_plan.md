> **Status Update (2025-10-08):** Stage‑B logging now records ridge dirty/update counts and cache hits each step, and undo/redo restores the cached ridge directions instead of forcing a full recompute. Oceanic parity at L7 shows the CPU baseline touching only 192 vertices (Stage B 19.3 ms, Ridge 0.04 ms, Readback 0.00 ms; see `Saved/Logs/PlanetaryCreation.log:1547`). Keep `RidgeDirectionCacheTest` plus the Milestone 6 GPU parity suites on CI to guard the snapshot path.
>
> **Status Update (2025-10-07):** Mainline now gates ridge recomputes through `RefreshRidgeDirectionsIfNeeded()` and pulls stage‑B tangents from `RenderVertexBoundaryCache`, falling back to the age gradient only when cached data is missing. Keep `RidgeDirectionCacheTest` on the CI roster to guard future regressions.
>
> The historical investigation that prompted this fix is preserved below for context.

Totally hear you—this is one of those “everything *almost* works so the wrong fallback looks plausible” bugs. I pulled up your repo and the latest commit history. Your Oct 7 commit explicitly notes that you *added* a render‑vertex boundary cache (`FRenderVertexBoundaryInfo` + `BuildRenderVertexBoundaryCache()`), but **you haven’t actually plugged that cache into ridge‑direction sampling yet**. The result matches your log: many ridge‑zone vertices can’t find a valid tangent and fall back to the age‑gradient path, which tanks amplification + alignment (your own commit message shows \~976 “Missing” and \~7.1k “Gradient fallback,” with \~49% alignment) 🔗. The README also confirms where the M6 GPU/oceanic tests live and how to run them, which lines up with the automation you’re using for parity checks.

Below is a crisp root cause + a drop‑in correction you can make today.

---

## Root cause

1. **Resolution mismatch (coarse boundary vs. render vertices).**\
   `FPlateBoundary::SharedEdgeVertices` is coarse (dozens of verts). Your render mesh at test LOD is thousands (10k+). Sampling a ridge tangent from the coarse shared‑edge graph means **most render‑space ridge vertices never see a boundary‑tangent** and you fall back to the age gradient. That explains the “Missing ≈ 976 / Gradient Fallback ≈ 7k / Alignment ≈ 49%” numbers you’re seeing. Your own commit notes this exact chain and introduces a render‑level cache to fix it, but it isn’t wired into the ridge‑vector computation yet.

2. **(Already fixed) Voronoi reassignment didn’t reset elevation/age.**\
   You fixed the mismatch after `BuildVoronoiMapping()` so continental/oceanic elevations + ages get reset when plate membership changes. That unblocks oceanic logic for young crust, but without the proper ridge tangents the amplification still underperforms.

---

## Correction action (surgical & testable)

### 1) Use the render‑vertex boundary cache during ridge‑direction sampling

You already have:

- **Struct:** `FRenderVertexBoundaryInfo` in `TectonicSimulationService.h`.
- **Builder:** `BuildRenderVertexBoundaryCache()` in `TectonicSimulationService.cpp`.\
  Both are called out in your Oct 7 commit summary. What’s missing is using that cache inside your ridge field builder (your commit literally says “Status: structure in place, **integration with **``** needed**”).

**What to do**

- Ensure `BuildRenderVertexBoundaryCache()` runs **after** Voronoi reassign / plate IDs are final and **before** ridge computation and amplification.
- In `ComputeRidgeDirections()` (or wherever you set the per‑vertex ridge direction used by oceanic Stage B), source the vector from `FRenderVertexBoundaryInfo` when a vertex is at/near a **divergent** boundary; only fall back to the age‑gradient for mid‑ocean vertices *far* from ridges.

**Pseudo‑patch (adapt names to your branch):**

```cpp
// TectonicSimulationService.h (already present per commit)
// struct FRenderVertexBoundaryInfo {
//   uint8 Flags;          // bitmask: HasBoundary, IsDivergent, ...
//   float ArcDistance;    // radians to nearest divergent boundary
//   FVector3d TangentWS;  // unit vector in tangent plane at vertex
// };


// TectonicSimulationService.cpp
void UTectonicSimulationService::RebuildCachesAndFields()
{
    BuildVoronoiMapping();                    // your existing call
    BuildRenderVertexAdjacency();             // existing
    BuildRenderVertexBoundaryCache();         // <-- make sure this runs here
    ComputeRidgeDirections();                 // <-- integration happens inside
}

void UTectonicSimulationService::ComputeRidgeDirections()
{
    const double MaxInfluence = 0.05; // radians (~3°) — same threshold noted in your commit
    int32 CacheHits=0, Missing=0, GradientFallback=0;

    RidgeDirectionWS.SetNumZeroed(RenderPositions.Num());
    for (int32 v = 0; v < RenderPositions.Num(); ++v)
    {
        const auto& Info = RenderBoundaryInfo[v];
        const bool bUseBoundaryTangent =
            (Info.Flags & ERidgeBoundaryFlags::HasBoundary) &&
            (Info.Flags & ERidgeBoundaryFlags::IsDivergent ) &&
            (Info.ArcDistance <= MaxInfluence) &&
            !Info.TangentWS.IsNearlyZero();

        if (bUseBoundaryTangent)
        {
            RidgeDirectionWS[v] = Info.TangentWS; // already in the local tangent plane
            ++CacheHits;
        }
        else if (IsOceanicVertex(v))
        {
            RidgeDirectionWS[v] = ComputeAgeGradientDir(v); // existing path
            ++GradientFallback;
        }
        else
        {
            RidgeDirectionWS[v] = FVector3d::ZeroVector;
            ++Missing;
        }
    }

    UE_LOG(LogPlanetaryCreation, Display,
        TEXT("[RidgeDir] CacheHits=%d Missing=%d GradientFallback=%d"),
        CacheHits, Missing, GradientFallback);
}
```

**Notes on correctness:**

- *Tangent orientation on a sphere.* When you build the cache: for an edge (p→q) that crosses a **divergent** plate pair, you can compute a geodesic tangent at p as\
  `n = Normalize(Cross(p, q));`\
  `t = Normalize(Cross(n, p));`\
  Then re‑project and normalize to ensure it lies in p’s tangent plane (orthogonal to p). Do the symmetric version at q. Average multiple incident edge tangents per vertex and normalize at the end.
- *Sign consistency.* If your test is sign‑sensitive, choose a stable sign using the relative plate‑motion vector at the boundary: flip `t` so `Dot( t, Normalize(Cross(n, RelativeMotionWS)) ) ≥ 0`. (If your test uses absolute alignment, sign won’t matter.)
- *Cache coverage.* Mark `HasBoundary|IsDivergent` on *both* vertices of any render‑adjacent edge that crosses a divergent boundary. Write `ArcDistance=0` for those and propagate minimal distances outward (multi‑source BFS over the render adjacency) until `ArcDistance > MaxInfluence`. That guarantees every ridge‑zone vertex has a valid tangent.

### 2) Recompute after plugging in the cache

Your own commit lists the expected thresholds once this integration happens: ridge alignment ≥ 80% and young‑crust amplification ≥ 60% if you use the cache within 0.05 rad of the boundary and only fall back mid‑ocean. That’s the right target for your test’s acceptance.

**Order of operations in the step loop**

1. Plate motion update
2. Voronoi mapping / render‑plate IDs (your fixed path)
3. `BuildRenderVertexAdjacency()` (already there)
4. `BuildRenderVertexBoundaryCache()` (**new required step before ridge**)
5. `ComputeRidgeDirections()` (**reads the cache now**)
6. Stage B oceanic amplification & dampening

---

## Why this fixes your symptoms

- **“Missing \~1k” & “Fallback \~7k” collapses.** With render‑space boundary discovery, *every* ridge‑zone vertex reads a valid tangent; only mid‑ocean points fall back to the age gradient. That flips your ratio and pushes alignment above the 80% target (your commit’s “next steps / expected” block states exactly this).
- **Young crust amplification pops back.** With correct ridge vectors at age≈0 near boundaries, your anisotropic noise aligns to ridges, so the oceanic Stage B signal ramps to spec for young crust instead of being smeared by age‑gradient directions.

---

## Quick validation checklist

1. **Run the automation** (editor or commandlet). The README shows the patterns & log locations; M6 GPU smoke/parity commands are listed there. Tail `Saved/Logs/PlanetaryCreation.log` and check ridge diagnostics.
2. **Metrics to look for** (pulled from your commit’s diagnostic block):
   - Ridge alignment ≥ 80%
   - Young‑crust amplification ≥ 60%
   - Missing tangents ≈ 0 in ridge zone; gradient fallback should only dominate far from ridges.
3. **GPU parity**—your parity tests already pass; this change only affects CPU ridge vectors used by Stage B and will simply improve consistency.

---

## Edge cases & gotchas (so this doesn’t regress later)

- **Triple junctions.** Average multiple incident edge tangents (normalize after summation). Consider a soft clamp on the deviation from the dominant tangent so you don’t get a “Y” smear.
- **Seams / duplicates.** If you maintain a duplicate‐vertex map for seam continuity, write the cache for both the canonical and duplicate indices. (Your terrane/mesh‑surgery work mentions restoring duplicates—keep that in mind when writing per‑vertex caches.)
- **LOD changes.** Invalidate and rebuild `FRenderVertexBoundaryInfo` whenever you change render LOD or re‑tessellate.
- **Performance.** The multi‑source BFS distance fill is linear in edges and only needs to run when the Voronoi/plate IDs change. You already cache adjacency and adjacency weight totals (used by dampening), so reuse that to keep this cheap.

---
