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

`Automation RunTests PlanetaryCreation.Milestone6.GPU.OceanicParity` and `…ContinentalParity` drive the CPU baseline + replay followed by the GPU path with Stage B profiling enabled. (Recorded with the PowerShell command pattern above on 2025-10-10 and re-validated 2025-10-13 alongside the unified parity harness.)

**Oceanic GPU parity (2025-10-10)**

Running the oceanic suite by itself now records a flat **1.7–1.8 ms** Stage B cost at L7. The log shows `OceanicGPU 0.00 ms` because the GPU preview path shares the same displacement buffer the CPU baseline populates during the warm-up pass; only the hydraulic pass (~1.6 ms) executes on the CPU before the parity harness snapshots the result.

**Continental parity (2025-10-10)**

| Phase | Stage B Total (ms) | Baseline | Ridge | Oceanic GPU | Continental CPU | Continental GPU | Cache | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Step 1 warm-up | **65.4** | 0.10 | 0.03 | **11.6** | **26.2** | **27.5** | 0.00 | First replay primes the GPU snapshot and still executes the legacy CPU path once. |
| Steps 2‑10 steady-state (avg) | **33.7** | 0.10 | 0.03 | **8.2** | **2.8** | **22.6** | 0.00 | Snapshot hash now matches every frame, so the GPU readback is applied directly with ≈2.8 ms CPU overhead. |
| CPU fallback validation (Steps 11‑12) | **44.2** | 0.11 | 0.03 | **≈19.5 (CPU)** | **14.9** | 0.00 | **9.6** | Parity intentionally replays the CPU cache to validate drift handling before exiting. |

Log excerpt with the new instrumentation:
```
[ContinentalGPU] Hash check JobId 11 Snapshot=0x92b4ed5b Current=0x92b4ed5b Match=1 (DataSerial=83/83 Topology=0/0 Surface=22/22)
```

**Key takeaways (2025-10-13 refresh)**
- Hash refinement keeps the GPU snapshot hot: steps 2‑10 now stay on the fast path (`ContinentalCPU ≈2.8 ms`, `ContinentalGPU ≈22.6 ms`) instead of replaying the ~19 ms CPU fallback every frame.
- The only time we pay the CPU/cache cost is when the parity harness deliberately rewinds (steps 11‑12); steady-state Stage B is down to **≈33–34 ms** at L7 (Oceanic GPU ~8 ms + Continental GPU ~23 ms + 2–3 ms CPU bookkeeping).
- Oceanic parity remains cheap when run solo (≈1.7 ms total) because the hydraulic pass dominates; when combined with continental parity, the oceanic GPU leg contributes the expected ~8 ms per frame.
- Ridge recompute and Voronoi updates remain around **0.03 ms**, and readback stays at zero thanks to the async job fencing.
- Snapshot/serial guards now log their status explicitly, making it easy to confirm when the GPU replay path is active in automation runs.
- Unified Stage B parity (the new `PlanetaryCreation.StageB.UnifiedGPUParity` harness) reports **MaxΔ = 0.003 m** and **MeanΔ = 0.0002 m**, confirming the shader’s blended heights match the CPU sampler down to millimetres.

**Stage B + Surface Processes (Paper defaults, L7)**  
`Automation RunTests PlanetaryCreation.Milestone6.Perf.StageBSurfaceProcesses` (with GPU amplification on) reports:
- Stage B steady-state: **1.8 ms** (null GPU path for this targeted run)
- Hydraulic erosion: **≈1.7 ms**
- Sediment diffusion: **≈8.5–10.1 ms**
- Oceanic dampening: **≈1.0–1.2 ms**

These figures confirm sediment diffusion is the dominant remaining surface-process cost when Stage B is disabled for profiling; optimisation passes should target bringing sediment under 10 ms at L7.

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
- `PlanetaryCreation.Milestone6.GPU.OceanicParity` — **PASS** (2025‑10‑10), Stage B profiling on, solo run reports ~1.7 ms total (hydraulic only) while combined runs log Oceanic GPU ≈8 ms; max delta 0.0003 m.
- `PlanetaryCreation.Milestone6.GPU.ContinentalParity` — **PASS**, steady-state frames stay on the GPU fast path (Continental GPU ≈22–23 ms, Continental CPU ≈2–3 ms, Cache 0 ms); parity undo still exercises the CPU fallback (~44 ms) by design.
- `PlanetaryCreation.StageB.UnifiedGPUParity` — **PASS** (2025‑10‑13), automation-only GPU replay flag enabled; MaxΔ 0.003 m, MeanΔ 0.0002 m.
- `PlanetaryCreation.Milestone6.TerranePersistence` — **PASS**, CSV export + deterministic IDs verified in `Saved/TectonicMetrics/`.
- `PlanetaryCreation.Milestone5.PerformanceRegression` — **PASS**, full M5 overhead 0.32 ms (reference baseline).
- `PlanetaryCreation.Milestone6.ContinentalBlendCache` — **PASS**, blend cache serial matches Stage B serial.

Keep this document current whenever Stage B optimizations land or hardware changes. Remove stale per-milestone performance tables elsewhere and point to this file as the canonical baseline.
