// Milestone 6: Heightmap Visualization Test
// Generates color-coded PNG showing elevation gradient

#include "Misc/AutomationTest.h"
#include "TectonicSimulationController.h"
#include "TectonicSimulationService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeightmapVisualizationTest,
    "PlanetaryCreation.Milestone6.HeightmapVisualization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHeightmapVisualizationTest::RunTest(const FString& Parameters)
{
    // Get simulation service
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
        return false;

    // Setup: Enable both oceanic and continental amplification for full detail
    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.SubdivisionLevel = 0; // 20 plates
    Params.RenderSubdivisionLevel = 5; // 10,242 vertices
    Params.bEnableOceanicDampening = true;
    Params.bEnableOceanicAmplification = true;
    Params.bEnableContinentalAmplification = true;
    Params.MinAmplificationLOD = 5;
    Service->SetParameters(Params);

    // Step simulation to generate terrain
    Service->AdvanceSteps(10); // 20 My

    // Export heightmap visualization
    const FString OutputPath = Service->ExportHeightmapVisualization(2048, 1024);

    // Verify export succeeded
    TestTrue(TEXT("Heightmap export path is not empty"), !OutputPath.IsEmpty());
    TestTrue(TEXT("Heightmap file exists"), FPaths::FileExists(OutputPath));

    if (!OutputPath.IsEmpty())
    {
        UE_LOG(LogTemp, Log, TEXT("✅ Heightmap visualization exported to: %s"), *OutputPath);
        AddInfo(FString::Printf(TEXT("Heightmap exported to: %s"), *OutputPath));
    }

    // Verify continental amplification enables amplified displacement even without oceanic amplification.
    {
        FTectonicSimulationParameters ContinentalOnlyParams = Params;
        ContinentalOnlyParams.bEnableOceanicAmplification = false;
        ContinentalOnlyParams.bEnableContinentalAmplification = true;
        Service->SetParameters(ContinentalOnlyParams);
        Service->AdvanceSteps(5);

        FTectonicSimulationController Controller;
        Controller.Initialize();

        const FMeshBuildSnapshot Snapshot = Controller.CreateMeshBuildSnapshot();
        TestTrue(TEXT("Continental-only amplification enables Stage B elevations"), Snapshot.bUseAmplifiedElevation);

        bool bFoundContinentalAmplification = false;
        for (int32 VertexIdx = 0; VertexIdx < Snapshot.RenderVertices.Num(); ++VertexIdx)
        {
            if (!Snapshot.VertexAmplifiedElevation.IsValidIndex(VertexIdx) ||
                !Snapshot.VertexElevationValues.IsValidIndex(VertexIdx))
            {
                continue;
            }

            const double Difference = FMath::Abs(Snapshot.VertexAmplifiedElevation[VertexIdx] - Snapshot.VertexElevationValues[VertexIdx]);
            if (Difference > 1.0)
            {
                bFoundContinentalAmplification = true;
                break;
            }
        }

        TestTrue(TEXT("At least one vertex shows continental Stage B displacement"), bFoundContinentalAmplification);

        Controller.Shutdown();

        // Restore combined amplification parameters for downstream tests.
        Service->SetParameters(Params);
        Service->AdvanceSteps(1);
    }

    return true;
}
