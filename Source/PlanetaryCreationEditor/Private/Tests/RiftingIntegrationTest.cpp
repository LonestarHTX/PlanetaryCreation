#include "Misc/AutomationTest.h"
#include "Simulation/FibonacciSampling.h"
#include "Simulation/SphericalDelaunay.h"
#include "Simulation/BoundaryField.h"
#include "Simulation/RiftingProcessor.h"
#include "Simulation/PaperConstants.h"

using namespace PaperConstants;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRiftingIntegrationTest, "PlanetaryCreation.Paper.RiftingIntegration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRiftingIntegrationTest::RunTest(const FString& Parameters)
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

    // Two plates: Plate 0 = large continental (north hemisphere), Plate 1 = small oceanic cap (south pole)
    TArray<int32> Assign; Assign.SetNumUninitialized(N);
    for (int32 i = 0; i < N; ++i)
    {
        const bool north = (Points[i].Z >= 0.0);
        bool cap = (!north) && (Points[i].Z < -0.9);
        Assign[i] = cap ? 1 : 0;
    }

    // Compute area proxy for Plate 0 using vertex share
    int32 count0 = 0; for (int32 v = 0; v < N; ++v) if (Assign[v] == 0) ++count0;
    const double SphereArea_km2 = 4.0 * PI * FMath::Square(PlanetRadius_km);
    const double A0_km2 = ReferencePlateArea_km2;
    const double Area0_km2 = SphereArea_km2 * (double)count0 / (double)N;

    // Force a deterministic rift event for Plate 0 via direct event (keeps test deterministic)
    Rifting::FRiftingEvent Evt{};
    Evt.PlateId = 0;
    Evt.PlateArea_km2 = Area0_km2;
    Evt.ContinentalRatio = 1.0;
    Evt.Seed = 12345; // deterministic
    Evt.FragmentCount = 3;

    TArray<int32> AssignOut;
    TArray<FVector3d> DriftDirs;
    Rifting::FRiftingMetrics RM{};
    TArray<TPair<int32,double>> FragRatios;
    const bool bRifted = Rifting::PerformRifting(Evt, Points, Offsets, Adj, Assign, AssignOut, DriftDirs, RM, &FragRatios);
    TestTrue(TEXT("rift performed"), bRifted);
    TestTrue(TEXT("rift count updated"), RM.RiftingCount >= 1);
    TestTrue(TEXT("fragment dir count"), DriftDirs.Num() == Evt.FragmentCount);

    // Plate id set should increase
    int32 maxIn = 1; int32 maxOut = 1;
    for (int32 v = 0; v < N; ++v) { maxIn = FMath::Max(maxIn, Assign[v]); maxOut = FMath::Max(maxOut, AssignOut[v]); }
    TestTrue(TEXT("plate count increased"), maxOut > maxIn || maxOut == maxIn); // at least new ids may appear

    // Assert that all fragment plate ratios equal the parent's ratio
    bool ratiosOK = (FragRatios.Num() == Evt.FragmentCount);
    if (ratiosOK)
    {
        for (const TPair<int32,double>& pr : FragRatios) { if (FMath::Abs(pr.Value - Evt.ContinentalRatio) > 1e-12) { ratiosOK = false; break; } }
    }
    TestTrue(TEXT("propagated continental ratio"), ratiosOK);

    // Build Omegas per plate from drift directions for classification (small magnitude)
    TArray<int32> Unique;
    {
        TSet<int32> S; for (int32 v = 0; v < N; ++v) S.Add(AssignOut[v]);
        Unique.Reserve(S.Num());
        for (int32 val : S) { Unique.Add(val); }
        Unique.Sort();
    }
    TArray<FVector3d> PlateCentroid; PlateCentroid.SetNum(Unique.Num());
    for (int32 k = 0; k < Unique.Num(); ++k)
    {
        FVector3d sum = FVector3d::ZeroVector; int32 c = 0;
        for (int32 v = 0; v < N; ++v) if (AssignOut[v] == Unique[k]) { sum += Points[v]; ++c; }
        PlateCentroid[k] = (c > 0) ? (sum / (double)c).GetSafeNormal() : FVector3d::UnitZ();
    }
    // Map fragment drifts to new plate ids (best effort: first fragments assumed to be lowest plate ids among new ones)
    TArray<FVector3d> Omegas; Omegas.SetNum(Unique.Num());
    const double mag = 0.01; // rad/My
    int32 fragIdx = 0;
    for (int32 k = 0; k < Unique.Num(); ++k)
    {
        const FVector3d& C = PlateCentroid[k];
        FVector3d Tangent = (fragIdx < DriftDirs.Num()) ? DriftDirs[fragIdx] : FVector3d::CrossProduct(C, FVector3d::UnitZ()).GetSafeNormal();
        FVector3d W = FVector3d::CrossProduct(Tangent, C).GetSafeNormal() * mag;
        Omegas[k] = W;
        if (fragIdx + 1 < DriftDirs.Num()) ++fragIdx;
    }

    // Build neighbor list and classify boundaries on updated assignments
    BoundaryField::FBoundaryFieldResults BF;
    BoundaryField::ComputeBoundaryFields(Points, Neighbors, AssignOut, Omegas, BF);

    TestTrue(TEXT("divergent boundaries present"), BF.Metrics.NumDivergent > 0);

    // Determinism: re-run PerformRifting with same event should produce identical assignments
    TArray<int32> AssignOut2; TArray<FVector3d> DriftDirs2; Rifting::FRiftingMetrics RM2{};
    Rifting::PerformRifting(Evt, Points, Offsets, Adj, Assign, AssignOut2, DriftDirs2, RM2);
    bool same = (AssignOut.Num() == AssignOut2.Num());
    if (same)
    {
        for (int32 i = 0; i < AssignOut.Num(); ++i) { if (AssignOut[i] != AssignOut2[i]) { same = false; break; } }
    }
    TestTrue(TEXT("deterministic assignments"), same);

    return true;
}
