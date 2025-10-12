# VIS-Guide Log

## 2025-10-09 – paper_faithful_shape kickoff
- Re-read `Docs/paper_faithful_shape.md` and `Docs/heightmap_debug_review.md` to restate palette expectations (absolute hypsometric default, normalized diagnostic option) and confirm exporter pain points (seam continuity, Stage B gating).
- Audited current editor implementation: `HeightmapColorPalette.h` already defines both absolute and normalized gradients but exposes them as free functions; `UTectonicSimulationService::ExportHeightmapVisualization` selects the mode via `r.PlanetaryCreation.HeightmapExportUseNormalized`; CLI hook (`Scripts/ExportHeightmap4096.py`) calls the exporter with no palette or readiness parameters.
- Noted that exporters still infer readiness from `VertexAmplifiedElevation.Num()==RenderVertices.Num()`; once STG lands `bStageBAmplificationReady` we must treat it as the source of truth before enabling amplified palette previews or toggles.
- Dependency audit:
  - VIS-01 relies on ALG keeping `FHeightmapSampler` as the sampling entry point; any Stage B data consumers must read tangents/fold fields as soon as STG exposes them.
  - VIS-02 needs STG’s `IsStageBAmplificationReady()` accessor to gate palette/exporter controls and warn when Stage B data is stale; CLI script should respect the latch and surface fallback messaging.
  - VIS-03 documentation updates depend on final palette API signatures and Stage B readiness wording once STG confirms terminology.
- VIS implementation plan:
  - Split palette API into a lightweight `EHeightmapPaletteMode` + struct wrapper so callers request `AbsoluteHypsometric` vs `NormalizedRange` explicitly; keep gradient tables centralized in `HeightmapColorPalette.h` but expose a single `FHeightmapPalette::Sample(Elevation, MinMax)` entry point.
  - Extend `UTectonicSimulationService` with a cached palette mode (default absolute) and surface it through editor UI (tool panel toggle) and CLI (`ExportHeightmap4096.py --normalized/--absolute`); route both paths through the shared accessor.
  - Integrate Stage B readiness gating: exports warn + fall back to baseline when `!IsStageBAmplificationReady()` unless the user overrides; UI toggle remains disabled until readiness flips true.
  - Stage VIS-03 doc updates (README heightmap section, `Docs/paper_faithful_shape.md` addendum, CLI usage snippet) once API/behavior finalize.
- Questions queued for ALG/STG:
  - Confirm that `RidgeTangent`/`FoldDir` will publish as `FVector3f` (float) so palette tooling can mirror GPU previews without conversions.
  - Provide expected signal when `bStageBAmplificationReady` flips (delegate vs polling) to wire into UI state.
- Status | Scope review complete; palette/exporter surfaces documented.  
  Blockers | Await STG `bStageBAmplificationReady` interface + tangent precision confirmation; need ALG timeline for seam delta telemetry so VIS automation references stay in sync.  
  Next | Draft palette wrapper header, sketch UI/CLI toggle flow, and sync with STG/ALG on readiness notifications before starting VIS-01 refactor.

## 2025-10-09 – Readiness interface notes
- Consuming the latch: subscribe editor UI to `UTectonicSimulationService::OnStageBAmplificationReadyChanged`; while STG-01/02 are in flight, poll `IsStageBAmplificationReady()` on panel refresh/tick so the palette toggle reflects current state. CLI workflow will poll immediately before invoking the exporter and emit warnings based on the reported reason enum once the delegate lands.
- Palette plan update: treat `RidgeTangent`/`FoldDir` as `FVector3f` throughout VIS tooling so normalized palette overlays and future anisotropy previews can ingest Stage B fields without precision conversions.
- Wiring to stage ahead of latch: begin implementing the UI toggle in `SPTectonicToolPanel` with disabled state when readiness is false (temporary polling), and add CLI flag parsing that checks readiness before export; swap to delegate-driven updates after STG delivers the broadcast.
- Remaining questions/blockers: need the finalized set of readiness reason enum labels (e.g., `Reset`, `SimulationStep`, `GPUReadbackPending`) so UI/CLI warnings can map to user-facing strings; still tracking ALG’s seam telemetry timeline for VIS automation references.

## 2025-10-09 – Palette toggle wiring (temporary polling)
- Implemented `EHeightmapPaletteMode` UENUM + service accessors so palette selection runs through `UTectonicSimulationService`; exporter now respects the service mode and warns when normalized sampling falls back due to zero range.
- Added normalized palette toggle to `SPTectonicToolPanel` guarded by `IsStageBAmplificationReady()` polling; UI shows readiness messaging and temp-stub `BindStageBReadyDelegate()` blocks for STG-02 delegate hookup.
- Extended CLI script (`Scripts/ExportHeightmap4096.py`) with argparse flags (`--normalized/--absolute`, width/height overrides), readiness warning, and palette mode logging prior to export.
- Status | UI + CLI toggles live with polling fallback; service owns palette state; delegate stub prepared.  
  Blockers | Await readiness reason enum strings for richer UI messaging; still need ALG seam telemetry timeline.  
  Next | Circle with STG once `OnStageBAmplificationReadyChanged` lands to wire the delegate and remove per-tick polling.

## 2025-10-10 – Delegate hookup & CLI verification
- Wired `SPTectonicToolPanel` into `UTectonicSimulationService::OnStageBAmplificationReadyChanged`, removed tick-based polling, and surface Stage B reason strings in the UI (“Stage B pending: …”) using `PlanetaryCreation::StageB::GetReadyReasonDescription`.
- Added delegate lifetime management (bind on construct, remove on shutdown) and refreshed palette readiness messaging after the GPU prime helper runs so the toggle updates immediately.
- CLI export script continues to poll right before export; sanity-checked `Scripts/ExportHeightmap4096.py --normalized` / `--absolute` parsing after the delegate switch (logic unchanged, still logs readiness warning when latch is false).
- Status | Delegate-driven UI gating live; CLI polling intact; readiness copy shows reason text.  
  Blockers | None; awaiting ALG seam telemetry timeline remains.  
  Next | Prep VIS-01 palette API refactor once ALG-03 seam metrics land.

## 2025-10-09 – Stage B readiness copy plan
- Adopt `EStageBAmplificationReadyReason` labels for UI/CLI messaging with the following copy:
  - `None` → “Stage B amplification ready.”
  - `NoRenderMesh` → “Waiting for render mesh to initialize.”
  - `PendingCPUAmplification` → “Stage B CPU amplification still running.”
  - `PendingGPUReadback` → “Stage B GPU readback pending; amplified data not yet available.”
  - `ParametersDirty` → “Amplification parameters changed; awaiting rebuild.”
  - `LODChange` → “Render LOD changed; Stage B rebuild in progress.”
  - `ExternalReset` → “Stage B reset requested; rerun amplification to refresh detail.”
  - (Future) `AutomationHold` → “Automation run holding Stage B amplification.”
  - (Future) `GPUFailure` → “GPU amplification failed; rerun Stage B or fall back to CPU.”
- Implementation plan:
  - Editor UI will surface the reason string via localized `LOCTEXT` entries keyed by enum value, keeping copy centralized in `StageBAmplificationTypes.h` to simplify future localization. CLI path will reuse the same string table through `NSLOCTEXT`.
  - When reason ≠ `None`, palette toggles remain disabled and exporter warns before falling back to baseline sampling; both paths will suggest the remedy implied by the copy (e.g., rerun amplification).
- Localization stance: hard-coded English strings are acceptable for the current milestone, but structuring them as `LOCTEXT` tokens now keeps the door open for a string table later without refactoring.
- Delegate readiness: once STG-02 lands (ETA 2025-10-10 morning), VIS is prepared to swap from tick-based polling to the broadcast delegate for stage readiness updates; CLI will perform a final poll immediately before export to handle headless scenarios.

## 2025-10-10 – Plan realignment briefing
- Heightmap palette work remains merged, but new guidance moves additional VIS scope (automation, docs polish) to Phase 4; do not block the shape pipeline.
- Await confirmation from MNG that the Stage B heartbeat diagnostic + isotropic spike succeeded before taking new VIS actions.
- Prep doc copy (`README`, `Docs/paper_faithful_shape.md` addendum) to describe the final palette UX once anisotropic kernels are validated; draft now, publish after Day 5 deliverables are green.
