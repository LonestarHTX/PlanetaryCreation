# Milestone 6 Performance Baseline (Updated 2025-10-08)

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
| Stage B profiling + oceanic parity (LOD 7) | `"/mnt/c/Program Files/Epic Games/UE_5.5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject" -ExecCmds="r.PlanetaryCreation.StageBProfiling 1; Automation RunTests PlanetaryCreation.Milestone6.GPU.OceanicParity; Quit" -unattended -nop4 -nosplash` |
| L3 regression baseline (M5 vs M4) | `"/mnt/c/Program Files/Epic Games/UE_5.5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject" -ExecCmds="Automation RunTests PlanetaryCreation.Milestone5.PerformanceRegression; Quit" -unattended -nop4 -nosplash` |
| Insights capture (optional) | append `-trace=cpu,frame,counters,stats -statnamedevents -csvStats="csv:FrameTime,FrameTimeGPU"` to either command. Traces emit to `%LocalAppData%/UnrealEngine/Common/Analytics/UnrealInsights`. |

Logs for every run are written to `Saved/Logs/PlanetaryCreation.log`. The Stage B profiling block is keyed by `[StepTiming]`.

## Results Snapshot

### Stage B Profiling (LOD 7)
`Automation RunTests PlanetaryCreation.Milestone6.GPU.OceanicParity` runs the CPU pass, a deterministic CPU replay, then the GPU path with profiling enabled.

| Step | Total (ms) | Stage B Total (ms) | Baseline | Ridge | Oceanic | Continental | Readback |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 5 | 419.69 | 19.42 | 0.10 | 0.04 | 19.28 | 0.00 | 0.00 |
| 6 | 162.65 | 19.67 | 0.10 | 0.04 | 19.53 | 0.00 | 0.00 |
| 7 | 165.78 | 23.64 | 0.10 | 4.31 | 19.23 | 0.00 | 0.00 |
| 8 | 240.12 | **14.50** | 0.10 | **4.35** | **10.05** | 0.00 | **0.00** |

**Key takeaways**
- Stage B now lands at **14.50 ms** on the GPU path with readback eliminated, comfortably under the 50 ms M6 allocation.
- Undoing to the cached snapshot dirties the ridge cache once (Step 7), explaining the 4.31 ms spike before the steady-state 4.35 ms ridge cost on the GPU pass.
- CPU baseline/replay remain ~19–24 ms with ridge work minimal unless the topology cache invalidates.
- No `[StageB][GPU] … hash mismatch` warnings after the snapshot serial/hash fix; GPU parity exits with max delta **0.0003 m**.
- `[StepTiming]` now prints ridge dirty/update counts and cache statistics; the L7 undo replay only touched **192 vertices**, while the continental parity pass still reports full-mesh dirties (expected until exemplar caching lands).
- Navigation-system repository ensure is now intercepted by the editor module handler; parity logs show a single warning (`NavigationSystem.cpp:3808`) without repeated error spam.

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
- `PlanetaryCreation.Milestone6.GPU.OceanicParity` — **PASS**, Stage B profiling enabled, max delta 0.00 m.
- `PlanetaryCreation.Milestone5.PerformanceRegression` — **PASS**, full M5 overhead 0.32 ms.

Keep this document current whenever Stage B optimizations land or hardware changes. Remove stale per-milestone performance tables elsewhere and point to this file as the canonical baseline.
