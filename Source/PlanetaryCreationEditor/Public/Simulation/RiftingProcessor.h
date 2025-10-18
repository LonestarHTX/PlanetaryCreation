#pragma once

#include "CoreMinimal.h"

namespace Rifting
{
    struct PLANETARYCREATIONEDITOR_API FRiftingEvent
    {
        int32 PlateId = INDEX_NONE;
        int32 FragmentCount = 0;       // [2,4]
        double PlateArea_km2 = 0.0;    // pre-split area
        double ContinentalRatio = 0.0; // 0..1
        int32 Seed = 0;                // deterministic seed
    };

    struct PLANETARYCREATIONEDITOR_API FRiftingMetrics
    {
        int32 RiftingCount = 0;
        double MeanFragments = 0.0;
        double ApplyMs = 0.0;
    };

    // Evaluate whether a plate should rift this cadence based on a simple probabilistic model.
    PLANETARYCREATIONEDITOR_API bool EvaluateRiftingProbability(
        int32 PlateId,
        double PlateArea_km2,
        double ContinentalRatio,
        double LambdaBase,
        double A0_km2,
        FRiftingEvent& OutEvent);

    // Perform a plate split into FragmentCount fragments, updating assignments and producing per-fragment drift.
    PLANETARYCREATIONEDITOR_API bool PerformRifting(
        const FRiftingEvent& Event,
        const TArray<FVector3d>& Points,
        const TArray<int32>& CSR_Offsets,
        const TArray<int32>& CSR_Adj,
        const TArray<int32>& PlateIdPerVertexIn,
        TArray<int32>& PlateIdPerVertexOut,
        TArray<FVector3d>& OutFragmentDriftDirections,
        FRiftingMetrics& InOutMetrics,
        TArray<TPair<int32,double>>* OutFragmentPlateRatiosOrNull = nullptr);

    // Append rifting metrics to an existing Phase 4 JSON (or create one if missing).
    PLANETARYCREATIONEDITOR_API FString WritePhase4MetricsJsonAppendRifting(
        const FString& ExistingPhase4JsonPath,
        const FRiftingMetrics& Metrics);
}
