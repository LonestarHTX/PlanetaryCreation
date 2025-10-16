#include "Misc/AutomationTest.h"
#include "Simulation/TectonicSimulationService.h"
#include "StageB/OceanicAmplificationGPU.h"
#include "Editor.h"
#include "RHI.h"
#include "Tests/PlanetaryCreationAutomationGPU.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGPUPreviewSeamMirroringTest,
    "PlanetaryCreation.Milestone6.GPU.PreviewSeamMirroring",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGPUPreviewSeamMirroringTest::RunTest(const FString& Parameters)
{
    using namespace PlanetaryCreation::Automation;
    if (!ShouldRunGPUAmplificationAutomation(*this, TEXT("GPU.PreviewSeamMirroring")))
    {
        return true;
    }

    if (!GDynamicRHI || FCString::Stricmp(GDynamicRHI->GetName(), TEXT("NullDrv")) == 0)
    {
        AddInfo(TEXT("Skipping GPU preview seam mirroring test (NullRHI detected)."));
        return true;
    }

    FScopedStageBThrottleGuard StageBThrottleGuard(*this, 50.0f);
    if (StageBThrottleGuard.ShouldSkipTest())
    {
        return true;
    }

    UTectonicSimulationService* Service = GEditor ? GEditor->GetEditorSubsystem<UTectonicSimulationService>() : nullptr;
    if (!Service)
    {
        AddError(TEXT("Failed to acquire UTectonicSimulationService."));
        return false;
    }

    Service->ResetSimulation();

    FTectonicSimulationParameters Params = Service->GetParameters();
    Params.RenderSubdivisionLevel = 7;
    Params.bEnableDynamicRetessellation = false;
    Params.bEnableOceanicAmplification = true;
    Params.VisualizationMode = ETectonicVisualizationMode::Elevation;
    Service->SetParameters(Params);

    // Ensure render mesh reflects the configured LOD.
    Service->SetRenderSubdivisionLevel(Params.RenderSubdivisionLevel);

    FTextureRHIRef HeightTexture;
    int32 LeftCoverage = 0;
    int32 RightCoverage = 0;
    int32 MirroredCoverage = 0;

    const bool bPreviewWritten = PlanetaryCreation::GPU::ApplyOceanicAmplificationGPUPreview(
        *Service,
        HeightTexture,
        FIntPoint(2048, 1024),
        &LeftCoverage,
        &RightCoverage,
        &MirroredCoverage);

    TestTrue(TEXT("GPU preview height texture written"), bPreviewWritten);
    TestTrue(TEXT("Seam left coverage present"), LeftCoverage > 0);
    TestTrue(TEXT("Seam right coverage present"), RightCoverage > 0);

    const int32 CoverageDelta = FMath::Abs(LeftCoverage - RightCoverage);
    TestTrue(
        TEXT("Seam coverage difference within tolerance"),
        CoverageDelta <= 512);

    AddInfo(FString::Printf(TEXT("Seam coverage: Left=%d Right=%d Mirrored=%d Δ=%d"),
        LeftCoverage, RightCoverage, MirroredCoverage, CoverageDelta));

    return true;
}
