// Copyright 2025 Michael Hall. All Rights Reserved.

#include "TectonicSimulationService.h"

#include "Async/ParallelFor.h"
#include "PlanetaryCreationLogging.h"

namespace
{
    inline bool IsContinentalPlate(const TArray<FTectonicPlate>& Plates, int32 PlateIdx)
    {
        return Plates.IsValidIndex(PlateIdx) && Plates[PlateIdx].CrustType == ECrustType::Continental;
    }
}

void UTectonicSimulationService::ApplyHydraulicErosion(double DeltaTimeMy)
{
    LastHydraulicTotalEroded = 0.0;
    LastHydraulicTotalDeposited = 0.0;
    LastHydraulicLostToOcean = 0.0;

    if (!Parameters.bEnableHydraulicErosion || DeltaTimeMy <= 0.0)
    {
        return;
    }

    const int32 VertexCount = VertexAmplifiedElevation.Num();
    if (VertexCount == 0)
    {
        return;
    }

    if (RenderVertexAdjacencyOffsets.Num() != VertexCount + 1 || RenderVertexAdjacency.Num() == 0)
    {
        BuildRenderVertexAdjacency();
    }
    if (RenderVertexAdjacencyOffsets.Num() != VertexCount + 1 || RenderVertexAdjacency.Num() == 0)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[Hydraulic] Missing render adjacency; skipping hydraulic erosion"));
        return;
    }

    HydraulicDownhillNeighbor.SetNum(VertexCount);
    HydraulicFlowAccumulation.SetNum(VertexCount);
    HydraulicErosionBuffer.SetNumZeroed(VertexCount);
    HydraulicSelfDepositBuffer.SetNumZeroed(VertexCount);
    HydraulicDownstreamDepositBuffer.SetNumZeroed(VertexCount);
    HydraulicUpstreamCount.SetNumZeroed(VertexCount);
    HydraulicProcessingQueue.Reset();
    HydraulicProcessingQueue.Reserve(VertexCount);

    const double PlanetRadius = FMath::Max(Parameters.PlanetRadius, 1.0);
    const double DownstreamRatio = FMath::Clamp(Parameters.HydraulicDownstreamDepositRatio, 0.0, 1.0);
    const double SelfRatio = 1.0 - DownstreamRatio;
    const double AreaExponent = FMath::Max(0.0, Parameters.HydraulicAreaExponent);
    const double SlopeExponent = FMath::Max(0.0, Parameters.HydraulicSlopeExponent);

    auto ComputeAmplifiedSlope = [this, PlanetRadius](int32 VertexIdx) -> double
    {
        if (!RenderVertices.IsValidIndex(VertexIdx))
        {
            return 0.0;
        }

        const int32 StartOffset = RenderVertexAdjacencyOffsets[VertexIdx];
        const int32 EndOffset = RenderVertexAdjacencyOffsets[VertexIdx + 1];
        if (StartOffset == EndOffset)
        {
            return 0.0;
        }

        const FVector3d& VertexPos = RenderVertices[VertexIdx];
        const FVector3d Normalized = VertexPos.GetSafeNormal();
        const double BaseElevation = VertexAmplifiedElevation.IsValidIndex(VertexIdx) ? VertexAmplifiedElevation[VertexIdx] : 0.0;

        double MaxSlope = 0.0;
        for (int32 Offset = StartOffset; Offset < EndOffset; ++Offset)
        {
            const int32 NeighborIdx = RenderVertexAdjacency[Offset];
            if (!RenderVertices.IsValidIndex(NeighborIdx) || !VertexAmplifiedElevation.IsValidIndex(NeighborIdx))
            {
                continue;
            }

            const FVector3d& NeighborPos = RenderVertices[NeighborIdx];
            const double NeighborElevation = VertexAmplifiedElevation[NeighborIdx];
            const double ElevDiff = FMath::Abs(BaseElevation - NeighborElevation);

            const double Dot = FMath::Clamp(Normalized | NeighborPos.GetSafeNormal(), -1.0, 1.0);
            const double GeodesicDistance = FMath::Acos(Dot) * PlanetRadius;
            if (GeodesicDistance > UE_DOUBLE_SMALL_NUMBER)
            {
                const double Slope = ElevDiff / GeodesicDistance;
                MaxSlope = FMath::Max(MaxSlope, Slope);
            }
        }
        return MaxSlope;
    };

    ParallelFor(VertexCount, [this](int32 VertexIdx)
    {
        if (!VertexPlateAssignments.IsValidIndex(VertexIdx))
        {
            HydraulicDownhillNeighbor[VertexIdx] = INDEX_NONE;
            return;
        }

        const int32 PlateIdx = VertexPlateAssignments[VertexIdx];
        if (!IsContinentalPlate(Plates, PlateIdx))
        {
            HydraulicDownhillNeighbor[VertexIdx] = INDEX_NONE;
            return;
        }

        const int32 StartOffset = RenderVertexAdjacencyOffsets[VertexIdx];
        const int32 EndOffset = RenderVertexAdjacencyOffsets[VertexIdx + 1];
        if (StartOffset == EndOffset)
        {
            HydraulicDownhillNeighbor[VertexIdx] = INDEX_NONE;
            return;
        }

        const double CurrentElevation = VertexAmplifiedElevation[VertexIdx];
        double MinElevation = CurrentElevation;
        int32 LowestIdx = INDEX_NONE;

        for (int32 Offset = StartOffset; Offset < EndOffset; ++Offset)
        {
            const int32 NeighborIdx = RenderVertexAdjacency[Offset];
            if (!VertexAmplifiedElevation.IsValidIndex(NeighborIdx))
            {
                continue;
            }

            const double NeighborElevation = VertexAmplifiedElevation[NeighborIdx];
            if (NeighborElevation < MinElevation - KINDA_SMALL_NUMBER)
            {
                MinElevation = NeighborElevation;
                LowestIdx = NeighborIdx;
            }
        }

        HydraulicDownhillNeighbor[VertexIdx] = LowestIdx;
    });

    for (int32 i = 0; i < VertexCount; ++i)
    {
        HydraulicFlowAccumulation[i] = 1.0f;
        const int32 DownstreamIdx = HydraulicDownhillNeighbor.IsValidIndex(i) ? HydraulicDownhillNeighbor[i] : INDEX_NONE;
        if (DownstreamIdx != INDEX_NONE && HydraulicUpstreamCount.IsValidIndex(DownstreamIdx))
        {
            ++HydraulicUpstreamCount[DownstreamIdx];
        }
    }

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        if (HydraulicUpstreamCount[VertexIdx] == 0)
        {
            HydraulicProcessingQueue.Add(VertexIdx);
        }
    }

    int32 QueueHead = 0;
    while (QueueHead < HydraulicProcessingQueue.Num())
    {
        const int32 VertexIdx = HydraulicProcessingQueue[QueueHead++];
        const float Flow = HydraulicFlowAccumulation.IsValidIndex(VertexIdx)
            ? HydraulicFlowAccumulation[VertexIdx]
            : 1.0f;

        const int32 DownstreamIdx = HydraulicDownhillNeighbor.IsValidIndex(VertexIdx)
            ? HydraulicDownhillNeighbor[VertexIdx]
            : INDEX_NONE;

        if (DownstreamIdx != INDEX_NONE && HydraulicFlowAccumulation.IsValidIndex(DownstreamIdx))
        {
            HydraulicFlowAccumulation[DownstreamIdx] += Flow;

            if (HydraulicUpstreamCount.IsValidIndex(DownstreamIdx))
            {
                int32& Remaining = HydraulicUpstreamCount[DownstreamIdx];
                if (Remaining > 0)
                {
                    --Remaining;
                    if (Remaining == 0)
                    {
                        HydraulicProcessingQueue.Add(DownstreamIdx);
                    }
                }
            }
        }
    }

    if (QueueHead != VertexCount)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("[Hydraulic] Topological traversal visited %d / %d vertices (possible cycle or disconnected component)"),
            QueueHead,
            VertexCount);
    }

    ParallelFor(VertexCount, [this, DeltaTimeMy, &ComputeAmplifiedSlope, DownstreamRatio, SelfRatio, AreaExponent, SlopeExponent](int32 VertexIdx)
    {
        HydraulicErosionBuffer[VertexIdx] = 0.0;
        HydraulicSelfDepositBuffer[VertexIdx] = 0.0;
        HydraulicDownstreamDepositBuffer[VertexIdx] = 0.0;

        if (!VertexPlateAssignments.IsValidIndex(VertexIdx))
        {
            return;
        }

        const int32 PlateIdx = VertexPlateAssignments[VertexIdx];
        if (!IsContinentalPlate(Plates, PlateIdx))
        {
            return;
        }

        const int32 DownstreamIdx = HydraulicDownhillNeighbor.IsValidIndex(VertexIdx) ? HydraulicDownhillNeighbor[VertexIdx] : INDEX_NONE;

        const double Flow = HydraulicFlowAccumulation.IsValidIndex(VertexIdx) ? static_cast<double>(HydraulicFlowAccumulation[VertexIdx]) : 1.0;
        const double Slope = ComputeAmplifiedSlope(VertexIdx);
        if (!FMath::IsFinite(Slope) || Slope <= UE_DOUBLE_SMALL_NUMBER)
        {
            return;
        }

        double AgeFactor = 1.0;
        const double OrogenyAge = VertexCrustAge.IsValidIndex(VertexIdx) ? VertexCrustAge[VertexIdx] : 0.0;
        if (OrogenyAge < 20.0)
        {
            AgeFactor = 0.3;
        }
        else if (OrogenyAge > 100.0)
        {
            AgeFactor = 2.0;
        }

        const double K = FMath::Max(0.0, Parameters.HydraulicErosionConstant) * AgeFactor;
        const double DischargeTerm = FMath::Pow(FMath::Max(Flow, 1.0), AreaExponent);
        const double SlopeTerm = FMath::Pow(FMath::Max(Slope, UE_DOUBLE_SMALL_NUMBER), SlopeExponent);

        double ErosionRate = K * DischargeTerm * SlopeTerm;
        if (!FMath::IsFinite(ErosionRate) || ErosionRate <= 0.0)
        {
            return;
        }

        const double ErosionAmount = ErosionRate * DeltaTimeMy;
        if (!FMath::IsFinite(ErosionAmount) || ErosionAmount <= 0.0)
        {
            return;
        }

        HydraulicErosionBuffer[VertexIdx] = ErosionAmount;
        HydraulicSelfDepositBuffer[VertexIdx] = SelfRatio * ErosionAmount;
        HydraulicDownstreamDepositBuffer[VertexIdx] = DownstreamRatio * ErosionAmount;
    });

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const double Erode = HydraulicErosionBuffer[VertexIdx];
        if (Erode <= 0.0 || !FMath::IsFinite(Erode))
        {
            continue;
        }

        const double SelfDeposit = HydraulicSelfDepositBuffer[VertexIdx];
        const double DownstreamDeposit = HydraulicDownstreamDepositBuffer[VertexIdx];

        if (VertexAmplifiedElevation.IsValidIndex(VertexIdx))
        {
            VertexAmplifiedElevation[VertexIdx] -= Erode;
            VertexAmplifiedElevation[VertexIdx] += SelfDeposit;
        }
        if (VertexElevationValues.IsValidIndex(VertexIdx))
        {
            VertexElevationValues[VertexIdx] -= Erode;
            VertexElevationValues[VertexIdx] += SelfDeposit;
        }

        LastHydraulicTotalEroded += Erode;
        LastHydraulicTotalDeposited += SelfDeposit;

        const int32 DownstreamIdx = HydraulicDownhillNeighbor.IsValidIndex(VertexIdx) ? HydraulicDownhillNeighbor[VertexIdx] : INDEX_NONE;
        if (DownstreamIdx != INDEX_NONE && DownstreamDeposit > 0.0 && VertexAmplifiedElevation.IsValidIndex(DownstreamIdx))
        {
            VertexAmplifiedElevation[DownstreamIdx] += DownstreamDeposit;
            if (VertexElevationValues.IsValidIndex(DownstreamIdx))
            {
                VertexElevationValues[DownstreamIdx] += DownstreamDeposit;
            }
            LastHydraulicTotalDeposited += DownstreamDeposit;
        }
        else
        {
            LastHydraulicLostToOcean += DownstreamDeposit;
        }
    }

    if (!FMath::IsFinite(LastHydraulicTotalEroded))
    {
        LastHydraulicTotalEroded = 0.0;
    }
    if (!FMath::IsFinite(LastHydraulicTotalDeposited))
    {
        LastHydraulicTotalDeposited = 0.0;
    }
    if (!FMath::IsFinite(LastHydraulicLostToOcean))
    {
        LastHydraulicLostToOcean = 0.0;
    }
}
