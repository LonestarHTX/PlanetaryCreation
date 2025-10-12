// Milestone 6: Heightmap Visualization Test
// Generates color-coded PNG showing elevation gradient

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "TectonicSimulationController.h"
#include "TectonicSimulationService.h"
#include "Tests/PlanetaryCreationAutomationGPU.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeightmapVisualizationTest,
    "PlanetaryCreation.Milestone6.HeightmapVisualization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHeightmapVisualizationTest::RunTest(const FString& Parameters)
{
    using namespace PlanetaryCreation::Automation;

    // Get simulation service
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
        return false;

    // Force CPU amplification to avoid GPU spikes during local automation runs.
    FScopedGPUAmplificationOverride ForceCPUAmplification(0);

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

    const FString OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PlanetaryCreation/Heightmaps"));

    if (!OutputPath.IsEmpty())
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Heightmap visualization exported to: %s"), *OutputPath);
        AddInfo(FString::Printf(TEXT("Heightmap exported to: %s"), *OutputPath));

        // Clean up artifact to avoid polluting subsequent runs.
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        TestTrue(TEXT("Heightmap export cleanup succeeded"), PlatformFile.DeleteFile(*OutputPath));
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

    // Negative test: invalid dimensions should fail gracefully.
    {
        AddExpectedError(TEXT("Cannot export heightmap: Invalid dimensions"), EAutomationExpectedErrorFlags::Contains, 1);
        const FString InvalidExport = Service->ExportHeightmapVisualization(0, 1024);
        TestTrue(TEXT("Export fails for invalid image width"), InvalidExport.IsEmpty());
    }

    // Negative test: simulate image wrapper module failure via test override.
    {
        AddExpectedError(TEXT("Image wrapper module forced offline"), EAutomationExpectedErrorFlags::Contains, 1);
        Service->SetHeightmapExportTestOverrides(true);
        const FString ForcedFailure = Service->ExportHeightmapVisualization(1024, 512);
        TestTrue(TEXT("Export fails when module load is forced to fail"), ForcedFailure.IsEmpty());
        Service->SetHeightmapExportTestOverrides(false);
    }

    // Negative test: unwritable target file should cause export failure.
    {
        const FString LockedPath = FPaths::Combine(OutputDirectory, TEXT("Heightmap_Visualization.png"));
        IFileManager::Get().MakeDirectory(*OutputDirectory, true);
        FFileHelper::SaveStringToFile(TEXT("locked"), *LockedPath);

        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        PlatformFile.SetReadOnly(*LockedPath, true);

        AddExpectedError(TEXT("Failed to overwrite heightmap"), EAutomationExpectedErrorFlags::Contains, 1);
        const FString LockedResult = Service->ExportHeightmapVisualization(2048, 1024);
        TestTrue(TEXT("Export fails when output file is read-only"), LockedResult.IsEmpty());

        PlatformFile.SetReadOnly(*LockedPath, false);
        PlatformFile.DeleteFile(*LockedPath);
    }

    // Negative test: invalid output directory should fail gracefully.
    {
        Service->SetHeightmapExportTestOverrides(false, false, TEXT("Z:/NonExistent/PlanetaryCreation"));
        AddExpectedError(TEXT("Failed to create output directory"), EAutomationExpectedErrorFlags::Contains, 1);
        const FString InvalidPathResult = Service->ExportHeightmapVisualization(1024, 512);
        TestTrue(TEXT("Export fails when override directory cannot be created"), InvalidPathResult.IsEmpty());
        Service->SetHeightmapExportTestOverrides(false, false, FString());
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeightmapSeamContinuityTest,
    "PlanetaryCreation.Milestone6.HeightmapSeamContinuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHeightmapSeamContinuityTest::RunTest(const FString& Parameters)
{
    using namespace PlanetaryCreation::Automation;

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("Simulation service must exist"), Service);
    if (!Service)
    {
        return false;
    }

    FScopedGPUAmplificationOverride ForceCPUAmplification(0);

    FTectonicSimulationParameters Params;
    Params.Seed = 1337;
    Params.RenderSubdivisionLevel = 5;
    Params.bEnableOceanicAmplification = true;
    Params.bEnableContinentalAmplification = true;
    Params.MinAmplificationLOD = 5;

    Service->SetHighlightSeaLevel(false);
    Service->SetParameters(Params);
    Service->AdvanceSteps(8);

    const FString OutputPath = Service->ExportHeightmapVisualization(1024, 512);
    TestTrue(TEXT("Export should succeed"), !OutputPath.IsEmpty());
    TestTrue(TEXT("Heightmap file exists"), FPaths::FileExists(OutputPath));

    const FHeightmapExportMetrics& Metrics = Service->GetLastHeightmapExportMetrics();
    TestTrue(TEXT("Heightmap export metrics should be valid"), Metrics.bValid);
    if (Metrics.bValid)
    {
        TestEqual(TEXT("Export width matches request"), Metrics.Width, 1024);
        TestEqual(TEXT("Export height matches request"), Metrics.Height, 512);
        TestTrue(TEXT("Pixel count matches width*height"),
            Metrics.PixelCount == static_cast<int64>(Metrics.Width) * static_cast<int64>(Metrics.Height));

        TestTrue(TEXT("Heightmap coverage should be 100%"),
            FMath::IsNearlyEqual(Metrics.CoveragePercent, 100.0, 1.0e-3));
        TestEqual(TEXT("No seam rows should fail sampling"), Metrics.SeamRowsWithFailures, 0);
        TestTrue(TEXT("Seam metrics should cover at least one row"), Metrics.SeamRowsEvaluated > 0);
        TestTrue(TEXT("Timing metrics captured"), Metrics.TotalMs >= 0.0 && Metrics.SamplingMs >= 0.0);

        const TArray<FHeightmapExportPerformanceSample>& PerfHistory = Service->GetHeightmapExportPerformanceHistory();
        TestTrue(TEXT("Performance history retains last sample"), PerfHistory.Num() > 0);
        if (PerfHistory.Num() > 0)
        {
            const FHeightmapExportPerformanceSample& LastSample = PerfHistory.Last();
            TestEqual(TEXT("History width matches metrics"), LastSample.Width, Metrics.Width);
            TestEqual(TEXT("History height matches metrics"), LastSample.Height, Metrics.Height);
            TestTrue(TEXT("History total ms matches metrics"),
                FMath::IsNearlyEqual(LastSample.TotalMs, Metrics.TotalMs, 1.0e-3));
        }

        if (Metrics.bSamplerUsedAmplified)
        {
            TestTrue(TEXT("Stage B seam max delta under 1 m"), Metrics.SeamMaxAbsDelta < 1.0);
        }
        else
        {
            AddInfo(TEXT("Stage B amplification inactive; seam delta threshold skipped (baseline export)."));
        }
    }

    if (!OutputPath.IsEmpty())
    {
        IFileManager::Get().Delete(*OutputPath, false, true, true);
    }

    return true;
}
