# LOD Refactor: Critical Blockers from SIMD/GPU Review

**Status:** 📋 Action Items for Current Refactor
**Source:** `simd_gpu_implementation_review.md`
**Created:** 2025-10-05

---

## **HIGH PRIORITY - Fold Into Current Refactor**

These are **must-fix** items that should be addressed while refactoring LOD caching/SoA:

### **1. Precompute UVs/Tangents Per LOD** ⚠️ **CRITICAL**

**Current Waste:**
- `BuildMeshFromSnapshot` recomputes equirectangular UVs **every frame**
- UVs only depend on unit direction + LOD topology (invariant across steps)
- Same for tangent seed (`UpVector`) heuristic

**Where:**
- `TectonicSimulationController.cpp` - `BuildMeshFromSnapshot()`

**Fix:**
```cpp
// In FTectonicSimulationController
TMap<int32, TArray<FVector2f>> CachedUVsByLOD;
TMap<int32, TArray<FVector3f>> CachedTangentSeedsByLOD;

// Compute once per LOD change
void PrecomputeInvariantsForLOD(int32 LODLevel)
{
    const TArray<FVector3d>& RenderVertices = Service->GetRenderVertices();

    TArray<FVector2f>& UVs = CachedUVsByLOD.FindOrAdd(LODLevel);
    UVs.SetNumUninitialized(RenderVertices.Num());

    for (int32 i = 0; i < RenderVertices.Num(); ++i)
    {
        FVector3d UnitDir = RenderVertices[i].GetSafeNormal();
        UVs[i] = FVector2f(
            0.5f + FMath::Atan2(UnitDir.Y, UnitDir.X) / (2.0f * PI),
            0.5f - FMath::Asin(UnitDir.Z) / PI
        );
    }

    // Same for tangent seeds...
}

// Reuse in BuildMeshFromSnapshot
for (int32 i = 0; i < RenderVertices.Num(); ++i)
{
    Builder.SetTexCoord(CachedUVsByLOD[CurrentLOD][i]);  // No trig!
}
```

**Savings:** ~5-10ms at L6/L7 (removes trig from tight loop)

---

### **2. Mirror Render Data as Float SoA** ⚠️ **CRITICAL**

**Current Waste:**
- Render vertices stored as `TArray<FVector3d>` (AoS, 24 bytes/vertex)
- Converted to `FVector3f` (AoS, 12 bytes) at RMC handoff
- GPU/SIMD can't vectorize AoS efficiently

**Where:**
- `TectonicSimulationController.cpp` - mesh build passes

**Fix:**
```cpp
// Add to controller (or service snapshot)
struct FRenderMeshSoA
{
    TArray<float> PositionX;
    TArray<float> PositionY;
    TArray<float> PositionZ;
    TArray<float> NormalX;
    TArray<float> NormalY;
    TArray<float> NormalZ;

    void ResizeForVertexCount(int32 Count)
    {
        PositionX.SetNumUninitialized(Count);
        PositionY.SetNumUninitialized(Count);
        PositionZ.SetNumUninitialized(Count);
        // ...
    }

    void SetPosition(int32 Index, const FVector3d& Pos)
    {
        PositionX[Index] = static_cast<float>(Pos.X);
        PositionY[Index] = static_cast<float>(Pos.Y);
        PositionZ[Index] = static_cast<float>(Pos.Z);
    }
};

FRenderMeshSoA RenderMeshSoA;  // Persistent, refreshed on LOD change
```

**Usage:**
- Convert `TArray<FVector3d>` → SoA once per LOD change
- Do math on SoA (normals, transforms, ridge dirs)
- Convert SoA → AoS only at RMC handoff

**Savings:** 2× memory bandwidth, enables SIMD later

---

### **3. Remove Logging from Hot Loops** ⚠️ **BLOCKING PERF**

**Current Issue:**
- Debug logs added to `CreateMeshBuildSnapshot()` in recent commits
- Tick inside per-vertex loops → kills perf

**Where:**
- `TectonicSimulationController.cpp` - mesh build
- `TectonicSimulationService.cpp` - amplification passes

**Fix:**
```cpp
// Wrap with compile-time guard
#if !UE_BUILD_SHIPPING
if (CVarPlanetaryCreationVerboseLogging->GetBool())
{
    UE_LOG(LogPlanetaryCreation, Verbose, TEXT("Debug info"));
}
#endif

// OR use TRACE macros (compiled out in Shipping)
TRACE_CPUPROFILER_EVENT_SCOPE(BuildMesh);
```

**Action:** Audit all `UE_LOG` calls in:
- `BuildMeshFromSnapshot`
- `ApplyOceanicAmplification`
- `ApplyContinentalAmplification`

---

### **4. RMC Stream-Level Updates** ⚠️ **WASTING GPU BANDWIDTH**

**Current Waste:**
- Rebuilding entire mesh (indices + positions + normals + UVs) every step
- Only **positions** and **normals** change per-step
- Indices/UVs static unless LOD changes

**Where:**
- `TectonicSimulationController.cpp` - RMC update logic

**Fix:**
```cpp
// Track what changed
bool bTopologyChanged = (Service->GetTopologyVersion() != LastTopologyVersion);

if (bTopologyChanged)
{
    // Full rebuild: indices, positions, normals, UVs
    Mesh->CreateSectionGroup(GroupKey, MoveTemp(StreamSet));
}
else
{
    // Incremental: positions + normals only
    StreamSet.UpdatePositions(Positions);
    StreamSet.UpdateNormals(Normals);
    Mesh->UpdateSectionGroup(GroupKey, MoveTemp(StreamSet));
}
```

**Savings:** 50-70% less GPU upload bandwidth per step

---

## **MEDIUM PRIORITY - After Initial Refactor**

These can wait until LOD/SoA refactor is stable:

### **5. Boundary Segments Flat Array**

**Issue:** `TMap<TPair<int32,int32>, FPlateBoundary>` not GPU/SIMD-friendly

**Fix:** Add `GetBoundarySegments()` returning CSR-style flat array

**When:** If `ComputeRidgeDirections` becomes a bottleneck

---

### **6. ParallelFor Large Loops**

**Target:** Per-vertex math in `BuildMeshFromSnapshot`

**Fix:**
```cpp
ParallelFor(RenderVertices.Num(), [&](int32 VertexIdx)
{
    // Compute position, normal, color
}, EParallelForFlags::None);
```

**When:** After SoA refactor complete (needs thread-safe SoA access)

---

### **7. Exemplar Preload** ✅ **DONE**

- ✅ Already implemented (`ExemplarTextureArray.cpp`)
- ✅ Loads 22 PNG16s once, uploads to GPU
- ✅ 11.25 MB resident, no per-step disk I/O

---

## **LOW PRIORITY - Future Optimization**

Defer until M6 GPU is complete:

### **8. ISPC Kernels**

- CPU SIMD for ridge directions, amplification math
- 2-6× speedup, but GPU gives 10×
- **Skip unless GPU path insufficient**

### **9. GPU Normals Pass**

- Compute normals on GPU from displaced positions
- Avoids CPU normal recompute entirely
- **Only if CPU normals become bottleneck**

### **10. Custom Vertex Factory**

- Move displacement to material/vertex shader
- Avoid GPU→CPU readback
- **Advanced - requires major RMC rework**

---

## **PUNCH LIST FOR CURRENT REFACTOR**

### **Must-Do (This Session):**
1. ✅ **Cache UVs per LOD** - `TMap<int32, TArray<FVector2f>> CachedUVsByLOD`
2. ✅ **Cache tangent seeds per LOD** - `TMap<int32, TArray<FVector3f>> CachedTangentSeedsByLOD`
3. ✅ **Add SoA render mesh struct** - `FRenderMeshSoA` with `float X[], Y[], Z[]`
4. ✅ **Audit hot-loop logging** - Wrap with `#if !UE_BUILD_SHIPPING`
5. ✅ **Track topology vs surface version** - Incremental RMC updates

### **Validation (Before Commit):**
- ✅ Run `GPUOceanicAmplificationTest` - tolerance still <0.05m
- ✅ Run `LODConsistencyTest` - ensure cached UVs match recomputed
- ✅ Capture `stat unit` before/after - measure savings
- ✅ Check memory usage - SoA should be ~50% less than double AoS

---

## **FILE CHECKLIST**

### **Files to Touch:**
- ✅ `TectonicSimulationController.h` - Add cache members
- ✅ `TectonicSimulationController.cpp` - Implement caching, SoA conversion
- ⚠️ Audit all `UE_LOG` calls in hot paths

### **Files to NOT Touch (Yet):**
- ❌ `TectonicSimulationService.cpp` - Keep doubles, don't convert to SoA yet
- ❌ Boundary data structures - Defer flat array until profiling shows need
- ❌ ISPC integration - GPU path first

---

## **PERFORMANCE TARGETS**

| Metric | Before (Current) | After (Refactor) | Goal |
|--------|------------------|------------------|------|
| L6 BuildMesh | ~15ms (est) | ~5-8ms | <10ms |
| L7 BuildMesh | ~60ms (est) | ~20-30ms | <40ms |
| Memory (L7 render) | ~16 MB (doubles) | ~8 MB (float SoA) | <10 MB |
| UV recompute | Every frame | Once per LOD | 0ms/step |

---

## **RISKS & MITIGATIONS**

### **Risk 1: Float Precision Loss**
- **Mitigation:** Keep doubles in service, convert to float only for rendering
- **Validation:** Add `FloatVsDoubleParity` test (max delta <1e-3 m)

### **Risk 2: UV Cache Invalidation**
- **Mitigation:** Invalidate cache on topology version bump
- **Validation:** `LODConsistencyTest` checks UV correctness

### **Risk 3: SoA Indexing Bugs**
- **Mitigation:** Helper methods (`SetPosition(i, Vec)` not raw `X[i] = ...`)
- **Validation:** Existing visual tests + `HeightmapVisualizationTest`

---

**Next Step:** Implement punch list items 1-5 while refactoring LOD caching. Wire up GPU oceanic path once parity test passes.
