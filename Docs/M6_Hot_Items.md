# M6 Hot Items

- **Current status:** CPU baseline realigned with mask-aware replay; parity logs now emit `[GPUOceanicParity][CPUBaselineMismatchResolved]` and `[StageB][Profile]` Step 6 shows BaselineReuse 0 / MaskMismatch 0. Snapshot guard (`Saved/Logs/SnapshotGuard-2025-10-11e.log`) and GPU parity (`Saved/Logs/StageBParity-2025-10-11e.log`) both succeeded at 50 ms throttle.
- GPU kernels respect oceanic mask; continental vertices untouched (`Saved/Logs/SnapshotGuard-2025-10-11f.log`, `Saved/Logs/StageBParity-2025-10-11f.log`).
- **Critical path:** Distribute the new archives + baseline correction notes to ALG/STG/VIS/QLT → bake the alignment update into automation/telemetry → proceed toward the M7 preview readiness brief.
- **Hardware constraint:** RTX 3080 still requires `r.PlanetaryCreation.StageBThrottleMs=50` to avoid Kernel-Power Event 41 resets; do not drop below 50 ms without QLT sign-off.
- **Next milestone gate:** M7 preview readiness after parity verification notes land and Stage B telemetry review is complete.
- **Stage B parity:** Heightmap sampler/export now consume the Stage B snapshot float buffers; seam delta instrumentation is driven by sampler metrics and surfaces through `[HeightmapExport][SeamDelta]`.
