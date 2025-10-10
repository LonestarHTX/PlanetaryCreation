# Hydraulic Erosion Implementation Plan (M6 Phase 3)

**Created:** 2025-10-10
**Status:** Implemented – Stage B hydraulic routing/erosion landed Oct 2025
**Owner:** Simulation Engineer
**Effort:** 6 days (4 days Task 3.1, 2 days Task 3.2)

---

## Goal
Add realistic valley formation to Stage B amplified terrain using stream power erosion law, achieving paper-parity terrain shapes (mountains & valleys).

## Architecture
- **New file**: `Source/PlanetaryCreationEditor/Private/HydraulicErosion.cpp`
- **Integration point**: After Stage B amplification in `AdvanceSteps()`
- **Target data**: Operate on `VertexAmplifiedElevation` (NOT base elevation)
- **Performance target**: <8ms at L6 (continental-only if needed)

---

## Implementation Steps

### Task 3.1: Hydraulic Routing (4 days)

#### Step 1: Create HydraulicErosion.cpp
- Implement `UTectonicSimulationService::ApplyHydraulicErosion(double DeltaTimeMy)`
- Use ParallelFor pattern like sediment transport (proven 6-11x speedup)
- SoA buffers: `DownhillNeighbor[]`, `FlowAccumulation[]`, `ErosionDelta[]`, `DepositionDelta[]`

#### Step 2: Downhill Flow Graph
```cpp
// Pass 1: Find lowest neighbor for each vertex (ParallelFor-safe)
ParallelFor(VertexCount, [&](int32 i) {
    if (bContinentalOnly && IsOceanic[i]) { DownhillNeighbor[i] = INDEX_NONE; return; }

    double MinElevation = VertexAmplifiedElevation[i];
    int32 LowestIdx = INDEX_NONE;
    for (Neighbor in Adjacency[i]) {
        if (VertexAmplifiedElevation[Neighbor] < MinElevation) {
            MinElevation = VertexAmplifiedElevation[Neighbor];
            LowestIdx = Neighbor;
        }
    }
    DownhillNeighbor[i] = LowestIdx;
});
```

#### Step 3: Flow Accumulation
```cpp
// Topological sort from peaks to sea (must be serial - watershed flows downstream)
FlowAccumulation.Init(1.0, VertexCount); // Each vertex contributes its area
for (int32 i : TopologicalOrder) { // Sorted highest-to-lowest elevation
    int32 Downstream = DownhillNeighbor[i];
    if (Downstream != INDEX_NONE) {
        FlowAccumulation[Downstream] += FlowAccumulation[i];
    }
}
```

#### Step 4: Stream Power Erosion
```cpp
// Pass 2: Compute erosion/deposition (ParallelFor-safe, write to separate buffers)
ParallelFor(VertexCount, [&](int32 i) {
    double Slope = ComputeVertexSlope(i); // Already exists
    double Discharge = FlowAccumulation[i];

    // Stream power law: E = K * A^m * S^n
    double ErosionRate = 0.002 * pow(Discharge, 0.5) * pow(Slope, 1.0);
    ErosionDelta[i] = ErosionRate * DeltaTimeMy;

    // Deposit 50% in downstream neighbor
    int32 Downstream = DownhillNeighbor[i];
    if (Downstream != INDEX_NONE) {
        DepositionDelta[Downstream] += ErosionDelta[i] * 0.5;
    }
});

// Pass 3: Apply deltas (serial, safe)
for (int32 i = 0; i < VertexCount; ++i) {
    VertexAmplifiedElevation[i] -= ErosionDelta[i];
    VertexAmplifiedElevation[i] += DepositionDelta[i];
}
```

#### Step 5: Integration in AdvanceSteps()
```cpp
// After line 1229 (after continental amplification):
if (Parameters.bEnableHydraulicErosion) {
    Profile.HydraulicMs = MeasureMs([&]() {
        ApplyHydraulicErosion(StepDurationMy);
    });
}
```

---

### Task 3.2: Erosion-Age Coupling (2 days)

#### Step 1: Age-Based Erosion Multiplier
```cpp
// In ApplyHydraulicErosion, scale K by orogeny age:
double AgeFactor = 1.0;
if (OrogenyAge < 20.0) AgeFactor = 0.3;      // Young: sharp peaks
else if (OrogenyAge < 100.0) AgeFactor = 1.0; // Medium: moderate
else AgeFactor = 2.0;                         // Old: heavily eroded

double K_adjusted = 0.002 * AgeFactor;
```

#### Step 2: Update Continental Amplification
```cpp
// In ComputeContinentalAmplification, reduce exemplar intensity for old mountains:
if (OrogenyAge > 100.0 && TerrainType == OldMountains) {
    AmplifiedDetail *= 0.5; // Smoother, more eroded appearance
}
```

---

### Task 3.3: Automation Tests

#### HydraulicRoutingTest.cpp:
- Create synthetic mountain range (5km peak)
- Run 100 steps with hydraulic erosion
- Assert: Valley depth >500m at base
- Assert: Mass conservation (erosion ≈ deposition within 5%)
- Assert: Performance <8ms at L6

#### ErosionCouplingTest.cpp:
- Young orogeny (<20 My): Verify peak height retained >90%
- Old orogeny (>100 My): Verify peak height reduced >30%

---

## Performance Safeguards

1. **Continental-only flag**: Skip oceanic vertices if budget exceeded
2. **Iteration limit**: Cap topological sort at 100k iterations
3. **Profiling integration**: Add `HydraulicMs` to `FStageBProfile`

---

## Files to Create/Modify

### New:
- `Source/PlanetaryCreationEditor/Private/HydraulicErosion.cpp`
- `Source/PlanetaryCreationEditor/Private/Tests/HydraulicRoutingTest.cpp`
- `Source/PlanetaryCreationEditor/Private/Tests/ErosionCouplingTest.cpp`

### Modify:
- `TectonicSimulationService.h` - Add `ApplyHydraulicErosion()` declaration, `bEnableHydraulicErosion` parameter
- `TectonicSimulationService.cpp` - Call hydraulic erosion after Stage B, add profiling
- `SPTectonicToolPanel.cpp` - Add "Enable Hydraulic Erosion" checkbox
- `FStageBProfile` - Add `HydraulicMs` timing field

---

## Acceptance Criteria
✅ River valleys visible in mountain ranges at L6+
✅ Mass conservation within 5%
✅ Performance <8ms at L6 (continental-only if needed)
✅ Young mountains stay sharp, old mountains erode
✅ Automation tests pass

## Implementation Notes (Oct 2025)
- Hydraulic routing/erosion lives in `UTectonicSimulationService::ApplyHydraulicErosion` (new `HydraulicErosion.cpp`) and executes immediately after Stage B amplification with per-step `Hydraulic` timing reported in `FStageBProfile`.
- Internal SoA buffers (`HydraulicDownhillNeighbor`, `HydraulicFlowAccumulation`, `HydraulicErosionBuffer`, `HydraulicSelfDepositBuffer`, `HydraulicDownstreamDepositBuffer`) are persisted on the service to avoid reallocations, and flow accumulation now uses a linear-time topological queue (no per-step sort).
- Stream-power law uses configurable parameters (`HydraulicErosionConstant`, `HydraulicAreaExponent`, `HydraulicSlopeExponent`, `HydraulicDownstreamDepositRatio`), with age-based scaling: <20 My → 0.3×, 20–100 My → 1×, >100 My → 2×.
- Mass is conserved by splitting eroded material between the source vertex and its downhill neighbour; any vertex without a downhill target tracks lost-to-ocean totals for diagnostics.
- `SPTectonicToolPanel` exposes an “Enable hydraulic erosion” checkbox (mirrors `r.PlanetaryCreation.EnableHydraulicErosion`), and paper defaults ship with the pass enabled.
- GPU port is deferred indefinitely: the optimised CPU implementation sits at ~1.7 ms (LOD 7), well inside the 8 ms budget, so `r.PlanetaryCreation.UseGPUHydraulic` currently routes to the CPU path.

---

## Technical Notes from M6 Plan

From `Docs/Milestone6_Plan.md` Task 3.1:

- **Stream power erosion law**: E = K * A^m * S^n
  - K = 0.002 (erodibility constant, m/My)
  - A = drainage area (flow accumulation)
  - m = 0.5 (area exponent)
  - n = 1.0 (slope exponent)

- **Alternative: Hierarchical routing** (if full routing too slow)
  1. Build coarse flow graph on L3 mesh (642 vertices)
  2. Interpolate flow directions to fine mesh (L6: 40,962 vertices)
  3. Refine locally within drainage basins
  - Expected speedup: 3-5× (trades accuracy for performance)

---

## Current M5 Erosion System (Reference)

The existing `ContinentalErosion.cpp` implements simple slope-based erosion:
- Formula: `ErosionRate = k × Slope × (Elevation - SeaLevel) × ThermalFactor × StressFactor`
- Operates on **base elevation** (VertexElevationValues)
- No flow routing or drainage basins

**M6 Hydraulic Erosion** will:
- Operate on **amplified elevation** (VertexAmplifiedElevation)
- Add flow routing and drainage basin simulation
- Create realistic river valleys through Stage B terrain
- Preserve existing M5 erosion for base elevation weathering

---

## Integration with Existing Systems

### Stage B Amplification Flow:
1. Base elevation → Oceanic amplification (Gabor noise, transform faults)
2. Base elevation → Continental amplification (exemplar blending)
3. **[NEW]** → Hydraulic erosion (valley carving)
4. Final amplified elevation → Mesh visualization

### Profiling Integration:
```cpp
struct FStageBProfile {
    double BaselineMs = 0.0;
    double RidgeMs = 0.0;
    double OceanicCPUMs = 0.0;
    double OceanicGPUMs = 0.0;
    double ContinentalCPUMs = 0.0;
    double ContinentalGPUMs = 0.0;
    double HydraulicMs = 0.0;      // [NEW]
    double GpuReadbackMs = 0.0;
    // ...
};
```

---

## Next Steps

1. Create `HydraulicErosion.cpp` with downhill flow graph
2. Implement flow accumulation with topological sort
3. Add stream power erosion law
4. Integrate into AdvanceSteps() after Stage B
5. Add UI toggle in tool panel
6. Write automation tests
7. Profile at L6/L7 and tune performance

---

**End of Plan**
