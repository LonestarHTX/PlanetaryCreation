#include "HeightmapSampling.h"

#include "SphericalKDTree.h"
#include "StageBAmplificationTypes.h"
#include "TectonicSimulationService.h"

#include "Containers/Map.h"
#include "Math/UnrealMathUtility.h"
#include <cfloat>

namespace
{
    /** Helper to generate a deterministic key for an undirected edge (A,B). */
    uint64 MakeEdgeKey(int32 A, int32 B)
    {
        const uint32 MinIndex = static_cast<uint32>(FMath::Min(A, B));
        const uint32 MaxIndex = static_cast<uint32>(FMath::Max(A, B));
        return (static_cast<uint64>(MinIndex) << 32) | static_cast<uint64>(MaxIndex);
    }
}

FHeightmapSampler::FHeightmapSampler(const UTectonicSimulationService& Service)
    : RenderVertices(Service.GetRenderVertices())
    , RenderTriangles(Service.GetRenderTriangles())
    , BaselineElevation(Service.GetVertexElevationValues())
    , AmplifiedElevation(Service.GetVertexAmplifiedElevation())
    , bUseAmplified(Service.IsStageBAmplificationReady() && AmplifiedElevation.Num() == RenderVertices.Num())
{
    if (bUseAmplified)
    {
        const TArray<float>* FloatBaseline = nullptr;
        const TArray<FVector4f>* DummyRidge = nullptr;
        const TArray<float>* DummyCrust = nullptr;
        const TArray<FVector3f>* DummyPositions = nullptr;
        const TArray<uint32>* DummyMask = nullptr;
        Service.GetOceanicAmplificationFloatInputs(FloatBaseline, DummyRidge, DummyCrust, DummyPositions, DummyMask);

        SnapshotAmplifiedElevation = FloatBaseline;
        bHasSnapshotFloatData =
            SnapshotAmplifiedElevation != nullptr &&
            SnapshotAmplifiedElevation->Num() == RenderVertices.Num();
    }

    const int32 TriangleCount = RenderTriangles.Num() / 3;

    if (RenderVertices.Num() == 0 || TriangleCount == 0 || BaselineElevation.Num() != RenderVertices.Num())
    {
        return;
    }

    TriangleData.SetNum(TriangleCount);
    TriangleDirections.SetNum(TriangleCount);
    TriangleIds.SetNum(TriangleCount);

    // Build adjacency by tracking the owning triangle for each undirected edge.
    TMap<uint64, TPair<int32, int32>> EdgeOwners;
    EdgeOwners.Reserve(TriangleCount * 3);

    for (int32 TriangleIdx = 0; TriangleIdx < TriangleCount; ++TriangleIdx)
    {
        const int32 IndexBase = TriangleIdx * 3;

        FTriangleData& Data = TriangleData[TriangleIdx];
        Data.Vertices[0] = RenderTriangles[IndexBase + 0];
        Data.Vertices[1] = RenderTriangles[IndexBase + 1];
        Data.Vertices[2] = RenderTriangles[IndexBase + 2];
        Data.Neighbors[0] = INDEX_NONE;
        Data.Neighbors[1] = INDEX_NONE;
        Data.Neighbors[2] = INDEX_NONE;

        const FVector3d& A = RenderVertices[Data.Vertices[0]];
        const FVector3d& B = RenderVertices[Data.Vertices[1]];
        const FVector3d& C = RenderVertices[Data.Vertices[2]];

        const FVector3d Centroid = (A + B + C).GetSafeNormal();
        TriangleDirections[TriangleIdx] = Centroid;
        TriangleIds[TriangleIdx] = TriangleIdx;

        for (int32 Edge = 0; Edge < 3; ++Edge)
        {
            const int32 VertexA = Data.Vertices[Edge];
            const int32 VertexB = Data.Vertices[(Edge + 1) % 3];
            const uint64 EdgeKey = MakeEdgeKey(VertexA, VertexB);

            if (TPair<int32, int32>* Existing = EdgeOwners.Find(EdgeKey))
            {
                // Existing triangle shares this edge: wire up adjacency both ways.
                const int32 OtherTriangle = Existing->Key;
                const int32 OtherEdge = Existing->Value;
                if (TriangleData.IsValidIndex(OtherTriangle))
                {
                    TriangleData[OtherTriangle].Neighbors[OtherEdge] = TriangleIdx;
                }
                Data.Neighbors[Edge] = OtherTriangle;
            }
            else
            {
                EdgeOwners.Add(EdgeKey, TPair<int32, int32>(TriangleIdx, Edge));
            }
        }
    }

    TriangleSearch.Build(TriangleDirections, TriangleIds);
    bIsValid = TriangleSearch.IsValid();
}

double FHeightmapSampler::SampleElevationAtUV(const FVector2d& UV, FSampleInfo* OutInfo) const
{
    if (OutInfo)
    {
        *OutInfo = FSampleInfo();
    }

    if (!bIsValid)
    {
        return 0.0;
    }

    const FVector3d Direction = UVToDirection(UV);

    int32 TriangleIndex = INDEX_NONE;
    FVector3d Barycentric;
    int32 StepsTaken = 0;
    if (!FindContainingTriangle(Direction, TriangleIndex, Barycentric, &StepsTaken))
    {
        if (OutInfo)
        {
            OutInfo->bHit = false;
            OutInfo->TriangleIndex = TriangleIndex;
            OutInfo->Barycentrics = FVector3d::ZeroVector;
            OutInfo->Steps = StepsTaken;
        }

        return 0.0;
    }

    if (OutInfo)
    {
        OutInfo->bHit = true;
        OutInfo->TriangleIndex = TriangleIndex;
        OutInfo->Barycentrics = Barycentric;
        OutInfo->Steps = StepsTaken;
    }

    const FTriangleData& Triangle = TriangleData[TriangleIndex];

    const double Elev0 = FetchElevation(Triangle.Vertices[0]);
    const double Elev1 = FetchElevation(Triangle.Vertices[1]);
    const double Elev2 = FetchElevation(Triangle.Vertices[2]);

    return (Barycentric.X * Elev0) + (Barycentric.Y * Elev1) + (Barycentric.Z * Elev2);
}

FHeightmapSampler::FMemoryStats FHeightmapSampler::GetMemoryStats() const
{
    FMemoryStats Stats;
    Stats.VertexCount = RenderVertices.Num();
    Stats.TriangleCount = TriangleData.Num();
    Stats.bUsingAmplified = bUseAmplified;
    Stats.bHasSnapshotFloatBuffer = bHasSnapshotFloatData;
    Stats.TriangleDataBytes = static_cast<int64>(TriangleData.GetAllocatedSize());
    Stats.TriangleDirectionsBytes = static_cast<int64>(TriangleDirections.GetAllocatedSize());
    Stats.TriangleIdsBytes = static_cast<int64>(TriangleIds.GetAllocatedSize());

    const FSphericalKDTree::FMemoryUsage KDUsage = TriangleSearch.EstimateMemoryUsage();
    Stats.KDTreeBytes = KDUsage.NodeBytes;
    Stats.KDTreeNodeCount = KDUsage.NodeCount;

    if (bHasSnapshotFloatData && SnapshotAmplifiedElevation != nullptr)
    {
        Stats.SnapshotFloatBytes = static_cast<int64>(SnapshotAmplifiedElevation->GetAllocatedSize());
    }

    return Stats;
}

FVector3d FHeightmapSampler::UVToDirection(const FVector2d& InUV)
{
    return PlanetaryCreation::StageB::DirectionFromEquirectUV(InUV, PoleAvoidanceEpsilon);
}

bool FHeightmapSampler::ComputeTriangleBarycentrics(int32 TriangleIndex, const FVector3d& Direction, FVector3d& OutBary) const
{
    if (!TriangleData.IsValidIndex(TriangleIndex))
    {
        return false;
    }

    const FTriangleData& Triangle = TriangleData[TriangleIndex];
    const FVector3d& A = RenderVertices[Triangle.Vertices[0]];
    const FVector3d& B = RenderVertices[Triangle.Vertices[1]];
    const FVector3d& C = RenderVertices[Triangle.Vertices[2]];

    const FVector3d V0 = B - A;
    const FVector3d V1 = C - A;

    const FVector3d Normal = FVector3d::CrossProduct(V0, V1);
    const double NormalLengthSq = Normal.SizeSquared();
    if (NormalLengthSq <= UE_DOUBLE_SMALL_NUMBER)
    {
        return false;
    }

    const double PlaneDistance = FVector3d::DotProduct(Direction - A, Normal) / NormalLengthSq;
    const FVector3d Projected = Direction - (PlaneDistance * Normal);
    const FVector3d V2 = Projected - A;

    const double D00 = FVector3d::DotProduct(V0, V0);
    const double D01 = FVector3d::DotProduct(V0, V1);
    const double D11 = FVector3d::DotProduct(V1, V1);
    const double D20 = FVector3d::DotProduct(V2, V0);
    const double D21 = FVector3d::DotProduct(V2, V1);

    const double Denominator = D00 * D11 - D01 * D01;
    if (FMath::IsNearlyZero(Denominator))
    {
        return false;
    }

    const double InvDenom = 1.0 / Denominator;
    const double V = (D11 * D20 - D01 * D21) * InvDenom;
    const double W = (D00 * D21 - D01 * D20) * InvDenom;
    const double U = 1.0 - V - W;

    OutBary = FVector3d(U, V, W);
    return true;
}

bool FHeightmapSampler::FindContainingTriangle(const FVector3d& Direction, int32& OutTriangleIndex, FVector3d& OutBary, int32* OutSteps) const
{
    double NearestDistSq = 0.0;
    int32 TriangleIndex = TriangleSearch.FindNearest(Direction, NearestDistSq);

    if (TriangleIndex == INDEX_NONE)
    {
        if (OutSteps)
        {
            *OutSteps = 0;
        }
        return false;
    }

    int32 PreviousTriangle = INDEX_NONE;
    int32 StepsTaken = 0;
    constexpr double InsideTolerance = -1.0e-6;
    constexpr double AcceptanceTolerance = -1.0e-3;
    constexpr int32 MaxTraversalSteps = 32;

    TArray<int32, TInlineAllocator<MaxTraversalSteps>> Visited;
    Visited.Add(TriangleIndex);

    double BestScore = -DBL_MAX;
    int32 BestTriangle = INDEX_NONE;
    FVector3d BestBary = FVector3d::ZeroVector;

    for (; StepsTaken < MaxTraversalSteps && TriangleIndex != INDEX_NONE; ++StepsTaken)
    {
        FVector3d Bary;
        if (!ComputeTriangleBarycentrics(TriangleIndex, Direction, Bary))
        {
            break;
        }

        const double MinCoord = FMath::Min3(Bary.X, Bary.Y, Bary.Z);
        if (MinCoord > BestScore)
        {
            BestScore = MinCoord;
            BestTriangle = TriangleIndex;
            BestBary = Bary;
        }

        if (MinCoord >= InsideTolerance)
        {
            OutTriangleIndex = TriangleIndex;
            OutBary = Bary;
            if (OutSteps)
            {
                *OutSteps = StepsTaken + 1;
            }
            return true;
        }

        // Determine which edges have negative barycentric weights and try adjacent faces.
        struct FEdgeCandidate
        {
            double Weight;
            int32 Edge;
        };

        FEdgeCandidate Candidates[3] = {
            { Bary.X, 0 },
            { Bary.Y, 1 },
            { Bary.Z, 2 }
        };

        // Bubble sort the three entries so we visit the most negative first.
        for (int32 I = 0; I < 2; ++I)
        {
            for (int32 J = I + 1; J < 3; ++J)
            {
                if (Candidates[J].Weight < Candidates[I].Weight)
                {
                    Swap(Candidates[I], Candidates[J]);
                }
            }
        }

        bool bAdvanced = false;

        for (const FEdgeCandidate& Candidate : Candidates)
        {
            if (Candidate.Weight >= InsideTolerance)
            {
                continue;
            }

            const int32 Neighbor = TriangleData[TriangleIndex].Neighbors[Candidate.Edge];
            if (Neighbor != INDEX_NONE && Neighbor != PreviousTriangle && !Visited.Contains(Neighbor))
            {
                PreviousTriangle = TriangleIndex;
                TriangleIndex = Neighbor;
                Visited.Add(Neighbor);
                bAdvanced = true;
                break;
            }
        }

        if (!bAdvanced)
        {
            break;
        }
    }

    if (BestTriangle != INDEX_NONE && BestScore >= AcceptanceTolerance)
    {
        FVector3d ClampedBary = BestBary;
        ClampedBary.X = FMath::Clamp(ClampedBary.X, 0.0, 1.0);
        ClampedBary.Y = FMath::Clamp(ClampedBary.Y, 0.0, 1.0);
        ClampedBary.Z = FMath::Clamp(ClampedBary.Z, 0.0, 1.0);

        const double Sum = ClampedBary.X + ClampedBary.Y + ClampedBary.Z;
        if (Sum > UE_DOUBLE_SMALL_NUMBER)
        {
            ClampedBary /= Sum;
        }

        OutTriangleIndex = BestTriangle;
        OutBary = ClampedBary;
        if (OutSteps)
        {
            *OutSteps = StepsTaken;
        }
        return true;
    }

    if (OutSteps)
    {
        *OutSteps = StepsTaken;
    }

    return false;
}

double FHeightmapSampler::FetchElevation(int32 VertexIndex) const
{
    if (VertexIndex == INDEX_NONE)
    {
        return 0.0;
    }

    if (bUseAmplified && AmplifiedElevation.IsValidIndex(VertexIndex))
    {
        if (SnapshotAmplifiedElevation && SnapshotAmplifiedElevation->IsValidIndex(VertexIndex))
        {
            return static_cast<double>((*SnapshotAmplifiedElevation)[VertexIndex]);
        }

        return AmplifiedElevation[VertexIndex];
    }

    return BaselineElevation.IsValidIndex(VertexIndex) ? BaselineElevation[VertexIndex] : 0.0;
}
