# SIMD/GPU Implementation Review for PlanetaryCreation

Heck yes—it’s SIMD/GPU time 😈. I took a full pass over the repo and recent commits and pulled together a concrete plan with pointed changes (and a few “don’t do this to Future-You” notes). Below is what’s solid already, what I’d change before vectorizing, and then a step‑by‑step path for a SIMD‑first and GPU‑first implementation you can pick from.

---

## Quick read of where things stand

- **Architecture is clean**: `UTectonicSimulationService` holds the canonical **double‑precision** state and advances the simulation; `FTectonicSimulationController` bridges to rendering, converts to floats, and feeds a **RealtimeMeshComponent** actor; everything runs entirely in the **editor**. Nice separation of concerns.
- **Recent work** added toggles without forced resets (LOD/heightmap), plus better snapshot logging in the controller. Good call for perf iteration.
- **Build loop shape**: the controller’s `BuildMeshFromSnapshot` iterates all render vertices, computes UVs (currently equirectangular), tangents, colors, etc. That’s going to be one of your hottest loops.
- **Data you expose is vectorization/gpu‑friendly**: `GetRenderVertices()`, `GetVertexPlateAssignments()`, `GetVertexElevationValues()`, `GetVertexAmplifiedElevation()`, `GetVertexRidgeDirections()`, `GetBoundaries()`, `GetSharedVertices()` are exactly the kind of flat arrays/buffers we want. Your tests also pin down expected behaviors (oceanic vs continental changes, determinism, etc.).
- **Stage‑B exemplar data**: a small library of 22 16‑bit PNG / COG exemplars with corrected content paths—perfect candidates to pre-upload as a **Texture2DArray** on GPU.
- **RMC plugin**: good choice; it’s faster and more flexible than ProceduralMeshComponent and plays well with procedural/gigantic geometry. It supports stream updates, which we can exploit (update only positions/normals instead of rebuilding everything).

---

## Before you vectorize/offload: three small but high‑leverage fixes

1. **Stop recomputing invariants per build**
   - The equirectangular UVs you compute from normalized vertex directions (in `BuildMeshFromSnapshot`) only depend on the unit direction and LOD topology—**not** on per‑step elevation. Precompute once per LOD and cache; then reuse when elevation changes.
   - Same for tangent seed (`UpVector`) heuristic—precompute per vertex.

2. **Data layout for math-heavy passes (SoA > AoS)**
   - Anywhere you do large vector ops (`ComputeRidgeDirections`, elevation transforms, per‑vertex color), store render-space geometry as **SoA** (`x[]`, `y[]`, `z[]`) for math; keep AoS (`FVector3f`) only for handoff to RMC. You’re already double in the service; use float SoA for render‑side math to cut bandwidth in half. (You can still preserve doubles in the canonical state.)

3. **No logging in hot loops**
   - Some debug logs were added recently (good for diagnosing), but make sure they compile out in Development and never tick inside big loops. Wrap with `#if UE_BUILD_DEBUG` or a dedicated `CVar`.

---

## Path A — SIMD‑first (quick wins on CPU)

**Targets**: per‑vertex transforms, ridge direction updates, Stage‑B amplification math (CPU path), colorization/packing.

1. **Threading first, then SIMD**
   - Wrap large loops with `ParallelFor` (or `UE::Tasks::ParallelForEach`) over contiguous blocks of vertex indices. Keep each block large enough (e.g., 8k–32k vertices) to amortize task overhead.

2. **Vectorize the math on float SoA buffers**
   - Convert `TArray<FVector3d>` → temporary SoA `TArray<float>` for `x`, `y`, `z` (or keep a mirrored SoA that you refresh when LOD changes).
   - Normalize/cross/dot using AVX2/AVX‑512 intrinsics **or** ISPC (easier). Typical pattern:
     - Load 8–16 `x`/`y`/`z` lanes
     - `rsqrt` normalize
     - Per‑lane trig (`atan2`, `asin`) is the slow bit; batch it and consider an **approx** version or table where visual error is OK (UVs can tolerate approximation since they don’t drive physics).
   - UE doesn’t SIMD‑accelerate `FVector3d` operations; relying on `FMath` here won’t vectorize. Rolling your own with ISPC is pragmatic.

3. **ISPC drop‑in (strongly recommended)**
   - Put a small **/Simd** folder with ISPC kernels for:
     - `ComputeRidgeDirections(float* x, float* y, float* z, float* out_rx, float* out_ry, float* out_rz, int N)`
     - `AmplifyElevationCPU(float* elevBase, float* rx, float* ry, float* rz, const uint16_t* exemplar, /*…*/, float* elevOut, int N)`
   - Compile with AVX2 (and AVX‑512 if you want) and call from C++. This keeps your C++ clean and gets you auto‑vectorization/gather/scatter without hand‑writing intrinsics.

4. **Avoid double in SIMD loops**
   - Keep doubles in the **service**; convert to float once per snapshot. AVX2 doubles cut your SIMD width in half for little visual upside. (Stick to doubles for steps where numerical drift matters—plate kinematics/accumulated transforms—then down‑cast.)

5. **Only update streams that changed**
   - With RMC, rebuild indices/topology only on LOD changes. For per‑step updates, update **position**, optional **color**, and **normal/tangent** streams. The recent code already computes tangents procedurally; that helps.

**What you’ll likely see**: 2–6× speedup on per‑vertex build paths and ridge updates on a modern desktop CPU, more if you were previously scalar and logging.

---

## Path B — GPU‑first (compute shader, minimal churn to your code)

You run in the **editor**, not PIE, and update geometry on demand—not 60 Hz—which is great for a GPU compute pass with read‑back. The sweet spot is to offload **Stage‑B displacement synthesis** (the expensive exemplar sampling + blending) to a compute shader, then read back a single float buffer of **amplified elevation** to feed your existing RMC pipeline.

### 1) Prepare resources on the CPU side

- **Pre‑upload exemplars** as a single `Texture2DArray` in PF_G16 or PF_R16F (PF_R16F is simpler on shader math). Use the paths in your fixed ExemplarLibrary JSON.
- Build per‑vertex **inputs** as SRVs:
  - `StructuredBuffer<float3> UnitDir` (unit sphere directions of render vertices)
  - `StructuredBuffer<float> ElevBase` (base meters)
  - `StructuredBuffer<int> PlateId`
  - `StructuredBuffer<float3> RidgeDir` (if used by oceanic amplification)
  - Optional: `StructuredBuffer<float2> UVPrecomputed` (if you decide not to recompute UVs on GPU)

You already compute the equirectangular mapping in C++; mirror that math in HLSL, or better, precompute once per LOD and pass `TexCoord` as input.

### 2) A single compute pass

Create a global shader `FStageBAmplifyCS` with parameters:

```hlsl
// StageBAmplifyCS.usf
RWStructuredBuffer<float> OutAmplifiedElevation;

StructuredBuffer<float3> UnitDir;          // render vertex directions
StructuredBuffer<float>  ElevBase;         // base elevation (m)
StructuredBuffer<int>    PlateId;          // for continental vs oceanic/mixing
StructuredBuffer<float3> RidgeDir;         // oceanic ridge influence

Texture2DArray<float>    Exemplars;        // PF_R16F texture array
SamplerState             ExemplarSampler;

cbuffer Params {
  float ElevScale;
  float OceanicStrength;
  float ContinentalStrength;
  uint  NumExemplars;
  uint  UseOceanic;
  uint  UseContinental;
  // ... any weighting/LOD thresholds
};

[numthreads(128,1,1)]
void Main(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    // 1) Optional: derive UV from UnitDir[i] (equirectangular)
    // 2) Select exemplar index / weights (simple heuristic or precomputed indirection)
    // 3) Sample & blend exemplars
    // 4) Write OutAmplifiedElevation[i] = ElevBase[i] + AmplifiedDelta;
}
```

Wire it with RDG in your editor module:

```cpp
// FStageBAmplifyCS::FParameters* P = GraphBuilder.AllocParameters<...>();
// Fill SRVs/UAVs, add pass, then EnqueueCopy to a readback buffer.
// On completion, copy back into your TArray<float> VertexAmplifiedElevation and proceed with RMC build.
```

(You’ll need `RenderCore`, `RHI`, and a small `Shaders` dir added in `PlanetaryCreationEditor.Build.cs`. RMC will happily take the updated float elevations and you can reuse your existing mesh build path.)

### 3) Where to begin offloading

- **Stage‑B only** first (low risk): one buffer in, one buffer out, minimal changes elsewhere.
- If that goes well, consider also computing **normals** in a second compute pass from the displaced positions (or from a neighbors table) and avoid CPU normal recompute entirely.

### 4) Read‑back isn’t scary here

You’re stepping in multi‑million‑year increments, not at frame rate. A one‑time GPU compute + read‑back per step is fine UX‑wise in the editor. (If/when you want real‑time camera‑driven updates, keep the displacement on‑GPU and drive it in the vertex factory/material, but that’s a bigger plumbing change.)

---

## Specific “change this” items with file pointers

- **Precompute UVs/tangents** instead of per‑build recompute  
  In `FTectonicSimulationController::BuildMeshFromSnapshot`, UVs are currently derived per vertex right before adding to the stream set. Persist them alongside render vertices per LOD and reuse.

- **Keep doubles in the service, float everywhere else**  
  The design intentionally uses doubles in the service to avoid drift; keep it. But snapshots feeding RMC should be in float SoA and reused across frames.

- **Boundary data** (`TMap<TPair<int32,int32>, FPlateBoundary>`) is not GPU‑friendly  
  If `ComputeRidgeDirections()` depends on it, generate a **flat “segments” array** (CSR‑style) next to the map so both SIMD and GPU can iterate without hashing or branching on map lookups. The tests already read `GetBoundaries()` & `GetSharedVertices()`; add a `GetBoundarySegments()` that returns contiguous memory.

- **Hot loop hygiene**  
  The debug prints added to `CreateMeshBuildSnapshot()` are helpful while stabilizing, but ensure they’re compiled out. (They’re inside `if` guards now—just make sure it doesn’t sneak into release builds.)

- **Exemplar IO**  
  You fixed pathing in `ExemplarLibrary.json`; at editor startup (or first enable of continental/oceanic amp), load PNG16s into `UTexture2D` and keep them resident. One trip to GPU, then never touch disk while stepping.

---

## Suggested implementation order (lowest effort → highest payoff)

1. **Cache invariants** (UVs, tangent seeds) and **swap to SoA** for render‑side arrays.  
2. **ParallelFor** the big loops; keep per‑task blocks large.  
3. **ISPC** kernels for ridge directions & Stage‑B math on CPU.  
4. **GPU Stage‑B compute** as a single pass with read‑back of one float buffer.  
5. Optional: **GPU normals** pass, then CPU only stitches RMC streams.  
6. Optional (advanced): move displacement to a custom vertex factory/material to avoid read‑backs for real‑time camera‑driven LOD.

---

## Guardrails & tests (so we don’t regress science or visuals)

- Reuse your automation tests (Continental/Oceanic amplification, determinism %, variance checks). Add one more test for **float vs double parity** on small meshes; assert `max|Δelev| < 1e‑3` m at LOD 5 for a fixed seed.
- Add quick timers around: `CreateMeshBuildSnapshot`, `BuildMeshFromSnapshot`, and (new) `RunStageBAmplifyCS`. Use Unreal Insights to verify we’re not on the game/editor thread during heavy lifting.

---

## Gotchas to plan around

- **Double‑precision SIMD**: painful and rarely worth it for visuals. If a specific tectonic integration truly needs doubles, keep that CPU‑scalar (or vectorize where possible) and downcast at snapshot time.
- **Divergent branches** in GPU compute (continental vs oceanic vs boundary): use permutations or per‑mask kernels to keep warps coherent rather than one giant `if` soup.
- **RMC update granularity**: update only the changed streams; avoid recreating sections. RMC supports fine‑grained stream updates.

---

## TL;DR / Punch list

- [ ] Precompute & cache UV/tangent seeds per LOD (no per‑build trig).  
- [ ] Mirror render arrays to **float SoA**; keep doubles only in the service.  
- [ ] `ParallelFor` large vertex loops; remove logs in hot paths.  
- [ ] Add **ISPC** kernels for ridge dirs + Stage‑B math on CPU.  
- [ ] Upload 22 exemplars once → `Texture2DArray`; implement **`FStageBAmplifyCS`** compute pass; read back one float buffer.  
- [ ] Consider GPU normals as a second pass.  
- [ ] Keep RMC index/topology static across steps; update only position/normal/color streams.

---

*If you’re leaning CPU‑first, start with ISPC kernels and ParallelFor. If you’re itching to jump straight to GPU, begin with `FStageBAmplifyCS` and the texture array upload—those two pieces will give you the biggest swing with minimal disruption.*

