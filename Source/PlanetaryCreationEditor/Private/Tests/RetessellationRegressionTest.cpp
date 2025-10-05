// Milestone 4 Task 1.1 Phase 3: Re-tessellation Regression Test

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 4 Task 1.1 Phase 3: Re-tessellation Regression & Performance Validation
 *
 * Tests re-tessellation stability and performance across multiple subdivision levels.
 * Validates both rebuild and no-rebuild paths as requested by Simulation Lead.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRetessellationRegressionTest,
    "PlanetaryCreation.Milestone4.RetessellationRegression",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRetessellationRegressionTest::RunTest(const FString& Parameters)
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
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Re-tessellation Regression Test ==="));

    // Test configuration
    struct FTestConfig
    {
        int32 SubdivisionLevel;
        int32 RenderSubdivisionLevel;
        int32 ExpectedPlateCount;
        int32 SimSteps; // Steps to cause drift
        double ThresholdDegrees;
        bool ExpectRebuild;
    };

    TArray<FTestConfig> TestConfigs = {
        // Level 0: Baseline (20 plates)
        { 0, 0, 20, 10, 120.0, false },  // No rebuild: 120° threshold (some plates drift >90° with 10 steps)
        { 0, 1, 20, 20, 5.0, true },     // Rebuild: 20 steps with 5° threshold triggers

        // Level 1: Higher resolution (80 plates)
        { 1, 1, 80, 10, 120.0, false },
        { 1, 2, 80, 20, 5.0, true },

        // Level 2: High resolution (320 plates)
        { 2, 2, 320, 10, 120.0, false },
        { 2, 3, 320, 15, 10.0, true },

        // Level 3: Ultra high resolution (1280 plates) - performance stress test
        { 3, 3, 1280, 5, 180.0, false },  // 5 steps, very high threshold (fast plates)
        { 3, 4, 1280, 10, 1.0, true },

        // Level 4-6: High-density render mesh performance benchmarks
        { 0, 4, 20, 20, 5.0, true },  // Level 4: 5120 faces
        { 0, 5, 20, 20, 5.0, true },  // Level 5: 20480 faces
        { 0, 6, 20, 20, 5.0, true },  // Level 6: 81920 faces
    };

    int32 TestNum = 1;
    TArray<double> RebuildTimes;

    for (const FTestConfig& Config : TestConfigs)
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Test %d: Level %d, Render Level %d, %d plates, %d steps, %.1f° threshold"),
            TestNum, Config.SubdivisionLevel, Config.RenderSubdivisionLevel,
            Config.ExpectedPlateCount, Config.SimSteps, Config.ThresholdDegrees);

        // Setup parameters
        FTectonicSimulationParameters Params;
        Params.Seed = 42;
        Params.SubdivisionLevel = Config.SubdivisionLevel;
        Params.RenderSubdivisionLevel = Config.RenderSubdivisionLevel;
        Params.LloydIterations = 0; // Skip for speed
        Params.RetessellationThresholdDegrees = Config.ThresholdDegrees;
        Params.bEnableDynamicRetessellation = false; // keep auto-retess off so we can exercise manual path deterministically
        Service->SetParameters(Params);

        // Verify plate count
        const int32 ActualPlateCount = Service->GetPlates().Num();
        TestEqual(FString::Printf(TEXT("Test %d: Plate count"), TestNum), ActualPlateCount, Config.ExpectedPlateCount);

        // Apply high angular velocities to cause drift
        TArray<FTectonicPlate>& Plates = Service->GetPlatesForModification();
        for (int32 i = 0; i < Plates.Num(); ++i)
        {
            // Vary velocities by plate to create diverse drift patterns
            Plates[i].EulerPoleAxis = FVector3d(
                FMath::Sin(i * 0.7),
                FMath::Cos(i * 0.9),
                FMath::Sin(i * 1.1)
            ).GetSafeNormal();
            Plates[i].AngularVelocity = 0.05; // rad/My (moderate speed for consistent drift)
        }

        // Advance simulation to cause drift
        Service->AdvanceSteps(Config.SimSteps);

        // Capture state before rebuild
        const int32 PreRebuildCount = Service->RetessellationCount;
        const int32 PreVertexCount = Service->GetRenderVertices().Num();

        // Perform re-tessellation
        const bool RebuildSuccess = Service->PerformRetessellation();
        TestTrue(FString::Printf(TEXT("Test %d: Re-tessellation succeeds"), TestNum), RebuildSuccess);

        // Verify rebuild behavior
        const int32 PostRebuildCount = Service->RetessellationCount;
        const int32 PostVertexCount = Service->GetRenderVertices().Num();
        const double RebuildTimeMs = Service->LastRetessellationTimeMs;

        if (Config.ExpectRebuild)
        {
            TestTrue(FString::Printf(TEXT("Test %d: Rebuild triggered"), TestNum), PostRebuildCount > PreRebuildCount);
            TestEqual(FString::Printf(TEXT("Test %d: Vertex count preserved"), TestNum), PostVertexCount, PreVertexCount);

            RebuildTimes.Add(RebuildTimeMs);
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Rebuild: %.2f ms, %d vertices"), RebuildTimeMs, PostVertexCount);
        }
        else
        {
            TestEqual(FString::Printf(TEXT("Test %d: No rebuild (early exit)"), TestNum), PostRebuildCount, PreRebuildCount);
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ No rebuild: Plates within threshold"));
        }

        TestNum++;
    }

    // Performance summary
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Performance Summary ==="));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Rebuild count: %d"), RebuildTimes.Num());

    if (RebuildTimes.Num() > 0)
    {
        double MinTime = RebuildTimes[0];
        double MaxTime = RebuildTimes[0];
        double TotalTime = 0.0;

        for (double Time : RebuildTimes)
        {
            MinTime = FMath::Min(MinTime, Time);
            MaxTime = FMath::Max(MaxTime, Time);
            TotalTime += Time;
        }

        const double AvgTime = TotalTime / RebuildTimes.Num();

        UE_LOG(LogPlanetaryCreation, Log, TEXT("Rebuild times: Min=%.2f ms, Avg=%.2f ms, Max=%.2f ms"), MinTime, AvgTime, MaxTime);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Performance budget: 50 ms (target), 120 ms (ship)"));

        // Verify performance budget (soft assertion - expected to exceed in initial implementation)
        TestTrue(TEXT("Max rebuild time under ship budget (120ms)"), MaxTime < 120.0);

        if (MaxTime < 50.0)
        {
            UE_LOG(LogPlanetaryCreation, Log, TEXT("✅ All rebuilds under target budget (50ms)"));
        }
        else if (MaxTime < 100.0)
        {
            UE_LOG(LogPlanetaryCreation, Log, TEXT("⚠️ Some rebuilds exceed target (50ms) but under stretch goal (100ms)"));
        }
        else if (MaxTime < 120.0)
        {
            UE_LOG(LogPlanetaryCreation, Log, TEXT("⚠️ Some rebuilds exceed stretch goal (100ms) but under ship budget (120ms)"));
        }
        else
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("❌ PERF OVERAGE: Max rebuild time %.2f ms exceeds ship budget (120ms)"), MaxTime);
            UE_LOG(LogPlanetaryCreation, Error, TEXT("   ⚠️ EXPECTED OVERAGE - Flagged for Milestone 6 optimization pass (SIMD/GPU)"));
            UE_LOG(LogPlanetaryCreation, Error, TEXT("   Current baseline: %.2f ms | Target: 50 ms | Ship budget: 120 ms"), MaxTime);
        }
    }

    AddInfo(TEXT("✅ Re-tessellation regression test complete"));
    AddInfo(FString::Printf(TEXT("Tested %d configurations | Rebuild count: %d"), TestConfigs.Num(), RebuildTimes.Num()));

    return true;
}
