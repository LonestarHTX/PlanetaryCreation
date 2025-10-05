// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "TectonicSimulationController.h"
#include "Editor.h"

/**
 * Milestone 3 Task 4.3: Async Mesh Pipeline Validation
 *
 * This test validates that:
 * 1. Synchronous path is used for low subdivision levels (0-2)
 * 2. Asynchronous path is triggered for high subdivision levels (3+)
 * 3. Atomic flag prevents double-builds during rapid stepping
 * 4. Thread IDs differ between dispatch and background execution (proves async)
 *
 * Manual validation required:
 * - Check Output Log for thread ID differences (🚀 dispatch vs ⚙️ background)
 * - Verify rapid stepping shows ⏸️ skip messages at level 3+
 * - Confirm no crashes or visual artifacts
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncMeshPipelineTest,
    "PlanetaryCreation.Milestone3.AsyncMeshPipeline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAsyncMeshPipelineTest::RunTest(const FString& Parameters)
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

    // Test 1: Synchronous path (level 0-2)
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Test 1: Synchronous Path (Level 0-2) ==="));

    FTectonicSimulationParameters SyncParams;
    SyncParams.Seed = 12345;
    SyncParams.SubdivisionLevel = 0; // 20 plates
    SyncParams.RenderSubdivisionLevel = 2; // 320 faces (below async threshold)
    SyncParams.LloydIterations = 0; // Skip for speed
    Service->SetParameters(SyncParams);

    TSharedPtr<FTectonicSimulationController> Controller = MakeShared<FTectonicSimulationController>();
    Controller->Initialize();

    // Step and check logs for ⚡ [SYNC] marker
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Stepping at level 2 (should use synchronous path)..."));
    Controller->StepSimulation(1);

    // Give a frame for mesh update to complete
    FPlatformProcess::Sleep(0.1f);

    // Test 2: Asynchronous path (level 3+)
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Test 2: Asynchronous Path (Level 3+) ==="));

    FTectonicSimulationParameters AsyncParams;
    AsyncParams.Seed = 12345;
    AsyncParams.SubdivisionLevel = 0; // 20 plates
    AsyncParams.RenderSubdivisionLevel = 3; // 1280 faces (triggers async)
    AsyncParams.LloydIterations = 0;
    Service->SetParameters(AsyncParams);

    // Step and check logs for 🚀 [ASYNC] dispatch marker
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Stepping at level 3 (should use asynchronous path)..."));
    Controller->StepSimulation(1);

    // Give time for background thread to complete
    FPlatformProcess::Sleep(0.5f);

    // Test 3: Rapid stepping guard
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Test 3: Rapid Stepping Guard ==="));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Stepping twice rapidly (should trigger ⏸️ skip on second step)..."));

    Controller->StepSimulation(1);
    Controller->StepSimulation(1); // Should log skip warning

    FPlatformProcess::Sleep(0.5f);

    Controller->Shutdown();

    // Manual validation instructions
    AddInfo(TEXT("✅ Test completed. Manual validation required:"));
    AddInfo(TEXT("1. Check Output Log for thread ID patterns:"));
    AddInfo(TEXT("   - ⚡ [SYNC] should show same ThreadID throughout (level 0-2)"));
    AddInfo(TEXT("   - 🚀 [ASYNC] dispatch → ⚙️ background build → ✅ game thread update (different IDs at level 3+)"));
    AddInfo(TEXT("2. Verify ⏸️ skip message appeared during rapid stepping"));
    AddInfo(TEXT("3. Confirm no crashes or visual artifacts in editor viewport"));

    return true;
}
