// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "TectonicSimulationController.h"
#include "Editor.h"
#include "HAL/PlatformMemory.h"

/**
 * Milestone 3 Task 4.4: Performance Profiling & Optimization
 *
 * Captures performance metrics across subdivision levels:
 * - Step time per level (0-6)
 * - Mesh build time (sync vs async)
 * - Memory footprint
 * - Vertex/triangle counts
 *
 * Runs 100-step simulation at level 3 to identify bottlenecks.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerformanceProfilingTest,
    "PlanetaryCreation.Milestone3.PerformanceProfiling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPerformanceProfilingTest::RunTest(const FString& Parameters)
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

    TSharedPtr<FTectonicSimulationController> Controller = MakeShared<FTectonicSimulationController>();
    Controller->Initialize();

    // Capture baseline memory before test
    FPlatformMemoryStats MemStatsBefore = FPlatformMemory::GetStats();
    const uint64 MemoryBeforeMB = MemStatsBefore.UsedPhysical / (1024 * 1024);

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== MILESTONE 3 PERFORMANCE PROFILING ==="));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Memory before test: %llu MB"), MemoryBeforeMB);
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // Test each subdivision level (0-6)
    struct FLevelStats
    {
        int32 Level;
        int32 VertexCount;
        int32 TriangleCount;
        double AvgStepTimeMs;
        double AvgMeshBuildTimeMs;
        bool bUsesAsync;
    };

    TArray<FLevelStats> LevelStats;

    for (int32 Level = 0; Level <= 6; ++Level)
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT("--- Testing Subdivision Level %d ---"), Level);

        FTectonicSimulationParameters Params;
        Params.Seed = 42;
        Params.SubdivisionLevel = 0; // 20 plates
        Params.RenderSubdivisionLevel = Level;
        Params.LloydIterations = 0; // Skip for speed
        Service->SetParameters(Params);

        // Warm-up step
        Controller->StepSimulation(1);
        FPlatformProcess::Sleep(0.2f);

        // Capture step times
        const int32 NumSteps = (Level <= 3) ? 20 : 10; // Fewer steps for high levels
        TArray<double> StepTimes;
        TArray<double> MeshBuildTimes;

        for (int32 i = 0; i < NumSteps; ++i)
        {
            const double StepStart = FPlatformTime::Seconds();
            Controller->StepSimulation(1);

            // Wait for async mesh build to complete
            FPlatformProcess::Sleep((Level >= 3) ? 0.1f : 0.01f);

            const double StepEnd = FPlatformTime::Seconds();
            StepTimes.Add((StepEnd - StepStart) * 1000.0);

            // Capture mesh build time from service
            MeshBuildTimes.Add(Service->GetLastStepTimeMs());
        }

        // Calculate averages
        double TotalStepTime = 0.0;
        double TotalMeshTime = 0.0;
        for (int32 i = 0; i < StepTimes.Num(); ++i)
        {
            TotalStepTime += StepTimes[i];
            TotalMeshTime += MeshBuildTimes[i];
        }

        FLevelStats Stats;
        Stats.Level = Level;
        Stats.VertexCount = Service->GetRenderVertices().Num();
        Stats.TriangleCount = Service->GetRenderTriangles().Num() / 3;
        Stats.AvgStepTimeMs = TotalStepTime / StepTimes.Num();
        Stats.AvgMeshBuildTimeMs = TotalMeshTime / MeshBuildTimes.Num();
        Stats.bUsesAsync = (Level >= 3);

        LevelStats.Add(Stats);

        UE_LOG(LogPlanetaryCreation, Log, TEXT("  Vertices: %d | Triangles: %d"), Stats.VertexCount, Stats.TriangleCount);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  Avg Step Time: %.2f ms"), Stats.AvgStepTimeMs);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  Avg Simulation Time: %.2f ms"), Stats.AvgMeshBuildTimeMs);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  Path: %s"), Stats.bUsesAsync ? TEXT("ASYNC") : TEXT("SYNC"));
        UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    }

    // 100-step benchmark at level 3 (acceptance criteria target)
    UE_LOG(LogPlanetaryCreation, Log, TEXT("--- 100-Step Benchmark (Level 3) ---"));

    FTectonicSimulationParameters BenchmarkParams;
    BenchmarkParams.Seed = 12345;
    BenchmarkParams.SubdivisionLevel = 0;
    BenchmarkParams.RenderSubdivisionLevel = 3; // 1280 triangles
    BenchmarkParams.LloydIterations = 0;
    Service->SetParameters(BenchmarkParams);

    double TotalBenchmarkTime = 0.0;
    double MinStepTime = DBL_MAX;
    double MaxStepTime = 0.0;

    const double BenchmarkStart = FPlatformTime::Seconds();

    for (int32 i = 0; i < 100; ++i)
    {
        const double StepStart = FPlatformTime::Seconds();
        Service->AdvanceSteps(1);
        const double StepEnd = FPlatformTime::Seconds();

        const double StepTime = (StepEnd - StepStart) * 1000.0;
        TotalBenchmarkTime += StepTime;
        MinStepTime = FMath::Min(MinStepTime, StepTime);
        MaxStepTime = FMath::Max(MaxStepTime, StepTime);

        if ((i + 1) % 20 == 0)
        {
            UE_LOG(LogPlanetaryCreation, Verbose, TEXT("  Completed %d/100 steps..."), i + 1);
        }
    }

    const double BenchmarkEnd = FPlatformTime::Seconds();
    const double TotalBenchmarkTimeSeconds = BenchmarkEnd - BenchmarkStart;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Total Time: %.2f seconds"), TotalBenchmarkTimeSeconds);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Avg Step Time: %.2f ms"), TotalBenchmarkTime / 100.0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Min Step Time: %.2f ms"), MinStepTime);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Max Step Time: %.2f ms"), MaxStepTime);
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // Memory footprint check
    FPlatformMemoryStats MemStatsAfter = FPlatformMemory::GetStats();
    const uint64 MemoryAfterMB = MemStatsAfter.UsedPhysical / (1024 * 1024);
    const int64 MemoryDeltaMB = static_cast<int64>(MemoryAfterMB) - static_cast<int64>(MemoryBeforeMB);

    UE_LOG(LogPlanetaryCreation, Log, TEXT("--- Memory Footprint ---"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Before: %llu MB"), MemoryBeforeMB);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  After: %llu MB"), MemoryAfterMB);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Delta: %lld MB"), MemoryDeltaMB);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Target: <500 MB total simulation state"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // Summary table
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== PERFORMANCE SUMMARY ==="));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Level | Vertices | Triangles | Avg Step (ms) | Path"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("------|----------|-----------|---------------|------"));
    for (const FLevelStats& Stats : LevelStats)
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  %d   | %8d | %9d | %13.2f | %s"),
            Stats.Level, Stats.VertexCount, Stats.TriangleCount,
            Stats.AvgStepTimeMs, Stats.bUsesAsync ? TEXT("ASYNC") : TEXT("SYNC "));
    }
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // Acceptance criteria validation
    const double Level3AvgTime = LevelStats[3].AvgStepTimeMs;
    const bool bMeetsPerformanceTarget = (Level3AvgTime < 100.0);

    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== ACCEPTANCE CRITERIA ==="));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Target: Step time <100ms at level 3"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Actual: %.2f ms"), Level3AvgTime);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Status: %s"), bMeetsPerformanceTarget ? TEXT("✅ PASS") : TEXT("❌ FAIL"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    Controller->Shutdown();

    // Test assertions
    TestTrue(TEXT("Step time <100ms at level 3"), bMeetsPerformanceTarget);
    TestTrue(TEXT("Memory delta reasonable (<500MB)"), FMath::Abs(MemoryDeltaMB) < 500);

    AddInfo(TEXT("✅ Performance profiling complete. Check Output Log for detailed metrics."));
    AddInfo(FString::Printf(TEXT("Level 3 avg step time: %.2f ms (target: <100ms)"), Level3AvgTime));

    return true;
}
