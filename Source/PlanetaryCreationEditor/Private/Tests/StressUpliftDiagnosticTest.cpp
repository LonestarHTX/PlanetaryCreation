#include "Misc/AutomationTest.h"

#if WITH_EDITOR
#include "Simulation/TectonicSimulationService.h"
#include "Utilities/PlanetaryCreationLogging.h"
#include "Math/UnrealMathUtility.h"
#include "Math/NumericLimits.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStressUpliftDiagnosticTest,
    "PlanetaryCreation.Milestone6.Diagnostics.StressUplift",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStressUpliftDiagnosticTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    UTectonicSimulationService* Service = GEditor ? GEditor->GetEditorSubsystem<UTectonicSimulationService>() : nullptr;
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
    {
        return false;
    }

    const FTectonicSimulationParameters OriginalParams = Service->GetParameters();

    FTectonicSimulationParameters DiagnosticParams = OriginalParams;
    DiagnosticParams.Seed = 12345;
    DiagnosticParams.RenderSubdivisionLevel = FMath::Max(DiagnosticParams.MinAmplificationLOD, 5);
    DiagnosticParams.SubdivisionLevel = 0;
    DiagnosticParams.bEnableAutomaticLOD = false;
    DiagnosticParams.bEnableOceanicAmplification = true;
    DiagnosticParams.bEnableContinentalAmplification = true;
    DiagnosticParams.bEnableHydraulicErosion = true;
    DiagnosticParams.bEnableContinentalErosion = true;
    DiagnosticParams.bEnableSedimentTransport = true;
    DiagnosticParams.bEnableOceanicDampening = true;
    DiagnosticParams.bSkipCPUAmplification = true;
    DiagnosticParams.VisualizationMode = ETectonicVisualizationMode::Amplified;

    Service->SetParameters(DiagnosticParams);
    Service->ResetSimulation();

    constexpr int32 StepsToRun = 6;
    double MaxObservedStress = 0.0;
    double MaxObservedElevation = TNumericLimits<double>::Lowest();

    for (int32 Step = 0; Step < StepsToRun; ++Step)
    {
        Service->AdvanceSteps(1);

        const TArray<double>& StressValues = Service->GetVertexStressValues();
        const TArray<double>& ElevationValues = Service->GetVertexElevationValues();
        double StepMaxStress = 0.0;
        double StepMaxElevation = TNumericLimits<double>::Lowest();

        for (double Stress : StressValues)
        {
            StepMaxStress = FMath::Max(StepMaxStress, Stress);
        }

        for (double Elevation : ElevationValues)
        {
            StepMaxElevation = FMath::Max(StepMaxElevation, Elevation);
        }

        MaxObservedStress = FMath::Max(MaxObservedStress, StepMaxStress);
        MaxObservedElevation = FMath::Max(MaxObservedElevation, StepMaxElevation);

        const double EstimatedStressLiftMeters = StepMaxStress * 10.0; // Mirrors ApplyContinentalErosion uplift scale

        UE_LOG(LogPlanetaryCreation, Log,
            TEXT("[StressUpliftDiagnostics] Step %d | MaxStress %.3f MPa | EstimatedStressLift %.1f m | MaxElevation %.1f m"),
            Step + 1,
            StepMaxStress,
            EstimatedStressLiftMeters,
            StepMaxElevation);
    }

    TestTrue(TEXT("Stress field should register non-zero magnitude"), MaxObservedStress > 0.0);

    Service->SetParameters(OriginalParams);
    Service->ResetSimulation();

    return true;
#else
    return false;
#endif
}
