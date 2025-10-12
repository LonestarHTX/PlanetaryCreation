Michael — here’s a “no‑polish” technical review of the realignment you’ve committed/documented. I’m going to be blunt about what’s still risky, what’s merely noisy, and what looks solid. I’ll anchor key points to the files in your repo so the agents can jump straight to the right place.

---

### Addendum · 2025-10-13
- Snapshot discipline is now enforced in `UTectonicSimulationService`: oceanic readbacks only apply when `(LOD, TopologyVersion, SurfaceVersion, InputHash)` matches and the float SoA cache is invalidated immediately after acceptance.
- The new automation test `PlanetaryCreation.StageB.StageB_Parity_UsesSnapshot` mutates live crust-age data between dispatch and readback and fails if the parity path touches live buffers instead of the captured snapshot.
- Logs now surface accept/drop decisions explicitly (see `[StageB][GPU] Accepted/Dropping oceanic snapshot …` in `Saved/Logs/PlanetaryCreation.log`).

---

## Executive summary (prioritized)

**Blockers / must fix now**

1) **Snapshot discipline is only partially enforced → replay drift remains possible.**  
   Your own guidance mandates: (a) capture a **SnapshotId** and per‑input hashes at GPU dispatch, (b) **accept/drop readbacks** by `(LOD, TopologyVersion, SurfaceDataVersion)` **and input hashes**, and (c) **invalidate the float SoA cache** after write‑back. Pieces are implemented, but not universally applied across the controller + automation paths; parity can still drift if any step runs between baseline and replay.

2) **Ridge direction integration is still incomplete.**  
   You built the **render‑vertex boundary cache**, but `ComputeRidgeDirections()` hasn’t been wired to *consume* it consistently. That undermines transform‑fault orientation and the visual look near divergent boundaries (even if parity mechanics are correct). Finish the integration the docs spell out.

3) **GPU readback correctness in tests:** force one submit, then poll.  
   Runtime path is fence‑based, but automation still needs the **“submit once, then poll `IsReady()`”** pattern or it hangs/regresses to flushes. You documented the fix; ensure all tests use it.

**High impact (correctness & determinism)**

4) **Oceanic kernel should early‑out by crust mask everywhere (CPU+GPU parity).**  
   CPU amplification returns early for continental crust; mirror with a **mask SRV** on GPU (both compute and preview). It’s correctness and perf.

5) **Plate split/merge determinism:** you added a priority sort; verify **no TMap iteration** remains in the decision path and that secondary/tertiary keys are stable. (Looks good in the current patch—keep it.)

**Performance**

6) **Sediment & dampening dominate frame (14–25 ms).**  
   The plan to move to CSR/SoA neighbors + `ParallelFor` and better Insights hooks is the right lever; prioritize it because Stage B perf is already ~33–34 ms steady state and these passes blow your 90 ms/step budget on some runs.

**Operational / UX / documentation**

7) **CVar defaults & docs must be a single source of truth.**  
   Agents rely on `PaperDefaults`, `StageBProfiling`, `bSkipCPUAmplification`, `UseGPUHydraulic`. Keep the quick‑reference current with the actual runtime defaults (and call out that GPU hydraulic routes to CPU even when “on”).

---

## What’s improved (acknowledging progress—but not over‑praising)

- **Fence‑based async readback** and **automation submit‑once** guidance is in place; this is the right pattern and removes your reliance on `FlushRenderingCommands()`. Keep it everywhere.  
- **Stage‑B profiling logs** are back and showing healthy reuse of continental exemplar caches after the ridge cache serial fix. Good hygiene; wire those hash‑match lines into CI gates.  
- **Paper formulas are implemented faithfully** in CPU Stage‑B (age falloff, Gabor/gradient detail, oceanic‑only path). That’s your correctness baseline.

---

## Deep dive by area

### A) Correctness & determinism

**A1. Replay drift after GPU readback (blocker).**  
*Symptoms:* CPU‑B can differ from CPU‑A/GPU by more than float‑quantization after readback.  
*Root causes you already identified:*  
- **Age field mutation** between A and B (`ResetCrustAgeForSeeds()` in dampening).  
- **Float SoA cache** not invalidated after overwriting doubles on readback.  
- **Preview skip flag** changing version bumps / ordering.

*Hard requirements to close this out (make them non‑optional):*

- **Snapshot object at dispatch**  
  `FOceanicAmpSnapshot { Params, LOD, TopologyVer, SurfaceVer, Baseline_f, Ridge_f, Age_f, Pos_f, Mask_u32, InputHash, SnapshotId }`. You’ve described exactly this—now make *every* Stage‑B job use it.
- **Accept/drop on readback** by **versions + input hash**. Don’t write into doubles unless everything matches.
- **Invalidate float caches** when you do write back (increment a version; rebuild on next use).
- **Parity harness uses the snapshot** for CPU‑B (no service getters). You already sketched the replay helper; ship it and have automation assert that **hashes match** before comparing fields.

**A2. Ridge directions are not yet sourced from boundary cache.**  
The missing integration means transform‑fault direction can be wrong near ridges, especially when the age‑gradient fallback dominates. Hook `ComputeRidgeDirections()` to the **render‑vertex boundary cache** and only fallback in mid‑ocean. The pseudo‑patch in your doc is exactly what’s needed; make sure it runs *after* plate IDs are final and *before* Stage‑B. Also log cache hits/fallbacks per step so regressions are obvious.

**A3. Oceanic vs continental amplification parity.**  
CPU path exits early for continental; ensure GPU kernels (compute and preview) use a **mask SRV** and early‑out. This is explicitly called out in your risk list—implement it and add a unit test that counts continental vertices processed by the GPU path (should be zero).

**A4. Split/merge determinism (now mostly addressed).**  
The new **priority sort** (primary metric, then secondary, then stable tiebreakers) removes the TMap iteration order dependency. Keep it limited to one split per step and confirm you take the **first element of the sorted array** (you do). Add an automation test that ensures the selected `(PlateToSplit, BoundaryKey)` is identical under randomized insertion orders.

**A5. Readback race condition regression tests.**  
Your fix replaces unsafe “return to pool while in‑flight” logic and moves to fence‑guarded lifetime; add a test that intentionally invalidates a job mid‑frame and asserts **no pool reuse** until `IsFenceComplete()`. Also ensure commandlets skip GPU runs under NullRHI.

---

### B) Performance & memory

**B1. Stage‑B steady state ~33–34 ms @ LOD 7; sediment/dampening another 14–25 ms.**  
- Continue with **CSR neighbor lists** (packed SoA) for diffusion and dampening; this removes scattered memory walks and enables cheap `ParallelFor` passes. Track both **L1 MPKI** (Insights/Trace) and **per‑pass ms**; gate in CI with thresholds using your `[StageB][Profile]` log rows.  
- **Ridge/exemplar caches**: your “blend cache serial” tied to Stage‑B serial is smart; extend this to guard against unnecessary sample recomputation on CPU fallbacks (it sounds like you already did this—keep the `[Overrides]` log quiet).

**B2. Hashing cost vs value.**  
Use **XXH3_64** (or your FNV64 helper) over raw mem; you only need “good enough” avalanche for CI gating. The helper you proposed is sufficient for now; consider moving to `CityHash64` if you want a drop‑in.

**B3. Preview seam work.**  
Equirect write with ±π filtering is inherently lossy; migrate preview to **cube‑face** textures or a wrap‑aware scatter to kill seam artifacts. Your seam metrics/atomics are the right stopgap, but this should move off the debt list.

---

### C) Architectural & API surface

**C1. Single source of truth for runtime defaults.**  
Make the **Parameter Quick Reference** the canonical place (generated if possible). Right now, agents learn from multiple docs that:  
- `PaperDefaults=1` is the new default,  
- `StageBProfiling=1` is required for perf runs,  
- `UseGPUHydraulic=1` still routes to CPU,  
- `bSkipCPUAmplification=true` is set when preview is on.  
These are spread across multiple docs—centralize or generate to avoid skew.

**C2. Tighten the replay interface.**  
Expose `EnqueueStageB(snapshot)` and `TryAcceptReadback(snapshotId)` at the controller boundary. That quarantines “live service state” from replay code and eliminates accidental getters during CPUB. (Your docs already hint at this separation—codify it.)

**C3. Data access patterns.**  
In hot loops (ridge dir build, exemplar blends) avoid per‑call scans like `FindPlateByID` over a `TArray`. Store `TMap<int32, int32> PlateIdToIndex` (or dense `TArray` keyed by PlateID) to remove O(N) lookups that sneak into perf-critical code and tests. The unit test sample still shows a linear scan; make sure production code does not.

---

### D) Tests, tooling, and observability

**D1. CI gating on hash‑match and timing.**  
- Fail CI if **any** Stage‑B readback is accepted with mismatched `(versions || hash)`.  
- Parse `[StageB][Profile]` rows into a CSV artifact per run; gate on **p95** over N steps (not single outliers). You already called out “hash‑match CI gating”—ship it.

**D2. Parity harness hardening.**  
- Begin each run by logging snapshot rows like:  
  `[Parity] Snap=42 LOD=7 Topo=133 Surf=912 Hash=…`  
  Then log the first N delta lines only when `|Δ|>0.2–0.5 m` (tunable). Your doc even sets that guidance; implement the threshold to keep logs legible.

**D3. GPU automation under NullRHI.**  
Keep the **skip** guard (you documented it) and validate via a “NullRHI dry‑run” job in CI that asserts no GPU paths executed.

---

## Deviations from the paper (and why they matter)

- **Scope of amplification:** Paper applies detail to **oceanic crust** only. The CPU path honors this; ensure GPU kernels never touch continental vertices. If they do, you’ll see persistent shelf artifacts and amplitude bias that are not in the paper, affecting visual parity.  
- **Ridge orientation source:** The paper assumes correct transform‑fault orientation; your current incomplete ridge‑direction integration can flip or smear those structures, giving the “doesn’t look like the example” complaint even when amplitudes are numerically fine. Finish the boundary‑tangent sourcing.

---

## Concrete, PR‑sized tasks (ready to hand out)

**Task 1 — Enforce snapshot policy everywhere (2 PRs).**  
- **Core:** Add `FOceanicAmpSnapshot` (header), switch `ProcessPending…` to **accept/drop** on versions + hash, and **invalidate float caches** on accept.  
- **Tests:** Add `StageB_Parity_UsesSnapshot` that fails if any service getter sneaks into CPU‑B replay.

**Task 2 — Ridge direction integration (1 PR).**  
- Wire `ComputeRidgeDirections()` to `FRenderVertexBoundaryInfo`; add counters (`CacheHits`, `GradientFallback`) and a unit test that verifies near‑boundary tangents align (cosθ>0.9).

**Task 3 — GPU kernels honor crust mask (1 PR).**  
- Add `OceanicMask` SRV to compute + preview. Unit test: count continental verts processed = 0.

**Task 4 — Automation: submit‑once, then poll (1 PR).**  
- Update all GPU tests to call `ProcessPending…(true)` once; add a watchdog timeout. Add NullRHI skip.

**Task 5 — CSR neighbors for sediment/dampening (2 PRs).**  
- Data structure PR (CSR build + SoA), then algorithm PR (replace neighbor loops with CSR traversal + `ParallelFor`). Gate p95 in CI.

**Task 6 — Single‑source CVar defaults (1 PR).**  
- Generate `Docs/ParameterQuickReference.md` from a JSON manifest built at compile‑time; assert on startup that runtime defaults match the manifest.

---

## Quick checks your team can run today

1) **Hash right before GPU dispatch vs. right before CPUB replay.** If any of Baseline/Age/Ridge/Pos/Mask differ → you found your drift.  
2) **Disable dampening once** between A and B. If parity collapses to <0.1 m deltas, age mutation was the culprit.  
3) **Force snapshot replay** (no service reads) and compare CPU‑B vs GPU. If perfect → pipeline timing was the problem.

---

## Notes on your “why doesn’t it look like the example” symptom

- With the ridge‑direction placeholder/fallback and the GPU touching continental verts, the visual field *will* deviate even if mean error is tiny; you’ll get mis‑oriented transform faults and shelf artifacts—exactly the kind of mismatch that’s obvious by eye but subtle in metrics. Finish the ridge cache integration and mask the kernels; then compare again.

---

## Closing

You’ve put the right scaffolding in place; now close the loop: **make snapshots authoritative, wire ridge directions to the boundary cache, and keep GPU strictly oceanic.** That will eliminate the remaining parity/visual complaints and give you room to focus on the sediment/dampening hot path next.

If you want, I can mark up your controller functions with the exact **accept/drop** checks and the float‑cache version bump you described in the docs so an engineer can paste them in.
