# Exemplar Audit

## Victory Baseline (2025-10-14 12:26:39)
- **Stage B CSV:** `Docs/Validation/ExemplarAudit/O01_stageb_20251014_122639.csv`
- **Analyzer Metrics:** `Docs/Validation/ExemplarAudit/O01_metrics_20251014_VICTORY.csv`
- **Comparison PNG:** `Docs/Validation/ExemplarAudit/O01_comparison_20251014_VICTORY.png`
- **Guardrails:** `|mean_diff_m| ≈ 13.38`, `max_abs_diff_m ≈ 1.436 km` (perimeter spike), `mask_valid_fraction = 1.0`, `[StageB][ForcedMiss] = 0`
- Result uses forced exemplar `O01`, NullRHI, render LOD 6, and the ProcessStartInfo environment hydration fix.

## Reproducing the Pipeline
1. `powershell.exe -ExecutionPolicy Bypass -File .\Scripts\RunExportHeightmap512.ps1 -RenderLOD 6`
2. `powershell.exe -ExecutionPolicy Bypass -File .\Scripts\RunExemplarAnalyzer.ps1 -TileId O01 -StageCsv <CSV> -MetricsCsv <CSV> -ComparisonPng <PNG>`
3. The analyzer defaults to guardrails of `|mean_diff_m| ≤ 50`, `interior max_abs_diff_m ≤ 100`, and perimeter spike warning at `750 m`. Override with `-MeanDiffThreshold`, `-InteriorDiffThreshold`, or `-SpikeWarningThreshold` if needed.
4. Enable perimeter masking only for debugging via `-EnablePerimeterMask`. Production runs keep it disabled to report the full footprint.

## Operational Notes
- The forced exemplar suite must run with `-NullRHI`. The commandlet and the in-editor export button both inject `PLANETARY_STAGEB_FORCE_CPU=1`, `PLANETARY_STAGEB_FORCE_EXEMPLAR=O01`, `PLANETARY_STAGEB_DISABLE_RANDOM_OFFSET=1`, and `PLANETARY_STAGEB_RENDER_LOD=<current>` so the paths stay aligned.
- One high-diff perimeter pixel (≈1.4 km) remains documented while Stage B transition regions are finalized; automation warns at `750 m` but does not fail yet. Track any changes in `Docs/Validation/ExemplarAudit/20251014_phase1_4_seam_fix_implementation.md`.
- Analyzer logs now echo the guardrail thresholds and flag any violations in stdout before writing metrics. The CSV captures the thresholds alongside the summary rows for downstream tooling.
