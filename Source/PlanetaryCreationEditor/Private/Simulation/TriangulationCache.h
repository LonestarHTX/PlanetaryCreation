#pragma once

#include "CoreMinimal.h"
#include "Simulation/SphericalDelaunay.h"

namespace TriCache
{
    struct FTriangulationMeta
    {
        int32 N = 0;
        int32 Seed = 0;
        bool bShuffle = false;
        uint64 Signature = 0;
    };

    bool Load(const FString& CacheDir,
        const FTriangulationMeta& Key,
        TArray<FVector3d>& OutPoints,
        TArray<FSphericalDelaunay::FTriangle>& OutTris,
        FTriangulationMeta& OutMeta,
        double& OutLoadSeconds);

    bool Save(const FString& CacheDir,
        const FTriangulationMeta& Meta,
        const TArray<FVector3d>& Points,
        const TArray<FSphericalDelaunay::FTriangle>& Tris,
        FString& OutPath,
        double& OutSaveSeconds);

    uint64 ComputeTriangleSetHash(const TArray<FSphericalDelaunay::FTriangle>& CanonicalTris);

    void CanonicalizeTriangles(TArray<FSphericalDelaunay::FTriangle>& InOutTriangles);
}
