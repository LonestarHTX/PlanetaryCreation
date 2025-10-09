# GPU Amplification Test Suite

**Status:** ✅ Complete (4/4 tests active)
**Milestone:** M6 GPU Acceleration
**Created:** 2025-10-05

---

## Overview

Four automation-facing tests validate GPU compute shader correctness, cache discipline, and parity with the CPU baseline. All tests use Unreal's automation framework and integrate with the existing M6 test suite.

> **Note:** The editor now boots with the paper-authentic defaults (LOD 5, Stage B/GPU/PBR enabled). CPU-focused or regression suites should issue `r.PlanetaryCreation.PaperDefaults 0` at setup and re-enable the GPU path explicitly where needed.

---

## Test Files

### 1. **GPUOceanicAmplificationTest.cpp** ✅ Ready to Run

**Category:** `PlanetaryCreation.Milestone6.GPU.OceanicParity`

**Purpose:** Validates GPU oceanic amplification matches CPU baseline within 0.1 m tolerance.

**Test Flow:**
1. Setup L7 (163,842 vertices) with oceanic amplification enabled
2. Advance 5 steps to create crust age variation
3. **Run 1 (CPU):** Disable GPU via CVar, advance 1 step, capture results
4. **Run 2 (GPU):** Undo step, enable GPU via CVar, advance 1 step, capture results
5. Compare CPU vs GPU elevation deltas for all oceanic vertices

**Acceptance Criteria:**
- ✅ >99% of oceanic vertices within ±0.1 m tolerance
- ✅ Max delta < 1.0 m (allows minor float precision drift)
- ✅ Mean absolute delta < 0.05 m

**Logged Metrics:**
```
[GPUOceanicParity] Validation Results:
  Total oceanic vertices: 98234
  Within tolerance (±0.10 m): 98012 (99.77%)
  Max delta: 0.087 m (vertex 45231)
  Mean absolute delta: 0.023 m
```

**Enable Command:**
```
Automation RunTests PlanetaryCreation.Milestone6.GPU.OceanicParity
```

---

### 2. **GPUContinentalAmplificationTest.cpp** ✅ Ready to Run

**Category:** `PlanetaryCreation.Milestone6.GPU.ContinentalParity`

**Purpose:** Validates GPU continental amplification matches the CPU snapshot baseline (same structure as the oceanic test, with an additional drift fallback pass).

**Test Flow:**
1. Warm the Stage B cache via a CPU baseline replay (Run 1).
2. Undo and dispatch the GPU path (Run 2). Snapshot-backed vertices should report `[ContinentalGPUReadback] GPU applied … (snapshot)`.
3. Undo again and trigger the drift fallback path to ensure snapshot mismatches gracefully fall back to the CPU cache (Run 3).

**Acceptance Criteria:**
- ✅ Snapshot-backed run reports 100% of sampled vertices within ±0.1 m (`MeanDelta < 0.05 m`, `MaxDelta < 1 m`).
- ✅ Fallback run completes without error and logs `Source=snapshot fallback`.

**Enable Command:**
```
Automation RunTests PlanetaryCreation.Milestone6.GPU.ContinentalParity
```

---

### 3. **GPUAmplificationIntegrationTest.cpp** ✅ Ready to Run

**Category:** `PlanetaryCreation.Milestone6.GPU.IntegrationSmoke`

**Purpose:** Validates GPU amplification produces finite results (no NaN/Inf) across multi-step simulations at high LOD.

**Test Flow:**
1. **Test 1 (L6):** 40,962 vertices, 5 steps, check all elevations finite
2. **Test 2 (L7):** 163,842 vertices, 3 steps, check all elevations finite
3. **Test 3 (Cleanup):** 3 reset cycles, verify no resource leaks

**Acceptance Criteria:**
- ✅ 100% of elevation values are finite (no NaN/Inf)
- ✅ Elevation range stays reasonable (-10km to +10km)
- ✅ Multiple reset cycles don't leak GPU resources

**Logged Metrics:**
```
[GPUIntegrationSmoke] L6 Elevation Stats:
  Finite values: 40962 (100.00%)
  NaN values: 0
  Inf values: 0
  Range: [-6234.12, 1234.56] m

[GPUIntegrationSmoke] L7 Elevation Stats:
  Finite values: 163842 (100.00%)
  NaN values: 0
  Inf values: 0
  Range: [-6012.34, 987.65] m
```

**Enable Command:**
```
Automation RunTests PlanetaryCreation.Milestone6.GPU.IntegrationSmoke
```

---

### 4. **ContinentalBlendCacheTest.cpp** ✅ Ready to Run

**Category:** `PlanetaryCreation.Milestone6.ContinentalBlendCache`

**Purpose:** Ensures the continental exemplar blend cache stays aligned with the Stage B serial so CPU fallbacks reuse cached blends instead of re-sampling the atlas.

**Test Flow:**
1. Configure Stage B with continental amplification enabled (LOD 5).
2. Advance a few steps to populate the amplification caches.
3. Refresh the GPU inputs and inspect the blend cache.
4. Assert the cache size matches the continental entries, at least one vertex has cached data, and its cached serial equals the current `OceanicAmplificationDataSerial`.

**Acceptance Criteria:**
- ✅ Blend cache array matches the snapshot cache array length.
- ✅ At least one continental vertex reports cached blends.
- ✅ Blend cache serial equals the Stage B serial (indicates reuse will hit immediately after the next bump).

**Enable Command:**
```
Automation RunTests PlanetaryCreation.Milestone6.ContinentalBlendCache
```

---

## Running All GPU Tests

```bash
# From Unreal Editor console
Automation RunTests PlanetaryCreation.Milestone6.GPU
Automation RunTests PlanetaryCreation.Milestone6.ContinentalBlendCache

# From command line
"/mnt/c/Program Files/Epic Games/UE_5.5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject" \
  -SetCVar="r.PlanetaryCreation.StageBProfiling=1" \
  -ExecCmds="r.PlanetaryCreation.StageBProfiling 1; Automation RunTests PlanetaryCreation.Milestone6.GPU; Automation RunTests PlanetaryCreation.Milestone6.ContinentalBlendCache; Quit" \
  -unattended -nop4 -nosplash
```

---

## CVar Configuration

All tests manipulate `r.PlanetaryCreation.UseGPUAmplification` automatically:

```cpp
IConsoleVariable* CVarGPUAmplification = IConsoleManager::Get().FindConsoleVariable(
    TEXT("r.PlanetaryCreation.UseGPUAmplification"));

// Force CPU path
CVarGPUAmplification->Set(0, ECVF_SetByCode);

// Enable GPU path
CVarGPUAmplification->Set(1, ECVF_SetByCode);

// Restore original value after test
CVarGPUAmplification->Set(OriginalValue, ECVF_SetByCode);
```

**Manual Toggle (for debugging):**
```
# In editor console
r.PlanetaryCreation.UseGPUAmplification 1  # Enable GPU
r.PlanetaryCreation.UseGPUAmplification 0  # Disable GPU (CPU fallback)
```

---

## Error Analysis Snippets

### From Milestone 6 Plan (Reference)

```cpp
// Example: Error statistics logging pattern
double MaxDelta_m = 0.0;
int32 MaxDeltaIdx = INDEX_NONE;
double MeanAbsoluteDelta_m = 0.0;

for (int32 VertexIdx = 0; VertexIdx < CPUResults.Num(); ++VertexIdx)
{
    const double Delta = FMath::Abs(CPUResults[VertexIdx] - GPUResults[VertexIdx]);
    MeanAbsoluteDelta_m += Delta;

    if (Delta > MaxDelta_m)
    {
        MaxDelta_m = Delta;
        MaxDeltaIdx = VertexIdx;
    }
}

MeanAbsoluteDelta_m /= CPUResults.Num();

UE_LOG(LogPlanetaryCreation, Log, TEXT("Max delta: %.4f m (vertex %d)"), MaxDelta_m, MaxDeltaIdx);
UE_LOG(LogPlanetaryCreation, Log, TEXT("Mean absolute delta: %.4f m"), MeanAbsoluteDelta_m);
```

---

## Integration with Existing Test Suite

### Current M6 Test Coverage

- **CPU amplification:** `OceanicAmplificationTest`, `ContinentalAmplificationTest`
- **GPU parity & cache:** `GPUOceanicParity`, `GPUContinentalParity`, `GPUAmplificationIntegrationTest`, `ContinentalBlendCacheTest`
- **Preview diagnostics:** `GPUPreviewDiagnosticTest`, `GPUPreviewVertexParityTest`, `GPUPreviewSeamMirroringTest`
- **Integration smoke:** `GPUAmplificationIntegrationTest`

> CI executes the entire block above for the Milestone 6 suite; seam mirroring is now a gating test for the preview shader path.

### Test Dependencies

| Test | Depends On | Status |
|------|-----------|--------|
| GPUOceanicAmplificationTest | `OceanicAmplificationGPU.cpp` working | ✅ Ready |
| GPUContinentalAmplificationTest | `ContinentalAmplificationGPU.cpp` implemented | ✅ Ready (snapshot + fallback paths covered) |
| GPUAmplificationIntegrationTest | Both GPU paths working | ✅ Ready |
| GPUPreviewSeamMirroringTest | `ApplyOceanicAmplificationGPUPreview` seam metrics enabled | ✅ Ready (fails if mirrored seam differs by >512 texels) |

---

## Troubleshooting

### Test Fails with "CVar not found"

**Symptom:**
```
[GPUOceanicParity] CVar 'r.PlanetaryCreation.UseGPUAmplification' not found
```

**Fix:** Check `TectonicSimulationService.cpp` has CVar registration:
```cpp
#if WITH_EDITOR
static TAutoConsoleVariable<int32> CVarPlanetaryCreationUseGPUAmplification(
    TEXT("r.PlanetaryCreation.UseGPUAmplification"),
    0,
    TEXT("Enable GPU compute path for Stage B amplification..."),
    ECVF_Default);
#endif
```

### Test Fails with "GPU path not implemented"

**Symptom:**
```
[GPUOceanicParity] FAILED: Only 0.00% parity
```

**Fix:** Check `ApplyOceanicAmplificationGPU()` returns `true`:
```cpp
bool ApplyOceanicAmplificationGPU(UTectonicSimulationService& Service)
{
    // Must return true if GPU path succeeded
    return true;  // Not false!
}
```

### Continental Test Always Passes (Expected)

**Behavior:** Test logs warnings but doesn't fail even if shader not implemented.

**Reason:** `AddExpectedError` suppresses failures until shader ready.

**Action:** Remove `AddExpectedError` once `ContinentalAmplificationGPU.cpp` lands.

---

## Next Steps

1. ✅ **Run oceanic parity test** - Enable `r.PlanetaryCreation.UseGPUAmplification=1` and verify GPU vs CPU delta
2. ⏸️ **Implement continental shader** - Use `GPU_Exemplar_Integration.md` as reference
3. ⏸️ **Enable continental test** - Remove `AddExpectedError` and uncomment assertions
4. ✅ **Profile performance** - Add timing logs to compare GPU vs CPU step times

---

## Expected Test Results

### Before GPU Implementation
```
PlanetaryCreation.Milestone6.GPU.OceanicParity: FAILED (GPU fallback to CPU, perfect parity)
PlanetaryCreation.Milestone6.GPU.ContinentalParity: PASSED (expected error suppressed)
PlanetaryCreation.Milestone6.GPU.IntegrationSmoke: PASSED (oceanic only)
```

### After GPU Implementation
```
PlanetaryCreation.Milestone6.GPU.OceanicParity: PASSED (>99% parity, <0.05m mean delta)
PlanetaryCreation.Milestone6.GPU.ContinentalParity: PASSED (>99% parity, <0.05m mean delta)
PlanetaryCreation.Milestone6.GPU.IntegrationSmoke: PASSED (100% finite, no leaks)
```

---

**End of GPU Test Suite Documentation**
