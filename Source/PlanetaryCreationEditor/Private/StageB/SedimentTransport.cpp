// Copyright 2025 Michael Hall. All Rights Reserved.

#include "Utilities/PlanetaryCreationLogging.h"
#include "Simulation/TectonicSimulationService.h"

#include "Async/ParallelFor.h"

void UTectonicSimulationService::ApplySedimentTransport(double DeltaTimeMy)
{
    if (!Parameters.bEnableSedimentTransport)
    {
        return;
    }

    const int32 VertexCount = RenderVertices.Num();
    if (VertexCount == 0)
    {
        return;
    }

    if (VertexSedimentThickness.Num() != VertexCount)
    {
        VertexSedimentThickness.SetNumZeroed(VertexCount);
    }
    if (VertexElevationValues.Num() != VertexCount)
    {
        VertexElevationValues.SetNumZeroed(VertexCount);
    }
    if (VertexErosionRates.Num() != VertexCount)
    {
        VertexErosionRates.SetNumZeroed(VertexCount);
    }

    if (RenderVertexAdjacencyOffsets.Num() != VertexCount + 1 || RenderVertexAdjacency.Num() == 0)
    {
        BuildRenderVertexAdjacency();
    }

    if (RenderVertexAdjacencyOffsets.Num() != VertexCount + 1 || RenderVertexAdjacency.Num() == 0)
    {
        return;
    }

    // Stage 0 erosion pool: existing sediment + new erosion from this step
    TArray<double> CurrentSediment;
    CurrentSediment.SetNumZeroed(VertexCount);

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        double Value = VertexSedimentThickness[VertexIdx];
        if (VertexErosionRates.IsValidIndex(VertexIdx))
        {
            const double ErosionThisStep = VertexErosionRates[VertexIdx] * DeltaTimeMy;
            if (ErosionThisStep > 0.0)
            {
                Value += ErosionThisStep;
            }
        }
        CurrentSediment[VertexIdx] = FMath::Max(0.0, Value);
    }

    const int32 EdgeCount = RenderVertexAdjacency.Num();
    if (RenderVertexReverseAdjacency.Num() != EdgeCount)
    {
        BuildRenderVertexReverseAdjacency();
    }

    const int32 DiffusionIterations = Parameters.bSkipCPUAmplification ? 4 : 6;

    TArray<double> NextSediment;
    NextSediment.SetNumZeroed(VertexCount);
    TArray<float> OutgoingFlow;
    OutgoingFlow.SetNumZeroed(EdgeCount);
    TArray<float> SelfReduction;
    SelfReduction.SetNumZeroed(VertexCount);

    for (int32 Iteration = 0; Iteration < DiffusionIterations; ++Iteration)
    {
        FMemory::Memset(OutgoingFlow.GetData(), 0, EdgeCount * sizeof(float));
        FMemory::Memset(SelfReduction.GetData(), 0, VertexCount * sizeof(float));

        ParallelFor(VertexCount, [this, &CurrentSediment, &OutgoingFlow, &SelfReduction, DiffusionIterations, DeltaTimeMy](int32 VertexIdx)
        {
            if (!VertexElevationValues.IsValidIndex(VertexIdx))
            {
                return;
            }

            const double CurrentElevation = VertexElevationValues[VertexIdx];
            const int32 StartOffset = RenderVertexAdjacencyOffsets[VertexIdx];
            const int32 EndOffset = RenderVertexAdjacencyOffsets[VertexIdx + 1];

            if (StartOffset == EndOffset)
            {
                return;
            }

            TArray<int32, TInlineAllocator<12>> DownhillOffsets;
            TArray<double, TInlineAllocator<12>> NeighborGradients;
            double TotalGradient = 0.0;

            for (int32 Offset = StartOffset; Offset < EndOffset; ++Offset)
            {
                const int32 NeighborIdx = RenderVertexAdjacency[Offset];
                if (!VertexElevationValues.IsValidIndex(NeighborIdx))
                {
                    continue;
                }

                const double NeighborElevation = VertexElevationValues[NeighborIdx];
                if (NeighborElevation < CurrentElevation)
                {
                    const double Gradient = CurrentElevation - NeighborElevation;
                    DownhillOffsets.Add(Offset);
                    NeighborGradients.Add(Gradient);
                    TotalGradient += Gradient;
                }
            }

            if (DownhillOffsets.Num() == 0 || TotalGradient <= 0.0)
            {
                return;
            }

            const double Available = CurrentSediment[VertexIdx];
            if (Available <= 0.0)
            {
                return;
            }

            double MaxGradient = 0.0;
            for (double Gradient : NeighborGradients)
            {
                MaxGradient = FMath::Max(MaxGradient, Gradient);
            }

            const double SlopeFactor = FMath::Min(1.0, MaxGradient / 500.0);
            const double TransferAmount = Available * Parameters.SedimentDiffusionRate * SlopeFactor * (DeltaTimeMy / DiffusionIterations);
            if (TransferAmount <= 0.0)
            {
                return;
            }

            SelfReduction[VertexIdx] = static_cast<float>(TransferAmount);

            for (int32 LocalIdx = 0; LocalIdx < DownhillOffsets.Num(); ++LocalIdx)
            {
                const int32 Offset = DownhillOffsets[LocalIdx];
                const double Gradient = NeighborGradients[LocalIdx];
                const double ProportionalTransfer = TransferAmount * (Gradient / TotalGradient);
                OutgoingFlow[Offset] = static_cast<float>(ProportionalTransfer);
            }
        });

        for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
        {
            double Delta = -static_cast<double>(SelfReduction[VertexIdx]);

            const int32 StartOffset = RenderVertexAdjacencyOffsets[VertexIdx];
            const int32 EndOffset = RenderVertexAdjacencyOffsets[VertexIdx + 1];

            for (int32 Offset = StartOffset; Offset < EndOffset; ++Offset)
            {
                const int32 ReverseIndex = RenderVertexReverseAdjacency.IsValidIndex(Offset)
                    ? RenderVertexReverseAdjacency[Offset]
                    : INDEX_NONE;
                if (ReverseIndex != INDEX_NONE)
                {
                    Delta += OutgoingFlow[ReverseIndex];
                }
            }

            if (ConvergentNeighborFlags.IsValidIndex(VertexIdx) && ConvergentNeighborFlags[VertexIdx])
            {
                Delta += 0.05 * Parameters.SedimentDiffusionRate * DeltaTimeMy;
            }

            const double Updated = FMath::Max(0.0, CurrentSediment[VertexIdx] + Delta);
            NextSediment[VertexIdx] = Updated;
        }

        CurrentSediment = NextSediment;
    }

    double TotalDepositedMass = 0.0;

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const double FinalSediment = CurrentSediment[VertexIdx];
        const double NetChange = FinalSediment - VertexSedimentThickness[VertexIdx];
        if (NetChange > 0.0)
        {
            TotalDepositedMass += NetChange;
        }

        VertexSedimentThickness[VertexIdx] = FinalSediment;
    }

    UE_LOG(LogPlanetaryCreation, VeryVerbose, TEXT("[Sediment] Deposited mass this step: %.4f m"), TotalDepositedMass);
}
