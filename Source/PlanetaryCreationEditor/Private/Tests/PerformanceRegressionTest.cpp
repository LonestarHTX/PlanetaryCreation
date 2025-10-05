// Milestone 5 Task 3.1: Performance Regression Test

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"
#include "HAL/PlatformTime.h"

/**
 * Milestone 5 Task 3.1: Performance Regression Test
 *
 * Measures M5 feature overhead compared to M4 baseline:
 * - Continental Erosion: <5ms per step
 * - Sediment Transport: <4ms per step
 * - Oceanic Dampening: <3ms per step
 * - Total M5 Overhead: <14ms per step
 *
 * Target: Keep Level 3 (642 vertices) under 110ms per step
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerformanceRegressionTest,
    "PlanetaryCreation.Milestone5.PerformanceRegression",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPerformanceRegressionTest::RunTest(const FString& Parameters)
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
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Performance Regression Test (M5 vs M4) ==="));

    // Ship-critical LOD configuration (Level 3)
    FTectonicSimulationParameters Params;
    Params.Seed = 777;
    Params.SubdivisionLevel = 1; // 80 plates
    Params.RenderSubdivisionLevel = 3; // 1280 faces (ship-critical LOD)
    Params.LloydIterations = 4;
    Params.bEnableDynamicRetessellation = true;
    Params.bEnableHotspots = true;
    Params.ErosionConstant = 0.001;
    Params.OceanicDampeningConstant = 0.0005;
    Params.SeaLevel = 0.0;
    Params.ElevationScale = 10000.0;

    // Initialize plate motion
    auto InitializePlates = [](TArray<FTectonicPlate>& Plates)
    {
        for (int32 i = 0; i < Plates.Num(); ++i)
        {
            Plates[i].EulerPoleAxis = FVector3d(
                FMath::Sin(i * 0.7),
                FMath::Cos(i * 0.9),
                FMath::Sin(i * 1.1)
            ).GetSafeNormal();
            Plates[i].AngularVelocity = 0.02;
        }
    };

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Configuration:"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  LOD Level: 3 (ship-critical)"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Plates: %d"), 80);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Faces: %d"), 1280);
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // ========================================
    // Test 1: M4 Baseline (No M5 Features)
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: M4 Baseline (No Erosion/Sediment/Dampening)"));

    Params.bEnableContinentalErosion = false;
    Params.bEnableSedimentTransport = false;
    Params.bEnableOceanicDampening = false;
    Service->SetParameters(Params);
    InitializePlates(Service->GetPlatesForModification());

    // Warmup
    Service->AdvanceSteps(5);

    // Measure M4 baseline
    const int32 BaselineSamples = 20;
    double BaselineTotalTime = 0.0;
    double BaselineMinTime = DBL_MAX;
    double BaselineMaxTime = 0.0;

    for (int32 i = 0; i < BaselineSamples; ++i)
    {
        const double StartTime = FPlatformTime::Seconds();
        Service->AdvanceSteps(1);
        const double EndTime = FPlatformTime::Seconds();
        const double StepTime = (EndTime - StartTime) * 1000.0; // Convert to ms

        BaselineTotalTime += StepTime;
        BaselineMinTime = FMath::Min(BaselineMinTime, StepTime);
        BaselineMaxTime = FMath::Max(BaselineMaxTime, StepTime);
    }

    const double BaselineAvgMs = BaselineTotalTime / BaselineSamples;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  M4 Baseline: %.2f ms avg (min: %.2f, max: %.2f)"),
        BaselineAvgMs, BaselineMinTime, BaselineMaxTime);

    // ========================================
    // Test 2: M5 with Continental Erosion Only
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 2: M5 with Continental Erosion"));

    Params.bEnableContinentalErosion = true;
    Params.bEnableSedimentTransport = false;
    Params.bEnableOceanicDampening = false;
    Service->SetParameters(Params);
    InitializePlates(Service->GetPlatesForModification());
    Service->AdvanceSteps(5); // Warmup

    double ErosionTotalTime = 0.0;
    for (int32 i = 0; i < BaselineSamples; ++i)
    {
        const double StartTime = FPlatformTime::Seconds();
        Service->AdvanceSteps(1);
        const double EndTime = FPlatformTime::Seconds();
        ErosionTotalTime += (EndTime - StartTime) * 1000.0;
    }

    const double ErosionAvgMs = ErosionTotalTime / BaselineSamples;
    const double ErosionOverheadMs = ErosionAvgMs - BaselineAvgMs;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  With Erosion: %.2f ms avg (overhead: %.2f ms)"),
        ErosionAvgMs, ErosionOverheadMs);

    // ========================================
    // Test 3: M5 with Sediment Transport Only
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: M5 with Sediment Transport"));

    Params.bEnableContinentalErosion = false;
    Params.bEnableSedimentTransport = true;
    Params.bEnableOceanicDampening = false;
    Service->SetParameters(Params);
    InitializePlates(Service->GetPlatesForModification());
    Service->AdvanceSteps(5); // Warmup

    double SedimentTotalTime = 0.0;
    for (int32 i = 0; i < BaselineSamples; ++i)
    {
        const double StartTime = FPlatformTime::Seconds();
        Service->AdvanceSteps(1);
        const double EndTime = FPlatformTime::Seconds();
        SedimentTotalTime += (EndTime - StartTime) * 1000.0;
    }

    const double SedimentAvgMs = SedimentTotalTime / BaselineSamples;
    const double SedimentOverheadMs = SedimentAvgMs - BaselineAvgMs;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  With Sediment: %.2f ms avg (overhead: %.2f ms)"),
        SedimentAvgMs, SedimentOverheadMs);

    // ========================================
    // Test 4: M5 with Oceanic Dampening Only
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 4: M5 with Oceanic Dampening"));

    Params.bEnableContinentalErosion = false;
    Params.bEnableSedimentTransport = false;
    Params.bEnableOceanicDampening = true;
    Service->SetParameters(Params);
    InitializePlates(Service->GetPlatesForModification());
    Service->AdvanceSteps(5); // Warmup

    double DampeningTotalTime = 0.0;
    for (int32 i = 0; i < BaselineSamples; ++i)
    {
        const double StartTime = FPlatformTime::Seconds();
        Service->AdvanceSteps(1);
        const double EndTime = FPlatformTime::Seconds();
        DampeningTotalTime += (EndTime - StartTime) * 1000.0;
    }

    const double DampeningAvgMs = DampeningTotalTime / BaselineSamples;
    const double DampeningOverheadMs = DampeningAvgMs - BaselineAvgMs;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  With Dampening: %.2f ms avg (overhead: %.2f ms)"),
        DampeningAvgMs, DampeningOverheadMs);

    // ========================================
    // Test 5: M5 with All Features Enabled
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 5: M5 with All Features (Erosion + Sediment + Dampening)"));

    Params.bEnableContinentalErosion = true;
    Params.bEnableSedimentTransport = true;
    Params.bEnableOceanicDampening = true;
    Service->SetParameters(Params);
    InitializePlates(Service->GetPlatesForModification());
    Service->AdvanceSteps(5); // Warmup

    double FullM5TotalTime = 0.0;
    double FullM5MinTime = DBL_MAX;
    double FullM5MaxTime = 0.0;

    for (int32 i = 0; i < BaselineSamples; ++i)
    {
        const double StartTime = FPlatformTime::Seconds();
        Service->AdvanceSteps(1);
        const double EndTime = FPlatformTime::Seconds();
        const double StepTime = (EndTime - StartTime) * 1000.0;

        FullM5TotalTime += StepTime;
        FullM5MinTime = FMath::Min(FullM5MinTime, StepTime);
        FullM5MaxTime = FMath::Max(FullM5MaxTime, StepTime);
    }

    const double FullM5AvgMs = FullM5TotalTime / BaselineSamples;
    const double TotalOverheadMs = FullM5AvgMs - BaselineAvgMs;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Full M5: %.2f ms avg (min: %.2f, max: %.2f)"),
        FullM5AvgMs, FullM5MinTime, FullM5MaxTime);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Total Overhead: %.2f ms"), TotalOverheadMs);

    // ========================================
    // Summary & Validation
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Performance Summary:"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  M4 Baseline:       %.2f ms"), BaselineAvgMs);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Erosion Overhead:  %.2f ms (target: <5 ms)"), ErosionOverheadMs);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Sediment Overhead: %.2f ms (target: <4 ms)"), SedimentOverheadMs);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Dampening Overhead: %.2f ms (target: <3 ms)"), DampeningOverheadMs);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Total M5 Overhead: %.2f ms (target: <14 ms)"), TotalOverheadMs);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Full M5 Step Time: %.2f ms (target: <110 ms)"), FullM5AvgMs);

    // Validate targets
    const bool ErosionOK = ErosionOverheadMs < 6.0; // 5ms target + 1ms tolerance
    const bool SedimentOK = SedimentOverheadMs < 5.0; // 4ms target + 1ms tolerance
    const bool DampeningOK = DampeningOverheadMs < 4.0; // 3ms target + 1ms tolerance
    const bool TotalOverheadOK = TotalOverheadMs < 16.0; // 14ms target + 2ms tolerance
    const bool FullM5OK = FullM5AvgMs < 115.0; // 110ms target + 5ms tolerance

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Target Validation:"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Erosion: %s"), ErosionOK ? TEXT("PASS") : TEXT("FAIL"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Sediment: %s"), SedimentOK ? TEXT("PASS") : TEXT("FAIL"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Dampening: %s"), DampeningOK ? TEXT("PASS") : TEXT("FAIL"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Total Overhead: %s"), TotalOverheadOK ? TEXT("PASS") : TEXT("FAIL"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Full M5: %s"), FullM5OK ? TEXT("PASS") : TEXT("FAIL"));

    TestTrue(TEXT("Erosion overhead within budget"), ErosionOK);
    TestTrue(TEXT("Sediment overhead within budget"), SedimentOK);
    TestTrue(TEXT("Dampening overhead within budget"), DampeningOK);
    TestTrue(TEXT("Total M5 overhead within budget"), TotalOverheadOK);
    TestTrue(TEXT("Full M5 step time within budget"), FullM5OK);

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Performance Regression Test COMPLETE"));

    return true;
}
