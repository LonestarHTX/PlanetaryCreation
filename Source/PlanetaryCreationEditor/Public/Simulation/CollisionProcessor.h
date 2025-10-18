#pragma once

#include "CoreMinimal.h"
#include "Simulation/BoundaryField.h"

namespace Collision
{
    struct PLANETARYCREATIONEDITOR_API FCollisionEvent
    {
        FVector3d CenterUnit = FVector3d::ZeroVector;
        double TerraneArea_km2 = 0.0;
        int32 CarrierPlateId = INDEX_NONE;
        int32 TargetPlateId = INDEX_NONE;
        double PeakGuardrail_m = 0.0; // 0 disables guardrail
    };

    struct PLANETARYCREATIONEDITOR_API FCollisionMetrics
    {
        int32 CollisionCount = 0;
        double MaxPeak_m = 0.0;
        double ApplyMs = 0.0;
    };

    // Detection is deterministic. Uses Boundary (Phase 3 results) to find continental-continental convergences.
    PLANETARYCREATIONEDITOR_API bool DetectCollisions(
        const TArray<FVector3d>& Points,
        const TArray<int32>& PlateIdPerVertex,
        const TArray<FVector3d>& OmegaPerPlate,     // rad/My
        const TArray<uint8>& PlateCrustType,        // 0=oceanic, 1=continental
        const TArray<int32>& CSR_Offsets,
        const TArray<int32>& CSR_Adj,
        const BoundaryField::FBoundaryFieldResults& Boundary,
        TArray<FCollisionEvent>& OutEvents);

    // Apply quartic collision surge once to affected vertices. Optionally set radial folds.
    PLANETARYCREATIONEDITOR_API FCollisionMetrics ApplyCollisionSurge(
        const TArray<FVector3d>& Points,
        const TArray<int32>& AffectedVertexIndices,
        const FCollisionEvent& Event,
        TArray<double>& InOutElevation_m,
        TArray<FVector3d>* InOutFoldVectorsOrNull);

    // Write Phase 4 metrics JSON; returns path to artifact.
    PLANETARYCREATIONEDITOR_API FString WritePhase4MetricsJson(
        const FString& BackendName,
        int32 SampleCount,
        int32 Seed,
        const FCollisionMetrics& Metrics);
}

