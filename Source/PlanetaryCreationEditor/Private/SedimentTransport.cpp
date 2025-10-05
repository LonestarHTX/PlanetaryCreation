// Copyright 2025 Michael Hall. All Rights Reserved.

#include "TectonicSimulationService.h"
#include "Math/UnrealMathUtility.h"

/**
 * Milestone 5 Task 2.2: Sediment Transport (Stage 0 Diffusion)
 *
 * Redistributes eroded material via mass-conserving diffusion.
 * Stage 0: Simple neighbor diffusion (deferred: hydraulic routing to Milestone 6).
 */

void UTectonicSimulationService::ApplySedimentTransport(double DeltaTimeMy)
{
    if (!Parameters.bEnableSedimentTransport)
    {
        return; // Feature disabled
    }

    const int32 VertexCount = RenderVertices.Num();
    if (VertexCount == 0)
    {
        return;
    }

    // Ensure M5 arrays are initialized (M5 Phase 3 lifecycle fix)
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

    // Compute total eroded material available for redistribution
    double TotalErodedMass = 0.0;
    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        if (VertexErosionRates.IsValidIndex(VertexIdx))
        {
            const double ErosionThisStep = VertexErosionRates[VertexIdx] * DeltaTimeMy;
            if (ErosionThisStep > 0.0)
            {
                TotalErodedMass += ErosionThisStep;
            }
        }
    }

    /**
     * M5 Phase 3 fix: Build available sediment pool FIRST (from erosion + existing sediment),
     * then diffuse from that pool. This avoids double-counting and allows same-step transport.
     */
    TArray<double> AvailableSediment;
    AvailableSediment.SetNumZeroed(VertexCount);

    // Step 1: Add freshly eroded material to available pool
    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        // Start with existing sediment
        AvailableSediment[VertexIdx] = VertexSedimentThickness[VertexIdx];

        // Add fresh erosion from this step
        if (VertexErosionRates.IsValidIndex(VertexIdx))
        {
            const double ErosionThisStep = VertexErosionRates[VertexIdx] * DeltaTimeMy;
            if (ErosionThisStep > 0.0)
            {
                AvailableSediment[VertexIdx] += ErosionThisStep;
            }
        }
    }

    // Step 2: Diffuse sediment downhill (multiple iterations per timestep)
    // M5 Phase 3.5 fix: Multiple iterations allow sediment to cascade through multiple hops
    // With 10 iterations, sediment can travel up to 10 vertex hops per timestep (2 My)
    // Deferred: Full hydraulic routing planned for Milestone 6
    const int32 DiffusionIterations = 10;

    for (int32 Iteration = 0; Iteration < DiffusionIterations; ++Iteration)
    {
        TArray<double> SedimentDelta;
        SedimentDelta.SetNumZeroed(VertexCount);

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        if (!VertexElevationValues.IsValidIndex(VertexIdx))
        {
            continue;
        }

        // M5 Phase 3.5 fix: Use BASE elevation only (not including sediment) for gradient calculation
        // This prevents sediment from creating artificial plateaus that block further transport
        const double CurrentElevation = VertexElevationValues[VertexIdx];
        const TArray<int32>* Neighbors = VertexNeighbors.Find(VertexIdx);

        if (!Neighbors || Neighbors->Num() == 0)
        {
            continue;
        }

        // Find downhill neighbors and calculate gradients based on base elevation
        TArray<int32> DownhillNeighbors;
        TArray<double> NeighborGradients;
        double TotalGradient = 0.0;

        for (int32 NeighborIdx : *Neighbors)
        {
            if (VertexElevationValues.IsValidIndex(NeighborIdx))
            {
                const double NeighborElevation = VertexElevationValues[NeighborIdx];
                if (NeighborElevation < CurrentElevation) // Only downhill neighbors (base elevation)
                {
                    const double Gradient = CurrentElevation - NeighborElevation;
                    DownhillNeighbors.Add(NeighborIdx);
                    NeighborGradients.Add(Gradient);
                    TotalGradient += Gradient;
                }
            }
        }

        // Diffuse sediment downhill (proportional to gradient)
        if (DownhillNeighbors.Num() > 0 && TotalGradient > 0.0 && AvailableSediment[VertexIdx] > 0.0)
        {
            // Calculate total transfer amount based on steepest gradient
            double MaxGradient = 0.0;
            for (double Gradient : NeighborGradients)
            {
                MaxGradient = FMath::Max(MaxGradient, Gradient);
            }

            // M5 Phase 3 fix: Adjust transfer amount scaling for meters (was /1000 for km)
            // Normalize by typical elevation range (500m) so slopes drive meaningful transfer
            const double SlopeFactor = FMath::Min(1.0, MaxGradient / 500.0);
            // M5 Phase 3.5 fix: Divide timestep by iteration count to keep total transfer per timestep constant
            const double TransferAmount = AvailableSediment[VertexIdx] * Parameters.SedimentDiffusionRate * SlopeFactor * (DeltaTimeMy / DiffusionIterations);

            SedimentDelta[VertexIdx] -= TransferAmount;

            // Distribute to downhill neighbors proportional to gradient (steeper slopes get more sediment)
            for (int32 i = 0; i < DownhillNeighbors.Num(); ++i)
            {
                const double ProportionalTransfer = TransferAmount * (NeighborGradients[i] / TotalGradient);
                SedimentDelta[DownhillNeighbors[i]] += ProportionalTransfer;
            }
        }

        // Bonus weight for convergent boundaries (preferred sinks)
        if (VertexPlateAssignments.IsValidIndex(VertexIdx))
        {
            // Check if this vertex is near a convergent boundary
            bool bNearConvergentBoundary = false;

            for (int32 NeighborIdx : *Neighbors)
            {
                if (VertexPlateAssignments.IsValidIndex(NeighborIdx))
                {
                    if (VertexPlateAssignments[VertexIdx] != VertexPlateAssignments[NeighborIdx])
                    {
                        // Different plates - check if boundary is convergent
                        const int32 PlateA = VertexPlateAssignments[VertexIdx];
                        const int32 PlateB = VertexPlateAssignments[NeighborIdx];
                        const TPair<int32, int32> BoundaryKey = (PlateA < PlateB) ? TPair<int32, int32>(PlateA, PlateB) : TPair<int32, int32>(PlateB, PlateA);

                        if (const FPlateBoundary* Boundary = Boundaries.Find(BoundaryKey))
                        {
                            if (Boundary->BoundaryType == EBoundaryType::Convergent)
                            {
                                bNearConvergentBoundary = true;
                                break;
                            }
                        }
                    }
                }
            }

            // Bonus sediment accumulation at trenches
            if (bNearConvergentBoundary)
            {
                SedimentDelta[VertexIdx] += 0.05 * Parameters.SedimentDiffusionRate * DeltaTimeMy;
            }
        }
    }

        // Apply this iteration's sediment changes to AvailableSediment for next iteration
        for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
        {
            AvailableSediment[VertexIdx] = FMath::Max(0.0, AvailableSediment[VertexIdx] + SedimentDelta[VertexIdx]);
        }
    } // End diffusion iterations

    // Step 3: Apply final sediment values (already includes erosion + all diffusion iterations)
    double TotalDepositedMass = 0.0; // Track net positive changes for mass conservation

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        // AvailableSediment now contains the final value after all diffusion iterations
        const double FinalSediment = AvailableSediment[VertexIdx];

        // Track net deposits (final > initial)
        const double NetChange = FinalSediment - VertexSedimentThickness[VertexIdx];
        if (NetChange > 0.0)
        {
            TotalDepositedMass += NetChange;
        }

        VertexSedimentThickness[VertexIdx] = FinalSediment;
    }

    // Log mass conservation (should be close to 1.0)
    if (TotalErodedMass > 0.0)
    {
        const double MassRatio = TotalDepositedMass / TotalErodedMass;
        UE_LOG(LogTemp, VeryVerbose, TEXT("Sediment transport mass conservation: %.3f (eroded: %.2f m, deposited: %.2f m)"),
            MassRatio, TotalErodedMass, TotalDepositedMass);
    }
}
