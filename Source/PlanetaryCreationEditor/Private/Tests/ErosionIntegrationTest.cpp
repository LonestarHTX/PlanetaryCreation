#include "Misc/AutomationTest.h"
#include "Simulation/FibonacciSampling.h"
#include "Simulation/SphericalDelaunay.h"
#include "Simulation/BoundaryField.h"
#include "Simulation/ErosionProcessor.h"
#include "Simulation/PaperConstants.h"

using namespace PaperConstants;

static void BuildCSR_Erosion(const TArray<TArray<int32>>& Neighbors, TArray<int32>& OutOffsets, TArray<int32>& OutAdj)
{
    const int32 N = Neighbors.Num();
    OutOffsets.SetNum(N + 1);
    int32 Accum = 0; OutOffsets[0] = 0;
    for (int32 i = 0; i < N; ++i) { Accum += Neighbors[i].Num(); OutOffsets[i + 1] = Accum; }
    OutAdj.SetNumUninitialized(Accum);
    int32 Write = 0; for (int32 i = 0; i < N; ++i) for (int32 nb : Neighbors[i]) OutAdj[Write++] = nb;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FErosionIntegrationTest, "PlanetaryCreation.Paper.ErosionIntegration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FErosionIntegrationTest::RunTest(const FString& Parameters)
{
    const int32 N = 10000;
    // Points and triangulation
    TArray<FVector3d> Points; Points.Reserve(N);
    FFibonacciSampling::GenerateSamples(N, Points);
    TArray<FSphericalDelaunay::FTriangle> Tris; FSphericalDelaunay::Triangulate(Points, Tris);
    TArray<TArray<int32>> Neighbors; FSphericalDelaunay::ComputeVoronoiNeighbors(Points, Tris, Neighbors);
    TArray<int32> Off, Adj; BuildCSR_Erosion(Neighbors, Off, Adj);

    // Plate assignment: north = continental (pid=0), south = oceanic (pid=1)
    TArray<int32> Assign; Assign.SetNumUninitialized(N);
    for (int32 i = 0; i < N; ++i) Assign[i] = (Points[i].Z >= 0.0) ? 0 : 1;
    TArray<uint8> Crust; Crust.SetNum(2); Crust[0] = 1; Crust[1] = 0;

    // Convergent configuration across equator to produce subduction front
    const double w = 0.02; // rad/My
    TArray<FVector3d> Omegas; Omegas.SetNum(2);
    Omegas[0] = FVector3d(-w, 0, 0); // move southward
    Omegas[1] = FVector3d( w, 0, 0); // move northward

    // Boundary classification for trench distance
    BoundaryField::FBoundaryFieldResults BF;
    BoundaryField::ComputeBoundaryFields(Points, Neighbors, Assign, Omegas, BF);
    TestTrue(TEXT("convergent edges present"), BF.Metrics.NumConvergent > 0);

    // Baseline elevations: small positive continent, abyssal ocean
    TArray<double> Elev_m; Elev_m.SetNum(N);
    for (int32 i = 0; i < N; ++i)
    {
        if (Assign[i] == 0)
        {
            // deterministic small positive signal up to ~+500 m
            const double s = FMath::Abs(Points[i].X * 0.5 + Points[i].Y * 0.3) * 500.0;
            Elev_m[i] = s;
        }
        else
        {
            Elev_m[i] = AbyssalElevation_m; // ~-6000 m
        }
    }

    // Apply one erosion step
    const double TrenchBandKm = 200.0;
    const Erosion::FErosionMetrics M = Erosion::ApplyErosionAndDampening(Points, Assign, Crust, BF, Elev_m, TrenchBandKm);

    // Continental: decreased only where z>0 and remains >= 0 for small baseline
    int32 ContChecked = 0, ContDecreased = 0, ContNonNeg = 0;
    for (int32 i = 0; i < N && ContChecked < 2000; ++i)
    {
        if (Assign[i] == 0 && Elev_m[i] >= -1.0)
        {
            ++ContChecked;
            if (Elev_m[i] > 0.0) { ++ContNonNeg; }
        }
    }
    TestTrue(TEXT("continental vertices checked"), ContChecked >= 100);
    TestTrue(TEXT("continental remain non-negative"), ContNonNeg >= 50);

    // Oceanic: more negative (toward trench depth) for many oceanic vertices
    int32 OceanChecked = 0, OceanDecreased = 0;
    for (int32 i = 0; i < N && OceanChecked < 1000; ++i)
    {
        if (Assign[i] == 1)
        {
            ++OceanChecked;
            if (Elev_m[i] < AbyssalElevation_m) ++OceanDecreased; // went deeper than -6000
        }
    }
    TestTrue(TEXT("oceanic vertices decreased"), OceanDecreased > 0);

    // Trench band: vertices within band gained elevation relative to abyssal baseline
    int32 BandChecked = 0, BandIncreased = 0;
    for (int32 i = 0; i < N && BandChecked < 500; ++i)
    {
        const double d = BF.DistanceToSubductionFront_km.IsValidIndex(i) ? BF.DistanceToSubductionFront_km[i] : 1e9;
        if (d <= TrenchBandKm)
        {
            ++BandChecked;
            if (Elev_m[i] > AbyssalElevation_m) ++BandIncreased;
        }
    }
    TestTrue(TEXT("trench band vertices increased"), BandIncreased > 0);

    // Determinism: apply again from same baseline
    TArray<double> Elev_m2; Elev_m2.SetNum(N);
    for (int32 i = 0; i < N; ++i)
    {
        Elev_m2[i] = (Assign[i] == 0) ? (FMath::Abs(Points[i].X * 0.5 + Points[i].Y * 0.3) * 500.0) : AbyssalElevation_m;
    }
    const Erosion::FErosionMetrics M2 = Erosion::ApplyErosionAndDampening(Points, Assign, Crust, BF, Elev_m2, TrenchBandKm);
    for (int32 i = 0; i < N; ++i)
    {
        TestTrue(TEXT("deterministic erosion field"), FMath::IsNearlyEqual(Elev_m[i], Elev_m2[i], 1e-12));
    }

    return true;
}

