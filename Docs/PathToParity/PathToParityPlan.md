# Path to Paper Parity

This document lays out the end-to-end execution plan required to return **PlanetaryCreation** to the visual and performance parity demonstrated in the *Procedural Tectonic Planets* paper. It assumes the current hot‑fix state (heightmap exporter stable, Stage B rescue instrumentation in place) and builds forward in well-defined phases. Each phase includes owners, deliverables, validation gates, and explicit hand-offs.

---

## Phase 0 — Baseline Guardrails (Complete)

> **Status:** ✅ Finished. These items form the safety net for all subsequent work and must remain green in CI.

- Heightmap exporter tiling + Stage B rescue (100 % coverage, seam ≤0.5 m).
- Stage B snapshot discipline (dispatch hashes, read-back accept/drop gates, float SoA invalidation).
- Quantitative metrics automation (`PlanetaryCreation.QuantitativeMetrics.Export`).
- Stage B heartbeat & rescue telemetry documented (`Docs/Validation/README.md`, `Docs/heightmap_export_review.md`).

**Regression hook:** `powershell.exe -ExecutionPolicy Bypass -File .\Scripts\RunMilestone3Tests.ps1 -ArchiveLogs` after every engine-side change.

---

## Phase 1 — Visual Parity Restoration

### Phase 1.0 — Paper Profiling Baseline
- Capture Unreal Insights trace of current Stage B pipeline (LOD 7, real hardware).
- Recreate Figure 9 from the paper line-by-line; note any passes the paper is running on GPU vs CPU.
- Record results in `Docs/heightmap_export_review.md` before making changes.

### Phase 1.1 — Ridge Direction Lifecycle & Coverage

| Owners | Summary | Deliverables | Definition of Done |
| --- | --- | --- | --- |
| STG (lead), ALG | Fully specify and implement the ridge tangent cache lifecycle; raise cache hit rate to ≥99 %. | Lifecycle spec, instrumentation, `StageB_RidgeDirectionCoverage` automation, log counters. | Cache hits ≥99 %, gradient fallback ≤1 %, motion fallback ≤0.1 %. Automation fails if thresholds missed. |

**Lifecycle Specification**
1. **Build:** on simulation reset, Voronoi rebuild, retessellation, LOD change, plate split/merge.
2. **Persist:** across simulation steps, terrane transport/orogeny, hydraulic erosion, Stage B amplification.
3. **Invalidate:** plate rifting, convergence surgery, render subdivision change, tessellation delta, exemplar atlas swap.
4. **Usage order:** Cached tangent → Age gradient → Plate motion. Counters logged per step: `[RidgeDir] CacheHit`, `GradientFallback`, `MotionFallback`.
5. **Monitoring:** `[StageB][RidgeDiag]` logs every step; automation suite fails if cache hit <95 % or motion fallback >0.5 %.

**Execution Notes**
- Implement cache rebuild scheduling relative to `ComputeRidgeDirections()`; ensure incremental updates during terrane extraction/reattachment.
- Capture ridge tangent heatmaps pre/post fix; archive under `Docs/Validation/ParityFigures/`.

### Phase 1.2 — Unified Stage B GPU Pipeline

| Owners | Summary | Deliverables | Definition of Done |
| --- | --- | --- | --- |
| STG (lead), GPU, VIS | Merge oceanic and continental amplification into a single GPU compute pipeline with mask routing. | `StageB_Unified.usf`, exemplar texture atlas/array, unified parameter struct, updated CPU dispatch. | CPU vs GPU parity delta <0.1 m on mixed crust test; transform-fault visuals smooth across continental margins; automation suite `PlanetaryCreation.StageB.UnifiedGPUParity` green. |

**Execution Notes**
1. Shader unification:
   - Integrate oceanic Gabor noise (current `OceanicAmplificationPreview.usf`) and continental exemplar sampling into `StageB_Unified.usf`.
   - Route by crust mask: `Oceanic → Gabor`, `Continental → exemplar blend`, handle transition zones (age <10 My).
2. Exemplar data:
   - Pack catalogued SRTM90 exemplars into a texture atlas or array; stream to GPU.
   - Expose sampler bindings in `FStageB_UnifiedParameters`.
3. CPU fallback remains unchanged; GPU path ONLY executes when mask indicates oceanic/continental as appropriate.
4. Automation suite adds `PlanetaryCreation.StageB.UnifiedGPUParity` (CPU vs GPU comparison) and updates GPU preview parity.

**Validation**
- Run mixed continental/oceanic sphere parity test; CPU vs GPU height delta ≤0.1 m.
- Visual inspection of 1024×512 & 4096×2048 previews at continental margins; store before/after comparisons.
- Stage B logs report `[StageB][RescueSummary]` with Fail=0 and row reuse consistent with unified pipeline.

---

## Phase 2 — Performance Parity

| Workstream | Owners | Summary | Deliverables | Definition of Done |
| --- | --- | --- | --- | --- |
| CSR/SoA Neighbor Refit | ALG (lead), STG | Replace scattered neighbor loops (sediment + dampening) with CSR/SoA representation and `ParallelFor`. | CSR builder, refactored passes, Insights markers, perf report. | Stage B step time meets target table (below); automation fails on regression. |
| Performance Accounting | QLT, STG | Produce pass-by-pass performance table and monitor budget compliance. | Updated Stage B heartbeat telemetry, performance tables in docs. | Perf budget table maintained in `Docs/heightmap_export_review.md`; once targets met, Stage B frame budget satisfied. |

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
2. **Algorithm PR:** Rework sediment and dampening loops to use CSR with `ParallelFor` (balanced chunking). Remove per-vertex TMap lookups and dynamic allocations.
3. Instrumentation:
   - Add Unreal Insights markers: `[StageB][CSR] Sediment`, `[StageB][CSR] Dampening`.
   - Update `[StageB][Profile]` logs with CSR timings.
4. Documentation & Automation:
   - Update perf tables in `Docs/heightmap_export_review.md`.
   - Extend `Scripts/RunStageBHeartbeat.ps1` to report CSR metrics.
   - Automation guard fails if passes exceed table targets.

**Exit Criteria:** Insights trace shows Stage B frame budget compliant with target table; automation fails on regressions; perf documentation updated.

---

## Phase 3 — Automation & Export Tooling

| Workstream | Owners | Summary | Deliverables | Definition of Done |
| --- | --- | --- | --- | --- |
| Python API Coverage | STG | Expose all heightmap export & Stage B controls to Python. | `UTectonicSimulationService` Python bindings (e.g., `set_allow_unsafe_export`, `force_stage_b_rebuild`, `get_stage_b_status`). | Export scripts avoid direct CVar hacking; automation uses subsystem methods. |
| Export Script Templates | OPS | Provide canonical scripts for common export resolutions. | `Scripts/ExportHeightmap_512.py`, `_1024.py`, `_2048.py`, `_4096.py` (with CVar handling, logging). | Running `python Scripts/ExportHeightmap_2048.py` performs a full export with documented expectations. |
| Automation Command Builder | OPS | Simplify UnrealEditor-Cmd usage (CVars, `-TestExit`, logging). | `Scripts/Invoke-UnrealAutomation.ps1` that handles CVars, log capture, and test filters. | Automations triggered with a single command; no manual escaping. |
| Documentation Unification | OPS | Centralize automation/export instructions. | `Docs/Automation_QuickStart.md` with tested examples; other docs link here. | All docs point to single quick-start; instructions match scripts. |

**Exit Criteria:** Common automation/export tasks are single-command operations; teams no longer hand-edit CVars or command lines.

---

## Phase 4 — Parity Verification & Sign-off

| Workstream | Owners | Summary | Deliverables | Definition of Done |
| --- | --- | --- | --- | --- |
| Visual Re-capture | VIS, ALG | Produce updated parity figures mirroring the paper. | PNG comparisons + commentary stored under `Docs/Validation/ParityFigures/`. | Numerical tolerances (below) met; reviewers sign off. |
| Quantitative Metrics Refresh | QLT | Regenerate metrics after final changes, compare to baseline. | New CSV snapshot + changelog entry in `Docs/Validation/README.md`. | Metrics fall within tolerance bands. |
| Performance Report | STG, QLT | Summarize Stage B timings post-refit. | Insights summary + textual report attached to `Docs/heightmap_export_review.md`. | Stage B step time ≤90 ms; full tick ≤150 ms. |
| Final Review | MNG (lead), All | Verify goals, archive decisions, close plan. | Addendum in `Docs/realignment_review.md`, checklist in `Docs/PlanningAgentPlan.md` marked complete. | All deliverables archived; no open parity tasks. |

### Acceptance Criteria (Numerical)

**Visual Parity**
- 1024×512 export: Mean pixel delta vs reference (paper Figure 8) < 2 grayscale levels (~16 m elevation).
- 4096×2048 export: SSIM > 0.95; transform faults clearly visible (≥3 faults confirmed).

**Quantitative Metrics**
- Ridge/trench ratio: 1.8 ± 0.2.
- Hypsometric curve: mean absolute deviation <5 % vs paper Figure 7.
- Terrane drift: ≥1 extracted terrane per 50 My simulation; drift ≤5 %.

**Performance**
- Stage B step time (LOD 7): ≤90 ms on reference hardware (GTX 1080-equivalent).
- Full simulation tick: ≤150 ms.

**Automation**
- Milestone 3 suite: 100 % pass.
- Milestone 6 GPU parity: 100 % pass, height delta <0.1 m.
- Stage B rescue not triggered in final parity runs (Fail=0).

### Validation Checklist
- ✅ Milestone 3 suite + supervised exports (512×256, 1024×512, 2048×1024, 4096×2048) with logs archived.
- ✅ Stage B heartbeat (`Scripts/RunStageBHeartbeat.ps1 -ThrottleMs 50`) showing Ready=1 and CSR timings.
- ✅ Quantitative metrics automation passing (with tolerances enforced).
- ✅ Performance metrics meet numerical targets.
- ✅ Visual comparisons archived with tolerance calculations.

**Exit Criteria:** Updated parity appendix (visual + numeric), performance report, and `Docs/realignment_review.md` addendum describing outcomes and future work. Once complete, the parity milestone is officially closed.

---

## Phase Dependencies & Parallelism

```
Phase 0 (complete)
   ├─> Phase 1.1 (Ridge Lifecycle) ──┐
   │      └─> Phase 1.2 (Unified GPU) ──┐
   │              └─> Phase 4 (Parity Verification)
   │
   └─> Phase 2 (CSR Performance) ─────┘
          └─> Phase 4 (Performance Check)

Phase 3 (Automation Tooling) –– runs in parallel, supports Phase 4
```

**Critical Path:** Phase 1.1 → Phase 1.2 → Phase 4 (visual/GPU parity).  
**Parallel Work:** Phase 2 can begin after Phase 1.1 plans are defined (ensure CSR changes don’t break ridge cache).  
**Support Work:** Phase 3 improves tooling; not blocking but reduces friction for Phase 4.

--- 

## Cross-Phase Practices

- **Automation Discipline:** Run `Scripts/RunMilestone3Tests.ps1 -ArchiveLogs` and supervised exports after each major PR. Archive logs under `Saved/Logs/Automation`. Keep `Docs/agent_logs/MNG-Guide.md` current.
- **Telemetry Logging:** Preserve `[StageB][RescueSummary]`, `[StageB][Profile]`, ridge diagnostics, and CSR timings in production logs.
- **Artifact Storage:** All parity figures, CSVs, and Insights traces live under `Docs/Validation/` (with dated filenames).
- **Communication:** Weekly (or ad-hoc) sync using the plan tracker in `Docs/PlanningAgentPlan.md`; ensure blockers/risks logged.

---

## Key Risks & Mitigations

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Ridge cache integration introduces new fallbacks | Visual regressions, parity drift | Add coverage automation & counters; run supervised exports before merging. |
| CSR refit destabilises Stage B | Performance or correctness regressions | Stage rollout via feature flag; capture before/after Insights; involve QLT early. |
| CVar manifest drift | Ops confusion, mismatched automation | Enforce manifest validation at startup; generate docs auto-magically. |
| Time overruns | Schedule slip | Keep phases strictly scoped; gate merges with automation + doc sign-off. |

---

## Tailwind Summary

We’ve already solved the unstable foundations (Stage B rescue, automation, quantitative metrics). The remaining push is focused: finish ridge visuals, enforce GPU/CPU parity, bring Stage B perf back under budget, and lock the operational tooling so it never drifts again. Follow the phases in order, keep the guardrails green, and we land exactly where the paper sets the bar. Let’s execute. 
