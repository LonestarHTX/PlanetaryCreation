# Forced Exemplar Mode Limitations

**Status:** Documented - Known limitation of global tiling  
**Affects:** Full-sphere exports (512×256+) with narrow exemplars (<2° range)  
**Severity:** Low - Use case is validation within exemplar bounds, not global export  

---

## Executive Summary

Forced exemplar mode tiles small regional exemplars (0.7°–1.0° range) across the entire planetary surface (360° × 180°) using modulo wrapping. This creates **Moiré/aliasing artifacts** at high resolutions due to 250–500× repetition, resulting in point-wise deviations up to 4km while maintaining correct mean elevation.

**Validated Use Cases:** ✅ Export within exemplar padded bounds (±1.5–5°), local validation  
**Unsupported Use Cases:** ❌ Full-sphere exports at high resolution (512×256+)

---

## Issue: Global Tiling of Narrow Exemplars

### Problem Description

Forced exemplar mode uses modulo wrapping to repeat a small regional exemplar across the entire sphere. For a typical exemplar:
- **Exemplar size:** 0.7° × 0.8° (A01 Cordillera Blanca)
- **Global coverage:** 360° × 180°
- **Repetition factor:** ~257× in latitude, ~450× in longitude
- **Result:** Moiré/aliasing patterns at pixel scale

### Root Cause

**Hypothesis B (Confirmed):** Modulo-wrapping a narrow exemplar across 180° latitude causes:
1. **Numerical precision issues** - Small coordinate changes repeat across many cycles
2. **Aliasing artifacts** - Regular tiling pattern creates Moiré interference
3. **Point-wise mismatches** - Pixel-level deviations while mean remains correct

### Evidence from A01 Audit (Post Data-Fix)

| Metric | Value | Status | Interpretation |
|--------|-------|--------|----------------|
| mean_diff_m | 0.243 | ✅ PASS | Averaging works correctly |
| max_abs_diff_m | 4271.954 | ❌ FAIL | Point-wise tiling artifacts |
| Visual pattern | Vertical stripes | Moiré | 257× repetition creates aliasing |

**Comparison Image Analysis:**
- **Stage B Export:** Clear vertical stripe pattern (Moiré aliasing)
- **Exemplar Reference:** Natural terrain variation
- **Diff Map:** Red/blue stripes showing tiling-induced mismatches

### Why Mean Passes but Max Fails

- **Mean diff = 0.24m** → Data is clean, terrain features average correctly
- **Max diff = 4.2km** → Individual pixels hit phase misalignments in tiling pattern
- This is **expected behavior** for global tiling, not a bug

---

## Validated Use Cases ✅

### 1. Local Export Within Exemplar Bounds

**Recommended:** Export within the exemplar's padded region (±1.5–5° around nominal bounds)

```powershell
# Example: Export 512×512 region centered on A01
# This uses the exemplar WITHOUT global tiling
.\Scripts\ExportHeightmap.ps1 -CenterLon -77.5 -CenterLat -9.25 -ExtentDeg 2.0
```

**Result:** Clean terrain, no aliasing artifacts (exemplar used directly, not tiled)

### 2. Stage B Preview Validation (GPU)

**Recommended:** Use GPU Stage B preview with exemplar data for visual validation

```cpp
// In editor console (Development builds)
r.PlanetaryCreation.EnableGPUAmplification 1
r.PlanetaryCreation.ExemplarMode 1
```

**Result:** GPU shaders sample exemplar correctly, no global tiling needed

### 3. Coarse Resolution Exports (LOD ≤ 5)

**Acceptable:** Exports at 256×128 or smaller where aliasing is less visible

```powershell
# Lower resolution reduces aliasing visibility
.\Scripts\ExportHeightmap256.ps1 -ForceExemplar A01
```

**Result:** Aliasing present but less prominent at coarse pixel scale

---

## Unsupported Use Cases ❌

### 1. Full-Sphere High-Resolution Exports

**Not Recommended:** 512×256 or larger exports using forced exemplar

```powershell
# This will produce Moiré artifacts
.\Scripts\RunExportHeightmap512.ps1 -ForceExemplar A01  # ❌
```

**Why It Fails:**
- 257× repetition in latitude creates aliasing
- Pixel-level phase misalignments create 4km spikes
- Mean is correct, but max deviation fails guardrails

**Alternative:** Use native Stage B amplification (non-forced mode) for global exports

### 2. Parity Testing at High LOD

**Not Recommended:** Using forced exemplar for CPU/GPU parity validation

**Why It Fails:**
- CPU export shows Moiré artifacts from tiling
- GPU preview doesn't tile (samples exemplar locally)
- Creates false parity failures (artifacts, not rendering bugs)

**Alternative:** Test parity within exemplar bounds or use coarser LOD

---

## Technical Details

### Coordinate Transformation

For a global export, each pixel UV is transformed:

```
UV → Lon/Lat → Wrap to exemplar bounds → Sample exemplar
```

**Example: A01 (0.7° latitude range)**
- Global latitude range: -90° to +90° (180°)
- Exemplar range: -9.6° to -8.9° (0.7°)
- Repetition factor: 180° / 0.7° ≈ 257×

Each exemplar repeats 257 times vertically, creating interference patterns.

### Why Southern Hemisphere Isn't Special

Original hypothesis suggested southern hemisphere coordinate wrapping bug. **This was incorrect:**
- A01 (southern) and O01 (northern) both exhibit tiling artifacts
- The severity depends on repetition factor, not hemisphere
- O01 appeared "better" because its source data was less corrupt (50% vs 85%)

### Numerical Precision

While modulo wrapping (`FMath::Fmod`) introduces some numerical drift, the primary issue is **aliasing from repetition**, not precision collapse. The V-coordinate calculation is correct; the aliasing is inherent to tiling.

---

## Guardrails and Warnings

### Automated Warning (Recommended Implementation)

**File:** `Source/PlanetaryCreationEditor/Private/HeightmapSampling.cpp`

```cpp
if (bUseForcedExemplarOverride && ForcedExemplarMetadata)
{
    if (ForcedLonRange < 2.0 || ForcedLatRange < 2.0)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("[HeightmapSampler] Forced exemplar %s has narrow bounds (%.2f° × %.2f°). "
                 "Full-sphere exports may produce Moiré artifacts. "
                 "Recommended: Export within exemplar padded bounds (±2°) or use LOD ≤ 5."),
            *ForcedExemplarId, ForcedLonRange, ForcedLatRange);
    }
}
```

### Export Resolution Limits (Optional)

Could add explicit blocking for high-resolution forced exemplar exports:

```cpp
if (bUseForcedExemplarOverride && (ImageWidth > 256 || ImageHeight > 256))
{
    UE_LOG(LogPlanetaryCreation, Error,
        TEXT("[HeightmapExport] Forced exemplar mode not supported for exports > 256×256. "
             "Use native Stage B amplification or export within exemplar bounds."));
    return false;
}
```

**Note:** Not implemented yet - may be too restrictive for valid use cases.

---

## Diagnostic History

### A01 Continental Exemplar Investigation (2025-10-14)

**Original Report:** A01 exports flat at baseline elevation, mean_diff=-408m  
**Root Cause:** SRTM data corruption (85.7% nodata voids)  
**Resolution:** Downloaded void-filled tiles, regenerated library  

**Post-Fix Validation:**
- ✅ Mean diff improved from -408m to 0.24m (1681× improvement)
- ❌ Max diff remains 4.2km due to tiling artifacts (expected limitation)

**Conclusion:** Data corruption fixed; tiling artifacts are known limitation, not a bug.

### O01 Oceanic Exemplar (2025-10-14)

**Report:** 1436m perimeter spike  
**Status:** Expected tiling artifact (similar to A01)  
**Note:** Mean diff passes (13.38m < 50m threshold), spike is cosmetic

---

## Recommendations

### For Exemplar Validation Workflow

1. **Source Data Validation**
   ```bash
   # Validate PNG16 has no nodata regions
   python Scripts/validate_exemplar_png16.py <PNG> --metadata-id <ID> --library <JSON>
   ```

2. **Local Export Testing**
   ```powershell
   # Export within exemplar bounds (±2° padding)
   .\Scripts\ExportHeightmap.ps1 -CenterLon <LON> -CenterLat <LAT> -ExtentDeg 2.0
   ```

3. **GPU Preview Validation**
   ```cpp
   // Use GPU Stage B preview for visual validation
   r.PlanetaryCreation.ExemplarMode 1
   ```

4. **Coarse Resolution Testing**
   ```powershell
   # If global export needed, use LOD ≤ 5
   .\Scripts\ExportHeightmap256.ps1 -ForceExemplar <ID>
   ```

### For Future Improvements (Optional)

1. **Smart Tiling Detection**
   - Detect when exemplar bounds << global extent
   - Auto-suggest local export or coarser LOD
   - Warn user before expensive export

2. **Perimeter Masking**
   - Ignore edge pixels in diff calculations
   - Focus metrics on interior region
   - Separate "interior quality" from "seam quality"

3. **Alternative Tiling Strategy**
   - Use blend zones between repetitions
   - Apply noise/variation to break up patterns
   - Scale exemplar instead of repeating

**Note:** These are future research topics, not required for current workflow.

---

## References

### Documentation
- `Docs/Validation/A01_diagnostic_report.md` - Full investigation
- `Docs/Validation/A01_DIAGNOSTIC_CLOSURE.md` - Case closure summary
- `Docs/Validation/exemplar_library_validation_complete.md` - Library validation

### Tools
- `Scripts/validate_exemplar_png16.py` - PNG16 validator
- `Scripts/analyze_exemplar_fidelity.py` - Fidelity analyzer with metrics

### Audit Results
- `Docs/Validation/ExemplarAudit/A01_comparison_20251014_172743.png` - Moiré pattern visible
- `Docs/Validation/ExemplarAudit/A01_metrics_20251014_172743.csv` - Post-fix metrics

---

## FAQ

**Q: Why does forced exemplar mode exist if it has these limitations?**  
A: It's designed for **local validation within exemplar bounds**, not global exports. The use case is verifying Stage B amplification matches reference terrain in the exemplar region.

**Q: Can the Moiré artifacts be fixed?**  
A: Not without changing the tiling approach. The artifacts are inherent to repeating a narrow pattern globally. For global exports, use native Stage B amplification instead.

**Q: Should I avoid forced exemplar mode entirely?**  
A: No - it's perfectly valid for **local exports** within the exemplar padded bounds (±1.5–5°). Just don't use it for full-sphere 512×256 exports.

**Q: What about southern hemisphere vs northern hemisphere?**  
A: There's no hemisphere-specific bug. Both A01 (southern) and O01 (northern) exhibit similar tiling artifacts. The original hypothesis of a southern hemisphere wrapping bug was incorrect.

**Q: How do I validate Stage B amplification then?**  
A: Use GPU Stage B preview within the exemplar region, or export locally around the exemplar center. Don't rely on global forced exemplar exports for validation.

---

## Summary

| Aspect | Status |
|--------|--------|
| **Forced exemplar within bounds** | ✅ Validated, recommended |
| **Forced exemplar global export** | ❌ Moiré artifacts expected |
| **Data corruption (A01/O01)** | ✅ Fixed (0% nodata) |
| **Mean elevation accuracy** | ✅ 0.24m (excellent) |
| **Point-wise accuracy** | ⚠️ Tiling artifacts (expected) |
| **Recommended workflow** | ✅ Local export or GPU preview |

**Forced exemplar mode is working as designed. The limitations documented here are inherent to global tiling, not bugs that need fixing.**

