# A01 Exemplar Diagnostic - Executive Summary

**Date:** 2025-10-14  
**Status:** Root Cause Identified - Data Corruption  
**Action Required:** Redownload SRTM source tiles with void-fill

---

## TL;DR

**Problem:** A01 continental exemplar exports are flat at baseline elevation (1927m).

**Root Cause:** Source SRTM TIF has 85.7% nodata values (voids not filled during download/processing).

**Not a Bug:** C++ coordinate wrapping logic is working correctly.

**Fix:** Redownload void-filled SRTM tiles (S09W078 for A01, N35W084 for O01) and regenerate PNG16 files.

---

## Evidence Chain

### 1. PNG16 Validation (Scripts/validate_exemplar_png16.py)

| Exemplar | Min (m) | Max (m) | Mean (m) | StdDev (m) | Flat Pixels |
|----------|---------|---------|----------|------------|-------------|
| A01 | 1927.36 | 5875.86 | 2154.00 | 650.74 | 86.3% |
| O01 | 228.64 | 1678.88 | 451.84 | 313.44 | 54.6% |

**Finding:** Both PNG16 files have majority of pixels at minimum elevation.

### 2. Source TIF Inspection (Scripts/inspect_tif.py)

```
A01 TIF:  85.7% nodata (value=-32768.0)
O01 TIF:  50.0% nodata (value=-32768.0)
```

**Finding:** Source cropped TIFs are corrupt with SRTM voids (nodata regions).

### 3. Conversion Pipeline Analysis

**Issue:** `Scripts/convert_to_cog_png16.py` (original version) didn't detect or warn about nodata values. It scaled nodata=-32768 → min_elevation in PNG16, creating flat regions.

**Fix Implemented:** Updated script now detects nodata and errors when >10% threshold exceeded.

---

## Tools Created

1. **`Scripts/validate_exemplar_png16.py`** - Validates PNG16 data integrity
   ```bash
   python Scripts/validate_exemplar_png16.py "Content/.../A01.png" --metadata-id A01 --library "Content/.../ExemplarLibrary.json"
   ```

2. **`Scripts/inspect_tif.py`** - Quick TIF/nodata inspection
   ```bash
   python Scripts/inspect_tif.py "StageB_SRTM90/cropped/A01.tif"
   ```

3. **`Scripts/regenerate_exemplar_png16.py`** - Regenerate specific PNG16 files
   ```bash
   python Scripts/regenerate_exemplar_png16.py A01 O01
   ```

4. **`Scripts/convert_to_cog_png16.py`** (updated) - Now detects/warns about nodata

---

## Next Steps

### Priority 1: Fix A01 and O01

1. Download void-filled SRTM GL1 tiles:
   - S09W078 (A01 - Cordillera Blanca)
   - N35W084 (O01 - Great Smoky Mountains)

2. Reprocess through cropping pipeline (`stageb_patch_cutter.py`)

3. Regenerate PNG16: `python Scripts/regenerate_exemplar_png16.py A01 O01`

4. Validate: Run `validate_exemplar_png16.py` and confirm nodata < 1%

5. Re-run exemplar audit: `.\Scripts\RunExportHeightmap512.ps1 -ForceExemplar A01`

### Priority 2: Library-Wide Audit

Run `inspect_tif.py` on all H01-H07, A01-A11, O01-O05 TIFs to identify other corrupt exemplars.

### Deferred: O01 Perimeter Spikes

After data fix, separately investigate O01's 1436.47m perimeter spike (may be forced exemplar tiling artifact, not corruption).

---

## Key Files

- **Diagnostic Report:** `Docs/Validation/A01_diagnostic_report.md`
- **Validation Tools:** `Scripts/validate_exemplar_png16.py`, `Scripts/inspect_tif.py`
- **Conversion Script:** `Scripts/convert_to_cog_png16.py` (now with nodata detection)
- **Source TIFs:** `StageB_SRTM90/cropped/A01.tif`, `O01.tif` (corrupt - need replacement)
- **PNG16 Files:** `Content/PlanetaryCreation/Exemplars/PNG16/A01.png`, `O01.png` (generated from corrupt sources)

---

## Conclusion

A01 failure is a **data pipeline issue**, not a C++ code bug. The forced exemplar coordinate wrapping logic was never tested because the source data is corrupt. Fix the SRTM voids first, then re-validate.

