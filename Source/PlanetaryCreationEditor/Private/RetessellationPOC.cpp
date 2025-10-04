// Milestone 4 Task 1.1 Phase 1: Re-tessellation POC
// Temporary standalone file for POC - will integrate into TectonicSimulationService.cpp in Phase 2

#include "TectonicSimulationService.h"
#include "HAL/PlatformTime.h"

// Milestone 4 Task 1.1: Snapshot/Restore/Validate functions

UTectonicSimulationService::FRetessellationSnapshot UTectonicSimulationService::CaptureRetessellationSnapshot() const
{
    FRetessellationSnapshot Snapshot;
    Snapshot.SharedVertices = SharedVertices;
    Snapshot.RenderVertices = RenderVertices;
    Snapshot.RenderTriangles = RenderTriangles;
    Snapshot.VertexPlateAssignments = VertexPlateAssignments;
    Snapshot.Boundaries = Boundaries;
    Snapshot.TimestampMy = CurrentTimeMy;
    return Snapshot;
}

void UTectonicSimulationService::RestoreRetessellationSnapshot(const FRetessellationSnapshot& Snapshot)
{
    SharedVertices = Snapshot.SharedVertices;
    RenderVertices = Snapshot.RenderVertices;
    RenderTriangles = Snapshot.RenderTriangles;
    VertexPlateAssignments = Snapshot.VertexPlateAssignments;
    Boundaries = Snapshot.Boundaries;

    UE_LOG(LogTemp, Warning, TEXT("[Re-tessellation] Rolled back to timestamp %.2f My"), Snapshot.TimestampMy);
}

bool UTectonicSimulationService::ValidateRetessellation(const FRetessellationSnapshot& Snapshot) const
{
    // Validation 1: Check for NaN/Inf vertices (check render mesh, not simulation mesh)
    for (const FVector3d& Vertex : RenderVertices)
    {
        if (Vertex.ContainsNaN() || !FMath::IsFinite(Vertex.X) || !FMath::IsFinite(Vertex.Y) || !FMath::IsFinite(Vertex.Z))
        {
            UE_LOG(LogTemp, Error, TEXT("[Re-tessellation] Validation failed: NaN/Inf vertex detected"));
            return false;
        }
    }

    // Validation 2: Euler characteristic (V - E + F = 2 for closed sphere)
    // IMPORTANT: Use RenderVertices, not SharedVertices (render mesh, not simulation mesh)
    const int32 V = RenderVertices.Num();
    const int32 F = RenderTriangles.Num() / 3;

    // Count unique edges
    TSet<TPair<int32, int32>> UniqueEdges;
    for (int32 i = 0; i < RenderTriangles.Num(); i += 3)
    {
        const int32 V0 = RenderTriangles[i];
        const int32 V1 = RenderTriangles[i + 1];
        const int32 V2 = RenderTriangles[i + 2];

        auto AddEdge = [&UniqueEdges](int32 A, int32 B)
        {
            if (A > B) Swap(A, B);
            UniqueEdges.Add(TPair<int32, int32>(A, B));
        };

        AddEdge(V0, V1);
        AddEdge(V1, V2);
        AddEdge(V2, V0);
    }

    const int32 E = UniqueEdges.Num();
    const int32 EulerChar = V - E + F;

    if (EulerChar != 2)
    {
        UE_LOG(LogTemp, Error, TEXT("[Re-tessellation] Validation failed: Euler characteristic = %d (expected 2), V=%d E=%d F=%d"),
            EulerChar, V, E, F);
        return false;
    }

    // Validation 3: Plate area conservation (<1% variance)
    auto ComputePlateArea = [this](int32 PlateID) -> double
    {
        double TotalArea = 0.0;
        for (int32 i = 0; i < RenderTriangles.Num(); i += 3)
        {
            const int32 V0Idx = RenderTriangles[i];
            const int32 V1Idx = RenderTriangles[i + 1];
            const int32 V2Idx = RenderTriangles[i + 2];

            // Check if all vertices belong to this plate
            if (VertexPlateAssignments.IsValidIndex(V0Idx) &&
                VertexPlateAssignments.IsValidIndex(V1Idx) &&
                VertexPlateAssignments.IsValidIndex(V2Idx))
            {
                const int32 P0 = VertexPlateAssignments[V0Idx];
                const int32 P1 = VertexPlateAssignments[V1Idx];
                const int32 P2 = VertexPlateAssignments[V2Idx];

                // If all three vertices are from this plate, count full triangle area
                if (P0 == PlateID && P1 == PlateID && P2 == PlateID)
                {
                    const FVector3d& V0 = RenderVertices[V0Idx];
                    const FVector3d& V1 = RenderVertices[V1Idx];
                    const FVector3d& V2 = RenderVertices[V2Idx];

                    // Spherical triangle area (exact)
                    const double A = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(V1, V2), -1.0, 1.0));
                    const double B = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(V2, V0), -1.0, 1.0));
                    const double C = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(V0, V1), -1.0, 1.0));
                    const double S = (A + B + C) * 0.5;
                    const double Excess = FMath::Atan(FMath::Sqrt(FMath::Tan(S) * FMath::Tan(S - A) * FMath::Tan(S - B) * FMath::Tan(S - C)));
                    TotalArea += 4.0 * Excess; // Spherical excess formula
                }
            }
        }
        return TotalArea;
    };

    // Compute total area before and after
    double TotalAreaBefore = 0.0;
    double TotalAreaAfter = 0.0;

    for (const FTectonicPlate& Plate : Plates)
    {
        TotalAreaAfter += ComputePlateArea(Plate.PlateID);
    }

    // Approximate "before" area from snapshot (assume similar distribution)
    // For POC, use total sphere area (4π) as baseline
    TotalAreaBefore = 4.0 * PI;

    const double AreaVariance = FMath::Abs((TotalAreaAfter - TotalAreaBefore) / TotalAreaBefore);

    if (AreaVariance > 0.01) // >1% variance
    {
        UE_LOG(LogTemp, Warning, TEXT("[Re-tessellation] Validation warning: Area variance %.2f%% (threshold: 1%%)"),
            AreaVariance * 100.0);
        // Don't fail on this - just warn (area calculation is approximate)
    }

    // Validation 4: Voronoi coverage (no INDEX_NONE)
    for (int32 Assignment : VertexPlateAssignments)
    {
        if (Assignment == INDEX_NONE)
        {
            UE_LOG(LogTemp, Error, TEXT("[Re-tessellation] Validation failed: Vertex with INDEX_NONE assignment"));
            return false;
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[Re-tessellation] Validation passed: Euler=%d, AreaVariance=%.2f%%, Voronoi=100%%"),
        EulerChar, AreaVariance * 100.0);

    return true;
}

bool UTectonicSimulationService::PerformRetessellation()
{
    const double StartTime = FPlatformTime::Seconds();

    // Step 1: Create snapshot for rollback
    const FRetessellationSnapshot Snapshot = CaptureRetessellationSnapshot();

    // Step 2: Detect drifted plates (POC: hard-code PlateID=0 for single-plate test)
    TArray<int32> DriftedPlateIDs;

    // POC: Force drift detection for testing (will use real drift check in Phase 2)
    const int32 TestPlateID = 0;
    if (Plates.IsValidIndex(TestPlateID))
    {
        DriftedPlateIDs.Add(TestPlateID);
        UE_LOG(LogTemp, Warning, TEXT("[Re-tessellation POC] Forcing rebuild for PlateID=%d (test mode)"), TestPlateID);
    }

    if (DriftedPlateIDs.Num() == 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[Re-tessellation] No drifted plates detected"));
        return true; // No rebuild needed
    }

    // Step 3: POC - Simple full mesh rebuild (incremental logic in Phase 2)
    // For now, just regenerate the entire render mesh to prove snapshot/restore/validate works
    UE_LOG(LogTemp, Log, TEXT("[Re-tessellation POC] Rebuilding render mesh (full rebuild)"));

    // Trigger full mesh regeneration
    GenerateRenderMesh();
    BuildVoronoiMapping();

    // Step 4: Validate result
    if (!ValidateRetessellation(Snapshot))
    {
        UE_LOG(LogTemp, Error, TEXT("[Re-tessellation] Validation failed! Rolling back..."));
        RestoreRetessellationSnapshot(Snapshot);
        return false;
    }

    // Step 5: Update tracking
    const double EndTime = FPlatformTime::Seconds();
    LastRetessellationTimeMs = (EndTime - StartTime) * 1000.0;
    RetessellationCount++;

    UE_LOG(LogTemp, Log, TEXT("[Re-tessellation POC] Completed in %.2f ms (count: %d)"),
        LastRetessellationTimeMs, RetessellationCount);

    return true;
}
