// Copyright 2025 Michael Hall. All Rights Reserved.

#include "Simulation/TectonicSimulationService.h"
#include "Math/UnrealMathUtility.h"
#include "Async/ParallelFor.h"

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

    TArray<uint8> OceanicMask;
    OceanicMask.SetNumZeroed(VertexCount);

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const int32 PlateIdx = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;
        const bool bIsOceanicPlate = (PlateIdx != INDEX_NONE && Plates.IsValidIndex(PlateIdx))
            ? (Plates[PlateIdx].CrustType == ECrustType::Oceanic)
            : false;

        if (bIsOceanicPlate && VertexElevationValues.IsValidIndex(VertexIdx) && VertexElevationValues[VertexIdx] < Parameters.SeaLevel)
        {
            OceanicMask[VertexIdx] = 1;
        }
    }

    TArray<double> NextElevation;
    NextElevation.SetNumUninitialized(VertexCount);
    TArray<double> NextCrustAge;
    NextCrustAge.SetNumUninitialized(VertexCount);

    const double RidgeDepth = PaperElevationConstants::OceanicRidgeDepth_m;
    const double AbyssalDepth = PaperElevationConstants::AbyssalPlainDepth_m;
    const double DampFactor = FMath::Clamp(Parameters.OceanicDampeningConstant * DeltaTimeMy, 0.0, 1.0);
    const double AgePullScale = 0.01 * DeltaTimeMy;

    ParallelFor(VertexCount, [this, &OceanicMask, &NextElevation, &NextCrustAge, DampFactor, AgePullScale, RidgeDepth, AbyssalDepth, DeltaTimeMy](int32 VertexIdx)
    {
        const double CurrentElevation = VertexElevationValues.IsValidIndex(VertexIdx) ? VertexElevationValues[VertexIdx] : 0.0;
        const double CurrentAge = VertexCrustAge.IsValidIndex(VertexIdx) ? VertexCrustAge[VertexIdx] : 0.0;

        if (!OceanicMask[VertexIdx])
        {
            NextElevation[VertexIdx] = CurrentElevation;
            NextCrustAge[VertexIdx] = CurrentAge;
            return;
        }

        const double UpdatedAge = CurrentAge + DeltaTimeMy;

        const double AgeSubsidence = Parameters.OceanicAgeSubsidenceCoeff * FMath::Sqrt(UpdatedAge);
        const double TargetDepth = FMath::Max(RidgeDepth - AgeSubsidence, AbyssalDepth);

        double WeightedSum = 0.0;
        const double WeightTotal = RenderVertexAdjacencyWeightTotals.IsValidIndex(VertexIdx)
            ? static_cast<double>(RenderVertexAdjacencyWeightTotals[VertexIdx])
            : 0.0;

        const int32 StartOffset = RenderVertexAdjacencyOffsets[VertexIdx];
        const int32 EndOffset = RenderVertexAdjacencyOffsets[VertexIdx + 1];

        for (int32 Offset = StartOffset; Offset < EndOffset; ++Offset)
        {
            const int32 NeighborIdx = RenderVertexAdjacency.IsValidIndex(Offset) ? RenderVertexAdjacency[Offset] : INDEX_NONE;
            if (!VertexElevationValues.IsValidIndex(NeighborIdx))
            {
                continue;
            }

            const double Weight = RenderVertexAdjacencyWeights.IsValidIndex(Offset)
                ? RenderVertexAdjacencyWeights[Offset]
                : 0.0f;

            if (Weight <= 0.0)
            {
                continue;
            }

            WeightedSum += Weight * VertexElevationValues[NeighborIdx];
        }

        double SmoothedElevation = CurrentElevation;
        if (WeightTotal > UE_DOUBLE_SMALL_NUMBER)
        {
            SmoothedElevation = (CurrentElevation + WeightedSum) / (1.0 + WeightTotal);
        }

        const double DampedElevation = FMath::Lerp(CurrentElevation, SmoothedElevation, DampFactor);
        const double AgePull = (TargetDepth - DampedElevation) * AgePullScale;
        const double ClampedElevation = FMath::Min(DampedElevation + AgePull, Parameters.SeaLevel - 1.0);

        NextElevation[VertexIdx] = ClampedElevation;
        NextCrustAge[VertexIdx] = UpdatedAge;
    });

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        VertexElevationValues[VertexIdx] = NextElevation[VertexIdx];
        VertexCrustAge[VertexIdx] = NextCrustAge[VertexIdx];
    }

    BumpOceanicAmplificationSerial();
}
