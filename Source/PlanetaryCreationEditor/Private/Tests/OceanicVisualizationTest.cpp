// Phase 5 Validation: Oceanic Crust Generation Visual Validation
// Emits CSV for elevation profiles, alpha maps, and ridge directions

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOceanicVisualizationTest, "PlanetaryCreation.Paper.OceanicVisualization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOceanicVisualizationTest::RunTest(const FString& Parameters)
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

    // Two oceanic plates: hemisphere split
    TArray<int32> Assign; Assign.SetNumUninitialized(N);
    for (int32 i = 0; i < N; ++i) Assign[i] = (Points[i].Z >= 0.0) ? 0 : 1;
    TArray<uint8> Crust; Crust.SetNum(2); Crust[0] = 0; Crust[1] = 0; // Both oceanic

    // Divergent configuration (spreading at equator)
    const double w = 0.02; // rad/My
    TArray<FVector3d> Omegas; Omegas.SetNum(2);
    Omegas[0] = FVector3d(w, 0, 0);
    Omegas[1] = FVector3d(-w, 0, 0);

    // Boundary classification
    BoundaryField::FBoundaryFieldResults BF;
    BoundaryField::ComputeBoundaryFields(Points, Neighbors, Assign, Omegas, BF);
    TestTrue(TEXT("divergent edges present"), BF.Metrics.NumDivergent > 0);

    // Baseline elevation: -5500 on plate 0, -6500 on plate 1
    TArray<double> Baseline_m; Baseline_m.SetNum(N);
    for (int32 i = 0; i < N; ++i) Baseline_m[i] = (Assign[i] == 0) ? -5500.0 : -6500.0;
    TArray<double> Elev_m = Baseline_m;

    // Ridge cache
    Oceanic::FRidgeCache Cache;
    Oceanic::BuildRidgeCache(Points, Offsets, Adj, BF, Cache);

    // Apply oceanic crust generation
    Oceanic::FOceanicMetrics M = Oceanic::ApplyOceanicCrust(
        Points, Offsets, Adj, BF, Assign, Crust, Baseline_m, Elev_m, &Cache);

    // ========================================================================
    // Validation Artifact 1: Elevation Profile CSV
    // ========================================================================
    FString ProfileCSV;
    ProfileCSV += TEXT("vertex_id,lat_deg,lon_deg,dGamma_km,dP_km,alpha,baseline_m,elevation_m,plate_id,oceanic\n");

    for (int32 i = 0; i < N; ++i)
    {
        const FVector3d& P = Points[i];
        const double lat = FMath::Asin(FMath::Clamp(P.Z, -1.0, 1.0)) * 180.0 / PI;
        const double lon = FMath::Atan2(P.Y, P.X) * 180.0 / PI;
        const double dG = BF.DistanceToRidge_km.IsValidIndex(i) ? BF.DistanceToRidge_km[i] : 1e9;
        const double dP = BF.DistanceToPlateBoundary_km.IsValidIndex(i) ? BF.DistanceToPlateBoundary_km[i] : 1e9;
        const double denom = FMath::Max(dG + dP, 1e-9);
        const double alpha = FMath::Clamp(dG / denom, 0.0, 1.0);
        const int32 pid = Assign.IsValidIndex(i) ? Assign[i] : -1;
        const bool bOceanic = (pid >= 0 && Crust.IsValidIndex(pid) && Crust[pid] == 0);

        ProfileCSV += FString::Printf(TEXT("%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.3f,%.3f,%d,%d\n"),
            i, lat, lon, dG, dP, alpha, Baseline_m[i], Elev_m[i], pid, bOceanic ? 1 : 0);
    }

    const FString Dir = FPaths::ProjectDir() / TEXT("Docs/Automation/Validation/Phase5");
    IFileManager::Get().MakeDirectory(*Dir, true);
    const FString ProfilePath = Dir / TEXT("oceanic_elevation_profile.csv");
    FFileHelper::SaveStringToFile(ProfileCSV, *ProfilePath);
    UE_LOG(LogTemp, Display, TEXT("[Phase5] Elevation profile CSV: %s"), *ProfilePath);

    // ========================================================================
    // Validation Artifact 2: Ridge Direction CSV
    // ========================================================================
    FString RidgeCSV;
    RidgeCSV += TEXT("vertex_id,lat_deg,lon_deg,pos_x,pos_y,pos_z,ridge_dir_x,ridge_dir_y,ridge_dir_z,dGamma_km\n");

    for (int32 i = 0; i < N; ++i)
    {
        const FVector3d& P = Points[i];
        const FVector3f R = Cache.RidgeDirections.IsValidIndex(i) ? Cache.RidgeDirections[i] : FVector3f::ZeroVector;
        const double dG = BF.DistanceToRidge_km.IsValidIndex(i) ? BF.DistanceToRidge_km[i] : 1e9;

        if (R.Size() > 0.1f && dG < 1000.0) // Only output near-ridge vertices with valid directions
        {
            const double lat = FMath::Asin(FMath::Clamp(P.Z, -1.0, 1.0)) * 180.0 / PI;
            const double lon = FMath::Atan2(P.Y, P.X) * 180.0 / PI;
            RidgeCSV += FString::Printf(TEXT("%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.3f\n"),
                i, lat, lon, P.X, P.Y, P.Z, R.X, R.Y, R.Z, dG);
        }
    }

    const FString RidgePath = Dir / TEXT("ridge_directions.csv");
    FFileHelper::SaveStringToFile(RidgeCSV, *RidgePath);
    UE_LOG(LogTemp, Display, TEXT("[Phase5] Ridge directions CSV: %s"), *RidgePath);

    // ========================================================================
    // Validation Artifact 3: Cross-Boundary Transect CSV
    // ========================================================================
    // Sample vertices along a meridian crossing the equator (divergent boundary)
    FString TransectCSV;
    TransectCSV += TEXT("transect_index,lat_deg,lon_deg,distance_from_equator_km,elevation_m,plate_id,alpha\n");

    const double transectLon = 0.0; // Sample along prime meridian
    TArray<int32> TransectIndices;
    for (int32 i = 0; i < N; ++i)
    {
        const FVector3d& P = Points[i];
        const double lon = FMath::Atan2(P.Y, P.X) * 180.0 / PI;
        if (FMath::Abs(lon - transectLon) < 5.0) // Within 5° of prime meridian
        {
            TransectIndices.Add(i);
        }
    }

    // Sort by latitude (distance from equator)
    TransectIndices.Sort([&](int32 A, int32 B) {
        return Points[A].Z < Points[B].Z;
    });

    for (int32 k = 0; k < TransectIndices.Num(); ++k)
    {
        const int32 i = TransectIndices[k];
        const FVector3d& P = Points[i];
        const double lat = FMath::Asin(FMath::Clamp(P.Z, -1.0, 1.0)) * 180.0 / PI;
        const double lon = FMath::Atan2(P.Y, P.X) * 180.0 / PI;
        const double distKm = lat * (PlanetRadius_km * PI / 180.0); // Approximate
        const double dG = BF.DistanceToRidge_km.IsValidIndex(i) ? BF.DistanceToRidge_km[i] : 1e9;
        const double dP = BF.DistanceToPlateBoundary_km.IsValidIndex(i) ? BF.DistanceToPlateBoundary_km[i] : 1e9;
        const double alpha = FMath::Clamp(dG / FMath::Max(dG + dP, 1e-9), 0.0, 1.0);
        const int32 pid = Assign[i];

        TransectCSV += FString::Printf(TEXT("%d,%.6f,%.6f,%.3f,%.3f,%d,%.6f\n"),
            k, lat, lon, distKm, Elev_m[i], pid, alpha);
    }

    const FString TransectPath = Dir / TEXT("cross_boundary_transect.csv");
    FFileHelper::SaveStringToFile(TransectCSV, *TransectPath);
    UE_LOG(LogTemp, Display, TEXT("[Phase5] Cross-boundary transect CSV: %s"), *TransectPath);

    // ========================================================================
    // Validation Metrics
    // ========================================================================
    TestTrue(TEXT("some vertices updated"), M.VerticesUpdated > 0);
    TestTrue(TEXT("ridge length reasonable"), M.RidgeLength_km > 1000.0 && M.RidgeLength_km < 100000.0);
    TestTrue(TEXT("alpha range spans [0,1]"), M.MinAlpha < 0.1 && M.MaxAlpha > 0.9);

    // Sample elevation profile sanity
    int32 nearRidgeCount = 0; double nearRidgeAvg = 0.0;
    int32 farCount = 0; double farAvg = 0.0;
    for (int32 i = 0; i < N; ++i)
    {
        const double dG = BF.DistanceToRidge_km.IsValidIndex(i) ? BF.DistanceToRidge_km[i] : 1e9;
        if (dG < 100.0) { nearRidgeAvg += Elev_m[i]; nearRidgeCount++; }
        if (dG > 1200.0) { farAvg += Elev_m[i]; farCount++; }
    }
    if (nearRidgeCount > 0) nearRidgeAvg /= nearRidgeCount;
    if (farCount > 0) farAvg /= farCount;

    UE_LOG(LogTemp, Display, TEXT("[Phase5] Near ridge (<100km): %.1f m avg (n=%d)"), nearRidgeAvg, nearRidgeCount);
    UE_LOG(LogTemp, Display, TEXT("[Phase5] Far interior (>1200km): %.1f m avg (n=%d)"), farAvg, farCount);

    TestTrue(TEXT("near ridge elevation ~ -1000m"), nearRidgeCount > 0 && nearRidgeAvg > -2000.0 && nearRidgeAvg < -500.0);
    TestTrue(TEXT("far interior elevation ~ baselines"), farCount > 0 && farAvg < -5000.0 && farAvg > -7000.0);

    // Ridge directions tangent check
    int32 tangentOK = 0; int32 tangentChecked = 0;
    for (int32 i = 0; i < N && tangentChecked < 500; ++i)
    {
        const FVector3f R = Cache.RidgeDirections.IsValidIndex(i) ? Cache.RidgeDirections[i] : FVector3f::ZeroVector;
        if (R.Size() > 0.1f)
        {
            const double dot = FMath::Abs(FVector3d(R).Dot(Points[i]));
            if (dot < 1e-3) ++tangentOK;
            ++tangentChecked;
        }
    }
    TestTrue(TEXT("ridge directions tangent to sphere"), tangentChecked > 0 && tangentOK > tangentChecked * 0.95);

    UE_LOG(LogTemp, Display, TEXT("[Phase5] Validation artifacts written:"));
    UE_LOG(LogTemp, Display, TEXT("  - Elevation profile: %s"), *ProfilePath);
    UE_LOG(LogTemp, Display, TEXT("  - Ridge directions: %s"), *RidgePath);
    UE_LOG(LogTemp, Display, TEXT("  - Cross-boundary transect: %s"), *TransectPath);

    // Write metrics JSON
    FString BackendName; bool bUsedFallback = false;
    FSphericalTriangulatorFactory::Resolve(BackendName, bUsedFallback);
    M.CadenceSteps = 1; // Single evaluation for this test
    const FString JsonPath = Oceanic::WritePhase5MetricsJson(BackendName, N, 42, M);
    TestTrue(TEXT("Phase5 metrics JSON exists"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*JsonPath));

    return true;
}
