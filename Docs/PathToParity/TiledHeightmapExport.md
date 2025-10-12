# Tiled Heightmap Export Roadmap

## Goal
Deliver a robust, crash-safe heightmap exporter that supports 2048×1024 and 4096×2048 renders so we can finish Milestone 6 paper parity (visual validation, quantitative metrics, terrane checks, expert review).

## Guiding Principles
- Fix the root cause (parallel sampling spike) rather than patching symptoms.
- Keep Stage B warm-up unchanged; guard the exporter, not the pipeline.
- Always capture/export from a single Stage B snapshot to avoid mid-run drift.
- Build telemetry into every stage (tile memory, progress, seam metrics) so we can monitor and automate later.
- Deliver the chunked/tiled exporter first; defer parallel tile execution until the sequential version is stable.

## Phase 1 – Tiled Export Implementation (ALG, 4–5 working days)

### Day 1: Design & Scaffolding
- Lock tile dimensions (512×512 recommended) with 2-pixel overlap; document constants.
- Capture a Stage B snapshot (amplified heights if ready, baseline otherwise) before tiling starts; seed `FHeightmapSampler` from this immutable snapshot.
- Add Stage B mutation guard using `OceanicAmplificationDataSerial`; log and abort if serial changes mid-export.
- Stub `ProcessTile(StartX, StartY, EndX, EndY)` that samples tile pixels into a local RGBA buffer and logs per-tile telemetry.
- Add stitching helper that copies tile buffers into the final image (handle partial edge tiles).
- Introduce per-tile memory telemetry hooks (`[HeightmapExport][TileMemory]`).

### Day 2–3: Core Implementation
- Refactor the current `ParallelFor` sampling loop into sequential tile processing.
- Implement overlap handling: expand tiles by 2 pixels and discard overlap on stitch (upgrade to guard bands if seam deltas exceed 0.5 m).
- Aggregate seam metrics (`RowSeams`, success counts, traversal stats) across tiles so logs stay identical to baseline.
- Add tile progress logging (`[HeightmapExport][TileProgress] Tile 3/8`).
- Preserve NullRHI protections—oversized exports require `SetAllowUnsafeHeightmapExport(true)` just like the CLI path.
- Review sampler reuse carefully (no touching live Stage B arrays after snapshot capture).

### Day 4: Testing & Validation
- Run exports at 512×256 (single tile), 1024×512 (2×1 tiles), 2048×1024 (4×2), 4096×2048 (8×4) on real RHI hardware.
- Confirm seam delta metrics (<0.5 m) and success counts match the current exporter at equivalent resolutions.
- Verify per-tile memory logs stay <100 MB; total peak <500 MB.
- Visual diff 512×256 tiled vs baseline to ensure pixel-perfect output.

### Day 5: Hardening & Docs
- Re-enable automation suite (`PlanetaryCreation.Milestone6.HeightmapVisualization`) at 512×256 and 2048×1024.
- Add optional `--tile-size=N` CLI override for debugging.
- Document architecture/guardrails in `CLAUDE.md` / `AGENTS.md`; remove legacy crash warning once validated.
- Archive exemplar outputs (2048×1024, 4096×2048) under `Docs/Validation/` for paper comparisons.

## Phase 2 – Quantitative Metrics (QLT, ~1 week — starts after Phase 1)
- Implement `ExportQuantitativeMetrics()` to emit CSVs for:
  - Hypsometric curve (paper Fig. 6 reference)
  - Plate velocity histogram (Fig. 4)
  - Ridge/trench length ratios (Table 1)
  - Terrane area preservation (<5% drift)
- Establish baselines from paper data and add automation checks with tolerances.

## Phase 3 – Terrane Validation (STG, ~3 days)
- Create automation (`FTerraneAreaPreservationTest`) that extracts, transports, and reattaches terranes while verifying:
  - Area drift <5%
  - Elevation signature retention (<100 m delta)
  - Topological consistency (no gaps/overlaps, Euler characteristic stable)
- Leverage the metrics pipeline for logging and regression thresholds.

## Phase 4 – Visual Comparison & Review (VIS/OPS, ~1 week)
- Use tiled exporter to produce 4096×2048 renders.
- Build comparison grids: [Our output | Paper reference | SRTM Earth] with qualitative checklist (ridges, trenches, mountain anisotropy, terrane signatures).
- Package renders + metrics for expert review (paper Section 6 parity).
- Update docs/checklists; publish validation brief.

## Dependencies & Coordination
- ALG completes Phase 1 before QLT/STG/VIS continue.
- QLT metrics feed STG automation and VIS comparison work.
- Automation remains capped at 512×256 until tiled exporter lands; NullRHI oversized exports stay blocked.
- Update `PlanningAgentPlan.md` milestones and agent logs as each phase progresses.

## Risks & Mitigations
- **Tile seam artifacts** → Use overlap; if seam delta >0.5 m, adopt guard-band sampling.
- **Stage B mutation mid-export** → Snapshot heights + serial guard; fail fast with `[HeightmapExport][Corruption]` log.
- **Performance regressions** → Track `[HeightmapExport][TileStats]`, target <400 ms total for 2048×1024; consider tile-level `ParallelFor` only after sequential version is rock solid.
- **Automation regression** → Keep Milestone 6 suite at 512×256 until 2048×1024 proves stable under CI conditions.

## Source of Truth
- This roadmap lives at `Docs/PathToParity/TiledHeightmapExport.md`.
- Reference `Docs/agent_logs/MNG-Guide.md` and `Docs/agent_logs/STG-Anchor.md` for crash history and recovery context.
- Coordinate updates through `PlanningAgentPlan.md` and #planetary-heightmap status posts.
