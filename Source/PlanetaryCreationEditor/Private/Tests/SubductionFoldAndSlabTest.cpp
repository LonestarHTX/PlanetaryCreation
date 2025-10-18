#include "Misc/AutomationTest.h"
#include "Simulation/FibonacciSampling.h"
#include "Simulation/SphericalDelaunay.h"
#include "Simulation/BoundaryField.h"
#include "Simulation/SubductionProcessor.h"
#include "Simulation/PaperConstants.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"

using namespace Subduction;

static void BuildCSR(const TArray<TArray<int32>>& Neighbors, TArray<int32>& OutOffsets, TArray<int32>& OutAdj)
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

static void ComputePlateCentroids(const TArray<FVector3d>& Points, const TArray<int32>& PlateIds, int32 PlateCount, TArray<FVector3d>& OutCentroids)
{
    OutCentroids.Init(FVector3d::ZeroVector, PlateCount);
    TArray<int32> Counts; Counts.Init(0, PlateCount);
    for (int32 i = 0; i < Points.Num(); ++i)
    {
        const int32 p = PlateIds[i];
        if (p >= 0 && p < PlateCount)
        {
            OutCentroids[p] += Points[i];
            Counts[p]++;
        }
    }
    for (int32 p = 0; p < PlateCount; ++p)
    {
        if (Counts[p] > 0) OutCentroids[p].Normalize(); else OutCentroids[p] = FVector3d::ZAxisVector;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSubductionFoldAndSlabTest, "PlanetaryCreation.Paper.SubductionFoldAndSlab",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSubductionFoldAndSlabTest::RunTest(const FString& Parameters)
{
    using namespace PaperConstants;

    const int32 N = 10000;
    TArray<FVector3d> Points;
    FFibonacciSampling::GenerateSamples(N, Points);

    // Triangulation and neighbors
    TArray<FSphericalDelaunay::FTriangle> Tris;
    FSphericalDelaunay::Triangulate(Points, Tris);
    TArray<TArray<int32>> Neighbors;
    FSphericalDelaunay::ComputeVoronoiNeighbors(Points, Tris, Neighbors);
    TArray<int32> Offsets, Adj; BuildCSR(Neighbors, Offsets, Adj);

    // Three-plate partition: north = 0; south-east = 1; south-west = 2
    TArray<int32> PlateAssign; PlateAssign.SetNumUninitialized(N);
    for (int32 i = 0; i < N; ++i)
    {
        if (Points[i].Z >= 0.0) PlateAssign[i] = 0;
        else PlateAssign[i] = (Points[i].X >= 0.0) ? 1 : 2;
    }
    const int32 PlateCount = 3;

    // Convergent setup: north vs both southern plates
    const double w = 0.02;
    TArray<FVector3d> Omegas; Omegas.SetNumUninitialized(PlateCount);
    Omegas[0] = FVector3d(-w, 0, 0); // north
    Omegas[1] = FVector3d(w, 0, 0);  // south-east
    Omegas[2] = FVector3d(0.5 * w, 0, 0); // south-west (slower)

    // Boundary field for convergent edges
    BoundaryField::FBoundaryFieldResults BF;
    BoundaryField::ComputeBoundaryFields(Points, Neighbors, PlateAssign, Omegas, BF);

    // Fold directions: initialize to zero
    TArray<FVector3d> Folds; Folds.Init(FVector3d::ZeroVector, N);
    TArray<int32> Off2, Adj2; // CSR for fold update call
    BuildCSR(Neighbors, Off2, Adj2);
    FFoldMetrics FM1 = UpdateFoldDirections(Points, Off2, Adj2, PlateAssign, Omegas, BF, Folds);

    // Assertions: tangent and unit where updated
    int32 Checked = 0; int32 NonZero = 0;
    for (int32 i = 0; i < N && Checked < 200; ++i)
    {
        const double d = BF.DistanceToSubductionFront_km[i];
        if (d > 1e-6 && d <= SubductionControlDistance_km)
        {
            const FVector3d& f = Folds[i];
            if (!f.IsNearlyZero())
            {
                NonZero++;
                TestTrue(TEXT("fold tangent"), FMath::Abs(f.Dot(Points[i])) < 1e-8);
                TestTrue(TEXT("fold unit"), FMath::IsNearlyEqual(f.Size(), 1.0, 1e-6));
            }
            Checked++;
        }
    }
    TestTrue(TEXT("some non-zero folds in influence band"), NonZero >= 10);

    // Determinism
    TArray<FVector3d> Folds2; Folds2.Init(FVector3d::ZeroVector, N);
    FFoldMetrics FM2 = UpdateFoldDirections(Points, Off2, Adj2, PlateAssign, Omegas, BF, Folds2);
    for (int32 i = 0; i < N; ++i)
    {
        TestTrue(TEXT("fold deterministic"), Folds[i].Equals(Folds2[i], 1e-12));
    }

    // Slab pull
    TArray<Subduction::FConvergentEdge> ConvergentEdges;
    for (int32 e = 0; e < BF.Edges.Num(); ++e)
    {
        if (BF.Classifications[e] == BoundaryField::EBoundaryClass::Convergent)
        {
            const int32 a = BF.Edges[e].Key;
            const int32 b = BF.Edges[e].Value;
            // Determine subducting plate using same rule as service
            const FVector3d M = (Points[a] + Points[b]).GetSafeNormal();
            const FVector3d t = ((Points[b] - Points[a]) - ((Points[b] - Points[a]).Dot(M)) * M).GetSafeNormal();
            const FVector3d Nb = FVector3d::CrossProduct(M, t);
            const int32 pa = PlateAssign[a];
            const int32 pb = PlateAssign[b];
            const FVector3d Si = FVector3d::CrossProduct(Omegas[pa], M) * PaperConstants::PlanetRadius_km;
            const FVector3d Sj = FVector3d::CrossProduct(Omegas[pb], M) * PaperConstants::PlanetRadius_km;
            const double projA = Si.Dot(Nb);
            const double projB = Sj.Dot(Nb);
            Subduction::FConvergentEdge CE;
            CE.A = a; CE.B = b;
            if (projA < projB) { CE.SubductingPlateId = pa; CE.OverridingPlateId = pb; }
            else { CE.SubductingPlateId = pb; CE.OverridingPlateId = pa; }
            ConvergentEdges.Add(CE);
        }
    }
    TArray<FVector3d> Centroids; ComputePlateCentroids(Points, PlateAssign, PlateCount, Centroids);

    TArray<FVector3d> OmegasBefore = Omegas;
    FSlabPullMetrics SM = ApplySlabPull(Centroids, ConvergentEdges, Points, Omegas);
    TSet<int32> SubductingSet;
    for (const auto& CE : ConvergentEdges) SubductingSet.Add(CE.SubductingPlateId);
    for (int32 p = 0; p < PlateCount; ++p)
    {
        const double dmag = (Omegas[p] - OmegasBefore[p]).Size();
        if (SubductingSet.Contains(p))
        {
            TestTrue(TEXT("slab pull non-zero for subducting"), dmag > 0.0);
        }
        else
        {
            TestTrue(TEXT("slab pull zero for non-subducting"), dmag == 0.0);
        }
    }

    // No convergent edges -> zero delta
    Omegas = OmegasBefore;
    TArray<Subduction::FConvergentEdge> Empty;
    FSlabPullMetrics SM0 = ApplySlabPull(Centroids, Empty, Points, Omegas);
    TestTrue(TEXT("slab pull zero when no front"), (Omegas[0] - OmegasBefore[0]).IsNearlyZero() && (Omegas[1] - OmegasBefore[1]).IsNearlyZero());

    // Metrics JSON (structural check)
    const FString Backend = TEXT("Geogram");
    const FString Path = WritePhase3MetricsJson(
        TEXT("SubductionFoldAndSlab"),  // Test name for provenance
        Backend,
        N,
        42,
        0,  // No simulation steps (structural test only)
        BF.Metrics.NumConvergent,
        BF.Metrics.NumDivergent,
        BF.Metrics.NumTransform,
        {0,0.0,0.0,0.0}, // uplift minimal stub for structure
        FM1,
        0.0,
        SM);

    TestTrue(TEXT("metrics JSON exists"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*Path));
    FString Content; FFileHelper::LoadFileToString(Content, *Path);
    TestTrue(TEXT("contains boundary_counts"), Content.Contains(TEXT("boundary_counts")));
    TestTrue(TEXT("contains uplift_stats"), Content.Contains(TEXT("uplift_stats")));
    TestTrue(TEXT("contains fold_coherence"), Content.Contains(TEXT("fold_coherence")));
    TestTrue(TEXT("contains timing_ms"), Content.Contains(TEXT("timing_ms")));

    return true;
}
