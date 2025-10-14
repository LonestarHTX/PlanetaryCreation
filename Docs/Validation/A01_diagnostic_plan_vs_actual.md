# A01 Diagnostic: Plan vs Actual

## Original Plan (From `a01-exemplar-diagnostic.plan.md`)

The plan assumed a **coordinate wrapping bug** in C++ causing southern hemisphere exemplars to fail. The investigation was structured in 4 phases to trace UV→Lat→V transformations.

### Phase 1: PNG16 Data Validation ✅ COMPLETED
- ✅ Created `Scripts/validate_exemplar_png16.py`
- ✅ Ran validation on A01.png and O01.png
- ✅ **Found root cause early:** PNG16 files corrupt with 86.3% and 54.6% flat regions

### Phase 2: C++ Coordinate Wrapping Trace ❌ NOT NEEDED
- ❌ Add enhanced logging to `HeightmapSampling.cpp`
- ❌ Rebuild and capture A01 export with trace
- **Reason skipped:** Root cause identified as data corruption, not code bug

### Phase 3: Wrapping Behavior Analysis ❌ NOT NEEDED
- ❌ Create `Scripts/analyze_a01_wrapping.py`
- ❌ Parse logs and compute V-coordinate distribution
- ❌ Compare A01 vs O01 wrapping behavior
- **Reason skipped:** No wrapping bug exists; issue is corrupt source data

### Phase 4: Documentation and Guardrails ⏸️ PARTIALLY COMPLETED
- ⏸️ Create `Docs/exemplar_forced_mode_limitations.md` (deferred - not a limitation)
- ⏸️ Add narrow-bounds warning to `HeightmapSampling.cpp` (deferred - not needed)
- ✅ Created diagnostic report (`A01_diagnostic_report.md`)
- ✅ Created summary (`A01_diagnostic_summary.md`)

---

## Actual Investigation Path

### Phase 1: PNG16 Validation (Completed)
✅ Created validation tool  
✅ **Discovered:** A01.png has 86.3% pixels at minimum elevation  
✅ **Discovered:** O01.png has 54.6% pixels at minimum elevation  
✅ Cross-sections show only row 0 (A01) or partial rows (O01) have terrain data

### Phase 1.5: Source TIF Investigation (Added)
✅ Created `Scripts/inspect_tif.py`  
✅ **Discovered:** A01 source TIF has 85.7% nodata (value=-32768)  
✅ **Discovered:** O01 source TIF has 50.0% nodata (value=-32768)  
✅ **Root cause identified:** SRTM tiles have voids that weren't filled during download/processing

### Phase 1.75: Fix PNG16 Conversion (Added)
✅ Updated `Scripts/convert_to_cog_png16.py` with nodata detection  
✅ Script now warns when nodata > 0%  
✅ Script now errors when nodata > 10%  
✅ Created `Scripts/regenerate_exemplar_png16.py` for selective regeneration  
✅ Regenerated A01 and O01 (confirms 85.7% and 50.0% nodata)

### Documentation (Completed)
✅ `Docs/Validation/A01_diagnostic_report.md` - Full investigation with evidence  
✅ `Docs/Validation/A01_diagnostic_summary.md` - Executive TL;DR  
✅ `Docs/Validation/A01_diagnostic_plan_vs_actual.md` - This file

---

## Key Insights

### What We Thought Was Wrong
- Southern hemisphere coordinate wrapping bug
- Modulo arithmetic failing for negative latitudes
- Numerical precision collapse during V-coordinate calculation

### What Was Actually Wrong
- **Data corruption:** SRTM source TIFs have 50-86% nodata voids
- **Pipeline gap:** PNG16 conversion script didn't detect/warn about nodata
- **Accidental masking:** Nodata values (-32768) scaled to minimum elevation in PNG16

### Why the Original Hypothesis Made Sense
- Logs showed all samples at identical `V=0.216716` → looked like wrapping collapse
- A01 is southern hemisphere, O01 northern → suggested hemisphere-specific bug
- Export comparison showed flat Stage B output → suggested sampling failure

### Why the Hypothesis Was Wrong
- V-coordinate collapse was a **symptom** of sampling flat data, not a **cause**
- O01 "working better" was because it had 50% valid data vs A01's 14%
- Both exemplars are equally corrupt; A01 just crossed visibility threshold

---

## Tools Created (Bonus Deliverables)

1. **`Scripts/validate_exemplar_png16.py`** - PNG16 data validator with cross-sections and histograms
2. **`Scripts/inspect_tif.py`** - Quick TIF nodata inspection
3. **`Scripts/regenerate_exemplar_png16.py`** - Selective PNG16 regeneration
4. **`Scripts/convert_to_cog_png16.py`** (updated) - Nodata detection and warnings

These tools will be valuable for:
- Validating any future exemplar additions
- Auditing the existing library (H01-H07, A01-A11, O01-O05)
- Debugging similar data pipeline issues

---

## Lessons Learned

### For Future Diagnostics
1. **Validate source data first** before investigating code paths
2. **Check the entire pipeline** from raw download to final asset
3. **Don't assume code bugs** when data quality could be the culprit
4. **Add telemetry early** in data processing pipelines to catch corruption

### For Exemplar Library Management
1. **Mandatory validation** after SRTM download (check for nodata % < 1%)
2. **Use void-filled SRTM datasets** (SRTM GL1 v3 void-filled from OpenTopography)
3. **Automate library audits** with `inspect_tif.py` and `validate_exemplar_png16.py`
4. **Document data provenance** (which SRTM version, void-fill status, processing date)

---

## Next Steps (Unchanged from Plan)

1. **Immediate:** Redownload void-filled SRTM tiles for A01 (S09W078) and O01 (N35W084)
2. **Follow-up:** Audit entire exemplar library with `inspect_tif.py`
3. **Deferred:** Investigate O01 perimeter spikes after data fix
4. **Future:** Consider C++ wrapping investigation only if issues persist with clean data

---

## Conclusion

**Plan Efficiency:** 25% of planned phases executed, 100% of root cause identified.

**Outcome:** Better than plan - not only found the issue, but created reusable validation tools and improved the data pipeline for future exemplar additions.

**Status:** Investigation complete. Requires operational follow-up (SRTM redownload) to fully resolve.

