#include "Misc/AutomationTest.h"
#include "Simulation/FibonacciSampling.h"
#include "Simulation/SphericalDelaunay.h"
#include "Simulation/BoundaryField.h"
#include "Simulation/OceanicProcessor.h"
#include "Simulation/PaperConstants.h"

using namespace PaperConstants;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOceanicCrustMaskTest, "PlanetaryCreation.Paper.OceanicCrustMask",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOceanicCrustMaskTest::RunTest(const FString& Parameters)
{
    const int32 N = 10000;

    // Points
    TArray<FVector3d> Points; Points.Reserve(N);
    FFibonacciSampling::GenerateSamples(N, Points);

    // Triangulation and neighbors
    TArray<FSphericalDelaunay::FTriangle> Tris;
    FSphericalDelaunay::Triangulate(Points, Tris);
    TArray<TArray<int32>> Neighbors;
    FSphericalDelaunay::ComputeVoronoiNeighbors(Points, Tris, Neighbors);

    // CSR adjacency
    TArray<int32> Offsets; Offsets.SetNum(N + 1);
    TArray<int32> Adj; Adj.Reserve(N * 6);
    int32 cursor = 0; Offsets[0] = 0;
    for (int32 i = 0; i < N; ++i)
    {
        for (int32 nb : Neighbors[i]) Adj.Add(nb);
        cursor += Neighbors[i].Num();
        Offsets[i + 1] = cursor;
    }

    // Plate assignments: hemisphere split
    TArray<int32> Assign; Assign.SetNumUninitialized(N);
    for (int32 i = 0; i < N; ++i) Assign[i] = (Points[i].Z >= 0.0) ? 0 : 1;

    // Crust types: Plate 0 = Continental (1), Plate 1 = Oceanic (0)
    TArray<uint8> Crust; Crust.SetNum(2); Crust[0] = 1; Crust[1] = 0;

    // Divergent configuration about X-axis
    const double w = 0.02; // rad/My
    TArray<FVector3d> Omegas; Omegas.SetNum(2);
    Omegas[0] = FVector3d(w, 0, 0);
    Omegas[1] = FVector3d(-w, 0, 0);

    // Boundary classification (fills DistanceToRidge_km and DistanceToPlateBoundary_km)
    BoundaryField::FBoundaryFieldResults BF;
    BoundaryField::ComputeBoundaryFields(Points, Neighbors, Assign, Omegas, BF);
    TestTrue(TEXT("divergent edges present"), BF.Metrics.NumDivergent > 0);

    // Baseline elevation: abyssal everywhere
    TArray<double> Baseline_m; Baseline_m.Init(AbyssalElevation_m, N);
    TArray<double> Elev_m = Baseline_m;

    // Ridge cache (optional)
    Oceanic::FRidgeCache Cache;
    Oceanic::BuildRidgeCache(Points, Offsets, Adj, BF, Cache);

    // Apply oceanic crust generation
    Oceanic::FOceanicMetrics M1 = Oceanic::ApplyOceanicCrust(Points, Offsets, Adj, BF, Assign, Crust, Baseline_m, Elev_m, &Cache);

    // Determinism: re-apply from the same baseline and compare
    TArray<double> Elev2 = Baseline_m;
    Oceanic::FOceanicMetrics M2 = Oceanic::ApplyOceanicCrust(Points, Offsets, Adj, BF, Assign, Crust, Baseline_m, Elev2, &Cache);
    bool same = (Elev_m.Num() == Elev2.Num());
    if (same) { for (int32 i = 0; i < N; ++i) { if (FMath::Abs(Elev_m[i] - Elev2[i]) > 1e-12) { same = false; break; } } }
    TestTrue(TEXT("deterministic results"), same);

    // Continental vertices unchanged near and far
    int32 contChecked = 0; bool contOK = true;
    for (int32 i = 0; i < N && contChecked < 500; ++i)
    {
        if (Assign[i] == 0)
        {
            if (FMath::Abs(Elev_m[i] - AbyssalElevation_m) > 1e-6) { contOK = false; break; }
            ++contChecked;
        }
    }
    TestTrue(TEXT("continental unchanged"), contOK);

    // Oceanic near ridge: pick a few with dGamma <= 50 km
    int32 nearOK = 0; int32 nearTried = 0;
    for (int32 i = 0; i < N && nearTried < 500; ++i)
    {
        const double dG = BF.DistanceToRidge_km.IsValidIndex(i) ? BF.DistanceToRidge_km[i] : 1e9;
        if (Assign[i] == 1 && dG <= 50.0)
        {
            const double e = Elev_m[i];
            if (FMath::Abs(e - RidgeElevation_m) <= 150.0) ++nearOK;
            ++nearTried;
        }
    }
    TestTrue(TEXT("oceanic near ridge ~ crest"), nearOK > 0);

    // Oceanic far interior: dGamma >= 1100 km
    int32 farOK = 0; int32 farTried = 0;
    for (int32 i = 0; i < N && farTried < 500; ++i)
    {
        const double dG = BF.DistanceToRidge_km.IsValidIndex(i) ? BF.DistanceToRidge_km[i] : 0.0;
        if (Assign[i] == 1 && dG >= 1100.0)
        {
            const double e = Elev_m[i];
            if (FMath::Abs(e - AbyssalElevation_m) <= 200.0) ++farOK;
            ++farTried;
        }
    }
    TestTrue(TEXT("oceanic far ~ abyssal"), farOK > 0);

    return true;
}

