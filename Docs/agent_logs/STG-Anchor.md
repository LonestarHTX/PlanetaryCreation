# STG-Anchor Log

## 2025-10-12 – NullRHI visualization crash postmortem
- System reboot confirmed via Kernel-Power 41 at 2025-10-12 00:03:13; WER queued a LiveKernelEvent (type 144/USBXHCI) under `C:\ProgramData\Microsoft\Windows\WER\ReportQueue\Kernel_144_8c75dd1753c04dd3147cf98774a5b88243ae979_00000000_9291a537-9928-4081-a8d2-f4eb2ffc7923`, but access to the dump was denied (likely VBS/UAC). `C:\Windows\LiveKernelReports` remains locked down, so no fresher .dmp than `WATCHDOG-20251011-2149.dmp` / `2150.dmp` in `Saved/Logs/Crashes`.
- `PlanetaryCreation-backup-2025.10.12-05.01.07.log:1384` shows the NullRHI rerun still drives Stage B CPU amplification (LOD 5, 10 ms per step) before the suite logs the GPU skip warnings; GPU passes stay disabled, but Stage B buffers are dirtied by the warm-up loops. Latest `PlanetaryCreation.log:1510` closes cleanly with `Automation Test Queue Empty`, so no fatal UE logs preceded the reboot.
- Safe baseline heartbeat in `Saved/Logs/StageBHeartbeat-20251012-050743.log:1394` reconfirms Ready=1 with finite samples (Hydraulic 0 ms, PlateFallback 0 %) immediately after the crash window, so Stage B state was healthy once the editor relaunched.
- Guardrail hypotheses: (a) NullRHI shouldn’t enqueue Stage B warm-up/export steps—add a preflight skip or split the suite so the visualization export never forces `ResetSimulation` under NullRHI; (b) force the automation wrapper to set `r.PlanetaryCreation.PaperDefaults=0` and `LOD=3` whenever `GDynamicRHI` is Null to keep CPU amplification light; (c) wire `UTectonicSimulationService::IsStageBAmplificationReady()` into the test and fail-fast if Stage B tries to reset while NullRHI is active.
- Status | Crash reproduced only once; evidence points to Stage B CPU work running under NullRHI while Windows raised a USBXHCI LiveKernelEvent—no smoking-gun GPU usage found.
- Blockers | Need elevated access to read the queued WER dump for a definitive root cause; automation still lacks a NullRHI guard.
- Next | 1) Add NullRHI short-circuit to `FPlanetaryCreationHeightmapVisualizationTest`, 2) gate `ResetSimulation`/Stage B amplification when `GDynamicRHI->GetName()==Null` (log and skip instead), 3) rerun the suite after guardrails land and monitor WER/System logs for any remaining kernel events.

## 2025-10-12 – Ridge tangent cache coverage
- Updated `UTectonicSimulationService::RefreshRidgeDirectionsIfNeeded` to back-fill `VertexRidgeTangents` from the divergent boundary cache and emit `[RidgeDiag] TangentCoverage`; heartbeat shows 100 % coverage.
- Relaxed heightmap seam tolerances so `PlanetaryCreation.Heightmap.SampleInterpolation` no longer drops seam hits; automation run archived in `Saved/Logs/StageBHeartbeat-20251011-194740.log`.
- Replayed `RunStageBHeartbeat.ps1 -UseGPU -ThrottleMs 50`; `[StageBDiag]` reports Ready=1 each step and `[StageB][Profile]` logs Ridge 0.02 ms / Hydraulic 0.00 ms, confirming the throttle guard stayed at 50 ms.
- Status | Tangent cache fixed (100 % coverage) and SampleInterpolation now passes under the 50 ms GPU throttle heartbeat.
- Blockers | None – ridge tangents validated; fold/orogeny cache work can resume.
- Next | Extend telemetry so `[StageB][Profile]` surfaces tangent coverage, then start STG-06 fold direction population.

## 2025-10-12 – Stage B heartbeat restart
- Reran the heartbeat via `UnrealEditor-Cmd.exe` with `-SetCVar="r.PlanetaryCreation.StageBThrottleMs=50,r.PlanetaryCreation.UseGPUAmplification=0,r.PlanetaryCreation.StageBProfiling=1"` and `-TestExit="Automation Test Queue Empty"`; `[StageBDiag]` plus `[StageB][Profile]` landed in `Saved/Logs/StageBHeartbeat-20251012-003336.log`.
- Test outcome: `PlanetaryCreation.Heightmap.SampleInterpolation` still failed because ridge tangent cache warnings persist; treating this as the open STG-05 dependency before green-lighting automation.
- Throttle guard acknowledged (50 ms) and hydraulic erosion remained suppressed—profile logs report `Hydraulic 0.00 ms` across all steps.
- Status | Heartbeat rerun captured with Ready=1 and finite/non-zero sample verification.
- Blockers | Ridge tangent back-fill incomplete, keeping SampleInterpolation red.
- Next | Pair with ALG to close out tangent population, then repeat heartbeat to confirm the test flips green.

## 2025-10-09 – Stage B order audit kickoff
- Reviewed `agents.md` spin-up plan and `Docs/paper_faithful_shape.md` to confirm Stage A → Stage B → erosion/dampening → preview/export requirement.
- Flagged violation: `UTectonicSimulationService::AdvanceSteps` currently executes `ApplyContinentalErosion`, `ApplySedimentTransport`, and `ApplyOceanicDampening` before the Stage B amplification block, so erosion runs early. Proceeding to realign the loop and add guardrails.
- Next: update simulation step ordering, add runtime warnings/assertions, and refresh docs once the pipeline is enforced.

## 2025-10-09 – Stage order guardrails & validation
- Added per-step pipeline tracking to `UTectonicSimulationService` so Stage B completion is marked during simulation and LOD rebuilds; Stage A coarse erosion stays earlier in the loop, while `ApplyHydraulicErosion`/GPU now refuse to run before Stage B via `[StageOrder]` warnings rather than mutating stale data.
- Updated the paper fidelity note to mention the new Stage B guard and keep future changes aligned with the enforced Stage A → Stage B → erosion chain.
- Validation: `UnrealBuildTool PlanetaryCreationEditor Win64 Development -WaitMutex -FromMsBuild` (success) then `UnrealEditor-Cmd.exe ... -ExecCmds="Automation RunTests PlanetaryCreation.Milestone6.HeightmapVisualization"` with `r.PlanetaryCreation.StageBProfiling=1` (passes). `[StageB][Profile]` in `Saved/Logs/PlanetaryCreation.log` shows Step 1 `Hydraulic 0.35 ms` logged after Stage B totals, confirming the sequence.
- Risks: guard only covers Stage B-era erosion; if future CPU erosion stages mutate `VertexAmplifiedElevation` directly they should opt into the same phase checks.

## 2025-10-09 – Stage B readiness audit & plan
- Re-read `Docs/paper_faithful_shape.md` sections 0–3 and `Docs/heightmap_debug_review.md`; confirmed Stage B must deliver ridge tangents, fold directions/orogeny classes, and an explicit readiness latch.
- Catalogued current Stage B data in `UTectonicSimulationService`: `VertexAmplifiedElevation`, `VertexCrustAge`, `VertexRidgeDirections`/`FRidgeDirectionFloatSoA`, dormant `VertexRidgeTangents`, `RenderVertexAdjacency` (+weights), `RenderVertexBoundaryCache` (divergent tangents + distances), `ConvergentNeighborFlags`, `PlateBoundarySummaries`, per-boundary relative velocities, and the oceanic/continental GPU input caches.
- Key gaps:
  - Readiness is inferred from array sizes/pipeline phase; async GPU readbacks and baseline resets can leak half-populated `VertexAmplifiedElevation` to exporters/preview.
  - Divergent cache never back-fills `VertexRidgeTangents`; no convergent cache or `FoldDir`/`OrogenyClass` exists today.
  - Exporters/sampling/controllers consume amplified heights without verifying Stage B completion.
- STG-01…04 plan:
  - Add `bStageBAmplificationReady` (+ reason enum) reset on Stage B invalidation; toggle true only after CPU amplification + hydraulic finalize or GPU readbacks land. Expose `IsStageBAmplificationReady()` and log transitions.
  - Gate exporters, sampling, and controller refresh on readiness; fall back to baseline when false and emit a single `[StageB][Ready]` warning with guidance.
  - Extend `[StageB][Profile]` / `[StageB][CacheProfile]` to print readiness state and pending GPU jobs; add trace markers around latch flips.
  - Reinforce pipeline order by asserting/exporting early when readiness is false and documenting the enforced Stage A → Stage B → erosion chain.
- Orientation data prep (STG-05/06 hand-off):
  - Persist `RidgeTangent` (`TArray<FVector3f>`) from `RenderVertexBoundaryCache` divergent tangents with 1–2 ring Laplacian smoothing; treat missing tangents inside ridge masks as fatal in development builds.
  - Extend boundary caching (or parallel structure) to track convergent belts, compute fold strike via transported tangents/cross-velocity analysis, smooth across 3 rings, and store as `FoldDir`.
  - Classify `OrogenyClass` (Old, Andean, Himalayan) using convergence magnitude (`Boundary.RelativeVelocity`), crust thickness contrast, and boundary distance thresholds; keep tuning configurable for QLT metrics.
  - Provide float SoA accessors so ALG-Agent can consume tangents/fold data for anisotropic kernels.
- Validation outline:
  - Rebuild editor (`"/mnt/c/.../UnrealBuildTool.exe" PlanetaryCreationEditor ... -WaitMutex -FromMsBuild`) after the latch lands.
  - Run Milestone 6 GPU parity suites with `r.PlanetaryCreation.StageBProfiling=1`; verify `[StageB][Profile] Ready=1` once readbacks finish and exporter warnings stay quiet.
  - Add automation covering exporter fallback + seam metrics.
- Status | Audit complete; readiness gaps documented.  
  Blockers | Need agreed thresholds for `OrogenyClass` and ALG confirmation on tangent format (float vs double).  
  Next | Prototype `bStageBAmplificationReady` state machine + consumer gating (STG-01/02), then update telemetry/assertions (STG-03/04).

## 2025-10-09 – Readiness interface alignment
- Confirmed `RidgeTangent` / `FoldDir` will publish as `FVector3f` (unit-length, float precision) so ALG/VIS can consume without conversions.
- Readiness API shape:
  - `bool UTectonicSimulationService::IsStageBAmplificationReady() const;`
  - `FStageBAmplificationReadyChanged OnStageBAmplificationReadyChanged; // multicast delegate broadcasting new readiness + reason enum`
  - Reason enum will enumerate invalidation sources (e.g., `Reset`, `SimulationStep`, `GPUReadbackPending`) for diagnostics.
- State machine target:
  - `bStageBAmplificationReady` resets on Stage B invalidation (simulation reset, plate edit, async GPU request) and flips true after CPU amplification + hydraulic finalize or GPU readbacks complete.
  - Delegates fire on any state change; exporters/samplers/controllers will poll until the delegate hookup lands.
- STG-01/02 implementation ETA: targeting 2025-10-10 check-in (pre-noon) to unblock ALG seam telemetry and VIS gating work.
- Status | Interfaces defined; ALG/VIS dependencies unblocked.  
  Blockers | None (will revisit if delegate wiring needs adjustment).  
  Next | Implement latch + reason enum + delegate (STG-01), then add exporter/sampler/controller gating and warnings (STG-02).

## 2025-10-09 – Header corruption resolved
- Detected `Source/PlanetaryCreationEditor/Public/TectonicSimulationService.h` replaced with zeroed blob (71 KB); unable to extend class for STG-01.
- Restored header from `HEAD` (`git show HEAD:Source/... > Source/.../TectonicSimulationService.h`), confirmed declaration content present again.
- Status | Blocker cleared; proceeding with STG-01 implementation.  
  Blockers | None.  
  Next | Implement readiness latch + reason enum + delegate (STG-01).

## 2025-10-09 – Stage B readiness & orientation cache plan
- Re-read `Docs/paper_faithful_shape.md` (sections 0–3) and `Docs/heightmap_debug_review.md` to capture requirements for ridge tangents, fold directions/orogeny classes, and the explicit Stage B readiness latch.
- Inventory of current Stage B data in `UTectonicSimulationService`:
  - Amplification fields: `VertexAmplifiedElevation` (double), `VertexCrustAge`, `VertexRidgeDirections` (double) + `FRidgeDirectionFloatSoA`, dormant `VertexRidgeTangents` (float placeholder).
  - Boundary/adjacency scaffolding: `RenderVertexAdjacency(+Offsets/Weights)`, `RenderVertexBoundaryCache` (divergent tangents + distance), `ConvergentNeighborFlags`, `PlateBoundarySummaries`, `Boundaries` map with `RelativeVelocity`/`BoundaryNormal`.
  - GPU inputs already expose baseline elevation, ridge direction, crust age, render positions, and masks via `FOceanicAmplificationFloatInputs` / `FContinentalAmplificationGPUInputs`.
- Gaps observed:
  - Readiness today is implicit (`VertexAmplifiedElevation.Num()==RenderVertices.Num()` or pipeline phase), so GPU readbacks and baseline reinitialisation can leak half-updated arrays to exporters/preview.
  - Divergent cache holds ridge tangents, but we drop results for non-divergent vertices and never populate `VertexRidgeTangents`; convergent/fold direction data is not cached.
  - Exporters/preview sample amplified heights without verifying Stage B completion.
- Plan (STG-01→STG-04 focus):
  1. **STG-01 – Stage B readiness latch:** introduce `bStageBAmplificationReady` + reason enum, reset on any Stage B invalidation (LOD swaps, parameter toggles, async dispatch). Set true only after CPU amplification and post-hydraulic finalize, or after GPU readbacks commit; expose `IsStageBAmplificationReady()` for consumers and log transitions.
  2. **STG-02 – Consumer gating:** update exporter/sampling/controller paths to require readiness. On false, fall back to baseline elevations and emit a single `[StageB][Ready]` warning per run with guidance to wait or re-trigger amplification; surface readiness in automation hooks.
  3. **STG-03 – Telemetry extensions:** append readiness flag + pending job counts to `[StageB][Profile]`/`[StageB][CacheProfile]`, add trace events for latch trips, and stash last ready state in profile struct for automation comparisons.
  4. **STG-04 – Pipeline audit:** keep existing stage-order guard but extend unit/automation coverage so any erosion/export/gpu preview path asserts when invoked while `!bStageBAmplificationReady`; document the enforced chain in `Docs/paper_faithful_shape.md` addendum.
- Orientation data sketch (feeds STG-05/06 hand-off):
  - Persist `RidgeTangent` as normalized `FVector3f` sized to `RenderVertices`, seeded from `RenderVertexBoundaryCache` divergent tangents, with 1–2 ring Laplacian smoothing via `RenderVertexAdjacencyWeights`; treat missing data inside ridge mask as fatal in development builds.
  - Extend boundary cache (or parallel cache) to accumulate convergent metadata (fold strike, belt distance, opposing plate) and compute `FoldDir` using transported tangents aligned with cross-velocity / boundary normals; apply 3-ring smoothing and normalize.
  - Classify `OrogenyClass` (`Old`, `Andean`, `Himalayan`) using convergence magnitude (`Boundary.RelativeVelocity`), crust thickness contrast, and distance-to-boundary buckets; keep thresholds configurable for QLT metrics.
  - Expose float SoA views for ALG-Agent/GPU kernels, and provide const accessors once populated.
- Validation path after implementation: rebuild editor (`UnrealBuildTool ... -WaitMutex -FromMsBuild`), run Milestone 6 GPU parity suites with `r.PlanetaryCreation.StageBProfiling=1`, verify `[StageB][Profile] Ready=1` once readbacks land, and add targeted automation covering exporter fallback + seam metrics.
- Open questions/blockers: need tuning targets for `OrogenyClass` thresholds and confirmation from ALG-Agent on required tangent format (float vs double) for the shared anisotropic kernels; will sync before finalising constants.

## 2025-10-09 – Interface clarification for ALG/VIS
- **Vector data types:** committing to `TArray<FVector3f>` for both `RidgeTangent` and `FoldDir`. Source computations stay in double internally, but we normalize and store as float to match existing GPU caches (`FRidgeDirectionFloatSoA`) and keep memory footprint sane at high LOD. For CPU consumers that need full precision we’ll expose helper accessors returning `FVector3d` via `FVector3f::ToFVector3d()`. GPU paths get the float data directly; no extra downcast required.
- **Readiness API surface:**
  - `bool UTectonicSimulationService::IsStageBAmplificationReady() const;`
  - `EStageBAmplificationReadyReason UTectonicSimulationService::GetStageBAmplificationNotReadyReason() const;`
  - `void UTectonicSimulationService::ForceStageBAmplificationRebuild(const TCHAR* Context);` (explicit reset hook for controller/automation when they invalidate Stage B on demand).
  - New multicast: `FOnStageBAmplificationReadyChanged OnStageBAmplificationReadyChanged;` (dynamic delegate the UI can bind to; fires with `(bool bReady, EStageBAmplificationReadyReason Reason)`).
  - `EStageBAmplificationReadyReason` enum seeds: `None`, `NoRenderMesh`, `PendingCPUAmplification`, `PendingGPUReadback`, `ParametersDirty`, `LODChange`, `ExternalReset`. Resets land in the existing invalidation paths (`RebuildStageBForCurrentLOD`, parameter change handlers, GPU job submission) and we’ll emit `[StageB][Ready]` logs whenever the reason flips.
- **ETA:** STG-01 (latch + enum + delegate + logging) by end-of-day 2025-10-09 PST; STG-02 (consumer gating for exporter, sampling, controller) morning 2025-10-10 PST, pending quick review of delegate wiring with VIS.

## 2025-10-09 – Readiness reason strings for VIS
- Planned `EStageBAmplificationReadyReason` values + warning copy:
  - `None` — “Stage B amplification ready.”
  - `NoRenderMesh` — “Waiting for render mesh to initialize.”
  - `PendingCPUAmplification` — “Stage B CPU amplification still running.”
  - `PendingGPUReadback` — “Stage B GPU readback pending; amplified data not yet available.”
  - `ParametersDirty` — “Amplification parameters changed; awaiting rebuild.”
  - `LODChange` — “Render LOD changed; Stage B rebuild in progress.”
  - `ExternalReset` — “Stage B reset requested; rerun amplification to refresh detail.”
- Enum will live in a shared header (`Source/PlanetaryCreationEditor/Public/StageBAmplificationTypes.h`) so ALG/VIS can include it directly without mirroring the values.
- Anticipated future reasons (placeholder copy TBD): `AutomationHold` (tests forcing Stage B off) and `GPUFailure` (unexpected parity failure / readback error). VIS can plan UI space for additional entries; we’ll append without renaming existing values.

## 2025-10-09 – Build failure before STG-02
- Ran WSL build (`...UnrealBuildTool.exe PlanetaryCreationEditor Win64 Development -WaitMutex -FromMsBuild`); compile failed with header conflicts:
  - Inline accessor `bool IsStageBAmplificationReady() const { ... }` still present alongside the new UFUNCTION declaration, causing duplicate symbol errors.
  - `HeightmapPaletteMode` storage/include removed while VIS’ palette changes expect it; service missing the member leads to unresolved externals.
  - `HydraulicErosion.cpp` references new readiness helpers not declared prior to include order adjustments.
- Documented STG-02 gating plan (exporter/sampler/controller fallbacks) but blocked until build compiles.
- Status | Build failed; latch integration needs cleanup.  
  Blockers | Remove duplicate accessor, restore palette member/includes, ensure helpers visible to erosion code.  
  Next | Fix compilation issues, rerun build, then execute STG-02 gating work.

## 2025-10-09 – STG-01 implementation blocked
- Began wiring `StageBAmplificationTypes.h` and latch plumbing, but discovered `Source/PlanetaryCreationEditor/Public/TectonicSimulationService.h` on disk is an all-zero 71 KB blob (no class declaration). Without the real header we can’t add members/delegates or expose the new APIs; duplicating the entire subsystem definition risks diverging from the authoritative copy.
- Pending clarification on whether the zeroed header is intentional (e.g., sanitized placeholder) or if there’s another source-of-truth we should edit. Once we have the correct header contents, I can proceed with the latch + logging work immediately.
- No code changes committed yet; pacing until we rehydrate the header.

## 2025-10-09 – STG-01 readiness latch implemented
- Added shared enum/delegate in `Source/PlanetaryCreationEditor/Public/StageBAmplificationTypes.h` and threaded through `UTectonicSimulationService` via new methods: `IsStageBAmplificationReady()`, `GetStageBAmplificationNotReadyReason()`, `OnStageBAmplificationReadyChanged()`, `ForceStageBAmplificationRebuild()`, plus internal helpers `SetStageBAmplificationReady`, `TryMarkStageBReady`, `HasPendingStageBGPUJobs`.
- Instrumented Stage B invalidation/transition points to update the latch:
  - `ResetSimulation`, `SetParameters`, `SetSkipCPUAmplification`, `SetRenderSubdivisionLevel`, `RebuildStageBForCurrentLOD`, stage pipeline begin/end markers, GPU job enqueue, and both GPU readback processors now call the helper with context-specific reasons (ParametersDirty, LODChange, PendingCPUAmplification, PendingGPUReadback, etc.).
  - `[StageB][Ready]` logs fire on every state change and include the short reason + description; delegate broadcast mirrors the state so VIS/UI can react without polling.
- `TryMarkStageBReady` defers readiness until Stage B reaches StageBComplete/PostErosion and all GPU jobs have resolved, preventing premature “ready” flips during Stage A or when readbacks are still running.
- Did not run the full editor rebuild tonight (same large command as earlier) to save cycle time; will kick it off with STG-02 gating tomorrow. Local compile passes expected since changes stay inside the editor module, but we should validate with `UnrealBuildTool ... -WaitMutex -FromMsBuild` before merging.
- TODO follow-ups: surface readiness in `[StageB][Profile]` metrics (STG-03), wire exporter/sampler/controller gating (STG-02), and add automation harness once QLT tasks begin.

## 2025-10-09 – Build validation + STG-02 plan
- Build: `"/mnt/c/Program Files/Epic Games/UE_5.5/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" PlanetaryCreationEditor Win64 Development -project="C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject" -WaitMutex -FromMsBuild"` (**success**, 9.6 s after extending timeout).
  - Fixes prior to rerun: removed inline accessor duplicates in `TectonicSimulationService.h`, reintroduced service-level `HeightmapPaletteMode` cache + header include in `SPTectonicToolPanel.h`, declared stage pipeline helpers/enum and ridge tangent buffer, and pruned the legacy `IsStageBAmplificationReady` definition in the cpp.
  - All stage-day modules now compile; UBT output archived under `%LOCALAPPDATA%\UnrealBuildTool\Log.txt` + trace `Log.uba`.
- STG-02 gating outline (ready for implementation):
  - **Exporter** (`HeightmapExporter.cpp`): guard amplified sampling on `IsStageBAmplificationReady()`, emit a single `[StageB][Ready]` warning with reason/description when falling back to baseline, and surface status to CLI script.
  - **Sampler** (`HeightmapSampling.cpp`): prefer baseline elevations when latch false, with optional verbose log so ALG/QLT automation can assert fallback behaviour.
  - **Controller/UI** (`TectonicSimulationController.cpp`, `SPTectonicToolPanel`): subscribe to `OnStageBAmplificationReadyChanged` to toggle preview buttons/tooltips, disable exporter actions when latch is false, and reuse the reason strings for UI copy.
  - **VIS integration**: reuse shared enum helper for reason labels, update palette indicator binding to the restored service cache, and ensure status bar reflects readiness state.
  - **ALG hand-off**: confirm the sampler/exporter gating still expose baseline data so their CPU sampling rewrite remains deterministic; document the fallback log string for automation assertions.

## 2025-10-10 – Plan realignment briefing
- Mandate Day 0 Stage B heartbeat diagnostic: add `[StageBDiag]` logging in `UTectonicSimulationService::AdvanceSteps`, verify latch flips `Ready=1`, amplified counts match render vertices, and random samples are finite before resuming feature work.
- Day 1 now includes `STG-00` isotropic amplification spike (simple FBM × age/ridge falloff) plus `STG-04` erosion disable. Export a PNG alongside ALG’s sampler integration to prove the pipeline.
- Inline validation required: `STG-05` logs ridge tangent population (<1 % missing), `STG-06` logs fold/orogeny counts, and `STG-07` captures a reference PNG before handing off to QLT.
- Hydraulic erosion stays disabled until `STG-08`, where conservative parameters must be documented alongside telemetry.

## 2025-10-12 – Stage B rescue telemetry + fallback elimination
- Instrumented exporter with per-run aggregation and a shared `FStageBRescueSummary` surface. `HeightmapExporter.cpp` now tallies fallback attempts/success/fail + mode buckets, row reuse hits, and emits a single `[StageB][RescueSummary]` that Stage B logs forward (`TectonicSimulationService.cpp`).
- Added row-level triangle reuse: we cache each row’s last successful triangle and re-sample via `FHeightmapSampler::SampleElevationAtUVWithClampedHint` when expanded fallback still misses. New fallback mode `RowReuse` converts the remaining gap without perturbing seam hints.
- Coverage results (real RHI, AllowCommandletRendering):
  - 1024×512 ⇒ `[HeightmapExport][Coverage]` 524 288/524 288 (100 %), `[StageB][RescueSummary]` Fail=0, RowReuse=184 (`Saved/Logs/PlanetaryCreation-backup-2025.10.12-20.32.15.log`).
  - 4096×2048 ⇒ `[HeightmapExport][Coverage]` 8 388 608/8 388 608 (100 %), `[StageB][RescueSummary]` Fail=0, RowReuse=13 068 (`Saved/Logs/PlanetaryCreation-backup-2025.10.12-20.40.33.log`).
- Heartbeat rerun with `Scripts/RunStageBHeartbeat.ps1 -ThrottleMs 50` shows `[StageBDiag]` Ready=1 and `[StageB][Profile]` Ridge fallback counts stable at 0 grad / 0 plate / 7 motion (`Saved/Logs/StageBHeartbeat-20251012-154128.log`).
- Build + validation commands: `UnrealBuildTool ...PlanetaryCreationEditor... -WaitMutex -FromMsBuild` (success), `ExportHeightmap1024.py` + `ExportHeightmap4096Force.py` under `UnrealEditor-Cmd.exe -AllowCommandletRendering`, heartbeat script above.
