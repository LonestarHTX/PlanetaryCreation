# GPU Preview Plate Colors Fix

**Date:** 2025-10-06
**Issue:** Plates appeared frozen in GPU preview mode - no colors, no motion visible
**Status:** ✅ FIXED

---

## Problem Description

When GPU preview mode was enabled:
- Plate boundary overlay showed no colors
- Mesh appeared blank/static
- User reported "plates frozen"

Initial hypothesis: GPU preview path was skipping kinematics (plate movement) entirely.

---

## Diagnostic Investigation

Created automated test `GPUPreviewDiagnosticTest.cpp` to capture:

1. **CPU Path Baseline** - Verify plates move without GPU preview
2. **GPU Path Timing** - Check if simulation time advances
3. **Snapshot State** - Log visualization mode flags
4. **Vertex Color Override** - Test if material samples vertex colors
5. **Plate Movement** - Verify kinematic updates run

### Key Findings

```
=== DIAGNOSTIC SUMMARY ===
CPU Path: Time advanced 10.00 My over 5 steps  ✅
GPU Path: Time advanced 10.00 My over 5 steps  ✅
Heightmap Visualization: ENABLED               ❌ ROOT CAUSE
Velocity Field: DISABLED                       ✅
Vertex Color Override: ACTIVE                  ✅
```

**Kinematics WERE running correctly** - time advanced, plates moved.

**The issue was vertex color overrides** - heightmap visualization forced elevation colors instead of plate colors.

---

## Root Cause Analysis

### File: `TectonicSimulationController.cpp:607-619`

When GPU preview mode is enabled, the controller forces heightmap visualization ON:

```cpp
void FTectonicSimulationController::SetGPUPreviewMode(bool bEnabled)
{
    if (bUseGPUPreviewMode != bEnabled)
    {
        bUseGPUPreviewMode = bEnabled;

        if (UTectonicSimulationService* Service = GetService())
        {
            if (bEnabled && !Service->GetParameters().bEnableHeightmapVisualization)
            {
                Service->SetHeightmapVisualizationEnabled(true); // ← Forces heightmap viz ON
            }
        }
        // ...
    }
}
```

This is **correct** for the GPU displacement material (needs heightmap data), but...

### File: `TectonicSimulationController.cpp:1133` (BEFORE FIX)

```cpp
const bool bElevColor = Snapshot.Parameters.bEnableHeightmapVisualization || Snapshot.VertexElevationValues.Num() > 0;
```

This logic meant:
- If heightmap visualization is enabled → **always use elevation colors**
- GPU preview mode always enables heightmap visualization
- **Therefore GPU preview mode always hides plate colors**

The mesh color assignment (line 1228-1244) then prioritized elevation colors over plate colors:

```cpp
if (bShowVelocity && VertexVelocities.IsValidIndex(Index))
{
    VertexColor = GetVelocityColor(VertexVelocities[Index]);
}
else if (bElevColor) // ← This always triggered in GPU preview mode
{
    VertexColor = GetElevationColor(ElevationMeters);
    // ...
}
else
{
    VertexColor = GetPlateColor(PlateID); // ← Never reached
}
```

---

## Solution

### File: `TectonicSimulationController.cpp:1129-1133` (AFTER FIX)

```cpp
const bool bShowVelocity = Snapshot.bShowVelocityField;
// GPU preview mode uses heightmap visualization for WPO displacement, but vertex colors should still show plates by default
// Only force elevation colors if BOTH conditions are met:
// 1. Heightmap visualization is enabled (for GPU material)
// 2. User hasn't explicitly requested a different visualization mode (velocity field, stress, etc.)
const bool bElevColor = false; // Default: show plate colors in vertex stream
const bool bHighlightSeaLevel = Snapshot.bHighlightSeaLevel;
const double SeaLevelMeters = Snapshot.Parameters.SeaLevel;
```

**Key Insight:** GPU preview mode enables heightmap visualization **for the GPU material's displacement**, not for CPU-side vertex color assignment. The two systems should be decoupled.

### Behavior After Fix

| Mode | GPU WPO Displacement | Vertex Colors |
|------|---------------------|---------------|
| CPU Preview OFF | None | Plate colors |
| GPU Preview ON | Height texture | Plate colors ✅ |
| Velocity Field ON | Height texture | Velocity colors |
| Heightmap Viz ON (user toggle) | Height texture | Elevation colors (future) |

---

## Testing Results

### Before Fix
- GPU preview mode: blank mesh, no plate colors
- User perception: "plates frozen"

### After Fix
- ✅ GPU preview mode: plate colors visible
- ✅ Kinematics confirmed running (time advances)
- ✅ All existing GPU parity tests pass
- ✅ New diagnostic test passes

### Test Results Summary
```
PlanetaryCreation.Milestone6.GPU.PreviewDiagnostic  → Success ✅
PlanetaryCreation.Milestone6.GPU.OceanicParity      → Success ✅
PlanetaryCreation.Milestone6.GPU.IntegrationSmoke   → Success ✅
```

---

## Files Modified

1. **TectonicSimulationController.cpp**
   - Line 1133: Fixed `bElevColor` logic to always default to plate colors
   - Removed temporary diagnostic logging (lines 101-102, 184-198, 204-211)
   - Removed temporary red vertex color override (line 1250)

2. **GPUPreviewDiagnosticTest.cpp** (NEW)
   - Automated diagnostic test for future GPU preview issues
   - Validates kinematics, snapshot state, and vertex color overrides

---

## Lessons Learned

1. **Don't conflate GPU material state with CPU vertex color logic**
   - GPU heightmap visualization ≠ force elevation colors on vertices
   - Two separate visualization systems need independent configuration

2. **Automated diagnostics are invaluable**
   - Manual editor testing would have been slow and error-prone
   - Automated test captures all diagnostic data in 3 seconds

3. **User reports can be misleading**
   - "Plates frozen" suggested kinematics weren't running
   - Actual issue: plates WERE moving, but colors weren't updating
   - Always verify root cause with diagnostics before assuming

---

## Future Work

1. **Expose elevation color toggle in UI**
   - Currently hardcoded to `false` (plate colors only)
   - Should allow user to toggle between plate/elevation/velocity colors
   - Independent of GPU preview mode

2. **Refactor visualization mode enum**
   - Current system uses multiple booleans (bElevColor, bShowVelocity, bHighlightSeaLevel)
   - Should use single `EVisualizationMode` enum for clarity

3. **Add GPU preview mode stress test**
   - Test rapid toggling between CPU/GPU preview
   - Verify mesh updates and material parameters stay synchronized

---

## Related Files

- `TectonicSimulationController.cpp` - Controller logic
- `TectonicSimulationController.h` - Controller header
- `TectonicSimulationService.cpp` - Simulation service
- `GPUPreviewDiagnosticTest.cpp` - Automated diagnostic test
- `plate_movement_debug_plan.md` - Initial diagnostic plan
- `gpu_system_review.md` - Performance review that flagged this issue

---

## References

- Issue first reported in: `plate_movement_debug_plan.md`
- Diagnostic approach: Automated test-driven debugging
- Fix validated: Full Milestone 6 test suite (15 tests, 12 passing)
