// Copyright 2024 Planetary Creation
#pragma once

#include "CoreMinimal.h"
#include "SphericalKDTree.h"

class UTectonicSimulationService;

/**
 * Helper that samples the current render mesh at arbitrary equirectangular UVs.
 * Uses a spherical KD-tree to locate the containing triangle and performs
 * barycentric interpolation on either the amplified or baseline elevation data.
 */
class FHeightmapSampler
{
public:
    static constexpr double PoleAvoidanceEpsilon = 1.0e-6;

    struct FSampleInfo
    {
        bool bHit = false;
        int32 TriangleIndex = INDEX_NONE;
        FVector3d Barycentrics = FVector3d::ZeroVector;
        int32 Steps = 0;
    };

    explicit FHeightmapSampler(const UTectonicSimulationService& Service);

    /** Returns true when the sampler has enough data to answer queries. */
    bool IsValid() const { return bIsValid; }

    /** Returns true when Stage B amplified elevations are being sampled. */
    bool UsesAmplifiedElevation() const { return bUseAmplified; }

    /** Returns true when Stage B snapshot float data is available and in use. */
    bool UsesSnapshotFloatBuffer() const { return bUseAmplified && bHasSnapshotFloatData; }

    /** Sample elevation (meters) at the provided UV coordinate. */
    double SampleElevationAtUV(const FVector2d& UV, FSampleInfo* OutInfo = nullptr) const;

    struct FMemoryStats
    {
        int32 VertexCount = 0;
        int32 TriangleCount = 0;
        bool bUsingAmplified = false;
        bool bHasSnapshotFloatBuffer = false;
        int64 TriangleDataBytes = 0;
        int64 TriangleDirectionsBytes = 0;
        int64 TriangleIdsBytes = 0;
        int64 KDTreeBytes = 0;
        int32 KDTreeNodeCount = 0;
        int64 SnapshotFloatBytes = 0;
    };

    FMemoryStats GetMemoryStats() const;

private:
    struct FTriangleData
    {
        int32 Vertices[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
        int32 Neighbors[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
    };

    /** Convert UV coordinates to a unit-length direction vector. */
    static FVector3d UVToDirection(const FVector2d& InUV);

    /** Compute barycentric coordinates of Direction relative to TriangleIndex. */
    bool ComputeTriangleBarycentrics(int32 TriangleIndex, const FVector3d& Direction, FVector3d& OutBary) const;

    /** Locate the triangle that contains Direction, returning barycentric weights. */
    bool FindContainingTriangle(const FVector3d& Direction, int32& OutTriangleIndex, FVector3d& OutBary, int32* OutSteps = nullptr) const;

    /** Lookup helper that picks amplified or baseline elevation depending on readiness. */
    double FetchElevation(int32 VertexIndex) const;

private:
    const TArray<FVector3d>& RenderVertices;
    const TArray<int32>& RenderTriangles;
    const TArray<double>& BaselineElevation;
    const TArray<double>& AmplifiedElevation;
    const TArray<float>* SnapshotAmplifiedElevation = nullptr;

    TArray<FTriangleData> TriangleData;
    TArray<FVector3d> TriangleDirections;
    TArray<int32> TriangleIds;

    FSphericalKDTree TriangleSearch;

    bool bUseAmplified = false;
    bool bIsValid = false;
    bool bHasSnapshotFloatData = false;
};
