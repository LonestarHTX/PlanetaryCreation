#pragma once

#include "CoreMinimal.h"
#include "Simulation/PaperConstants.h"

// BoundaryField.h
// Deterministic classification of Voronoi/Delaunay neighbor edges and multi-source
// geodesic distance fields to subduction fronts (convergent) and ridges (divergent).

namespace BoundaryField
{
    // Classification for undirected neighbor edges. Separate from the runtime EBoundaryType
    // to allow an explicit Interior state without impacting simulation enums.
    enum class EBoundaryClass : uint8
    {
        Convergent,
        Divergent,
        Transform,
        Interior
    };

    struct PLANETARYCREATIONEDITOR_API FBoundaryFieldMetrics
    {
        int32 NumEdges = 0;
        int32 NumInterior = 0;
        int32 NumDivergent = 0;
        int32 NumConvergent = 0;
        int32 NumTransform = 0;

        double LengthInterior_km = 0.0;
        double LengthDivergent_km = 0.0;
        double LengthConvergent_km = 0.0;
        double LengthTransform_km = 0.0;
    };

    struct PLANETARYCREATIONEDITOR_API FBoundaryFieldResults
    {
        // Undirected edge list aligned with Classifications
        TArray<TPair<int32,int32>> Edges;
        TArray<EBoundaryClass> Classifications;

        // Per-vertex geodesic distance fields (km)
        TArray<double> DistanceToSubductionFront_km;
        TArray<double> DistanceToRidge_km;
        TArray<double> DistanceToPlateBoundary_km; // any boundary type

        FBoundaryFieldMetrics Metrics;
    };

    // Core entry point: classify edges and compute distance fields.
    // - Points: unit vectors on sphere
    // - Neighbors: adjacency as vector-of-vectors (Voronoi neighbors)
    // - PlateAssignments: per-vertex plate id
    // - PlateAngularVelocities: per-plate angular velocity vector (rad/My)
    // TransformEpsilon_km_per_My:
    // - If < 0, uses CVar r.PaperBoundary.TransformEpsilonKmPerMy (default 1e-3 km/My)
    // - Previously defaulted to 1e-12 km/My; paper does not specify this threshold
    void PLANETARYCREATIONEDITOR_API ComputeBoundaryFields(
        const TArray<FVector3d>& Points,
        const TArray<TArray<int32>>& Neighbors,
        const TArray<int32>& PlateAssignments,
        const TArray<FVector3d>& PlateAngularVelocities,
        FBoundaryFieldResults& OutResults,
        double TransformEpsilon_km_per_My = -1.0);
}
