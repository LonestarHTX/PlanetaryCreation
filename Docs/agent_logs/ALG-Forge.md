# ALG-Forge Log

## 2025-10-09 – SampleElevationAtUV Implementation
- Implemented reusable `FHeightmapSampler` helper powering `SampleElevationAtUV` (KD-tree search + barycentric interpolation with Stage B preference).
- Added `PlanetaryCreation.Heightmap.SampleInterpolation` automation test validating baseline vs amplified sampling on a controlled mesh subset.
- Validation: rebuilt `PlanetaryCreationEditor Win64 Development` and executed automation (`PlanetaryCreation.Heightmap.SampleInterpolation`, `PlanetaryCreation.Milestone6.HeightmapVisualization`).

## 2025-10-09 – paper_faithful_shape kickoff
- Scope alignment: reviewed `Docs/paper_faithful_shape.md` §§1–4 and `Docs/heightmap_debug_review.md` to restate exporter goals (triangle sampling, seam continuity, normalized palette option).
- Exporter audit:
  - `UTectonicSimulationService::ExportHeightmapVisualization` builds an `FHeightmapSampler` per export and drives a pixel-space `ParallelFor`; the legacy vertex-splat/dilation path appears removed.
  - `FHeightmapSampler` constructs face adjacency each export, seeds queries from a centroid KD-tree, and falls back to the first vertex of the nearest face when barycentrics fail, which can reintroduce seam noise if we miss a face.
  - `UVToDirection` wraps U via `FMath::Frac` and clamps V close to the poles; automation only asserts seam columns are non-empty, so we still lack delta metrics or coverage logging.
  - Stage B readiness check still looks at `AmplifiedElevation.Num() == RenderVertices.Num()`, so we cannot detect partially built amplification without STG's latched flag.
- ALG-03/ALG-04 implementation plan:
  1. Delete leftover vertex-splat/dilation support files/config and route exports exclusively through `SampleElevationAtUV`.
  2. Harden triangle search: extend adjacency walks, add seam wrap guards for `U≈0/1`, expose configurable pole epsilon, and eliminate the "nearest vertex" fallback.
  3. Introduce instrumentation (per-export coverage %, seam delta stats) and tighten automation (`PlanetaryCreation.Milestone6.HeightmapSeamContinuity` upgrade plus new seam delta threshold test).
  4. Capture performance baselines (KD-tree build, parallel sampling) and feed them into ALG-05 regression logging.
  5. Validation: run coverage/seam automation after each change and line up exports vs Stage B preview to confirm <1 m seam delta.
- Dependencies / asks:
  - Need STG's forthcoming `bStageBAmplificationReady` latch (or equivalent callback) so we know when to trust amplified elevations.
  - Prefer a reusable triangle/KD cache from the mesh service to avoid rebuilds on repeated exports; will sync with STG if they can surface it.
- Questions for MNG/STG:
  - Is it acceptable to cache triangle adjacency across exports until a new mesh build?
  - Any runtime budget constraints for the enhanced seam delta automation?
- Coordination notes:
  - Stage B orientation data: `FVector3f` precision is acceptable for ridge/fold vectors as long as the API documents the unit-length expectation; no double-buffer required on ALG side.
  - Seam delta + coverage instrumentation (ALG-03/04 telemetry) targeted for check-in by 2025-10-11 so VIS can hook metrics in their automation/docs backlog immediately after.
  - Requesting a latched `IsStageBAmplificationReady()` accessor plus an optional multicast notification when Stage B recomputes; polling is fine short-term, but an event hook would simplify cache invalidation when amplification reruns.

## 2025-10-09 – Seam stabilization + telemetry
- Hardened `FHeightmapSampler` (`HeightmapSampling.h:17`, `HeightmapSampling.cpp:168`) with plane-projected barycentrics, bounded triangle walks, and per-sample stats to avoid seam fallback noise.
- Exporter now records coverage + seam metrics and emits `[HeightmapExport][Coverage|SeamDelta]` logs for automation consumers (`HeightmapExporter.cpp:93-191`).
- `PlanetaryCreation.Heightmap.SampleInterpolation` asserts triangle hits via the new sample info (`HeightmapSamplerTest.cpp:110-134`); seam delta automation still pending QLT hook-up.
- UnrealBuildTool compile failed due to upstream Stage B latch/palette changes (`TectonicSimulationService.h` duplicates, `CachedPaletteMode` rename). Need STG fixes before full validation rerun.

## 2025-10-10 – Automation test unblocked
- Build failures traced to the wrong delegate include in `SPTectonicToolPanel.h:9`; swapped `Delegates/DelegateHandle.h` for `Delegates/Delegate.h`, restoring access to `FDelegateHandle`.
- Adjusted `FHeightmapSampler` test expectations to reflect plane-projected barycentrics: centroid sampling plus using the sampler’s returned weights keeps interpolation deterministic (`HeightmapSamplerTest.cpp:74`, `:125`).
- Added a sanity check that amplified elevation diverges from baseline by ~50 m so regression catches Stage B plumbing breaks.
- Rebuilt `PlanetaryCreationEditor Win64 Development` and re-ran `Automation RunTests PlanetaryCreation.Heightmap.SampleInterpolation`; both now pass (`EXIT CODE: 0`).

## 2025-10-10 – Plan realignment briefing
- Critical path updated: Day 0 Stage B heartbeat diagnostic (STG) must pass before sampler/exporter work continues.
- `ALG-03` pulled forward to Day 1 to finish removing vertex splat remnants and rely solely on `FHeightmapSampler`; follow with `ALG-04` seam telemetry on Day 2.
- Inline validation expectations remain: rerun `PlanetaryCreation.Heightmap.SampleInterpolation` + seam telemetry suite after integrating the heartbeat latch and isotropic spike.
- Coordinate with STG on the Day 1 isotropic amplification export so seam metrics capture terrain with visible detail; document results in the next log entry.

## 2025-10-11 – SM6 permutation fix pending automation
- Relaxed `FOceanicAmplificationCS`/preview/continental shader permutations to compile for platforms reporting `MaxFeatureLevel` SM5 or SM6, clearing the assert on SM6-capable DX12.
- Rebuilt `PlanetaryCreationEditor Win64 Development` successfully after the shader change.
- Attempted to rerun `Automation RunTests PlanetaryCreation.Milestone6.HeightmapVisualization` via WSL, but the command still hits `UtilBindVsockAnyPort` (WSL restriction on launching `UnrealEditor-Cmd.exe`). Needs a native Windows shell invocation to capture `[StageBDiag]`/`[StageB][Profile]` output.
- Next action once automation is re-run from Windows: log Stage B Ready/finite sample status and metric deltas here.
