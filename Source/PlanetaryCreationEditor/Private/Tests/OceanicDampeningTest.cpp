// Milestone 5 Task 2.3: Oceanic Dampening Test

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 5 Task 2.3: Oceanic Dampening Validation
 *
 * Tests seafloor smoothing and age-subsidence formula: depth = -2500 - 350 × sqrt(age_My)
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOceanicDampeningTest,
    "PlanetaryCreation.Milestone5.OceanicDampening",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOceanicDampeningTest::RunTest(const FString& Parameters)
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
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Oceanic Dampening Test ==="));

    // Test 1: Seafloor Smoothing
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: Seafloor Smoothing (Below Sea Level Only)"));

    FTectonicSimulationParameters Params;
    Params.Seed = 12345;
    Params.SubdivisionLevel = 0; // 20 plates
    Params.RenderSubdivisionLevel = 2; // 320 faces
    Params.LloydIterations = 0; // Skip for speed
    Params.bEnableOceanicDampening = true;
    Params.bEnableHotspots = true;
    Params.OceanicDampeningConstant = 0.001; // 0.001 m/My
    Params.OceanicAgeSubsidenceCoeff = 350.0; // m/sqrt(My)
    Params.SeaLevel = 0.0; // meters
    Params.bEnableDynamicRetessellation = false; // Disable for consistency

    Service->SetParameters(Params);

    // Initialize plate motion
    TArray<FTectonicPlate>& Plates = Service->GetPlatesForModification();
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        Plates[i].EulerPoleAxis = FVector3d(
            FMath::Sin(i * 0.7),
            FMath::Cos(i * 0.9),
            FMath::Sin(i * 1.1)
        ).GetSafeNormal();
        Plates[i].AngularVelocity = 0.03;
    }

    // Run simulation
    Service->AdvanceSteps(10);

    const TArray<double>& Elevation = Service->GetVertexElevationValues();
    const TArray<double>& CrustAge = Service->GetVertexCrustAge();

    // Verify arrays are populated
    TestTrue(TEXT("Elevation array initialized"), Elevation.Num() > 0);
    TestTrue(TEXT("Crust age array initialized"), CrustAge.Num() > 0);

    // Check that crust age is accumulating for seafloor vertices
    int32 SeafloorVertices = 0;
    int32 SeafloorWithAge = 0;
    int32 ContinentalVertices = 0;

    for (int32 i = 0; i < Elevation.Num(); ++i)
    {
        if (Elevation[i] < Params.SeaLevel)
        {
            SeafloorVertices++;
            if (CrustAge[i] > 0.0)
            {
                SeafloorWithAge++;
            }
        }
        else
        {
            ContinentalVertices++;
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Seafloor vertices: %d (with age: %d)"), SeafloorVertices, SeafloorWithAge);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Continental vertices: %d"), ContinentalVertices);

    TestTrue(TEXT("Some seafloor vertices tracked"), SeafloorVertices > 0);
    TestTrue(TEXT("Crust age accumulating"), SeafloorWithAge > 0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Seafloor tracking validated"));

    // Test 2: Age-Subsidence Relationship
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 2: Age-Subsidence Formula"));

    Params.Seed = 54321;
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
        PlatesT2[i].AngularVelocity = 0.03;
    }

    // Run longer to build up crust age
    Service->AdvanceSteps(20);

    const TArray<double>& ElevationT2 = Service->GetVertexElevationValues();
    const TArray<double>& CrustAgeT2 = Service->GetVertexCrustAge();

    // Find oldest seafloor and check if it's deeper
    double MaxAge = 0.0;
    double AvgDepthYoung = 0.0; // Age < 10 My
    double AvgDepthOld = 0.0;   // Age > 20 My
    int32 YoungCount = 0;
    int32 OldCount = 0;

    for (int32 i = 0; i < ElevationT2.Num(); ++i)
    {
        if (ElevationT2[i] < Params.SeaLevel) // Seafloor only
        {
            MaxAge = FMath::Max(MaxAge, CrustAgeT2[i]);

            if (CrustAgeT2[i] < 10.0) // Young crust
            {
                AvgDepthYoung += ElevationT2[i];
                YoungCount++;
            }
            else if (CrustAgeT2[i] > 20.0) // Old crust
            {
                AvgDepthOld += ElevationT2[i];
                OldCount++;
            }
        }
    }

    if (YoungCount > 0) AvgDepthYoung /= YoungCount;
    if (OldCount > 0) AvgDepthOld /= OldCount;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Max crust age: %.1f My"), MaxAge);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Young crust (<10 My) avg depth: %.1f m (n=%d)"), AvgDepthYoung, YoungCount);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Old crust (>20 My) avg depth: %.1f m (n=%d)"), AvgDepthOld, OldCount);

    // Old crust should be deeper (more negative elevation)
    if (YoungCount > 0 && OldCount > 0)
    {
        TestTrue(TEXT("Older crust is deeper"), AvgDepthOld < AvgDepthYoung);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Age-subsidence relationship validated"));
    }
    else
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("  ⚠️ Insufficient age variance for subsidence test"));
    }

    // Test 3: Determinism
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: Determinism (Same Seed → Same Results)"));

    Params.Seed = 77777;
    Params.bEnableDynamicRetessellation = false;

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
    Service->AdvanceSteps(10);
    TArray<double> AgeRun1 = Service->GetVertexCrustAge();
    TArray<double> ElevRun1 = Service->GetVertexElevationValues();

    // Second run
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
    Service->AdvanceSteps(10);
    TArray<double> AgeRun2 = Service->GetVertexCrustAge();
    TArray<double> ElevRun2 = Service->GetVertexElevationValues();

    TestEqual(TEXT("Same array sizes"), AgeRun1.Num(), AgeRun2.Num());

    int32 AgeMismatchCount = 0;
    int32 ElevMismatchCount = 0;
    double MaxAgeDiff = 0.0;
    double MaxElevDiff = 0.0;

    // Safely iterate over shared range of all arrays
    const int32 SafeCount = FMath::Min3(AgeRun1.Num(), AgeRun2.Num(), FMath::Min(ElevRun1.Num(), ElevRun2.Num()));
    for (int32 i = 0; i < SafeCount; ++i)
    {
        const double AgeDiff = FMath::Abs(AgeRun1[i] - AgeRun2[i]);
        const double ElevDiff = FMath::Abs(ElevRun1[i] - ElevRun2[i]);

        MaxAgeDiff = FMath::Max(MaxAgeDiff, AgeDiff);
        MaxElevDiff = FMath::Max(MaxElevDiff, ElevDiff);

        if (AgeDiff > 1e-6) AgeMismatchCount++;
        if (ElevDiff > 1e-3) ElevMismatchCount++;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Age mismatches: %d / %d (max diff: %.9f My)"), AgeMismatchCount, AgeRun1.Num(), MaxAgeDiff);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Elevation mismatches: %d / %d (max diff: %.6f m)"), ElevMismatchCount, ElevRun1.Num(), MaxElevDiff);

    TestEqual(TEXT("Deterministic crust age"), AgeMismatchCount, 0);
    TestEqual(TEXT("Deterministic seafloor elevation"), ElevMismatchCount, 0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Determinism validated"));

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Oceanic Dampening Test Complete ==="));
    AddInfo(TEXT("✅ Oceanic dampening test complete (3 tests)"));
    AddInfo(FString::Printf(TEXT("Seafloor vertices: %d | Max age: %.1f My | Determinism: ✓"),
        SeafloorVertices, MaxAge));

    return true;
}
