#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorPromotionCommon.h"

#include "Engine/Engine.h"
#include "Editor.h"
#include "HAL/IConsoleManager.h"

#include "Simulation/TectonicSimulationController.h"
#include "Simulation/TectonicSimulationService.h"
#include "Tests/PlanetaryCreationAutomationGPU.h"

namespace StageBVisualization
{
struct FState
{
    UTectonicSimulationService* Service = nullptr;
    FTectonicSimulationController* Controller = nullptr;
    FTectonicSimulationParameters OriginalParameters;
    int32 OriginalStageBProfiling = 0;
    int32 OriginalUseGPUAmplification = 0;
};

static FState GState;

class FSetVisualizationModeCommand : public IAutomationLatentCommand
{
public:
    explicit FSetVisualizationModeCommand(int32 InModeValue)
        : ModeValue(InModeValue)
    {
    }

    virtual bool Update() override
    {
        if (FTectonicSimulationController* Controller = GState.Controller)
        {
            Controller->SetVisualizationMode(static_cast<ETectonicVisualizationMode>(ModeValue));
            Controller->RefreshPreviewColors();
        }
        else if (UTectonicSimulationService* Service = GState.Service)
        {
            Service->SetVisualizationMode(static_cast<ETectonicVisualizationMode>(ModeValue));
        }
        return true;
    }

private:
    int32 ModeValue;
};

class FTakeStageBScreenshotCommand : public IAutomationLatentCommand
{
public:
    explicit FTakeStageBScreenshotCommand(const FString& InName)
        : ScreenshotName(InName)
    {
    }

    virtual bool Update() override
    {
        FAutomationScreenshotOptions Options;
        Options.Resolution = FVector2D(3840.0f, 2160.0f);
        Options.bDisableNoisyRenderingFeatures = true;
        Options.bDisableTonemapping = true;
        Options.bIgnoreAntiAliasing = true;

        FEditorPromotionTestUtilities::TakeScreenshot(ScreenshotName, Options, true);
        return true;
    }

private:
    FString ScreenshotName;
};

class FStageBVisualizationCleanupCommand : public IAutomationLatentCommand
{
public:
    virtual bool Update() override
    {
        if (GState.Controller)
        {
            GState.Controller->SetGPUPreviewMode(false);
            GState.Controller->Shutdown();
            delete GState.Controller;
            GState.Controller = nullptr;
        }

        if (UTectonicSimulationService* Service = GState.Service)
        {
            Service->SetParameters(GState.OriginalParameters);
            Service->ResetSimulation();
            GState.Service = nullptr;
        }

        if (IConsoleVariable* StageBVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.StageBProfiling")))
        {
            StageBVar->Set(GState.OriginalStageBProfiling, ECVF_SetByCode);
        }
        if (IConsoleVariable* GPUVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.UseGPUAmplification")))
        {
            GPUVar->Set(GState.OriginalUseGPUAmplification, ECVF_SetByCode);
        }

        return true;
    }
};

static TArray<TPair<ETectonicVisualizationMode, FString>> BuildModeList()
{
    return {
        { ETectonicVisualizationMode::PlateColors, TEXT("PlateColors") },
        { ETectonicVisualizationMode::Elevation, TEXT("Elevation") },
        { ETectonicVisualizationMode::Stress, TEXT("Stress") },
        { ETectonicVisualizationMode::Amplified, TEXT("AmplifiedStageB") },
        { ETectonicVisualizationMode::AmplificationBlend, TEXT("AmplificationBlend") }
    };
}
} // namespace StageBVisualization

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStageBVisualizationCaptureTest,
    "PlanetaryCreation.Milestone6.StageBScreenshots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStageBVisualizationCaptureTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    using namespace PlanetaryCreation::Automation;
    if (!ShouldRunGPUAmplificationAutomation(*this, TEXT("StageBScreenshots")))
    {
        return true;
    }

    if (!GDynamicRHI || FCString::Stricmp(GDynamicRHI->GetName(), TEXT("NullDrv")) == 0)
    {
       AddWarning(TEXT("Skipping Stage B screenshot capture on NullRHI."));
       return true;
    }

    FScopedStageBThrottleGuard StageBThrottleGuard(*this, 50.0f);
    if (StageBThrottleGuard.ShouldSkipTest())
    {
        return true;
    }

    UTectonicSimulationService* Service = GEditor ? GEditor->GetEditorSubsystem<UTectonicSimulationService>() : nullptr;
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
    {
        return false;
    }

    using namespace StageBVisualization;

    GState.Service = Service;
    GState.OriginalParameters = Service->GetParameters();

    if (IConsoleVariable* StageBVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.StageBProfiling")))
    {
        GState.OriginalStageBProfiling = StageBVar->GetInt();
        StageBVar->Set(0, ECVF_SetByCode);
    }
    if (IConsoleVariable* GPUVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.UseGPUAmplification")))
    {
        GState.OriginalUseGPUAmplification = GPUVar->GetInt();
        GPUVar->Set(1, ECVF_SetByCode);
    }

    FTectonicSimulationParameters Params = GState.OriginalParameters;
    Params.Seed = 12345;
    Params.SubdivisionLevel = 0;
    Params.RenderSubdivisionLevel = FMath::Max(Params.MinAmplificationLOD, 5);
    Params.bEnableAutomaticLOD = false;
    Params.bEnableOceanicAmplification = true;
    Params.bEnableContinentalAmplification = true;
    Params.bEnableHydraulicErosion = true;
    Params.bEnableSedimentTransport = true;
    Params.bEnableOceanicDampening = true;
    Params.bSkipCPUAmplification = true;
    Params.VisualizationMode = ETectonicVisualizationMode::Amplified;

    Service->SetParameters(Params);
    Service->SetSkipCPUAmplification(true);
    Service->ResetSimulation();

    // Advance to geologically meaningful time (100 My) for mountain building
    const int32 TargetSteps = 50;  // 50 steps × 2 My/step = 100 My total
    Service->AdvanceSteps(TargetSteps);

    // Validate terrain features emerged before capturing screenshots
    const TArray<double>& Elevations = Service->GetVertexAmplifiedElevation();
    double MinElev = TNumericLimits<double>::Max();
    double MaxElev = TNumericLimits<double>::Lowest();
    int32 MountainVertices = 0;  // Elev > 1000m
    int32 DeepOceanVertices = 0; // Elev < -3000m

    for (double Elev : Elevations)
    {
        if (!FMath::IsFinite(Elev))
        {
            continue;
        }

        MinElev = FMath::Min(MinElev, Elev);
        MaxElev = FMath::Max(MaxElev, Elev);

        if (Elev > 1000.0) ++MountainVertices;
        if (Elev < -3000.0) ++DeepOceanVertices;
    }

    const double ElevationRange = MaxElev - MinElev;
    const double MountainPercent = (Elevations.Num() > 0) ? (double)MountainVertices / Elevations.Num() * 100.0 : 0.0;
    const double DeepOceanPercent = (Elevations.Num() > 0) ? (double)DeepOceanVertices / Elevations.Num() * 100.0 : 0.0;

    UE_LOG(LogTemp, Display, TEXT("=== Geological Features @ %d My ==="), TargetSteps * 2);
    UE_LOG(LogTemp, Display, TEXT("Elevation range: %.1f m to %.1f m (span: %.1f m)"),
        MinElev, MaxElev, ElevationRange);
    UE_LOG(LogTemp, Display, TEXT("Mountain vertices (>1000m): %d (%.1f%%)"),
        MountainVertices, MountainPercent);
    UE_LOG(LogTemp, Display, TEXT("Deep ocean vertices (<-3000m): %d (%.1f%%)"),
        DeepOceanVertices, DeepOceanPercent);

    TestTrue(TEXT("Mountain peaks exist (>1000m)"), MaxElev > 1000.0);
    TestTrue(TEXT("Ocean trenches exist (<-3000m)"), MinElev < -3000.0);
    TestTrue(TEXT("Elevation range >4km (mountains + ocean trenches)"), ElevationRange > 4000.0);

    FTectonicSimulationController* Controller = new FTectonicSimulationController();
    Controller->Initialize();
    Controller->SetPBRShadingEnabled(true);
    Controller->SetBoundariesVisible(false);
    Controller->SetGPUPreviewMode(true);
    Controller->ResetCamera();
    Controller->ZoomCamera(-2000.0f);
    Controller->RotateCamera(75.0f, -25.0f);
    GState.Controller = Controller;

    const TArray<TPair<ETectonicVisualizationMode, FString>> Modes = BuildModeList();
    for (const TPair<ETectonicVisualizationMode, FString>& ModePair : Modes)
    {
        const int32 ModeValue = static_cast<int32>(ModePair.Key);
        const FString ScreenshotName = FString::Printf(TEXT("Hypsometric_100My_%s"), *ModePair.Value);

        ADD_LATENT_AUTOMATION_COMMAND(FSetVisualizationModeCommand(ModeValue));
        ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FTakeStageBScreenshotCommand(ScreenshotName));
        ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.2f));
    }

    ADD_LATENT_AUTOMATION_COMMAND(FStageBVisualizationCleanupCommand());
    return true;
#else
    return false;
#endif
}
