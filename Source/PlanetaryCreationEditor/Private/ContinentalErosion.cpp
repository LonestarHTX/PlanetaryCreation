// Copyright 2025 Michael Hall. All Rights Reserved.

#include "PlanetaryCreationLogging.h"
#include "TectonicSimulationService.h"
#include "Math/UnrealMathUtility.h"

/**
 * Milestone 5 Task 2.1: Continental Erosion Implementation
 *
 * Applies erosion to continental crust above sea level based on paper Section 4.5.
 * Formula: ErosionRate = k × Slope × (Elevation - SeaLevel)⁺ × ThermalFactor × StressFactor
 */

void UTectonicSimulationService::ApplyContinentalErosion(double DeltaTimeMy)
{
    if (!Parameters.bEnableContinentalErosion)
    {
        return; // Feature disabled
    }

    const int32 VertexCount = RenderVertices.Num();
    if (VertexCount == 0)
    {
        return;
    }

    // Ensure erosion arrays are initialized
    if (VertexElevationValues.Num() != VertexCount)
    {
        VertexElevationValues.SetNumZeroed(VertexCount);
    }
    if (VertexErosionRates.Num() != VertexCount)
    {
        VertexErosionRates.SetNumZeroed(VertexCount);
    }

    // Get max stress/temperature for normalization
    double MaxStress = 1.0;
    double MaxTemperature = 1000.0; // Kelvin

    if (VertexStressValues.Num() > 0)
    {
        for (double Stress : VertexStressValues)
        {
            MaxStress = FMath::Max(MaxStress, Stress);
        }
    }

    if (VertexTemperatureValues.Num() > 0)
    {
        for (double Temp : VertexTemperatureValues)
        {
            MaxTemperature = FMath::Max(MaxTemperature, Temp);
        }
    }

    // Apply erosion to each vertex
    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        // M5 Phase 3 fix: Elevations now seeded in ResetSimulation(), just read them here
        double Elevation_m = VertexElevationValues[VertexIdx]; // Elevation in METERS (non-const for stress-lift update)

        // M5 Phase 3 fix: Skip erosion for oceanic crust entirely (only erode continental)
        const int32 PlateIdx = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;

        // M5 Phase 3 fix: Treat INDEX_NONE vertices as oceanic (skip erosion and log warning)
        if (PlateIdx == INDEX_NONE)
        {
            VertexErosionRates[VertexIdx] = 0.0;
            continue;
        }

        const bool bIsOceanic = Plates.IsValidIndex(PlateIdx)
            ? (Plates[PlateIdx].CrustType == ECrustType::Oceanic)
            : false;

        if (bIsOceanic)
        {
            VertexErosionRates[VertexIdx] = 0.0;
            continue; // Oceanic crust not subject to continental erosion
        }

        // M5 Phase 3.7 fix: Apply stress-driven uplift BEFORE checking sea level
        // Scaling: 1 MPa → 100 m elevation (reasonable for tectonic mountain building)
        // Example: 50 MPa convergence → 5 km mountain (Himalayas-scale)
        const double StressLift_m = VertexStressValues.IsValidIndex(VertexIdx)
            ? (VertexStressValues[VertexIdx] * 100.0) // 1 MPa → 100 m
            : 0.0;

        if (StressLift_m > 0.0)
        {
            VertexElevationValues[VertexIdx] = FMath::Max(Elevation_m + StressLift_m, 250.0);
            Elevation_m = VertexElevationValues[VertexIdx]; // Update local var for erosion calc
        }

        // Only erode terrain above sea level (both in meters)
        if (Elevation_m <= Parameters.SeaLevel)
        {
            VertexErosionRates[VertexIdx] = 0.0;
            continue;
        }

        // Compute slope at this vertex
        const double Slope = ComputeVertexSlope(VertexIdx);

        // Base erosion rate (both elevation and sea level in meters)
        double ErosionRate = Parameters.ErosionConstant * Slope * (Elevation_m - Parameters.SeaLevel);

        // Thermal factor: Hotter regions erode faster (1.0-1.5× multiplier)
        double ThermalFactor = 1.0;
        if (VertexTemperatureValues.IsValidIndex(VertexIdx) && MaxTemperature > 0.0)
        {
            ThermalFactor = 1.0 + 0.5 * (VertexTemperatureValues[VertexIdx] / MaxTemperature);
        }

        // Stress factor: High-stress regions (mountains) erode faster (1.0-1.3× multiplier)
        double StressFactor = 1.0;
        if (VertexStressValues.IsValidIndex(VertexIdx) && MaxStress > 0.0)
        {
            StressFactor = 1.0 + 0.3 * (VertexStressValues[VertexIdx] / MaxStress);
        }

        // Total erosion for this step (meters)
        const double TotalErosion = ErosionRate * ThermalFactor * StressFactor * DeltaTimeMy;

        // Store erosion rate for visualization/CSV export (m/My)
        VertexErosionRates[VertexIdx] = ErosionRate * ThermalFactor * StressFactor;

        // Apply erosion (never go below sea level, all in meters)
        VertexElevationValues[VertexIdx] = FMath::Max(Parameters.SeaLevel, Elevation_m - TotalErosion);
    }
}

double UTectonicSimulationService::ComputeVertexSlope(int32 VertexIdx) const
{
    if (!RenderVertices.IsValidIndex(VertexIdx))
    {
        return 0.0;
    }

    if (RenderVertexAdjacencyOffsets.Num() != RenderVertices.Num() + 1 || RenderVertexAdjacency.Num() == 0)
    {
        const_cast<UTectonicSimulationService*>(this)->BuildRenderVertexAdjacency();
    }

    if (RenderVertexAdjacencyOffsets.Num() != RenderVertices.Num() + 1 || RenderVertexAdjacency.Num() == 0)
    {
        return 0.0;
    }

    const int32 StartOffset = RenderVertexAdjacencyOffsets[VertexIdx];
    const int32 EndOffset = RenderVertexAdjacencyOffsets[VertexIdx + 1];

    if (StartOffset == EndOffset)
    {
        return 0.0;
    }

    // Compute average elevation difference to neighbors
    const double CurrentElevation = VertexElevationValues.IsValidIndex(VertexIdx) ? VertexElevationValues[VertexIdx] : 0.0;
    double MaxElevationDiff = 0.0;

    for (int32 Offset = StartOffset; Offset < EndOffset; ++Offset)
    {
        const int32 NeighborIdx = RenderVertexAdjacency[Offset];
        if (!VertexElevationValues.IsValidIndex(NeighborIdx))
        {
            continue;
        }

        const double NeighborElevation = VertexElevationValues[NeighborIdx];
        const double ElevationDiff_m = FMath::Abs(CurrentElevation - NeighborElevation); // meters

        // Geodesic distance on unit sphere (radians → meters)
        // M5 Phase 3 fix: Use PlanetRadius parameter (not hardcoded Earth radius)
        const FVector3d& V1 = RenderVertices[VertexIdx];
        const FVector3d& V2 = RenderVertices[NeighborIdx];
        const double DotProduct = FMath::Clamp(FVector3d::DotProduct(V1.GetSafeNormal(), V2.GetSafeNormal()), -1.0, 1.0);
        const double GeodesicDistance_m = FMath::Acos(DotProduct) * Parameters.PlanetRadius;

        if (GeodesicDistance_m > 0.0)
        {
            const double Slope = ElevationDiff_m / GeodesicDistance_m; // Dimensionless rise/run (m/m)
            MaxElevationDiff = FMath::Max(MaxElevationDiff, Slope);
        }
    }

    return MaxElevationDiff; // Dimensionless slope (|∇h|)
}
