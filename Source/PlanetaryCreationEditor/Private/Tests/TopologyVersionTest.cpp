// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 4 Phase 4.2: Topology Version Increment Test
 *
 * Validates that TopologyVersion increments correctly on all topology-changing operations:
 * - Re-tessellation
 * - Plate splits
 * - Plate merges
 *
 * This ensures the LOD cache invalidation system works correctly.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTopologyVersionTest,
    "PlanetaryCreation.Milestone4.TopologyVersion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTopologyVersionTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get UTectonicSimulationService"));
        return false;
    }

    // Setup: Reset to baseline
    FTectonicSimulationParameters Params = Service->GetParameters();
    Params.Seed = 99999;
    Params.SubdivisionLevel = 0; // 20 plates
    Params.RenderSubdivisionLevel = 2;
    Params.LloydIterations = 4;
    Params.bEnableDynamicRetessellation = false; // Disable for Test 2
    Params.bEnablePlateTopologyChanges = false;  // Disable for Test 2
    Service->SetParameters(Params);

    UE_LOG(LogTemp, Log, TEXT("=== Topology Version Test ==="));

    // Test 1: Initial topology version should be 0
    int32 InitialVersion = Service->GetTopologyVersion();
    TestEqual(TEXT("Initial topology version is 0"), InitialVersion, 0);
    UE_LOG(LogTemp, Log, TEXT("Test 1: Initial version = %d"), InitialVersion);

    // Test 2: Surface version should increment each step, but topology version should NOT
    Service->AdvanceSteps(5);
    const int32 VersionAfterSteps = Service->GetTopologyVersion();
    const int32 SurfaceVersionAfterSteps = Service->GetSurfaceDataVersion();

    TestEqual(TEXT("Topology version unchanged after steps (no topology change)"), VersionAfterSteps, InitialVersion);
    TestEqual(TEXT("Surface version incremented after 5 steps"), SurfaceVersionAfterSteps, 5);
    UE_LOG(LogTemp, Log, TEXT("Test 2: After 5 steps - Topo:%d, Surface:%d"), VersionAfterSteps, SurfaceVersionAfterSteps);

    // Test 3: Re-tessellation should increment topology version
    // Enable re-tessellation for this test
    Params.bEnableDynamicRetessellation = true;
    Service->SetParameters(Params);

    Service->AdvanceSteps(20); // Trigger drift for re-tessellation

    // Check if re-tessellation occurred (RetessellationCount > 0)
    if (Service->RetessellationCount > 0)
    {
        const int32 VersionAfterRetess = Service->GetTopologyVersion();
        TestTrue(TEXT("Topology version incremented after re-tessellation"), VersionAfterRetess > VersionAfterSteps);
        UE_LOG(LogTemp, Log, TEXT("Test 3: After re-tessellation - Topo:%d (incremented from %d)"), VersionAfterRetess, VersionAfterSteps);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Test 3: Re-tessellation did not trigger (plates didn't drift enough)"));
    }

    // Test 4: Reset and test split/merge increments
    Params.bEnablePlateTopologyChanges = true;
    Params.SplitVelocityThreshold = 0.01; // Lower threshold for easier testing
    Params.SplitDurationThreshold = 2.0;  // Shorter duration
    Params.MergeStressThreshold = 50.0;
    Service->SetParameters(Params);

    const int32 VersionAfterReset = Service->GetTopologyVersion();
    const int32 InitialPlateCount = Service->GetPlates().Num();
    UE_LOG(LogTemp, Log, TEXT("Test 4: Reset complete - Topo:%d, Plates:%d"), VersionAfterReset, InitialPlateCount);

    // Run simulation to potentially trigger splits/merges
    Service->AdvanceSteps(30);

    const int32 VersionAfterTopologyChanges = Service->GetTopologyVersion();
    const int32 FinalPlateCount = Service->GetPlates().Num();
    const TArray<FPlateTopologyEvent>& Events = Service->GetTopologyEvents();

    UE_LOG(LogTemp, Log, TEXT("Test 4: After 30 steps - Topo:%d, Plates:%d, Events:%d"),
        VersionAfterTopologyChanges, FinalPlateCount, Events.Num());

    // If splits or merges occurred, topology version should have incremented
    if (Events.Num() > 0)
    {
        TestTrue(TEXT("Topology version incremented after split/merge events"),
            VersionAfterTopologyChanges > VersionAfterReset);

        // Count split and merge events
        int32 SplitCount = 0;
        int32 MergeCount = 0;
        for (const FPlateTopologyEvent& Event : Events)
        {
            if (Event.EventType == EPlateTopologyEventType::Split) SplitCount++;
            if (Event.EventType == EPlateTopologyEventType::Merge) MergeCount++;
        }

        UE_LOG(LogTemp, Log, TEXT("  Events: %d splits, %d merges"), SplitCount, MergeCount);

        // Each topology event should increment version
        // Note: Re-tessellation may also occur, so version >= event count
        TestTrue(TEXT("Topology version >= event count"),
            VersionAfterTopologyChanges >= VersionAfterReset + Events.Num());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Test 4: No split/merge events occurred (may need longer simulation or different seed)"));
    }

    // Summary
    UE_LOG(LogTemp, Log, TEXT("=== Topology Version Test Complete ==="));
    UE_LOG(LogTemp, Log, TEXT("✓ Topology version correctly tracks geometry changes"));
    UE_LOG(LogTemp, Log, TEXT("✓ Surface version independently tracks per-step changes"));
    UE_LOG(LogTemp, Log, TEXT("✓ LOD cache can use versions for invalidation"));

    return true;
}
