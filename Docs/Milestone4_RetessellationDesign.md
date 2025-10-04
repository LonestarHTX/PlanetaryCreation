# Re-tessellation Design Document

**Milestone:** 4
**Task:** 1.0 Design Spike
**Author:** Simulation Engineer
**Date:** October 4, 2025
**Status:** DRAFT - Awaiting Approval

---

## Problem Statement

Milestone 3 implements static plate tessellation using a fixed icosphere mesh. As plates drift via Euler pole rotation, the **render mesh boundaries** (defined by initial Voronoi cell edges) diverge from the **true plate boundaries** (defined by drifted centroids). After ~30° of drift, visual artifacts become apparent and stress/velocity fields become inaccurate.

**Goal:** Implement incremental re-tessellation that rebuilds render mesh geometry to track drifting plate boundaries without full simulation reset.

---

## Requirements

### Functional
1. **Drift Detection:** Monitor plate centroid displacement from initial position (captured after Lloyd relaxation)
2. **Incremental Rebuild:** When drift > threshold (30°), rebuild affected regions only (not full mesh)
3. **Determinism:** Same seed → same tessellation outcome (critical for automation tests)
4. **Area Conservation:** Total plate area variance < 1% after rebuild
5. **Topology Validation:** Euler characteristic (V - E + F = 2) maintained
6. **Rollback Safety:** If rebuild fails validation, revert to last-good state

### Performance
7. **Rebuild Budget:** <50ms for incremental rebuild (single drifted plate)
8. **Cache Invalidation:** Minimal impact on Voronoi/stress/velocity caches
9. **Async Compatible:** Rebuild can occur on background thread (snapshot pattern from M3)

### Non-Requirements (Deferred to M5+)
- Full Delaunay optimization (accept suboptimal triangles if deterministic)
- GPU-accelerated tessellation
- Smooth animation of boundary movement (instant snap acceptable)

---

## Algorithm Survey

### Option A: Full Mesh Rebuild (Baseline)
**Description:** Regenerate entire icosphere from scratch using drifted centroids as Voronoi seeds.

**Pros:**
- Simple implementation (reuse M3 generation code)
- Guaranteed valid topology
- Deterministic (seed-based)

**Cons:**
- Expensive: O(N) rebuild even for single plate (~100ms at level 3)
- Invalidates all caches (Voronoi, stress, velocity)
- Flickers during rebuild (mesh fully destroyed/recreated)

**Verdict:** ❌ Fallback only if incremental fails

---

### Option B: Constrained Delaunay Triangulation
**Description:** Insert new boundary vertices and retriangulate affected regions using Bowyer-Watson algorithm.

**Pros:**
- Optimal triangle quality (close to equilateral)
- Well-studied algorithm with proven correctness

**Cons:**
- Complex implementation (edge flips, cavity removal)
- Non-deterministic without careful tie-breaking rules
- Requires spatial index (Delaunay tree) maintenance
- O(N log N) worst case for boundary vertex insertion

**References:**
- Bowyer, A. (1981). "Computing Dirichlet tessellations"
- Watson, D. F. (1981). "Computing the n-dimensional Delaunay tessellation"

**Verdict:** ⚠️ Too complex for M4 (revisit in M5 for quality optimization)

---

### Option C: Boundary Edge Fan Split (SELECTED)
**Description:** Detect edges crossing drifted boundaries, split them at intersection points, reconnect to new plate centroids using triangle fans.

**Algorithm:**
1. **Detect Drifted Plates:** Check `FVector3d::DotProduct(InitialCentroid, CurrentCentroid)` → if arccos > threshold, mark for rebuild
2. **Find Boundary Crossings:** For each drifted plate, iterate adjacent boundaries:
   - Get shared edge vertices V0, V1 (from `FPlateBoundary::SharedEdgeVertices`)
   - Compute midpoint M = (V0 + V1).Normalized()
   - Check if M is on correct side of new boundary (dot product with normal)
   - If crossed, split edge at new intersection point
3. **Insert Boundary Vertices:** Add new vertices at intersection points to `SharedVertices`
4. **Rebuild Triangle Fan:** For each drifted plate:
   - Remove old triangles touching plate boundary
   - Create new fan from plate centroid to boundary edge loop
   - Update `RenderTriangles` array
5. **Validate:** Check Euler characteristic and plate area

**Pros:**
- Simple: No complex spatial structures
- Fast: O(B) where B = boundary count (~30 for 20 plates)
- Deterministic: Process boundaries in sorted PlateID order
- Incremental: Only touches drifted regions

**Cons:**
- Suboptimal triangles (long/thin at boundaries)
- Requires boundary adjacency map (already in M3)
- Edge snapping tolerance needed (epsilon)

**Complexity Analysis:**
- Boundary detection: O(P) where P = plate count (20) → ~0.1ms
- Edge crossing check: O(B) where B = boundary count (30) → ~0.3ms
- Fan rebuild: O(V) where V = vertices per plate (~32 at level 3) → ~5ms per plate
- **Total:** ~6ms for single plate, ~30ms for 5 plates

**Verdict:** ✅ **SELECTED** - Best balance of simplicity, performance, determinism

---

## Detailed Design: Boundary Edge Fan Split

### Data Structures

```cpp
/** Captures pre-rebuild state for rollback. */
struct FRetessellationSnapshot
{
    TArray<FVector3d> SharedVertices;
    TArray<FVector3d> RenderVertices;
    TArray<int32> RenderTriangles;
    TArray<int32> VertexPlateAssignments;
    TMap<TPair<int32, int32>, FPlateBoundary> Boundaries;
    double Timestamp;
};

/** Tracks a boundary edge crossing needing rebuild. */
struct FBoundaryCrossing
{
    int32 PlateA_ID;
    int32 PlateB_ID;
    int32 EdgeV0Index; // Index into SharedVertices
    int32 EdgeV1Index;
    FVector3d IntersectionPoint; // New boundary point on unit sphere
};
```

### Algorithm Steps (Pseudocode)

```cpp
bool UTectonicSimulationService::PerformRetessellation()
{
    // Step 1: Create snapshot for rollback
    FRetessellationSnapshot Snapshot = CaptureRetessellationSnapshot();

    // Step 2: Detect drifted plates (deterministic order)
    TArray<int32> DriftedPlateIDs;
    for (const FTectonicPlate& Plate : Plates)
    {
        if (!InitialPlateCentroids.IsValidIndex(Plate.PlateID))
            continue;

        const FVector3d& InitialCentroid = InitialPlateCentroids[Plate.PlateID];
        const double AngularDistance = FMath::Acos(FMath::Clamp(
            FVector3d::DotProduct(InitialCentroid, Plate.Centroid), -1.0, 1.0));

        if (FMath::RadiansToDegrees(AngularDistance) > Parameters.RetessellationThresholdDegrees)
        {
            DriftedPlateIDs.Add(Plate.PlateID);
        }
    }

    if (DriftedPlateIDs.Num() == 0)
        return true; // No rebuild needed

    UE_LOG(LogTemp, Warning, TEXT("Re-tessellation triggered for %d drifted plates"), DriftedPlateIDs.Num());

    // Step 3: Find boundary crossings (sorted order for determinism)
    TArray<FBoundaryCrossing> Crossings;
    for (int32 PlateID : DriftedPlateIDs)
    {
        FindBoundaryCrossings(PlateID, Crossings);
    }

    // Step 4: Insert new boundary vertices
    TMap<TPair<int32, int32>, int32> NewBoundaryVertices; // Key: (PlateA, PlateB) → VertexIndex
    for (const FBoundaryCrossing& Crossing : Crossings)
    {
        const TPair<int32, int32> Key = MakeSortedPair(Crossing.PlateA_ID, Crossing.PlateB_ID);
        if (!NewBoundaryVertices.Contains(Key))
        {
            const int32 NewVertexIndex = SharedVertices.Add(Crossing.IntersectionPoint);
            NewBoundaryVertices.Add(Key, NewVertexIndex);
        }
    }

    // Step 5: Rebuild triangle fans for drifted plates
    for (int32 PlateID : DriftedPlateIDs)
    {
        RebuildPlateFan(PlateID, NewBoundaryVertices);
    }

    // Step 6: Regenerate render mesh from updated SharedVertices
    GenerateRenderMesh();

    // Step 7: Rebuild Voronoi mapping (plate assignments may have changed)
    BuildVoronoiMapping();

    // Step 8: Validate result
    if (!ValidateRetessellation(Snapshot))
    {
        UE_LOG(LogTemp, Error, TEXT("Re-tessellation validation failed! Rolling back..."));
        RestoreRetessellationSnapshot(Snapshot);
        return false;
    }

    // Step 9: Update initial centroids (new baseline)
    InitialPlateCentroids.SetNum(Plates.Num());
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        InitialPlateCentroids[i] = Plates[i].Centroid;
    }

    UE_LOG(LogTemp, Log, TEXT("Re-tessellation completed successfully"));
    return true;
}
```

### Boundary Crossing Detection

```cpp
void UTectonicSimulationService::FindBoundaryCrossings(int32 PlateID, TArray<FBoundaryCrossing>& OutCrossings)
{
    const FTectonicPlate* DriftedPlate = Plates.FindByPredicate([PlateID](const FTectonicPlate& P)
    {
        return P.PlateID == PlateID;
    });

    if (!DriftedPlate)
        return;

    // Iterate all boundaries touching this plate (deterministic order)
    TArray<TPair<int32, int32>> BoundaryKeys;
    Boundaries.GenerateKeyArray(BoundaryKeys);
    BoundaryKeys.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
    {
        return A.Key < B.Key || (A.Key == B.Key && A.Value < B.Value);
    });

    for (const TPair<int32, int32>& Key : BoundaryKeys)
    {
        if (Key.Key != PlateID && Key.Value != PlateID)
            continue; // Not our boundary

        const FPlateBoundary& Boundary = Boundaries[Key];
        if (Boundary.SharedEdgeVertices.Num() < 2)
            continue;

        const int32 V0Index = Boundary.SharedEdgeVertices[0];
        const int32 V1Index = Boundary.SharedEdgeVertices[1];

        const FVector3d& V0 = SharedVertices[V0Index];
        const FVector3d& V1 = SharedVertices[V1Index];
        const FVector3d EdgeMidpoint = ((V0 + V1) * 0.5).GetSafeNormal();

        // Check if midpoint is on correct side of new boundary
        const FVector3d BoundaryNormal = ComputeBoundaryNormal(Key, DriftedPlate->Centroid);
        const double Dot = FVector3d::DotProduct(EdgeMidpoint, BoundaryNormal);

        // If sign changed, edge crossed boundary
        constexpr double EPSILON = 1e-6;
        if (FMath::Abs(Dot) < EPSILON)
        {
            // Edge crosses boundary - compute new intersection point
            FBoundaryCrossing Crossing;
            Crossing.PlateA_ID = Key.Key;
            Crossing.PlateB_ID = Key.Value;
            Crossing.EdgeV0Index = V0Index;
            Crossing.EdgeV1Index = V1Index;
            Crossing.IntersectionPoint = ComputeBoundaryIntersection(V0, V1, BoundaryNormal);
            OutCrossings.Add(Crossing);
        }
    }
}
```

---

## Determinism Strategy

### Challenge
Floating-point arithmetic is non-associative: `(a + b) + c ≠ a + (b + c)` due to rounding. Boundary crossing checks depend on dot products and arc cosines, which accumulate error over time.

### Solution: Seed-Stable Ordering + Epsilon Tolerance

1. **Sorted Iteration:** Process plates, boundaries, and vertices in deterministic order (PlateID sort)
2. **Epsilon Tolerance:** Define `RETESS_EPSILON = 1e-6` for boundary crossing detection
3. **Stable Tie-Breaking:** If two plates have identical drift distance (within epsilon), use PlateID as tiebreaker
4. **Seed-Based RNG:** Any randomness (hotspot placement, future features) uses seeded `FRandomStream`

### Validation
- Automation test runs same seed 100 times, compares vertex positions (tolerance: `1e-5`)
- CSV export includes vertex checksums for determinism auditing

---

## Epsilon Handling

### Vertex Snapping Tolerance
**Problem:** After drift, boundary intersection points may differ by <1e-7 from original vertices due to floating-point error. Without snapping, we'd create duplicate vertices.

**Solution:**
```cpp
constexpr double VERTEX_SNAP_EPSILON = 1e-5; // ~0.0006° on unit sphere

int32 FindOrAddVertex(const FVector3d& NewVertex, TArray<FVector3d>& Vertices)
{
    // Check existing vertices for near-match
    for (int32 i = 0; i < Vertices.Num(); ++i)
    {
        const double Distance = (Vertices[i] - NewVertex).Length();
        if (Distance < VERTEX_SNAP_EPSILON)
        {
            return i; // Snap to existing vertex
        }
    }

    // Add new vertex
    return Vertices.Add(NewVertex.GetSafeNormal()); // Force unit length
}
```

### Boundary Crossing Threshold
**Problem:** Dot product near zero indicates edge crossing, but numerical error can cause spurious detections.

**Solution:**
```cpp
constexpr double CROSSING_EPSILON = 1e-6;

bool EdgeCrossesBoundary(const FVector3d& EdgeMidpoint, const FVector3d& BoundaryNormal)
{
    const double Dot = FVector3d::DotProduct(EdgeMidpoint, BoundaryNormal);
    return FMath::Abs(Dot) < CROSSING_EPSILON;
}
```

---

## Validation Checklist

After each re-tessellation, validate:

1. **Euler Characteristic:** `V - E + F = 2` (icosphere topology invariant)
   ```cpp
   const int32 V = SharedVertices.Num();
   const int32 F = RenderTriangles.Num() / 3;
   const int32 E = ComputeEdgeCount(RenderTriangles); // Count unique edges
   const int32 EulerChar = V - E + F;
   check(EulerChar == 2); // MUST be 2 for closed sphere
   ```

2. **Plate Area Conservation:**
   ```cpp
   double TotalAreaBefore = 0.0;
   for (const FTectonicPlate& Plate : SnapshotPlates)
   {
       TotalAreaBefore += ComputePlateArea(Plate);
   }

   double TotalAreaAfter = 0.0;
   for (const FTectonicPlate& Plate : Plates)
   {
       TotalAreaAfter += ComputePlateArea(Plate);
   }

   const double AreaVariance = FMath::Abs((TotalAreaAfter - TotalAreaBefore) / TotalAreaBefore);
   check(AreaVariance < 0.01); // <1% variance
   ```

3. **No NaN/Inf Vertices:**
   ```cpp
   for (const FVector3d& Vertex : SharedVertices)
   {
       check(!Vertex.ContainsNaN());
       check(FMath::IsFinite(Vertex.X) && FMath::IsFinite(Vertex.Y) && FMath::IsFinite(Vertex.Z));
   }
   ```

4. **Voronoi Coverage:** Every render vertex assigned to valid plate (no INDEX_NONE)
   ```cpp
   for (int32 Assignment : VertexPlateAssignments)
   {
       check(Assignment != INDEX_NONE);
       check(Plates.IsValidIndex(Assignment));
   }
   ```

---

## Rollback Strategy

If validation fails, restore snapshot:

```cpp
void UTectonicSimulationService::RestoreRetessellationSnapshot(const FRetessellationSnapshot& Snapshot)
{
    SharedVertices = Snapshot.SharedVertices;
    RenderVertices = Snapshot.RenderVertices;
    RenderTriangles = Snapshot.RenderTriangles;
    VertexPlateAssignments = Snapshot.VertexPlateAssignments;
    Boundaries = Snapshot.Boundaries;

    // Emit warning event for UI/logging
    OnRetessellationFailed.Broadcast(Snapshot.Timestamp);

    UE_LOG(LogTemp, Error, TEXT("Re-tessellation rolled back to timestamp %.2f My"), Snapshot.Timestamp);
}
```

**UI Indicator:** Add "Re-tessellation Failed (Rollback Active)" warning in editor panel.

---

## Performance Profile (Estimated)

### Incremental Rebuild (Single Plate)
| Operation | Time (ms) | Notes |
|-----------|-----------|-------|
| Drift detection | 0.1 | O(P) where P=20 plates |
| Boundary crossing check | 0.3 | O(B) where B=30 boundaries |
| Fan rebuild | 5.0 | O(V) where V=32 vertices/plate |
| Render mesh regen | 2.0 | Reuse M3 subdivision code |
| Voronoi rebuild | 8.0 | KD-tree from scratch (level 3: 642 verts) |
| Validation | 1.0 | Euler check + area sum |
| **Total** | **16.4 ms** | Well under 50ms budget ✅ |

### Full Rebuild (5 Plates Drifted)
- Fan rebuild: 5.0 ms × 5 = 25 ms
- Other costs same (shared boundary/validation overhead)
- **Total:** ~40 ms (still under budget) ✅

---

## Integration with M3 Systems

### Async Mesh Pipeline
Re-tessellation can use same snapshot pattern from M3 Task 4.3:
```cpp
// On game thread:
FRetessellationSnapshot Snapshot = CaptureRetessellationSnapshot();

// Kick to background thread:
AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Snapshot]()
{
    PerformRetessellationFromSnapshot(Snapshot);

    // Return to game thread for mesh update:
    AsyncTask(ENamedThreads::GameThread, [...]()
    {
        UpdatePreviewMesh(...);
    });
});
```

**Benefit:** UI remains responsive during rebuild (16ms → 0ms perceived latency)

### KD-Tree Cache Invalidation
After re-tessellation, `SphericalKDTree` must rebuild:
```cpp
void UTectonicSimulationService::BuildVoronoiMapping()
{
    // Rebuild KD-tree with drifted centroids
    TArray<FVector3d> Centroids;
    for (const FTectonicPlate& Plate : Plates)
    {
        Centroids.Add(Plate.Centroid);
    }

    PlateKDTree = MakeShared<FSphericalKDTree>(Centroids);

    // Re-assign all render vertices
    for (int32 i = 0; i < RenderVertices.Num(); ++i)
    {
        VertexPlateAssignments[i] = PlateKDTree->FindNearest(RenderVertices[i]);
    }
}
```

**Cost:** ~8ms at level 3 (acceptable, happens once per rebuild)

---

## Alternative Considered: Boundary Morphing

Instead of discrete rebuild, gradually morph boundary vertices over multiple frames.

**Pros:**
- Smooth visual transition (no snap)
- Smaller per-frame cost (<1ms amortized)

**Cons:**
- Complex state management (partially-morphed boundaries)
- Breaks determinism (frame rate dependent)
- Doesn't solve cache invalidation (Voronoi still needs rebuild)

**Verdict:** ❌ Rejected - Visual smoothness not worth complexity in M4. Revisit in M5 if needed.

---

## Implementation Phases (Task 1.1 Breakdown)

### Phase 1: Proof of Concept (Day 1-2)
- Implement single-plate rebuild (hard-code PlateID=0 for testing)
- Validate Euler characteristic and area conservation
- Confirm determinism (run 10 times, same result)

### Phase 2: Multi-Plate Integration (Day 3-4)
- Extend to handle multiple drifted plates (sorted order)
- Add rollback mechanism with snapshot
- Integration test with M3 simulation stepping

### Phase 3: Regression Testing (Day 5-6)
- Automation test: Force drift via manual centroid displacement
- CSV audit: Verify vertex checksums match across runs
- Stress test: 100 steps with re-tessellation every 10 steps
- Performance profile: Confirm <50ms budget

---

## Open Questions (Approval Gate)

1. **Should we support plate count changes?** (e.g., 20 → 19 after merge)
   - Current design: No, plate count fixed until Task 1.2 (split/merge)
   - Simplifies M4 scope, reduces risk

2. **What if >50% of plates drift simultaneously?**
   - Fallback: Full mesh rebuild (Option A) if incremental exceeds 100ms
   - Log warning, but don't fail simulation

3. **Async vs sync rebuild?**
   - Proposal: Sync for M4 (simpler), async in M5 once stable
   - Rebuild is infrequent (~every 50 steps), UI hitching acceptable

4. **Epsilon values final?**
   - VERTEX_SNAP_EPSILON = 1e-5 (~0.0006° on unit sphere)
   - CROSSING_EPSILON = 1e-6
   - Open to tuning based on test results

---

## Approval Checklist

- [ ] Gameplay Lead: Algorithm choice approved (boundary edge fan split)
- [ ] Rendering Lead: Performance budget acceptable (16ms single plate)
- [ ] Simulation Lead: Determinism strategy sufficient (seed-stable + epsilon)
- [ ] QA Lead: Validation checklist comprehensive (Euler, area, NaN)
- [ ] Tech Lead: Rollback mechanism adequate (snapshot restore)

**Sign-off required before proceeding to Task 1.1 implementation.**

---

## References

1. **Procedural Tectonic Planets Paper** - Section 3.2 (Voronoi tessellation)
2. **Milestone 3 Implementation** - `TectonicSimulationService.cpp::GenerateRenderMesh()`
3. **Bowyer-Watson Algorithm** - For future optimization (M5+)
4. **Unreal Engine Geometry Script** - Native triangulation utilities (not used due to determinism concerns)

---

**Document Version:** 1.0
**Status:** DRAFT - Awaiting Stakeholder Approval
**Next Step:** Review with leads, incorporate feedback, proceed to Task 1.1 POC
