#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "Simulation/StripackConfig.h"

/**
 * Lightweight spherical Delaunay wrapper. Points are expected to be unit vectors with 0-based indices.
 * Triangulation is provided by a backend selected via `r.PaperTriangulation.Backend` (Auto/Geogram/Stripack).
 * Geogram uses the convex hull in 3D (exact spherical Delaunay for points on the unit sphere);
 * STRIPACK provides a reference spherical Delaunay implementation. Results are canonicalized for determinism.
 */
class PLANETARYCREATIONEDITOR_API FSphericalDelaunay
{
public:
    struct FTriangle
    {
        int32 V0 = INDEX_NONE;
        int32 V1 = INDEX_NONE;
        int32 V2 = INDEX_NONE;
    };

    static void Triangulate(const TArray<FVector3d>& SpherePoints, TArray<FTriangle>& OutTriangles);

    static void ComputeVoronoiNeighbors(const TArray<FVector3d>& SpherePoints, const TArray<FTriangle>& Triangles, TArray<TArray<int32>>& OutNeighbors);

    /**
     * Compute Voronoi neighbors for each vertex with a deterministic cyclic ordering.
     *
     * - For each vertex i with position p_i on the unit sphere, neighbors are ordered counter‑clockwise (CCW)
     *   around the outward normal n = normalize(p_i).
     * - Ordering is backend‑agnostic and deterministic for identical inputs.
     * - Falls back to index-sorted neighbors if a degenerate local basis is detected.
     */
    static void ComputeVoronoiNeighborsCyclic(const TArray<FVector3d>& SpherePoints, const TArray<FTriangle>& Triangles, TArray<TArray<int32>>& OutNeighborsCyclic);

    static void BuildCSR(const TArray<TArray<int32>>& Neighbors, TArray<int32>& OutOffsets, TArray<int32>& OutAdjacency);
};
