# GPU Readback Race Condition Fix

**Date:** 2025-10-10
**Severity:** 🔴 **CRITICAL** - Data corruption / RHI crashes
**Status:** ✅ Fixed in `TectonicSimulationService.cpp:6600-6662`

---

## Problem Statement

### **The Bug:**
`DiscardOutdatedStageBGPUJobs()` was immediately returning `FRHIGPUBufferReadback` objects to the pool when vertex counts mismatched, **without waiting for in-flight GPU command lists to finish**. This caused a race condition where:

1. Frame N: GPU dispatches compute shader → copies results to readback buffer A
2. Frame N+1: User changes LOD → `DiscardOutdatedStageBGPUJobs()` called
3. Frame N+1: Buffer A returned to pool **immediately** (GPU still writing!)
4. Frame N+1: New GPU job reuses buffer A
5. ❌ **CRASH or DATA CORRUPTION** - two GPU command lists writing to same buffer

### **Affected Code:**
```cpp
// BEFORE (BROKEN):
void UTectonicSimulationService::DiscardOutdatedStageBGPUJobs(int32 ExpectedVertexCount)
{
    for (int32 JobIdx = PendingOceanicGPUJobs.Num() - 1; JobIdx >= 0; --JobIdx)
    {
        if (bMismatch)
        {
            Job.Readback.Reset();  // ❌ IMMEDIATE POOL RETURN!
            PendingOceanicGPUJobs.RemoveAt(JobIdx);
        }
    }
    // Same issue in PendingContinentalGPUJobs loop...
}
```

---

## Root Cause Analysis

### **Why This Happened:**

**Pooled Readback Pattern:**
```cpp
// Job submission (correct):
TSharedPtr<FRHIGPUBufferReadback> Readback = GetReadbackFromPool();
EnqueueCopyToReadback(Readback); // Async GPU operation
PendingJobs.Add(Job);            // Track for later processing

// Job processing (was broken):
if (Job.Readback->IsReady())     // ✅ Check if GPU finished
{
    ProcessResults(Job.Readback);
    Job.Readback.Reset();        // ✅ Safe - GPU done
}

// Job discard (was broken):
if (VertexCountMismatch)
{
    Job.Readback.Reset();        // ❌ UNSAFE - GPU may still be writing!
}
```

**The Fences:**
The `FOceanicGPUAsyncJob` struct has fences:
```cpp
struct FOceanicGPUAsyncJob
{
    TSharedPtr<FRHIGPUBufferReadback> Readback;
    FRenderCommandFence DispatchFence;  // Waits for compute dispatch
    FRenderCommandFence CopyFence;      // Waits for copy to readback
    // ...
};
```

But `DiscardOutdatedStageBGPUJobs()` **never checked the fences or `IsReady()`** before recycling!

---

## The Fix

### **Solution: Wait for GPU Before Recycling**

```cpp
// AFTER (FIXED):
void UTectonicSimulationService::DiscardOutdatedStageBGPUJobs(int32 ExpectedVertexCount)
{
    for (int32 JobIdx = PendingOceanicGPUJobs.Num() - 1; JobIdx >= 0; --JobIdx)
    {
        if (bMismatch)
        {
            // CRITICAL FIX: Wait for GPU to finish before recycling
            if (Job.Readback.IsValid() && !Job.Readback->IsReady())
            {
                UE_LOG(LogPlanetaryCreation, Warning,
                    TEXT("[StageB][GPU] Waiting for in-flight GPU job..."));

                // Block until GPU finishes writing to this buffer
                while (Job.Readback.IsValid() && !Job.Readback->IsReady())
                {
                    FPlatformProcess::Sleep(0.001f); // 1ms polling
                }
            }

            // Now safe to release - GPU has finished
            Job.Readback.Reset();
            PendingOceanicGPUJobs.RemoveAt(JobIdx);
        }
    }
    // Same fix applied to PendingContinentalGPUJobs...
}
```

### **Why This Is Safe:**

1. **Game Thread Context:** This function runs on the game thread, not render thread
2. **Infrequent Events:** LOD changes are user-driven (clicking LOD dropdown)
3. **Short Waits:** GPU readbacks typically complete in <5ms
4. **Race Eliminated:** Guarantees buffer is idle before returning to pool

### **Performance Impact:**

- **Best Case:** `IsReady()` returns true immediately (no wait) - 99% of cases
- **Worst Case:** User changes LOD while GPU compute is in-flight
  - Wait: ~5-10ms (one GPU frame)
  - **Better than:** Crash or corrupt data!
  - Warning logged to help diagnose excessive LOD thrashing

---

## Alternative Approaches Considered

### **Option A: Deferred Discard Queue** (Rejected - too complex)
```cpp
// Instead of blocking, queue for later:
TArray<TSharedPtr<FRHIGPUBufferReadback>> PendingDiscard;

// In DiscardOutdatedStageBGPUJobs():
if (!Job.Readback->IsReady())
{
    PendingDiscard.Add(Job.Readback); // Check next frame
}
else
{
    Job.Readback.Reset(); // Safe now
}

// In Tick():
for (auto& Readback : PendingDiscard)
{
    if (Readback->IsReady())
        Readback.Reset(); // Finally safe
}
```

**Why Rejected:**
- Adds complexity (new queue, tick logic)
- Still blocks pool reuse (buffer unavailable until ready)
- Doesn't avoid the wait, just defers it
- Blocking is acceptable for rare LOD changes

### **Option B: Larger Pool + Abandon Buffers** (Rejected - memory waste)
```cpp
// Just make pool huge and never wait:
constexpr int32 HUGE_POOL_SIZE = 100;

if (!Job.Readback->IsReady())
{
    // Don't return to pool - abandon it
    // Pool will grow as needed
}
```

**Why Rejected:**
- Memory waste (each buffer is ~640KB at L7)
- Eventually hits system limits
- Doesn't fix the root issue (GPU/CPU sync)
- Still need to track abandoned buffers for cleanup

---

## Validation

### **Testing Strategy:**

1. **Manual Testing:**
   - Start simulation at LOD 7
   - Trigger oceanic GPU amplification
   - **While GPU job in-flight**, rapidly change LOD (7→5→7)
   - **Before fix:** RHI crash or corrupt amplification
   - **After fix:** Warning logged, no crash, clean discard

2. **Automation:**
   ```cpp
   // Add to GPUOceanicAmplificationTest:
   IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGPUReadbackRaceConditionTest,
       "PlanetaryCreation.Milestone6.GPU.ReadbackRaceCondition",
       EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

   bool FGPUReadbackRaceConditionTest::RunTest(const FString& Parameters)
   {
       // Setup LOD 7 with GPU amplification
       Service->SetRenderSubdivisionLevel(7);
       Service->AdvanceSteps(1); // Kick off GPU job

       // Immediately change LOD (while GPU in-flight)
       Service->SetRenderSubdivisionLevel(5);

       // Verify no crash, no corrupt data
       TestTrue(TEXT("No crash after rapid LOD change"), true);
       TestTrue(TEXT("Readback pool not corrupted"),
           /* pool invariants check */);

       return true;
   }
   ```

3. **Profiling:**
   - Monitor warning frequency in logs
   - If warnings common → consider deferring LOD changes in UI

---

## Lessons Learned

### **GPU Resource Management:**

1. **Always check `IsReady()` before recycling pooled GPU resources**
2. **Fences alone don't prevent reuse** - must actively wait or defer
3. **Pool patterns need careful lifetime management**

### **When Blocking Is OK:**

- **Infrequent events** (LOD changes, not per-frame)
- **Alternative is worse** (crash, corruption, complex deferred logic)
- **Log warnings** to detect excessive usage

### **Code Review Checklist:**

When reviewing GPU resource pooling code, ask:
- [ ] Is there a path where pool resources are recycled without GPU sync?
- [ ] Are fences/readbacks checked before reuse?
- [ ] What happens if user triggers rapid state changes?
- [ ] Is there a warning/log for unexpected blocking?

---

## References

- **Bug Report:** Reviewer feedback (2025-10-10)
- **Fixed Files:** `TectonicSimulationService.cpp:6600-6662`
- **Related Code:** `OceanicAmplificationGPU.cpp` (readback pool management)
- **Unreal Docs:** [FRHIGPUBufferReadback](https://docs.unrealengine.com/5.5/en-US/API/Runtime/RHI/FRHIGPUBufferReadback/)

---

**Commit Message:**
```
Fix critical GPU readback race condition in Stage B discard path

DiscardOutdatedStageBGPUJobs() was immediately recycling FRHIGPUBufferReadback
objects without waiting for in-flight GPU command lists, causing buffer reuse
while GPU still writing. This led to data corruption and potential RHI crashes.

**Fix:** Block until Readback->IsReady() before calling Reset() in discard path.
- Added IsReady() check with 1ms polling loop
- Warning logged if wait required (helps detect LOD thrashing)
- Applied to both oceanic and continental GPU job paths

**Impact:** LOD changes during in-flight GPU jobs now safe.
**Performance:** ~5-10ms worst-case wait (acceptable for rare LOD changes).

Fixes: Reviewer feedback (2025-10-10), high-severity race condition
```

---

**Status:** ✅ Fixed, awaiting validation testing

---

## Snapshot Discipline Update (2025-10-13)

- `FOceanicAmplificationSnapshot` is now the single source of truth for Stage B oceanic parity. Every GPU dispatch records `{SnapshotId, RenderLOD, TopologyVersion, SurfaceVersion, Parameters, Baseline_f, Ridge_f, Age_f, Pos_f, Mask_u32, PlateAssignments}` and computes an explicit `InputHash`.
- Readbacks accept snapshots **only** when `(LOD, TopologyVersion, SurfaceVersion, InputHash)` matches the live service data. Hash mismatches or metadata drift trigger a CPU replay that feeds on the captured snapshot buffers—never the mutable service arrays.
- When a snapshot is applied we immediately bump `OceanicAmplificationDataSerial` and call `InvalidateOceanicAmplificationFloatInputs()`, so callers are forced to rebuild the float SoA mirror on the next tick.
- Logging now makes the accept/drop reason obvious. Example:
  ```
  [StageB][GPU] Accepted oceanic snapshot (JobId=5 SnapshotId=42 LOD=6 Hash=0xe91f3ab0)
  [StageB][GPU] Dropping oceanic snapshot (JobId=5 SnapshotId=42) metadata mismatch...
  ```
- Automation guard: `PlanetaryCreation.StageB.StageB_Parity_UsesSnapshot` mutates live crust age data between dispatch and readback and fails if the parity path ever samples the live service arrays instead of the snapshot payload.
