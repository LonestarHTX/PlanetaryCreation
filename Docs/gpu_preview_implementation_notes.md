# GPU Preview Path Implementation Notes

## Overview
Implements **Option A** from `step_time_optimization_plan.md` - PF_R16F equirectangular height texture + WPO material workflow for Stage B displacement.

## Implementation Status

### ✅ Completed Components

#### 1. GPU Compute Shader (`OceanicAmplificationPreview.usf`)
- Writes amplified elevation directly to `PF_R16F` texture
- Uses `RWTexture2D<float>` UAV for on-device writes
- Dispatches per-vertex (64 threads/group) and writes to equirectangular pixel coords
- **Location:** `Source/PlanetaryCreationEditor/Shaders/Private/OceanicAmplificationPreview.usf`

#### 2. C++ Shader Integration (`OceanicAmplificationGPU.cpp`)
- New function: `ApplyOceanicAmplificationGPUPreview()`
- Creates `PF_R16F` FRDGTexture (2048x1024 default)
- Eliminates `FRHIGPUBufferReadback` - **no CPU stall**
- Returns persistent `FTextureRHIRef` for material binding
- **Location:** `Source/PlanetaryCreationEditor/Private/OceanicAmplificationGPU.cpp:215-339`

#### 3. Equirectangular UVs
- **Already implemented** in `TectonicSimulationController.cpp:463-467`
- Formula:
  ```cpp
  const double UAngle = FMath::Atan2(UnitNormal.Y, UnitNormal.X);
  const double VAngle = FMath::Asin(FMath::Clamp(UnitNormal.Z, -1.0, 1.0));
  const float U = 0.5f + UAngle / (2.0 * PI);
  const float V = 0.5f - VAngle / PI;
  ```
- Mesh vertices automatically have correct UVs for height texture sampling

### ✅ Integration Complete (Controller + Material)

#### 5. Controller Integration (`TectonicSimulationController`)
**Added:**
- `bool bUseGPUPreviewMode` flag
- `FTextureRHIRef GPUHeightTexture` persistent texture handle
- `FIntPoint HeightTextureSize` (2048x1024 default)
- `SetGPUPreviewMode(bool)` / `IsGPUPreviewModeEnabled()` API

**Material Loading** (`EnsurePreviewActor()` - line 622):
- Loads `M_PlanetSurface_GPUDisplacement` when GPU mode enabled
- Falls back to vertex color material if load fails

**GPU Preview Execution** (`UpdatePreviewMesh()` - line 692):
- Calls `ApplyOceanicAmplificationGPUPreview()` after mesh update
- Creates `MaterialInstanceDynamic` from base material
- Binds GPU height texture to `HeightTexture` parameter
- Sets `ElevationScale` to 100.0 (meters → centimeters)

### ✅ UI & Material Wiring
- Slate toggle: `SPTectonicToolPanel` exposes a **GPU Preview Mode** checkbox that calls `FTectonicSimulationController::SetGPUPreviewMode` (`Source/PlanetaryCreationEditor/Private/SPTectonicToolPanel.cpp:560`).
- Material binding: `FTectonicSimulationController::UpdatePreviewMesh` creates/updates a dynamic material instance, pushes the height texture, and sets the elevation scalar automatically (`Source/PlanetaryCreationEditor/Private/TectonicSimulationController.cpp:1504`).
- Default asset `M_PlanetSurface_GPUDisplacement` lives in `/Game/PlanetaryCreation/Materials/` and is auto-loaded when the toggle is enabled.

### ✅ Runtime Texture Flow
- Preview requests allocate from a small `FRHIGPUBufferReadback` pool; jobs finalize through `ProcessPendingOceanicGPUReadbacks`, preserving async behaviour.
- `UTectonicSimulationService` keeps CPU snapshots in sync and flips `bSkipCPUAmplification` so the legacy amplification loops are bypassed while preview mode is active.

## Performance Benefits

### Before (CPU Readback Path):
```
GPU Compute → Buffer → Readback Fence → CPU Copy → Mesh Update
              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
              ⚠️ STALL: ~5-10 ms @ 10K vertices
```

### After (GPU Preview Path):
```
GPU Compute → Texture (stays on GPU) → Material WPO → Render
              ^^^^^^^^^^^^^^^^^^^^^^^^
              ✅ NO STALL: Direct render-thread access
```

### Expected Improvements:
- **Removes readback stall** from step loop
- **Enables real-time updates** at high LOD (L5-L7)
- **Paves way for continental amplification** GPU preview
- **Maintains CPU path** for collision/picking (not affected)

## Testing Plan

### Automation
- `PlanetaryCreation.Milestone6.GPU.PreviewVertexParity` verifies vertex duplication and seam coverage for the preview texture write path.
- `PlanetaryCreation.Milestone6.GPU.OceanicParity` continues to compare CPU/GPU elevation outputs; the preview toggle is exercised as part of that suite.

### Manual Testing:
1. Enable GPU preview mode via the Tectonic Tool panel checkbox.
2. Step simulation 10 times at L7 (81,920 vertices).
3. Verify:
   - Height displacement matches CPU path visually
   - Frame time improves (measure with `stat unit`)
   - No readback stalls in profiler (Unreal Insights)

## Material Seam Handling

Equirectangular textures have a seam at U=0/U=1 (longitude ±180°). Current vertex-driven writes will naturally duplicate values at seam vertices.

**Mitigation:**
- Texture filtering (bilinear) smooths minor discontinuities
- Future: Add seam-aware texture write with atomic max/min

## Collision and Picking

**Preview mode is visualization-only:**
- Collision mesh remains CPU-side (uses readback path if needed)
- Editor picking uses CPU vertex positions
- GPU preview only affects rendered appearance

## Next Steps
1. Continue reducing seam artefacts (investigate duplicate-column writes or cube-face projection).
2. Expand preview to continental amplification once exemplar sampling is GPU-backed.
3. Investigate keeping the preview entirely GPU-resident (material-only displacement) for camera-driven LOD without readback.

## References
- **Plan Document:** `Docs/step_time_optimization_plan.md` (Section 3, Option A)
- **Paper:** "Procedural Tectonic Planets" (Stage B amplification, Section 5)
- **Existing CPU Path:** `OceanicAmplification.cpp` (baseline for parity tests)
