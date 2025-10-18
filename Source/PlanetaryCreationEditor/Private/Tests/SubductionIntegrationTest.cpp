#include "Misc/AutomationTest.h"
#include "Simulation/FibonacciSampling.h"
#include "Simulation/SphericalDelaunay.h"
#include "Simulation/SubductionProcessor.h"
#include "Simulation/BoundaryField.h"
#include "Simulation/PaperConstants.h"

using namespace Subduction;

static void BuildCSR_SubductionIntegration(const TArray<TArray<int32>>& Neighbors, TArray<int32>& OutOffsets, TArray<int32>& OutAdj)
{
    const int32 N = Neighbors.Num();
    OutOffsets.SetNum(N + 1);
    int32 Accum = 0;
    OutOffsets[0] = 0;
    for (int32 i = 0; i < N; ++i)
    {
        Accum += Neighbors[i].Num();
        OutOffsets[i + 1] = Accum;
    }
    OutAdj.SetNumUninitialized(Accum);
    int32 Write = 0;
    for (int32 i = 0; i < N; ++i)
    {
        for (int32 nb : Neighbors[i])
        {
            OutAdj[Write++] = nb;
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSubductionIntegrationTest, "PlanetaryCreation.Paper.SubductionIntegration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSubductionIntegrationTest::RunTest(const FString& Parameters)
{
    using namespace PaperConstants;

    const int32 N = 10000;
    TArray<FVector3d> Points;
    FFibonacciSampling::GenerateSamples(N, Points);

    // Build neighbors
    TArray<FSphericalDelaunay::FTriangle> Tris;
    FSphericalDelaunay::Triangulate(Points, Tris);
    TArray<TArray<int32>> Neighbors;
    FSphericalDelaunay::ComputeVoronoiNeighbors(Points, Tris, Neighbors);

    // CSR
    TArray<int32> Offsets, Adj;
    BuildCSR_SubductionIntegration(Neighbors, Offsets, Adj);

    // Two plates by hemisphere
    TArray<int32> PlateAssign;
    PlateAssign.SetNumUninitialized(N);
    for (int32 i = 0; i < N; ++i)
    {
        PlateAssign[i] = (Points[i].Z >= 0.0) ? 0 : 1;
    }

    // Convergent setup across equator: opposite omega around X axis
    const double w = 0.02; // rad/My
    TArray<FVector3d> Omegas;
    Omegas.SetNumUninitialized(2);
    Omegas[0] = FVector3d(-w, 0, 0);
    Omegas[1] = FVector3d(w, 0, 0);

    // Start elevations at 0 m
    TArray<double> Elev_m;
    Elev_m.Init(0.0, N);

    // Distance to convergent front for assertions
    BoundaryField::FBoundaryFieldResults BF;
    BoundaryField::ComputeBoundaryFields(Points, Neighbors, PlateAssign, Omegas, BF);

    // Single cadence step (ApplyUplift scales by dt=2 My)
    const FSubductionMetrics M1 = ApplyUplift(Points, Offsets, Adj, PlateAssign, Omegas, Elev_m);
    TestTrue(TEXT("some vertices uplifted"), M1.VerticesTouched > 0);

    // Assert near-front uplift > 0 and beyond rs == 0
    int32 CheckedNear = 0;
    int32 CheckedFar = 0;
    int32 NearPositive = 0;
    for (int32 i = 0; i < N && (CheckedNear < 200 || CheckedFar < 200); ++i)
    {
        const double d = BF.DistanceToSubductionFront_km[i];
        if (d > 1e-6 && d <= SubductionControlDistance_km && CheckedNear < 200)
        {
            if (Elev_m[i] > 0.0) { NearPositive++; }
            ++CheckedNear;
        }
        else if (d >= SubductionDistance_km && CheckedFar < 200)
        {
            TestTrue(TEXT("beyond-rs zero uplift"), Elev_m[i] == 0.0);
            ++CheckedFar;
        }
    }
    TestTrue(TEXT("near-front vertices checked"), CheckedNear >= 10);
    TestTrue(TEXT("near-front positive uplift exists"), NearPositive >= 10);
    TestTrue(TEXT("far vertices checked"), CheckedFar >= 10);

    // Determinism: run from the same initial state
    TArray<double> Elev2;
    Elev2.Init(0.0, N);
    const FSubductionMetrics M2 = ApplyUplift(Points, Offsets, Adj, PlateAssign, Omegas, Elev2);
    for (int32 i = 0; i < N; ++i)
    {
        TestTrue(TEXT("deterministic uplift array"), FMath::IsNearlyEqual(Elev_m[i], Elev2[i], 1e-12));
    }

    return true;
}
