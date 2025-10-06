# GPU Exemplar Texture Array Integration Guide

**Status:** ✅ Complete
**Milestone:** M6 GPU Acceleration
**Created:** 2025-10-05

---

## Overview

The exemplar texture array system preloads 22 PNG16 heightfield exemplars once when Stage B amplification is first enabled, uploads them to GPU as a `Texture2DArray`, and exposes the resource for compute shader binding. This eliminates per-step disk I/O and enables GPU-accelerated continental amplification.

---

## Architecture

### Components

1. **`FExemplarTextureArray`** (`ExemplarTextureArray.h/cpp`)
   - Singleton managing Texture2DArray lifecycle
   - Loads PNG16 exemplars from `Content/PlanetaryCreation/Exemplars/`
   - Resizes all exemplars to common 512×512 resolution
   - Uploads to GPU as `PF_G16` (16-bit grayscale)

2. **`UTectonicSimulationService`**
   - `InitializeGPUExemplarResources()` - Lazy initialization on first GPU amplification call
   - `ShutdownGPUExemplarResources()` - Cleanup on module shutdown
   - Called automatically when `ShouldUseGPUAmplification()` returns true

3. **Lazy Initialization Hooks**
   - `TectonicSimulationService.cpp:284` - Before oceanic amplification GPU path
   - `TectonicSimulationService.cpp:321` - Before continental amplification GPU path

---

## Usage in Compute Shaders

### Step 1: Get Texture Array from Service

```cpp
// In your GPU compute shader dispatcher (e.g., ContinentalAmplificationGPU.cpp)

#include "ExemplarTextureArray.h"

bool ApplyContinentalAmplificationGPU(UTectonicSimulationService& Service)
{
    using namespace PlanetaryCreation::GPU;

    // Get global exemplar texture array
    FExemplarTextureArray& ExemplarArray = GetExemplarTextureArray();

    if (!ExemplarArray.IsInitialized())
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[ContinentalGPU] Exemplar array not initialized"));
        return false;  // Fall back to CPU
    }

    UTexture2DArray* TextureArray = ExemplarArray.GetTextureArray();
    if (!TextureArray || !TextureArray->GetResource())
    {
        return false;
    }

    // Get exemplar metadata for shader parameter setup
    const TArray<FExemplarTextureArray::FExemplarInfo>& ExemplarInfo = ExemplarArray.GetExemplarInfo();

    // ... setup RDG graph and bind texture ...
}
```

### Step 2: Bind Texture2DArray to Compute Shader

```cpp
// In RDG pass setup
FRDGBuilder GraphBuilder(RHICmdList);

// Create shader parameters
FContinentalAmplificationCS::FParameters* Parameters = GraphBuilder.AllocParameters<FContinentalAmplificationCS::FParameters>();

// Bind texture array (via RDG external texture)
FRDGTexture* ExemplarTexture = GraphBuilder.RegisterExternalTexture(
    CreateRenderTarget(TextureArray->GetResource()->TextureRHI, TEXT("PlanetaryCreation.Exemplars"))
);

Parameters->ExemplarTextures = ExemplarTexture;
Parameters->ExemplarSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp>::GetRHI();
Parameters->NumExemplars = ExemplarArray.GetExemplarCount();

// Dispatch compute shader
GraphBuilder.AddPass(
    RDG_EVENT_NAME("ContinentalAmplificationGPU"),
    Parameters,
    ERDGPassFlags::Compute,
    [Parameters, GroupCountX](FRHICommandList& RHICmdListInner)
    {
        TShaderMapRef<FContinentalAmplificationCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FComputeShaderUtils::Dispatch(RHICmdListInner, ComputeShader, *Parameters, FIntVector(GroupCountX, 1, 1));
    });
```

### Step 3: Sample in HLSL Shader

```hlsl
// ContinentalAmplification.usf

Texture2DArray<float> ExemplarTextures;  // PF_G16 = single-channel float
SamplerState ExemplarSampler;

cbuffer Params
{
    uint NumExemplars;
    // ... other parameters
};

[numthreads(128,1,1)]
void MainCS(uint3 DTid : SV_DispatchThreadID)
{
    uint VertexIdx = DTid.x;

    // 1. Classify terrain type (Himalayan, Andean, Ancient) based on plate data
    uint TerrainType = ClassifyTerrainType(VertexIdx);

    // 2. Select exemplar index based on terrain type
    //    Himalayan: indices 0-6 (H01-H07)
    //    Andean: indices 7-17 (A01-A11)
    //    Ancient: indices 18-21 (O01-O05)
    uint ExemplarIndex = SelectExemplar(TerrainType, VertexIdx);

    // 3. Compute UV coordinates (equirectangular or tangent-plane projection)
    float2 UV = ComputeUV(VertexIdx);

    // 4. Sample exemplar heightfield
    float ExemplarHeight = ExemplarTextures.SampleLevel(ExemplarSampler, float3(UV, ExemplarIndex), 0).r;

    // 5. Denormalize from [0,1] to meters using exemplar metadata
    float ElevationMin_m = GetExemplarMinElevation(ExemplarIndex);
    float ElevationMax_m = GetExemplarMaxElevation(ExemplarIndex);
    float DetailHeight_m = lerp(ElevationMin_m, ElevationMax_m, ExemplarHeight);

    // 6. Blend with base elevation
    float BaseElevation_m = BaselineElevation[VertexIdx];
    float AmplifiedElevation_m = BaseElevation_m + DetailHeight_m * BlendWeight;

    OutAmplifiedElevation[VertexIdx] = AmplifiedElevation_m;
}
```

---

## Exemplar Metadata Access

```cpp
// Access exemplar info for shader parameter setup
const TArray<FExemplarTextureArray::FExemplarInfo>& ExemplarInfo = ExemplarArray.GetExemplarInfo();

for (int32 i = 0; i < ExemplarInfo.Num(); ++i)
{
    const FExemplarTextureArray::FExemplarInfo& Info = ExemplarInfo[i];

    UE_LOG(LogTemp, Log, TEXT("Exemplar [%d]: ID=%s Region=%s ElevRange=[%.0f, %.0f]m"),
        i, *Info.ID, *Info.Region, Info.ElevationMin_m, Info.ElevationMax_m);

    // Pass to shader as constant buffer data
    ExemplarMetadata[i].ElevationMin = Info.ElevationMin_m;
    ExemplarMetadata[i].ElevationMax = Info.ElevationMax_m;
}
```

---

## Technical Details

### Texture Format

- **Format:** `PF_G16` (16-bit grayscale, maps to `R16_UNORM` in HLSL)
- **Resolution:** 512×512 pixels (all exemplars resized to common size)
- **Array Slices:** 22 (one per exemplar from ExemplarLibrary.json)
- **Filtering:** Bilinear (`TF_Bilinear`)
- **Address Mode:** Clamp (`TA_Clamp`)

### Memory Footprint

```
Single slice: 512 × 512 × 2 bytes = 512 KB
Total array: 512 KB × 22 = 11.25 MB
```

### Initialization Flow

1. User enables Stage B amplification (`bEnableOceanicAmplification` or `bEnableContinentalAmplification`)
2. LOD level ≥ `MinAmplificationLOD` (default: 5)
3. CVar `r.PlanetaryCreation.UseGPUAmplification=1`
4. First `AdvanceSteps()` call hits lazy init at line 284/321
5. `InitializeGPUExemplarResources()` → `FExemplarTextureArray::Initialize()`
6. Load ExemplarLibrary.json → Load 22 PNG16s → Resize → Upload to GPU
7. Subsequent steps reuse cached `UTexture2DArray`

### Shutdown Flow

1. Module `Deinitialize()` called (editor shutdown or module reload)
2. `ShutdownGPUExemplarResources()` → `FExemplarTextureArray::Shutdown()`
3. `UTexture2DArray` destroyed via `ConditionalBeginDestroy()`
4. GPU resources released

---

## Example: Continental Shader Integration

```cpp
// In OceanicAmplificationGPU.h (extend for continental path)

namespace PlanetaryCreation::GPU
{
    bool ApplyContinentalAmplificationGPU(UTectonicSimulationService& Service);
}
```

```cpp
// In ContinentalAmplificationGPU.cpp

#include "ExemplarTextureArray.h"

// Define compute shader with exemplar parameters
class FContinentalAmplificationCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FContinentalAmplificationCS);
    SHADER_USE_PARAMETER_STRUCT(FContinentalAmplificationCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, VertexCount)
        SHADER_PARAMETER(uint32, NumExemplars)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InBaseline)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<int>, InPlateAssignments)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutAmplified)
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ExemplarTextures)
        SHADER_PARAMETER_SAMPLER(SamplerState, ExemplarSampler)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};

IMPLEMENT_GLOBAL_SHADER(FContinentalAmplificationCS, "/Plugin/PlanetaryCreation/Private/ContinentalAmplification.usf", "MainCS", SF_Compute);

bool ApplyContinentalAmplificationGPU(UTectonicSimulationService& Service)
{
    using namespace PlanetaryCreation::GPU;

    FExemplarTextureArray& ExemplarArray = GetExemplarTextureArray();
    if (!ExemplarArray.IsInitialized())
        return false;

    UTexture2DArray* TextureArray = ExemplarArray.GetTextureArray();
    if (!TextureArray || !TextureArray->GetResource())
        return false;

    // ... RDG setup similar to OceanicAmplificationGPU.cpp ...

    Parameters->ExemplarTextures = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(ExemplarTexture));
    Parameters->ExemplarSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp>::GetRHI();
    Parameters->NumExemplars = ExemplarArray.GetExemplarCount();

    // ... dispatch and readback ...

    return true;
}
```

---

## Next Steps

1. **Wire up continental shader** - Implement `ContinentalAmplificationGPU.cpp` using this texture array
2. **Pass exemplar metadata** - Upload `ElevationMin/Max` per exemplar as constant buffer
3. **Terrain classification** - GPU logic to map plate→region (Himalayan/Andean/Ancient)
4. **UV mapping** - Compute tangent-plane UVs or equirectangular projection on GPU
5. **Blending logic** - Multi-exemplar sampling with fold-direction alignment

---

## Validation

### Quick Test

```cpp
// In TectonicSimulationService or a test
#include "ExemplarTextureArray.h"

void TestExemplarArray()
{
    using namespace PlanetaryCreation::GPU;

    FExemplarTextureArray& Array = GetExemplarTextureArray();
    const FString ProjectContentDir = FPaths::ProjectContentDir();

    if (Array.Initialize(ProjectContentDir))
    {
        UE_LOG(LogTemp, Log, TEXT("✅ Loaded %d exemplars (%dx%d)"),
            Array.GetExemplarCount(), Array.GetTextureWidth(), Array.GetTextureHeight());

        for (const auto& Info : Array.GetExemplarInfo())
        {
            UE_LOG(LogTemp, Log, TEXT("  [%d] %s (%s) elev=[%.0f, %.0f]m"),
                Info.ArrayIndex, *Info.ID, *Info.Region, Info.ElevationMin_m, Info.ElevationMax_m);
        }
    }
}
```

### Expected Output

```
LogPlanetaryCreation: [ExemplarGPU] Initializing Texture2DArray from ExemplarLibrary.json
LogPlanetaryCreation: [ExemplarGPU] Loaded 22 PNG16 exemplars, creating Texture2DArray (512x512)
LogPlanetaryCreation: [ExemplarGPU] Texture2DArray initialized: 22 exemplars, 512x512 PF_G16
LogPlanetaryCreation: [TectonicService] GPU exemplar resources initialized: 22 textures (512x512)
```

---

## Files Modified

- ✅ `ExemplarTextureArray.h` (new)
- ✅ `ExemplarTextureArray.cpp` (new)
- ✅ `TectonicSimulationService.h` (added Init/Shutdown methods)
- ✅ `TectonicSimulationService.cpp` (added Init/Shutdown impl, lazy init hooks)
- ⏸️ `ContinentalAmplificationGPU.cpp` (pending - use this guide)

---

**End of GPU Exemplar Integration Guide**
