#include "Utilities/PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "Simulation/TectonicSimulationService.h"
#include "Tests/AutomationEditorCommon.h"

/**
 * Test boundary classification with known Euler poles to prevent regression.
 * Sets up two plates with deterministic poles and verifies divergent/convergent classification.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTectonicBoundaryClassificationTest,
    "PlanetaryCreation.TectonicSimulation.BoundaryClassification",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTectonicBoundaryClassificationTest::RunTest(const FString& Parameters)
{
    // Get the simulation service
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("TectonicSimulationService should be available"), Service);
    if (!Service) return false;

    // Reset with known seed
    FTectonicSimulationParameters TestParams;
    TestParams.Seed = 12345;
    Service->SetParameters(TestParams);

    // Run a few steps to allow plate migration
    Service->AdvanceSteps(5);

    // Verify boundaries were classified
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries = Service->GetBoundaries();
    TestTrue(TEXT("Should have boundaries after initialization"), Boundaries.Num() > 0);

    // Count boundary types
    int32 DivergentCount = 0;
    int32 ConvergentCount = 0;
    int32 TransformCount = 0;

    for (const auto& BoundaryPair : Boundaries)
    {
        const FPlateBoundary& Boundary = BoundaryPair.Value;
        switch (Boundary.BoundaryType)
        {
        case EBoundaryType::Divergent:
            DivergentCount++;
            break;
        case EBoundaryType::Convergent:
            ConvergentCount++;
            break;
        case EBoundaryType::Transform:
            TransformCount++;
            break;
        }
    }

    // With 20 icosahedron plates, we should have 30 boundaries
    TestEqual(TEXT("Should have 30 boundaries (icosahedron topology)"), Boundaries.Num(), 30);

    // Verify we have a mix of boundary types (not all Transform due to bad normals)
    TestTrue(TEXT("Should have at least one divergent boundary"), DivergentCount > 0);
    TestTrue(TEXT("Should have at least one convergent boundary"), ConvergentCount > 0);

    // Log breakdown for debugging
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Boundary classification: %d divergent, %d convergent, %d transform"),
        DivergentCount, ConvergentCount, TransformCount);

    // Verify determinism: run again with same seed
    Service->SetParameters(TestParams);
    Service->AdvanceSteps(5);

    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries2 = Service->GetBoundaries();
    int32 DivergentCount2 = 0;
    int32 ConvergentCount2 = 0;

    for (const auto& BoundaryPair : Boundaries2)
    {
        const FPlateBoundary& Boundary = BoundaryPair.Value;
        if (Boundary.BoundaryType == EBoundaryType::Divergent) DivergentCount2++;
        if (Boundary.BoundaryType == EBoundaryType::Convergent) ConvergentCount2++;
    }

    TestEqual(TEXT("Divergent count should be deterministic"), DivergentCount, DivergentCount2);
    TestEqual(TEXT("Convergent count should be deterministic"), ConvergentCount, ConvergentCount2);

    return true;
}
