// Milestone 4 Task 1.1 Phase 1: Re-tessellation POC Test

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 4 Task 1.1 Phase 1: Re-tessellation POC Validation
 *
 * Tests snapshot/restore/validate functions with forced rebuild.
 * POC uses full mesh rebuild to prove infrastructure works before
 * implementing incremental boundary fan split in Phase 2.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRetessellationPOCTest,
    "PlanetaryCreation.Milestone4.RetessellationPOC",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRetessellationPOCTest::RunTest(const FString& Parameters)
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
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Re-tessellation POC Test ==="));

    // Test 1: Snapshot/Restore
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: Snapshot/Restore"));

    FTectonicSimulationParameters Params;
    Params.Seed = 12345;
    Params.SubdivisionLevel = 0; // 20 plates
    Params.RenderSubdivisionLevel = 2; // 320 faces
    Params.LloydIterations = 0; // Skip for speed
    Service->SetParameters(Params);

    const int32 OriginalVertexCount = Service->GetRenderVertices().Num();
    const int32 OriginalTriangleCount = Service->GetRenderTriangles().Num() / 3;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Original: %d vertices, %d triangles"), OriginalVertexCount, OriginalTriangleCount);

    // Capture snapshot
    auto Snapshot = Service->CaptureRetessellationSnapshot();
    TestEqual(TEXT("Snapshot vertex count"), Snapshot.RenderVertices.Num(), OriginalVertexCount);
    TestEqual(TEXT("Snapshot triangle count"), Snapshot.RenderTriangles.Num() / 3, OriginalTriangleCount);

    // Modify state (advance simulation)
    Service->AdvanceSteps(5);
    const int32 ModifiedVertexCount = Service->GetRenderVertices().Num();

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  After 5 steps: %d vertices"), ModifiedVertexCount);

    // Restore snapshot
    Service->RestoreRetessellationSnapshot(Snapshot);
    const int32 RestoredVertexCount = Service->GetRenderVertices().Num();

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  After restore: %d vertices"), RestoredVertexCount);

    TestEqual(TEXT("Restored vertex count matches original"), RestoredVertexCount, OriginalVertexCount);

    // Test 2: Validation (should pass for valid mesh)
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 2: Validation (Clean Mesh)"));

    Service->SetParameters(Params); // Reset to clean state
    auto CleanSnapshot = Service->CaptureRetessellationSnapshot();

    const bool ValidationResult = Service->ValidateRetessellation(CleanSnapshot);
    TestTrue(TEXT("Validation passes for clean mesh"), ValidationResult);

    // Test 3: Re-tessellation with real drift detection
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: Re-tessellation (Drift Detection)"));

    Service->SetParameters(Params); // Reset

    // First check: no drift initially (threshold = 30°, no steps taken)
    const bool NoDriftRebuild = Service->PerformRetessellation();
    TestTrue(TEXT("Re-tessellation succeeds when no drift"), NoDriftRebuild);
    TestEqual(TEXT("No rebuild when plates haven't drifted"), Service->RetessellationCount, 0);

    // Advance simulation to cause drift
    Service->AdvanceSteps(10); // 20 My should cause some drift

    const int32 PreRebuildVertexCount = Service->GetRenderVertices().Num();

    // Lower threshold to 1° to force rebuild
    Params.RetessellationThresholdDegrees = 1.0;
    Service->SetParameters(Params);
    Service->AdvanceSteps(10); // Another 20 My

    const bool RebuildSuccess = Service->PerformRetessellation();
    TestTrue(TEXT("Re-tessellation succeeds after drift"), RebuildSuccess);

    const int32 PostRebuildVertexCount = Service->GetRenderVertices().Num();
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Pre-rebuild: %d vertices"), PreRebuildVertexCount);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Post-rebuild: %d vertices"), PostRebuildVertexCount);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Rebuild time: %.2f ms"), Service->LastRetessellationTimeMs);

    // Full rebuild preserves vertex count
    TestEqual(TEXT("Vertex count preserved after rebuild"), PostRebuildVertexCount, PreRebuildVertexCount);

    // Test 4: Performance logging
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 4: Performance Logging"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Rebuild count: %d"), Service->RetessellationCount);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Last rebuild time: %.2f ms"), Service->LastRetessellationTimeMs);

    TestTrue(TEXT("Rebuild time logged"), Service->LastRetessellationTimeMs > 0.0);
    TestTrue(TEXT("Rebuild count incremented"), Service->RetessellationCount >= 1);

    // Test 5: No-rebuild case (high threshold)
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 5: No-Rebuild Case (High Threshold)"));

    Params.RetessellationThresholdDegrees = 90.0; // Very high threshold
    Service->SetParameters(Params);
    Service->AdvanceSteps(5);

    const int32 PreNoRebuildCount = Service->RetessellationCount;
    const bool NoRebuildSuccess = Service->PerformRetessellation();
    TestTrue(TEXT("Re-tessellation succeeds with high threshold"), NoRebuildSuccess);
    TestEqual(TEXT("Rebuild count unchanged when no drift"), Service->RetessellationCount, PreNoRebuildCount);

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Rebuild avoided (no plates drifted beyond 90°)"));

    AddInfo(TEXT("✅ Re-tessellation POC test complete"));
    AddInfo(FString::Printf(TEXT("Snapshot/Restore: Working | Validation: Passing | Rebuild: %.2f ms"),
        Service->LastRetessellationTimeMs));

    return true;
}
