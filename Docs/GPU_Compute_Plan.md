# GPU Compute Acceleration Implementation Plan

**Status:** 📋 Planning
**Timeline:** 3-4 weeks (1 engineer full-time)
**Dependencies:** Milestone 6 Phase 2 complete (Stage B amplification CPU baseline)
**Performance Goal:** Enable LOD Levels 7-8 (163K-655K vertices) with <500ms step times

---

## 🎯 **Objective**

Accelerate Stage B amplification (oceanic + continental) using **Unreal Engine 5.5 RDG compute shaders** to enable LOD Levels 7-8 (163K-655K vertices) with <500ms step times.

---

## 📊 **Current Bottlenecks** (Projected L6 @ 40,962 vertices)

| System | Current (CPU) | GPU Target | Speedup |
|--------|---------------|------------|---------|
| Oceanic Amplification | ~6-8ms | ~0.5-1ms | 8-12× |
| Continental Amplification | ~8-12ms | ~1-2ms | 6-10× |
| Stress Interpolation | ~4.4ms | ~0.8ms | 5× |
| **Total Amplification** | **~18-24ms** | **~2-4ms** | **6-10×** |

**L7 (163K vertices) projection**: CPU ~80ms → GPU ~10ms
**L8 (655K vertices) projection**: CPU ~300ms → GPU ~35ms

---

## 🛠️ **Phase 1: Infrastructure Setup** (Week 1)

### Task 1.1: Module Dependencies
**Files**: `PlanetaryCreationEditor.Build.cs`
- Add `"RenderCore"`, `"RHI"`, `"Renderer"` to `PrivateDependencyModuleNames`
- Add `"Projects"` for virtual shader path mapping

**Code Change**:
```csharp
PrivateDependencyModuleNames.AddRange(new[]
{
    "UnrealEd",
    "LevelEditor",
    "Projects",
    "RealtimeMeshComponent",
    "InputCore",
    "Blutility",
    "EditorStyle",
    "UMG",
    "WorkspaceMenuStructure",
    "ImageWrapper",
    "Json",
    "JsonUtilities",
    "RenderCore",      // GPU compute shader support
    "RHI",             // Low-level rendering interface
    "Renderer"         // RDG (Render Dependency Graph)
});
```

### Task 1.2: Shader Directory Structure
**Create**:
```
Source/PlanetaryCreationEditor/
├── Shaders/
│   ├── Private/
│   │   ├── OceanicAmplification.usf       # Gabor + Perlin noise
│   │   ├── ContinentalAmplification.usf   # Exemplar sampling
│   │   ├── StressInterpolation.usf        # Gaussian kernels (future)
│   │   └── TectonicCommon.ush             # Shared utilities
│   └── Shaders.Build.cs                    # Virtual path mapping
```

### Task 1.3: Virtual Shader Path Registration
**File**: `PlanetaryCreationEditorModule.cpp`

**Add to `StartupModule()`**:
```cpp
void FPlanetaryCreationEditorModule::StartupModule()
{
    // Existing code...

    // Register virtual shader path for compute shaders
    FString ShaderDirectory = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("PlanetaryCreationEditor/Shaders"));
    AddShaderSourceDirectoryMapping(TEXT("/Plugin/PlanetaryCreation"), ShaderDirectory);
}
```

**Add to `ShutdownModule()`**:
```cpp
void FPlanetaryCreationEditorModule::ShutdownModule()
{
    // Existing code...

    // Unregister shader path
    ResetAllShaderSourceDirectoryMappings();
}
```

---

## 🌊 **Phase 2: Oceanic Amplification GPU** (Week 1-2)

### Task 2.1: Shader Implementation
**File**: `Shaders/Private/OceanicAmplification.usf`

**Input Buffers** (Structured):
- `RenderVertices` (float3)
- `VertexPlateAssignments` (int32)
- `VertexElevationValues` (float)
- `VertexCrustAge` (float)
- `VertexRidgeDirections` (float3)
- `PlatesCrustType` (uint8 array, oceanic=0)

**Output Buffer** (UAV):
- `VertexAmplifiedElevation` (float)

**Shader Logic**:
```hlsl
#include "/Engine/Private/Common.ush"
#include "/Plugin/PlanetaryCreation/Private/TectonicCommon.ush"

// Parameters
StructuredBuffer<float3> RenderVertices;
StructuredBuffer<int> VertexPlateAssignments;
StructuredBuffer<float> VertexElevationValues;
StructuredBuffer<float> VertexCrustAge;
StructuredBuffer<float3> VertexRidgeDirections;
StructuredBuffer<uint> PlatesCrustType;
RWStructuredBuffer<float> VertexAmplifiedElevation;

uint VertexCount;

[numthreads(256, 1, 1)]
void OceanicAmplificationCS(uint3 ThreadID : SV_DispatchThreadID)
{
    uint VertexIdx = ThreadID.x;
    if (VertexIdx >= VertexCount)
        return;

    // Skip continental vertices (branch divergence acceptable)
    int PlateID = VertexPlateAssignments[VertexIdx];
    if (PlatesCrustType[PlateID] != 0) // Continental
    {
        VertexAmplifiedElevation[VertexIdx] = VertexElevationValues[VertexIdx];
        return;
    }

    float3 Position = RenderVertices[VertexIdx];
    float BaseElevation = VertexElevationValues[VertexIdx];
    float CrustAge = VertexCrustAge[VertexIdx];
    float3 RidgeDir = VertexRidgeDirections[VertexIdx];

    // ============================================================================
    // TRANSFORM FAULT DETAIL (Gabor noise approximation)
    // ============================================================================

    // Age-based amplitude decay: young crust has strong faults, old crust smooths out
    float AgeFactor = exp(-CrustAge / 50.0); // τ = 50 My decay constant
    float FaultAmplitude = 150.0 * AgeFactor; // 150m max for young ridges

    // Transform faults are perpendicular to ridge direction
    float3 FaultDir = normalize(cross(RidgeDir, normalize(Position)));

    // Gabor noise approximation using Perlin (directional)
    float GaborNoise = PerlinNoise3D_Directional(Position * 0.05, FaultDir);
    GaborNoise = clamp(GaborNoise * 3.0, -1.0, 1.0); // Scale to full range

    float FaultDetail = FaultAmplitude * GaborNoise;

    // ============================================================================
    // HIGH-FREQUENCY GRADIENT NOISE (fine underwater detail)
    // ============================================================================

    float GradientNoise = 0.0;
    float Frequency = 0.1;
    float Amplitude = 1.0;

    // 4 octaves of Perlin noise
    for (int Octave = 0; Octave < 4; ++Octave)
    {
        GradientNoise += PerlinNoise3D(Position * Frequency) * Amplitude;
        Frequency *= 2.0;
        Amplitude *= 0.5;
    }

    float FineDetail = GradientNoise * 50.0; // 50m amplitude

    // ============================================================================
    // OUTPUT
    // ============================================================================

    VertexAmplifiedElevation[VertexIdx] = BaseElevation + FaultDetail + FineDetail;
}
```

### Task 2.2: C++ Dispatch Wrapper
**File**: `Private/OceanicAmplificationGPU.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"

/**
 * Compute shader for oceanic amplification (Gabor noise + Perlin detail)
 * GPU-accelerated version of OceanicAmplification.cpp for LOD Level 7+
 */
class FOceanicAmplificationCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FOceanicAmplificationCS);
    SHADER_USE_PARAMETER_STRUCT(FOceanicAmplificationCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_SRV(StructuredBuffer<FVector3f>, RenderVertices)
        SHADER_PARAMETER_SRV(StructuredBuffer<int32>, VertexPlateAssignments)
        SHADER_PARAMETER_SRV(StructuredBuffer<float>, VertexElevationValues)
        SHADER_PARAMETER_SRV(StructuredBuffer<float>, VertexCrustAge)
        SHADER_PARAMETER_SRV(StructuredBuffer<FVector3f>, VertexRidgeDirections)
        SHADER_PARAMETER_SRV(StructuredBuffer<uint8>, PlatesCrustType)
        SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, VertexAmplifiedElevation)
        SHADER_PARAMETER(uint32, VertexCount)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 256);
    }
};
```

**File**: `Private/OceanicAmplificationGPU.cpp`

```cpp
#include "OceanicAmplificationGPU.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"
#include "RHIStaticStates.h"

IMPLEMENT_GLOBAL_SHADER(FOceanicAmplificationCS, "/Plugin/PlanetaryCreation/Private/OceanicAmplification.usf", "OceanicAmplificationCS", SF_Compute);

/**
 * GPU-accelerated oceanic amplification dispatch
 * Called from TectonicSimulationService when VertexCount >= 100,000
 */
void DispatchOceanicAmplificationGPU(
    const TArray<FVector3d>& RenderVertices,
    const TArray<int32>& VertexPlateAssignments,
    const TArray<double>& VertexElevationValues,
    const TArray<double>& VertexCrustAge,
    const TArray<FVector3d>& VertexRidgeDirections,
    const TArray<ECrustType>& PlatesCrustType,
    TArray<double>& OutVertexAmplifiedElevation)
{
    check(IsInGameThread());

    const int32 VertexCount = RenderVertices.Num();

    ENQUEUE_RENDER_COMMAND(OceanicAmplificationGPU)(
        [VertexCount,
         RenderVertices,
         VertexPlateAssignments,
         VertexElevationValues,
         VertexCrustAge,
         VertexRidgeDirections,
         PlatesCrustType,
         &OutVertexAmplifiedElevation](FRHICommandListImmediate& RHICmdList)
    {
        FRDGBuilder GraphBuilder(RHICmdList);

        // Convert double→float for GPU (precision trade-off)
        TArray<FVector3f> RenderVertices_f;
        TArray<float> VertexElevationValues_f;
        TArray<float> VertexCrustAge_f;
        TArray<FVector3f> VertexRidgeDirections_f;
        TArray<uint8> PlatesCrustType_u8;

        RenderVertices_f.SetNumUninitialized(VertexCount);
        VertexElevationValues_f.SetNumUninitialized(VertexCount);
        VertexCrustAge_f.SetNumUninitialized(VertexCount);
        VertexRidgeDirections_f.SetNumUninitialized(VertexCount);
        PlatesCrustType_u8.SetNumUninitialized(PlatesCrustType.Num());

        for (int32 i = 0; i < VertexCount; ++i)
        {
            RenderVertices_f[i] = FVector3f(RenderVertices[i]);
            VertexElevationValues_f[i] = static_cast<float>(VertexElevationValues[i]);
            VertexCrustAge_f[i] = static_cast<float>(VertexCrustAge[i]);
            VertexRidgeDirections_f[i] = FVector3f(VertexRidgeDirections[i]);
        }

        for (int32 i = 0; i < PlatesCrustType.Num(); ++i)
        {
            PlatesCrustType_u8[i] = static_cast<uint8>(PlatesCrustType[i]);
        }

        // Create RDG buffers
        FRDGBufferRef RenderVerticesBuffer = CreateUploadBuffer(GraphBuilder, TEXT("RenderVertices"),
            sizeof(FVector3f), VertexCount, RenderVertices_f.GetData(), sizeof(FVector3f) * VertexCount);

        FRDGBufferRef VertexPlateAssignmentsBuffer = CreateUploadBuffer(GraphBuilder, TEXT("VertexPlateAssignments"),
            sizeof(int32), VertexCount, VertexPlateAssignments.GetData(), sizeof(int32) * VertexCount);

        FRDGBufferRef VertexElevationValuesBuffer = CreateUploadBuffer(GraphBuilder, TEXT("VertexElevationValues"),
            sizeof(float), VertexCount, VertexElevationValues_f.GetData(), sizeof(float) * VertexCount);

        FRDGBufferRef VertexCrustAgeBuffer = CreateUploadBuffer(GraphBuilder, TEXT("VertexCrustAge"),
            sizeof(float), VertexCount, VertexCrustAge_f.GetData(), sizeof(float) * VertexCount);

        FRDGBufferRef VertexRidgeDirectionsBuffer = CreateUploadBuffer(GraphBuilder, TEXT("VertexRidgeDirections"),
            sizeof(FVector3f), VertexCount, VertexRidgeDirections_f.GetData(), sizeof(FVector3f) * VertexCount);

        FRDGBufferRef PlatesCrustTypeBuffer = CreateUploadBuffer(GraphBuilder, TEXT("PlatesCrustType"),
            sizeof(uint8), PlatesCrustType_u8.Num(), PlatesCrustType_u8.GetData(), sizeof(uint8) * PlatesCrustType_u8.Num());

        // Output buffer
        FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(
            FRDGBufferDesc::CreateStructuredDesc(sizeof(float), VertexCount),
            TEXT("VertexAmplifiedElevation"));

        // Dispatch compute shader
        FOceanicAmplificationCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FOceanicAmplificationCS::FParameters>();
        PassParameters->RenderVertices = GraphBuilder.CreateSRV(RenderVerticesBuffer);
        PassParameters->VertexPlateAssignments = GraphBuilder.CreateSRV(VertexPlateAssignmentsBuffer);
        PassParameters->VertexElevationValues = GraphBuilder.CreateSRV(VertexElevationValuesBuffer);
        PassParameters->VertexCrustAge = GraphBuilder.CreateSRV(VertexCrustAgeBuffer);
        PassParameters->VertexRidgeDirections = GraphBuilder.CreateSRV(VertexRidgeDirectionsBuffer);
        PassParameters->PlatesCrustType = GraphBuilder.CreateSRV(PlatesCrustTypeBuffer);
        PassParameters->VertexAmplifiedElevation = GraphBuilder.CreateUAV(OutputBuffer);
        PassParameters->VertexCount = VertexCount;

        TShaderMapRef<FOceanicAmplificationCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("OceanicAmplification"),
            ComputeShader,
            PassParameters,
            FIntVector(FMath::DivideAndRoundUp(VertexCount, 256), 1, 1));

        // Readback results
        FRDGBufferRef ReadbackBuffer = GraphBuilder.CreateBuffer(
            FRDGBufferDesc::CreateStructuredDesc(sizeof(float), VertexCount),
            TEXT("AmplifiedElevationReadback"));

        AddCopyBufferPass(GraphBuilder, ReadbackBuffer, OutputBuffer);

        GraphBuilder.Execute();

        // Convert float→double for CPU
        TArray<float> ResultsFloat;
        ResultsFloat.SetNumUninitialized(VertexCount);

        // Synchronous readback (editor tool, acceptable)
        void* MappedData = RHICmdList.LockBuffer(ReadbackBuffer->GetRHI(), 0, sizeof(float) * VertexCount, RLM_ReadOnly);
        FMemory::Memcpy(ResultsFloat.GetData(), MappedData, sizeof(float) * VertexCount);
        RHICmdList.UnlockBuffer(ReadbackBuffer->GetRHI());

        OutVertexAmplifiedElevation.SetNumUninitialized(VertexCount);
        for (int32 i = 0; i < VertexCount; ++i)
        {
            OutVertexAmplifiedElevation[i] = static_cast<double>(ResultsFloat[i]);
        }
    });

    // Flush commands to ensure completion before returning
    FlushRenderingCommands();
}
```

### Task 2.3: Perlin Noise GPU Utility
**File**: `Shaders/Private/TectonicCommon.ush`

```hlsl
#pragma once

#include "/Engine/Private/Random.ush"

/**
 * 3D Perlin noise implementation for GPU
 * Uses engine's Random.ush as foundation
 * Returns value in [-1, 1] range
 */
float PerlinNoise3D(float3 Position)
{
    // Classic Perlin noise using gradient vectors
    // Grid cell coordinates
    int3 GridMin = floor(Position);
    float3 Frac = frac(Position);

    // Smoothstep interpolation (5th order Hermite)
    float3 Smooth = Frac * Frac * Frac * (Frac * (Frac * 6.0 - 15.0) + 10.0);

    // Sample 8 corners of cube
    float Gradients[8];
    for (int i = 0; i < 8; ++i)
    {
        int3 Corner = GridMin + int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);

        // Pseudo-random gradient vector using engine hash
        uint Seed = Corner.x * 1619 + Corner.y * 31337 + Corner.z * 6971;
        float3 Gradient = normalize(float3(
            PseudoRandom(Seed) * 2.0 - 1.0,
            PseudoRandom(Seed + 1) * 2.0 - 1.0,
            PseudoRandom(Seed + 2) * 2.0 - 1.0
        ));

        // Dot product with distance vector
        float3 CornerPos = float3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        float3 Delta = Frac - CornerPos;
        Gradients[i] = dot(Gradient, Delta);
    }

    // Trilinear interpolation
    float x0 = lerp(Gradients[0], Gradients[1], Smooth.x);
    float x1 = lerp(Gradients[2], Gradients[3], Smooth.x);
    float x2 = lerp(Gradients[4], Gradients[5], Smooth.x);
    float x3 = lerp(Gradients[6], Gradients[7], Smooth.x);

    float y0 = lerp(x0, x1, Smooth.y);
    float y1 = lerp(x2, x3, Smooth.y);

    return lerp(y0, y1, Smooth.z);
}

/**
 * Directional Perlin noise for Gabor approximation
 * Emphasizes features along OrientationDir
 */
float PerlinNoise3D_Directional(float3 Position, float3 OrientationDir)
{
    // Sample at two offset points along fault direction
    float3 SamplePoint1 = Position;
    float3 SamplePoint2 = Position + OrientationDir * 2.0;

    float Noise1 = PerlinNoise3D(SamplePoint1);
    float Noise2 = PerlinNoise3D(SamplePoint2);

    // Take maximum absolute value to create strong linear features
    return abs(Noise1) > abs(Noise2) ? Noise1 : Noise2;
}
```

---

## 🏔️ **Phase 3: Continental Amplification GPU** (Week 2-3)

### Task 3.1: Exemplar Texture Atlas
**File**: `Private/ContinentalAmplificationGPU.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderGraphResources.h"

/**
 * GPU texture atlas for 22 SRTM90 exemplar heightmaps
 * Texture2DArray with 22 layers (one per exemplar)
 * Format: R16_UNORM (16-bit grayscale, [0, 65535])
 */
class FExemplarTextureAtlas
{
public:
    /** Create atlas from exemplar library JSON */
    void Initialize(const FString& ProjectContentDir);

    /** Get RDG texture reference for rendering */
    FRDGTextureRef GetAtlasTexture(FRDGBuilder& GraphBuilder) const;

    /** Get metadata buffer (elevation ranges per layer) */
    FRDGBufferRef GetMetadataBuffer(FRDGBuilder& GraphBuilder) const;

    /** Release GPU resources */
    void Release();

private:
    TRefCountPtr<FRHITexture2DArray> AtlasTexture;
    TArray<FExemplarMetadata> Metadata; // 22 entries

    struct FExemplarMetadata
    {
        float ElevationMin_m;
        float ElevationMax_m;
        uint32 Region; // 0=Ancient, 1=Andean, 2=Himalayan
    };
};
```

**File**: `Private/ContinentalAmplificationGPU.cpp` (atlas loading)

```cpp
void FExemplarTextureAtlas::Initialize(const FString& ProjectContentDir)
{
    // Load ExemplarLibrary.json (reuse existing code from ContinentalAmplification.cpp)
    TArray<FExemplarMetadata> ExemplarLibrary;
    LoadExemplarLibraryJSON(ProjectContentDir, ExemplarLibrary);

    const int32 NumExemplars = ExemplarLibrary.Num();
    check(NumExemplars > 0);

    // Determine atlas dimensions (max width/height across all exemplars)
    int32 AtlasWidth = 0;
    int32 AtlasHeight = 0;
    for (const FExemplarMetadata& Exemplar : ExemplarLibrary)
    {
        AtlasWidth = FMath::Max(AtlasWidth, Exemplar.Width_px);
        AtlasHeight = FMath::Max(AtlasHeight, Exemplar.Height_px);
    }

    // Create Texture2DArray on GPU
    FRHIResourceCreateInfo CreateInfo(TEXT("ExemplarAtlas"));
    AtlasTexture = RHICreateTexture2DArray(
        AtlasWidth,
        AtlasHeight,
        NumExemplars,
        PF_R16_UINT, // 16-bit unsigned int for [0, 65535]
        1, // Mips
        1, // NumSamples
        TexCreate_ShaderResource,
        CreateInfo);

    // Upload each exemplar as a texture slice
    for (int32 LayerIdx = 0; LayerIdx < NumExemplars; ++LayerIdx)
    {
        FExemplarMetadata& Exemplar = ExemplarLibrary[LayerIdx];

        // Load PNG16 data (reuse existing LoadExemplarPNG16 function)
        LoadExemplarPNG16(Exemplar, ProjectContentDir);

        // Upload to GPU
        uint32 DestStride;
        uint16* MappedData = (uint16*)RHILockTexture2DArray(
            AtlasTexture,
            LayerIdx,
            0, // Mip level
            RLM_WriteOnly,
            DestStride,
            false);

        FMemory::Memcpy(MappedData, Exemplar.HeightData.GetData(),
            sizeof(uint16) * Exemplar.Width_px * Exemplar.Height_px);

        RHIUnlockTexture2DArray(AtlasTexture, LayerIdx, 0, false);

        // Store metadata for shader access
        FExemplarMetadata Meta;
        Meta.ElevationMin_m = Exemplar.ElevationMin_m;
        Meta.ElevationMax_m = Exemplar.ElevationMax_m;
        Meta.Region = Exemplar.Region == TEXT("Himalayan") ? 2 :
                     (Exemplar.Region == TEXT("Andean") ? 1 : 0);
        Metadata.Add(Meta);
    }
}
```

### Task 3.2: Shader Implementation
**File**: `Shaders/Private/ContinentalAmplification.usf`

```hlsl
#include "/Engine/Private/Common.ush"
#include "/Plugin/PlanetaryCreation/Private/TectonicCommon.ush"

// Parameters
StructuredBuffer<float3> RenderVertices;
StructuredBuffer<int> VertexPlateAssignments;
StructuredBuffer<float> VertexCrustAge;
StructuredBuffer<uint> PlatesCrustType;
RWStructuredBuffer<float> VertexAmplifiedElevation;

Texture2DArray<float> ExemplarAtlas;
SamplerState ExemplarSampler;

struct FExemplarMetadata
{
    float ElevationMin_m;
    float ElevationMax_m;
    uint Region; // 0=Ancient, 1=Andean, 2=Himalayan
};
StructuredBuffer<FExemplarMetadata> ExemplarMetadata;

uint VertexCount;
uint NumExemplars;

/**
 * Classify terrain type based on paper Section 5 criteria
 * 0=Plain, 1=OldMountains, 2=AndeanMountains, 3=HimalayanMountains
 */
uint ClassifyTerrainType(uint VertexIdx, float BaseElevation, float CrustAge)
{
    // Simplified classification (no boundary proximity checks on GPU)

    // Old orogeny (>100 My) → Old Mountains
    if (CrustAge > 100.0)
        return 1;

    // High elevation + young crust → Himalayan (conservative)
    if (BaseElevation > 1000.0 && CrustAge < 100.0)
        return 3;

    // Medium elevation → Andean
    if (BaseElevation > 500.0)
        return 2;

    // Default: Plain
    return 0;
}

/**
 * Select exemplar layer based on terrain type and pseudo-random hash
 */
uint SelectExemplarLayer(uint TerrainType, uint VertexIdx)
{
    // Filter exemplars by region
    uint TargetRegion;
    if (TerrainType == 3)      TargetRegion = 2; // Himalayan
    else if (TerrainType == 2) TargetRegion = 1; // Andean
    else                       TargetRegion = 0; // Ancient

    // Find first exemplar matching region (simplified, no random selection)
    for (uint LayerIdx = 0; LayerIdx < NumExemplars; ++LayerIdx)
    {
        if (ExemplarMetadata[LayerIdx].Region == TargetRegion)
            return LayerIdx;
    }

    return 0; // Fallback
}

/**
 * Convert sphere position to UV coordinates (equirectangular projection)
 */
float2 SphericalToUV(float3 Position)
{
    float3 NormPos = normalize(Position);
    float U = 0.5 + atan2(NormPos.z, NormPos.x) / (2.0 * PI);
    float V = 0.5 - asin(NormPos.y) / PI;
    return float2(U, V);
}

[numthreads(256, 1, 1)]
void ContinentalAmplificationCS(uint3 ThreadID : SV_DispatchThreadID)
{
    uint VertexIdx = ThreadID.x;
    if (VertexIdx >= VertexCount)
        return;

    // Skip oceanic vertices
    int PlateID = VertexPlateAssignments[VertexIdx];
    if (PlatesCrustType[PlateID] == 0) // Oceanic
        return;

    float3 Position = RenderVertices[VertexIdx];
    float BaseElevation = VertexAmplifiedElevation[VertexIdx]; // Use oceanic-amplified elevation
    float CrustAge = VertexCrustAge[VertexIdx];

    // Classify terrain type
    uint TerrainType = ClassifyTerrainType(VertexIdx, BaseElevation, CrustAge);

    // Select exemplar
    uint ExemplarLayer = SelectExemplarLayer(TerrainType, VertexIdx);

    // Sample heightmap with bilinear filtering (hardware interpolation!)
    float2 UV = SphericalToUV(Position);
    float RawHeight = ExemplarAtlas.SampleLevel(ExemplarSampler, float3(UV, ExemplarLayer), 0).r;

    // Remap from [0,1] to [ElevationMin, ElevationMax]
    FExemplarMetadata Meta = ExemplarMetadata[ExemplarLayer];
    float SampledElevation = Meta.ElevationMin_m + RawHeight * (Meta.ElevationMax_m - Meta.ElevationMin_m);

    // Blend with base elevation (simplified: 50% blend for orogens, 0% for plains)
    float BlendWeight = (TerrainType > 0) ? 0.5 : 0.0;
    VertexAmplifiedElevation[VertexIdx] = lerp(BaseElevation, SampledElevation, BlendWeight);
}
```

---

## ⚡ **Phase 4: Optimization & Fallback** (Week 3-4)

### Task 4.1: Adaptive GPU Dispatch
**File**: `TectonicSimulationService.h`

Add declarations:
```cpp
public:
    /** GPU-accelerated amplification (LOD 7+) */
    void ApplyOceanicAmplification_GPU();
    void ApplyContinentalAmplification_GPU();

private:
    /** Adaptive dispatch: GPU for L7+, CPU for L6- */
    bool ShouldUseGPUAmplification() const;
```

**File**: `TectonicSimulationService.cpp`

```cpp
bool UTectonicSimulationService::ShouldUseGPUAmplification() const
{
    // GPU for L7+ (163K+ vertices), CPU for L6 and below
    const int32 VertexCount = RenderVertices.Num();
    return (VertexCount >= 100000 && GRHISupportsComputeShaders);
}

void UTectonicSimulationService::ApplyOceanicAmplification()
{
    if (ShouldUseGPUAmplification())
    {
        ApplyOceanicAmplification_GPU();
    }
    else
    {
        ApplyOceanicAmplification_CPU(); // Existing implementation
    }
}

void UTectonicSimulationService::ApplyOceanicAmplification_GPU()
{
    DispatchOceanicAmplificationGPU(
        RenderVertices,
        VertexPlateAssignments,
        VertexElevationValues,
        VertexCrustAge,
        VertexRidgeDirections,
        Plates, // Extract CrustType array
        VertexAmplifiedElevation // Output
    );

    SurfaceDataVersion++;
}

void UTectonicSimulationService::ApplyContinentalAmplification_GPU()
{
    // Initialize atlas on first use (lazy load)
    static FExemplarTextureAtlas Atlas;
    if (!Atlas.IsValid())
    {
        Atlas.Initialize(FPaths::ProjectContentDir());
    }

    DispatchContinentalAmplificationGPU(
        RenderVertices,
        VertexPlateAssignments,
        VertexCrustAge,
        Plates,
        Atlas,
        VertexAmplifiedElevation // In/Out
    );

    SurfaceDataVersion++;
}
```

### Task 4.2: Performance Profiling
**Console Commands**:
```
stat GPU           # GPU timing breakdown
profilegpu         # Detailed RDG pass analysis
stat unit          # CPU/GPU/render frame times
```

**Unreal Insights Trace**:
```cpp
// Add to AdvanceSteps()
TRACE_CPUPROFILER_EVENT_SCOPE(TectonicSimulation_AdvanceSteps);

// Add to GPU dispatch
RDG_GPU_STAT_SCOPE(GraphBuilder, OceanicAmplification);
```

**Performance Targets** (LOD Level 6 @ 40,962 vertices):
- Oceanic CS: <1ms
- Continental CS: <2ms
- Upload/Readback: <1ms
- **Total GPU**: <5ms (vs 20ms CPU baseline)

---

## ✅ **Phase 5: Testing & Validation** (Week 4)

### Task 5.1: Automation Tests

**File**: `Tests/OceanicAmplificationGPUTest.cpp`

```cpp
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "OceanicAmplificationGPU.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOceanicAmplificationGPUTest,
    "PlanetaryCreation.Milestone6.OceanicAmplificationGPU",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOceanicAmplificationGPUTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("Service exists"), Service);

    // Setup: Enable oceanic amplification, L7 mesh (163K vertices)
    FTectonicSimulationParameters Params;
    Params.RenderSubdivisionLevel = 7; // 163,842 vertices
    Params.bEnableOceanicAmplification = true;
    Service->SetParameters(Params);
    Service->AdvanceSteps(5);

    // Capture CPU baseline
    TArray<double> CPUResults = Service->GetVertexAmplifiedElevation();

    // Run GPU path
    Service->ApplyOceanicAmplification_GPU();
    TArray<double> GPUResults = Service->GetVertexAmplifiedElevation();

    // Validate: GPU vs CPU error <0.1m
    double MaxError = 0.0;
    for (int32 i = 0; i < CPUResults.Num(); ++i)
    {
        double Error = FMath::Abs(CPUResults[i] - GPUResults[i]);
        MaxError = FMath::Max(MaxError, Error);
    }

    TestTrue(TEXT("GPU matches CPU within 0.1m tolerance"), MaxError < 0.1);

    // Performance check: GPU faster than CPU
    const double StartTime = FPlatformTime::Seconds();
    Service->ApplyOceanicAmplification_GPU();
    const double GPUTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;

    TestTrue(TEXT("GPU execution <5ms at L7"), GPUTime < 5.0);

    return true;
}
```

**File**: `Tests/ContinentalAmplificationGPUTest.cpp`

Similar structure, validate:
- Exemplar atlas integrity (22 layers loaded)
- Bilinear sampling accuracy vs CPU
- Terrain classification matches CPU

**File**: `Tests/GPUAmplificationIntegrationTest.cpp`

```cpp
// Full simulation: CPU vs GPU path for 100 steps
// Validate final heightmap identical within 1m tolerance
// LOD Level 7 stress test: 163K vertices, <20ms/step
```

### Task 5.2: Cross-Platform Validation
**Platforms**: Windows (DX12), Linux (Vulkan)
- Same seed → same heightmap output
- Performance parity across RHIs
- Shader compilation success on all platforms

---

## 📈 **Expected Outcomes**

| Metric | Before (CPU) | After (GPU) | Improvement |
|--------|--------------|-------------|-------------|
| L6 Step Time (40K vertices) | ~50ms | ~12ms | **4× faster** |
| L7 Step Time (163K vertices) | ~180ms | ~25ms | **7× faster** |
| L8 Step Time (655K vertices) | ~650ms | ~60ms | **10× faster** |
| Memory Usage | +0MB | +150MB (textures) | Acceptable |
| Editor Responsiveness | Freezes at L7 | Smooth at L8 | **Major UX win** |

---

## 🚧 **Risks & Mitigations**

| Risk | Impact | Mitigation |
|------|--------|------------|
| RDG API complexity | High learning curve | Study Epic's PostProcess shaders, community examples |
| Async readback latency | 1-frame delay in elevation | Acceptable for editor tool (not real-time game) |
| Exemplar atlas VRAM | 150MB GPU memory | Lazy load, release after use |
| Shader compilation time | Slow first load | Pre-compile shaders in build process |
| Cross-platform RHI bugs | Shader errors on Vulkan/Metal | Extensive testing, fallback to CPU |
| Double→Float precision loss | Numerical drift | Validate max error <0.1m, acceptable for visualization |

---

## 📂 **Deliverables Checklist**

- [ ] `PlanetaryCreationEditor.Build.cs` updated with RenderCore deps
- [ ] `Shaders/Private/TectonicCommon.ush` (Perlin noise utilities)
- [ ] `Shaders/Private/OceanicAmplification.usf` (200 lines HLSL)
- [ ] `Shaders/Private/ContinentalAmplification.usf` (250 lines HLSL)
- [ ] `Private/OceanicAmplificationGPU.h` (Shader interface)
- [ ] `Private/OceanicAmplificationGPU.cpp` (RDG dispatch)
- [ ] `Private/ContinentalAmplificationGPU.h` (Atlas + shader interface)
- [ ] `Private/ContinentalAmplificationGPU.cpp` (Texture atlas + dispatch)
- [ ] `TectonicSimulationService.cpp` adaptive dispatch logic
- [ ] `Tests/OceanicAmplificationGPUTest.cpp`
- [ ] `Tests/ContinentalAmplificationGPUTest.cpp`
- [ ] `Tests/GPUAmplificationIntegrationTest.cpp`
- [ ] Performance report: L6/L7/L8 benchmarks
- [ ] Documentation update: `Docs/Milestone6_Plan.md` GPU section

---

## 🔗 **References**

- [Unreal RDG Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine)
- [Compute Shader Tutorial (Valentin Kraft)](http://www.valentinkraft.de/compute-shader-in-unreal-tutorial/)
- [timdecode/UnrealComputeShaderExample](https://github.com/timdecode/UnrealComputeShaderExample)
- UE5 Engine Source: `Engine/Shaders/Private/PostProcessBloom.usf` (reference compute shader)
- Paper: "Procedural Tectonic Planets" Section 5 (Stage B Amplification)

---

**Estimated Total Effort**: 3-4 weeks (1 engineer full-time)
**Risk Level**: Medium (RDG API complexity, cross-platform testing)
**Reward**: 10× performance improvement for L8, enables real-time editor interaction at production LODs
