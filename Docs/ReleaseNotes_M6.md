# Milestone 6 Release Notes – Stage B Amplification & Terrane Lifecycle

**Date:** 2025‑10‑10  
**Scope:** Paper Section 6 completion – Stage B terrain amplification, terrane extraction/reattachment, GPU parity harness

---

## Overview

Milestone 6 delivers the “Procedural Tectonic Planets” Stage B feature set inside the Unreal editor:
- GPU-backed oceanic and continental amplification at ~100 m detail, with snapshot caching and CPU fallback stability.
- Terrane extraction, transport, and reattachment, including deterministic IDs and CSV lifecycle exports.
- Integrated hydraulic erosion (stream-power pass) operating on amplified terrain.
- Tooling updates: paper-default launch config (LOD 5, Stage B/GPU/PBR on), PBR preview toggle, Stage B profiling harness, and GPU parity automation suites.

Stage B is now enabled by default; use `r.PlanetaryCreation.PaperDefaults 0` to revert to the lean Milestone 5 configuration when profiling CPU-only paths.

---

## Highlights

### Stage B Amplification
- Oceanic amplification compute shader (Perlin + transform faults) feeds both CPU fallback and GPU preview.
- Continental amplification leverages exemplar blending on the GPU with snapshot caching and async readback pooling.
- In-place realtime mesh refresh skips section rebuilds when topology is unchanged, reducing render thread churn.
- Hash fix prevents per-frame invalidation: `ComputeCurrentContinentalInputHash` now ignores `VertexAmplifiedElevation`, keeping GPU snapshots hot.

### Terrane Lifecycle
- Extraction snapshots capture full vertex payloads; reattachment performs mesh surgery while maintaining adjacency.
- Deterministic terrane IDs (seed + plate + extraction timestamp); undo/redo preserves hashes for cross-platform parity.
- CSV exports (`Terranes_*.csv`) list centroid latitude/longitude, lifecycle states, areas, and timestamps.

### Hydraulic Erosion
- Stream-power routing runs immediately after Stage B amplification.
- Topological queue replaces O(N log N) sorting, cutting the L7 hydraulic pass from 18.3 ms to **~1.7 ms** (91 % reduction).
- Erosion deltas now persist into the Stage A baseline to avoid “valleys resetting” each step.

### Tooling & Automation
- `r.PlanetaryCreation.StageBProfiling` logs `[StageB][Profile]` / `[StageB][CacheProfile]` per step, capturing ridge dirty counts, Voronoi refresh metrics, and pass timings.
- GPU parity tests (`PlanetaryCreation.Milestone6.GPU.*`) validate CPU vs GPU results; hash logging (`[ContinentalGPU] Hash check … Match=1`) is required for steady-state steps.
- Boundary overlay simplifier renders single-strand plate seams; UI reorganised into collapsible sections with a PBR toggle.

---

## Performance Snapshot (LOD 7, Paper Defaults, 2025‑10‑10)

| Phase | Stage B (ms) | Notes |
| --- | --- | --- |
| Warm-up (Step 1) | **65.4** | First Stage B replay seeds the GPU snapshot and runs the CPU path once. |
| Steady-state (Steps 2‑10) | **33–34** | Oceanic GPU ≈8 ms, Continental GPU ≈23 ms, Continental CPU bookkeeping ≈3 ms; readback 0 ms. |
| Parity fallback (Step 11) | **~44** | Intentional CPU/cache replay to validate drift handling before automation exits. |

Supporting passes (when enabled) currently profile at:
- Sediment diffusion: **~14–19 ms**
- Oceanic dampening: **~24–25 ms**
- Hydraulic erosion: **~1.7 ms**

Total steady-state frame (with Stage B + hydraulic, without sediment/dampening) sits at **≈43.6 ms** (≤90 ms target, ~51 % headroom). Sediment/dampening optimisation (CSR SoA + `ParallelFor`) is the next scheduled workstream.

---

## Testing & Automation

Primary suites (run from Windows PowerShell to keep GPU paths active):
- `PlanetaryCreation.Milestone6.GPU.OceanicParity`
- `PlanetaryCreation.Milestone6.GPU.ContinentalParity`
- `PlanetaryCreation.Milestone6.GPU.IntegrationSmoke`
- `PlanetaryCreation.Milestone6.GPU.PreviewSeamMirroring`
- `PlanetaryCreation.Milestone6.ContinentalBlendCache`

Expectations:
- Parity logs must include `[StageB][Profile] …` for every step and `[ContinentalGPU] Hash check … Match=1` for steady-state frames. Absence of hash matches indicates snapshot invalidation and should fail CI.
- `r.PlanetaryCreation.StageBProfiling 1` is required for detailed logging; automation commands prepend both the setter and `-SetCVar`.
- Hydraulic erosion remains enabled (`r.PlanetaryCreation.EnableHydraulicErosion 1`) and should report ≈1.6–1.8 ms in parity runs.

Regression suites:
- Terrane lifecycle (`TerraneMeshSurgeryTest`, `TerranePersistenceTest`)
- Ridge cache, Voronoi reassignment, boundary overlay UIs
- Milestone 5 performance regression (`PlanetaryCreation.Milestone5.PerformanceRegression`) with `r.PlanetaryCreation.PaperDefaults 0`

---

## Known Issues & Follow-Up Work

1. **Sediment & Dampening Cost** – Currently dominate frames (~14–25 ms). Planned fixes: CSR-style neighbour arrays, `ParallelFor`, Insights instrumentation, automated timing checks.
2. **Continental parity fallout logging** – Drift replay still produces large `[ContinentalGPUReadback][Compare]` deltas; acceptable for now but verbose. Consider culling once sediment/dampening work lands.
3. **Paper parity collateral** – Update documentation (`Docs/PaperParityReport_M6.md`) and gallery captures with the 65 / 33–34 / 44 ms Stage B pattern.
4. **Hash-match CI gating** – Wire `[ContinentalGPU] Hash check … Match` coverage into automation dashboards so regressions fail fast.
5. **Navigation ensure** – Continue monitoring the once-suppressed `NavigationSystem.cpp:3808` warning; confirm no new ensures leak into logs.

---

## Upgrade Notes

- **Console variables:**  
  - `r.PlanetaryCreation.PaperDefaults 1` is the new launch default; toggle to `0` for the M5 baseline.  
  - `r.PlanetaryCreation.UsePBRShading` enables/disables the preview PBR material.  
  - `r.PlanetaryCreation.StageBProfiling 1` emits detailed profiling logs (required for parity runs).  
  - `r.PlanetaryCreation.UseGPUHydraulic` remains a placeholder (`1` routes to the tuned CPU path).
- **Editor workflow:** Stage B, GPU preview, and hydraulic erosion are now considered standard operating modes; documentation instructs agents to leave them enabled unless running legacy CPU regressions.
- **Assets:** No new cooked assets; Stage B exemplar CSV updated (19 SRTM90 patches).

---

## Credits

- **Simulation & Terrane Systems:** Claude (sim engineer), Michael (simulation lead)  
- **GPU Amplification & Preview:** Rendering engineering team  
- **Automation & Profiling:** QA/perf engineering team  
- **Reference:** Cordonnier et al., “Procedural Tectonic Planets”, Sections 4–6

Milestone 6 delivers a paper-faithful Stage B workflow inside the editor with healthy GPU coverage and logging. Focus now shifts to polishing sediment/dampening, documenting parity results, and preparing the presentation tooling targeted for Milestone 7.
