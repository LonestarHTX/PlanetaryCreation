# Automation Quick Start

## Paper Ready preset
- Open the **Tectonic Tool** tab (Editor toolbar → Planetary Creation → Tectonic Tool).
- Click **Paper Ready** to load the paper-authentic configuration (seed 42, LOD 5 render mesh, GPU Stage B, erosion/dampening enabled).
- The preset clears the STG‑04 hydraulic latch, forces a Stage B rebuild, blocks on GPU readbacks, and pre-warms neighbouring LODs.
- Watch the output log for `[PaperReady] Applied … StageBReady=true`; automation can grep for this marker before continuing.
- After the button finishes the Stage B panel should show _Ready_ and the preview mesh will refresh with amplified detail.
- The button also disables any dev-only GPU replay overrides so production runs respect the normal hash checks.

> Tip: No extra console work is required—the button takes care of `r.PlanetaryCreation.PaperDefaults`, GPU amplification, profiling CVars, and surface-process toggles.

## Export Heightmap workflow
- In the same panel, click **Export Heightmap…** to trigger the 512×256 heightmap commandlet run.
- The button logs `[PaperReady] UsingExportButton`, applies the Paper Ready preset if it has not already completed, and then launches `UnrealEditor-Cmd.exe` with `Saved/Scripts/RunHeightmapExport.py`.
- The export runs headless with `-NullRHI -unattended -nop4 -nosplash` and waits for completion. The command line uses `-SetCVar=r.PlanetaryCreation.PaperDefaults=0,r.PlanetaryCreation.UseGPUAmplification=0,r.PlanetaryCreation.SkipCPUAmplification=0` so CVars land before automation queues.
- Both the button and `Scripts/RunExportHeightmap512.ps1` inject `PLANETARY_STAGEB_FORCE_EXEMPLAR=O01`, `PLANETARY_STAGEB_FORCE_CPU=1`, `PLANETARY_STAGEB_DISABLE_RANDOM_OFFSET=1`, and the current render LOD to keep forced runs deterministic. The Python helpers abort if `-NullRHI` is missing.
- On success you’ll see a confirmation dialog plus `[HeightmapExport] Completed Path=<Docs/Validation/...png>` in the log; failures surface the return code and the log contains the detailed reason.

## Exemplar Analyzer quick run
- `Scripts/RunExemplarAnalyzer.ps1 -TileId O01 -StageCsv <path> -MetricsCsv <path> -ComparisonPng <path>` wraps the analyzer under NullRHI and now exposes `-MeanDiffThreshold`, `-InteriorDiffThreshold`, `-SpikeWarningThreshold`, and `-EnablePerimeterMask`.
- Defaults match the automation guardrails: `|mean_diff_m| ≤ 50`, interior delta ≤ 100, spike warning at `750 m`. Overrides propagate to `Scripts/analyze_exemplar_fidelity.py`, which echoes the guardrails before writing metrics.
