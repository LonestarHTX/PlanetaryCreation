# RealtimeMeshComponent-Pro How-To Guide

## Table of Contents
1. [Introduction](#introduction)
2. [Installation](#installation)
3. [Core Concepts](#core-concepts)
4. [Basic Usage](#basic-usage)
5. [Advanced Features](#advanced-features)
6. [Working with LODs](#working-with-lods)
7. [Performance Tips](#performance-tips)
8. [Migration from ProceduralMeshComponent](#migration-from-proceduralmeshcomponent)

## Introduction

RealtimeMeshComponent (RMC) is a powerful replacement for Unreal Engine's ProceduralMeshComponent (PMC). It offers significantly better performance, more features, and a flexible architecture suitable for everything from simple runtime model loading to complex procedural world generation.

### Key Features
- High-performance mesh rendering
- Support for multiple LODs
- Efficient mesh updates
- Multiple UV channels support
- Polygroups for multi-material meshes
- Collision support
- Blueprint-friendly API

## Installation

1. Copy the `RealtimeMeshComponent-Pro` folder to your project's `Plugins` directory
2. Regenerate project files
3. Compile your project
4. Enable the plugin in your project settings

## Core Concepts

### 1. RealtimeMesh
The main mesh object that holds all mesh data and configuration.

### 2. StreamSet
Contains all the vertex and index buffers for your mesh data:
- **Position Stream**: Vertex positions
- **Tangent Stream**: Normals and tangents
- **TexCoord Stream**: UV coordinates
- **Color Stream**: Vertex colors
- **Triangle Stream**: Triangle indices
- **PolyGroup Stream**: Material indices per triangle

### 3. Section Groups and Sections
- **Section Group**: Contains shared vertex/index buffers
- **Section**: Defines a renderable part of the mesh with specific material and draw settings

### 4. Keys
- **FRealtimeMeshSectionGroupKey**: Unique identifier for a section group
- **FRealtimeMeshSectionKey**: Unique identifier for a section within a group

## Basic Usage

### Creating a Simple Triangle

Here's the simplest example from `RealtimeMeshBasic.cpp`:

```cpp
#include "RealtimeMeshActor.h"
#include "RealtimeMeshSimple.h"

class AMyRealtimeMeshActor : public ARealtimeMeshActor
{
    void OnConstruction(const FTransform& Transform) override
    {
        Super::OnConstruction(Transform);
        
        // Initialize the mesh
        URealtimeMeshSimple* RealtimeMesh = GetRealtimeMeshComponent()->InitializeRealtimeMesh<URealtimeMeshSimple>();
        
        // Create a stream set for mesh data
        FRealtimeMeshStreamSet StreamSet;
        
        // Use a builder for convenience
        TRealtimeMeshBuilderLocal<uint16, FPackedNormal, FVector2DHalf, 1> Builder(StreamSet);
        
        // Enable features
        Builder.EnableTangents();
        Builder.EnableTexCoords();
        Builder.EnableColors();
        Builder.EnablePolyGroups();
        
        // Add vertices
        int32 V0 = Builder.AddVertex(FVector3f(-50.0f, 0.0f, 0.0f))
            .SetNormalAndTangent(FVector3f(0.0f, -1.0f, 0.0f), FVector3f(1.0f, 0.0f, 0.0f))
            .SetColor(FColor::Red)
            .SetTexCoord(FVector2f(0.0f, 0.0f));
            
        int32 V1 = Builder.AddVertex(FVector3f(0.0f, 0.0f, 100.0f))
            .SetNormalAndTangent(FVector3f(0.0f, -1.0f, 0.0f), FVector3f(1.0f, 0.0f, 0.0f))
            .SetColor(FColor::Green)
            .SetTexCoord(FVector2f(0.5f, 1.0f));
            
        int32 V2 = Builder.AddVertex(FVector3f(50.0, 0.0, 0.0))
            .SetNormalAndTangent(FVector3f(0.0f, -1.0f, 0.0f), FVector3f(1.0f, 0.0f, 0.0f))
            .SetColor(FColor::Blue)
            .SetTexCoord(FVector2f(1.0f, 0.0f));
        
        // Add triangles with polygroup indices
        Builder.AddTriangle(V0, V1, V2, 0);  // Front face
        Builder.AddTriangle(V2, V1, V0, 1);  // Back face
        
        // Setup materials
        RealtimeMesh->SetupMaterialSlot(0, "PrimaryMaterial");
        RealtimeMesh->SetupMaterialSlot(1, "SecondaryMaterial");
        
        // Create section group and sections
        const FRealtimeMeshSectionGroupKey GroupKey = FRealtimeMeshSectionGroupKey::Create(0, FName("TestTriangle"));
        const FRealtimeMeshSectionKey PolyGroup0SectionKey = FRealtimeMeshSectionKey::CreateForPolyGroup(GroupKey, 0);
        const FRealtimeMeshSectionKey PolyGroup1SectionKey = FRealtimeMeshSectionKey::CreateForPolyGroup(GroupKey, 1);
        
        // Create the section group (this automatically creates sections for polygroups)
        RealtimeMesh->CreateSectionGroup(GroupKey, StreamSet);
        
        // Configure sections
        RealtimeMesh->UpdateSectionConfig(PolyGroup0SectionKey, FRealtimeMeshSectionConfig(0));
        RealtimeMesh->UpdateSectionConfig(PolyGroup1SectionKey, FRealtimeMeshSectionConfig(1));
    }
};
```

### Manual Component Setup

If you need more control, you can manually create the component (from `RealtimeMeshDirect.cpp`):

```cpp
AMyActor::AMyActor()
{
    // Create the component
    RealtimeMeshComponent = CreateDefaultSubobject<URealtimeMeshComponent>(TEXT("RealtimeMeshComponent"));
    RealtimeMeshComponent->SetMobility(EComponentMobility::Movable);
    RealtimeMeshComponent->SetGenerateOverlapEvents(false);
    RealtimeMeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
    SetRootComponent(RealtimeMeshComponent);
}
```

### Updating Topology at Runtime

RMC lets you replace the geometry of a section group without tearing down the component. The stress-test actor in `RealtimeMeshTests` demonstrates the pattern:

1. Build a fresh `FRealtimeMeshStreamSet` containing the new vertex and index data.
2. Call `UpdateSectionGroup(GroupKey, MoveTemp(StreamSet))` to swap the buffers in place.
3. Recompute `FRealtimeMeshStreamRange` values for each section that belongs to the group and pass them to `UpdateSectionRange`.

This approach supports plate rifting or other topology changes because the vertex count can shrink or grow each update. Collision data will refresh asynchronously after the visual buffers finish uploading, so expect a short delay before physics reflects the new shape.

## Advanced Features

### 1. Stream-by-Stream Building

For more control over data types and precision (from `RealtimeMeshByStreamBuilder.cpp`):

```cpp
FRealtimeMeshStreamSet StreamSet;

// Create individual stream builders with specific types
TRealtimeMeshStreamBuilder<FVector3f> PositionBuilder(
    StreamSet.AddStream(FRealtimeMeshStreams::Position, GetRealtimeMeshBufferLayout<FVector3f>()));

// Use packed normals for memory efficiency
TRealtimeMeshStreamBuilder<FRealtimeMeshTangentsHighPrecision, FRealtimeMeshTangentsNormalPrecision> TangentBuilder(
    StreamSet.AddStream(FRealtimeMeshStreams::Tangents, GetRealtimeMeshBufferLayout<FRealtimeMeshTangentsNormalPrecision>()));

// Use half-precision UVs
TRealtimeMeshStreamBuilder<FVector2f, FVector2DHalf> TexCoordsBuilder(
    StreamSet.AddStream(FRealtimeMeshStreams::TexCoords, GetRealtimeMeshBufferLayout<FVector2DHalf>()));

// Add data
int32 V0 = PositionBuilder.Add(FVector3f(-50.0f, 0.0f, 0.0f));
TangentBuilder.Add(FRealtimeMeshTangentsHighPrecision(FVector3f(0.0f, -1.0f, 0.0f), FVector3f(1.0f, 0.0f, 0.0f)));
TexCoordsBuilder.Add(FVector2f(0.0f, 0.0f));
```

### 2. Multiple UV Channels

Support for multiple texture coordinate sets (from `RealtimeMeshMultipleUVs.cpp`):

```cpp
// Create a builder with 2 UV channels
TRealtimeMeshBuilderLocal<uint16, FPackedNormal, FVector2DHalf, 2> Builder(StreamSet);

// Set UVs for both channels
int32 V0 = Builder.AddVertex(FVector3f(-50.0f, 0.0f, 0.0f))
    .SetTexCoord(FVector2f(0.0f, 0.0f))        // Channel 0
    .SetTexCoord(1, FVector2f(0.25f, 0.25f));  // Channel 1
```

### 3. Using Built-in Shape Generators

RMC provides utility functions for common shapes:

```cpp
FRealtimeMeshStreamSet StreamSet;
FTransform3f Transform = FTransform3f::Identity;

// Create a box mesh
URealtimeMeshBasicShapeTools::AppendBoxMesh(
    StreamSet, 
    FVector3f(100.0f, 100.0f, 100.0f),  // Box radius
    Transform,                           // Transform
    0,                                  // Material group
    FColor::White                       // Vertex color
);

// Create the mesh with the generated data
RealtimeMesh->CreateSectionGroup(GroupKey, StreamSet);
```

## Working with LODs

RMC supports multiple LODs with automatic switching based on screen size. The LOD system uses screen height percentage to determine when to switch between LODs.

### LOD Configuration Structure

```cpp
// FRealtimeMeshLODConfig structure
struct FRealtimeMeshLODConfig
{
    bool bIsVisible;     // Whether this LOD is visible
    float ScreenSize;    // Screen height percentage (0.0 - 1.0)
    
    FRealtimeMeshLODConfig(float InScreenSize = 0.0f)
        : bIsVisible(true)
        , ScreenSize(InScreenSize)
    {
    }
};
```

### Setting Up Multiple LODs

```cpp
void SetupLODs()
{
    URealtimeMeshSimple* RealtimeMesh = GetRealtimeMeshComponent()->InitializeRealtimeMesh<URealtimeMeshSimple>();
    
    // Configure LOD 0 (already exists by default)
    // 0.75 means this LOD shows when mesh takes up 75% or more of screen height
    RealtimeMesh->UpdateLODConfig(0, FRealtimeMeshLODConfig(0.75));
    
    // Add additional LODs
    for (int32 LODIndex = 1; LODIndex < 4; LODIndex++)
    {
        // Add LOD with screen size threshold
        // Each subsequent LOD has half the screen size of the previous
        RealtimeMesh->AddLOD(FRealtimeMeshLODConfig(FMath::Pow(0.5f, LODIndex)));
        
        // Create different mesh data for each LOD
        FRealtimeMeshStreamSet StreamSet;
        TRealtimeMeshBuilderLocal<uint16, FPackedNormal, FVector2DHalf, 2> Builder(StreamSet);
        
        // Generate LOD-specific geometry
        FTransform3f LODTransform(FRotator3f(0.0f, 45.0f * LODIndex, 0.0f));
        URealtimeMeshBasicShapeTools::AppendBoxMesh(StreamSet, FVector3f(100.0f), LODTransform);
        
        // Create section group for this LOD
        const FRealtimeMeshSectionGroupKey GroupKey = FRealtimeMeshSectionGroupKey::Create(LODIndex, FName("LODBox"));
        RealtimeMesh->CreateSectionGroup(GroupKey, StreamSet);
    }
}
```

### Important LOD Notes

1. **LOD 0 Always Exists**: LOD 0 is created automatically when you initialize a RealtimeMesh
2. **Screen Size Values**: 
   - 1.0 = Mesh fills entire screen height
   - 0.5 = Mesh fills half screen height
   - 0.25 = Mesh fills quarter screen height
3. **LOD Index in Section Groups**: The first parameter of `FRealtimeMeshSectionGroupKey::Create()` is the LOD index
4. **Order Matters**: Configure LODs before creating their mesh data

## Performance Tips

### 1. Use Appropriate Data Types
- Use `uint16` for indices when possible (supports up to 65,536 vertices)
- Use `FPackedNormal` for normals/tangents to save memory
- Use `FVector2DHalf` for texture coordinates when precision allows

### 2. Batch Updates
- Group multiple mesh modifications together
- Use update contexts to batch render thread updates

### 3. Stream Reuse
- Reuse StreamSets when updating mesh data
- Only update changed streams, not the entire mesh

### 4. LOD Strategy
- Use aggressive LODs for distant objects
- Reduce vertex count significantly between LOD levels
- Consider using simpler shaders for distant LODs

### 5. Vertex Count Limits
- Be mindful of vertex counts, especially with procedural generation
- Use `uint32` indices for meshes with >65k vertices
- Consider clamping subdivision levels:
```cpp
// Example: Quadsphere with 6 faces
// Subdivision 6 = 64x64 grid per face = ~25k vertices total
// Subdivision 8 = 256x256 grid per face = ~393k vertices total
int32 ClampedSubdivision = FMath::Clamp(SubdivisionLevel, 0, 8);
```

### 6. Builder Type Selection
Choose your index type based on expected vertex count:
```cpp
// For smaller meshes (<65k vertices)
TRealtimeMeshBuilderLocal<uint16, FPackedNormal, FVector2DHalf, 1> Builder(StreamSet);

// For larger meshes
TRealtimeMeshBuilderLocal<uint32, FPackedNormal, FVector2DHalf, 1> Builder(StreamSet);
```

## Migration from ProceduralMeshComponent

### Key Differences

1. **Initialization**:
   ```cpp
   // PMC
   ProceduralMeshComponent->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);
   
   // RMC
   URealtimeMeshSimple* RealtimeMesh = RealtimeMeshComponent->InitializeRealtimeMesh<URealtimeMeshSimple>();
   RealtimeMesh->CreateSectionGroup(GroupKey, StreamSet);
   ```

2. **Data Organization**:
   - PMC uses separate arrays for each attribute
   - RMC uses StreamSets with builders or direct access

3. **Material Assignment**:
   - PMC: Set material on component
   - RMC: Setup material slots on mesh, assign to sections

### Migration Checklist

- [ ] Replace `UProceduralMeshComponent` with `URealtimeMeshComponent`
- [ ] Convert vertex arrays to StreamSet format
- [ ] Replace `CreateMeshSection` calls with `CreateSectionGroup`
- [ ] Update material assignment to use material slots
- [ ] Convert collision setup if using complex collision
- [ ] Test performance improvements

## Common Pitfalls and Solutions

### 1. Builder Method Chaining

**Problem**: Compiler errors when trying to set vertex attributes separately:
```cpp
// WRONG - This will cause compilation errors
int32 VertexIndex = Builder.AddVertex(Position);
Builder.SetNormalAndTangent(Normal, Tangent);  // Error!
Builder.SetTexCoord(UV);                       // Error!
```

**Why it fails**: The vertex builder returned by `AddVertex()` is non-copyable (inherits from `FNoncopyable`). This prevents storing it in a variable:
```cpp
// WRONG - Attempting to reference a deleted function
auto VertexBuilder = Builder.AddVertex(Position);  // Error: copy constructor deleted
VertexBuilder.SetNormalAndTangent(Normal, Tangent);
```

**Solution**: All vertex attributes must be chained from the `AddVertex()` call:
```cpp
// CORRECT - Chain all methods
int32 VertexIndex = Builder.AddVertex(Position)
    .SetNormalAndTangent(Normal, Tangent)
    .SetTexCoord(UV)
    .SetColor(Color);
```

### 2. Forward Declaration Issues

**Problem**: Compiler error about 'struct' vs 'class':
```cpp
// May cause C4099 error
class RealtimeMesh::TRealtimeMeshBuilderLocal<...>& Builder
```

**Solution**: Use `struct` for forward declarations:
```cpp
// Correct forward declaration
struct RealtimeMesh::TRealtimeMeshBuilderLocal<...>& Builder
```

### 3. Conditional Vertex Attributes

**Problem**: Not all vertices need all attributes (e.g., colors are optional):

**Solution**: Use conditional chaining:
```cpp
if (bGenerateVertexColors)
{
    VertexIndex = Builder.AddVertex(Position)
        .SetNormalAndTangent(Normal, Tangent)
        .SetTexCoord(UV)
        .SetColor(VertexColor);
}
else
{
    VertexIndex = Builder.AddVertex(Position)
        .SetNormalAndTangent(Normal, Tangent)
        .SetTexCoord(UV);
}
```

**Important**: Never store the builder reference:
```cpp
// WRONG - This violates the chaining pattern
auto& VertexBuilder = Builder.AddVertex(Position).SetNormalAndTangent(Normal, Tangent);
if (bGenerateUVs)
{
    VertexBuilder.SetTexCoord(UV);  // Compilation error!
}

// CORRECT - All combinations must be explicit
if (bGenerateUVs && bGenerateColors)
{
    Builder.AddVertex(Position)
        .SetNormalAndTangent(Normal, Tangent)
        .SetTexCoord(UV)
        .SetColor(Color);
}
else if (bGenerateUVs)
{
    Builder.AddVertex(Position)
        .SetNormalAndTangent(Normal, Tangent)
        .SetTexCoord(UV);
}
// etc...
```

### 4. Section Creation Without Polygroups

**Problem**: Runtime errors "Failed to find Section" when not using polygroups:
```cpp
// WRONG - This assumes polygroups are enabled
Builder.EnableTangents();
Builder.EnableTexCoords();
// No polygroups enabled, but...
RealtimeMesh->CreateSectionGroup(GroupKey, StreamSet); // Auto-creates sections for polygroups
RealtimeMesh->UpdateSectionConfig(SectionKey, Config); // ERROR: Section doesn't exist!
```

**Solution**: Explicitly control section creation when not using polygroups:
```cpp
// CORRECT - Manual section creation
Builder.EnableTangents();
Builder.EnableTexCoords();
// NO Builder.EnablePolyGroups()

// Create group without auto-section creation (false parameter)
RealtimeMesh->CreateSectionGroup(GroupKey, StreamSet, 
    FRealtimeMeshSectionGroupConfig(ERealtimeMeshSectionDrawType::Static), 
    false);  // <-- Critical: false = don't auto-create sections

// Manually create sections
RealtimeMesh->CreateSection(SectionKey, 
    FRealtimeMeshSectionConfig(0),  // Material slot
    FRealtimeMeshStreamRange(),     // Empty = use full range
    false);
```

**Key Points**:
- With polygroups: Sections are auto-created based on polygroup indices
- Without polygroups: You must manually create sections with explicit ranges
- The `false` parameter in `CreateSectionGroup` prevents automatic section creation
- Draw type (Static/Dynamic) is set at the group level, not section level

### 5. Empty Stream Ranges Cause Invisible Meshes

**Problem**: Mesh disappears, ray tracing warnings about "unaccounted triangles":
```cpp
// WRONG - Empty range means no geometry!
RealtimeMesh->CreateSection(SectionKey, 
    FRealtimeMeshSectionConfig(0), 
    FRealtimeMeshStreamRange(),  // Empty range = invisible mesh
    false);
```

**Solution**: Specify the correct vertex and index ranges:
```cpp
// CORRECT - Track your mesh data
int32 TotalVertexCount = Builder.NumVertices();
int32 TotalTriangleCount = Builder.NumTriangles();

FRealtimeMeshStreamRange SectionRange;
SectionRange.Vertices = FInt32Range(0, TotalVertexCount);
SectionRange.Indices = FInt32Range(0, TotalTriangleCount * 3);  // × 3 for indices!

RealtimeMesh->CreateSection(SectionKey, FRealtimeMeshSectionConfig(0), SectionRange, false);
```

**Remember**: 
- Each triangle has 3 indices, so multiply triangle count by 3
- Empty ranges are valid but result in nothing being rendered
- Ray tracing warnings often indicate range mismatches

## Troubleshooting

### Common Issues

1. **Mesh not visible**:
   - Check winding order (counter-clockwise for front faces)
   - Verify material slot assignment
   - Ensure section configuration is correct

2. **Performance issues**:
   - Profile stream types (use packed types when possible)
   - Check update frequency
   - Verify LOD settings

3. **Collision not working**:
   - Enable collision on sections that need it
   - Ensure collision profile is set correctly
   - Check mesh validity for complex collision

4. **Compilation errors with builders**:
   - Ensure all vertex attributes are chained from AddVertex()
   - Use struct instead of class for forward declarations
   - Check that optional features (colors, tangents) are enabled before use

5. **Inverted normals/faces**:
   - Ensure counter-clockwise winding when viewed from outside
   - For grid-based generation, verify axis ordering:
   ```cpp
   // Correct face generation for outward normals
   // LocalUp × AxisA should point in direction of AxisB
   GenerateFace(LocalUp, AxisA, AxisB);
   
   // Example for cube faces:
   // Right (+X): Up=X, A=Z, B=Y
   // Top (+Z): Up=Z, A=Y, B=X (not A=X, B=Y which would invert)
   
   // When deriving tangents from a fallback axis, prefer Normal × Up so the
   // tangent/normal/binormal basis stays right-handed. In situations where
   // the cross product degenerates, pick another fallback axis before
   // calling SetNormalAndTangent.
   ```

## Common Pitfalls and Solutions (continued)

### 6. Missing Includes for Editor Features

**Problem**: Compilation errors about undefined types like `FLevelEditorViewportClient`:
```cpp
// ERROR: use of undefined type 'FLevelEditorViewportClient'
```

**Solution**: Include the proper headers with editor guards:
```cpp
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif
```

### 7. GPU Structure Size Mismatches

**Problem**: Static assert failures for GPU structures:
```cpp
// ERROR: FQuadtreeGPUNode must be 32 bytes for GPU efficiency
```

**Solution**: Use proper vector types and verify sizes:
```cpp
struct FQuadtreeGPUNode
{
    FVector4f CenterAndRadius;  // 16 bytes (not FVector3f which is 12)
    uint32 Data[4];            // 16 bytes
    // Total: 32 bytes
};
```

## Best Practices

### Component Safety Checks

When working with RMC in actors, especially during construction/destruction, always check for null pointers:

```cpp
void AMyActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    
    // Always check the component exists
    URealtimeMeshComponent* MeshComponent = GetRealtimeMeshComponent();
    if (!MeshComponent)
    {
        return;
    }
    
    // Check mesh initialization succeeded
    URealtimeMeshSimple* RealtimeMesh = MeshComponent->InitializeRealtimeMesh<URealtimeMeshSimple>();
    if (!RealtimeMesh)
    {
        return;
    }
    
    // Safe to proceed with mesh generation
}
```

This prevents crashes during:
- Editor shutdown sequences
- Actor destruction
- Component initialization failures

## Additional Resources

- [Discord Community](https://discord.gg/KGvBBTv) - Active support community
- Example implementations in `Source/RealtimeMeshExamples/`
- API documentation in header files

## Summary

RealtimeMeshComponent provides a powerful, flexible system for runtime mesh generation in Unreal Engine. By understanding its core concepts of streams, sections, and groups, you can create efficient procedural meshes for any use case. Start with the simple examples and gradually explore advanced features as your needs grow.
