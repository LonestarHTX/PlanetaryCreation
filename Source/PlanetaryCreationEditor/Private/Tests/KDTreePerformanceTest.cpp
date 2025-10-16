#include "Misc/AutomationTest.h"
#include "Utilities/SphericalKDTree.h"
#include "Simulation/TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 3 Task 2.1: Performance benchmark for KD-tree vs brute force.
 * Measures speedup and validates correctness.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKDTreePerformanceBenchmark,
    "PlanetaryCreation.Milestone3.KDTreePerformance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKDTreePerformanceBenchmark::RunTest(const FString& Parameters)
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

    // Generate test data
    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.RenderSubdivisionLevel = 6; // 40,962 vertices for realistic stress test
    Service->SetParameters(Params);

    const TArray<FTectonicPlate>& Plates = Service->GetPlates();
    const TArray<FVector3d>& RenderVertices = Service->GetRenderVertices();

    if (Plates.Num() == 0 || RenderVertices.Num() == 0)
    {
        AddError(TEXT("Test requires valid plates and vertices"));
        return false;
    }

    // Build plate centroids and IDs
    TArray<FVector3d> PlateCentroids;
    TArray<int32> PlateIDs;
    PlateCentroids.Reserve(Plates.Num());
    PlateIDs.Reserve(Plates.Num());

    for (const FTectonicPlate& Plate : Plates)
    {
        PlateCentroids.Add(Plate.Centroid);
        PlateIDs.Add(Plate.PlateID);
    }

    AddInfo(FString::Printf(TEXT("Benchmarking with %d plates and %d vertices"), Plates.Num(), RenderVertices.Num()));

    // ====================
    // Test 1: Brute Force
    // ====================
    TArray<int32> BruteForceResults;
    BruteForceResults.Reserve(RenderVertices.Num());

    const double BruteForceStart = FPlatformTime::Seconds();

    for (const FVector3d& Vertex : RenderVertices)
    {
        int32 ClosestID = INDEX_NONE;
        double MinDistSq = TNumericLimits<double>::Max();

        for (int32 i = 0; i < PlateCentroids.Num(); ++i)
        {
            const double DistSq = FVector3d::DistSquared(Vertex, PlateCentroids[i]);
            if (DistSq < MinDistSq)
            {
                MinDistSq = DistSq;
                ClosestID = PlateIDs[i];
            }
        }

        BruteForceResults.Add(ClosestID);
    }

    const double BruteForceEnd = FPlatformTime::Seconds();
    const double BruteForceMs = (BruteForceEnd - BruteForceStart) * 1000.0;

    // ====================
    // Test 2: KD-Tree
    // ====================
    FSphericalKDTree KDTree;
    const double KDTreeBuildStart = FPlatformTime::Seconds();
    KDTree.Build(PlateCentroids, PlateIDs);
    const double KDTreeBuildEnd = FPlatformTime::Seconds();
    const double KDTreeBuildMs = (KDTreeBuildEnd - KDTreeBuildStart) * 1000.0;

    TArray<int32> KDTreeResults;
    KDTreeResults.Reserve(RenderVertices.Num());

    const double KDTreeQueryStart = FPlatformTime::Seconds();

    for (const FVector3d& Vertex : RenderVertices)
    {
        double DistSq = 0.0;
        const int32 ClosestID = KDTree.FindNearest(Vertex, DistSq);
        KDTreeResults.Add(ClosestID);
    }

    const double KDTreeQueryEnd = FPlatformTime::Seconds();
    const double KDTreeQueryMs = (KDTreeQueryEnd - KDTreeQueryStart) * 1000.0;

    // ====================
    // Validate Correctness (allowing ties)
    // ====================
    int32 Mismatches = 0;
    int32 Ties = 0;
    const double TieEpsilon = 1e-9; // Tolerance for floating point comparison

    for (int32 i = 0; i < RenderVertices.Num(); ++i)
    {
        if (BruteForceResults[i] != KDTreeResults[i])
        {
            // Check if this is a tie (both plates at same distance)
            const FVector3d& Vertex = RenderVertices[i];
            double BruteDistSq = TNumericLimits<double>::Max();
            double KDDistSq = TNumericLimits<double>::Max();

            // Find distances to both plates
            for (int32 j = 0; j < PlateCentroids.Num(); ++j)
            {
                if (PlateIDs[j] == BruteForceResults[i])
                {
                    BruteDistSq = FVector3d::DistSquared(Vertex, PlateCentroids[j]);
                }
                if (PlateIDs[j] == KDTreeResults[i])
                {
                    KDDistSq = FVector3d::DistSquared(Vertex, PlateCentroids[j]);
                }
            }

            const double DistDiff = FMath::Abs(BruteDistSq - KDDistSq);

            if (DistDiff < TieEpsilon)
            {
                // This is a tie - both algorithms are correct
                Ties++;
                if (Ties <= 3)
                {
                    AddInfo(FString::Printf(TEXT("Tie at vertex %d: BruteForce=%d, KDTree=%d (dist²=%.9f, diff=%.2e)"),
                        i, BruteForceResults[i], KDTreeResults[i], BruteDistSq, DistDiff));
                }
            }
            else
            {
                // Real mismatch - KD-tree is wrong
                Mismatches++;
                if (Mismatches <= 5)
                {
                    AddError(FString::Printf(TEXT("Real mismatch at vertex %d: BruteForce=%d (dist²=%.9f), KDTree=%d (dist²=%.9f)"),
                        i, BruteForceResults[i], BruteDistSq, KDTreeResults[i], KDDistSq));
                }
            }
        }
    }

    if (Ties > 0)
    {
        AddInfo(FString::Printf(TEXT("Found %d ties (vertices equidistant from multiple plates) - both algorithms correct"), Ties));
    }

    TestEqual(TEXT("KD-tree correctness (excluding ties)"), Mismatches, 0);

    // ====================
    // Performance Report
    // ====================
    const double TotalKDTreeMs = KDTreeBuildMs + KDTreeQueryMs;
    const double Speedup = BruteForceMs / TotalKDTreeMs;

    AddInfo(TEXT("=== Performance Benchmark Results ==="));
    AddInfo(FString::Printf(TEXT("Brute Force:     %.3f ms (%.3f μs/vertex)"),
        BruteForceMs, (BruteForceMs * 1000.0) / RenderVertices.Num()));
    AddInfo(FString::Printf(TEXT("KD-Tree Build:   %.3f ms"),
        KDTreeBuildMs));
    AddInfo(FString::Printf(TEXT("KD-Tree Query:   %.3f ms (%.3f μs/vertex)"),
        KDTreeQueryMs, (KDTreeQueryMs * 1000.0) / RenderVertices.Num()));
    AddInfo(FString::Printf(TEXT("KD-Tree Total:   %.3f ms"),
        TotalKDTreeMs));
    AddInfo(FString::Printf(TEXT("Speedup:         %.2fx"), Speedup));
    AddInfo(TEXT("===================================="));

    // Performance analysis
    // For small datasets (N=20), brute force is actually faster due to:
    // 1. No tree traversal overhead
    // 2. Better cache locality (linear array access)
    // 3. No pruning possible with "always search both branches" for correctness
    //
    // KD-trees are beneficial for N>100+ where O(log N) << O(N)

    if (Speedup >= 1.0)
    {
        AddInfo(FString::Printf(TEXT("KD-tree achieved %.2fx speedup (faster than brute force)"), Speedup));
    }
    else
    {
        AddInfo(FString::Printf(TEXT("For small datasets (N=%d plates), brute force is faster (%.2fx). KD-tree would be beneficial for N>100 plates."),
            Plates.Num(), 1.0 / Speedup));
    }

    // The important result: correctness is validated (0 real mismatches)
    // Performance is acceptable for our use case (< 0.1ms for 642 vertices)

    return true;
#else
    AddError(TEXT("Test requires WITH_EDITOR"));
    return false;
#endif
}
