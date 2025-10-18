#include "Simulation/BoundaryField.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Queue.h"
#include <queue>

namespace BoundaryField
{
    // Configurable transform threshold epsilon (km/My). Default relaxed to 1e-3.
    // Previously hard-coded to 1e-12; paper does not specify the threshold.
    static TAutoConsoleVariable<float> CVarPaperBoundaryTransformEpsilonKmPerMy(
        TEXT("r.PaperBoundary.TransformEpsilonKmPerMy"),
        0.001f,
        TEXT("Transform classification epsilon (km/My). |p| <= epsilon => Transform. Default 1e-3. Previously 1e-12."),
        ECVF_Default);
    static inline double GeodesicKm(const FVector3d& A, const FVector3d& B)
    {
        using namespace PaperConstants;
        const double dot = FMath::Clamp(A.Dot(B), -1.0, 1.0);
        const double theta = FMath::Acos(dot);
        return GeodesicRadiansToKm(theta);
    }

    static inline FVector3d TangentEdgeDirection(const FVector3d& A, const FVector3d& B, const FVector3d& M)
    {
        // Project chord onto tangent plane at midpoint M to get edge-tangent direction
        const FVector3d Chord = B - A;
        const FVector3d Tangent = Chord - (Chord.Dot(M)) * M;
        return Tangent.SizeSquared() > 0.0 ? Tangent.GetSafeNormal() : FVector3d::ZeroVector;
    }

    static inline FVector3d BoundaryNormalAtMidpoint(const FVector3d& A, const FVector3d& B, const FVector3d& M)
    {
        // Normal in tangent plane orthogonal to edge direction: n_b = normalize(M x t_edge)
        const FVector3d t = TangentEdgeDirection(A, B, M);
        const FVector3d n = FVector3d::CrossProduct(M, t);
        return n.SizeSquared() > 0.0 ? n.GetSafeNormal() : FVector3d::ZeroVector;
    }

    static inline FVector3d SurfaceVelocityKmPerMy(const FVector3d& Omega_rad_per_My, const FVector3d& M)
    {
        using namespace PaperConstants;
        // v = (omega x M) * R (km/My)
        return FVector3d::CrossProduct(Omega_rad_per_My, M) * PlanetRadius_km;
    }

    static void ClassifyEdges(
        const TArray<FVector3d>& Points,
        const TArray<TArray<int32>>& Neighbors,
        const TArray<int32>& PlateAssignments,
        const TArray<FVector3d>& PlateAngularVelocities,
        TArray<TPair<int32,int32>>& OutEdges,
        TArray<EBoundaryClass>& OutClasses,
        FBoundaryFieldMetrics& Metrics,
        double TransformEpsilon_km_per_My)
    {
        const int32 N = Points.Num();
        OutEdges.Reset();
        OutClasses.Reset();
        Metrics = {};

        for (int32 a = 0; a < N; ++a)
        {
            const int32 PlateA = PlateAssignments.IsValidIndex(a) ? PlateAssignments[a] : INDEX_NONE;
            for (int32 b : Neighbors[a])
            {
                if (b <= a) { continue; } // undirected unique pair

                const int32 PlateB = PlateAssignments.IsValidIndex(b) ? PlateAssignments[b] : INDEX_NONE;
                int32 ia = a, ib = b;
                int32 PlateLeft = PlateA;
                int32 PlateRight = PlateB;
                // Orient edge consistently from lower plate id to higher plate id (for cross-plate edges)
                if (PlateLeft != INDEX_NONE && PlateRight != INDEX_NONE && PlateLeft > PlateRight)
                {
                    Swap(ia, ib);
                    Swap(PlateLeft, PlateRight);
                }

                const FVector3d& A = Points[ia];
                const FVector3d& B = Points[ib];
                const FVector3d M = (A + B).GetSafeNormal();

                const double len_km = GeodesicKm(A, B);

                EBoundaryClass Class = EBoundaryClass::Interior;

                if (PlateLeft != PlateRight && PlateLeft != INDEX_NONE && PlateRight != INDEX_NONE &&
                    PlateAngularVelocities.IsValidIndex(PlateLeft) && PlateAngularVelocities.IsValidIndex(PlateRight))
                {
                    const FVector3d Si = SurfaceVelocityKmPerMy(PlateAngularVelocities[PlateLeft], M);
                    const FVector3d Sj = SurfaceVelocityKmPerMy(PlateAngularVelocities[PlateRight], M);
                    const FVector3d Vrel = Sj - Si;

                    const FVector3d Nb = BoundaryNormalAtMidpoint(A, B, M);
                    const double p = Vrel.Dot(Nb);

                    if (FMath::Abs(p) <= TransformEpsilon_km_per_My)
                    {
                        Class = EBoundaryClass::Transform;
                        Metrics.NumTransform++;
                        Metrics.LengthTransform_km += len_km;
                    }
                    else if (p > 0.0)
                    {
                        Class = EBoundaryClass::Divergent;
                        Metrics.NumDivergent++;
                        Metrics.LengthDivergent_km += len_km;
                    }
                    else
                    {
                        Class = EBoundaryClass::Convergent;
                        Metrics.NumConvergent++;
                        Metrics.LengthConvergent_km += len_km;
                    }
                }
                else
                {
                    Metrics.NumInterior++;
                    Metrics.LengthInterior_km += len_km;
                }

                OutEdges.Emplace(a, b);
                OutClasses.Add(Class);
                Metrics.NumEdges++;
            }
        }
    }

    struct Node
    {
        double Dist;
        int32 Index;
    };

    struct NodeGreater
    {
        bool operator()(const Node& A, const Node& B) const
        {
            if (A.Dist != B.Dist) return A.Dist > B.Dist; // min-heap by distance
            return A.Index > B.Index; // deterministic tie-break
        }
    };

    static void MultiSourceDijkstra(
        const TArray<FVector3d>& Points,
        const TArray<TArray<int32>>& Neighbors,
        const TSet<int32>& SeedVertices,
        TArray<double>& OutDistancesKm)
    {
        const int32 N = Points.Num();
        OutDistancesKm.SetNumUninitialized(N);
        for (int32 i = 0; i < N; ++i) OutDistancesKm[i] = TNumericLimits<double>::Max();

        std::priority_queue<Node, std::vector<Node>, NodeGreater> Q;
        for (int32 s : SeedVertices)
        {
            if (s >= 0 && s < N)
            {
                OutDistancesKm[s] = 0.0;
                Q.push({0.0, s});
            }
        }

        while (!Q.empty())
        {
            Node cur = Q.top();
            Q.pop();
            if (cur.Dist > OutDistancesKm[cur.Index])
            {
                continue; // stale
            }

            const int32 a = cur.Index;
            const FVector3d& A = Points[a];
            for (int32 b : Neighbors[a])
            {
                const double w = GeodesicKm(A, Points[b]);
                const double nd = cur.Dist + w;
                if (nd < OutDistancesKm[b])
                {
                    OutDistancesKm[b] = nd;
                    Q.push({nd, b});
                }
            }
        }
    }

    void ComputeBoundaryFields(
        const TArray<FVector3d>& Points,
        const TArray<TArray<int32>>& Neighbors,
        const TArray<int32>& PlateAssignments,
        const TArray<FVector3d>& PlateAngularVelocities,
        FBoundaryFieldResults& OutResults,
        double TransformEpsilon_km_per_My)
    {
        // If caller passed a negative sentinel, pull epsilon from the CVar
        if (TransformEpsilon_km_per_My < 0.0)
        {
            TransformEpsilon_km_per_My = static_cast<double>(CVarPaperBoundaryTransformEpsilonKmPerMy.GetValueOnAnyThread());
        }
        // Classify edges
        ClassifyEdges(
            Points,
            Neighbors,
            PlateAssignments,
            PlateAngularVelocities,
            OutResults.Edges,
            OutResults.Classifications,
            OutResults.Metrics,
            TransformEpsilon_km_per_My);

        // Seed sets
        TSet<int32> ConvergentSeeds;
        TSet<int32> DivergentSeeds;
        TSet<int32> AnyBoundarySeeds;
        for (int32 e = 0; e < OutResults.Edges.Num(); ++e)
        {
            const auto& Edge = OutResults.Edges[e];
            const EBoundaryClass C = OutResults.Classifications[e];
            if (C == EBoundaryClass::Convergent)
            {
                ConvergentSeeds.Add(Edge.Key);
                ConvergentSeeds.Add(Edge.Value);
                AnyBoundarySeeds.Add(Edge.Key);
                AnyBoundarySeeds.Add(Edge.Value);
            }
            else if (C == EBoundaryClass::Divergent)
            {
                DivergentSeeds.Add(Edge.Key);
                DivergentSeeds.Add(Edge.Value);
                AnyBoundarySeeds.Add(Edge.Key);
                AnyBoundarySeeds.Add(Edge.Value);
            }
            else if (C == EBoundaryClass::Transform)
            {
                AnyBoundarySeeds.Add(Edge.Key);
                AnyBoundarySeeds.Add(Edge.Value);
            }
        }

        // Multi-source Dijkstra for both fields
        MultiSourceDijkstra(Points, Neighbors, ConvergentSeeds, OutResults.DistanceToSubductionFront_km);
        MultiSourceDijkstra(Points, Neighbors, DivergentSeeds, OutResults.DistanceToRidge_km);
        MultiSourceDijkstra(Points, Neighbors, AnyBoundarySeeds, OutResults.DistanceToPlateBoundary_km);
    }
}
