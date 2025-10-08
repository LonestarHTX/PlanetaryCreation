# Fixing the GPU Readback Hang in Automation Tests

## Problem Summary
After the asynchronous refactor of the GPU path, readbacks are enqueued but never drained because the automation harness doesn’t tick the render thread. The tests block waiting for `IsReady()` while the render commands are still queued. Without `FlushRenderingCommands()` or `BlockUntilGPUIdle()`, the graph never submits.

---

## Root Cause
- The render thread doesn’t progress in headless commandlet mode.
- `ProcessPendingOceanicGPUReadbacks()` relies on `IsReady()`, but the job has not yet reached the GPU.
- Since we removed blocking flushes for async behavior, nothing triggers the RDG dispatch during automation tests.

---

## Corrective Strategy
Keep the async behavior for runtime, but explicitly push the render work during tests.

### 1. Add a Per-Job Dispatch Fence
Use a `FRenderCommandFence` to ensure the dispatch lambda is executed without blocking on GPU completion.

```cpp
struct FPendingReadback
{
    TUniquePtr<FRHIGPUBufferReadback> Readback;
    FRenderCommandFence DispatchFence;
    uint32 SnapshotId;
    uint32 NumBytes;
};

FPendingReadback& Req = PendingReadbacks.Emplace_GetRef();
Req.Readback = MakeUnique<FRHIGPUBufferReadback>(TEXT("OceanicAmplifiedElevation"));

ENQUEUE_RENDER_COMMAND(DispatchOceanicCS)(
    [/* captures */](FRHICommandListImmediate& RHICmdList)
    {
        // Build RDG, enqueue compute pass, enqueue readback copy
    });

Req.DispatchFence.BeginFence();
```

### 2. Update the Pump Function
Add a forced submit mode to drain pending work when needed.

```cpp
void ProcessPendingOceanicGPUReadbacks(bool bForceSubmitOnce)
{
    for (int32 i = PendingReadbacks.Num() - 1; i >= 0; --i)
    {
        FPendingReadback& Req = PendingReadbacks[i];

        if (bForceSubmitOnce && !Req.DispatchFence.IsFenceComplete())
        {
            Req.DispatchFence.Wait(); // forces dispatch submission
        }

        if (Req.Readback && Req.Readback->IsReady())
        {
            const void* Ptr = Req.Readback->Lock(Req.NumBytes);
            // Copy back data to host buffer here
            Req.Readback->Unlock();

            PendingReadbacks.RemoveAtSwap(i);
        }
    }
}
```

### 3. Patch the Automation Tests
Before polling `IsReady()`, call a forced submit once:

```cpp
Controller->KickOceanicAmplificationGPU(SnapshotId);
Controller->ProcessPendingOceanicGPUReadbacks(/*bForceSubmitOnce=*/true);

const double Deadline = FPlatformTime::Seconds() + 30.0;
bool bReady = false;
while (FPlatformTime::Seconds() < Deadline)
{
    Controller->ProcessPendingOceanicGPUReadbacks(false);
    if (Controller->AreAllReadbacksReadyFor(SnapshotId))
    {
        bReady = true;
        break;
    }
    FPlatformProcess::SleepNoStats(0.001f);
}
```

This ensures dispatch happens once, then polling proceeds asynchronously.

✅ **Status (2025-10-07):** Dispatch/copy fences are live, forced submit waits once during automation, Stage B profiling records readback stalls, and replay snapshots now bump their serial + hash after mutation so `ProcessPendingOceanicGPUReadbacks()` stays on the async path instead of tripping the fallback every frame.

---

## 4. Temporary Hotfix (if needed)
To unblock tests immediately, insert this after dispatch:

```cpp
FRenderCommandFence Fence;
Fence.BeginFence();
Fence.Wait();
```

This submits the render graph without reverting to blocking GPU flushes.

---

## 5. Guardrails
- **Skip GPU tests under NullRHI**:
  ```cpp
  if (IsRunningCommandlet() && (GDynamicRHI == nullptr || IsRunningNullRHI()))
      return true; // skip safely
  ```
- **Tag each readback** with SnapshotId and ignore stale data.
- **Never poll with `FlushRenderingCommands()`**; use the fence pattern instead.
- **Set a timeout** (30–60s) to avoid infinite waits.

---

## TL;DR
✅ Add `FRenderCommandFence` after dispatch.
✅ Wait it once in `ProcessPending…(true)` before polling.
✅ Update automation tests to call `ProcessPending…(true)` then poll.
✅ Use fence-based submission instead of `FlushRenderingCommands()`.
✅ Skip GPU tests under NullRHI.

Result: async GPU path stays non-blocking, automation no longer hangs.
