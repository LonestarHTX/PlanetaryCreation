// Copyright 2025 Michael Hall. All Rights Reserved.

#include "TectonicSimulationService.h"
#include "Math/UnrealMathUtility.h"

/**
 * Milestone 5 Task 2.3: Oceanic Dampening
 *
 * Applies smoothing to seafloor elevation and age-dependent subsidence.
 * Paper Section 4.5: Oceanic crust deepens with age per empirical formula.
 */

void UTectonicSimulationService::ApplyOceanicDampening(double DeltaTimeMy)
{
    if (!Parameters.bEnableOceanicDampening)
    {
        return; // Feature disabled
    }

    const int32 VertexCount = RenderVertices.Num();
    if (VertexCount == 0)
    {
        return;
    }

    // Ensure M5 arrays are initialized (M5 Phase 3 lifecycle fix)
    // These may be empty if dampening runs before erosion or if feature flags toggle
    if (VertexCrustAge.Num() != VertexCount)
    {
        VertexCrustAge.SetNumZeroed(VertexCount);
    }
    if (VertexElevationValues.Num() != VertexCount)
    {
        VertexElevationValues.SetNumZeroed(VertexCount);
    }
    if (VertexSedimentThickness.Num() != VertexCount)
    {
        VertexSedimentThickness.SetNumZeroed(VertexCount);
    }

    // Build adjacency map: vertex -> neighbors
    TMap<int32, TArray<int32>> VertexNeighbors;
    const int32 TriangleCount = RenderTriangles.Num() / 3;

    for (int32 TriIdx = 0; TriIdx < TriangleCount; ++TriIdx)
    {
        const int32 V0 = RenderTriangles[TriIdx * 3 + 0];
        const int32 V1 = RenderTriangles[TriIdx * 3 + 1];
        const int32 V2 = RenderTriangles[TriIdx * 3 + 2];

        VertexNeighbors.FindOrAdd(V0).AddUnique(V1);
        VertexNeighbors.FindOrAdd(V0).AddUnique(V2);
        VertexNeighbors.FindOrAdd(V1).AddUnique(V0);
        VertexNeighbors.FindOrAdd(V1).AddUnique(V2);
        VertexNeighbors.FindOrAdd(V2).AddUnique(V0);
        VertexNeighbors.FindOrAdd(V2).AddUnique(V1);
    }

    // Apply dampening to vertices below sea level
    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        if (!VertexElevationValues.IsValidIndex(VertexIdx))
        {
            continue;
        }

        const double Elevation_m = VertexElevationValues[VertexIdx]; // Elevation in METERS

        // Only dampen seafloor (elevation < sea level, both in meters)
        if (Elevation_m >= Parameters.SeaLevel)
        {
            continue;
        }

        // Determine if this vertex is oceanic crust (only age oceanic plates)
        const int32 PlateIdx = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;
        const bool bIsOceanic = (PlateIdx != INDEX_NONE && Plates.IsValidIndex(PlateIdx))
            ? (Plates[PlateIdx].CrustType == ECrustType::Oceanic)
            : false;

        if (!bIsOceanic)
        {
            continue; // Only dampen oceanic crust
        }

        // Increment crust age for oceanic crust
        VertexCrustAge[VertexIdx] += DeltaTimeMy;

        // Age-subsidence formula: depth = BaseDepth + Coeff × sqrt(age)
        // Paper: h_ocean = -2500 - 350 × sqrt(age_My)
        // All values in METERS
        const double BaseDepth_m = -2500.0; // Mean oceanic depth (meters)
        const double AgeSubsidence_m = Parameters.OceanicAgeSubsidenceCoeff * FMath::Sqrt(VertexCrustAge[VertexIdx]); // meters
        const double TargetDepth_m = BaseDepth_m - AgeSubsidence_m;

        // Gaussian smoothing: average with neighbors (dampens roughness)
        const TArray<int32>* Neighbors = VertexNeighbors.Find(VertexIdx);

        if (Neighbors && Neighbors->Num() > 0)
        {
            double SmoothedElevation = Elevation_m; // Start with current elevation (meters)
            double WeightSum = 1.0; // Self weight

            for (int32 NeighborIdx : *Neighbors)
            {
                if (!VertexElevationValues.IsValidIndex(NeighborIdx))
                {
                    continue;
                }

                const double NeighborElevation = VertexElevationValues[NeighborIdx];

                // Geodesic distance on unit sphere
                const FVector3d& V1 = RenderVertices[VertexIdx];
                const FVector3d& V2 = RenderVertices[NeighborIdx];
                const double DotProduct = FMath::Clamp(FVector3d::DotProduct(V1.GetSafeNormal(), V2.GetSafeNormal()), -1.0, 1.0);
                const double GeodesicDistance = FMath::Acos(DotProduct); // Radians

                // Gaussian weight (radius = 0.1 rad ≈ 5.7°)
                const double SmoothingRadius = 0.1;
                const double Weight = FMath::Exp(-GeodesicDistance * GeodesicDistance / (2.0 * SmoothingRadius * SmoothingRadius));

                SmoothedElevation += NeighborElevation * Weight;
                WeightSum += Weight;
            }

            SmoothedElevation /= WeightSum;

            // Dampen toward smoothed elevation (slow subsidence rate)
            const double DampenRate = Parameters.OceanicDampeningConstant * DeltaTimeMy;
            const double NewElevation_m = FMath::Lerp(Elevation_m, SmoothedElevation, FMath::Min(1.0, DampenRate));

            // Also pull toward age-subsidence target depth (all in meters)
            const double AgeSubsidencePull = (TargetDepth_m - NewElevation_m) * 0.01 * DeltaTimeMy; // 1% per My pull

            // M5 Phase 3 fix: Clamp oceanic elevations below sea level (prevent stress from lifting them)
            VertexElevationValues[VertexIdx] = FMath::Min(NewElevation_m + AgeSubsidencePull, Parameters.SeaLevel - 1.0);
        }
        else
        {
            // No neighbors - just apply age-subsidence (meters)
            const double AgeSubsidencePull = (TargetDepth_m - Elevation_m) * 0.01 * DeltaTimeMy;

            // M5 Phase 3 fix: Clamp oceanic elevations below sea level
            VertexElevationValues[VertexIdx] = FMath::Min(Elevation_m + AgeSubsidencePull, Parameters.SeaLevel - 1.0);
        }
    }

    // Reset crust age at divergent boundaries (new oceanic crust)
    for (const auto& BoundaryPair : Boundaries)
    {
        const FPlateBoundary& Boundary = BoundaryPair.Value;

        if (Boundary.BoundaryType == EBoundaryType::Divergent)
        {
            // Find vertices on this boundary edge and reset their age
            for (int32 EdgeVertexIdx : Boundary.SharedEdgeVertices)
            {
                // Find render vertices near this shared edge vertex
                if (SharedVertices.IsValidIndex(EdgeVertexIdx))
                {
                    const FVector3d& EdgePos = SharedVertices[EdgeVertexIdx];

                    for (int32 RenderVertexIdx = 0; RenderVertexIdx < VertexCount; ++RenderVertexIdx)
                    {
                        const FVector3d& RenderPos = RenderVertices[RenderVertexIdx];
                        const double Distance = FVector3d::Distance(EdgePos, RenderPos);

                        // If very close to ridge, reset crust age (new crust)
                        if (Distance < 0.01) // ~0.01 radians ≈ 0.6° proximity
                        {
                            VertexCrustAge[RenderVertexIdx] = 0.0;
                        }
                    }
                }
            }
        }
    }
}
