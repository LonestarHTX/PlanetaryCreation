Got it—let’s treat this like a parity investigation between *CPU baseline A* and *CPU re‑eval B after GPU readback*. From your description, “overwriting to ensure parity” isn’t eliminating the delta, which strongly suggests a **state drift** problem rather than a simple float↔double precision issue.

Below is a concrete, end‑to‑end plan that (1) nails *why* the numbers differ, (2) gives you targeted instrumentation to prove it, and (3) proposes surgical fixes. I’m opinionated where needed; push back where you think the assumptions don’t fit your branch.

---

## TL;DR — most likely root causes (ranked)

1) **Parameter / input snapshot drift between dispatch and re‑eval.**  
   The GPU pass captures `SimParams` and float inputs at dispatch time, but your CPU re‑evaluation after readback likely uses *current* service state instead of the *same* snapshot (baseline elevations, ridge vectors, crust ages, positions, mask). This is the classic “replay with different inputs” bug. Your GPU path explicitly grabs `SimParams` and the float caches at dispatch time, e.g. via `GetOceanicAmplificationFloatInputs(...)` (baseline, ridge, age, positions, mask) and then enqueues the render pass with those pointers captured in the lambda. If the CPU re‑eval isn’t replaying against the exact same inputs, you’ll get a consistent offset.

2) **Timing/order: crust age or “seed reset” changes between baseline and re‑eval.**  
   `ApplyOceanicDampening` starts with `ResetCrustAgeForSeeds(...)` and conditionally rebuilds/zeros arrays; if that runs between baseline and the re‑eval, your age field (which directly modulates amplification via `exp(-Age*Falloff)`) is different for CPU B than for CPU A/GPU, guaranteeing mismatches.

3) **Float cache invalidation gap.**  
   Your GPU path depends on a float SoA cache for baseline, ridge, age, pos, and mask via `GetOceanicAmplificationFloatInputs(...)`. If you overwrite doubles after readback but do not invalidate/rebuild the corresponding float cache, the next replay may push stale inputs to GPU or any path that consumes those caches. I don’t see an explicit invalidation call tied to surface versioning around this cache in the shared snippets; the GPU preview scaffolding defensively checks for size mismatches but not staleness of content.

4) **Preview/skip toggles affecting the CPU path.**  
   The project added `bSkipCPUAmplification` for GPU preview; controller code flips this flag to bypass CPU work when preview is on. If that remains set somewhere in your parity run, the CPU “re‑eval” may not actually recompute, or it may be running in a different sequence than baseline (e.g., SurfaceDataVersion not bumped when preview path is used). That *can* lead to subtle “baseline drift” if other steps (dampening, erosion) still advance state.

5) **Small but systematic numeric differences (float vs. double) amplified by extra steps.**  
   CPU uses double math; your GPU/USF is float. Both CPU and GPU agree on the core formulae (e.g., Age falloff, Gabor approximation, gradient noise) but float truncation enters at inputs (baseline/age/ridge/pos), in shader math, and on readback (float→double). The USF and CPU formulas line up closely (e.g., the “max‑abs two‑sample Perlin → sharpen^0.6 → ×3 clamp” path) so the raw numeric variance *alone* should be sub‑meter and random‐looking, not a persistent bias. If your delta is consistent across steps/areas, it’s almost certainly 1–3 above.

---

## What to instrument (minimal, decisive)

> Goal: prove whether CPU B is using the same exact inputs as CPU A/GPU.

### 1) Stamp the job with a **SnapshotId** + input **hashes**
At the instant you **dispatch** GPU oceanic amplification, compute and store:

- `SnapshotId` (monotonic counter or 128‑bit GUID)
- `LOD`, `TopologyVersion`, `SurfaceDataVersion` at dispatch  
- `SimParamsSnapshot` (store the struct)
- Per‑buffer 64‑bit hashes (xxhash/cityhash) of:
  - BaselineFloat, RidgeDirFloat, CrustAgeFloat, PositionFloat, OceanicMask (the float cache you pass to RDG)  
  - Optional: the *double* `VertexAmplifiedElevation` and `VertexCrustAge` you consider “baseline” for CPU A

You are already lifting `SimParams` and the float arrays and capturing them into the render‑thread lambda for GPU paths (both regular and preview paths). Extend your `Enqueue*GPUJob` payload with those hashes and the SnapshotId.

### 2) On **readback**, assert invariants before any CPU re‑eval

- Recompute the same per‑buffer hashes from the *current* service state you plan to use for CPU B.
- Log a one‑liner if any hash differs from the dispatch snapshot (and print which array).
- Assert that `(LOD, TopologyVersion)` match; if not, treat the readback as stale and drop it (your “readback fix” doc already suggests SnapshotId gating) — this is especially important if you remove the synchronous `FlushRenderingCommands()` later.

### 3) Log a condensed **parity row** for deltas
For the first N mismatching vertices:
```
[Parity] Snap=42 V=123 Plate=7 Mask=1
  BaseA=... BaseB=... AgeA=... AgeB=... RidgeA=(...) RidgeB=(...) CPUA=... GPURB=... CPUB=... Δ(GPU–A)=... Δ(B–A)=...
```
You already have infrastructure logging per‑vertex diffs in GPU parity tests; copy that style here (e.g., how continental parity logs Base/CPU/GPU deltas).

> If the hashes match but B still differs from A by more than float‑quantization (say >0.2–0.5 m), then it’s a math divergence; otherwise it’s state drift.

---

## Concrete places that need the guardrails

- **Dispatch captures vs. replay:**  
  In your GPU path you capture pointers to the float caches and `SimParams` in the `ENQUEUE_RENDER_COMMAND` lambda (both regular and preview paths). That’s good. The problem is the *CPU re‑evaluation after readback* typically calls into service getters again (`GetVertexAmplifiedElevation`, `GetVertexCrustAge`, `GetVertexRidgeDirections`, etc.). Unless you reconstruct B from the *same snapshot* you fed to GPU, you’re replaying against potentially altered state.

- **Dampening/age mutation in the step loop:**  
  `ApplyOceanicDampening(...)` mutates `VertexCrustAge` and other fields and begins by resetting age at seeds (`ResetCrustAgeForSeeds(...)`) depending on a ring parameter. If your step order is **[A: CPU Stage‑B] → [GPU Stage‑B] → [readback] → [CPU re‑eval B]** but another system ticked in between (or the re‑eval happens on a later button press/frame), `CrustAge` is no longer identical.

- **Preview skip flag impacting versioning/rebuilds:**  
  When `bSkipCPUAmplification=true` (set by the GPU preview toggle), you bypass CPU amplification and also throttle `SurfaceDataVersion++` to avoid rebuilds. Ensure your parity scenario isn’t inadvertently running with this flag set; otherwise “CPU re‑eval” may be a no‑op or run at a different point in the lifecycle than baseline.

---

## Fixes I’d implement (low‑risk, high‑value)

### A) Make CPU re‑eval use a **closed world** snapshot (no service reads)
Introduce a tiny helper to *replay* Stage‑B CPU using the exact arrays captured at dispatch:

```cpp
struct FOceanicAmpSnapshot {
  FTectonicSimulationParameters Params;
  TArray<float>   Baseline_f;
  TArray<FVector4f> Ridge_f;
  TArray<float>   Age_f;
  TArray<FVector3f> Pos_f;
  TArray<uint32>  Mask_u32;
  // Optional: doubles if you want exact pre-quantization baseline
  TArray<double>  Baseline_d;
  // Debugging
  uint64 InputHash = 0;
  int32  LOD, TopologyVer, SurfaceVer;
  uint64 SnapshotId;
};
```

- On dispatch: fill this `FOceanicAmpSnapshot` and store it alongside the `FRHIGPUBufferReadback` in your pending‑job list.
- On readback (right before CPU re‑eval): **do not** fetch from `Service`; instead, run `ComputeOceanicAmplification(...)` against `Baseline_d` (or `Baseline_f` cast to double), `Age_f`, `Ridge_f`, `Pos_f`, and `Params` from the snapshot.  
  That gives you CPU‑B that is *guaranteed* to use the same inputs.

This mirrors how you already capture/convert data on the RDG path; see how you create SRVs from the captured float arrays for the preview/regular kernels.

### B) Gate readbacks by **SnapshotId/versions** and **hashes**
Implement a tiny “accept/drop” check in `ProcessPending*GPUReadbacks(...)`:

- Drop if `LOD`, `TopologyVersion`, or `SurfaceDataVersion` no longer match the snapshot.
- Drop if any input hash differs from the dispatch snapshot (this catches late reallocation or cache reuse bugs).
- If accepted, write the readback into `VertexAmplifiedElevation` and **invalidate the float cache** used by `GetOceanicAmplificationFloatInputs(...)` so subsequent GPU/CPU calls rebuild from the new doubles.

> Your GPU readback code presently uses `FlushRenderingCommands()` in the editor path (synchronous) so this may not bite you *there*; it *will* bite you if/when you move to the async plan described in your readback‑fix doc.

### C) Invalidate/rebuild the **float SoA cache** on writeback
Where you copy readback → `VertexAmplifiedElevation` (double), also call into something like:
```cpp
void UTectonicSimulationService::InvalidateOceanicFloatCache()
{
  OceanicFloatCacheVersion++;
}
```
…and ensure `GetOceanicAmplificationFloatInputs(...)` rebuilds when it sees a new version. Your preview path currently only checks size; add a version or hash check to avoid stale content being reused with different doubles later (you already log when sizes mismatch).

### D) Freeze age for the duration of a Stage‑B job
When you create `FOceanicAmpSnapshot`, include the *age* array from that exact tick. If you absolutely must re‑use service arrays for CPU B, then *at minimum* store the `StepIdx` / time stamp and assert it hasn’t advanced. But the snapshot approach above is safer and deterministic.

### E) Keep preview skip out of parity
For any test or manual check that compares CPU A vs CPU B, force `bSkipCPUAmplification=false`, and if you’re in preview mode, ensure you still advance `SurfaceDataVersion` appropriately so caches rebuild consistently (your perf doc proposes *not* bumping this when skip=true, which is correct for perf, but inappropriate for parity runs).

---

## Sanity checks you can run today

1) **Hash your inputs** right before GPU dispatch and again right before CPU re‑eval B. If *any* of `Baseline`, `Age`, `Ridge`, `Pos`, or `Mask` differ (hash mismatch), you’ve found the cause.

2) **Disable dampening** between baseline and re‑eval**:**  
   Toggle `bEnableOceanicDampening=false` just to see if the mismatch collapses. If it does, age mutation is your culprit (see `ResetCrustAgeForSeeds(...)`).

3) **Force snapshot replay** for CPU B using only the captured arrays (no service getters). If parity becomes perfect (within float quantization), your pipeline timing was the only problem.

---

## Why I don’t think it’s just numerics

- CPU and GPU formulas are aligned down to the sharpening exponent (0.6), scaling/clamp of the “Gabor‑like” noise, and the multi‑octave gradient noise loop (4 octaves, 0.1 base frequency) both on CPU and in the USF; the differences present as small sub‑meter deviations in your own GPU parity tests when everything else is equal.  
- A *consistent* offset that survives overwrites means *inputs changed*, not just FP rounding.

---

## Related correctness items (worth double‑checking once parity is fixed)

- **Ridge directions source:** Make sure your CPU ridge vectors are being built from your **render‑vertex boundary cache** near divergent edges and not falling back to age gradients too often. Your own doc call‑outs explain how to compute boundary tangents and when to fallback mid‑ocean, and this materially affects Stage‑B amplitude patterns (alignment/coverage metrics). This is orthogonal to the parity bug but impacts “why the field looks different than expected”.

- **GPU preview:** The preview pass writes an R16F height texture and logs seam coverage; ensure its inputs are the same float caches as the compute readback path (they are), and that you invalidate caches after a writeback so preview and CPU paths don’t diverge.

---

## Quick code sketches (drop‑in patterns)

**Hash helper (xxhash64 or FNV):**
```cpp
template<typename T>
uint64 HashArray(const TArray<T>& A) {
  // simple, fast; swap in xxhash if you have it
  uint64 H = 1469598103934665603ull; // FNV offset
  const uint8* Ptr = reinterpret_cast<const uint8*>(A.GetData());
  const size_t N = sizeof(T) * A.Num();
  for (size_t i = 0; i < N; ++i) { H ^= Ptr[i]; H *= 1099511628211ull; }
  return H;
}
```

**Snapshot capture at dispatch:**
```cpp
FOceanicAmpSnapshot Snap;
Snap.Params = Service.GetParameters();
Service.GetOceanicAmplificationFloatInputs(&Snap.Baseline_f, &Snap.Ridge_f, &Snap.Age_f, &Snap.Pos_f, &Snap.Mask_u32);
Snap.LOD = Service.GetRenderSubdivisionLevel();
Snap.TopologyVer = Service.GetTopologyVersion();
Snap.SurfaceVer  = Service.GetSurfaceDataVersion();
Snap.InputHash   = HashArray(Snap.Baseline_f) ^ HashArray(Snap.Age_f) ^ HashArray(Snap.Mask_u32);
// (Optionally copy Baseline_d if your CPU Stage-B expects doubles.)
```

**CPU re‑eval against snapshot (no service):**
```cpp
ParallelFor(Snap.Baseline_f.Num(), [&](int32 i){
  if (Snap.Mask_u32[i] == 0u) {
    CPU_B[i] = (double)Snap.Baseline_f[i];
    return;
  }
  // Rebuild the same TransformFaultDir and run the same formula as in OceanicAmplification.cpp
  // using Snap.Params, (double)Snap.Age_f[i], (FVector3d)Snap.Ridge_f[i].xyz, (FVector3d)Snap.Pos_f[i]
  CPU_B[i] = ComputeOceanicAmplification(/* position, plateId?, age, baseline, ridge, plates?, boundaries?, */ Snap.Params);
});
```

---

## If you confirm drift, here’s the minimal policy to adopt

1) **Every Stage‑B job is snapshot‑based** (inputs, params, versions).  
2) **Readbacks are accepted only if SnapshotId & versions still match.**  
3) **All CPU replays for validation use the same snapshot—not live service state.**  
4) **Float caches are versioned and invalidated whenever doubles are overwritten.**

That will make this class of parity bugs go away for good.

Pointers into your code/docs that matter for this investigation

GPU dispatch paths that capture float caches & params into RDG lambdas / preview path (what you’ll be snapshotting and later comparing).

The readback sequences that currently FlushRenderingCommands() then enqueue a service job; useful as you move to the async/fence plan.

Dampening mutating age (the likely “silent drift” between A and B if runs in between). 

