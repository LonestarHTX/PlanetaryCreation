#include "Misc/AutomationTest.h"
#include "Simulation/FibonacciSampling.h"
#include "Simulation/SphericalDelaunay.h"
#include "Simulation/SphericalTriangulatorFactory.h"
#include "Simulation/BoundaryField.h"
#include "Simulation/OceanicProcessor.h"
#include "Simulation/PaperConstants.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

using namespace PaperConstants;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOceanicCrustTest, "PlanetaryCreation.Paper.OceanicCrust",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOceanicCrustTest::RunTest(const FString& Parameters)
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

    // Two oceanic plates: split by hemisphere; both oceanic for interpolation test
    TArray<int32> Assign; Assign.SetNumUninitialized(N);
    for (int32 i = 0; i < N; ++i) Assign[i] = (Points[i].Z >= 0.0) ? 0 : 1;
    TArray<uint8> Crust; Crust.SetNum(2); Crust[0] = 0; Crust[1] = 0;

    // Divergent configuration around equator (omegas)
    const double w = 0.02; // rad/My
    TArray<FVector3d> Omegas; Omegas.SetNum(2);
    Omegas[0] = FVector3d(w, 0, 0);
    Omegas[1] = FVector3d(-w, 0, 0);

    // Boundary classification
    BoundaryField::FBoundaryFieldResults BF;
    BoundaryField::ComputeBoundaryFields(Points, Neighbors, Assign, Omegas, BF);
    TestTrue(TEXT("divergent edges present"), BF.Metrics.NumDivergent > 0);

    // Ridge cache
    Oceanic::FRidgeCache Cache;
    Oceanic::BuildRidgeCache(Points, Offsets, Adj, BF, Cache);

    // Baseline elevation: -5500 on plate 0, -6500 on plate 1
    TArray<double> Baseline_m; Baseline_m.SetNum(N);
    for (int32 i = 0; i < N; ++i) Baseline_m[i] = (Assign[i] == 0) ? -5500.0 : -6500.0;
    TArray<double> Elev_m = Baseline_m;

    // Apply oceanic crust generation twice for determinism check
    Oceanic::FOceanicMetrics M1 = Oceanic::ApplyOceanicCrust(Points, Offsets, Adj, BF, Assign, Crust, Baseline_m, Elev_m, &Cache);
    TArray<double> Elev_copy = Elev_m;
    Oceanic::FOceanicMetrics M2 = Oceanic::ApplyOceanicCrust(Points, Offsets, Adj, BF, Assign, Crust, Baseline_m, Elev_m, &Cache);

    // Determinism: second application from same baseline produces identical result
    bool same = (Elev_copy.Num() == Elev_m.Num());
    if (same) { for (int32 i = 0; i < Elev_m.Num(); ++i) { if (FMath::Abs(Elev_copy[i] - Elev_m[i]) > 1e-12) { same = false; break; } } }
    TestTrue(TEXT("deterministic elevations"), same);

    // Near ridge: elevation close to ridge crest (-1000 m)
    // Pick a vertex closest to a divergent edge midpoint
    int32 nearIdx = 0; double bestDang = 1e9; FVector3d bestQ = FVector3d::ZeroVector;
    for (int32 e = 0; e < BF.Edges.Num(); ++e)
    {
        if (BF.Classifications.IsValidIndex(e) && BF.Classifications[e] == BoundaryField::EBoundaryClass::Divergent)
        {
            const int32 a = BF.Edges[e].Key, b = BF.Edges[e].Value;
            const FVector3d Q = (Points[a] + Points[b]).GetSafeNormal();
            for (int32 i = 0; i < N; ++i)
            {
                const double dang = FMath::Acos(FMath::Clamp(Points[i].Dot(Q), -1.0, 1.0));
                if (dang < bestDang) { bestDang = dang; bestQ = Q; nearIdx = i; }
            }
        }
    }
    const double nearElev = Elev_copy.IsValidIndex(nearIdx) ? Elev_copy[nearIdx] : 0.0;
    TestTrue(TEXT("near ridge ~ crest"), nearElev > RidgeElevation_m - 1000.0 && nearElev < RidgeElevation_m + 1000.0);

    // Far interiors: elevation matches plate baselines
    int32 farIdxA = -1, farIdxB = -1;
    for (int32 i = 0; i < N; ++i)
    {
        const double dG = BF.DistanceToRidge_km.IsValidIndex(i) ? BF.DistanceToRidge_km[i] : 0.0;
        if (dG > 1200.0)
        {
            if (Assign[i] == 0 && farIdxA < 0) farIdxA = i;
            if (Assign[i] == 1 && farIdxB < 0) farIdxB = i;
            if (farIdxA >= 0 && farIdxB >= 0) break;
        }
    }
    if (farIdxA >= 0) TestTrue(TEXT("far interior plate 0 ~ baseline"), FMath::Abs(Elev_copy[farIdxA] - (-5500.0)) < 200.0);
    if (farIdxB >= 0) TestTrue(TEXT("far interior plate 1 ~ baseline"), FMath::Abs(Elev_copy[farIdxB] - (-6500.0)) < 200.0);

    // Mid-boundary band: alpha ~ 0.5 → value between baselines
    int32 midIdx = -1;
    for (int32 i = 0; i < N; ++i)
    {
        const double dG = BF.DistanceToRidge_km.IsValidIndex(i) ? BF.DistanceToRidge_km[i] : 0.0;
        const double dP = BF.DistanceToPlateBoundary_km.IsValidIndex(i) ? BF.DistanceToPlateBoundary_km[i] : 1e9;
        const double a = (dG + dP > 1e-9) ? dG / (dG + dP) : 0.0;
        if (Assign[i] == 1 && FMath::Abs(a - 0.5) < 0.05) { midIdx = i; break; }
    }
    if (midIdx >= 0)
    {
        const double midElev = Elev_copy[midIdx];
        TestTrue(TEXT("mid band interpolation"), midElev < -5500.0 && midElev > -6500.0);
    }

    // Alpha stats reasonable
    TestTrue(TEXT("alpha in (0,1)"), M1.MinAlpha >= 0.0 && M1.MaxAlpha <= 1.0 && M1.MeanAlpha > 0.0 && M1.MeanAlpha < 1.0);

    // Ridge directions tangent near ridge
    int32 nonZeroCount = 0; int32 checked = 0;
    for (int32 i = 0; i < N && checked < 1000; ++i)
    {
        const double dG = BF.DistanceToRidge_km.IsValidIndex(i) ? BF.DistanceToRidge_km[i] : 1e9;
        if (dG < 300.0)
        {
            const FVector3f r = (Cache.RidgeDirections.IsValidIndex(i) ? Cache.RidgeDirections[i] : FVector3f::ZeroVector);
            if (r.Size() > 0.0f)
            {
                ++nonZeroCount;
                const double tang = FMath::Abs(FVector3d(r).Dot(Points[i]));
                TestTrue(TEXT("ridge dir tangent"), tang < 1e-6);
            }
            ++checked;
        }
    }
    TestTrue(TEXT("some ridge directions set"), nonZeroCount > 0);

    // Continental mask: pick a northern (continental) vertex near ridge and far; ensure unchanged (still baseline)
    int32 contIdxNear = -1; int32 contIdxFar = -1;
    for (int32 i = 0; i < N; ++i)
    {
        if (Assign[i] == 0)
        {
            if (contIdxNear < 0 && BF.DistanceToRidge_km.IsValidIndex(i) && BF.DistanceToRidge_km[i] < 200.0) contIdxNear = i;
            if (contIdxFar < 0 && BF.DistanceToRidge_km.IsValidIndex(i) && BF.DistanceToRidge_km[i] > 1100.0) contIdxFar = i;
            if (contIdxNear >= 0 && contIdxFar >= 0) break;
        }
    }
    if (contIdxNear >= 0) TestTrue(TEXT("continental near unchanged"), FMath::Abs(Elev_copy[contIdxNear] - AbyssalElevation_m) < 1e-9);
    if (contIdxFar >= 0) TestTrue(TEXT("continental far unchanged"), FMath::Abs(Elev_copy[contIdxFar] - AbyssalElevation_m) < 1e-9);

    // Metrics JSON
    FString BackendName; bool bUsedFallback = false;
    FSphericalTriangulatorFactory::Resolve(BackendName, bUsedFallback);
    const FString JsonPath = Oceanic::WritePhase5MetricsJson(BackendName, N, 42, M1);
    TestTrue(TEXT("Phase5 metrics JSON exists"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*JsonPath));
    FString Content; FFileHelper::LoadFileToString(Content, *JsonPath);
    TestTrue(TEXT("contains mean_alpha"), Content.Contains(TEXT("mean_alpha")));

    return true;
}
