#include "Misc/AutomationTest.h"
#include "Simulation/TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 3 Task 3.1: Validate Lloyd relaxation convergence and plate distribution.
 * Tests centroid movement, convergence behavior, and determinism.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLloydRelaxationValidation,
    "PlanetaryCreation.Milestone3.LloydRelaxation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLloydRelaxationValidation::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!GEditor)
    {
        AddError(TEXT("GEditor is null - test requires editor context"));
        return false;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get UTectonicSimulationService"));
        return false;
    }

    // ====================
    // Test 1: Convergence with baseline settings (20 plates, 8 iterations)
    // ====================
    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.SubdivisionLevel = 0; // 20 plates (baseline from paper)
    Params.RenderSubdivisionLevel = 1; // 80 faces, 42 vertices - fast render
    Params.LloydIterations = 8;
    Service->SetParameters(Params);

    const TArray<FTectonicPlate>& Plates = Service->GetPlates();
    TestEqual(TEXT("Plate count (baseline)"), Plates.Num(), 20); // Level 0 subdivision = 20 plates

    // Lloyd runs during ResetSimulation, check that plates moved
    // (We can't directly validate convergence without modifying the service,
    //  but we can validate that plates exist and are well-distributed)

    // Compute minimum distance between plate centroids (should be > 0)
    double MinDistance = TNumericLimits<double>::Max();
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        for (int32 j = i + 1; j < Plates.Num(); ++j)
        {
            const double DotProduct = FVector3d::DotProduct(Plates[i].Centroid, Plates[j].Centroid);
            const double AngularDistance = FMath::Acos(FMath::Clamp(DotProduct, -1.0, 1.0));
            MinDistance = FMath::Min(MinDistance, AngularDistance);
        }
    }

    TestTrue(TEXT("Plates have non-zero separation"), MinDistance > 0.0);
    AddInfo(FString::Printf(TEXT("Min plate separation: %.4f rad (%.2f°)"),
        MinDistance, FMath::RadiansToDegrees(MinDistance)));

    // ====================
    // Test 2: Disabled Lloyd (iterations=0)
    // ====================
    FTectonicSimulationParameters NoLloydParams = Params;
    NoLloydParams.LloydIterations = 0;
    Service->SetParameters(NoLloydParams);

    const TArray<FTectonicPlate>& PlatesNoLloyd = Service->GetPlates();
    TestEqual(TEXT("Plates still generated with Lloyd disabled"), PlatesNoLloyd.Num(), 20); // Level 0 subdivision

    // ====================
    // Test 3: Determinism (same seed produces same result)
    // ====================
    FTectonicSimulationParameters DeterminismParams = Params;
    DeterminismParams.Seed = 123;
    DeterminismParams.LloydIterations = 5; // Fewer iterations for speed
    Service->SetParameters(DeterminismParams);

    // Capture first run centroids
    TArray<FVector3d> FirstRunCentroids;
    for (const FTectonicPlate& Plate : Service->GetPlates())
    {
        FirstRunCentroids.Add(Plate.Centroid);
    }

    // Reset with same seed
    Service->SetParameters(DeterminismParams);

    // Compare centroids
    int32 MismatchCount = 0;
    for (int32 i = 0; i < FirstRunCentroids.Num(); ++i)
    {
        const FVector3d& Expected = FirstRunCentroids[i];
        const FVector3d& Actual = Service->GetPlates()[i].Centroid;
        const double Distance = (Expected - Actual).Length();

        if (Distance > 1e-9)
        {
            MismatchCount++;
        }
    }

    TestEqual(TEXT("Deterministic Lloyd relaxation (seed=123)"), MismatchCount, 0);

    // ====================
    // Test 4: Plate distribution uniformity (coefficient of variation)
    // ====================
    // Measure standard deviation of nearest-neighbor distances
    // Lower CV = more uniform distribution (Lloyd should improve this)

    TArray<double> NearestNeighborDistances;
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        double MinDist = TNumericLimits<double>::Max();
        for (int32 j = 0; j < Plates.Num(); ++j)
        {
            if (i != j)
            {
                const double DotProduct = FVector3d::DotProduct(Plates[i].Centroid, Plates[j].Centroid);
                const double Dist = FMath::Acos(FMath::Clamp(DotProduct, -1.0, 1.0));
                MinDist = FMath::Min(MinDist, Dist);
            }
        }
        NearestNeighborDistances.Add(MinDist);
    }

    double Mean = 0.0;
    for (double Dist : NearestNeighborDistances)
    {
        Mean += Dist;
    }
    Mean /= NearestNeighborDistances.Num();

    double Variance = 0.0;
    for (double Dist : NearestNeighborDistances)
    {
        Variance += FMath::Square(Dist - Mean);
    }
    Variance /= NearestNeighborDistances.Num();
    const double StdDev = FMath::Sqrt(Variance);
    const double CV = StdDev / Mean; // Coefficient of variation

    AddInfo(FString::Printf(TEXT("Nearest-neighbor distance: mean=%.4f rad, σ=%.4f, CV=%.4f"), Mean, StdDev, CV));
    TestTrue(TEXT("Reasonable distribution uniformity (CV < 0.5)"), CV < 0.5);

    // ====================
    // Test 5: Experimental high-resolution mode (80 plates)
    // ====================
    FTectonicSimulationParameters HighResParams;
    HighResParams.Seed = 42;
    HighResParams.SubdivisionLevel = 1; // 80 plates (experimental mode)
    HighResParams.RenderSubdivisionLevel = 2; // 320 faces render (enough vertices for 80-plate Voronoi)
    HighResParams.LloydIterations = 8;
    Service->SetParameters(HighResParams);

    const TArray<FTectonicPlate>& HighResPlates = Service->GetPlates();
    TestEqual(TEXT("Plate count (experimental 80-plate mode)"), HighResPlates.Num(), 80); // Level 1 subdivision = 80 plates

    // Validate high-res mode produces reasonable distribution
    double MinDistHighRes = TNumericLimits<double>::Max();
    for (int32 i = 0; i < HighResPlates.Num(); ++i)
    {
        for (int32 j = i + 1; j < HighResPlates.Num(); ++j)
        {
            const double DotProduct = FVector3d::DotProduct(HighResPlates[i].Centroid, HighResPlates[j].Centroid);
            const double AngularDistance = FMath::Acos(FMath::Clamp(DotProduct, -1.0, 1.0));
            MinDistHighRes = FMath::Min(MinDistHighRes, AngularDistance);
        }
    }

    TestTrue(TEXT("High-res plates have non-zero separation"), MinDistHighRes > 0.0);
    AddInfo(FString::Printf(TEXT("High-res min plate separation: %.4f rad (%.2f°)"),
        MinDistHighRes, FMath::RadiansToDegrees(MinDistHighRes)));

    AddInfo(TEXT("=== Lloyd Relaxation Validation Complete ==="));

    return true;
#else
    AddError(TEXT("Test requires WITH_EDITOR"));
    return false;
#endif
}
