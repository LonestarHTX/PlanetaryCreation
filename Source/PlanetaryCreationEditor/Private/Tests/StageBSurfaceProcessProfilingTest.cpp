// Milestone 6 profiling helper: run Stage B with sediment and dampening enabled so StepTiming logs capture their cost.

#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#include "Simulation/TectonicSimulationService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStageBSurfaceProcessProfilingTest,
    "PlanetaryCreation.Milestone6.Perf.StageBSurfaceProcesses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStageBSurfaceProcessProfilingTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    UTectonicSimulationService* Service = GEditor ? GEditor->GetEditorSubsystem<UTectonicSimulationService>() : nullptr;
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
    {
        return false;
    }

    const FTectonicSimulationParameters OriginalParams = Service->GetParameters();

    FTectonicSimulationParameters ProfilingParams = OriginalParams;
    ProfilingParams.Seed = 12345;
    ProfilingParams.RenderSubdivisionLevel = FMath::Max(7, ProfilingParams.MinAmplificationLOD);
    ProfilingParams.SubdivisionLevel = 0;
    ProfilingParams.bEnableAutomaticLOD = false;
    ProfilingParams.bEnableOceanicAmplification = true;
    ProfilingParams.bEnableContinentalAmplification = true;
    ProfilingParams.bEnableHydraulicErosion = true;
    ProfilingParams.bEnableSedimentTransport = true;
    ProfilingParams.bEnableOceanicDampening = true;
    ProfilingParams.bSkipCPUAmplification = true;
    ProfilingParams.VisualizationMode = ETectonicVisualizationMode::Amplified;

    Service->SetParameters(ProfilingParams);
    Service->ResetSimulation();

    IConsoleVariable* StageBCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.StageBProfiling"));
    const int32 OriginalStageBValue = StageBCVar ? StageBCVar->GetInt() : 0;
    if (StageBCVar)
    {
        StageBCVar->Set(1, ECVF_SetByCode);
    }

    constexpr int32 WarmupSteps = 8;
    Service->AdvanceSteps(WarmupSteps);

    if (StageBCVar)
    {
        StageBCVar->Set(OriginalStageBValue, ECVF_SetByCode);
    }

    Service->SetParameters(OriginalParams);
    Service->ResetSimulation();

    return true;
#else
    return false;
#endif
}
