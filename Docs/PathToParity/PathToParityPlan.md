# Path to Paper Parity

This document lays out the end-to-end execution plan required to return **PlanetaryCreation** to the visual and performance parity demonstrated in the *Procedural Tectonic Planets* paper. It assumes the current hot‑fix state (heightmap exporter stable, Stage B rescue instrumentation in place) and builds forward in well-defined phases. Each phase includes owners, deliverables, validation gates, and explicit hand-offs.

---

## Phase 0 — Baseline Guardrails (Complete)

> **Status:** ✅ Finished. These items form the safety net for all subsequent work and must remain green in CI.

 - Heightmap exporter tiling + Stage B rescue (100 % coverage, seam ≤0.5 m).
 - Heightmap export safety gates: default cap at 512×256 under NullRHI; pixel-count guard with explicit `--force-large-export` override; large resolutions use tiling path.
 - Known crash hazard documented: never approve 4096×2048 exports on dev hardware without explicit confirmation and guard flags.
- Stage B snapshot discipline (dispatch hashes, read-back accept/drop gates, float SoA invalidation).
- Quantitative metrics automation (`PlanetaryCreation.QuantitativeMetrics.Export`).
- Stage B heartbeat & rescue telemetry documented (`Docs/Validation/README.md`, `Docs/heightmap_export_review.md`).

**Regression hook:** `powershell.exe -ExecutionPolicy Bypass -File .\Scripts\RunMilestone3Tests.ps1 -ArchiveLogs` after every engine-side change.

**Checklist — Phase 0 (Complete)**
- [x] Heightmap exporter tiling + Stage B rescue (100% coverage, seam ≤ 0.5 m)
- [x] Heightmap export safety gates (NullRHI cap 512×256; tiling; force-large flag)
- [x] Stage B snapshot discipline (dispatch hashes, accept/drop gates)
- [x] Quantitative metrics automation in place
- [x] Stage B heartbeat/rescue telemetry documented

---

## Phase 1 — Visual Parity Restoration

### Phase 1.0 — Paper Profiling Baseline ✅ *Completed 2025-10-12*
- Capture Unreal Insights trace of current Stage B pipeline (LOD 7, real hardware).
- Recreate Figure 9 from the paper line-by-line; note any passes the paper is running on GPU vs CPU.
- Record results in `Docs/heightmap_export_review.md` before making changes.

**Checklist — Phase 1.0 (Complete)**
- [x] Insights trace at LOD 7 captured
- [x] Figure 9 re-created and annotated (GPU vs CPU passes)
- [x] Baseline recorded in Docs before changes

### Phase 1.1 — Ridge Direction Lifecycle & Coverage ✅ *Completed 2025-10-12*

| Owners | Summary | Deliverables | Definition of Done |
| --- | --- | --- | --- |
| STG (lead), ALG | Fully specify and implement the ridge tangent cache lifecycle; raise cache hit rate to ≥99 %. | Lifecycle spec, instrumentation, `StageB_RidgeDirectionCoverage` automation, log counters. | Cache hits ≥99 %, gradient fallback ≤1 %, motion fallback ≤0.1 %. Automation fails if thresholds missed. |

**Lifecycle Specification**
1. **Build:** on simulation reset, Voronoi rebuild, retessellation, LOD change, plate split/merge.
2. **Persist:** across simulation steps, terrane transport/orogeny, hydraulic erosion, Stage B amplification.
3. **Invalidate:** plate rifting, convergence surgery, render subdivision change, tessellation delta, exemplar atlas swap.
4. **Usage order:** Cached tangent → Age gradient → Plate motion. Counters logged per step: `[RidgeDir] CacheHit`, `GradientFallback`, `MotionFallback`.
5. **Monitoring & Gates:** Automation fails if cache_hit <99 %, gradient_fallback >1 %, or motion_fallback >0.1 %. Include fixtures for ridge triple-junctions and age discontinuities to exercise cache rebuild paths.

**Execution Notes**
- Implement cache rebuild scheduling relative to `ComputeRidgeDirections()`; ensure incremental updates during terrane extraction/reattachment:
  1. **Extraction:** mark affected vertex IDs as *stale* (retain cache entries).  
  2. **Transport:** keep snapshot tangents frozen while terranes move.  
  3. **Reattachment:** rebuild tangents for reattached vertices using new plate assignment.  
  4. **Validation:** log `[RidgeDir][Terrane]` pre/post reattachment; automation fails if post-hit-rate <90 %.
- Capture ridge tangent heatmaps pre/post fix; archive under `Docs/Validation/ParityFigures/`.

**Checklist — Phase 1.1 (Complete)**
- [x] Lifecycle spec implemented (build/persist/invalidate)
- [x] Cache hit ≥ 99%; gradient fallback ≤ 1%; motion fallback ≤ 0.1%
- [x] Terrane extract/reattach incremental updates validated (≥ 90% post-hit-rate)
- [x] Heatmaps archived under Docs/Validation/ParityFigures

### Phase 1.2 — Unified Stage B GPU Pipeline ✅ *Completed 2025-10-13*

| Owners | Summary | Deliverables | Definition of Done |
| --- | --- | --- | --- |
| STG (lead), GPU, VIS | Merge oceanic and continental amplification into a single GPU compute pipeline with mask routing. | `StageB_Unified.usf`, exemplar texture atlas/array, unified parameter struct, updated CPU dispatch. | CPU vs GPU parity delta <0.1 m on mixed crust test; transform-fault visuals smooth across continental margins; automation suite `PlanetaryCreation.StageB.UnifiedGPUParity` green. |

**Execution Notes**
1. Shader unification:
   - Integrate oceanic Gabor noise (current `OceanicAmplificationPreview.usf`) and continental exemplar sampling into `StageB_Unified.usf`.
   - Use a **split dispatch**: build compacted indirect-args buffers for oceanic vs continental work and launch separate kernels to avoid warp divergence; transition-zone handling (age <10 My) lives in the continental kernel and may short-circuit to the oceanic math.
   - Batch continental work by exemplar ID when possible to improve atlas locality; start with an 8×8 thread group and tune after profiling (persisting threads/work queues if clustering pays off).
   - **Transition Zone Strategy:**  
     - Option A (default): apply Gabor noise only for crustAge <10 My.  
     - Option B: blend Gabor/Exemplar (`weight = age/10`).  
     - Option C: exemplar with reduced amplitude (`scale = age/10`).  
     - Evaluate during Phase 1.0 profiling; document selection and rationale.
2. Exemplar data:
   - Pack catalogued SRTM90 exemplars into a texture atlas or array; stream to GPU.
   - Precompute per-exemplar normalization (mean/variance) and sampling metadata (LUTs, bounds); select mip levels by footprint; expose sampler bindings in `FStageB_UnifiedParameters`.
3. CPU fallback remains unchanged; GPU path ONLY executes when mask indicates oceanic/continental as appropriate.
4. Automation suite adds `PlanetaryCreation.StageB.UnifiedGPUParity` (CPU vs GPU comparison) and updates GPU preview parity.

**Validation**
- Run mixed continental/oceanic sphere parity test; CPU vs GPU height delta ≤0.1 m.
- Visual inspection of 1024×512 & 4096×2048 previews at continental margins; store before/after comparisons.
- Stage B logs report `[StageB][RescueSummary]` with Fail=0 and row reuse consistent with unified pipeline.

**Checklist — Phase 1.2 (Complete)**
- [x] Unified shader built and routed (oceanic + continental)
- [x] CPU/GPU parity < 0.1 m on mixed crust test
- [x] Previews captured at 1024×512 and 4096×2048
- [x] Rescue summary clean (Fail=0)

---

### Phase 1.3 — “Paper Ready” Preset ✅ *Completed 2025-10-13*

| Owners | Summary | Deliverables | Definition of Done |
| --- | --- | --- | --- |
| STG (lead), OPS | Provide a one-click preset that applies all paper-parity settings in the editor. | Toolbar/menu action, subsystem hook, documentation update. | Clicking “Paper Ready” sets paper defaults, enables Stage B amplification, clears hydraulic latch, rebuilds Stage B to Ready=1, and logs `[PaperReady] Applied`. |

**Execution Notes**
- Add a button/command in the Tectonic Tool window labeled “Paper Ready.”
- When triggered it must:
  1. Apply the paper defaults profile (seed, render subdivision/LOD, amplification toggles, CVars).
  2. Enable both oceanic & continental amplification; clear the STG‑04 hydraulic latch.
  3. Reset Stage B, run the readiness warm-up, and confirm `[StageB][Ready]` reports true.
  4. Disable any dev-only overrides (e.g., `ForceStageBGPUReplayForTests`).
  5. Emit `[PaperReady] Applied` in logs for automation hooks.
- Documentation: update `Docs/Automation_QuickStart.md` and tooling notes so parity validation references the preset.
- Optional: expose a commandlet wrapper (`Scripts/ApplyPaperReady.ps1`) for automated runs.

**Exit Criteria:** One click yields the paper parity configuration; automation detects the log entry; docs reference the new workflow.

**Checklist — Phase 1.3 (Complete)**
- [x] Paper Ready toolbar action in Tectonic Tool
- [x] Clears hydraulic latch and rebuilds Stage B to Ready=1
- [x] Logs `[PaperReady] Applied`
- [x] QuickStart/docs updated

---

### Phase 1.4 — Exemplar Fidelity Audit ✅ *Completed 2025-10-14*

| Owners | Summary | Deliverables | Definition of Done |
| --- | --- | --- | --- |
| ALG (lead), QLT | Prove Stage B can replay real SRTM exemplars without distortion. | Forced exemplar export scripts, comparison artifacts (`Docs/Validation/ExemplarAudit`), automation gate. | Stage B export matches the reference DEM within guardrails (mean ≤50 m, interior ≤100 m), automation hooked into CI, docs updated. |

**Results**
- Forced exemplar overrides now survive commandlet runs (PowerShell wrappers hydrate env vars via `ProcessStartInfo`).
- `Scripts/analyze_exemplar_fidelity.py` exposes guardrails (mean 50 m / interior 100 m, perimeter warning 750 m) and emits unmasked metrics by default.
- Automation test `PlanetaryCreation.StageB.ExemplarFidelity` exercises the forced run under NullRHI and warns on perimeter spikes.
- Final “Victory” artifacts: `O01_stageb_20251014_122639.csv`, `O01_metrics_20251014_VICTORY.csv`, `O01_comparison_20251014_VICTORY.png`. Docs refreshed in `Docs/Validation/ExemplarAudit/README.md`, `Docs/heightmap_export_review.md`, and the Phase 1.4 implementation log.

**Follow-ups**
- A single perimeter sample still reverts to the 228 m baseline (~1.4 km spike). Edge conditioning is scheduled under Phase 1.5.
- Oceanic exemplar parity, time-lapse durability, and weight/audit tooling graduate to Phase 1.5 so they ship alongside the Stage B hygiene work.

**Checklist — Phase 1.4 (Complete)**
- [x] Forced exemplar plumbing (wrappers + env) working in commandlets
- [x] Analyzer thresholds (mean ≤ 50 m; interior ≤ 100 m) exposed
- [x] Automation test `StageB.ExemplarFidelity` integrated
- [x] Artifacts/Docs updated under `Docs/Validation/ExemplarAudit`

---

### Phase 1.5 — Stage B Hygiene & Detail Recovery *(In Progress)*

| Owners | Summary | Deliverables | Definition of Done |
| --- | --- | --- | --- |
| STG (lead), ALG, QLT, OPS | Finish Stage B amplification polish: ridge/fold caches, anisotropic kernels, tuned erosion, and exemplar parity extensions. | STG-05‒08 implementations + validation PNGs, QLT-05‒07 automation, OPS rollout docs/scripts. | Free-run terrain (50–100 My) shows paper-level mountain/ridge detail, automation suites green, documentation/workflows updated. |

**Scope**
1. **Ridge tangent cache validation (STG-05)** — ≥99 % population, diagnostics logged, automation asserting cache hit thresholds.
2. **Fold direction & orogeny classes (STG-06)** — Generate classifications, log coverage, expose data for anisotropic kernels.
3. **Anisotropic amplification kernels (STG-07)** — Integrate continental/oceanic kernels, capture Day 5 PNG, ensure Stage B profile timings stay within budget.
4. **Post-amplification erosion tune (STG-08)** — Re-enable conservative hydraulic erosion once kernels validated; log metrics to prove anisotropy survives.
5. **Exemplar parity extensions** — Edge conditioning to eliminate perimeter spikes; run forced audits on additional exemplars (continental + oceanic) at LOD 6/7; execute time-lapse durability checks, cache weight logging, and PNG roundtrip validation.
   - DONE: Border conditioning via clamped bilinear sampling + unified UV epsilon on CPU/GPU; A01 validated under thresholds.
   - Verify normalization parity: decoder offset/scale and hypsometric mode match analysis; unify PNG16 decode constants across CPU/GPU.
   - Add weight‑sum diagnostics: log total and normalized weights, exemplar ID, and detail scale per sampled vertex when forced mode is active (dev-only sampling).
   - DONE: Runtime version logging: `[StageB][ExemplarVersion]` with ID, path, size, mtime; library fingerprint logged; assert forced‑ID presence in dev.
6. **Automation & metrics (QLT-05‒07)** — Ridge transect, orogeny alignment, CPU/GPU UV parity harness integrated into CI with the new guardrails.
7. **Tooling polish (OPS-01/02)** — Script enhancements (`ExportHeightmap4096.py`, analyzer wrappers) and design checklist updates to support wider rollout.
8. **Legacy GPU parity test alignment** — Update or retire `PlanetaryCreation.Milestone6.GPU.ContinentalParity` to use the unified parity harness and snapshot semantics; fail until <0.1 m delta.

**Dependencies**
- Ridge tangent cache lifecycle from Phase 1.1 must remain green.
- Exemplar audit tooling from Phase 1.4 (RunExportHeightmap512.ps1 / RunExemplarAnalyzer.ps1) is the baseline for additional captures.
- Coordinate with Phase 2 CSR work so shared data structures (SoA, amplification buffers) stay compatible.

**Exit Criteria**
- Stage B free-run “Paper Ready” workflow produces detailed ridges/mountains consistent with the paper after 50–100 My.
- Automation thresholds (mean 50 m / interior 100 m) hold across all forced exemplar suites without masking; perimeter spikes eliminated.
- LOD 6 and LOD 7 forced exports archived for at least one continental and one oceanic exemplar.
- Docs updated with new captures, metrics, and any tuned presets. Remaining gaps (if any) escalated before Phase 2 starts.

**Progress — 2025-10-14**
- Baseline reconfirmed: Milestone 3 suite PASS; Stage B heartbeat stable under NullRHI (OceanicCPU≈0 ms | ContinentalCPU≈0 ms | Total≈0.02 ms). Logs archived at `Saved/Logs/Automation/20251014_140536_Milestone3Suite.log` and `Saved/Logs/StageBHeartbeat-20251014-140614.log`.
- Initial audits: `O02` breached thresholds (mean≈112 m; interior≈1105 m; perimeter≈1105 m). `C01` missing from exemplar library (analyzer aborted).
- Library fix landed (see `Docs/Validation/A01_DIAGNOSTIC_CLOSURE.md`): downloaded 40 missing SRTM tiles; regenerated all 22 exemplars; 0.00% nodata; validation PASS across library. New validation tools added: `Scripts/validate_exemplar_png16.py`, `Scripts/inspect_tif.py`, `Scripts/regenerate_exemplar_png16.py`, enhanced `Scripts/convert_to_cog_png16.py`.
- Next actions: re‑audit known‑good IDs at LOD 6/512×256 under NullRHI — `A01` (continental) and `O01` (oceanic) — using `-ForceExemplar`; then select a valid continental alternative if needed. Track any residual perimeter warnings for edge‑conditioning task.

**Progress — 2025-10-14 (PM)**
- Re‑audited A01, O01, H01, A09 at LOD 6 / 512×256 (NullRHI) with `-ForceExemplar`; all runs logged `[StageB][ForcedApply]` and produced artifacts under `Docs/Validation/ExemplarAudit/`.
- Results summary:
  - A01: mean≈0.24 m (PASS); interior max≈4272 m (FAIL); perimeter max≈3497 m (FAIL); mask_valid_fraction=1.0.
  - O01: mean≈21.1 m (PASS); interior max≈1401 m (FAIL); perimeter max≈1172 m (FAIL); mask_valid_fraction=1.0.
  - H01: mean≈4.07 m (PASS); interior max≈4054 m (FAIL); perimeter max≈2776 m (FAIL); mask_valid_fraction=1.0.
  - A09: mean≈10.2 m (PASS); interior max≈4057 m (FAIL); perimeter max≈2618 m (FAIL); mask_valid_fraction=1.0.
- Interpretation: data coverage is complete (no nodata), means are within 50 m, but large interior/perimeter spikes violate guardrails. Focus areas: edge conditioning near tile borders/seams, exemplar normalization/LUT parity between CPU/GPU, blend‑weight sum/normalization checks, and runtime exemplar manifest/hash verification to confirm regenerated assets are loaded.

**Progress — 2025-10-14 (Edge Fix Validation)**
- Edge spike fix implemented and validated (clamped bilinear sampling + unified UV epsilon on CPU/GPU).
- A01 re‑audit (LOD 6 / 512×256, NullRHI): mean ≈ −0.61 m (PASS), interior max ≈ 99.99 m (PASS), mask_valid_fraction = 1.0; spikes reduced ~4 km → ~100 m.
- Diagnostics integrated: `[StageB][ExemplarVersion]` (with path/size/mtime), library fingerprint logged (0x74493b798db8b2ce), weight‑sum checks healthy (no errors).
- Report archived: `Docs/Validation/EDGE_SPIKE_FIX_VALIDATION.md` (implementation details, before/after metrics, hypsometric and slope histograms).
- Next: rerun O01/H01/A09 with the fix to confirm guardrails (interior ≤100 m; perimeter ≤750 m) and then advance to STG‑06/07/08.

**Progress — 2025-10-15 (Edge Fix Verification Batch)**
- O01/H01/A09 re‑audited (LOD 6 / 512×256, NullRHI) still violate edge guardrails despite A01 passing:
  - O01: mean ≈ −21.5 m (PASS); interior ≈ 1401 m (FAIL); perimeter ≈ 1159 m (FAIL); mask_valid_fraction = 1.0.
  - H01: mean ≈ 5.3 m (PASS); interior ≈ 4030 m (FAIL); perimeter ≈ 2793 m (FAIL); mask_valid_fraction = 1.0.
  - A09: mean ≈ −9.3 m (PASS); interior ≈ 4053 m (FAIL); perimeter ≈ 2618 m (FAIL); mask_valid_fraction = 1.0.
- NullRHI headless confirmed; exemplar manifest + library fingerprint logged; no `[StageB][BlendTrace]` detected (trace likely not enabled in wrapper).
- Immediate actions:
  - Re‑run analyzer with blend tracing enabled (set env `PLANETARY_STAGEB_TRACE_CONTINENTAL_BLEND=1` via wrapper; or `-TraceBlend` flag if supported) to capture weights around failing lon/lat.
- Inspect normalization parity (decode offset/scale, hypsometric mode) and confirm forced CPU path applies exemplar normalization prior to sampling/export.

**Progress — 2025-10-15 (All Exemplar Audits Pass)**
- Clean rebuild + diagnostics yielded full green across four tiles (LOD 6 / 512×256, NullRHI):
  - O01: mean ≈ 9.74 m; interior max ≈ 99.99 m — PASS
  - H01: mean ≈ 0.42 m; interior max ≈ 99.996 m — PASS
  - A09: mean ≈ 0.16 m; interior max ≈ 99.97 m — PASS
  - A01: mean ≈ −0.61 m; interior max ≈ 99.99 m — PASS (no regression)
- Runtime metadata parity checks: no mismatches; decode normalization verified; DetailScale within nominal range; library fingerprint logged.
- Artifacts updated under `Docs/Validation/ExemplarAudit/`; summary at `Docs/Validation/EXEMPLAR_VALIDATION_COMPLETE.md`.

**Progress — 2025-10-15 (STG‑06 Fold/Orogeny Implemented)**
- Per‑vertex fold direction and orogeny classification added and integrated post‑boundary updates; arrays exposed on service, history snapshot/restore updated.
- ParallelFor implementation with atomic counters; `[FoldDir]` coverage/timing logs emitted during longer runs.
- Sanity automation `PlanetaryCreation.Milestone6.FoldDirectionConvergence` added (non‑failing alert); Milestone 3 + heartbeat runs complete and logs archived.

**Checklist — Phase 1.5**
- [x] Verify edge fix across O01/H01/A09 (LOD 6, 512×256): mean ≤ 50 m, interior ≤ 100 m, perimeter ≤ 750 m, mask_valid_fraction = 1.0
- [x] Exemplar normalization parity: unify decode offset/scale + hypsometric mode (CPU/GPU/analyzer); add analyzer parity row
- [x] Weight‑sum diagnostics: log normalized weights; analyzer ingestion; gate with mean ±2% and min ≥ 0.9
- [x] STG‑06: Fold direction & orogeny classes (coverage logs, arrays exposed, sanity test green)
- [ ] STG‑07: Anisotropic amplification kernels (continental + oceanic) with validation captures and Stage B budget checks
- [ ] STG‑08: Post‑amplification hydraulic erosion (conservative), verify anisotropy survives; metrics logged
- [ ] Align/retire legacy `PlanetaryCreation.Milestone6.GPU.ContinentalParity` to unified parity harness (< 0.1 m)
- [ ] Forced exemplar audits on additional tiles (continental + oceanic) at LOD 6 and LOD 7; time‑lapse durability; cache weight logging; PNG round‑trip validation
- [ ] STG‑06: Fold direction & orogeny classes (coverage logs, data exposed for anisotropic kernels)
- [ ] STG‑07: Anisotropic amplification kernels (continental + oceanic) with validation captures and Stage B budget checks
- [ ] STG‑08: Post‑amplification hydraulic erosion (conservative), verify anisotropy survives; metrics logged
- [ ] QLT‑05/06/07: Ridge transect metric, orogeny belt alignment, CPU/GPU UV parity harness; integrate into CI
- [ ] Align/retire legacy `PlanetaryCreation.Milestone6.GPU.ContinentalParity` to unified parity harness (< 0.1 m)
- [ ] Docs/automation updates: validation figures, thresholds, QuickStart refresh; add new diagnostics to analyzer output and CI prechecks

---

### Phase 2.0 — Dampening Decision Gate

- **Trigger:** Complete Phase 1.0 profiling and confirm the paper’s dampening implementation (CPU vs GPU).
- **Decision Owner:** STG (lead) with ALG/GPU consultation.
- **Options:**
  1. **CPU CSR refit** — Target ≤8 ms. Lowest implementation cost.  
  2. **GPU port (with CSR)** — Target ≤5 ms. Higher complexity but matches paper if GPU-based.  
  3. **Hybrid** — CSR adjacency on CPU, dampening kernel on GPU.
- **Criteria:**  
  - If paper shows CPU dampening reaching 5 ms → pursue option 1.  
  - If paper confirms GPU dampening → pursue option 2.  
  - If unclear → prototype option 1 first; escalate to option 2 if target missed.
- **Documentation:** Record decision and rationale in `Docs/heightmap_export_review.md` under “Phase 2 Decision Log”.

**Checklist — Phase 2.0 (Gate)**
- [ ] Phase 1.0 profiling complete and reviewed
- [ ] Confirm paper’s dampening implementation (CPU vs GPU)
- [ ] Stakeholder decision (STG lead; ALG/GPU consulted)
- [ ] Option selected (CPU CSR / GPU / Hybrid) with targets
- [ ] Decision logged in Docs (Phase 2 Decision Log)

---

## Phase 2 — Performance Parity

| Workstream | Owners | Summary | Deliverables | Definition of Done |
| --- | --- | --- | --- | --- |
| CSR/SoA Neighbor Refit | ALG (lead), STG | Replace scattered neighbor loops (sediment + dampening) with CSR/SoA representation and `ParallelFor`. | CSR builder, refactored passes, Insights markers, perf report. | Stage B step time meets target table (below); automation fails on regression. |
| Performance Accounting | QLT, STG | Produce pass-by-pass performance table and monitor budget compliance. | Updated Stage B heartbeat telemetry, performance tables in docs. | Perf budget table maintained in `Docs/heightmap_export_review.md`; once targets met, Stage B frame budget satisfied. |

### Phase 2.1 — Continental GPU Optimisation

| Owners | Summary | Deliverables | Definition of Done |
| --- | --- | --- | --- |
| STG (lead), GPU, ALG | Reduce continental amplification cost (currently ~26 ms) via exemplar-routing optimisations. | Profiling report, exemplar scheduling improvements, shader updates, automation gates. | Continental pass ≤20 ms with no parity regression; automation asserts <0.1 m CPU/GPU delta. |

**Execution Notes**
- Profile current continental GPU kernel (Nsight, Insights) to identify bottlenecks (texture cache misses, divergence).
- Introduce exemplar batching: cluster work by exemplar ID / mip, drive persistent-thread work queues if necessary.
- Evaluate alternate sampling strategies (gather4, manual filtering) and normalization LUT usage.
- Add counters for exemplar cache hits/miss, reuse metrics, and log them in `[StageB][Profile]`.
- Extend automation with `PlanetaryCreation.StageB.ContinentalGPUProfiler` (non-blocking alert) and fail if CPU/GPU height delta exceeds 0.1 m.

### Current Test Hardware *(record during Phase 1.0 — source of truth: Docs/heightmap_export_review.md)*

| Component | Spec | Paper Reference |
| --- | --- | --- |
| GPU | NVIDIA GeForce RTX 3080 (10 GB, driver 32.0.15.8142) | GTX 1080 (8 TFLOPS FP32) |
| CPU | AMD Ryzen 9 7950X (16 cores / 32 threads @ 4.5 GHz) | Paper era ≈ i7‑6700K |
| RAM | 64 GB DDR5 | ≥16 GB |
| OS | Windows 10 Pro (build 26100) | Paper likely Windows 10 |

> **Normalization:** Scale performance expectations relative to GTX 1080 when hardware differs (e.g., RTX 3060 ≈1.5× GTX 1080).

### Target Budget Table (to be updated as work progresses)

| Pass | Current (LOD 7) | Target (LOD 7) | Paper (LOD 7, GTX 1080) | Notes |
| --- | --- | --- | --- | --- |
| Oceanic Amplification (GPU) | 8 ms | 8 ms | ~10 ms (est.) | Requires unified GPU pass (Phase 1.2). |
| Continental Amplification (GPU) | 23 ms | 20 ms | ~12 ms (est.) | Investigate exemplar sampling cache; may need GPU optimizations. |
| Hydraulic Erosion (CPU) | 1.7 ms | 1.5 ms | <2 ms | Acceptable; monitor. |
| Oceanic Dampening (CPU) | 24–25 ms | 5 ms (CSR + GPU?) | ~5 ms | Decision point: move to GPU or heavily optimize CPU. |
| Sediment Diffusion (CPU) | 14–19 ms | 5 ms (CSR) | ~4 ms | CSR + `ParallelFor` required. |
| **Total Stage B** | **70–77 ms** | **≤40 ms** | **~33 ms** | Validate target hardware equivalence (paper used GTX 1080). |

**Action Items Before Implementation**
- Validate whether the paper’s dampening is GPU-based; contact authors or re-analyze text if necessary.
- Profile continental exemplar sampling to identify GPU bottlenecks (texture fetch vs compute).

### Execution Notes
1. **Data Structure PR:** Build CSR neighbor lists at simulation reset and whenever topology changes. Maintain separate arrays for oceanic/continental to reduce branching.
2. **Algorithm PR:** Implement diffusion/dampening as **Jacobi ping-pong** (two buffers) using the CSR adjacency; this guarantees deterministic ordering under `ParallelFor`. Remove per-vertex TMap lookups and dynamic allocations. (If GPU path later requires color sets, reuse the same ping-pong structure.)
3. Instrumentation & Invariants:
   - Add Unreal Insights markers: `[StageB][CSR] Sediment`, `[StageB][CSR] Dampening`.
   - Update `[StageB][Profile]` logs with CSR timings.
   - Add CI checks for **mass conservation** and **L∞ error** against a high-precision reference step.
4. Documentation & Automation:
   - Update perf tables in `Docs/heightmap_export_review.md`.
   - Extend `Scripts/RunStageBHeartbeat.ps1` to report CSR metrics.
   - Automation guard fails if passes exceed table targets or invariant checks trip.

**Exit Criteria:** Insights trace shows Stage B frame budget compliant with target table; automation fails on regressions; perf documentation updated.

**Checklist — Phase 2**
- [ ] CSR/SoA neighbor refit implemented (sediment + dampening)
- [ ] Deterministic Jacobi ping‑pong kernels + ParallelFor
- [ ] Mass conservation and L∞ checks in CI
- [ ] Stage B heartbeat extended with CSR timings
- [ ] Performance table updated; targets met (see budget table)

---

## Phase 3 — Automation & Export Tooling

| Workstream | Owners | Summary | Deliverables | Definition of Done |
| --- | --- | --- | --- | --- |
| Python API Coverage | STG | Expose all heightmap export & Stage B controls to Python. | `UTectonicSimulationService` Python bindings (e.g., `set_allow_unsafe_export`, `force_stage_b_rebuild`, `get_stage_b_status`). | Export scripts avoid direct CVar hacking; automation uses subsystem methods. |
| Export Script Templates | OPS | Provide canonical scripts for common export resolutions. | `Scripts/ExportHeightmap_512.py`, `_1024.py`, `_2048.py`, `_4096.py` (with CVar handling, logging). | Running `python Scripts/ExportHeightmap_2048.py` performs a full export with documented expectations. |
| Automation Command Builder | OPS | Simplify UnrealEditor-Cmd usage (CVars, `-TestExit`, logging). | `Scripts/Invoke-UnrealAutomation.ps1` that handles CVars, log capture, and test filters. | Automations triggered with a single command; no manual escaping. |
| Documentation Unification | OPS | Centralize automation/export instructions. | `Docs/Automation_QuickStart.md` with tested examples; other docs link here. | All docs point to single quick-start; instructions match scripts. |

**Execution Notes**
- Attempt Python binding exposure for the subsystem methods (time-box to 3 working days).  
- **Fallback:** If bindings are still blocked after day 3, pivot Phase 3 deliverables to PowerShell/Python wrapper scripts that encapsulate CVar management and command invocation; document the decision in the plan tracker.
- Build a small CLI test suite that exercises the wrapper scripts (export resolutions, automation filters) and run it in CI to catch regressions.
- Memory safety for long runs: execute Milestone suites in batches (e.g., M3, then M4, then M5 subsets, then M6 GPU subsets) to avoid OOM; increase paging file size on CI agents where needed. Document run profiles in `Docs/Automation_QuickStart.md`.
- Integrate exemplar data validation tools into CI prechecks for library updates: `Scripts/validate_exemplar_png16.py`, `Scripts/inspect_tif.py`, `Scripts/regenerate_exemplar_png16.py`, and the enhanced `Scripts/convert_to_cog_png16.py` (fail on >10% nodata or mismatched metadata).

**Exit Criteria:** Common automation/export tasks are single-command operations; teams no longer hand-edit CVars or command lines.

**Checklist — Phase 3**
- [ ] Python bindings for key subsystem methods (or fallback triggered)
- [ ] Export script templates for 512/1024/2048/4096 with CVars/logging
- [ ] Invoke‑UnrealAutomation.ps1 wrapper (CVars, -TestExit, logs)
- [ ] Automation QuickStart unified and verified
- [ ] Exemplar validation prechecks integrated in CI (fail on nodata/metadata mismatch)

---

## Phase 4 — Parity Verification & Sign-off

| Workstream | Owners | Summary | Deliverables | Definition of Done |
| --- | --- | --- | --- | --- |
| Visual Re-capture | VIS, ALG | Produce updated parity figures mirroring the paper. | PNG comparisons + commentary stored under `Docs/Validation/ParityFigures/`. | Numerical tolerances (below) met; reviewers sign off. |
| Quantitative Metrics Refresh | QLT | Regenerate metrics after final changes, compare to baseline. | New CSV snapshot + changelog entry in `Docs/Validation/README.md`. | Metrics fall within tolerance bands. |
| Performance Report | STG, QLT | Summarize Stage B timings post-refit. | Insights summary + textual report attached to `Docs/heightmap_export_review.md`. | Stage B step time ≤40 ms; full tick ≤150 ms (40–60 ms treated as yellow alert). |
| Final Review | MNG (lead), All | Verify goals, archive decisions, close plan. | Addendum in `Docs/realignment_review.md`, checklist in `Docs/PlanningAgentPlan.md` marked complete. | All deliverables archived; no open parity tasks. |

### Phase 4.0 — SSIM Baseline Calibration
- Measure SSIM between the published paper figure (PNG extracted from PDF) and its highest-quality available copy.  
- If the paper asset is lossy (SSIM <0.98), reduce the parity SSIM target by 0.03 (e.g., baseline 0.93 ⇒ target 0.90).  
- Document calibration results in `Docs/Validation/ParityFigures/README.md`.

### Acceptance Criteria (Numerical)

**Visual Parity**
- 1024×512 export: Mean absolute elevation delta vs reference (paper Figure 8 or forced exemplar) < 2 grayscale levels (~16 m).
- 4096×2048 export: SSIM ≥ calibrated target from Phase 4.0 (default 0.95 if paper assets are lossless).
- Seam continuity: equirectangular seam/pole deltas under the continuity threshold used by `FHeightmapSeamContinuityTest`.
- Transform faults clearly visible (≥3 confirmed) along major ridges at 1024×512 (qualitative checklist archived).
- Non-blocking alert: surface if metrics creep within 10% of thresholds to prompt manual review.

**Quantitative Metrics**
- Ridge/trench length ratio: 1.8 ± 0.2 (paper ≈1.9).
- Hypsometric curve: mean absolute deviation < 5% vs paper Figure 7.
- Terrane area preservation: < 5% drift over extract→transport→reattach; belt/orogeny alignment tests pass.

**Performance**
- Stage B step time (LOD 7): ≤ 40 ms on current hardware (normalize against GTX 1080 if needed). Treat 40–60 ms as yellow alert but >40 ms blocks Phase 4 sign‑off.
- Full simulation tick: ≤ 150 ms.

**Automation**
- Milestone 3 suite: 100% pass.
- Unified GPU parity: 100% pass, < 0.1 m CPU/GPU delta; legacy `…GPU.ContinentalParity` aligned or retired.
- Exemplar fidelity: mean ≤ 50 m; interior ≤ 100 m; mask_valid_fraction = 1.0; perimeter spikes eliminated post edge‑conditioning.
- Stage B rescue not triggered in final parity runs (Fail=0); CI fails on any `[StageB][RescueSummary]` with `Fail > 0` or `FallbackAttempts > 0`.
- CI executes suites in memory‑safe batches; GPU suites gated behind explicit allow flag.

### Validation Checklist
- ✅ Milestone 3 suite + supervised exports (512×256, 1024×512, 2048×1024, 4096×2048) with logs archived.
- ✅ Stage B heartbeat (`Scripts/RunStageBHeartbeat.ps1 -ThrottleMs 50`) showing Ready=1 and CSR timings.
- ✅ Quantitative metrics automation passing (with tolerances enforced).
- ✅ Performance metrics meet numerical targets.
- ✅ Visual comparisons archived with tolerance calculations.

**Phase 4 Closure Checklist** *(tracked in `Docs/PlanningAgentPlan.md`)*  
☐ Phase 1.1: Ridge cache hit rate ≥99% (logs + automation; heatmaps archived)  
☐ Phase 1.2: Unified GPU parity delta <0.1 m (automation green)  
☐ Phase 1.3: “Paper Ready” preset used in captures; log markers present  
☐ Phase 1.4: Exemplar fidelity thresholds met; artifacts under `Docs/Validation/ExemplarAudit/`  
☐ Phase 1.5: Hygiene complete; perimeter spikes removed; ridge/fold/anisotropy validated  
☐ Phase 2: Performance targets met (table updated, automation green)  
☐ Phase 3: One‑command export/automation workflows operational (wrapper tests green)  
☐ Phase 4.0: SSIM calibration recorded; parity gates adjusted if required  
☐ Phase 4: Visual parity (pixel delta, SSIM) documented; quantitative metrics within tolerance (CSV archived)  
☐ Phase 4: Performance report (Insights summary) published; final addendum in `Docs/realignment_review.md`

**Exit Criteria:** Checklist above completed and signed off; parity appendix, performance report, and addendum archived. Milestone formally closed once recorded in PlanningAgentPlan.md.

**Checklist — Phase 4 (Execution)**
- [ ] Visual re‑capture complete; figures archived and meet gates (pixel delta/SSIM)
- [ ] Quantitative metrics refreshed; CSV updated and within tolerances
- [ ] Performance report (Insights + table) attached
- [ ] Final review addendum written; all checklists closed in `Docs/PlanningAgentPlan.md`

---

## Phase Dependencies & Parallelism

```
Phase 0 (complete)
   ├─> Phase 1.1 (Ridge Lifecycle) ──┐
   │      └─> Phase 1.2 (Unified GPU) ──> Phase 1.5 (Stage B Hygiene) ──┐
   │                                       └─> Phase 4 (Parity Verification)
   │
   └─> Phase 2 (CSR Performance) ────────────────┘
          └─> Phase 4 (Performance Check)

Phase 3 (Automation Tooling) –– runs in parallel, supports Phases 1.5 & 4
```

**Critical Path:** Phase 1.1 → Phase 1.2 → Phase 1.5 → Phase 4 (visual/GPU parity).  
**Parallel Work:** Phase 2 can begin after Phase 1.1 plans are defined (ensure CSR changes don’t break ridge cache or Stage B hygiene assumptions).  
**Support Work:** Phase 3 improves tooling; not blocking but reduces friction for Phases 1.5 and 4.

--- 

## Cross-Phase Practices

- **Automation Discipline:** Run `Scripts/RunMilestone3Tests.ps1 -ArchiveLogs` and supervised exports after each major PR. Archive logs under `Saved/Logs/Automation`. Keep `Docs/agent_logs/MNG-Guide.md` current.
- **Telemetry Logging:** Preserve `[StageB][RescueSummary]`, `[StageB][Profile]`, ridge diagnostics, and CSR timings in production logs.
- **Artifact Storage:** All parity figures, CSVs, and Insights traces live under `Docs/Validation/` (with dated filenames). For any export above the NullRHI baseline, add a note about guardrails used (tiling, force flags) and any risks.
- **Communication:** Weekly (or ad-hoc) sync using the plan tracker in `Docs/PlanningAgentPlan.md`; ensure blockers/risks logged.
- **Versioning Discipline:** Maintain a single `StageBTopologyVersion` / parameter hash that all caches (ridge, CSR, unified GPU buffers) validate before use.

---

## Key Risks & Mitigations

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Ridge cache integration introduces new fallbacks | Visual regressions, parity drift | Add coverage automation & counters; run supervised exports before merging. |
| CSR refit destabilises Stage B | Performance or correctness regressions | Stage rollout via feature flag; capture before/after Insights; involve QLT early. |
| Unified GPU pipeline divergence | Poor occupancy / cache thrash | Use split dispatch + exemplar clustering; profile with Nsight/Insights before shipping. |
| Automation tooling regressions | Broken export/automation scripts | Maintain wrapper test suite; document scripts in Automation QuickStart; fall back to manual commands if wrappers fail. |
| Time overruns | Schedule slip | Keep phases strictly scoped; gate merges with automation + doc sign-off. |

---

## Tailwind Summary

We’ve already solved the unstable foundations (Stage B rescue, automation, quantitative metrics). The remaining push is focused: finish ridge visuals, enforce GPU/CPU parity, bring Stage B perf back under budget, and lock the operational tooling so it never drifts again. Follow the phases in order, keep the guardrails green, and we land exactly where the paper sets the bar. Let’s execute. 
