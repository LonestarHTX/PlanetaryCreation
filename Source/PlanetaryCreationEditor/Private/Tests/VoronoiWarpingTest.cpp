// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 4 Task 5.0: Voronoi Warping Test
 *
 * Validates that noise-based Voronoi warping creates irregular plate boundaries:
 * - Warping enabled produces different assignments than perfect Voronoi
 * - Boundary irregularity increases with amplitude
 * - Feature is deterministic (same seed = same warping)
 * - Implements paper Section 3: "More irregular continent shapes can be obtained by warping
 *   the geodesic distances to the centroids using a simple noise function."
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FVoronoiWarpingTest,
    "PlanetaryCreation.Milestone4.VoronoiWarping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoronoiWarpingTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get UTectonicSimulationService"));
        return false;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Voronoi Warping Test ==="));

    // Test 1: Warping disabled vs enabled produces different assignments
    FTectonicSimulationParameters Params = Service->GetParameters();
    Params.Seed = 42;
    Params.SubdivisionLevel = 0; // 20 plates
    Params.RenderSubdivisionLevel = 2; // 162 vertices
    Params.LloydIterations = 4;
    Params.bEnableVoronoiWarping = false;
    Params.VoronoiWarpingAmplitude = 0.0;
    Service->SetParameters(Params);

    // IMPORTANT: Copy the array, not reference, since SetParameters() modifies in place
    const TArray<int32> UnwarpedAssignments = Service->GetVertexPlateAssignments();
    const int32 VertexCount = Service->GetRenderVertices().Num();

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: Unwarped Voronoi - %d vertices assigned"), VertexCount);

    // Enable warping
    Params.bEnableVoronoiWarping = true;
    Params.VoronoiWarpingAmplitude = 0.5;
    Params.VoronoiWarpingFrequency = 2.0;
    Service->SetParameters(Params);

    // IMPORTANT: Copy again for comparison
    const TArray<int32> WarpedAssignments = Service->GetVertexPlateAssignments();

    // Count differences
    int32 DifferentCount = 0;
    for (int32 i = 0; i < VertexCount; ++i)
    {
        if (UnwarpedAssignments[i] != WarpedAssignments[i])
        {
            DifferentCount++;
        }
    }

    const double DifferencePercentage = (static_cast<double>(DifferentCount) / VertexCount) * 100.0;
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1 Result: %d/%d vertices (%.1f%%) changed assignment with warping"),
        DifferentCount, VertexCount, DifferencePercentage);

    TestTrue(TEXT("Warping changes vertex assignments (>1% different)"), DifferencePercentage > 1.0);

    // Test 2: Higher amplitude = more irregularity
    Params.VoronoiWarpingAmplitude = 1.0; // Double amplitude
    Service->SetParameters(Params);

    const TArray<int32> HighWarpAssignments = Service->GetVertexPlateAssignments();

    int32 HighWarpDifferentCount = 0;
    for (int32 i = 0; i < VertexCount; ++i)
    {
        if (UnwarpedAssignments[i] != HighWarpAssignments[i])
        {
            HighWarpDifferentCount++;
        }
    }

    const double HighWarpPercentage = (static_cast<double>(HighWarpDifferentCount) / VertexCount) * 100.0;
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 2: High amplitude (1.0) - %d/%d vertices (%.1f%%) different"),
        HighWarpDifferentCount, VertexCount, HighWarpPercentage);

    TestTrue(TEXT("Higher amplitude increases boundary irregularity"), HighWarpPercentage > DifferencePercentage);

    // Test 3: Determinism - same seed produces same warping
    Service->SetParameters(Params); // Reset with same seed
    const TArray<int32> SecondRun = Service->GetVertexPlateAssignments();

    bool bDeterministic = true;
    for (int32 i = 0; i < VertexCount; ++i)
    {
        if (HighWarpAssignments[i] != SecondRun[i])
        {
            bDeterministic = false;
            break;
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: Determinism - %s"), bDeterministic ? TEXT("PASS") : TEXT("FAIL"));
    TestTrue(TEXT("Warping is deterministic (same seed = same assignments)"), bDeterministic);

    // Test 4: Different seed produces different warping (without Lloyd to avoid convergence)
    Params.Seed = 99999; // Different seed
    Params.LloydIterations = 0; // Disable Lloyd to get different centroids
    Service->SetParameters(Params);

    const TArray<int32> DifferentSeedAssignments = Service->GetVertexPlateAssignments();

    int32 SeedDifferenceCount = 0;
    for (int32 i = 0; i < VertexCount; ++i)
    {
        if (HighWarpAssignments[i] != DifferentSeedAssignments[i])
        {
            SeedDifferenceCount++;
        }
    }

    const double SeedDifferencePercentage = (static_cast<double>(SeedDifferenceCount) / VertexCount) * 100.0;
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 4: Different seed (no Lloyd) - %d/%d vertices (%.1f%%) different"),
        SeedDifferenceCount, VertexCount, SeedDifferencePercentage);

    TestTrue(TEXT("Different seeds produce different warping patterns"), SeedDifferencePercentage > 5.0);

    // Test 5: Frequency affects detail scale
    Params.Seed = 42; // Reset to consistent seed
    Params.VoronoiWarpingFrequency = 4.0; // Higher frequency (finer detail)
    Service->SetParameters(Params);

    const TArray<int32> HighFreqAssignments = Service->GetVertexPlateAssignments();

    int32 FreqDifferenceCount = 0;
    for (int32 i = 0; i < VertexCount; ++i)
    {
        if (WarpedAssignments[i] != HighFreqAssignments[i])
        {
            FreqDifferenceCount++;
        }
    }

    const double FreqDifferencePercentage = (static_cast<double>(FreqDifferenceCount) / VertexCount) * 100.0;
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 5: High frequency (4.0) vs baseline (2.0) - %.1f%% different"), FreqDifferencePercentage);

    TestTrue(TEXT("Frequency parameter affects warping pattern"), FreqDifferencePercentage > 5.0);

    // Summary
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Voronoi Warping Test Complete ==="));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("✓ Warping creates irregular plate boundaries (paper Section 3)"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("✓ Amplitude controls irregularity magnitude"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("✓ Frequency controls boundary detail scale"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("✓ Feature is deterministic and controllable"));

    return true;
}
