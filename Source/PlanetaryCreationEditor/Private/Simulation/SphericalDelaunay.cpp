#include "Simulation/SphericalDelaunay.h"

#include "Algo/Sort.h"
#include "HAL/CriticalSection.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeLock.h"
#include "HAL/UnrealMemory.h"
#include "Simulation/SphericalTriangulatorFactory.h"
#include "Simulation/Triangulators/GeogramTriangulator.h"
#include "Simulation/Triangulators/StripackTriangulator.h"

namespace
{
    using FTriangle = FSphericalDelaunay::FTriangle;

    constexpr double kAngleEpsilon = 1e-12;
    constexpr double kBasisDegeneracyEpsilon = 1e-15;

    struct FTangentFrame
    {
        FVector3d N;  // normal (unit)
        FVector3d E1; // tangent axis 1 (unit)
        FVector3d E2; // tangent axis 2 (unit), E2 = N x E1
        bool bValid = false;
    };

    FTangentFrame MakeTangentFrame(const FVector3d& P)
    {
        FTangentFrame Frame;
        Frame.N = P.GetSafeNormal();
        // Choose a stable reference axis: Z unless nearly parallel, then X
        const FVector3d Ref = (FMath::Abs(Frame.N.Z) > 0.9) ? FVector3d(1.0, 0.0, 0.0) : FVector3d(0.0, 0.0, 1.0);
        FVector3d E1 = FVector3d::CrossProduct(Ref, Frame.N);
        const double LenE1 = E1.Length();
        if (LenE1 < kBasisDegeneracyEpsilon)
        {
            Frame.bValid = false;
            return Frame;
        }
        Frame.E1 = E1 / LenE1;
        Frame.E2 = FVector3d::CrossProduct(Frame.N, Frame.E1);
        Frame.bValid = true;
        return Frame;
    }

    double AzimuthAngleCCW(const FTangentFrame& Frame, const FVector3d& P)
    {
        // Project onto tangent plane implicitly using dot with E1,E2
        const double x = FVector3d::DotProduct(P, Frame.E1);
        const double y = FVector3d::DotProduct(P, Frame.E2);
        return FMath::Atan2(y, x); // [-pi, pi]
    }

    struct FTriangleKey
    {
        int32 Min = INDEX_NONE;
        int32 Mid = INDEX_NONE;
        int32 Max = INDEX_NONE;

        static FTriangleKey FromTriangle(const FTriangle& Triangle)
        {
            const int32 MinIndex = FMath::Min3(Triangle.V0, Triangle.V1, Triangle.V2);
            const int32 MaxIndex = FMath::Max3(Triangle.V0, Triangle.V1, Triangle.V2);
            const int32 MidIndex = Triangle.V0 + Triangle.V1 + Triangle.V2 - MinIndex - MaxIndex;
            return {MinIndex, MidIndex, MaxIndex};
        }

        friend bool operator==(const FTriangleKey& Lhs, const FTriangleKey& Rhs)
        {
            return Lhs.Min == Rhs.Min && Lhs.Mid == Rhs.Mid && Lhs.Max == Rhs.Max;
        }
    };

    FORCEINLINE bool IsValidTriangle(const TArray<FVector3d>& Points, const FTriangle& Triangle)
    {
        const int32 NumPoints = Points.Num();
        if (!Points.IsValidIndex(Triangle.V0) || !Points.IsValidIndex(Triangle.V1) || !Points.IsValidIndex(Triangle.V2))
        {
            return false;
        }

        return Triangle.V0 != Triangle.V1 && Triangle.V0 != Triangle.V2 && Triangle.V1 != Triangle.V2;
    }

    void EnsureOutwardWinding(const TArray<FVector3d>& Points, FTriangle& Triangle)
    {
        const FVector3d& A = Points[Triangle.V0];
        const FVector3d& B = Points[Triangle.V1];
        const FVector3d& C = Points[Triangle.V2];

        const FVector3d Cross = FVector3d::CrossProduct(B, C);
        const double Orientation = FVector3d::DotProduct(Cross, A);

        if (Orientation < 0.0)
        {
            Swap(Triangle.V1, Triangle.V2);
        }
    }

    void CanonicalizeTriangles(const TArray<FVector3d>& Points, TArray<FTriangle>& Triangles)
    {
        TArray<FTriangle> ValidTriangles;
        ValidTriangles.Reserve(Triangles.Num());

        for (FTriangle Triangle : Triangles)
        {
            if (!IsValidTriangle(Points, Triangle))
            {
                continue;
            }

            EnsureOutwardWinding(Points, Triangle);
            ValidTriangles.Add(Triangle);
        }

        ValidTriangles.Sort([](const FTriangle& A, const FTriangle& B)
        {
            const FTriangleKey KeyA = FTriangleKey::FromTriangle(A);
            const FTriangleKey KeyB = FTriangleKey::FromTriangle(B);

            if (KeyA.Min != KeyB.Min)
            {
                return KeyA.Min < KeyB.Min;
            }

            if (KeyA.Mid != KeyB.Mid)
            {
                return KeyA.Mid < KeyB.Mid;
            }

            if (KeyA.Max != KeyB.Max)
            {
                return KeyA.Max < KeyB.Max;
            }

            if (A.V0 != B.V0)
            {
                return A.V0 < B.V0;
            }

            if (A.V1 != B.V1)
            {
                return A.V1 < B.V1;
            }

            return A.V2 < B.V2;
        });

        int32 WriteIndex = 0;
        FTriangleKey PreviousKey;
        bool bHasPrevious = false;

        for (const FTriangle& Triangle : ValidTriangles)
        {
            const FTriangleKey CurrentKey = FTriangleKey::FromTriangle(Triangle);
            if (!bHasPrevious || !(CurrentKey == PreviousKey))
            {
                ValidTriangles[WriteIndex++] = Triangle;
                PreviousKey = CurrentKey;
                bHasPrevious = true;
            }
        }

        ValidTriangles.SetNum(WriteIndex);
        Triangles = MoveTemp(ValidTriangles);
    }

    uint64 HashPoints(const TArray<FVector3d>& Points)
    {
        constexpr uint64 FnvOffset = 14695981039346656037ull;
        constexpr uint64 FnvPrime = 1099511628211ull;
        uint64 Hash = FnvOffset;

        const int32 NumPoints = Points.Num();
        uint32 NumBytes = static_cast<uint32>(NumPoints);
        for (int32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
        {
            const uint8 Byte = static_cast<uint8>((NumBytes >> (ByteIndex * 8)) & 0xFF);
            Hash ^= static_cast<uint64>(Byte);
            Hash *= FnvPrime;
        }

        for (const FVector3d& Point : Points)
        {
            const double Components[3] = {Point.X, Point.Y, Point.Z};
            for (double Value : Components)
            {
                uint64 Bits = 0;
                FMemory::Memcpy(&Bits, &Value, sizeof(uint64));
                for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
                {
                    const uint8 Byte = static_cast<uint8>((Bits >> (ByteIndex * 8)) & 0xFF);
                    Hash ^= static_cast<uint64>(Byte);
                    Hash *= FnvPrime;
                }
            }
        }

        return Hash;
    }

    FCriticalSection GTriangulationCacheMutex;
    uint64 GTriangulationCachedHash = 0;
    int32 GTriangulationCachedShuffleValue = 0;
    int32 GTriangulationCachedShuffleSeed = 0;
    bool GTriangulationCacheValid = false;
    TArray<FTriangle> GTriangulationCachedTriangles;
    FString GTriangulationCachedBackend;
}

void FSphericalDelaunay::Triangulate(const TArray<FVector3d>& SpherePoints, TArray<FTriangle>& OutTriangles)
{
    OutTriangles.Reset();

    if (SpherePoints.Num() < 3)
    {
        return;
    }

    FString BackendName;
    bool bUsedFallback = false;
    ISphericalTriangulator& Backend = FSphericalTriangulatorFactory::Resolve(BackendName, bUsedFallback);
    const FString RequestedBackend = FSphericalTriangulatorFactory::GetConfiguredBackend();

    if (bUsedFallback)
    {
        UE_LOG(LogTemp, Warning, TEXT("Triangulation backend fallback: using %s (requested: %s)"), *BackendName, *RequestedBackend);
    }

    UE_LOG(LogTemp, Verbose, TEXT("FSphericalDelaunay::Triangulate invoking %s (N=%d)"),
        *BackendName, SpherePoints.Num());

    IConsoleVariable* ShuffleVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperTriangulation.Shuffle"));
    IConsoleVariable* ShuffleSeedVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperTriangulation.ShuffleSeed"));
    const int32 ShuffleValue = ShuffleVar ? ShuffleVar->GetInt() : 0;
    const int32 ShuffleSeed = ShuffleSeedVar ? ShuffleSeedVar->GetInt() : 0;
    const uint64 PointsHash = HashPoints(SpherePoints);

    {
        FScopeLock CacheLock(&GTriangulationCacheMutex);
        if (GTriangulationCacheValid &&
            GTriangulationCachedBackend.Equals(BackendName, ESearchCase::IgnoreCase) &&
            GTriangulationCachedHash == PointsHash &&
            GTriangulationCachedShuffleValue == ShuffleValue &&
            GTriangulationCachedShuffleSeed == ShuffleSeed)
        {
            OutTriangles = GTriangulationCachedTriangles;
            UE_LOG(LogTemp, Verbose, TEXT("FSphericalDelaunay::Triangulate returning cached triangulation (backend=%s hash=%016llX shuffle=%d seed=%d)"),
                *BackendName, PointsHash, ShuffleValue, ShuffleSeed);
            return;
        }
    }

    TArray<FTriangle> RawTriangles;
    const double TriangulateStart = FPlatformTime::Seconds();
    if (!Backend.Triangulate(SpherePoints, RawTriangles))
    {
        UE_LOG(LogTemp, Warning, TEXT("FSphericalDelaunay::Triangulate %s call failed"), *BackendName);
        return;
    }
    const double TriangulateEnd = FPlatformTime::Seconds();

    const double CanonicalizeStart = FPlatformTime::Seconds();
    CanonicalizeTriangles(SpherePoints, RawTriangles);
    const double CanonicalizeEnd = FPlatformTime::Seconds();

    {
        FScopeLock CacheLock(&GTriangulationCacheMutex);
        GTriangulationCachedHash = PointsHash;
        GTriangulationCachedShuffleValue = ShuffleValue;
        GTriangulationCachedShuffleSeed = ShuffleSeed;
        GTriangulationCachedTriangles = RawTriangles;
        GTriangulationCacheValid = true;
        GTriangulationCachedBackend = BackendName;
    }

    OutTriangles = MoveTemp(RawTriangles);
    UE_LOG(LogTemp, Verbose, TEXT("FSphericalDelaunay::Triangulate completed. Backend=%s Triangles=%d (Compute=%.2f ms Canonicalize=%.2f ms)"),
        *BackendName,
        OutTriangles.Num(),
        (TriangulateEnd - TriangulateStart) * 1000.0,
        (CanonicalizeEnd - CanonicalizeStart) * 1000.0);
}

void FSphericalDelaunay::ComputeVoronoiNeighbors(const TArray<FVector3d>& SpherePoints, const TArray<FTriangle>& Triangles, TArray<TArray<int32>>& OutNeighbors)
{
    const int32 NumPoints = SpherePoints.Num();
    OutNeighbors.SetNum(NumPoints);

    for (int32 VertexIndex = 0; VertexIndex < NumPoints; ++VertexIndex)
    {
        OutNeighbors[VertexIndex].Reset();
    }

    for (const FTriangle& Triangle : Triangles)
    {
        const int32 A = Triangle.V0;
        const int32 B = Triangle.V1;
        const int32 C = Triangle.V2;

        if (!SpherePoints.IsValidIndex(A) || !SpherePoints.IsValidIndex(B) || !SpherePoints.IsValidIndex(C))
        {
            continue;
        }

        OutNeighbors[A].AddUnique(B);
        OutNeighbors[A].AddUnique(C);

        OutNeighbors[B].AddUnique(A);
        OutNeighbors[B].AddUnique(C);

        OutNeighbors[C].AddUnique(A);
        OutNeighbors[C].AddUnique(B);
    }

    for (int32 VertexIndex = 0; VertexIndex < NumPoints; ++VertexIndex)
    {
        Algo::Sort(OutNeighbors[VertexIndex]);
    }
}

void FSphericalDelaunay::ComputeVoronoiNeighborsCyclic(const TArray<FVector3d>& SpherePoints, const TArray<FTriangle>& Triangles, TArray<TArray<int32>>& OutNeighborsCyclic)
{
    const int32 NumPoints = SpherePoints.Num();
    OutNeighborsCyclic.SetNum(NumPoints);

    for (int32 VertexIndex = 0; VertexIndex < NumPoints; ++VertexIndex)
    {
        OutNeighborsCyclic[VertexIndex].Reset();
    }

    // Build unique neighbor sets exactly like ComputeVoronoiNeighbors
    for (const FTriangle& Triangle : Triangles)
    {
        const int32 A = Triangle.V0;
        const int32 B = Triangle.V1;
        const int32 C = Triangle.V2;

        if (!SpherePoints.IsValidIndex(A) || !SpherePoints.IsValidIndex(B) || !SpherePoints.IsValidIndex(C))
        {
            continue;
        }

        OutNeighborsCyclic[A].AddUnique(B);
        OutNeighborsCyclic[A].AddUnique(C);

        OutNeighborsCyclic[B].AddUnique(A);
        OutNeighborsCyclic[B].AddUnique(C);

        OutNeighborsCyclic[C].AddUnique(A);
        OutNeighborsCyclic[C].AddUnique(B);
    }

    // Sort neighbors CCW around outward normal at each vertex
    struct FNeighborAngle { int32 Neighbor; double Angle; };
    TArray<FNeighborAngle> Angles;

    for (int32 VertexIndex = 0; VertexIndex < NumPoints; ++VertexIndex)
    {
        TArray<int32>& Nbs = OutNeighborsCyclic[VertexIndex];
        if (Nbs.Num() <= 1)
        {
            // Nothing to sort
            Algo::Sort(Nbs);
            continue;
        }

        const FVector3d& P = SpherePoints[VertexIndex];
        const FTangentFrame Frame = MakeTangentFrame(P);

        if (!Frame.bValid)
        {
            Algo::Sort(Nbs);
            continue;
        }

        Angles.Reset();
        Angles.Reserve(Nbs.Num());
        for (int32 Neighbor : Nbs)
        {
            if (!SpherePoints.IsValidIndex(Neighbor))
            {
                continue; // should not happen if inputs are valid
            }
            const double Theta = AzimuthAngleCCW(Frame, SpherePoints[Neighbor]);
            Angles.Add({Neighbor, Theta});
        }

        // Stable sort by angle ascending, tie-break by index for determinism
        Algo::StableSort(Angles, [](const FNeighborAngle& A, const FNeighborAngle& B)
        {
            const double Diff = A.Angle - B.Angle;
            if (FMath::Abs(Diff) <= kAngleEpsilon)
            {
                return A.Neighbor < B.Neighbor;
            }
            return Diff < 0.0;
        });

        // Write back in sorted order
        Nbs.SetNumUninitialized(Angles.Num());
        for (int32 k = 0; k < Angles.Num(); ++k)
        {
            Nbs[k] = Angles[k].Neighbor;
        }
    }
}

void FSphericalDelaunay::BuildCSR(const TArray<TArray<int32>>& Neighbors, TArray<int32>& OutOffsets, TArray<int32>& OutAdjacency)
{
    const int32 NumVertices = Neighbors.Num();
    OutOffsets.SetNum(NumVertices + 1);

    int32 Accumulated = 0;
    for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
    {
        OutOffsets[VertexIndex] = Accumulated;
        Accumulated += Neighbors[VertexIndex].Num();
    }
    OutOffsets[NumVertices] = Accumulated;

    OutAdjacency.SetNumUninitialized(Accumulated);

    int32 WriteIndex = 0;
    for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
    {
        const TArray<int32>& VertexNeighbors = Neighbors[VertexIndex];
        for (int32 Neighbor : VertexNeighbors)
        {
            OutAdjacency[WriteIndex++] = Neighbor;
        }
    }
}
