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
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("Simulation service must exist"), Service);
    if (!Service)
    {
        return false;
    }

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

    bool bResult = true;

    if (!OutputPath.IsEmpty())
    {
        TArray<uint8> FileData;
        if (FFileHelper::LoadFileToArray(FileData, *OutputPath))
        {
            IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
            TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
            if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
            {
                TArray<uint8> Raw;
                if (ImageWrapper->GetRaw(ERGBFormat::RGBA, 8, Raw))
                {
                    const int32 Width = ImageWrapper->GetWidth();
                    const int32 Height = ImageWrapper->GetHeight();

                    int64 LeftAlphaSum = 0;
                    int64 RightAlphaSum = 0;
                    for (int32 Y = 0; Y < Height; ++Y)
                    {
                        const int32 LeftIdx = (Y * Width) * 4;
                        const int32 RightIdx = (Y * Width + (Width - 1)) * 4;

                        const uint8 AlphaLeft = Raw[LeftIdx + 3];
                        const uint8 AlphaRight = Raw[RightIdx + 3];

                        LeftAlphaSum += AlphaLeft;
                        RightAlphaSum += AlphaRight;
                    }

                    bResult = (LeftAlphaSum > 0) && (RightAlphaSum > 0);
                    if (!bResult)
                    {
                        AddError(TEXT("Seam continuity failed: one of the seam columns is entirely empty"));
                    }
                }
            }
        }

        IFileManager::Get().Delete(*OutputPath, false, true, true);
    }

    return bResult;
}
