// Diagnostic test to investigate plate movement freeze at 114 Myr

#include "Misc/AutomationTest.h"
#include "Simulation/TectonicSimulationService.h"
#include "Simulation/TectonicSimulationController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlateMovementDiagnosticTest,
    "PlanetaryCreation.Milestone6.Debug.PlateMovementDiagnostic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlateMovementDiagnosticTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get TectonicSimulationService"));
        return false;
    }

    // Reset with known seed
    FTectonicSimulationParameters Params = Service->GetParameters();
    Params.Seed = 42;
    Params.SubdivisionLevel = 0; // 20 plates
    Params.RenderSubdivisionLevel = 7; // Match user's LOD
    Params.VoronoiRefreshIntervalSteps = 5;
    Service->SetParameters(Params);

    const TArray<FTectonicPlate>& Plates = Service->GetPlates();
    const int32 NumPlates = Plates.Num();

    UE_LOG(LogPlanetaryCreation, Warning, TEXT("=== PLATE MOVEMENT DIAGNOSTIC ==="));
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("Initial plate count: %d"), NumPlates);

    // Capture initial state
    TArray<FVector3d> InitialCentroids;
    TArray<double> AngularVelocities;
    InitialCentroids.SetNum(NumPlates);
    AngularVelocities.SetNum(NumPlates);

    for (int32 i = 0; i < NumPlates; ++i)
    {
        InitialCentroids[i] = Plates[i].Centroid;
        AngularVelocities[i] = Plates[i].AngularVelocity;
    }

    // Log angular velocities
    double MinVelocity = TNumericLimits<double>::Max();
    double MaxVelocity = 0.0;
    double AvgVelocity = 0.0;
    int32 ZeroVelocityCount = 0;

    for (int32 i = 0; i < NumPlates; ++i)
    {
        const double AbsVel = FMath::Abs(AngularVelocities[i]);
        MinVelocity = FMath::Min(MinVelocity, AbsVel);
        MaxVelocity = FMath::Max(MaxVelocity, AbsVel);
        AvgVelocity += AbsVel;

        if (AbsVel < 1e-9)
        {
            ZeroVelocityCount++;
        }

        if (i < 5)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("Plate %d: AngularVel = %.6f rad/My"), i, AngularVelocities[i]);
        }
    }

    AvgVelocity /= NumPlates;

    UE_LOG(LogPlanetaryCreation, Warning, TEXT("Velocity Stats: Min=%.6f, Max=%.6f, Avg=%.6f rad/My"), MinVelocity, MaxVelocity, AvgVelocity);
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("Plates with zero velocity: %d / %d"), ZeroVelocityCount, NumPlates);

    // Advance simulation to 114 Myr (57 steps at 2 My/step)
    const int32 TargetSteps = 57;
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("Advancing %d steps to reach 114 Myr..."), TargetSteps);

    Service->AdvanceSteps(TargetSteps);

    const TArray<FTectonicPlate>& PlatesAfter = Service->GetPlates();

    // Measure centroid displacement
    double MinDisplacement = TNumericLimits<double>::Max();
    double MaxDisplacement = 0.0;
    double AvgDisplacement = 0.0;
    int32 NoMovementCount = 0;

    for (int32 i = 0; i < NumPlates; ++i)
    {
        const FVector3d& Initial = InitialCentroids[i];
        const FVector3d& Final = PlatesAfter[i].Centroid;

        const double DotProduct = FVector3d::DotProduct(Initial, Final);
        const double DisplacementRadians = FMath::Acos(FMath::Clamp(DotProduct, -1.0, 1.0));
        const double DisplacementKm = DisplacementRadians * 6370.0; // Earth radius
        const double DisplacementDegrees = FMath::RadiansToDegrees(DisplacementRadians);

        MinDisplacement = FMath::Min(MinDisplacement, DisplacementRadians);
        MaxDisplacement = FMath::Max(MaxDisplacement, DisplacementRadians);
        AvgDisplacement += DisplacementRadians;

        if (DisplacementRadians < 1e-6)
        {
            NoMovementCount++;
        }

        if (i < 5)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("Plate %d displacement: %.4f rad (%.2f deg, %.0f km)"),
                i, DisplacementRadians, DisplacementDegrees, DisplacementKm);
        }
    }

    AvgDisplacement /= NumPlates;

    UE_LOG(LogPlanetaryCreation, Warning, TEXT("Displacement Stats after 114 Myr:"));
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("  Min: %.6f rad (%.2f deg, %.0f km)"), MinDisplacement, FMath::RadiansToDegrees(MinDisplacement), MinDisplacement * 6370.0);
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("  Max: %.6f rad (%.2f deg, %.0f km)"), MaxDisplacement, FMath::RadiansToDegrees(MaxDisplacement), MaxDisplacement * 6370.0);
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("  Avg: %.6f rad (%.2f deg, %.0f km)"), AvgDisplacement, FMath::RadiansToDegrees(AvgDisplacement), AvgDisplacement * 6370.0);
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("  Plates with no movement: %d / %d"), NoMovementCount, NumPlates);

    // Check Voronoi refresh count
    const int32 ExpectedVoronoiRefreshes = TargetSteps / Params.VoronoiRefreshIntervalSteps;
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("Expected Voronoi refreshes: %d (every %d steps)"), ExpectedVoronoiRefreshes, Params.VoronoiRefreshIntervalSteps);

    // Check vertex reassignments
    const TArray<int32>& VertexAssignments = Service->GetVertexPlateAssignments();
    const int32 VertexCount = VertexAssignments.Num();
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("Render vertices: %d"), VertexCount);

    // CRITICAL CHECK: Verify mesh updates are happening
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("=== MESH UPDATE CHECK ==="));
    const int32 TopologyVersion = Service->GetTopologyVersion();
    const int32 SurfaceVersion = Service->GetSurfaceDataVersion();
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("TopologyVersion: %d"), TopologyVersion);
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("SurfaceDataVersion: %d (should be ~11 for 11 Voronoi refreshes)"), SurfaceVersion);

    if (SurfaceVersion < ExpectedVoronoiRefreshes)
    {
        AddError(FString::Printf(TEXT("SurfaceDataVersion (%d) is less than expected Voronoi refreshes (%d) - mesh not updating!"),
            SurfaceVersion, ExpectedVoronoiRefreshes));
    }

    // Sample vertex plate assignments to show boundaries ARE changing
    TArray<int32> SampleVertices = {0, 100, 500, 1000, 5000, 10000};
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("Sample vertex plate assignments (showing Voronoi changes):"));
    for (int32 SampleIdx : SampleVertices)
    {
        if (SampleIdx < VertexCount)
        {
            const int32 PlateID = VertexAssignments[SampleIdx];
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("  Vertex %d: Plate %d"), SampleIdx, PlateID);
        }
    }

    // Expected displacement at typical plate velocity (5 cm/year)
    // 5 cm/year = 0.05 m/year = 50 km/My
    // Angular displacement = linear / radius = 50 km/My / 6370 km = 0.00785 rad/My
    // Over 114 My: 0.00785 * 114 = 0.895 radians = 51.3 degrees = 5700 km
    const double TypicalVelocity = 50.0 / 6370.0; // 50 km/My in rad/My
    const double ExpectedDisplacement = TypicalVelocity * 114.0; // radians
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("Expected displacement at 5 cm/year: %.4f rad (%.2f deg, %.0f km)"),
        ExpectedDisplacement, FMath::RadiansToDegrees(ExpectedDisplacement), ExpectedDisplacement * 6370.0);

    // Diagnostics
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("=== DIAGNOSIS ==="));

    if (ZeroVelocityCount == NumPlates)
    {
        AddError(TEXT("ALL plates have zero angular velocity - plate initialization failed!"));
    }
    else if (ZeroVelocityCount > NumPlates / 2)
    {
        AddWarning(FString::Printf(TEXT("Over half the plates (%d/%d) have zero velocity"), ZeroVelocityCount, NumPlates));
    }

    if (MaxDisplacement < 0.01) // Less than 0.57 degrees
    {
        AddError(FString::Printf(TEXT("Maximum displacement is only %.4f rad (%.2f deg) after 114 Myr - plates barely moved!"), MaxDisplacement, FMath::RadiansToDegrees(MaxDisplacement)));
        AddError(TEXT("ROOT CAUSE: Angular velocities are too small or plates aren't rotating"));
    }
    else if (MaxDisplacement < ExpectedDisplacement * 0.1)
    {
        AddWarning(FString::Printf(TEXT("Displacement is only %.1f%% of expected (%.4f vs %.4f rad)"),
            100.0 * MaxDisplacement / ExpectedDisplacement, MaxDisplacement, ExpectedDisplacement));
        AddWarning(TEXT("Plates are moving but slower than typical Earth velocities"));
    }
    else
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("✓ Plates are moving as expected (%.1f%% of typical Earth velocity)"),
            100.0 * AvgDisplacement / ExpectedDisplacement);
    }

    if (NoMovementCount > 0)
    {
        AddWarning(FString::Printf(TEXT("%d plates showed no movement - possible stationary plates or numerical precision issue"), NoMovementCount));
    }

    UE_LOG(LogPlanetaryCreation, Warning, TEXT("=== END DIAGNOSTIC ==="));

    return true;
}
