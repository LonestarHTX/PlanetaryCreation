#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Algo/Sort.h"
#include "Simulation/FibonacciSampling.h"
#include "Simulation/SphericalDelaunay.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSphericalDelaunayCyclicOrderTest, "PlanetaryCreation.Paper.SphericalDelaunayCyclic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace
{
    struct FTangentFrame
    {
        FVector3d N;
        FVector3d E1;
        FVector3d E2;
        bool bValid = false;
    };

    FTangentFrame MakeTangentFrame(const FVector3d& P)
    {
        FTangentFrame F;
        F.N = P.GetSafeNormal();
        const FVector3d Ref = (FMath::Abs(F.N.Z) > 0.9) ? FVector3d(1.0, 0.0, 0.0) : FVector3d(0.0, 0.0, 1.0);
        FVector3d E1 = FVector3d::CrossProduct(Ref, F.N);
        const double Len = E1.Length();
        if (Len <= 1e-15)
        {
            F.bValid = false;
            return F;
        }
        F.E1 = E1 / Len;
        F.E2 = FVector3d::CrossProduct(F.N, F.E1);
        F.bValid = true;
        return F;
    }

    double AngleCCW(const FTangentFrame& F, const FVector3d& P)
    {
        const double x = FVector3d::DotProduct(P, F.E1);
        const double y = FVector3d::DotProduct(P, F.E2);
        return FMath::Atan2(y, x);
    }

    // Returns true if arrays contain the same elements (as sets)
    bool SameSet(const TArray<int32>& A, const TArray<int32>& B)
    {
        if (A.Num() != B.Num()) return false;
        TSet<int32> SA;
        SA.Reserve(A.Num());
        for (int32 v : A) SA.Add(v);
        for (int32 v : B) if (!SA.Contains(v)) return false;
        return true;
    }
}

bool FSphericalDelaunayCyclicOrderTest::RunTest(const FString& Parameters)
{
    // Case 1: Tetrahedron with known vertices
    {
        TArray<FVector3d> V;
        V.Add(FVector3d(1.0, 1.0, 1.0).GetSafeNormal());
        V.Add(FVector3d(1.0, -1.0, -1.0).GetSafeNormal());
        V.Add(FVector3d(-1.0, 1.0, -1.0).GetSafeNormal());
        V.Add(FVector3d(-1.0, -1.0, 1.0).GetSafeNormal());

        TArray<FSphericalDelaunay::FTriangle> T;
        T.Add({0, 1, 2});
        T.Add({0, 1, 3});
        T.Add({0, 2, 3});
        T.Add({1, 2, 3});

        TArray<TArray<int32>> Cyclic;
        FSphericalDelaunay::ComputeVoronoiNeighborsCyclic(V, T, Cyclic);

        TestEqual(TEXT("tetra: array size"), Cyclic.Num(), V.Num());
        for (int32 i = 0; i < V.Num(); ++i)
        {
            const TArray<int32>& Nbs = Cyclic[i];
            TestEqual(*FString::Printf(TEXT("tetra: deg v%d"), i), Nbs.Num(), 3);

            // Independently compute CCW order via local frame
            const FTangentFrame F = MakeTangentFrame(V[i]);
            TestTrue(TEXT("tetra: frame valid"), F.bValid);

            struct Pair { int32 Idx; double Ang; };
            TArray<Pair> Pairs;
            for (int32 nb : Nbs)
            {
                Pairs.Add({nb, AngleCCW(F, V[nb])});
            }
            Algo::Sort(Pairs, [](const Pair& A, const Pair& B){ return A.Ang < B.Ang; });

            // Expected order from angles
            TArray<int32> Expected;
            Expected.Reserve(3);
            for (const Pair& p : Pairs) Expected.Add(p.Idx);

            TestTrue(*FString::Printf(TEXT("tetra: CCW order v%d"), i), Nbs == Expected);
        }
    }

    // Case 2: Fibonacci set with triangulation
    {
        const int32 N = 64;
        TArray<FVector3d> Pts;
        Pts.Reserve(N);
        FFibonacciSampling::GenerateSamples(N, Pts);

        TArray<FSphericalDelaunay::FTriangle> Tris;
        FSphericalDelaunay::Triangulate(Pts, Tris);

        TArray<TArray<int32>> NeighBase;
        TArray<TArray<int32>> NeighCyc1, NeighCyc2;
        FSphericalDelaunay::ComputeVoronoiNeighbors(Pts, Tris, NeighBase);
        FSphericalDelaunay::ComputeVoronoiNeighborsCyclic(Pts, Tris, NeighCyc1);
        FSphericalDelaunay::ComputeVoronoiNeighborsCyclic(Pts, Tris, NeighCyc2);

        TestEqual(TEXT("fib: arrays sized"), NeighCyc1.Num(), Pts.Num());
        TestEqual(TEXT("fib: determinism size"), NeighCyc1.Num(), NeighCyc2.Num());

        constexpr double Eps = 1e-12;
        for (int32 i = 0; i < Pts.Num(); ++i)
        {
            const TArray<int32>& B = NeighBase[i];
            const TArray<int32>& C1 = NeighCyc1[i];
            const TArray<int32>& C2 = NeighCyc2[i];

            TestTrue(*FString::Printf(TEXT("fib: set equality v%d"), i), SameSet(B, C1));
            TestTrue(*FString::Printf(TEXT("fib: deterministic v%d"), i), C1 == C2);

            // Angles non-decreasing modulo 2π
            const FTangentFrame F = MakeTangentFrame(Pts[i]);
            if (!F.bValid || C1.Num() <= 1)
            {
                continue;
            }

            TArray<double> Ang;
            Ang.Reserve(C1.Num());
            for (int32 nb : C1) Ang.Add(AngleCCW(F, Pts[nb]));

            // Find minimal angle to rotate start for monotonic check
            int32 minIdx = 0; double minVal = Ang[0];
            for (int32 k = 1; k < Ang.Num(); ++k)
            {
                if (Ang[k] < minVal) { minVal = Ang[k]; minIdx = k; }
            }

            auto GetWrapped = [&](int32 t) { return Ang[(minIdx + t) % Ang.Num()]; };

            double prev = GetWrapped(0);
            for (int32 t = 1; t < Ang.Num(); ++t)
            {
                const double cur = GetWrapped(t);
                // Account for periodicity: allow small equalities
                TestTrue(TEXT("fib: non-decreasing angles"), cur + Eps >= prev);
                prev = cur;
            }
        }
    }

    return true;
}
