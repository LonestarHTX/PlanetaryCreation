// Milestone 4 Task 1.1 Phase 3: Re-tessellation Regression Test

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

    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("=== Re-tessellation Regression Test ==="));

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
        UE_LOG(LogTemp, Log, TEXT(""));
        UE_LOG(LogTemp, Log, TEXT("Test %d: Level %d, Render Level %d, %d plates, %d steps, %.1f° threshold"),
            TestNum, Config.SubdivisionLevel, Config.RenderSubdivisionLevel,
            Config.ExpectedPlateCount, Config.SimSteps, Config.ThresholdDegrees);

        // Setup parameters
        FTectonicSimulationParameters Params;
        Params.Seed = 42;
        Params.SubdivisionLevel = Config.SubdivisionLevel;
        Params.RenderSubdivisionLevel = Config.RenderSubdivisionLevel;
        Params.LloydIterations = 0; // Skip for speed
        Params.RetessellationThresholdDegrees = Config.ThresholdDegrees;
        Service->SetParameters(Params);

        // Verify plate count
        const int32 ActualPlateCount = Service->GetPlates().Num();
        TestEqual(FString::Printf(TEXT("Test %d: Plate count"), TestNum), ActualPlateCount, Config.ExpectedPlateCount);

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
            UE_LOG(LogTemp, Log, TEXT("  ✓ Rebuild: %.2f ms, %d vertices"), RebuildTimeMs, PostVertexCount);
        }
        else
        {
            TestEqual(FString::Printf(TEXT("Test %d: No rebuild (early exit)"), TestNum), PostRebuildCount, PreRebuildCount);
            UE_LOG(LogTemp, Log, TEXT("  ✓ No rebuild: Plates within threshold"));
        }

        TestNum++;
    }

    // Performance summary
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("=== Performance Summary ==="));
    UE_LOG(LogTemp, Log, TEXT("Rebuild count: %d"), RebuildTimes.Num());

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

        UE_LOG(LogTemp, Log, TEXT("Rebuild times: Min=%.2f ms, Avg=%.2f ms, Max=%.2f ms"), MinTime, AvgTime, MaxTime);
        UE_LOG(LogTemp, Log, TEXT("Performance budget: 50 ms (target), 120 ms (ship)"));

        // Verify performance budget
        TestTrue(TEXT("Max rebuild time under ship budget (120ms)"), MaxTime < 120.0);

        if (MaxTime < 50.0)
        {
            UE_LOG(LogTemp, Log, TEXT("✅ All rebuilds under target budget (50ms)"));
        }
        else if (MaxTime < 100.0)
        {
            UE_LOG(LogTemp, Log, TEXT("⚠️ Some rebuilds exceed target (50ms) but under stretch goal (100ms)"));
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("⚠️ Some rebuilds exceed stretch goal (100ms) but under ship budget (120ms)"));
        }
    }

    AddInfo(TEXT("✅ Re-tessellation regression test complete"));
    AddInfo(FString::Printf(TEXT("Tested %d configurations | Rebuild count: %d"), TestConfigs.Num(), RebuildTimes.Num()));

    return true;
}
