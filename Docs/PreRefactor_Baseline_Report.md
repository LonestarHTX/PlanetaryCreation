# Pre-Refactor Baseline Report

**Date:** 2025-10-05
**Build:** Fresh compile after exemplar/GPU test additions
**Status:** ⚠️ GPU threading issue discovered

---

## **1. Build Baseline** ✅

**Command:**
```bash
UnrealBuildTool.exe PlanetaryCreationEditor Win64 Development \
  -project="...\PlanetaryCreation.uproject" -WaitMutex -FromMsBuild
```

**Result:** **SUCCESS** (12.87 seconds, 15 actions)

**New Files Compiled:**
- ✅ `ExemplarTextureArray.cpp` - PNG16 loader, Texture2DArray upload
- ✅ `OceanicAmplificationGPU.cpp` - RDG compute shader dispatcher
- ✅ `GPUOceanicAmplificationTest.cpp` - Parity validation
- ✅ `GPUContinentalAmplificationTest.cpp` - Scaffolded (shader pending)
- ✅ `GPUAmplificationIntegrationTest.cpp` - Smoke test

**Warnings:**
- `Plugin 'RealtimeMeshComponent' depends on deprecated 'StructUtils'` (non-blocking)

---

## **2. GPU Oceanic Parity Test** ⚠️ **THREADING ASSERTION**

**Test:** `PlanetaryCreation.Milestone6.GPU.OceanicParity`

**Command:**
```bash
UnrealEditor-Cmd.exe ... -ExecCmds="Automation RunTests PlanetaryCreation.Milestone6.GPU.OceanicParity; Quit"
```

**Result:** **FAILED** (Threading violation in GPU code)

### **Error Details:**

**Location:** `OceanicAmplificationGPU.cpp:99`

**Assertion:**
```
Ensure condition failed: IsInParallelRenderingThread()
File: HAL/IConsoleManager.h:1620
```

**Callstack:**
```
1. GPUOceanicAmplificationTest::RunTest() (line 75)
2. TectonicSimulationService::AdvanceSteps() (line 288)
3. ApplyOceanicAmplificationGPU() (line 99)  <-- ASSERTION HERE
```

**Root Cause:** GPU compute graph execution from game/editor thread instead of render thread

**What Worked:**
- ✅ Exemplar texture array initialized (22 textures, 512×512 PF_G16)
- ✅ CPU path ran successfully (163,842 vertices)
- ✅ GPU path attempted (CVar toggled correctly)
- ❌ RDG graph execution hit threading guard

### **Fix Required:**

The GPU compute code needs to execute on the render thread. Current code (line 97-99):

```cpp
// WRONG - executing on game thread
FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
FRDGBuilder GraphBuilder(RHICmdList);  // <-- Assertion here
```

**Correct Pattern:**
```cpp
// Queue render thread execution
ENQUEUE_RENDER_COMMAND(OceanicAmplificationGPU)(
    [Service, VertexCount](FRHICommandListImmediate& RHICmdList)
    {
        FRDGBuilder GraphBuilder(RHICmdList);
        // ... shader dispatch ...
    });

// Wait for completion (or use callback for async)
FlushRenderingCommands();
```

**Blocker for:** GPU parity test validation

**Impact:** Can't measure GPU vs CPU tolerance until threading fixed

---

## **3. Performance Baseline** ⏸️ **DEFERRED**

**Reason:** Need working GPU path before capturing "before GPU" perf

**Next Step:** Fix threading issue, then capture:
- `stat unit` - Frame time breakdown
- `stat RHI` - GPU timing
- LOD 7, 10 steps, both CPU and GPU paths

---

## **4. SIMD/GPU Refactor Blockers** ✅ **DOCUMENTED**

**Source:** `simd_gpu_implementation_review.md`

### **Critical Blockers (Fold Into Refactor):**

1. **Precompute UVs/Tangents Per LOD** ⚠️ **HIGH PRIORITY**
   - Currently recomputes equirectangular UVs every frame
   - UVs only depend on topology (invariant)
   - **Savings:** ~5-10ms at L6/L7

2. **Mirror Render Data as Float SoA** ⚠️ **HIGH PRIORITY**
   - Current: `TArray<FVector3d>` (AoS, 24 bytes/vertex)
   - Target: `TArray<float> X, Y, Z` (SoA, 12 bytes total)
   - **Savings:** 50% memory bandwidth, enables SIMD

3. **Remove Logging from Hot Loops** ⚠️ **BLOCKING PERF**
   - Debug logs in `BuildMeshFromSnapshot` tick per-vertex
   - Wrap with `#if !UE_BUILD_SHIPPING` or CVar guard

4. **RMC Stream-Level Updates** ⚠️ **WASTING GPU BANDWIDTH**
   - Currently rebuilding entire mesh (indices + all streams) every step
   - Only positions/normals change per-step
   - **Savings:** 50-70% GPU upload bandwidth

### **Medium Priority (After Refactor):**

5. Boundary segments flat array (if `ComputeRidgeDirections` bottlenecks)
6. ParallelFor large loops (after SoA refactor stable)
7. ✅ Exemplar preload (**DONE** - `ExemplarTextureArray.cpp`)

### **Low Priority (Future):**

8. ISPC kernels (skip unless GPU insufficient)
9. GPU normals pass (only if CPU normals bottleneck)
10. Custom vertex factory (advanced - major rework)

**Full details:** `Docs/LOD_Refactor_Blockers.md`

---

## **Summary & Next Actions**

### **What's Working:**
- ✅ Build system stable (12.87s clean compile)
- ✅ Exemplar texture array preloads correctly (22 textures, 11.25 MB)
- ✅ CPU amplification path functional
- ✅ Test harnesses ready (3 GPU tests scaffolded)

### **What's Broken:**
- ❌ GPU compute path hits threading assertion
- ❌ Can't validate GPU vs CPU parity until threading fixed
- ❌ No perf baseline yet (blocked by threading issue)

### **Immediate Next Steps:**

1. **Fix GPU Threading (You)** ⚠️ **BLOCKING**
   - Move RDG execution to render thread via `ENQUEUE_RENDER_COMMAND`
   - Add readback synchronization (blocking or async callback)
   - Reference: `OceanicAmplificationGPU.cpp:97-168`

2. **Run Parity Test (Claude - After Fix)**
   - Execute `GPUOceanicAmplificationTest`
   - Validate tolerance <0.1 m (>99% parity)
   - Flag any drift >0.05 m

3. **Capture Perf Baseline (Claude - After Fix)**
   - LOD 7, 10 steps, `stat unit` + `stat RHI`
   - Record CPU vs GPU timing
   - Establish "before refactor" numbers

4. **Start LOD Refactor (You)**
   - Implement UV/tangent caching per LOD
   - Add SoA render mesh structure
   - Audit hot-loop logging
   - Track topology vs surface version for incremental RMC updates

---

## **Performance Targets (Estimated)**

| Metric | Current (Baseline) | After Refactor | Goal |
|--------|-------------------|----------------|------|
| L6 BuildMesh | ~15ms (est) | ~5-8ms | <10ms |
| L7 BuildMesh | ~60ms (est) | ~20-30ms | <40ms |
| L7 GPU Amplification | N/A (broken) | ~1-2ms | <5ms |
| Memory (L7 render) | ~16 MB (doubles) | ~8 MB (float SoA) | <10 MB |

---

## **Risk Assessment**

### **High Risk:**
- Threading fix may require async readback → impacts test determinism
- Float SoA conversion may expose precision bugs → need parity test

### **Medium Risk:**
- UV cache invalidation logic if not tracking topology version correctly
- SoA indexing bugs if not using helper methods

### **Low Risk:**
- Exemplar texture array stable (already tested load/upload)
- RMC stream updates well-documented in RealtimeMeshComponent API

---

**End of Baseline Report**

**Action for You:** Fix `OceanicAmplificationGPU.cpp` threading issue before continuing LOD refactor. Once GPU path works, Claude will capture perf baseline and re-run parity test.
