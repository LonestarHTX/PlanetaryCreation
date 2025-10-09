# Milestone 6 Performance Baseline (Updated 2025-10-10)

## Test Environment
- **Host:** Windows 11 (WSL2 client), 16C/32T CPU
- **GPU:** NVIDIA GeForce RTX 3080 (driver 576.88, SM 6.7)
- **UE Version:** 5.5.4
- **Config:** Development editor build (`PlanetaryCreationEditor`)
- **Project Seed:** 12345 for Stage B profiling, 777 for M5 regression

All commands below are executed from WSL and invoke the Windows editor binary so GPU compute paths remain active.

## Harness Commands
| Purpose | Command |
| --- | --- |
| Stage B profiling + GPU parity (run from Windows PowerShell) | `& 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject' -SetCVar='r.PlanetaryCreation.StageBProfiling=1' -ExecCmds='Automation RunTests PlanetaryCreation.Milestone6.GPU.<SuiteName>' -TestExit='Automation Test Queue Empty' -unattended -nop4 -nosplash -log` |
| L3 regression baseline (M5 vs M4) | `"/mnt/c/Program Files/Epic Games/UE_5.5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject" -ExecCmds="Automation RunTests PlanetaryCreation.Milestone5.PerformanceRegression; Quit" -unattended -nop4 -nosplash` |
| Insights capture (optional) | append `-trace=cpu,frame,counters,stats -statnamedevents -csvStats="csv:FrameTime,FrameTimeGPU"` to either command. Traces emit to `%LocalAppData%/UnrealEngine/Common/Analytics/UnrealInsights`. |

Logs for every run are written to `Saved/Logs/PlanetaryCreation.log`. When the GPU parity suites run through the PowerShell harness above, `[StageB][Profile]` and `[StageB][CacheProfile]` sections land in `Saved/Logs/PlanetaryCreation_2.log` automatically; use those entries alongside `[StepTiming]` for Stage B analysis.

## Results Snapshot

### Stage B Profiling (LOD 7)

`Automation RunTests PlanetaryCreation.Milestone6.GPU.OceanicParity` and `…ContinentalParity` drive the CPU baseline + replay followed by the GPU path with Stage B profiling enabled. (Recorded with the PowerShell command pattern above on 2025-10-10.)

**Oceanic GPU parity (2025-10-10)**

| Phase | Total (ms) | Stage B (ms) | Baseline | Ridge | Oceanic | Cache | Readback | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| CPU baseline (steps 2‑7) | 167–175 | **19.4** | 0.10 | 0.03 | **19.3 ms (CPU)** | 0.00 | 0.00 | Warm steady-state before GPU swap |
| GPU pass (step 8) | 240.1 | **10.9** | 0.10 | 0.03 | **10.8 ms (GPU)** | 0.00 | 0.00 | Includes one-time preview/texture init cost |

**Continental parity**

| Phase | Stage B (ms) | Baseline | Ridge | Continental CPU | Cache | Readback | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Snapshot-backed GPU run | *(Async write-back)* | – | – | – | – | 0.00 | `[ContinentalGPUReadback] GPU applied … (snapshot)` now writes the GPU results back into `VertexAmplifiedElevation` (mean delta ≈0 m, max delta 0.0003 m). |
| Drift fallback replay | **19.6** | 0.10 | 0.00 | **12.4–13.1 ms** | **≈7.1 ms** | 0.00 | Intentional undo/replay that verifies the CPU cache path (`Source=snapshot fallback` in the log). |
| Reduced vertex set | 16.7 | 0.11 | 0.00 | 10.8 ms | 5.8 ms | 0.00 | After Voronoi trim during the fallback replay. |


**Key takeaways**
- Oceanic Stage B now holds at **~19.4 ms** on the CPU baseline and **~10.9 ms** once the GPU pass takes over; the GPU path keeps readback at zero and only pays a one-time preview setup cost.
- Continental parity’s first replay now applies pure GPU output (`[ContinentalGPUReadback] GPU applied … (snapshot)`), while the follow-up undo still exercises the legacy cache fallback (Stage B ≈19.6 ms) for drift coverage.
- Ridge recompute cost stays near **0.03 ms** courtesy of incremental Voronoi dirtying; only the post-reset frame rebuilds the full `163 842` vertex set.
- Snapshot serial/hash protections are intact—no `[StageB][GPU]` mismatches and oceanic parity exits with max delta **0.0003 m**.
- `[StageB][CacheProfile]` continues to flag continental cache rebuild time (classification ~1.5 ms, exemplar selection ~1.3 ms); this remains the top optimisation target while the CPU fallback path is still executed.
- Navigation system ensure suppression still works; parity logs surface only the single informational warning (`NavigationSystem.cpp:3808`).

### Level 3 Baseline (M5 Regression Harness)

| Scenario | Avg Step (ms) | Overhead vs M4 | Target |
| --- | --- | --- | --- |
| M4 Baseline (Erosion/Sediment/Dampening off) | 3.38 | – | <110 ms ✅ |
| Erosion only | 3.40 | +0.02 | <5 ms ✅ |
| Sediment only | 3.56 | +0.18 | <4 ms ✅ |
| Dampening only | 3.41 | +0.03 | <3 ms ✅ |
| Full M5 stack | **3.70** | **+0.32** | <14 ms ✅ |

These measurements replace the extrapolated L3 figures in earlier performance docs; all future reports should cite this harness.

## Recommended Reporting Workflow
1. **Reset logs:** `rm Saved/Logs/PlanetaryCreation.log` (optional) to isolate the run.
2. **Run the Stage B profiling command** to capture L7 timings and parity status.
3. **Run the M5 regression command** to refresh the L3 baseline.
4. (Optional) **Capture an Insights trace** for deeper analysis with the `-trace` flags.
5. **Record metrics** in this document and in any milestone summary. Use the single source above to avoid divergent numbers.

## Latest Automation Status
- `PlanetaryCreation.Milestone6.GPU.OceanicParity` — **PASS** (2025‑10‑10), Stage B profiling on, GPU pass steady at 10.9 ms, max delta 0.0003 m.
- `PlanetaryCreation.Milestone6.GPU.ContinentalParity` — **PASS**, currently exercising the snapshot fallback (no GPU override yet); cache rebuild ≈7 ms, deltas 0.000 m.
- `PlanetaryCreation.Milestone6.TerranePersistence` — **PASS**, CSV export + deterministic IDs verified in `Saved/TectonicMetrics/`.
- `PlanetaryCreation.Milestone5.PerformanceRegression` — **PASS**, full M5 overhead 0.32 ms (reference baseline).
- `PlanetaryCreation.Milestone6.ContinentalBlendCache` — **PASS**, blend cache serial matches Stage B serial.

Keep this document current whenever Stage B optimizations land or hardware changes. Remove stale per-milestone performance tables elsewhere and point to this file as the canonical baseline.
