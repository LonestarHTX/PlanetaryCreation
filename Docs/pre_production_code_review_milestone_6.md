Below is a pre‑production code review that assumes you’ll ship this to other engineers. I’ve organized it by **severity**, given **actionable fixes**, and tied gaps back to the **paper** where relevant. I’m deliberately blunt where needed.

---

## TL;DR — top risks you should address first

1) **Parity drift (CPU A vs CPU B after GPU readback)** — very likely caused by **stale inputs / age mutation between baseline and replay**, and/or **float cache not invalidated** after writing readback into doubles. Adopt **snapshot‑based Stage‑B jobs**, gate readbacks by **versions + input hashes**, and **invalidate the float SoA cache** on write‑back. Your own docs outline the exact guardrails but they are not universally enforced yet.

2) **Oceanic GPU kernel touches all verts (including continental)** — CPU path only amplifies **oceanic** crust; mirror that on GPU using a **crust mask SRV** to early‑out for continental verts. This is correctness and perf.

3) **Async readback correctness in automation** — runtime path is now fence‑based, but tests must **force submit once** before polling `IsReady()` or they hang. Ensure `ProcessPending…(true)` is called once, then poll without blocking. Avoid `FlushRenderingCommands()` in production.

4) **Non‑deterministic split/merge candidate selection** — the merge/split pass still shows a “first element” selection pattern tied to **TMap iteration order**. Determinize with an explicit **priority sort** (score, then stable tiebreaker by PlateId). This is a determinism foot‑gun.

5) **Ridge‑direction placeholder** — `ComputeRidgeDirection` remains a dead‑end in the CPU path; if anything ever routes through it, you’ll get wrong orientation for transform‑fault noise. Either hard‑assert if it’s called, or implement the real tangent‑space derivation from the **render‑vertex boundary cache**, which you’re already building elsewhere.

6) **Seam logic** — you fixed double counting in `ComputeSeamCoverageMetrics`, but preview still runs equirectangular with filtering at ±π. Long‑term, migrate preview to **cube‑face** or a proper wrap‑aware write. Short‑term: keep the seam‑aware metric and atomics as you planned.

---

## Detailed findings (with fixes) — prioritized

### A. Correctness / Determinism

**A1. CPU baseline vs CPU replay drift after GPU readback — snapshot discipline is incomplete (Blocker).**  
*Evidence / why it matters:* Your own write‑up calls for: (a) **readback accept/drop** by LOD/topology/surface versions and **input hashes**; (b) **float SoA cache invalidation** when doubles are overwritten; (c) snapshotting **age** so dampening/seed resets can’t mutate inputs between A and B. This is exactly the scenario you earlier reported (“baseline differs after readback”). If any of these are missing, drift is guaranteed.

*Fix (concrete):*
- Introduce `FOceanicAmpSnapshot { Params; LOD; TopologyVer; SurfaceVer; Baseline_f; Ridge_f; Age_f; Pos_f; Mask_u32; InputHash; SnapshotId }` at **dispatch time** (your doc shows the struct and hashing sketch). Store **double baselines** too if CPU replay expects doubles.
- On readback, **drop** if any version/hash mismatches; otherwise write to `VertexAmplifiedElevation` and call `InvalidateOceanicFloatCache()` (a simple versioned cache).
- In parity tests, **force `bSkipCPUAmplification=false`** and **do bump `SurfaceDataVersion`** so rebuilds happen consistently; don’t conflate preview perf gating with parity runs.

**A2. Split/Merge determinism leak (Critical).**  
*Evidence:* Candidate selection by **iteration order** in `PlateTopologyChanges.cpp` was flagged; that ordering is not stable. Determinism can silently break for the same seed on different platforms/builds.  
*Fix:* Build a `TArray` of candidates with a comparator `(Score desc, PlateId asc, VertexId asc)` and select deterministically. Log the chosen ID and score for tests.

**A3. Ridge direction fallback is a trap (High).**  
*Evidence:* `ComputeRidgeDirection` returns zero/Z‑axis; paper’s oceanic amplification depends on **correct ridge orientation** for the transform‑fault striations. If anything routes through this by accident, Stage‑B looks wrong.  
*Fix:* Either `checkNoEntry()` or implement the cached **render‑space boundary‑tangent** approach and invalidate cache on LOD/retess. Your docs already describe the cache and invalidation needs.

**A4. Oceanic kernel must skip continental verts (High).**  
*Evidence:* GPU review recommends masking; present kernel likely overwrites everything. That diverges from CPU behavior and distorts continental relief.  
*Fix (USF):* Add `VertexPlateAssignments` + `PlatesCrustType` SRVs; early‑out to baseline if not oceanic. (Your doc contains an exact kernel sketch.)

**A5. Equirect seam preview (Medium).**  
*Evidence:* Current preview smooths at ±180°. Your coverage metric fix avoids **double counting**, but the texture sampling is still wrap‑blind.  
*Fix:* Option 1: convert preview to **cube‑sphere** tiles (CDLOD‑style). Option 2: add explicit seam duplicate columns and write atomically to both; ensure sampling uses `Wrap` at runtime but `Clamp` in tests to avoid sampling across the seam.

**A6. Float ↔ double precision tolerances (Medium).**  
*Evidence:* You adopted 0.5 m max error; good. Keep double precision in the service, and **quantize only at the boundary** to GPU/WPO readback. Add a specific **FloatVsDoubleParity** test for the CPU path.

---

### B. GPU / RenderGraph plumbing

**B1. Asynchronous readback & commandlet submission (Critical).**  
*Evidence:* The corrected plan uses `FRHIGPUBufferReadback` plus a **per‑job `FRenderCommandFence`** and a **pump function** that can *wait once* in automation, then poll. This avoids both stalls and “never submitted” graphs. Ensure you removed any lingering `FlushRenderingCommands()` in shipping paths.  
*Fix:* Keep the fence pattern. In tests: `ProcessPending…(true)` once → poll loop. In runtime/editor: **never wait**, only poll.

**B2. Shader permutations / feature level guards (High, easy win).**  
*Evidence:* You already gate SM5 in review notes; ensure it’s implemented in code (global shader `ShouldCompilePermutation`). Add a cross‑platform shader compilation test (DX12/Vulkan/Metal).

**B3. Threadgroup size cohesion (Medium).**  
*Evidence:* The review emphasizes keeping `[numthreads]` and C++ dispatch divisor in sync. Extract `constexpr ThreadGroupSize=64` on C++ side.

**B4. Shader module structure / path mapping (Low but tidy).**  
*Evidence:* You’re using **virtual shader path mapping** already; a stray `Shaders.Build.cs` under an editor module won’t act as a real module. Remove or move it; the mapping is sufficient.

**B5. VRAM budget + graceful fallback (Medium).**  
*Evidence:* Docs add a **165–180 MB** L7/L8 budget and minimum **2 GB VRAM** requirement; add an upfront allocation probe and **warn + fallback** to CPU when insufficient. Verify the release path calls `Release()` on exemplar atlas when toggling.

---

### C. Simulation & Stage‑B algorithms

**C1. Paper alignment — oceanic Stage‑B uses oriented *3D Gabor* noise; you approximate with directional Perlin (High, but acceptable if documented).**  
*Evidence:* Paper: ridge‑perpendicular transform faults via **oriented 3D Gabor**, age‑modulated near young crust. Implementation: “3D Gabor **approximation** using directional Perlin;” this can reduce the crisp, sparse streaking seen in the paper’s Figure 11.  
*Why it matters:* It’s a **look** difference; not a correctness fault.  
*Fix options:*  
- Keep the Perlin‑based version for base perf and document the deviation.  
- Add a “**True Gabor**” permutation (sparse kernel sum, low lobe count) for parity screenshots and high‑quality stills.

**C2. Continental Stage‑B parity (High, in progress).**  
*Evidence:* GPU path scaffolded; exemplar loader/`Texture2DArray` present; continental parity is failing in recent session logs (3.1% within tolerance, large max deltas), so the **indexing/weighting** or **baseline blend** is still off.  
*Fix:* Log the exemplar **layer indices + weights** per vertex (a few samples) on both CPU and GPU and compare; clamp and normalize weights, and verify **22 layers loaded** as your test expects. Add a unit test that checks array count and exemplar metadata before dispatch.

**C3. Erosion, dampening, sediment (Medium).**  
*Evidence:* Paper’s simple per‑step formulas for continental erosion, oceanic dampening, and trench sediment are part of Section 4.5. You’ve implemented dampening; erosion/sediment are partially pending per earlier docs (now marked complete in M5 summaries; ensure the implementation really uses the paper’s form).  
*Fix:* Validate your erosion/dampening exactly follow the exponential forms (constants `e_c`, `e_o`, `ε_s`) and add a numerical regression test over N steps that reproduces the analytic solution on a toy case.

---

### D. Performance bottlenecks & low‑risk optimizations

**D1. Retessellation threshold (High impact, very safe).**  
*Evidence:* Raise default to **45°** with a “High Accuracy (30°)” toggle; this reduces frequent rebuilds and step jitter for high velocities.

**D2. Skip CPU amplification in GPU preview (Done; verify parity harness ignores it).**  
*Evidence:* `bSkipCPUAmplification` and conditional `SurfaceDataVersion++` landed; great for perf, but never use these flags during parity runs (see A1).

**D3. Adjacency caching reuse (Medium).**  
*Evidence:* You’ve cached adjacency weights for dampening; reuse the same cache anywhere you compute boundary distances (multi‑source BFS) to keep ridge‑cache refills cheap.

**D4. Async mesh build trace noise (Low, done).**  
*Evidence:* Added `TRACE_CPUPROFILER_EVENT_SCOPE` to async tasks; keep it.

---

### E. Tests & CI gaps (to catch regressions early)

- **GPU/CPU Parity suite:** Oceanic parity test present; make continental parity **mandatory** once fixed. Under **NullRHI**, skip GPU tests cleanly (you already have the pattern).
- **Ridge‑direction invariance:** Extend `RidgeDirectionCacheTest` to assert equality between **render‑vertex tangents** and automation lookup across LOD changes and retess. (Your docs recommend this explicitly.)
- **Float/double parity:** Add the suggested `FloatVsDoubleParity` unit test (max delta < 1e‑3 m on a synthetic case).
- **Shader compilation test:** Keep the cross‑platform compilation test; it will catch `acos()` NaNs on Vulkan/Metal early.
- **CVar coverage:** Ensure `r.PlanetaryCreation.UseGPUAmplification` is registered in the editor build; your troubleshooting note covers this.

---

## Paper alignment — where you’re faithful vs. deviating (and why it matters)

- **Oceanic amplification**: The paper prescribes **oriented 3D Gabor** with age modulation near ridges; your approximation is acceptable for performance but can soften the transform‑fault look. If you want 1:1 parity screenshots, ship a “True Gabor” toggle and keep Perlin for runtime.
- **Continental amplification (exemplar)**: The paper’s pipeline assigns terrain type, orogeny‑specific exemplars, and aligns to local fold direction `f`; your exemplar loader and orientation logic exist, but GPU parity isn’t there yet — fix index/weight/rotation alignment and then lock a regression image set.
- **Erosion/dampening/sediment**: Use the simple exponential formulas; they’re intentionally cheap at 50 km coarse resolution. Don’t over‑design here; stay with the paper’s model unless you profile a specific artifact needing change.

---

## Milestones — tempering expectations (what each actually delivers)

> Use this with stakeholders; it sets the right targets.

**Milestone 6 — Stage‑B & terranes (in progress)**  
- **Expect:** Oceanic Stage‑B on CPU+GPU preview with ~0.5 m tolerance; **CPU** continental Stage‑B with exemplar blends; terrane extraction/reattachment passes tests; Level‑7 parity captured for *validation*, not interactive use; L5 stays under ~120 ms.  
- **Not yet:** Fully interactive L7 with continental GPU parity or async readbacks everywhere. Those move to M7.

**Milestone 7 — Presentation & polish**  
- **Expect:** **Async GPU preview** (no stalls), **normals pass** for both oceanic/continental Stage‑B, biome/material layering, cinematic capture UX; Level‑7 becomes usable for demos.

**Milestone 8 — Climate & hydrosphere coupling**  
- **Expect:** Coarse climate/sea‑level overlays for **interpretation**, not physical simulation.

**Milestone 9 — Shipping & cinematic polish**  
- **Expect:** Release‑ready build with console‑class budgets; GPU offloads hardened; docs/tutorials complete.

---

## Concrete patch set you can queue now

1) **Snapshot‑based Stage‑B (CPU/GPU)**  
   - Implement `FOceanicAmpSnapshot` (fields already sketched in your docs). On dispatch: fill floats from `GetOceanicAmplificationFloatInputs`, record versions + hash; on readback: **accept/drop by versions + hash**; on accept: copy → doubles and **InvalidateOceanicFloatCache()**. Enforce in parity tests.

2) **Mask continental verts in oceanic USF**  
   - Add SRVs `VertexPlateAssignments` and `PlatesCrustType`; early‑out with baseline when not oceanic. (Kernel sketch in your doc.)

3) **Automation fence submit**  
   - Keep `FRenderCommandFence` per job; in tests call `ProcessPending…(true)` once, then poll `IsReady()` until a deadline. Remove any accidental `FlushRenderingCommands()` on shipping paths.

4) **Deterministic merge ordering**  
   - Replace “first element” choice with sorted candidate array; add unit test that asserts same winner for the same seed across 3 platforms.

5) **Ridge direction cache enforcement**  
   - Replace `ComputeRidgeDirection` with `checkNoEntry()` (short term) or implement the render‑tangent derivation; invalidate on **LOD change / retess**.

6) **Seam preview mitigation (short term)**  
   - Duplicate seam columns and write both; ensure texture sampler uses Wrap at runtime and Clamp in tests. Keep the seam coverage metric you fixed.

7) **Tighten shader build matrix + tests**  
   - Add explicit SM5 guard and keep the compilation test for DX12/Vulkan/Metal.

---

## Minor but worthwhile

- Keep the **thread metadata** scopes to clean up Insights traces (already added).  
- Consolidate perf reports to a single **`Performance_M6.md`** as your doc suggests; there are conflicting L3 numbers in logs vs summaries.  
- Verify the **CVar** registration and parity tests’ **NullRHI skip** guards (you’ve documented both).

---

## Why these gaps matter for the paper

- The paper’s Stage‑B oceanic look depends on **ridge‑oriented noise** and **age‑based attenuation**. If ridge direction is wrong or continental verts are touched, the transform‑fault “barcode” degrades.
- The paper’s erosion/dampening are deliberately **simple exponentials** at coarse scale; deviating here adds cost without visual gain. Keep it simple and deterministic.
- Exemplar‑based continental detail expects **type classification + fold alignment**; parity failures there usually mean **indexing/weight/rotation** mismatches, not stochastic differences.

---

## Appendix — what I reviewed

- GPU preview plumbing, parity tests, and optimization notes (skip CPU amp, version gating, fences, CVar)  
- Code audit notes (ridge placeholder, non‑deterministic candidate selection, seam metrics)  
- Paper alignment docs & the transcribed paper sections for Stage‑B, erosion/dampening, and exemplar generation  
- Session log & milestone summaries for expectations (M6→M9)

*(Repo tree & docs index referenced earlier in the thread.)*

---

### Final call‑outs

- You’re **not** off‑base relative to the paper; the remaining deltas are mostly **implementation hygiene** (snapshot discipline, deterministic ordering) and **presentation fidelity** (seams, normals, exemplar alignment). Fix the five red items at the top and the rest falls into place.  
- Treat **Level‑7** as **validation tier** for M6 (good screenshots + parity CSV), not a perf target yet; your own roadmap and GPU system review already say exactly this.
