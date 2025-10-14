<!-- 527b1f23-ea31-4b26-b7e2-c7d17d69b8a9 0b9b6e34-9ac4-4375-bee8-90ea020f3247 -->
# A01 Continental Exemplar Diagnostic Plan

## Problem Summary

A01 exemplar export produces flat terrain at baseline elevation (1927.36m) across 90%+ of pixels, while O01 oceanic exemplar shows terrain variation. Logs reveal all A01 samples map to identical V=0.216716 (WrappedLat=-9.0516), suggesting latitude wrapping collapse.

**Key Evidence:**

- A01 metrics: `mean_diff_m=-408.57`, `interior_max_abs_diff_m=3944.85m` (fails guardrails)
- A01 bounds: south=-9.59986°, north=-8.89986° (0.7° range), west=-77.90°, east=-77.10° (0.8° range)
- All logged samples: `WrappedLat=-9.0516 U=varies V=0.216716 Result=1927.361`
- O01 (northern hemisphere, 0.7° range): shows terrain variation, passes mean guardrail

## Phase 1: PNG16 Data Validation

### 1.1 Create Python Diagnostic Script

**File:** `Scripts/validate_exemplar_png16.py`

```python
# Load A01.png, verify dimensions, elevation range, and terrain variation
# Output: histogram of elevation values, min/max/mean/stddev
# Visualize: sample cross-sections at multiple V-coordinates
```

**Validates:**

- PNG16 decodes correctly (512×512, 16-bit grayscale)
- Elevation range matches metadata (1927.36m–5875.86m)
- Terrain variation exists (stddev > 100m, not flat)

### 1.2 Run Validation

```bash
python Scripts/validate_exemplar_png16.py Content/PlanetaryCreation/Exemplars/PNG16/A01.png --metadata-id A01 --library Content/PlanetaryCreation/Exemplars/ExemplarLibrary.json
```

**Exit Criteria:** Confirm A01.png contains valid terrain data before proceeding.

## Phase 2: C++ Coordinate Wrapping Trace

### 2.1 Add Enhanced Logging to HeightmapSampling.cpp

**Location:** `Source/PlanetaryCreationEditor/Private/HeightmapSampling.cpp:298-375`

Add logging at key transform stages in `FHeightmapSampler::SampleElevationAtUV`:

- UV → SampleLonDeg, SampleLatDeg (line 298-299)
- WrapLatitudeToBounds output (line 369)
- ForcedSampleV calculation (line 375)
- Final sampled elevation (line 486)

**Log every 64th row** to capture V-coordinate distribution across full 256-row export.

```cpp
if (bTraceSampler || (GlobalY % 64 == 0 && LocalX == 0))
{
    UE_LOG(LogPlanetaryCreation, Display,
        TEXT("[A01Trace] UV=(%.6f,%.6f) SampleLat=%.4f WrappedLat=%.4f ForcedV=%.6f Range=(%.4f,%.4f) Result=%.3f"),
        UV.X, UV.Y, SampleLatDeg, WrappedLatDeg, ForcedSampleV,
        ForcedSouthDeg, ForcedNorthDeg, ForcedHeight);
}
```

### 2.2 Rebuild and Capture Export

```bash
# Build
/mnt/c/Program\ Files/Epic\ Games/UE_5.5/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe \
  PlanetaryCreationEditor Win64 Development \
  -project="C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject" \
  -WaitMutex -FromMsBuild

# Export with trace
$env:PLANETARY_STAGEB_TRACE_SAMPLER="1"
.\Scripts\RunExportHeightmap512.ps1 -ForceExemplar A01
```

**Capture:** `Saved/Logs/PlanetaryCreation.log` with `[A01Trace]` entries showing V-coordinate progression.

## Phase 3: Wrapping Behavior Analysis

### 3.1 Extract and Analyze Trace Data

**Script:** `Scripts/analyze_a01_wrapping.py`

Parse log for `[A01Trace]` entries, compute:

- V-coordinate histogram (should span 0.0–1.0, not collapse to single value)
- WrappedLat distribution (should map to south=-9.59 to north=-8.89)
- Correlation between input UV.Y and output ForcedV

### 3.2 Root Cause Identification

Compare A01 (southern hemisphere, 0.7° range) vs O01 (northern hemisphere, 0.7° range) wrapping:

**Hypothesis A:** `WrapLatitudeToBounds` (line 341-366) uses `FMath::Fmod` which may behave unexpectedly for negative southern latitudes when wrapped from +90° to -90° range.

**Hypothesis B:** Modulo-wrapping a 0.7° exemplar across 180° latitude (257× repetition) causes numerical precision collapse, particularly for southern hemisphere where `North - SampleLat` creates large denominators.

**Hypothesis C:** The `FMath::Clamp(Wrapped, Minimum, Maximum)` (line 365) is clamping all wrapped latitudes to the same edge value.

### 3.3 Validate Against O01

Run O01 export with same trace logging to compare northern vs southern hemisphere behavior.

## Phase 4: Documentation and Guardrails

### 4.1 If Fundamental Limitation Confirmed

Create `Docs/exemplar_forced_mode_limitations.md`:

```markdown
# Forced Exemplar Mode Limitations

## Issue: Global Tiling of Narrow Exemplars
Forced exemplar mode uses modulo wrapping to tile small exemplars (0.7°–1.0° range) 
across full planetary surface (360° × 180°). This causes:
- 250–500× repetition factor
- Numerical precision issues in V-coordinate calculation
- Southern hemisphere coordinates may collapse to single latitude band

## Validated Use Cases
- ✅ Export within exemplar's padded bounds (1.5°–5° padding)
- ✅ Validation exports at LOD ≤ 5 (coarse resolution)
- ❌ Full-sphere exports (512×256 or larger) - unsupported

## Guardrails Update
- Warn when exemplar bounds cover < 2.0° in either dimension
- Block exports > 256×128 in forced exemplar mode
- Recommend Stage B preview GPU validation instead
```

### 4.2 Update Guardrails in Code

**File:** `Source/PlanetaryCreationEditor/Private/HeightmapSampling.cpp:50-90`

Add validation in `FHeightmapSampler` constructor when forced exemplar detected:

```cpp
if (bUseForcedExemplarOverride && ForcedExemplarMetadata)
{
    if (ForcedLonRange < 2.0 || ForcedLatRange < 2.0)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("[HeightmapSampler] Forced exemplar %s has narrow bounds (%.2f° × %.2f°). Full-sphere exports may produce artifacts."),
            *ForcedExemplarId, ForcedLonRange, ForcedLatRange);
    }
}
```

### 4.3 Update Exemplar Audit Documentation

**File:** `Docs/Validation/exemplar_audit_summary.md`

Add section documenting A01 findings, expected vs actual behavior, and recommended workflow for continental exemplar validation (use GPU Stage B preview within padded bounds, not global CPU exports).

## Deliverables

1. `Scripts/validate_exemplar_png16.py` - PNG16 data validator
2. Enhanced logging in `HeightmapSampling.cpp` with A01 trace path
3. `Scripts/analyze_a01_wrapping.py` - V-coordinate distribution analyzer
4. `Docs/exemplar_forced_mode_limitations.md` - Limitation documentation
5. Guardrail updates in `HeightmapSampling.cpp`
6. `Docs/Validation/A01_diagnostic_report.md` - Full findings with log excerpts and recommendations

## Success Criteria

- ✅ Confirm A01.png contains valid terrain variation (not corrupt)
- ✅ Identify specific wrapping behavior causing V-coordinate collapse
- ✅ Document limitation or propose targeted fix
- ✅ Update guardrails to prevent future confusion
- ✅ Provide clear guidance on validated use cases for forced exemplar mode

### To-dos

- [ ] Create Scripts/validate_exemplar_png16.py to verify A01.png data integrity (dimensions, elevation range, terrain variation)
- [ ] Execute PNG16 validator on A01.png and confirm terrain data is valid before proceeding
- [ ] Add enhanced A01 trace logging to HeightmapSampling.cpp capturing UV→Lat→WrappedLat→V transformation at multiple rows
- [ ] Rebuild C++ changes and run A01 export with PLANETARY_STAGEB_TRACE_SAMPLER=1 to capture wrapping logs
- [ ] Create Scripts/analyze_a01_wrapping.py to parse logs and compute V-coordinate histogram and correlation analysis
- [ ] Execute wrapping analyzer and identify root cause (Fmod behavior, precision collapse, or clamp issue)
- [ ] Create Docs/exemplar_forced_mode_limitations.md documenting findings and validated use cases
- [ ] Add narrow-bounds warning to HeightmapSampling.cpp constructor when forced exemplar detected
- [ ] Write Docs/Validation/A01_diagnostic_report.md with full findings, log excerpts, and recommendations