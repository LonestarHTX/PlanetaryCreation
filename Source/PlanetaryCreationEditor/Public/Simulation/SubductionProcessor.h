#pragma once

#include "CoreMinimal.h"
#include "Simulation/BoundaryField.h"
#include "Simulation/PaperProfiling.h"

namespace Subduction
{
    struct PLANETARYCREATIONEDITOR_API FSubductionMetrics
    {
        int32 VerticesTouched = 0;
        double TotalUplift_m = 0.0;
        double MaxUplift_m = 0.0;
        double ApplyMs = 0.0;
    };

    struct PLANETARYCREATIONEDITOR_API FFoldMetrics
    {
        int32 VerticesUpdated = 0;
        double MeanDelta = 0.0;
        double MaxDelta = 0.0;
        double MeanCoherence = 0.0; // mean |dot(f, rel_dir)| over updated vertices
        double ApplyMs = 0.0;
    };

    struct PLANETARYCREATIONEDITOR_API FSlabPullMetrics
    {
        int32 PlatesUpdated = 0;
        double MeanDeltaOmega = 0.0;
        double MaxDeltaOmega = 0.0;
        double ApplyMs = 0.0;
    };

    // Apply û = u0·f(d)·g(v)·h(z~) to elevations (meters), using:
    // - Points: unit vectors on sphere
    // - CSR adjacency: Offsets/Adj
    // - PlateIdPerVertex: plate id for each vertex
    // - OmegaPerPlate: angular velocity vectors (rad/My) per plate
    // Elevations are updated in-place. Returns metrics and optionally logs if profiling is enabled.
    PLANETARYCREATIONEDITOR_API FSubductionMetrics ApplyUplift(
        const TArray<FVector3d>& Points,
        const TArray<int32>& CSR_Offsets,
        const TArray<int32>& CSR_Adj,
        const TArray<int32>& PlateIdPerVertex,
        const TArray<FVector3d>& OmegaPerPlate,
        TArray<double>& InOutElevation_m);

    // Fold direction update: neighbor-aware using CSR adjacency and boundary results
    PLANETARYCREATIONEDITOR_API FFoldMetrics UpdateFoldDirections(
        const TArray<FVector3d>& Points,
        const TArray<int32>& CSR_Offsets,
        const TArray<int32>& CSR_Adj,
        const TArray<int32>& PlateIdPerVertex,
        const TArray<FVector3d>& OmegaPerPlate,
        const BoundaryField::FBoundaryFieldResults& Boundary,
        TArray<FVector3d>& InOutFoldVectors);

    struct FConvergentEdge
    {
        int32 A = INDEX_NONE;
        int32 B = INDEX_NONE;
        int32 SubductingPlateId = INDEX_NONE;
        int32 OverridingPlateId = INDEX_NONE;
    };

    // Slab pull: adjust per-plate omega by summing normalized (c_i x q_k) for convergent front midpoints
    PLANETARYCREATIONEDITOR_API FSlabPullMetrics ApplySlabPull(
        const TArray<FVector3d>& PlateCentroids,
        const TArray<FConvergentEdge>& ConvergentEdges,
        const TArray<FVector3d>& Points,
        TArray<FVector3d>& InOutOmegaPerPlate);

    // Helper: Write Phase 3 metrics JSON and return file path
    PLANETARYCREATIONEDITOR_API FString WritePhase3MetricsJson(
        const FString& TestName,
        const FString& Backend,
        int32 SampleCount,
        int32 Seed,
        int32 SimulationSteps,
        int32 ConvergentCount,
        int32 DivergentCount,
        int32 TransformCount,
        const FSubductionMetrics& Uplift,
        const FFoldMetrics& Fold,
        double ClassifyMs,
        const FSlabPullMetrics& Slab);
}
