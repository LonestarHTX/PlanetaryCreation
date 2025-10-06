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

    ResetCrustAgeForSeeds(FMath::Max(0, Parameters.RidgeDirectionDirtyRingDepth));

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

        /**
         * Age-subsidence formula (paper-compliant):
         * New oceanic crust forms at ridges (~-1000m depth, zᵀ from Appendix A).
         * As crust ages and moves away from ridge, it cools, densifies, and subsides toward abyssal depth.
         * Target asymptote: -6000m (zᵇ from Appendix A).
         *
         * Formula: depth(t) = RidgeDepth - SubsidenceCoeff × sqrt(age)
         * Clamped to never exceed abyssal depth.
         */
        const double RidgeDepth_m = PaperElevationConstants::OceanicRidgeDepth_m; // -1000m (zᵀ)
        const double AgeSubsidence_m = Parameters.OceanicAgeSubsidenceCoeff * FMath::Sqrt(VertexCrustAge[VertexIdx]); // meters
        const double TargetDepth_m = FMath::Max(
            RidgeDepth_m - AgeSubsidence_m,
            PaperElevationConstants::AbyssalPlainDepth_m  // Never deeper than -6000m
        );

        // Gaussian smoothing: average with neighbors (dampens roughness)
        if (!RenderVertexAdjacencyOffsets.IsValidIndex(VertexIdx) || !RenderVertexAdjacencyOffsets.IsValidIndex(VertexIdx + 1))
        {
            const double AgeSubsidencePull = (TargetDepth_m - Elevation_m) * 0.01 * DeltaTimeMy;
            VertexElevationValues[VertexIdx] = FMath::Min(Elevation_m + AgeSubsidencePull, Parameters.SeaLevel - 1.0);
            continue;
        }

        const int32 NeighborStart = RenderVertexAdjacencyOffsets[VertexIdx];
        const int32 NeighborEnd = RenderVertexAdjacencyOffsets[VertexIdx + 1];

        double SmoothedElevation = Elevation_m;
        double WeightSum = 1.0;

        for (int32 NeighborOffset = NeighborStart; NeighborOffset < NeighborEnd; ++NeighborOffset)
        {
            const int32 NeighborIdx = RenderVertexAdjacency.IsValidIndex(NeighborOffset) ? RenderVertexAdjacency[NeighborOffset] : INDEX_NONE;
            if (!VertexElevationValues.IsValidIndex(NeighborIdx))
            {
                continue;
            }

            const double NeighborElevation = VertexElevationValues[NeighborIdx];
            const double Weight = RenderVertexAdjacencyWeights.IsValidIndex(NeighborOffset)
                ? RenderVertexAdjacencyWeights[NeighborOffset]
                : 0.0f;

            if (Weight <= 0.0)
            {
                continue;
            }

            SmoothedElevation += NeighborElevation * Weight;
            WeightSum += Weight;
        }

        if (WeightSum > 1.0)
        {
            SmoothedElevation /= WeightSum;
        }

        // Dampen toward smoothed elevation (slow subsidence rate)
        const double DampenRate = Parameters.OceanicDampeningConstant * DeltaTimeMy;
        const double NewElevation_m = FMath::Lerp(Elevation_m, SmoothedElevation, FMath::Min(1.0, DampenRate));

        // Also pull toward age-subsidence target depth (all in meters)
        const double AgeSubsidencePull = (TargetDepth_m - NewElevation_m) * 0.01 * DeltaTimeMy; // 1% per My pull

        // M5 Phase 3 fix: Clamp oceanic elevations below sea level (prevent stress from lifting them)
        VertexElevationValues[VertexIdx] = FMath::Min(NewElevation_m + AgeSubsidencePull, Parameters.SeaLevel - 1.0);
    }

    BumpOceanicAmplificationSerial();
}
