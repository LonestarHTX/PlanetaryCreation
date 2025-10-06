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

### 🔨 Pending Components

#### 4. WPO Material Setup (Manual - Unreal Editor)
Create a new Material asset in Content Browser:

**Material Graph:**
```
[Texture Object Parameter: "HeightTexture" (PF_R16F)]
    ↓
[TextureSample]
    ↓ RGB
[Multiply] ← [Scalar Parameter: "ElevationScale" = 100.0]  // meters → cm
    ↓
[Multiply] ← [VertexNormalWS]  // Displace along normal
    ↓
[World Position Offset]
```

**Optional: Normal Recomputation (for lighting accuracy)**
```
[HeightTexture] → [ComputeFilterWidth] → [DDX/DDY] → [Cross Product] → [Normal]
```

**Material Properties:**
- Domain: Surface
- Blend Mode: Opaque
- Shading Model: Default Lit

#### 5. Controller Integration
Add preview mode toggle to `FTectonicSimulationController`:

```cpp
// In TectonicSimulationController.h
private:
    bool bUseGPUPreviewMode = false;
    FTextureRHIRef GPUHeightTexture;
    FIntPoint HeightTextureSize = FIntPoint(2048, 1024);

public:
    void SetGPUPreviewMode(bool bEnabled);
    bool IsGPUPreviewModeEnabled() const { return bUseGPUPreviewMode; }
```

#### 6. Material Parameter Binding
Update mesh build to bind height texture to material:

```cpp
// In BuildAndUpdateMesh() or UpdatePreviewMesh()
if (bUseGPUPreviewMode)
{
    // Run GPU preview shader (no readback)
    PlanetaryCreation::GPU::ApplyOceanicAmplificationGPUPreview(
        *Service, GPUHeightTexture, HeightTextureSize);

    // Bind texture to material dynamic instance
    if (PreviewActor.IsValid())
    {
        URealtimeMeshComponent* MeshComp = PreviewActor->GetRealtimeMeshComponent();
        if (UMaterialInstanceDynamic* MID = MeshComp->CreateDynamicMaterialInstance(0))
        {
            MID->SetTextureParameterValue(FName("HeightTexture"), GPUHeightTexture);
            MID->SetScalarParameterValue(FName("ElevationScale"), 100.0f);
        }
    }
}
else
{
    // Legacy CPU readback path
    Service->ApplyOceanicAmplificationGPU();
}
```

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

### Unit Test (Automation):
```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGPUPreviewParityTest,
    "PlanetaryCreation.Milestone6.GPU.PreviewParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGPUPreviewParityTest::RunTest(const FString& Parameters)
{
    // 1. Run CPU readback path, capture elevation
    // 2. Run GPU preview path, sample texture at vertex UVs
    // 3. Compare: Mean delta < 0.1m, Max delta < 1.0m
    return true;
}
```

### Manual Testing:
1. Enable GPU preview mode via UI toggle
2. Step simulation 10 times at L7 (81,920 vertices)
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
1. Create WPO material asset in Unreal Editor
2. Add `bUseGPUPreviewMode` flag to controller
3. Wire texture binding in `BuildAndUpdateMesh()`
4. Add UI toggle in `SPTectonicToolPanel`
5. Run parity test to validate displacement accuracy
6. Profile step time improvements at high LOD

## References
- **Plan Document:** `Docs/step_time_optimization_plan.md` (Section 3, Option A)
- **Paper:** "Procedural Tectonic Planets" (Stage B amplification, Section 5)
- **Existing CPU Path:** `OceanicAmplification.cpp` (baseline for parity tests)
