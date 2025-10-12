# Heightmap Export Overhaul Relaunch Briefing

## Current Snapshot
- Stage B heartbeat logging (`[StageBDiag]`) ships in `UTectonicSimulationService::AdvanceSteps`; `Scripts/RunStageBHeartbeat.ps1` now captures a clean NullRHI pulse (Saved/Logs/StageBHeartbeat-20251011-203516.log) with Steps 1–4 reporting `Ready=1`, amplified/render counts of 642, and finite sample checks.
- Temporary isotropic Stage B spike (`ShouldUseTemporaryIsotropicStageB`) and hydraulic erosion gating are live, so Day 1 verification can produce detailed terrain once the heartbeat run succeeds.
- Heightmap exporter now depends on `FHeightmapSampler` and records coverage/seam telemetry; automation stays green but parity suites are paused until the heartbeat check clears.
- GPU shader permutations accept SM5/SM6, eliminating the earlier `FOceanicAmplificationCS` crash; Windows rebuild is still required before rerunning automation.
- VIS palette toggles, CLI flags, and readiness delegate wiring landed; UI/CLI copy reads `PlanetaryCreation::StageB::GetReadyReasonDescription`.

## Critical Blockers
- `[RidgeDiag] Vertex … missing cache tangent` warnings persist in the latest heartbeat log; STG must close the tangent gap before ALG/QLT resume heavy automation.
- Stage B profiling/heartbeat output still needs to pair with an isotropic export capture so QLT can compare telemetry vs visuals.
- Need explicit pairing windows to coordinate the isotropic spike export (STG ↔ ALG) once Stage B readiness is validated.
- Agent registry/listeners drifted during the pause; confirm ownership + contact info before reassigning tasks.

## Immediate Actions
1. **Resolve ridge tangent warnings:** STG reviews `Saved/Logs/StageBHeartbeat-20251011-203516.log` to patch the cache population so `[RidgeDiag] Summary` reports `Missing=0`; rerun the heartbeat script afterward to confirm the warnings clear and log the new archive.
2. **Distribute recovery briefing:** share this document link in `#planetary-heightmap` with references to the updated heartbeat archive and outstanding warnings so all agents resume on the same baseline.
3. **Reconfirm task ownership:** update `Docs/agent_registry.md` (done below) and have each agent acknowledge their “Status | Blockers | Next” line in their log after reviewing the refreshed heartbeat log.
4. **Pair for isotropic spike validation:** after the ridge fix, STG captures the Day 1 isotropic export PNG and posts ridge/tangent stats so ALG can resume seam telemetry tuning; QLT uses the paired `[StageB][Profile]` output for regression thresholds.

## Agent Restart Checklist
- **ALG-Agent**
  - Review Saved/Logs/StageBHeartbeat-20251011-203516.log and stay paused until STG clears the ridge tangent warnings.
  - Once cleared, resume ALG-03 seam telemetry review followed by ALG-04 seam stabilization verification with the isotropic export.
  - Coordinate with QLT on thresholds once Stage B coverage numbers are verified.
- **STG-Agent**
  - Own the ridge tangent fix, rerun the heartbeat script, and publish the refreshed archive + coverage summary.
  - Capture `[StageB][Profile]` timings (expect Hydraulic ≈1.6–1.8 ms) and post PNG from the isotropic spike after the rerun.
  - Progress STG-02 gating audit using the new heartbeat reason enum and confirm erosion remains disabled.
- **VIS-Agent**
  - Validate palette toggle states against the readiness reason strings once STG republishes the clean heartbeat.
  - Stage doc updates but keep merges queued until ALG/QLT confirm seam metrics.
- **QLT-Agent**
  - After the ridge fix + isotropic export, prepare seam delta automation upgrades (`FHeightmapSeamContinuityTest`) and CPU/GPU parity harness.
  - Capture baseline perf numbers from the refreshed `[StageB][Profile]` output to seed benchmarks.
- **OPS-Agent**
  - Update release checklist drafts with the heartbeat archive step.
  - Ensure CLI docs reference the new readiness warnings and log expectations.

## Supporting Notes
- Stage B readiness helper: `UTectonicSimulationService::IsStageBAmplificationReady()` with reasons from `EStageBAmplificationReadyReason`.
- Heartbeat sanity expectations:
  - `Ready=1`, `Amplified=Render`, `Match=yes`
  - `Finite=3/3`, `NonZero>=1` for the sampled indices
  - `[StageB][Profile]` line reporting `Hydraulic` time even if erosion is temporarily gated off (should be ~0 when disabled).
- Latest sampler metrics live in `[HeightmapExport][Coverage|SeamDelta]`; verifying them requires running an export after the heartbeat log is captured.
- When running additional automation from WSL, remember to hop through `powershell.exe` if GPU access is required; NullRHI still skips Stage B GPU permutations.
