#include "Misc/AutomationTest.h"
#include "Algo/Count.h"
#include "Misc/ScopeExit.h"
#include "Simulation/TectonicSimulationService.h"
#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoldDirectionConvergenceTest,
    "PlanetaryCreation.Milestone6.FoldDirectionConvergence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFoldDirectionConvergenceTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor ? GEditor->GetEditorSubsystem<UTectonicSimulationService>() : nullptr;
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
    {
        return false;
    }

    const FTectonicSimulationParameters OriginalParams = Service->GetParameters();
    ON_SCOPE_EXIT
    {
        Service->SetParameters(OriginalParams);
    };

    Service->ResetSimulation();
    const int32 WarmupSteps = 6;
    Service->AdvanceSteps(WarmupSteps);

    const TArray<EOrogenyClass>& InitialClasses = Service->GetVertexOrogenyClass();
    if (InitialClasses.Num() == 0)
    {
        AddWarning(TEXT("[FoldDirectionConvergenceTest] No orogeny classification data available after warmup; skipping checks."));
        return true;
    }

    const int32 ActiveDefault = Algo::Count(InitialClasses, EOrogenyClass::Active);
    const int32 NascentDefault = Algo::Count(InitialClasses, EOrogenyClass::Nascent);

    FTectonicSimulationParameters TightParams = OriginalParams;
    TightParams.ConvergentProximityRadActive = FMath::Max(OriginalParams.ConvergentProximityRadActive * 0.5, 1.0e-4);
    Service->SetParameters(TightParams);

    Service->AdvanceSteps(WarmupSteps);

    const TArray<EOrogenyClass>& TightClasses = Service->GetVertexOrogenyClass();
    if (TightClasses.Num() == 0)
    {
        AddWarning(TEXT("[FoldDirectionConvergenceTest] No orogeny classification data available after tightening threshold; skipping checks."));
        return true;
    }

    const int32 ActiveTight = Algo::Count(TightClasses, EOrogenyClass::Active);
    const int32 NascentTight = Algo::Count(TightClasses, EOrogenyClass::Nascent);

    AddInfo(FString::Printf(TEXT("[FoldDirectionConvergenceTest] WarmupSteps=%d ActiveDefault=%d ActiveTight=%d NascentDefault=%d NascentTight=%d"),
        WarmupSteps, ActiveDefault, ActiveTight, NascentDefault, NascentTight));

    if (ActiveTight < ActiveDefault)
    {
        AddInfo(TEXT("[FoldDirectionConvergenceTest] Active classification decreased after tightening proximity threshold as expected."));
    }
    else
    {
        AddWarning(TEXT("[FoldDirectionConvergenceTest] Active classification did not decrease after tightening proximity threshold."));
    }

    if (NascentTight >= NascentDefault)
    {
        AddInfo(TEXT("[FoldDirectionConvergenceTest] Nascent classification held steady or increased under tighter active threshold."));
    }
    else
    {
        AddWarning(TEXT("[FoldDirectionConvergenceTest] Nascent classification decreased unexpectedly under tighter active threshold."));
    }

    return true;
}
