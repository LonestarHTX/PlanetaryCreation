# Simulation Parameter Quick Reference

This sheet captures the core simulation knobs now that the system operates in meters (with Unreal units handled via `MetersToUE`). Defaults come from `FTectonicSimulationParameters`; practical ranges reflect values we exercised during Phase 3 validation.

## Planet Scale & Mesh

| Parameter | Units | Default | Practical Range | Notes |
|-----------|-------|---------|-----------------|-------|
| `PlanetRadius` | m | 127,400 | 10,000 - 10,000,000 | Physical radius used for all geodesic math; keep consistent between runs for determinism. |
| `SubdivisionLevel` | - | 0 | 0 - 3 | Controls plate count (20 -> 1280). Higher values increase sim cost. |
| `RenderSubdivisionLevel` | - | 5 | 0 - 6 | Visualization mesh density (20 -> 81,920 faces). Paper defaults launch at L5; drop lower when profiling CPU-only paths. |
| `LloydIterations` | - | 8 | 0 - 10 | Plate centroid relaxation passes; reduce when rapid prototyping. |
| `ElevationScale` | - | 1.0 | 0.5 - 3.0 | Multiplies stress-derived uplift; with the meter pipeline, 1.0 ~ 100 m per 1 MPa. |

## Plate Topology Controls

| Parameter | Units | Default | Practical Range | Notes |
|-----------|-------|---------|-----------------|-------|
| `SplitVelocityThreshold` | rad / My | 0.05 | 0.02 - 0.07 | Divergent speed needed before a ridge starts splitting. |
| `SplitDurationThreshold` | My | 20.0 | 10 - 30 | Time the ridge must exceed the velocity threshold to trigger a split. |
| `MergeStressThreshold` | MPa | 80.0 | 60 - 100 | Convergent stress required to consume a plate. |
| `MergeAreaRatioThreshold` | ratio | 0.25 | 0.1 - 0.4 | Smaller plate must be below this fraction of its neighbor to merge. |
| `bEnablePlateTopologyChanges` | toggle | false | - | Enable once the thresholds above are tuned for the scenario. |

## Hotspot & Boundary Shaping

| Parameter | Units | Default | Practical Range | Notes |
|-----------|-------|---------|-----------------|-------|
| `MajorHotspotCount` | count | 3 | 0 - 6 | Large mantle plumes; pair with thermal multipliers below. |
| `MinorHotspotCount` | count | 5 | 0 - 12 | Smaller plumes. |
| `HotspotDriftSpeed` | rad / My | 0.01 | 0 - 0.03 | Mantle-frame hotspot drift. |
| `MajorHotspotThermalOutput` | multiplier | 2.0 | 1.5 - 3.0 | Stress/thermal boost from major hotspots. |
| `MinorHotspotThermalOutput` | multiplier | 1.0 | 0.5 - 1.5 | Baseline for minor hotspots. |
| `bEnableHotspots` | toggle | false | - | Turn on to couple thermal field to stress. |
| `bEnableVoronoiWarping` | toggle | true | - | Applies noise to plate boundaries. |
| `VoronoiWarpingAmplitude` | fraction | 0.5 | 0.0 - 0.7 | Higher values create rougher boundaries. |
| `VoronoiWarpingFrequency` | - | 2.0 | 1.0 - 4.0 | Noise frequency for plate shapes. |

## Rift Propagation

| Parameter | Units | Default | Practical Range | Notes |
|-----------|-------|---------|-----------------|-------|
| `RiftProgressionRate` | m / My / (rad / My) | 50,000 | 30,000 - 80,000 | Controls widening speed proportional to divergence. |
| `RiftSplitThresholdMeters` | m | 500,000 | 300,000 - 700,000 | Rift width that triggers plate creation. |
| `bEnableRiftPropagation` | toggle | false | - | Works best with topology changes enabled. |

## Surface Weathering & Ocean Dampening

| Parameter | Units | Default | Practical Range | Notes |
|-----------|-------|---------|-----------------|-------|
| `ErosionConstant` | m / My | 0.001 | 0.0005 - 0.005 | Higher values erode mountains faster. |
| `SeaLevel` | m | 0.0 | -500 - 2,000 | Reference elevation for "above sea level" checks. |
| `bEnableContinentalErosion` | toggle | false | - | Enables continental erosion pass each step. |
| `SedimentDiffusionRate` | fraction | 0.1 | 0.05 - 0.2 | Percentage of sediment diffused per step (Stage 0). |
| `bEnableSedimentTransport` | toggle | false | - | Activates sediment diffusion. |
| `OceanicDampeningConstant` | m / My | 0.0005 | 0.0002 - 0.001 | Seafloor smoothing rate. |
| `OceanicAgeSubsidenceCoeff` | m / sqrtMy | 350.0 | 250 - 400 | Depth gain per sqrtage for oceanic crust. |
| `bEnableOceanicDampening` | toggle | false | - | Turns on age-based subsidence and seafloor smoothing. |

## Stage B Amplification

| Parameter | Default | Notes |
|-----------|---------|-------|
| `bEnableOceanicAmplification` | true | Paper default; disable via `r.PlanetaryCreation.PaperDefaults 0` or the Stage B toggle when profiling CPU baselines. |
| `bEnableContinentalAmplification` | true | Paper default matching the published exemplar pipeline; revert with `r.PlanetaryCreation.PaperDefaults 0`. |
| `bEnableHydraulicErosion` | true | Stream-power routing on amplified terrain; toggle with `r.PlanetaryCreation.EnableHydraulicErosion 0/1`. |
| `bSkipCPUAmplification` | true | GPU preview renders Stage B directly; set to false if you need the CPU fallback for comparison tests. |
| `MinAmplificationLOD` | 5 | Stage B only runs at or above this render LOD. |
| `HydraulicErosionConstant` | 0.002 | Base stream-power strength (m/My); increase for more aggressive valley carving. |
| `HydraulicAreaExponent` | 0.5 | Flow accumulation exponent (A<sup>m</sup>) in stream-power law. |
| `HydraulicSlopeExponent` | 1.0 | Slope exponent (S<sup>n</sup>) in stream-power law. |
| `HydraulicDownstreamDepositRatio` | 0.5 | Fraction of eroded material transported to the downhill neighbour (remainder redeposits locally). |
| `UseGPUHydraulic` | 1 | Present for future GPU work; currently routes to the optimised CPU pass even when enabled. |

## Usage Tips

- Adjust `PlanetRadius` and LODs first; other distances (camera, rift widths) should then make intuitive sense.
- Toggle erosion/sediment/dampening together to observe the full Phase 5 weathering workflow.
- Use `r.PlanetaryCreation.EnableHydraulicErosion 0/1` to benchmark Stage B with or without the stream-power pass; the CPU implementation costs ~1.7 ms at L7.
- Keep deterministic runs locked to a single parameter set; changing any value mid-run alters snapshot fingerprints.
- Use `r.PlanetaryCreation.PaperDefaults 0/1` to flip between the paper-authentic defaults (LOD 5, Stage B/GPU/PBR on) and the lighter M5 baseline for CPU-only profiling.
