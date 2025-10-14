# Validation Artifacts

Tiled exporter outputs for comparison and automation.

| File | Notes |
| --- | --- |
| `Tiled_512x256.png` | Tile pipeline (NullRHI) – matches `Baseline_512x256.png`. |
| `Heightmap_2048x1024.png` | Post-fix signature + seam repair; opens cleanly, 99.923 % coverage. |
| `Heightmap_4096x2048.png` | Supervised real-RHI run (seam aggregate 0.072 m/0.500 m, coverage 99.844 %). |
| `StageB_O01_512x256.png` | Forced exemplar export (Stage B globe, 512×256, NullRHI). |
| `ExemplarAudit/O01_comparison.png` | Stage B vs SRTM O01 (Stage B, source, diff heatmap). |
| `ExemplarAudit/O01_metrics.csv` | Numerical comparison (mean/max Δ, hypsometric curve, slope histogram). |
| `ExemplarAudit/O01_stageb_20251014_003719.csv` | Stage B raw sample (LOD 7, seam fallback fix, NullRHI). |
| `ExemplarAudit/O01_metrics_20251014_003719.csv` | Analyzer output via `RunExemplarAnalyzer.ps1` (mean Δ ≈ 341 m). |
| `ExemplarAudit/O01_comparison_20251014_003719.png` | Stage B vs O01 diff after seam sampling patch. |

## Milestone 3 Automation Flow
- Run the full regression harness (geometry + quantitative metrics) with<br>
  `powershell.exe -ExecutionPolicy Bypass -File .\Scripts\RunMilestone3Tests.ps1 -ArchiveLogs`.
- Logs mirror to `Saved/Logs/Automation/<timestamp>_Milestone3Suite.log` when `-ArchiveLogs` is supplied; the live log remains at `Saved/Logs/PlanetaryCreation.log`.
- Quantitative coverage CSVs are emitted to `Saved/Metrics/heightmap_export_metrics.csv`; each successful run mirrors the file to `Docs/Validation/heightmap_export_metrics_<timestamp>.csv` for historical comparisons.

## Known Noise
- Commandlet runs may log `LogUsd: Warning: Failed to parse plugInfo.json` — treat this as a known warning per Phase 3 guidance (no suppression in place yet; continue tracking in planning docs).
- `[HeightmapExport][PerformanceBudgetExceeded]` warnings appear on large runs;
  informational only.

## Stage B Rescue Telemetry
- The exporter now emits `[StageB][RescueSummary]` lines whenever the rescue path runs. Current fields capture readiness transitions (`StartReady`, `FinishReady` with human-readable reasons), rescue utilisation (`RescueAttempted`, `RescueSucceeded`, `AmplifiedUsed`, `SnapshotFloat`), the image geometry (`Image`, `Pixels`), and coverage quality (`Coverage`, `Miss`).
- Failure modes are broken out explicitly: aggregate attempts/successes (`FallbackAttempts`, `Success`, `Fail`), expansion efficiency (`ExpandedSuccess`, `ExpandedAttempts`), and the per-mode histogram `Modes[...]` (notably `RowReuse`, which tracks how many rows were replayed from cached Stage B output).
- Ridge fallback classification lives in `RidgeFallbacks[...]` with counts for `Dirty`, `Gradient`, `Plate`, and `Motion` buckets.
- Latest supervised captures (2025‑10‑12 20:32 and 20:40 UTC) report 100 % coverage on 1024×512 and 4096×2048 exports with `Fail=0`, confirming the rescue passes now clear without gaps. RowReuse recorded at 184 (1 K) and 13 068 (4 K), matching expectations for the tiled replay.

## Supervised Export Helpers
- `Scripts/ExportHeightmap1024.py` and `Scripts/ExportHeightmap4096Force.py` run inside the editor commandlet and temporarily set `r.PlanetaryCreation.AllowUnsafeHeightmapExport` to `1` before restoring it to `0`. Both scripts call `UTectonicSimulationService::export_heightmap_visualization()` with the specified resolution and log the output path or error.
- `Scripts/RunExportHeightmap512.ps1` accepts optional `-RawExportPath` (defaults to a timestamp under `Docs/Validation/ExemplarAudit`) and `-TimeoutSeconds` so forced exemplar captures stop clobbering prior CSVs and can auto-terminate hung runs.
- `Scripts/RunExemplarAnalyzer.ps1` convenience wrapper sets the analyzer environment variables and launches the commandlet with `analyze_exemplar_fidelity.py`; pass `-TileId`, `-StageCsv`, `-MetricsCsv`, and `-ComparisonPng` to align exports and metrics in one step.
- Launch them from WSL with `powershell.exe` so Windows handles the RHI correctly. Replace `<UE5>` with the actual install path:
  ```powershell
  powershell.exe -Command "& '<UE5>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject' -AllowCommandletRendering -unattended -nop4 -nosplash -log -ExecutePythonScript='C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\Scripts\ExportHeightmap1024.py'"
  ```
  ```powershell
  powershell.exe -Command "& '<UE5>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject' -AllowCommandletRendering -unattended -nop4 -nosplash -log -ExecutePythonScript='C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\Scripts\ExportHeightmap4096Force.py'"
  ```
- The helpers log to `Saved/Logs/PlanetaryCreation.log` and roll to timestamped backups (e.g., `Saved/Logs/PlanetaryCreation-backup-2025.10.12-20.32.15.log` for 1024×512, `PlanetaryCreation-backup-2025.10.12-20.40.33.log` for 4096×2048). Expect `[StageB][RescueSummary]` with `Fail=0` and a matching `[HeightmapExport][Coverage]` line when Stage B is healthy.
- Recommended CVars when supervising large exports: let the helpers handle `r.PlanetaryCreation.AllowUnsafeHeightmapExport` and keep Stage B defaults unless you explicitly need the lean baseline (`-SetCVar=r.PlanetaryCreation.PaperDefaults=0`). When pairing a 4K capture with GPU parity suites, add `-SetCVar=r.PlanetaryCreation.StageBThrottleMs=50` to avoid watchdog timeouts.

## Quantitative Metrics
- `Saved/Metrics/heightmap_export_metrics.csv` is generated by `UTectonicSimulationService::ExportQuantitativeMetrics()`
  and mirrored to a timestamped filename (`heightmap_export_metrics_YYYYMMDD_HHMMSS.csv`) for historical tracking.
  The latest captured snapshot lives in `Docs/Validation/heightmap_export_metrics_20251012_181150.csv` for quick comparison.
  The CSV includes:
  - `[HypsometricCurve]` section with 50 elevation bins (absolute meters, % coverage).
  - `[PlateVelocityHistogram_cm_per_year]` section capturing vertex velocity magnitudes converted to cm/year.
  - `[RidgeTrenchLengths_km]` summary for divergent, convergent, and transform boundary lengths plus ridge:trench ratio.
    Latest baseline (2025‑10‑12): Ridge ≈ 2679 km, Trench ≈ 1552 km, Ratio ≈ 1.73 (test allows ±10 %).
  - `[TerraneAreaPreservation]` samples with per-terrane area drift and `[TerraneAreaSummary]` statistics (mean/max/RMS).
- Post-rescue Milestone 3 run (2025‑10‑12 19:38 UTC) continues to mirror zero-drift terrane samples (mean/max drift 0 %, sample count 1) and preserves the ridge:trench ratio within tolerance, so no action needed on drift thresholds.
- Automation coverage resides in `Source/PlanetaryCreationEditor/Private/Tests/QuantitativeMetricsExportTest.cpp`
  and enforces hypsometric normalization, ridge/trench tolerances (1.60–1.90 ratio band), and a ≤5 % terrane area drift envelope.
