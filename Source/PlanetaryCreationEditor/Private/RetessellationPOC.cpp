// Milestone 4 Task 1.1 Phase 1: Re-tessellation POC
// Temporary standalone file for POC - will integrate into TectonicSimulationService.cpp in Phase 2

#include "PlanetaryCreationLogging.h"
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

    // Milestone 5: Capture erosion state for rollback
    Snapshot.VertexElevationValues = VertexElevationValues;
    Snapshot.VertexErosionRates = VertexErosionRates;
    Snapshot.VertexSedimentThickness = VertexSedimentThickness;
    Snapshot.VertexCrustAge = VertexCrustAge;

    return Snapshot;
}

void UTectonicSimulationService::RestoreRetessellationSnapshot(const FRetessellationSnapshot& Snapshot)
{
    SharedVertices = Snapshot.SharedVertices;
    RenderVertices = Snapshot.RenderVertices;
    RenderTriangles = Snapshot.RenderTriangles;
    VertexPlateAssignments = Snapshot.VertexPlateAssignments;
    Boundaries = Snapshot.Boundaries;

    // Milestone 5: Restore erosion state on rollback
    VertexElevationValues = Snapshot.VertexElevationValues;
    VertexErosionRates = Snapshot.VertexErosionRates;
    VertexSedimentThickness = Snapshot.VertexSedimentThickness;
    VertexCrustAge = Snapshot.VertexCrustAge;

    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[Re-tessellation] Rolled back to timestamp %.2f My"), Snapshot.TimestampMy);
}

bool UTectonicSimulationService::ValidateRetessellation(const FRetessellationSnapshot& Snapshot) const
{
    // Validation 1: Check for NaN/Inf vertices (check render mesh, not simulation mesh)
    for (const FVector3d& Vertex : RenderVertices)
    {
        if (Vertex.ContainsNaN() || !FMath::IsFinite(Vertex.X) || !FMath::IsFinite(Vertex.Y) || !FMath::IsFinite(Vertex.Z))
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("[Re-tessellation] Validation failed: NaN/Inf vertex detected"));
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
        UE_LOG(LogPlanetaryCreation, Error, TEXT("[Re-tessellation] Validation failed: Euler characteristic = %d (expected 2), V=%d E=%d F=%d"),
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
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[Re-tessellation] Validation warning: Mesh area %.4f sr (expected %.4f sr, variance %.2f%%)"),
            TotalMeshArea, ExpectedSphereArea, AreaVariance * 100.0);
        // Don't fail on this - just warn
    }

    // Validation 4: Voronoi coverage (no INDEX_NONE)
    for (int32 Assignment : VertexPlateAssignments)
    {
        if (Assignment == INDEX_NONE)
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("[Re-tessellation] Validation failed: Vertex with INDEX_NONE assignment"));
            return false;
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[Re-tessellation] Validation passed: Euler=%d, MeshArea=%.4f sr, AreaVariance=%.2f%%, Voronoi=100%%"),
        EulerChar, TotalMeshArea, AreaVariance * 100.0);

    return true;
}

UTectonicSimulationService::FRetessellationAnalysis UTectonicSimulationService::ComputeRetessellationAnalysis() const
{
    FRetessellationAnalysis Analysis;

    if (Plates.Num() == 0 || RenderTriangles.Num() < 3)
    {
        return Analysis;
    }

    if (InitialPlateCentroids.Num() != Plates.Num())
    {
        return Analysis;
    }

    double MaxDriftRadians = 0.0;
    int32 MaxDriftPlateID = INDEX_NONE;

    for (int32 PlateIndex = 0; PlateIndex < Plates.Num(); ++PlateIndex)
    {
        const FVector3d& InitialCentroid = InitialPlateCentroids[PlateIndex];
        const FVector3d& CurrentCentroid = Plates[PlateIndex].Centroid;

        const double DotProduct = FMath::Clamp(FVector3d::DotProduct(InitialCentroid, CurrentCentroid), -1.0, 1.0);
        const double AngularDistance = FMath::Acos(DotProduct);

        if (AngularDistance > MaxDriftRadians)
        {
            MaxDriftRadians = AngularDistance;
            MaxDriftPlateID = Plates[PlateIndex].PlateID;
        }
    }

    Analysis.MaxDriftDegrees = FMath::RadiansToDegrees(MaxDriftRadians);
    Analysis.MaxDriftPlateID = MaxDriftPlateID;

    const double MinimumAngleThreshold = Parameters.RetessellationMinTriangleAngleDegrees;

    const int32 TriangleCount = RenderTriangles.Num() / 3;
    Analysis.TotalTriangleCount = TriangleCount;

    if (TriangleCount == 0)
    {
        return Analysis;
    }

    int32 BadTriangleCount = 0;

    for (int32 TriangleIndex = 0; TriangleIndex < RenderTriangles.Num(); TriangleIndex += 3)
    {
        const int32 IndexA = RenderTriangles[TriangleIndex];
        const int32 IndexB = RenderTriangles[TriangleIndex + 1];
        const int32 IndexC = RenderTriangles[TriangleIndex + 2];

        if (!RenderVertices.IsValidIndex(IndexA) ||
            !RenderVertices.IsValidIndex(IndexB) ||
            !RenderVertices.IsValidIndex(IndexC))
        {
            ++BadTriangleCount;
            continue;
        }

        const FVector3d& VertexA = RenderVertices[IndexA];
        const FVector3d& VertexB = RenderVertices[IndexB];
        const FVector3d& VertexC = RenderVertices[IndexC];

        const FVector3d EdgeAB = VertexB - VertexA;
        const FVector3d EdgeAC = VertexC - VertexA;
        const FVector3d EdgeBC = VertexC - VertexB;

        const double LengthAB = EdgeAB.Length();
        const double LengthAC = EdgeAC.Length();
        const double LengthBC = EdgeBC.Length();

        if (LengthAB < KINDA_SMALL_NUMBER ||
            LengthAC < KINDA_SMALL_NUMBER ||
            LengthBC < KINDA_SMALL_NUMBER)
        {
            ++BadTriangleCount;
            continue;
        }

        const FVector3d NormalizedAB = EdgeAB / LengthAB;
        const FVector3d NormalizedAC = EdgeAC / LengthAC;
        const FVector3d NormalizedBA = -NormalizedAB;
        const FVector3d NormalizedBC = EdgeBC / LengthBC;
        const FVector3d NormalizedCA = -NormalizedAC;
        const FVector3d NormalizedCB = -NormalizedBC;

        const double AngleA = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(NormalizedAB, NormalizedAC), -1.0, 1.0));
        const double AngleB = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(NormalizedBA, NormalizedBC), -1.0, 1.0));
        const double AngleC = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(NormalizedCA, NormalizedCB), -1.0, 1.0));

        const double MinimumAngleDegrees = FMath::RadiansToDegrees(FMath::Min3(AngleA, AngleB, AngleC));

        if (MinimumAngleDegrees < MinimumAngleThreshold)
        {
            ++BadTriangleCount;
        }
    }

    Analysis.BadTriangleCount = BadTriangleCount;
    Analysis.BadTriangleRatio = (TriangleCount > 0)
        ? static_cast<double>(BadTriangleCount) / static_cast<double>(TriangleCount)
        : 0.0;

    return Analysis;
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
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[Re-tessellation] Plate %d has no initial centroid (skipping drift check)"), Plates[i].PlateID);
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
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[Re-tessellation] Plate %d drifted %.2f° (threshold: %.2f°)"),
                Plates[i].PlateID, AngularDistanceDeg, Parameters.RetessellationThresholdDegrees);
            DriftedPlateIDs.Add(Plates[i].PlateID);
        }
    }

    if (DriftedPlateIDs.Num() == 0)
    {
        UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[Re-tessellation] No drifted plates detected"));
        return true; // No rebuild needed
    }

    // Step 3: Phase 2 - Full mesh rebuild for drifted plates
    // TODO Phase 2c: Replace with incremental boundary fan split
    UE_LOG(LogPlanetaryCreation, Log, TEXT("[Re-tessellation] Rebuilding mesh for %d drifted plate(s) (full rebuild)"), DriftedPlateIDs.Num());

    // Trigger full mesh regeneration
    GenerateRenderMesh();
    BuildVoronoiMapping();

    // Milestone 6 Fix: Refresh elevation baselines to match new plate assignments after retessellation
    // When Voronoi remaps vertices to different plates, elevation must update to reflect new crust type
    const int32 VertexCount = RenderVertices.Num();
    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const int32 PlateIdx = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;
        if (PlateIdx != INDEX_NONE && Plates.IsValidIndex(PlateIdx))
        {
            const bool bIsOceanic = (Plates[PlateIdx].CrustType == ECrustType::Oceanic);
            // Only reset elevation if it's inconsistent with current plate type
            // Oceanic should be deeply negative (abyssal plains), continental should be near sea level
            // CRITICAL: Check bounds before accessing VertexElevationValues (may not be resized yet)
            if (!VertexElevationValues.IsValidIndex(VertexIdx))
            {
                continue;
            }
            const bool bElevationMatchesType = bIsOceanic ?
                (VertexElevationValues[VertexIdx] < PaperElevationConstants::SeaLevel_m) :
                (VertexElevationValues[VertexIdx] >= PaperElevationConstants::SeaLevel_m - 500.0); // Allow some erosion below sea level
            if (!bElevationMatchesType)
            {
                // Use paper-compliant baselines
                VertexElevationValues[VertexIdx] = bIsOceanic ?
                    PaperElevationConstants::AbyssalPlainDepth_m :
                    PaperElevationConstants::ContinentalBaseline_m;
                // Also reset amplified elevation to match base (Stage B will recompute on next step)
                if (VertexAmplifiedElevation.IsValidIndex(VertexIdx))
                {
                    VertexAmplifiedElevation[VertexIdx] = VertexElevationValues[VertexIdx];
                }
            }
        }
    }

    // Refresh derived fields (velocity, stress) after Voronoi rebuild
    ComputeVelocityField();
    InterpolateStressToVertices();

    // Step 4: Validate result
    if (!ValidateRetessellation(Snapshot))
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("[Re-tessellation] Validation failed! Rolling back..."));
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
                UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[Re-tessellation] Reset reference centroid for Plate %d"), PlateID);
            }
        }
    }

    // Step 6: Update tracking
    const double EndTime = FPlatformTime::Seconds();
    LastRetessellationTimeMs = (EndTime - StartTime) * 1000.0;
    RetessellationCount++;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[Re-tessellation] Completed in %.2f ms (count: %d, plates rebuilt: %d)"),
        LastRetessellationTimeMs, RetessellationCount, DriftedPlateIDs.Num());

    // Milestone 4 Phase 4.2: Increment topology version (topology changed)
    TopologyVersion++;
    UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[LOD Cache] Topology version incremented: %d"), TopologyVersion);
    MarkAllRidgeDirectionsDirty();

    RetessellationCadenceStats.StepsSinceLastTrigger = 0;
    RetessellationCadenceStats.CurrentCooldownStepAccumulator = 0;

    return true;
}

void UTectonicSimulationService::MaybePerformRetessellation()
{
    const int32 EvaluationInterval = FMath::Max(1, Parameters.RetessellationCheckIntervalSteps);

    if (InitialPlateCentroids.Num() != Plates.Num())
    {
        InitialPlateCentroids.SetNum(Plates.Num());
        for (int32 PlateIndex = 0; PlateIndex < Plates.Num(); ++PlateIndex)
        {
            InitialPlateCentroids[PlateIndex] = Plates[PlateIndex].Centroid;
        }

        StepsSinceLastRetessellationCheck = 0;
        bRetessellationInCooldown = false;
        LastRetessellationMaxDriftDegrees = 0.0;
        LastRetessellationBadTriangleRatio = 0.0;
        RetessellationCadenceStats.StepsSinceLastTrigger = 0;
        RetessellationCadenceStats.CurrentCooldownStepAccumulator = 0;
        return;
    }

    StepsSinceLastRetessellationCheck++;

    RetessellationCadenceStats.StepsObserved++;

    if (bRetessellationInCooldown)
    {
        RetessellationCadenceStats.StepsSpentInCooldown++;
        if (RetessellationCadenceStats.CurrentCooldownStepAccumulator < TNumericLimits<int32>::Max())
        {
            RetessellationCadenceStats.CurrentCooldownStepAccumulator++;
        }
    }

    if (RetessellationCadenceStats.StepsSinceLastTrigger < TNumericLimits<int32>::Max())
    {
        RetessellationCadenceStats.StepsSinceLastTrigger++;
    }

    if (StepsSinceLastRetessellationCheck < EvaluationInterval)
    {
        return;
    }

    StepsSinceLastRetessellationCheck = 0;
    RetessellationCadenceStats.EvaluationCount++;

    const FRetessellationAnalysis Analysis = ComputeRetessellationAnalysis();
    LastRetessellationMaxDriftDegrees = Analysis.MaxDriftDegrees;
    LastRetessellationBadTriangleRatio = Analysis.BadTriangleRatio;

    if (Analysis.TotalTriangleCount == 0)
    {
        return;
    }

    const double TriggerDegrees = FMath::Max(Parameters.RetessellationTriggerDegrees, Parameters.RetessellationThresholdDegrees);
    const bool bExceededDrift = LastRetessellationMaxDriftDegrees >= TriggerDegrees;
    const bool bTriangleQualityPoor = LastRetessellationBadTriangleRatio >= Parameters.RetessellationBadTriangleRatioThreshold;

    if (!bRetessellationInCooldown && bExceededDrift && bTriangleQualityPoor)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("[Re-tessellation] Trigger condition met (max drift %.2f°, bad tri %.2f%%, plate %d, interval %d steps)"),
            LastRetessellationMaxDriftDegrees,
            LastRetessellationBadTriangleRatio * 100.0,
            Analysis.MaxDriftPlateID,
            EvaluationInterval);

        const bool bRetessSuccess = PerformRetessellation();
        if (bRetessSuccess)
        {
            bRetessellationInCooldown = true;
            RetessellationCadenceStats.TriggerCount++;
            RetessellationCadenceStats.LastTriggerTimeMy = CurrentTimeMy;
            RetessellationCadenceStats.LastTriggerMaxDriftDegrees = LastRetessellationMaxDriftDegrees;
            RetessellationCadenceStats.LastTriggerBadTriangleRatio = LastRetessellationBadTriangleRatio;
            RetessellationCadenceStats.LastTriggerInterval = RetessellationCadenceStats.StepsSinceLastTrigger;
            RetessellationCadenceStats.StepsSinceLastTrigger = 0;
            RetessellationCadenceStats.CurrentCooldownStepAccumulator = 0;
            RetessellationCadenceStats.LastCooldownDuration = 0;

            UE_LOG(LogPlanetaryCreation, Log,
                TEXT("[Re-tessellation] Auto trigger #%d (evals=%d, interval=%d steps, drift %.2f°, bad %.2f%%)"),
                RetessellationCadenceStats.TriggerCount,
                RetessellationCadenceStats.EvaluationCount,
                RetessellationCadenceStats.LastTriggerInterval,
                RetessellationCadenceStats.LastTriggerMaxDriftDegrees,
                RetessellationCadenceStats.LastTriggerBadTriangleRatio * 100.0);
        }

        return;
    }

    if (bRetessellationInCooldown)
    {
        if (bExceededDrift && bTriangleQualityPoor)
        {
            RetessellationCadenceStats.CooldownBlocks++;
        }

        if (LastRetessellationMaxDriftDegrees <= Parameters.RetessellationThresholdDegrees)
        {
            bRetessellationInCooldown = false;
            RetessellationCadenceStats.LastCooldownDuration = RetessellationCadenceStats.CurrentCooldownStepAccumulator;
            RetessellationCadenceStats.CurrentCooldownStepAccumulator = 0;
            UE_LOG(LogPlanetaryCreation, Verbose,
                TEXT("[Re-tessellation] Drift %.2f° <= cooldown %.2f°; rebuilds re-enabled after %d steps in cooldown."),
                LastRetessellationMaxDriftDegrees,
                Parameters.RetessellationThresholdDegrees,
                RetessellationCadenceStats.LastCooldownDuration);
        }
    }
}
