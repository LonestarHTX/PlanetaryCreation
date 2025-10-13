Michael — I pulled apart the exporter and sampler from your snapshot and focused on the exact code paths you flagged. Below is a concrete diagnosis + patches and a safe plan to get back to 4K without bricking the box.

---

### Phase 1.0 Profiling Baseline (2025‑10‑12)

- Stage B was profiled at LOD 7 via `PlanetaryCreation.Milestone6.Perf.StageBSurfaceProcesses` with `r.PlanetaryCreation.StageBProfiling=1` and the safety throttle set to 50 ms. Hydraulic erosion remains disabled by the STG‑04 latch, while sediment diffusion and oceanic dampening execute on the CPU.
- Command (run from WSL using the PowerShell wrapper):

  ```powershell
  powershell.exe -Command "& 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
    'C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject' `
    -SetCVar='r.PlanetaryCreation.StageBProfiling=1,r.PlanetaryCreation.StageBThrottleMs=50,r.PlanetaryCreation.UseGPUAmplification=1,r.PlanetaryCreation.AllowGPUAutomation=1,r.PlanetaryCreation.EnableHydraulicErosion=1,r.PlanetaryCreation.EnableSedimentTransport=1,r.PlanetaryCreation.EnableOceanicDampening=1' `
    -ExecCmds='Automation RunTests PlanetaryCreation.Milestone6.Perf.StageBSurfaceProcesses' `
    -TestExit='Automation Test Queue Empty' `
    '-trace=cpu,frame,counters,stats' `
    '-tracefile=C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\Saved\Profiling\Phase1_StageB_LOD7.utrace' `
    -statnamedevents -unattended -nop4 -nosplash -log"
  ```

- Hardware: AMD Ryzen 9 7950X (16C/32T, 4.5 GHz boost), NVIDIA GeForce RTX 3080 (driver 32.0.15.8142), 64 GB RAM, Windows 11 Pro 10.0.26100 (64‑bit).
- Unreal Insights trace: `Docs/Validation/PerfCaptures/Phase1_LOD7_StageB_2025-10-12.utrace`

| Step sample | Stage B total (ms) | Oceanic GPU (ms) | Continental GPU (ms) | Hydraulic (ms) | Sediment CPU (ms) | Dampening CPU (ms) |
| --- | --- | --- | --- | --- | --- | --- |
| Warm-up (Step 1) | 43.69 | 10.51 | 29.25 | 0.00 *(latched off)* | 9.32 | 1.01 |
| Steady state (Steps 5–8 avg) | 38.72 | 7.52 | 26.32 | 0.00 *(latched off)* | 11.41 | 1.25 |

**Notes**
- Oceanic and continental amplification are running entirely on the GPU fast path (CPU columns remain at 0 ms).
- Hydraulic erosion is still suppressed by `bForceHydraulicErosionDisabled`; re-enabling it will be required before we can publish a complete Stage B + surface budget.
- Sediment diffusion stabilises around 10–12 ms after the initial fill pass (Step 2 peaks at 23.85 ms while caches warm). Oceanic dampening holds at ~1.1–1.3 ms on the CPU.

### Ridge Tangent Cache Lifecycle
- Ridge fixture helpers now live in `Source/PlanetaryCreationEditor/Private/Tests/RidgeTestHelpers.{h,cpp}` (triple-junction selector + crust-age discontinuity pair). They back `PlanetaryCreation.StageB.RidgeTerraneCache`, which exercises extraction/reattachment with asserts on cache hit and fallback ceilings.
- Milestone 3 harness (including the ridge lifecycle + terrane cache coverage assertions) executed via:

  ```powershell
  powershell.exe -ExecutionPolicy Bypass -File .\Scripts\RunMilestone3Tests.ps1 -ArchiveLogs
  ```

  The 2025‑10‑12 rerun (`Saved/Logs/PlanetaryCreation.log`) confirms the cache thresholds:
  - `[RidgeDiag] Summary: Dirty=2556 Oceanic=1782 CacheHits=1782 (100.0%) StoredReuse=1657 Missing=559 (31.4%) GradientFallback=0 (0.0%) MotionFallback=0 (0.0%)`
  - `[RidgeDir][Terrane] Action=Extract Terrane=994958138 Coverage=100.00% Dirty=1782 CacheHits=1782` followed by `Action=Reattach … Coverage=100.00%`
  - Test harness output archived in `Docs/Validation/heightmap_export_metrics_20251012_231808.csv` with the associated parity renders under `Docs/Validation/ParityFigures/`.
- Pre/post ridge tangent heatmaps (`ridge_tangent_before_reattach.png`, `ridge_tangent_after_reattach.png`) remain in `Docs/Validation/ParityFigures/` for downstream parity checks.

## Phase Rollup
- **Phase 3 – Validation:** Complete (2025-10-12, re-verified 2025-10-13). Milestone 3 harness runs via `Scripts/RunMilestone3Tests.ps1 -ArchiveLogs`, archives the quantitative metrics CSV, and confirmed tiled exporter parity for Phase 4 handoff. The latest run (2025-10-13 22:04 UTC) incorporated the unified Stage B GPU parity fix — automation reported MaxΔ 0.003 m / MeanΔ 0.0002 m and refreshed `Docs/Validation/heightmap_export_metrics_*.csv`.
- **Phase 4 – Documentation:** Ready to begin. Capture USD `LogUsd: Warning: Failed to parse plugInfo.json` as a tracked known warning and leave suppression work pending.

## Stage B Rescue Telemetry Quick Reference
- `[StageB][RescueSummary]` emits once per export after the sampler resolves all fallbacks. `StartReady`/`FinishReady` echo the Stage B readiness latch and reason strings, `RescueAttempted`/`RescueSucceeded` count fallback usage, `AmplifiedUsed`/`SnapshotFloat` confirm which datasets backed successful samples, `Coverage`/`Miss` surface absolute hit totals, and `RowReuse` sits inside the `Modes[...]` histogram as the count of rows replayed from cached Stage B output. Treat any non-zero `Fail` as a regression — supervised runs now baseline at zero failures.
- Coverage captures from 2025‑10‑12 show 100 % hits at both scales: `Scripts/ExportHeightmap1024.py` (1024×512) logged to `Saved/Logs/PlanetaryCreation-backup-2025.10.12-20.32.15.log`; `Scripts/ExportHeightmap4096Force.py` (4096×2048) logged to `Saved/Logs/PlanetaryCreation-backup-2025.10.12-20.40.33.log`. Each file also includes the paired `[HeightmapExport][Coverage]` line for quick diffing.
- `[HeightmapExport][PerformanceBudgetExceeded]` still triggers when sampling or encode time crosses the configured ceiling. It remains advisory provided coverage and traversal stats match baseline; escalate only if the warning aligns with rising miss counts or degraded budgets.

## TL;DR: why 4K freezes now (post‑rewrite)

The new path stores **three full per‑pixel telemetry arrays** (`ElevationSamples<double>`, `SampleSuccess<uint8>`, `SampleTraversalSteps<uint8>`) **in addition to two copies of the image data** (`TArray<FColor>` and a second `TArray<uint8>` “RawData” for PNG). At 4096×2048 that’s **~144 MiB** just for these five arrays, before PNG compression buffers and the sampler’s KD‑tree/adjacency memory. That footprint didn’t exist in the pre‑rewrite “vertex‑splat” path, so the 4K step now pushes the machine past its headroom and you hit an OS‑level reset (Kernel‑Power 41) before Unreal can flush logs.

None of this touches the GPU under `-NullRHI`; it’s a CPU‑side memory/pressure issue.

---

## 1) Code review: `ExportHeightmapVisualization()` — all allocations & potential pitfalls

From `Source/PlanetaryCreationEditor/Private/HeightmapExporter.cpp` in your snapshot, the exporter does (key lines abbreviated):

- **Sampler setup**  
  ```cpp
  FHeightmapSampler Sampler(*this);  // builds adjacency + KD-tree
  ```
  > `FHeightmapSampler` constructs `TriangleData` (adjacency), `TriangleDirections` (FVector3d/triangle), `TriangleIds` (int32/triangle), a KD‑tree over triangle directions, and optionally references a snapshot float buffer.

- **Per‑pixel buffers (scale with W×H):**
  ```cpp
  const int32 PixelCount = ImageWidth * ImageHeight;

  TArray<FColor> ImageData;                // PixelCount × 4 B   (RGBA8 as FColor)
  ImageData.SetNumUninitialized(PixelCount);

  TArray<double> ElevationSamples;         // PixelCount × 8 B   (seam stats only)
  ElevationSamples.SetNumUninitialized(PixelCount);

  TArray<uint8> SampleSuccess;             // PixelCount × 1 B
  SampleSuccess.SetNumUninitialized(PixelCount);

  TArray<uint8> SampleTraversalSteps;      // PixelCount × 1 B
  SampleTraversalSteps.SetNumUninitialized(PixelCount);
  ```

- **Sampling loop**  
  ```cpp
  ParallelFor(ImageHeight, [&](int32 Y){ for (X) {
      FHeightmapSampler::FSampleInfo Info;
      double Elevation = Sampler.SampleElevationAtUV(UV, &Info);
      ElevationSamples[Idx]      = Elevation;
      SampleSuccess[Idx]         = Info.bHit ? 1 : 0;
      SampleTraversalSteps[Idx]  = (uint8)Clamp(Info.Steps,0,255);
      ImageData[Idx]             = Palette.Sample(Elevation);
  }});
  ```

- **Seam metrics pass (post‑loop)**  
  Uses only **the first and last pixel of each row**:
  ```cpp
  LeftIndex = Y*W; RightIndex = LeftIndex+(W-1);
  uses ElevationSamples[LeftIndex], ElevationSamples[RightIndex]
  and SampleSuccess[...] at those same two indices
  ```

- **Encoding**  
  Builds a *second* full‑size buffer and copies `ImageData` into it:
  ```cpp
  TArray<uint8> RawData;                   // PixelCount × 4 B
  RawData.SetNumUninitialized(W * H * 4);

  // fill RawData from ImageData, then:
  ImageWrapper->SetRaw(RawData.GetData(), RawData.Num(), W, H, ERGBFormat::RGBA, 8);
  TArray<uint8> CompressedData;            // PNG output (few MB, variable)
  ImageWrapper->Compress( ... , CompressedData);
  ```

- **Hidden allocations** (independent of W×H):
  - `FHeightmapSampler`:
    - `TriangleData` (neighbors per tri, indices)
    - `TriangleDirections` (FVector3d per tri)
    - `TriangleIds` (int32 per tri)
    - `TMap` `EdgeOwners` used to build adjacency (`Reserve(TriangleCount * 3)` ✅ done)
    - KD‑tree nodes (one per triangle; struct + two pointers)
    - Optional **Stage B snapshot float buffer reference** (not a copy, but resident memory)
  - `IImageWrapperModule` → zlib/PNG internal scratch during compression

> **Good news:** I didn’t see any `RHICreateTexture*`, `ENQUEUE_RENDER_COMMAND`, `FRHICommandList` or texture compression calls in this exporter/sampler path. The few RHI usages in the repo are for unrelated GPU exemplar work, not referenced here. Under `-NullRHI` this exporter stays CPU‑only.

---

## 2) Memory budgeting — expected allocations at common sizes

Per‑pixel arrays (MiB, binary MiB = 1,048,576 bytes):

| Size      | Pixels    | `FColor` | RawData | Elevation (double) | Success (u8) | Steps (u8) | **Subtotal** |
|-----------|-----------|----------|---------|--------------------|--------------|------------|--------------|
| 512×256   | 131,072   | 0.5      | 0.5     | 1.0                | 0.125        | 0.125      | **2.25**     |
| 1024×512  | 524,288   | 2.0      | 2.0     | 4.0                | 0.5          | 0.5        | **9.0**      |
| 2048×1024 | 2,097,152 | 8.0      | 8.0     | 16.0               | 2.0          | 2.0        | **36.0**     |
| **4096×2048** | **8,388,608** | **32.0** | **32.0** | **64.0** | **8.0** | **8.0** | **144.0** |

Add:
- PNG compressed buffer (varies: a few MiB).
- **Sampler memory** (KD‑tree + adjacency) — fixed per mesh, often **tens to hundreds of MiB** on dense Stage B meshes.
- The **Stage B snapshot float buffer** (4 B × vertex count) sitting resident in the process.

That combination is very likely what crosses the host’s margin at 4K.

---

## 3) Low‑risk fix: make telemetry O(height), drop the duplicate image copy

You can remove ~**96 MiB** from 4K immediately (and ~**~110 MiB** including copy elision) with very small changes:

### A. Stream seam stats & coverage — *don’t store the whole image of telemetry*

You only ever read the *leftmost* and *rightmost* pixel per row to compute seam deltas, and you reduce coverage/steps to totals/max. Replace the three full arrays with **per‑row seam scratch + per‑row stats**:

```cpp
// Replace the three big arrays with these tiny ones:
struct FRowSeam { float LeftElev; float RightElev; uint8 LeftOK; uint8 RightOK; };
TArray<FRowSeam> RowSeams; RowSeams.SetNumZeroed(ImageHeight);

TArray<uint32> RowSuccess;   RowSuccess.SetNumZeroed(ImageHeight);
TArray<uint64> RowStepSum;   RowStepSum.SetNumZeroed(ImageHeight);
TArray<uint8>  RowMaxSteps;  RowMaxSteps.SetNumZeroed(ImageHeight);

// Drop: ElevationSamples, SampleSuccess, SampleTraversalSteps
```

In the sampling loop, write **RawData directly** (skip `ImageData`), and accumulate per‑row stats locally; then publish to the per‑row arrays. Example:

```cpp
ParallelFor(ImageHeight, [&](int32 Y)
{
    uint32 LocalSuccess = 0;
    uint64 LocalStepSum = 0;
    uint8  LocalMaxSteps = 0;

    const double V = (double(Y) + 0.5) * InvHeight;
    const int32 RowBase = Y * ImageWidth;

    for (int32 X = 0; X < ImageWidth; ++X)
    {
        const double U = (double(X) + 0.5) * InvWidth;
        FHeightmapSampler::FSampleInfo Info;
        const double Elev = Sampler.SampleElevationAtUV(FVector2d(U, V), &Info);

        if (Info.bHit) { ++LocalSuccess; LocalStepSum += (uint8)FMath::Clamp(Info.Steps,0,255); }
        LocalMaxSteps = FMath::Max<uint8>(LocalMaxSteps, (uint8)FMath::Clamp(Info.Steps,0,255));

        const FColor C = Palette.Sample(Elev);
        const int32 RawIdx = (RowBase + X) * 4;
        RawData[RawIdx + 0] = C.R;
        RawData[RawIdx + 1] = C.G;
        RawData[RawIdx + 2] = C.B;
        RawData[RawIdx + 3] = 255;

        // Capture only seam endpoints:
        if (X == 0)      { RowSeams[Y].LeftElev = (float)Elev;  RowSeams[Y].LeftOK  = Info.bHit ? 1 : 0; }
        if (X == ImageWidth - 1) { RowSeams[Y].RightElev = (float)Elev; RowSeams[Y].RightOK = Info.bHit ? 1 : 0; }
    }

    RowSuccess[Y]  = LocalSuccess;
    RowStepSum[Y]  = LocalStepSum;
    RowMaxSteps[Y] = LocalMaxSteps;
}, EParallelForFlags::None);
```

Then reduce per‑row stats and seam deltas on the main thread:

```cpp
uint64 SuccessfulSamples = 0;
uint64 TraversalStepSum  = 0;
uint8  MaxTraversalSteps = 0;
int32  SeamRows = 0, SeamRowsAboveHalfMeter = 0, SeamRowsWithFailures = 0;
double SeamAbsAccum = 0.0, SeamRmsAccum = 0.0, SeamMaxAbsDelta = 0.0;

for (int32 Y = 0; Y < ImageHeight; ++Y)
{
    SuccessfulSamples += RowSuccess[Y];
    TraversalStepSum  += RowStepSum[Y];
    MaxTraversalSteps  = FMath::Max(MaxTraversalSteps, RowMaxSteps[Y]);

    const FRowSeam R = RowSeams[Y];
    if (!R.LeftOK || !R.RightOK) { ++SeamRowsWithFailures; continue; }
    ++SeamRows;

    const double Delta = double(R.LeftElev) - double(R.RightElev);
    const double AbsDelta = FMath::Abs(Delta);
    SeamAbsAccum += AbsDelta;  SeamRmsAccum += Delta * Delta;
    SeamMaxAbsDelta = FMath::Max(SeamMaxAbsDelta, AbsDelta);
    if (AbsDelta > 0.5) { ++SeamRowsAboveHalfMeter; }
}
```

### B. Eliminate the duplicate color buffer

Build **only** `RawData` and **skip** `TArray<FColor> ImageData` entirely (as shown above). `IImageWrapper` takes the raw RGBA bytes; you don’t need the intermediate `FColor` array.

*Net reduction @ 4K:* ~**32 MiB** (ImageData) + **64 MiB** (ElevationSamples) + **8 MiB** (Success) + **8 MiB** (Steps) ≈ **112 MiB** fewer transient allocations.  
(Your seam scratch becomes ~**32 KB** for 2048 rows.)

---

## 4) Pre‑flight and hard guardrails (avoid catastrophic runs)

Add a **preflight memory budget** check and an early abort before any big `SetNumUninitialized`:

```cpp
struct FExportBudget {
    uint64 PixelBytes;     // RawData only in the new path: 4 * W * H
    uint64 PNGScratchBytes;// rough upper bound (conservative few MiB)
    uint64 SamplerBytes;   // KD-tree + adjacency + snapshot (from Sampler.GetMemoryStats)
};

static bool PreflightHeightmapExport(int32 W, int32 H, const FHeightmapSampler::FMemoryStats& SM, FString& OutWhy)
{
    const uint64 Px = uint64(W) * uint64(H);
    FExportBudget B{};
    B.PixelBytes      = Px * 4ull;
    B.PNGScratchBytes = 8ull * 1024 * 1024; // safe cushion
    B.SamplerBytes    = SM.TriangleDataBytes + SM.TriangleDirectionsBytes + SM.TriangleIdsBytes
                      + SM.KDTreeBytes + SM.SnapshotFloatBytes;

    const FPlatformMemoryStats S = FPlatformMemory::GetStats();
    const uint64 FreePhys = S.AvailablePhysical;         // same field you already log
    const uint64 Safety   = 512ull * 1024 * 1024;        // 512 MiB headroom

    const uint64 Need = B.PixelBytes + B.PNGScratchBytes + B.SamplerBytes + Safety;

    if (Need > FreePhys) {
        OutWhy = FString::Printf(TEXT("Need≈%.1f MiB (pixels %.1f + sampler %.1f + scratch 8 + safety 512), Free≈%.1f MiB"),
            double(Need)/MiB, double(B.PixelBytes)/MiB, double(B.SamplerBytes)/MiB, double(FreePhys)/MiB);
        return false;
    }
    return true;
}
```

Call this right after `Sampler.IsValid()` and **before** allocating `RawData`. If it fails, log `[HeightmapExport][Preflight][Abort]` and return a friendly failure instead of proceeding into an OS‑level thrash.

> Optional: expose `PlanetaryCreation.Heightmap.Export.SafetyHeadroomMB` and `...PNGExtraMB` as CVars to make this tunable without code.

---

## 5) Instrumentation that won’t brick the box

You already have great scaffolding in this file:

- `LogBufferTelemetry(...)` and `LogMemoryCheckpoint(...)` around each allocation ✅  
- `[HeightmapExport][SamplerMemory]` based on `Sampler.GetMemoryStats()` ✅

**Keep those, but add:**

- A **single** `[HeightmapExport][Preflight]` line that prints the *predicted* bytes by category *before* any big allocations.
- `TRACE_CPUPROFILER_EVENT_SCOPE(HeightmapSampling)` and `TRACE_CPUPROFILER_EVENT_SCOPE(PNGEncode)` to correlate timings in Unreal Insights with your existing performance budget warnings.
- **Flush the log** immediately before PNG compression and right after (two `UE_LOG` lines with `GLog->Flush()` if needed). This gives you breadcrumbs if the box resets mid‑encode.

> Avoid per‑row logging; use your existing checkpoints only around allocations and the loop boundaries to keep overhead negligible.

---

## 6) RHI/driver sanity under `-NullRHI`

Add a one‑time info log (or assert in non‑shipping) to double‑check we’re not accidentally running with a real RHI:

```cpp
#if !UE_BUILD_SHIPPING
if (GDynamicRHI)
{
    const TCHAR* RHIName = GDynamicRHI->GetName();
    UE_LOG(LogPlanetaryCreation, Log, TEXT("[HeightmapExport][RHI] %s"), RHIName);
    ensureMsgf(FCString::Stristr(RHIName, TEXT("Null")) != nullptr,
        TEXT("Heightmap export expected NullRHI; got %s"), RHIName);
}
#endif
```

I did not find any RHI entry points in `HeightmapExporter.cpp`, `HeightmapSampling.cpp`, or their headers. The only texture creation calls I saw in the repo snapshot are unrelated exemplar work. With the assert above, you’ll catch any accidental GPU usage early in CI.

---

## 7) Safe test matrix & abort criteria

**Command‑line (keep what works):**
```
-Unattended -NullRHI -nop4 -nosplash --width <w> --height <h> [--force-large-export]
```

**Step‑ups (stop on first failure):**

1. 512×256 — baseline (already good). Expect `[PerformanceBudgetExceeded]` but logs intact.
2. 1024×512 — should pass and give you stable `[Coverage|SeamDelta]` metrics.
3. 2048×1024 — should pass post‑patch with total allocations ~36 MiB + sampler.
4. 3072×1536 — optional checkpoint; catches slippage before 4K.
5. 4096×2048 — only if **both**:
    - `PreflightHeightmapExport` passes with ≥512 MiB headroom, **and**
    - Previous step’s `SamplingMs + EncodeMs` < your budget and `[Coverage]` success ratio ≥99.5%.

**Hard abort conditions:**

- `Preflight` says “Need > Free” (log `[Preflight][Abort]` and do not allocate).
- Any single allocation checkpoint shows a delta > 1 GiB or `FreePhysicalMB < 1024`.
- Sampling time exceeds `PlanetaryCreation.Heightmap.SamplingBudgetMs × 2` (to avoid OS watchdog‑like conditions during encode on weak disks).

---

## 8) Windows crash forensics after a reboot

When the host resets, Unreal can’t write a crash dump. Use OS artifacts:

1. **Event Viewer** (System log):
    - Look for `Kernel-Power` **ID 41** (unexpected reset) at the exact time.
    - If it was a driver/hardware fault, you may also see `WHEA-Logger` **ID 18/19/1xx** or storage events.
2. **Minidumps**:
    - Check `C:\Windows\Minidump\*.dmp` (post‑bugcheck) and `C:\Windows\MEMORY.DMP`.
    - Check live kernel dumps under `C:\Windows\LiveKernelReports\` (e.g., `WATCHDOG\*.dmp` indicates a TDR‑like GPU reset even under NullRHI).
3. **Enable/verify dump capture** (run once, elevated):
    - GUI: `sysdm.cpl` → Advanced → Startup and Recovery → “Write debugging information” → *Automatic memory dump*; small dump directory: `%SystemRoot%\Minidump`.
    - PowerShell:
      ```powershell
      Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl" |
        Select-Object CrashDumpEnabled, DumpFile, MinidumpDir
      ```
4. **Pull recent events quickly**:
   ```powershell
   Get-WinEvent -LogName System -MaxEvents 200 |
     Where-Object { $_.Id -in 41, 1001, 18, 19 } |
     Format-Table TimeCreated, Id, ProviderName -AutoSize
   ```
5. **Correlation**: line up those times against your `[HeightmapExport][Preflight/AfterPixelBufferAlloc/AfterPNGCompression]` breadcrumbs.

---

## 9) Extra tightening you may want (not required to fix the freeze)

- **Use `float` for seam scratch** (I did above) — no need for `double`.
- **Early free**: `RawData.Reset(); RawData.Shrink();` right after compression to lower post‑encode peak before file I/O.
- **Clamp worker concurrency** for the sampling pass if the host has few cores and high memory pressure. (Less important once you remove the big arrays.)
- **Sampler cache**: If exports are frequent with the same mesh, consider caching KD‑tree + adjacency across runs (lifetime: until Stage B topology changes). That turns the `SamplerBytes` term into a one‑time cost.

---

## 10) What I verified in your snapshot

- Exporter currently allocates: `ImageData<FColor>`, `ElevationSamples<double>`, `SampleSuccess<uint8>`, `SampleTraversalSteps<uint8>`, then builds a second `RawData<uint8>` before PNG compression.
- Seam metrics read only the two edge pixels per row afterward.
- `FHeightmapSampler` builds adjacency & KD‑tree, logs memory via `GetMemoryStats()`, and **does not** touch any RHI APIs.
- No exporter‑path references to texture creation or compression on GPU.

---

## Suggested PR structure

1. **Commit 1 (functional no‑risk):** Remove `ImageData` and write straight into `RawData`. Keep telemetry arrays temporarily.  
   _Effect:_ −32 MiB @ 4K.
2. **Commit 2 (telemetry streaming):** Replace the three per‑pixel telemetry arrays with per‑row scratch + reduction.  
   _Effect:_ −∼80 MiB @ 4K.
3. **Commit 3 (preflight):** Add `PreflightHeightmapExport` and `[Preflight]` logging + early abort.
4. **Commit 4 (sanity):** Add `RHI` info log/ensure and two `TRACE_CPUPROFILER_EVENT_SCOPE`s.

These are small, isolated diffs; each has a measurable drop in `[AfterPixelBufferAlloc]` peaks and should keep you safe while you re‑enable 4K.

---
