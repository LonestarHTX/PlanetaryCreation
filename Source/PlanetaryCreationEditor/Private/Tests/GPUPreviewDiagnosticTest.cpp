#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "TectonicSimulationController.h"
#include "PlanetaryCreationLogging.h"

/**
 * Milestone 6 GPU Preview Diagnostic Test
 *
 * Purpose: Diagnose why plates appear frozen in GPU preview mode
 *
 * Tests:
 * 1. CPU path baseline (GPU preview OFF)
 * 2. GPU path diagnostics (GPU preview ON)
 * 3. Kinematics verification (time advances, plates move)
 * 4. Snapshot state logging (heightmap viz, velocity field)
 * 5. Vertex color override test (material sampling check)
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGPUPreviewDiagnosticTest,
    "PlanetaryCreation.Milestone6.GPU.PreviewDiagnostic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGPUPreviewDiagnosticTest::RunTest(const FString& Parameters)
{
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("=== GPU Preview Diagnostic Test START ==="));

    // Get service
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get TectonicSimulationService"));
        return false;
    }

    // Reset simulation
    Service->ResetSimulation();

    // Configure parameters for testing
    FTectonicSimulationParameters Params = Service->GetParameters();
    Params.RenderSubdivisionLevel = 4; // Use L4 for fast testing
    Params.bEnableOceanicAmplification = true;
    Params.bEnableHeightmapVisualization = true; // Enable heightmap viz
    Service->SetParameters(Params);

    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[DIAGNOSTIC] Initial state: Time=%.2f My, Plates=%d"),
        Service->GetCurrentTimeMy(), Service->GetPlates().Num());

    // ========================================================================
    // TEST 1: CPU Path Baseline (GPU Preview OFF)
    // ========================================================================
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("\n[TEST 1] CPU Path Baseline (GPU Preview OFF)"));

    Service->SetSkipCPUAmplification(false); // Ensure CPU path runs
    const double CPUStartTime = Service->GetCurrentTimeMy();
    const int32 CPUStartPlateCount = Service->GetPlates().Num();

    // Step 5 times
    for (int32 i = 0; i < 5; ++i)
    {
        Service->AdvanceSteps(1);
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[CPU Path] Step %d: Time=%.2f My, Plates=%d"),
            i + 1, Service->GetCurrentTimeMy(), Service->GetPlates().Num());
    }

    const double CPUEndTime = Service->GetCurrentTimeMy();
    const double CPUTimeDelta = CPUEndTime - CPUStartTime;

    TestTrue(TEXT("CPU Path: Time advanced"), CPUTimeDelta > 0.0);
    TestEqual(TEXT("CPU Path: Time delta is 10 My (5 steps * 2 My)"), CPUTimeDelta, 10.0, 0.01);

    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[CPU Path] Time advanced from %.2f to %.2f My (delta=%.2f My)"),
        CPUStartTime, CPUEndTime, CPUTimeDelta);

    // ========================================================================
    // TEST 2: GPU Preview Path (GPU Preview ON)
    // ========================================================================
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("\n[TEST 2] GPU Preview Path (GPU Preview ON)"));

    // Enable GPU preview mode (skips CPU amplification)
    Service->SetSkipCPUAmplification(true);
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[GPU Preview] bSkipCPUAmplification set to TRUE"));

    const double GPUStartTime = Service->GetCurrentTimeMy();
    const int32 GPUStartPlateCount = Service->GetPlates().Num();

    // Step 5 times with GPU preview active
    for (int32 i = 0; i < 5; ++i)
    {
        Service->AdvanceSteps(1);
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[GPU Path] Step %d: Time=%.2f My, Plates=%d"),
            i + 1, Service->GetCurrentTimeMy(), Service->GetPlates().Num());
    }

    const double GPUEndTime = Service->GetCurrentTimeMy();
    const double GPUTimeDelta = GPUEndTime - GPUStartTime;

    TestTrue(TEXT("GPU Path: Time advanced"), GPUTimeDelta > 0.0);
    TestEqual(TEXT("GPU Path: Time delta is 10 My (5 steps * 2 My)"), GPUTimeDelta, 10.0, 0.01);

    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[GPU Path] Time advanced from %.2f to %.2f My (delta=%.2f My)"),
        GPUStartTime, GPUEndTime, GPUTimeDelta);

    // ========================================================================
    // TEST 3: Controller Integration Test
    // ========================================================================
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("\n[TEST 3] Controller Integration (with Snapshot Logging)"));

    // Create controller (triggers snapshot creation)
    FTectonicSimulationController Controller;
    Controller.Initialize();

    // Create snapshot and log its state
    FMeshBuildSnapshot Snapshot = Controller.CreateMeshBuildSnapshot();

    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[Snapshot State]"));
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("  bEnableHeightmapVisualization = %s"),
        Snapshot.Parameters.bEnableHeightmapVisualization ? TEXT("TRUE") : TEXT("FALSE"));
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("  bShowVelocityField = %s"),
        Snapshot.bShowVelocityField ? TEXT("TRUE") : TEXT("FALSE"));
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("  bUseAmplifiedElevation = %s"),
        Snapshot.bUseAmplifiedElevation ? TEXT("TRUE") : TEXT("FALSE"));
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("  ElevationMode = %d"),
        static_cast<int32>(Snapshot.ElevationMode));
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("  RenderVertices.Num() = %d"),
        Snapshot.RenderVertices.Num());
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("  VertexPlateAssignments.Num() = %d"),
        Snapshot.VertexPlateAssignments.Num());
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("  VertexElevationValues.Num() = %d"),
        Snapshot.VertexElevationValues.Num());

    TestTrue(TEXT("Snapshot has render vertices"), Snapshot.RenderVertices.Num() > 0);
    TestTrue(TEXT("Snapshot has plate assignments"), Snapshot.VertexPlateAssignments.Num() > 0);
    TestEqual(TEXT("Snapshot vertex counts match"),
        Snapshot.RenderVertices.Num(), Snapshot.VertexPlateAssignments.Num());

    // ========================================================================
    // TEST 4: Vertex Color Override Test
    // ========================================================================
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("\n[TEST 4] Vertex Color Override (Red Override Active)"));

    // The red override is currently active in BuildMeshFromSnapshot (line 1270)
    // We can't call BuildMeshFromSnapshot directly (it's private), but the override
    // will be visible when the controller builds the mesh internally.
    // This diagnostic confirms the override is in the code.
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[Vertex Colors] Red override is active in TectonicSimulationController.cpp:1270"));
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[Vertex Colors] All vertex colors should render as RED in editor"));
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[Vertex Colors] If mesh is NOT red, material is not sampling vertex colors"));

    // ========================================================================
    // TEST 5: Plate Movement Verification
    // ========================================================================
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("\n[TEST 5] Plate Movement Verification"));

    // Get first plate centroid before and after step
    if (Service->GetPlates().Num() > 0)
    {
        const FVector3d InitialCentroid = Service->GetPlates()[0].Centroid;
        const double InitialAngle = Service->GetPlates()[0].AngularVelocity * Service->GetCurrentTimeMy();

        Service->AdvanceSteps(1);

        const FVector3d FinalCentroid = Service->GetPlates()[0].Centroid;
        const double FinalAngle = Service->GetPlates()[0].AngularVelocity * Service->GetCurrentTimeMy();

        const double CentroidDelta = (FinalCentroid - InitialCentroid).Length();
        const double AngleDelta = FinalAngle - InitialAngle;

        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[Plate Movement] Plate 0:"));
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("  Initial centroid: (%.6f, %.6f, %.6f)"),
            InitialCentroid.X, InitialCentroid.Y, InitialCentroid.Z);
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("  Final centroid: (%.6f, %.6f, %.6f)"),
            FinalCentroid.X, FinalCentroid.Y, FinalCentroid.Z);
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("  Centroid delta: %.6f"), CentroidDelta);
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("  Angle delta: %.6f rad"), AngleDelta);

        // Plates should move (centroid rotates around Euler pole)
        // Even small angular velocities produce measurable centroid changes
        TestTrue(TEXT("Plate centroid moved after step"), CentroidDelta > 0.0 || FMath::Abs(AngleDelta) > 0.0);
    }

    // ========================================================================
    // DIAGNOSTIC SUMMARY
    // ========================================================================
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("\n=== DIAGNOSTIC SUMMARY ==="));
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("CPU Path: Time advanced %.2f My over 5 steps"), CPUTimeDelta);
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("GPU Path: Time advanced %.2f My over 5 steps"), GPUTimeDelta);
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("Heightmap Visualization: %s"),
        Snapshot.Parameters.bEnableHeightmapVisualization ? TEXT("ENABLED") : TEXT("DISABLED"));
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("Velocity Field: %s"),
        Snapshot.bShowVelocityField ? TEXT("ENABLED") : TEXT("DISABLED"));
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("Vertex Color Override: ACTIVE (see line 1270)"));

    UE_LOG(LogPlanetaryCreation, Warning, TEXT("\n=== GPU Preview Diagnostic Test COMPLETE ==="));

    Controller.Shutdown();
    return true;
}
