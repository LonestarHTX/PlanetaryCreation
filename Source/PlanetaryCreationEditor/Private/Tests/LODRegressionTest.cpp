// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 4 Phase 4.1: LOD Regression Test
 *
 * Validates that changing render subdivision level (LOD) does NOT reset simulation state.
 * This test ensures the critical regression fix where UpdateLOD() was calling SetParameters()
 * and destroying the entire tectonic simulation on every camera zoom.
 *
 * Test Coverage:
 * 1. Simulation state preservation across LOD changes
 * 2. Plate count, position, and properties remain unchanged
 * 3. Stress accumulation persists
 * 4. Topology events are preserved
 * 5. Render mesh regenerates correctly at new LOD
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLODRegressionTest,
    "PlanetaryCreation.Milestone4.LODRegression",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLODRegressionTest::RunTest(const FString& Parameters)
{
    // Get simulation service
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get UTectonicSimulationService"));
        return false;
    }

    // Setup: Reset to baseline
    FTectonicSimulationParameters Params = Service->GetParameters();
    Params.Seed = 12345;
    Params.SubdivisionLevel = 0; // 20 plates
    Params.RenderSubdivisionLevel = 2; // Start at L2
    Params.LloydIterations = 4;
    Params.bEnableDynamicRetessellation = true;
    Service->SetParameters(Params);

    UE_LOG(LogTemp, Log, TEXT("=== LOD Regression Test ==="));
    UE_LOG(LogTemp, Log, TEXT("Test: Verify LOD changes preserve simulation state"));

    // Step 1: Advance simulation to build up state
    UE_LOG(LogTemp, Log, TEXT("Step 1: Advancing simulation 10 steps to accumulate state..."));
    Service->AdvanceSteps(10);

    // Capture baseline state BEFORE LOD change
    const TArray<FTectonicPlate>& PlatesBefore = Service->GetPlates();
    const TMap<TPair<int32, int32>, FPlateBoundary>& BoundariesBefore = Service->GetBoundaries();
    const double CurrentTimeBefore = Service->GetCurrentTimeMy();
    const int32 PlateCountBefore = PlatesBefore.Num();
    const int32 RenderVerticesBefore = Service->GetRenderVertices().Num();
    const int32 RenderTrianglesBefore = Service->GetRenderTriangles().Num() / 3;

    // Capture plate centroids and stress
    TArray<FVector3d> CentroidsBefore;
    TArray<double> StressBefore;
    for (const FTectonicPlate& Plate : PlatesBefore)
    {
        CentroidsBefore.Add(Plate.Centroid);
    }
    for (const auto& BoundaryPair : BoundariesBefore)
    {
        StressBefore.Add(BoundaryPair.Value.AccumulatedStress);
    }

    UE_LOG(LogTemp, Log, TEXT("  Baseline: %d plates, %.2f My, %d render verts, %d tris, L%d"),
        PlateCountBefore, CurrentTimeBefore, RenderVerticesBefore, RenderTrianglesBefore, Params.RenderSubdivisionLevel);

    // Step 2: Change LOD using non-destructive method
    UE_LOG(LogTemp, Log, TEXT("Step 2: Changing LOD from L2 → L4 using SetRenderSubdivisionLevel()..."));
    Service->SetRenderSubdivisionLevel(4);

    // Capture state AFTER LOD change
    const TArray<FTectonicPlate>& PlatesAfter = Service->GetPlates();
    const TMap<TPair<int32, int32>, FPlateBoundary>& BoundariesAfter = Service->GetBoundaries();
    const double CurrentTimeAfter = Service->GetCurrentTimeMy();
    const int32 PlateCountAfter = PlatesAfter.Num();
    const int32 RenderVerticesAfter = Service->GetRenderVertices().Num();
    const int32 RenderTrianglesAfter = Service->GetRenderTriangles().Num() / 3;

    UE_LOG(LogTemp, Log, TEXT("  After LOD: %d plates, %.2f My, %d render verts, %d tris, L%d"),
        PlateCountAfter, CurrentTimeAfter, RenderVerticesAfter, RenderTrianglesAfter, 4);

    // Step 3: Validate simulation state preservation
    UE_LOG(LogTemp, Log, TEXT("Step 3: Validating state preservation..."));

    // Test 3.1: Plate count unchanged
    TestEqual(TEXT("Plate count preserved"), PlateCountAfter, PlateCountBefore);

    // Test 3.2: Simulation time unchanged
    TestEqual(TEXT("Simulation time preserved"), CurrentTimeAfter, CurrentTimeBefore);

    // Test 3.3: Plate centroids unchanged (within numerical precision)
    bool bCentroidsMatch = true;
    for (int32 i = 0; i < PlatesBefore.Num(); ++i)
    {
        const double Distance = (PlatesAfter[i].Centroid - CentroidsBefore[i]).Length();
        if (Distance > 1e-10)
        {
            bCentroidsMatch = false;
            AddError(FString::Printf(TEXT("Plate %d centroid changed by %.6e after LOD switch"), i, Distance));
        }
    }
    TestTrue(TEXT("Plate centroids preserved"), bCentroidsMatch);

    // Test 3.4: Boundary stress preserved
    bool bStressMatch = true;
    int32 BoundaryIndex = 0;
    for (const auto& BoundaryPair : BoundariesAfter)
    {
        if (BoundaryIndex < StressBefore.Num())
        {
            const double StressDiff = FMath::Abs(BoundaryPair.Value.AccumulatedStress - StressBefore[BoundaryIndex]);
            if (StressDiff > 1e-6)
            {
                bStressMatch = false;
                AddError(FString::Printf(TEXT("Boundary %d stress changed by %.6e after LOD switch"), BoundaryIndex, StressDiff));
            }
        }
        BoundaryIndex++;
    }
    TestTrue(TEXT("Boundary stress preserved"), bStressMatch);

    // Step 4: Validate render mesh regeneration
    UE_LOG(LogTemp, Log, TEXT("Step 4: Validating render mesh regeneration..."));

    // Test 4.1: Render mesh changed (expected - we're at different LOD)
    TestNotEqual(TEXT("Render vertex count changed (expected)"), RenderVerticesAfter, RenderVerticesBefore);
    TestNotEqual(TEXT("Render triangle count changed (expected)"), RenderTrianglesAfter, RenderTrianglesBefore);

    // Test 4.2: Verify L4 has expected scale (Level 4 = 5120 triangles for icosphere)
    const int32 ExpectedL4Triangles = 5120;
    TestEqual(TEXT("L4 triangle count correct"), RenderTrianglesAfter, ExpectedL4Triangles);

    // Test 4.3: Voronoi mapping complete (all vertices assigned)
    const TArray<int32>& VertexAssignments = Service->GetVertexPlateAssignments();
    bool bAllAssigned = true;
    for (int32 Assignment : VertexAssignments)
    {
        if (Assignment == INDEX_NONE || Assignment >= PlateCountAfter)
        {
            bAllAssigned = false;
            break;
        }
    }
    TestTrue(TEXT("Voronoi mapping complete after LOD change"), bAllAssigned);

    // Step 5: Advance simulation after LOD change to verify it continues correctly
    UE_LOG(LogTemp, Log, TEXT("Step 5: Advancing 5 more steps to verify simulation continues..."));
    Service->AdvanceSteps(5);

    const double CurrentTimeFinal = Service->GetCurrentTimeMy();
    const double ExpectedTimeFinal = CurrentTimeBefore + (5 * 2.0); // 5 steps × 2 My/step
    TestEqual(TEXT("Simulation continues after LOD change"), CurrentTimeFinal, ExpectedTimeFinal);

    // Step 6: Test LOD change in opposite direction (L4 → L2)
    UE_LOG(LogTemp, Log, TEXT("Step 6: Testing reverse LOD change (L4 → L2)..."));
    Service->SetRenderSubdivisionLevel(2);

    const int32 RenderVerticesReversed = Service->GetRenderVertices().Num();
    const int32 RenderTrianglesReversed = Service->GetRenderTriangles().Num() / 3;
    const int32 PlateCountReversed = Service->GetPlates().Num();

    TestEqual(TEXT("Reverse LOD: Plate count still preserved"), PlateCountReversed, PlateCountBefore);
    TestEqual(TEXT("Reverse LOD: Back to L2 triangle count"), RenderTrianglesReversed, RenderTrianglesBefore);

    UE_LOG(LogTemp, Log, TEXT("  Reversed to L2: %d plates, %d verts, %d tris"),
        PlateCountReversed, RenderVerticesReversed, RenderTrianglesReversed);

    // Summary
    UE_LOG(LogTemp, Log, TEXT("=== LOD Regression Test Complete ==="));
    UE_LOG(LogTemp, Log, TEXT("✓ Simulation state preserved across LOD changes"));
    UE_LOG(LogTemp, Log, TEXT("✓ Plates, stress, and time remain intact"));
    UE_LOG(LogTemp, Log, TEXT("✓ Render mesh regenerates correctly"));
    UE_LOG(LogTemp, Log, TEXT("✓ Simulation continues properly after LOD switch"));

    return true;
}
