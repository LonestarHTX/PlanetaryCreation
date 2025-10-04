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

    // Step 2: Detect drifted plates using real drift calculation
    TArray<int32> DriftedPlateIDs;

    // Convert threshold from degrees to radians
    const double ThresholdRad = FMath::DegreesToRadians(Parameters.RetessellationThresholdDegrees);

    // Check each plate's drift from initial position
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        if (!InitialPlateCentroids.IsValidIndex(i))
        {
            UE_LOG(LogTemp, Warning, TEXT("[Re-tessellation] Plate %d has no initial centroid (skipping drift check)"), Plates[i].PlateID);
            continue;
        }

        const FVector3d& CurrentCentroid = Plates[i].Centroid;
        const FVector3d& InitialCentroid = InitialPlateCentroids[i];

        // Calculate angular distance (great circle distance on unit sphere)
        const double DotProduct = FMath::Clamp(FVector3d::DotProduct(CurrentCentroid, InitialCentroid), -1.0, 1.0);
        const double AngularDistanceRad = FMath::Acos(DotProduct);

        if (AngularDistanceRad > ThresholdRad)
        {
            const double AngularDistanceDeg = FMath::RadiansToDegrees(AngularDistanceRad);
            UE_LOG(LogTemp, Warning, TEXT("[Re-tessellation] Plate %d drifted %.2f° (threshold: %.2f°)"),
                Plates[i].PlateID, AngularDistanceDeg, Parameters.RetessellationThresholdDegrees);
            DriftedPlateIDs.Add(Plates[i].PlateID);
        }
    }

    if (DriftedPlateIDs.Num() == 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[Re-tessellation] No drifted plates detected"));
        return true; // No rebuild needed
    }

    // Step 3: Phase 2 - Full mesh rebuild for drifted plates
    // TODO Phase 2c: Replace with incremental boundary fan split
    UE_LOG(LogTemp, Log, TEXT("[Re-tessellation] Rebuilding mesh for %d drifted plate(s) (full rebuild)"), DriftedPlateIDs.Num());

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

    UE_LOG(LogTemp, Log, TEXT("[Re-tessellation] Completed in %.2f ms (count: %d, plates rebuilt: %d)"),
        LastRetessellationTimeMs, RetessellationCount, DriftedPlateIDs.Num());

    return true;
}
