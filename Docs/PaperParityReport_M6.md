# Milestone 6 Paper Parity Report

**Purpose:** Document how the Unreal implementation matches the “Procedural Tectonic Planets” Stage B results (Section 6) as of 2025‑10‑10.

---

## 1. Test Harness & Configuration

| Item | Unreal Implementation | Paper Reference |
| --- | --- | --- |
| Project seed | `PaperDefaults` (LOD 5+, Stage B/GPU/PBR enabled) | Random seed fixed per experiment |
| Render LOD | Level 7 (163,842 vertices) | Table 2 (25 km sampling) |
| Stage B toggles | Oceanic & continental amplification ON, hydraulic erosion ON | Stage B detail amplification, hydraulic routing |
| Execution | Windows PowerShell command (GPU parity suites) | Offline parity captures |
| Profiling CVars | `r.PlanetaryCreation.StageBProfiling=1`, `r.PlanetaryCreation.PaperDefaults=1` | Timing breakdown per pass |

Command template:
```powershell
& 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject' `
  -SetCVar='r.PlanetaryCreation.StageBProfiling=1' `
  -SetCVar='r.PlanetaryCreation.PaperDefaults=1' `
  -ExecCmds='Automation RunTests PlanetaryCreation.Milestone6.GPU.<Suite>' `
  -TestExit='Automation Test Queue Empty' -unattended -nop4 -nosplash -log
```

---

## 2. Stage B Timing Parity (LOD 7)

| Step | Total (ms) | Stage B (ms) | Oceanic GPU | Continental GPU | Continental CPU | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| Warm-up (Step 1) | 290.8 | **65.43** | 11.62 | 27.47 | 26.21 | Matches paper’s initial Stage B priming; CPU fallback runs once to seed the snapshot. |
| Steady-state (Step 2) | 199.7 | **33.85** | 8.26 | 22.20 | 3.26 | Hash-stable GPU path, readback 0 ms. |
| Steady-state (Step 5) | 206.6 | **33.32** | 8.23 | 21.72 | 3.37 | Consistent with predicted 33–34 ms envelope. |
| Steady-state (Step 10) | 456.6 | **33.12** | 8.50 | 21.59 | 3.70 | Voronoi refresh triggered (134 k dirty verts); Stage B cost remains ~33 ms. |
| Parity fallback (Step 11) | 309.4 | **42.47** | 0.00 | 0.00 | 13.53 | Expected CPU replay (`Source=snapshot fallback`) validates drift handling; cache rebuild ≈8.6 ms. |

Key evidence:
- Logs include `[ContinentalGPU] Hash check … Match=1` on Steps 2‑10, confirming GPU snapshot reuse.
- Hydraulic pass reports 1.6–1.8 ms, aligning with the paper’s “valley carving” phase.
- Oceanic parity max delta 0.0003 m; continental fallback also reports 0.0000 m deltas (snapshot parity).

---

## 3. Elevation & Visual Parity

| Verification | Unreal Result | Paper Expectation |
| --- | --- | --- |
| Oceanic amplification | 100 % vertices within ±0.1 m; mean delta 0.0 mm | Stage B oceanic amplification matches CPU baseline |
| Continental amplification | Snapshot replay 0.0 mm mean delta; fallback replays cached mix | Stage B continental amplification produces plate-consistent uplift patterns |
| Hydraulic erosion | Steady-state valleys maintained across steps | Paper Section 5 valley formation persists after Stage B detail |
| Boundary overlay | Simplified polylines render single seam strand | Paper visuals show crisp plate boundaries without starburst artifacts |
| GPU preview | Seam mirroring test passes; equirect seam balanced | Paper depiction (Figure 9/10) shows continuous detail at seams |

Screenshots/parity captures should be regenerated with `PaperDefaults` active to match the Stage B detail tables.

---

## 4. Outstanding Gaps vs Paper

| Area | Status | Planned Work |
| --- | --- | --- |
| Sediment & dampening cost | 14–19 ms / 24–25 ms; higher than paper’s CPU budget | CSR SoA + `ParallelFor`, profiling hooks (Milestone 6 follow-up) |
| Release collateral | Release notes, parity table pending | **This document + `Docs/ReleaseNotes_M6.md`** now capture updated timings; gallery screenshots still pending |
| PBR material polish | Basic PBR toggle shipped; biome textures deferred | Schedule for Milestone 7 presentation pass |
| Async GPU amplification | Snapshot reuse working; GPU still serialised with frame | Investigate RDG async staging + readback fencing in Milestone 7 |

---

## 5. Recommended Reporting Workflow

1. Reset logs (`rm Saved/Logs/PlanetaryCreation.log`) before each capture.
2. Run Oceanic + Continental parity suites with profiling CVars set (PowerShell command above).
3. Collect `[StageB][Profile]` and `[StepTiming]` tables for steps 1–12; ensure hash-match lines appear.
4. Update performance tables in `Docs/Performance_M6.md` and this report; keep numbers synchronised.
5. Capture before/after renders (Plate Colors, Amplified Stage B, Amplification Blend) at LOD 7.

---

## 6. Summary

Milestone 6 achieves the paper’s Stage B detail amplification goals inside Unreal:
- GPU amplification runs in parity with the CPU baseline and now remains resident thanks to the hash fix.
- Stage B steady-state cost sits at 33–34 ms per frame (LOD 7), matching the predicted budget and leaving >50 % headroom under the 90 ms step target.
- Hydraulic erosion’s topological queue optimisation brings valley carving in line with the paper’s expectations at ~1.7 ms.
- Remaining gaps focus on sediment/dampening performance and presentation polish, scheduled for subsequent milestones.

This report, together with `Docs/Performance_M6.md` and `Docs/ReleaseNotes_M6.md`, forms the canonical parity reference for Milestone 6.
