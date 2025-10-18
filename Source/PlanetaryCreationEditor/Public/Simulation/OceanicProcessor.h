#pragma once

#include "CoreMinimal.h"
#include "Simulation/BoundaryField.h"

namespace Oceanic
{
    struct PLANETARYCREATIONEDITOR_API FOceanicMetrics
    {
        int32 VerticesUpdated = 0;
        double MeanAlpha = 0.0;
        double MinAlpha = 1.0;
        double MaxAlpha = 0.0;
        double RidgeLength_km = 0.0;
        int32 CadenceSteps = 0;
        double ApplyMs = 0.0;
    };

    struct PLANETARYCREATIONEDITOR_API FRidgeCache
    {
        TArray<FVector3f> RidgeDirections; // unit tangent per vertex; zero if unset
        int32 Version = 0;
    };

    PLANETARYCREATIONEDITOR_API void BuildRidgeCache(
        const TArray<FVector3d>& Points,
        const TArray<int32>& CSR_Offsets,
        const TArray<int32>& CSR_Adj,
        const BoundaryField::FBoundaryFieldResults& Boundary,
        FRidgeCache& InOutCache);

    PLANETARYCREATIONEDITOR_API FOceanicMetrics ApplyOceanicCrust(
        const TArray<FVector3d>& Points,
        const TArray<int32>& CSR_Offsets,
        const TArray<int32>& CSR_Adj,
        const BoundaryField::FBoundaryFieldResults& Boundary,
        const TArray<int32>& PlateIdPerVertex,
        const TArray<uint8>& PlateCrustTypePerPlate,
        const TArray<double>& PlateBaselineElevation_m,
        TArray<double>& InOutElevation_m,
        FRidgeCache* OptionalRidgeCacheOrNull);

    PLANETARYCREATIONEDITOR_API FString WritePhase5MetricsJson(
        const FString& BackendName,
        int32 SampleCount,
        int32 Seed,
        const FOceanicMetrics& Metrics);
}
