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

    // Validation 3: Total sphere area conservation (<1% variance)
    // Simpler approach: Check that total mesh area ≈ 4π (surface area of unit sphere)
    double TotalMeshArea = 0.0;
    for (int32 i = 0; i < RenderTriangles.Num(); i += 3)
    {
        const int32 V0Idx = RenderTriangles[i];
        const int32 V1Idx = RenderTriangles[i + 1];
        const int32 V2Idx = RenderTriangles[i + 2];

        if (RenderVertices.IsValidIndex(V0Idx) &&
            RenderVertices.IsValidIndex(V1Idx) &&
            RenderVertices.IsValidIndex(V2Idx))
        {
            const FVector3d& V0 = RenderVertices[V0Idx];
            const FVector3d& V1 = RenderVertices[V1Idx];
            const FVector3d& V2 = RenderVertices[V2Idx];

            // Spherical triangle area using Girard's theorem: E = α + β + γ - π
            // where α, β, γ are the angles at each vertex of the spherical triangle
            // For unit sphere, Area = E (spherical excess in steradians)

            // Normalize vertices (should already be normalized, but ensure it)
            const FVector3d N0 = V0.GetSafeNormal();
            const FVector3d N1 = V1.GetSafeNormal();
            const FVector3d N2 = V2.GetSafeNormal();

            // Calculate angles using dot products (clamped to avoid NaN from acos)
            const double CosA = FMath::Clamp(FVector3d::DotProduct(N1, N2), -1.0, 1.0);
            const double CosB = FMath::Clamp(FVector3d::DotProduct(N2, N0), -1.0, 1.0);
            const double CosC = FMath::Clamp(FVector3d::DotProduct(N0, N1), -1.0, 1.0);

            // Arc lengths (sides of spherical triangle)
            const double a = FMath::Acos(CosA);
            const double b = FMath::Acos(CosB);
            const double c = FMath::Acos(CosC);

            // Skip degenerate triangles
            if (a < SMALL_NUMBER || b < SMALL_NUMBER || c < SMALL_NUMBER)
                continue;

            // Compute spherical angles at vertices using spherical law of cosines
            const double CosAlpha = (FMath::Cos(a) - FMath::Cos(b) * FMath::Cos(c)) / (FMath::Sin(b) * FMath::Sin(c));
            const double CosBeta = (FMath::Cos(b) - FMath::Cos(c) * FMath::Cos(a)) / (FMath::Sin(c) * FMath::Sin(a));
            const double CosGamma = (FMath::Cos(c) - FMath::Cos(a) * FMath::Cos(b)) / (FMath::Sin(a) * FMath::Sin(b));

            const double Alpha = FMath::Acos(FMath::Clamp(CosAlpha, -1.0, 1.0));
            const double Beta = FMath::Acos(FMath::Clamp(CosBeta, -1.0, 1.0));
            const double Gamma = FMath::Acos(FMath::Clamp(CosGamma, -1.0, 1.0));

            // Spherical excess (Girard's theorem)
            const double SphericalExcess = Alpha + Beta + Gamma - PI;

            // Area equals excess for unit sphere
            TotalMeshArea += SphericalExcess;
        }
    }

    const double ExpectedSphereArea = 4.0 * PI;
    const double AreaVariance = FMath::Abs((TotalMeshArea - ExpectedSphereArea) / ExpectedSphereArea);

    if (AreaVariance > 0.01) // >1% variance
    {
        UE_LOG(LogTemp, Warning, TEXT("[Re-tessellation] Validation warning: Mesh area %.4f sr (expected %.4f sr, variance %.2f%%)"),
            TotalMeshArea, ExpectedSphereArea, AreaVariance * 100.0);
        // Don't fail on this - just warn
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

    UE_LOG(LogTemp, Log, TEXT("[Re-tessellation] Validation passed: Euler=%d, MeshArea=%.4f sr, AreaVariance=%.2f%%, Voronoi=100%%"),
        EulerChar, TotalMeshArea, AreaVariance * 100.0);

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

    // Refresh derived fields (velocity, stress) after Voronoi rebuild
    ComputeVelocityField();
    InterpolateStressToVertices();

    // Step 4: Validate result
    if (!ValidateRetessellation(Snapshot))
    {
        UE_LOG(LogTemp, Error, TEXT("[Re-tessellation] Validation failed! Rolling back..."));
        RestoreRetessellationSnapshot(Snapshot);
        return false;
    }

    // Step 5: Reset initial centroids for drifted plates (prevent accumulation)
    // CRITICAL: After successful rebuild, update reference positions so next drift check is relative to NEW positions
    for (int32 PlateID : DriftedPlateIDs)
    {
        for (int32 i = 0; i < Plates.Num(); ++i)
        {
            if (Plates[i].PlateID == PlateID && InitialPlateCentroids.IsValidIndex(i))
            {
                InitialPlateCentroids[i] = Plates[i].Centroid;
                UE_LOG(LogTemp, Verbose, TEXT("[Re-tessellation] Reset reference centroid for Plate %d"), PlateID);
            }
        }
    }

    // Step 6: Update tracking
    const double EndTime = FPlatformTime::Seconds();
    LastRetessellationTimeMs = (EndTime - StartTime) * 1000.0;
    RetessellationCount++;

    UE_LOG(LogTemp, Log, TEXT("[Re-tessellation] Completed in %.2f ms (count: %d, plates rebuilt: %d)"),
        LastRetessellationTimeMs, RetessellationCount, DriftedPlateIDs.Num());

    return true;
}
