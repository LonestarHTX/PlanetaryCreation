# Phase 1 Migration Notes — Constants, Units, and Guardrails

Purpose
- Lock unit conventions and naming to prevent regression during Phases 2–6.
- Provide search-and-replace checklists to catch drift early.

Naming and Symbols
- Do not use identifier `zc` in code; reference explicitly:
  - `SeaLevel_m` = 0 m
  - `MaxContinentalAltitude_m` = 10,000 m (zc)
- Subduction normalization uses the vertical span `(zc − zt)`.
- Erosion normalization uses `ErosionNormalizationHeight_m = 10,000 m`.

Units and Conversions
- Speeds: `km/My` (v0 = 100 km/My); derive angular via `ω = v / R` (rad/My).
- Distances: km; Elevations: m; Time: My; Angular: rad.
- Unreal conversions only at render boundaries:
  - meters → uu: `m * 100`
  - kilometers → uu: `km * 100000`
- Clamps: `α ∈ [0,1]`; cadence ratio `∈ [0,1]`; dampening factor `∈ [0,1]`.

PaperMode Constraints
- Fibonacci + STRIPACK triangulation only (no planar approximations).
- True Gabor noise only (no Perlin fallback).

Search-and-Replace Checklists
- Regex guards to catch misuse and drift:
  - `\bzc\b` — disallow symbolic use; prefer explicit names above.
  - `(?<!PaperConstants\s*::)\b-?10000\b` — catch hard-coded zt/zc elevation magnitudes outside constants.
  - `\bmm\/yr\b` — flag unit usage in simulation paths; prefer `km/My` or `m/My` convenience constants.
  - `\b0\.1\s*km.*My\b` — flag suspicious "0.1 km/My" (should be 100.0, 1000× conversion error).
- Review all new simulation code paths for conversions against `PaperConstants.h` helpers.

