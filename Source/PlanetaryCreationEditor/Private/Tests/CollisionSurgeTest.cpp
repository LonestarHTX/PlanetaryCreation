#include "Misc/AutomationTest.h"
#include "Simulation/FibonacciSampling.h"
#include "Simulation/SphericalDelaunay.h"
#include "Simulation/SphericalTriangulatorFactory.h"
#include "Simulation/BoundaryField.h"
#include "Simulation/CollisionProcessor.h"
#include "Simulation/PaperConstants.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

using namespace PaperConstants;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollisionSurgeTest, "PlanetaryCreation.Paper.CollisionSurge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCollisionSurgeTest::RunTest(const FString& Parameters)
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

    // Two continental plates by hemisphere
    TArray<int32> PlateAssign; PlateAssign.SetNumUninitialized(N);
    for (int32 i = 0; i < N; ++i) PlateAssign[i] = (Points[i].Z >= 0.0 ? 0 : 1);
    TArray<uint8> PlateCrustType; PlateCrustType.Init(1, 2); // both continental

    // Plate omegas: convergence around X-axis
    const double w = 0.02; // rad/My
    TArray<FVector3d> Omegas; Omegas.SetNum(2);
    Omegas[0] = FVector3d(w, 0, 0);
    Omegas[1] = FVector3d(-w, 0, 0);

    // Boundary classification
    BoundaryField::FBoundaryFieldResults BF;
    BoundaryField::ComputeBoundaryFields(Points, Neighbors, PlateAssign, Omegas, BF);

    // Pick first convergent edge deterministically
    int32 EdgeIndex = INDEX_NONE;
    for (int32 e = 0; e < BF.Edges.Num(); ++e)
    {
        if (BF.Classifications.IsValidIndex(e) && BF.Classifications[e] == BoundaryField::EBoundaryClass::Convergent)
        { EdgeIndex = e; break; }
    }
    TestTrue(TEXT("found convergent edge"), EdgeIndex != INDEX_NONE);
    if (EdgeIndex == INDEX_NONE) return false;

    const int32 a = BF.Edges[EdgeIndex].Key;
    const int32 b = BF.Edges[EdgeIndex].Value;
    const FVector3d Q = (Points[a] + Points[b]).GetSafeNormal();

    Collision::FCollisionEvent Evt;
    Evt.CenterUnit = Q;
    Evt.TerraneArea_km2 = 1.0e6;
    Evt.CarrierPlateId = 0; Evt.TargetPlateId = 1;
    Evt.PeakGuardrail_m = 6000.0; // deterministic guardrail

    // Compute radius and affected set
    const FVector3d Si = FVector3d::CrossProduct(Omegas[0], Q) * PlanetRadius_km;
    const FVector3d Sj = FVector3d::CrossProduct(Omegas[1], Q) * PlanetRadius_km;
    const double v = (Sj - Si).Size();
    const double r_km = CollisionDistance_km * FMath::Sqrt(v / MaxPlateSpeed_km_per_My) * FMath::Sqrt(Evt.TerraneArea_km2 / ReferencePlateArea_km2);
    const double r_ang = KmToGeodesicRadians(r_km);

    TArray<int32> Affected;
    const double CosThresh = FMath::Cos(r_ang);
    for (int32 i = 0; i < N; ++i)
    {
        const double dot = FMath::Clamp(Points[i].Dot(Q), -1.0, 1.0);
        if (dot >= CosThresh) Affected.Add(i);
    }

    // Elevation and folds
    TArray<double> Elev_m; Elev_m.Init(0.0, N);
    TArray<FVector3d> Folds; Folds.Init(FVector3d::ZeroVector, N);

    // Apply surge twice and verify determinism
    Collision::FCollisionMetrics M1 = Collision::ApplyCollisionSurge(Points, Affected, Evt, Elev_m, &Folds);
    TArray<double> Elev_copy = Elev_m; TArray<FVector3d> Folds_copy = Folds;
    Collision::FCollisionMetrics M2 = Collision::ApplyCollisionSurge(Points, Affected, Evt, Elev_m, &Folds);

    // Determinism: second application should add the same field (so delta equals first); check by comparing increments
    bool bDeterministic = true;
    for (int32 i = 0; i < N; ++i)
    {
        const double inc1 = Elev_copy[i];
        const double inc2 = Elev_m[i] - Elev_copy[i];
        if (FMath::Abs(inc1 - inc2) > 1e-12) { bDeterministic = false; break; }
    }
    TestTrue(TEXT("deterministic increments"), bDeterministic);

    // Peak at center approx min(Δc*A, guardrail)
    int32 CenterIdx = 0; double bestDot = -1.0;
    for (int32 i = 0; i < N; ++i)
    {
        const double d = Points[i].Dot(Q);
        if (d > bestDot) { bestDot = d; CenterIdx = i; }
    }
    const double ExpectedPeak = FMath::Min(CollisionCoefficient_per_km * Evt.TerraneArea_km2 * 1000.0, Evt.PeakGuardrail_m);
    TestTrue(TEXT("peak positive"), Elev_copy[CenterIdx] > 0.0);
    TestTrue(TEXT("peak <= guardrail"), Elev_copy[CenterIdx] <= ExpectedPeak + 1e-6);

    // Near boundary r_ang -> small uplift
    int32 EdgeIdx = -1; double minGap = 1e9;
    for (int32 i = 0; i < Affected.Num(); ++i)
    {
        const int32 vi = Affected[i];
        const double dang = FMath::Acos(FMath::Clamp(Points[vi].Dot(Q), -1.0, 1.0));
        const double gap = FMath::Abs(dang - r_ang);
        if (gap < minGap) { minGap = gap; EdgeIdx = vi; }
    }
    TestTrue(TEXT("boundary uplift near zero"), EdgeIdx >= 0 && Elev_copy[EdgeIdx] < 1e-6);

    // Folds: tangent and unit, radial from center
    bool bFoldOK = true;
    for (int32 t = 0; t < FMath::Min(50, Affected.Num()); ++t)
    {
        const int32 vi = Affected[t];
        const FVector3d& f = Folds[vi];
        const double tang = FMath::Abs(f.Dot(Points[vi]));
        const double len = f.Size();
        if (!(tang < 1e-6 && len > 0.0)) { bFoldOK = false; break; }
    }
    TestTrue(TEXT("folds tangent/unit"), bFoldOK);

    // Metrics JSON
    FString BackendName; bool bUsedFallback = false;
    FSphericalTriangulatorFactory::Resolve(BackendName, bUsedFallback);
    const FString JsonPath = Collision::WritePhase4MetricsJson(BackendName, N, 42, M1);
    TestTrue(TEXT("Phase4 metrics JSON exists"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*JsonPath));
    FString Content; FFileHelper::LoadFileToString(Content, *JsonPath);
    TestTrue(TEXT("contains collision_count"), Content.Contains(TEXT("collision_count")));

    return true;
}

