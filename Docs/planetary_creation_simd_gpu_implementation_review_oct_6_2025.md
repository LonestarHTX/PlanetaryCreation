# PlanetaryCreation SIMD/GPU Implementation Review

**Repo:** `LonestarHTX/PlanetaryCreation`  
**Date:** Oct 6, 2025  
**Scope:** SIMD (CPU) & GPU (compute) path for Stage‑B elevation amplification and render‑side build.

---

## TL;DR
You’ve wired a working end‑to‑end GPU amplification path (shader → RDG dispatch → readback → mesh build) with a parity test against the CPU implementation. The next wins are: (1) mask out continental verts in the oceanic kernel, (2) convert the readback to async to remove stalls, and (3) small build/layout nits. After that, you’re set to scale L7–L8 comfortably.

---

## What I Looked At
- A large commit on Oct 6 adding the GPU compute path and tests.
- GPU wrapper: `OceanicAmplificationGPU.cpp/.h` (RDG setup, buffer binding, readback).
- Compute shader: `Shaders/Private/OceanicAmplification.usf`.
- Build rules: `PlanetaryCreationEditor.Build.cs`.
- Editor module startup: shader directory mapping & initialization.
- Service wiring: conditional GPU dispatch in the step loop.
- Exemplar texture array loader & lifetime management (for continental work next).
- Automation test: GPU vs CPU parity for oceanic amplification.

---

## Green Lights ✅
- **Shader path mapping** is registered at module startup so editor/tests can find USF.
- **End‑to‑end parity path**: parameters align, threadgroup sizing consistent, RDG‑native dispatch.
- **Fallback preserved**: CPU path still available behind a toggle.
- **Automation test present**: compares mean/max deltas with reasonable thresholds.
- **Exemplar infra** in place (PNG16 decode → `Texture2DArray`) for continental GPU.
- **Build deps** include RenderCore/RHI/ImageWrapper as needed.
- **Docs** call out current bottleneck (sync readback) and next steps.

---

## High‑Impact Fixes (Ranked)

### 1) Don’t Modify Continental Verts in the Oceanic Kernel
The current oceanic compute kernel writes amplified elevation for all vertices. Mirror the CPU behavior: **touch oceanic crust only**. Add a plate/crust mask SRV and early‑out for non‑oceanic verts.

**Kernel sketch:**
```hlsl
// OceanicAmplification.usf
StructuredBuffer<int>    VertexPlateAssignments; // per-vertex plate id
StructuredBuffer<uint>   PlatesCrustType;        // 0=oceanic, 1=continental

[numthreads(64,1,1)]
void Main(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    int plate = VertexPlateAssignments[i];
    if (plate < 0 || PlatesCrustType[plate] != 0) {
        OutAmplified[i] = Baseline[i];
        return;
    }
    // Oceanic-only amplification here...
}
```

### 2) Make Readback Asynchronous (Remove Stalls)
The current path does an immediate flush / GPU idle wait. Switch to **`FRHIGPUBufferReadback`** (or an RDG readback pass) without blocking and finish on the next tick via a tiny ring buffer. Swap in the new `AmplifiedElevation` only when `IsReady()`.

**Pattern:**
1. Dispatch compute and enqueue readback.
2. On subsequent tick(s), poll `Readback->IsReady()`.
3. When ready, map/copy into `TArray<float>` and trigger the mesh stream update.

### 3) Keep Threadgroup Size Unified
If you change `[numthreads]` (e.g., from 64 → 256), update both USF and dispatch divisor together. Add a `constexpr int ThreadGroupSize = 64;` on C++ side for clarity.

### 4) Tighten Shader Permutations & Build Deps
Gate compilation by feature level in the global shader:
```cpp
static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P)
{
    return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5);
}
```
Add `ShaderCore` explicitly to PrivateDependencyModuleNames if you include it.

### 5) Remove/Relocate `Shaders.Build.cs`
You already use **virtual shader path mapping**; a nested `Shaders.Build.cs` under the editor module won’t act as a real module. Delete it, or move to a dedicated top‑level shaders module (not necessary for this project).

### 6) Own Upload Data in RDG
Ensure the helper that creates SRVs copies from `TArray` into an RDG upload resource immediately so async readback/plumbing won’t depend on the lifetime of CPU arrays.

---

## SIMD (CPU) Quick Wins
1. **Cache invariants**: equirectangular UVs and tangent seeds depend only on LOD topology—precompute once per LOD, reuse every step.
2. **SoA for math, AoS for handoff**: maintain float SoA (`x[]/y[]/z[]`) for vectorized math; convert to `FVector3f` right before feeding RMC.
3. **ParallelFor** large loops with 8k–32k blocks to amortize task overhead.
4. **ISPC kernels** for ridge directions & Stage‑B transforms; compile AVX2/AVX‑512, call from C++.
5. **Doubles** stay in the simulation service; **floats** everywhere on the render path.
6. **Update only changed streams** in RMC (positions/normals/colors), keep topology static across steps.

---

## Tests To Add (Cheap & Protective)
- **Continents Unchanged**: After an oceanic GPU step, assert `Amplified == Baseline` (±ε) on all continental verts.
- **Multi‑seed, Multi‑LOD Parity**: L5/L6/L7 with 3 seeds; assert mean/max deltas within bounds.
- **Async Readback Smoke Test**: advance two steps and verify amplified data lags by ≤1 frame and never goes stale.

---

## Continental & Exemplar Notes (Looking Ahead)
- Verify **slice packing** in the `Texture2DArray` matches shader sampling order.
- Add a small **metadata SSBO** for per‑exemplar mins/maxes and array indices to avoid hardcoded constants.
- Consider a second compute pass for **GPU normals** once displacement is stable.

---

## Minor Polish
- Default‑on GPU for large meshes (e.g., `VertexCount ≥ 100k`) once parity + async are solid; keep CVar override.
- Guard verbose logging out of hot loops in Development builds.

---

## Suggested Implementation Order
1. Cache UVs/tangent seeds per LOD; mirror render arrays to float **SoA**.
2. `ParallelFor` the heavy loops; remove logging from hot paths.
3. ISPC ridge directions & Stage‑B transforms (CPU SIMD).
4. **GPU Stage‑B compute** (already in) → switch **readback to async**.
5. Add **oceanic mask** to the USF; keep continents unchanged.
6. Optional: GPU normals as a second pass; RMC updates only streams.

---

## Verdict
You’ve crossed the hardest threshold: a functioning, testable GPU compute path integrated with the sim and editor. Mask continents, make readback async, tidy the build bits, and you’re ready to push L7–L8 smoothly. After that, the continental/exemplar pipeline will drop in cleanly using the existing `Texture2DArray` infra.

