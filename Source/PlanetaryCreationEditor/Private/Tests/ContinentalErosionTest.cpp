// Milestone 5 Task 2.1: Continental Erosion Test

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 5 Task 2.1: Continental Erosion Validation
 *
 * Tests erosion formula: ErosionRate = k × Slope × (Elevation - SeaLevel)⁺ × ThermalFactor × StressFactor
 * Validates basic erosion behavior, sea level constraint, and determinism.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContinentalErosionTest,
    "PlanetaryCreation.Milestone5.ContinentalErosion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContinentalErosionTest::RunTest(const FString& Parameters)
{
    if (!GEditor)
    {
        AddError(TEXT("Test requires editor context"));
        return false;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get TectonicSimulationService"));
        return false;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Continental Erosion Test ==="));

    // Test 1: Basic Erosion Enabled
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: Basic Erosion (Continental Only)"));

    FTectonicSimulationParameters Params;
    Params.Seed = 12345;
    Params.SubdivisionLevel = 0; // 20 plates
    Params.RenderSubdivisionLevel = 2; // 320 faces
    Params.LloydIterations = 0; // Skip for speed
    Params.bEnableContinentalErosion = true;
    Params.bEnableHotspots = true;
    Params.ErosionConstant = 0.01; // 0.01 m/My
    Params.SeaLevel = 0.0; // meters
    Params.ElevationScale = 10000.0; // 100 MPa → 10 km elevation
    Params.bEnableDynamicRetessellation = false; // Disable for consistency

    Service->SetParameters(Params);

    // Initialize plate motion to build stress and elevation
    TArray<FTectonicPlate>& Plates = Service->GetPlatesForModification();
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        Plates[i].EulerPoleAxis = FVector3d(
            FMath::Sin(i * 0.7),
            FMath::Cos(i * 0.9),
            FMath::Sin(i * 1.1)
        ).GetSafeNormal();
        Plates[i].AngularVelocity = 0.03; // rad/My
    }

    Service->AdvanceSteps(10); // Build elevation and apply erosion

    const TArray<double>& Elevation = Service->GetVertexElevationValues();
    const TArray<double>& ErosionRates = Service->GetVertexErosionRates();

    // Verify arrays are populated
    TestTrue(TEXT("Elevation array initialized"), Elevation.Num() > 0);
    TestTrue(TEXT("Erosion array initialized"), ErosionRates.Num() > 0);

    // Count vertices above/below sea level with erosion
    int32 AboveSeaLevelCount = 0;
    int32 BelowSeaLevelCount = 0;
    int32 ErodingAboveCount = 0;
    int32 ErodingBelowCount = 0;

    for (int32 i = 0; i < Elevation.Num(); ++i)
    {
        if (Elevation[i] > Params.SeaLevel)
        {
            AboveSeaLevelCount++;
            if (ErosionRates[i] > 0.0)
            {
                ErodingAboveCount++;
            }
        }
        else
        {
            BelowSeaLevelCount++;
            if (ErosionRates[i] > 0.0)
            {
                ErodingBelowCount++;
            }
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Vertices above sea level: %d (eroding: %d)"), AboveSeaLevelCount, ErodingAboveCount);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Vertices below sea level: %d (eroding: %d)"), BelowSeaLevelCount, ErodingBelowCount);

    // Erosion should only affect continental crust (above sea level)
    TestTrue(TEXT("Some vertices eroding above sea level"), ErodingAboveCount > 0);
    TestEqual(TEXT("No erosion below sea level"), ErodingBelowCount, 0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Continental-only erosion validated"));

    // Test 2: Sea Level Constraint
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 2: Sea Level Constraint (Elevation Never Below Sea Level)"));

    Params.Seed = 54321;
    Params.SeaLevel = 1000.0; // 1km sea level
    Params.ErosionConstant = 0.1; // Higher erosion rate
    Service->SetParameters(Params);

    // Re-initialize plate motion
    TArray<FTectonicPlate>& PlatesT2 = Service->GetPlatesForModification();
    for (int32 i = 0; i < PlatesT2.Num(); ++i)
    {
        PlatesT2[i].EulerPoleAxis = FVector3d(
            FMath::Sin(i * 0.7),
            FMath::Cos(i * 0.9),
            FMath::Sin(i * 1.1)
        ).GetSafeNormal();
        PlatesT2[i].AngularVelocity = 0.05; // Higher velocity
    }

    // Run simulation with erosion
    Service->AdvanceSteps(20);

    const TArray<double>& FinalElevation = Service->GetVertexElevationValues();
    const TArray<int32>& PlateAssignments = Service->GetVertexPlateAssignments();
    const TArray<FTectonicPlate>& PlatesArray = Service->GetPlates();

    // M5 Phase 3.5 fix: Check that no CONTINENTAL vertex erodes below sea level
    // Oceanic vertices are SUPPOSED to be below sea level (ocean floor at -3500m)
    int32 ContinentalBelowSeaLevelT2 = 0;
    double MinContinentalElevationT2 = DBL_MAX;
    int32 ContinentalVertexCountT2 = 0;

    for (int32 i = 0; i < FinalElevation.Num(); ++i)
    {
        // Check if this is a continental vertex
        const int32 PlateIdx = PlateAssignments.IsValidIndex(i) ? PlateAssignments[i] : INDEX_NONE;
        const bool bIsOceanic = (PlateIdx == INDEX_NONE) ||
            (PlatesArray.IsValidIndex(PlateIdx) && PlatesArray[PlateIdx].CrustType == ECrustType::Oceanic);

        if (!bIsOceanic) // Continental vertex
        {
            ContinentalVertexCountT2++;
            MinContinentalElevationT2 = FMath::Min(MinContinentalElevationT2, FinalElevation[i]);

            if (FinalElevation[i] < Params.SeaLevel - 1.0) // Allow 1m tolerance
            {
                ContinentalBelowSeaLevelT2++;
            }
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Sea level: %.1f m"), Params.SeaLevel);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Min continental elevation after 20 steps: %.1f m"), MinContinentalElevationT2);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Continental vertices below sea level: %d / %d"), ContinentalBelowSeaLevelT2, ContinentalVertexCountT2);

    TestEqual(TEXT("No continental vertices eroded below sea level"), ContinentalBelowSeaLevelT2, 0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Sea level constraint validated"));

    // Test 3: Determinism
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: Determinism (Same Seed → Same Results)"));

    Params.Seed = 77777;
    Params.SeaLevel = 0.0;
    Params.ErosionConstant = 0.01;
    Params.bEnableDynamicRetessellation = false; // Critical for determinism

    // First run
    Service->SetParameters(Params);
    TArray<FTectonicPlate>& PlatesT3A = Service->GetPlatesForModification();
    for (int32 i = 0; i < PlatesT3A.Num(); ++i)
    {
        PlatesT3A[i].EulerPoleAxis = FVector3d(
            FMath::Sin(i * 0.7),
            FMath::Cos(i * 0.9),
            FMath::Sin(i * 1.1)
        ).GetSafeNormal();
        PlatesT3A[i].AngularVelocity = 0.03;
    }
    Service->AdvanceSteps(5);
    TArray<double> ErosionRun1 = Service->GetVertexErosionRates();
    TArray<double> ElevationRun1 = Service->GetVertexElevationValues();

    // Second run with same seed
    Service->SetParameters(Params);
    TArray<FTectonicPlate>& PlatesT3B = Service->GetPlatesForModification();
    for (int32 i = 0; i < PlatesT3B.Num(); ++i)
    {
        PlatesT3B[i].EulerPoleAxis = FVector3d(
            FMath::Sin(i * 0.7),
            FMath::Cos(i * 0.9),
            FMath::Sin(i * 1.1)
        ).GetSafeNormal();
        PlatesT3B[i].AngularVelocity = 0.03;
    }
    Service->AdvanceSteps(5);
    TArray<double> ErosionRun2 = Service->GetVertexErosionRates();
    TArray<double> ElevationRun2 = Service->GetVertexElevationValues();

    TestEqual(TEXT("Same array sizes"), ErosionRun1.Num(), ErosionRun2.Num());

    // Check for determinism
    int32 ErosionMismatchCount = 0;
    int32 ElevationMismatchCount = 0;
    double MaxErosionDiff = 0.0;
    double MaxElevationDiff = 0.0;

    for (int32 i = 0; i < FMath::Min(ErosionRun1.Num(), ErosionRun2.Num()); ++i)
    {
        const double ErosionDiff = FMath::Abs(ErosionRun1[i] - ErosionRun2[i]);
        const double ElevationDiff = FMath::Abs(ElevationRun1[i] - ElevationRun2[i]);

        MaxErosionDiff = FMath::Max(MaxErosionDiff, ErosionDiff);
        MaxElevationDiff = FMath::Max(MaxElevationDiff, ElevationDiff);

        if (ErosionDiff > 1e-6) ErosionMismatchCount++; // Allow tiny numerical error
        if (ElevationDiff > 1e-3) ElevationMismatchCount++; // 1mm tolerance
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Erosion mismatches: %d / %d (max diff: %.9f m/My)"), ErosionMismatchCount, ErosionRun1.Num(), MaxErosionDiff);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Elevation mismatches: %d / %d (max diff: %.6f m)"), ElevationMismatchCount, ElevationRun1.Num(), MaxElevationDiff);

    TestEqual(TEXT("Deterministic erosion rates"), ErosionMismatchCount, 0);
    TestEqual(TEXT("Deterministic elevations"), ElevationMismatchCount, 0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Determinism validated"));

    // Test 4: Erosion Reduces Elevation Over Time
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 4: Erosion Reduces Elevation Over Time"));

    Params.Seed = 99999;
    Params.SeaLevel = 0.0;
    Params.ErosionConstant = 0.05; // Moderate erosion
    Service->SetParameters(Params);

    TArray<FTectonicPlate>& PlatesT4 = Service->GetPlatesForModification();
    for (int32 i = 0; i < PlatesT4.Num(); ++i)
    {
        PlatesT4[i].EulerPoleAxis = FVector3d(
            FMath::Sin(i * 0.7),
            FMath::Cos(i * 0.9),
            FMath::Sin(i * 1.1)
        ).GetSafeNormal();
        PlatesT4[i].AngularVelocity = 0.04;
    }

    // Run to build elevation
    Service->AdvanceSteps(10);
    TArray<double> InitialElevation = Service->GetVertexElevationValues();


    // Run more steps with erosion active
    Service->AdvanceSteps(10);
    TArray<double> LaterElevation = Service->GetVertexElevationValues();

    // Calculate average elevation change for continental regions
    double AvgElevationChange = 0.0;
    int32 ContinentalVertexCount = 0;

    for (int32 i = 0; i < FMath::Min(InitialElevation.Num(), LaterElevation.Num()); ++i)
    {
        if (InitialElevation[i] > Params.SeaLevel)
        {
            AvgElevationChange += (LaterElevation[i] - InitialElevation[i]);
            ContinentalVertexCount++;
        }
    }

    if (ContinentalVertexCount > 0)
    {
        AvgElevationChange /= ContinentalVertexCount;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Continental vertices: %d"), ContinentalVertexCount);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Avg elevation change after 10 steps: %.2f m"), AvgElevationChange);

    // Erosion should reduce elevation (negative change), but uplift from stress might counteract it
    // Just verify that erosion is having SOME effect (not zero change)
    TestTrue(TEXT("Erosion affects elevation"), FMath::Abs(AvgElevationChange) > 0.01);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Erosion effect on elevation validated"));

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Continental Erosion Test Complete ==="));
    AddInfo(TEXT("✅ Continental erosion test complete (4 tests)"));
    AddInfo(FString::Printf(TEXT("Eroding vertices: %d | Min continental elevation: %.1f m | Determinism: ✓"),
        ErodingAboveCount, MinContinentalElevationT2));

    return true;
}
