#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "Simulation/TectonicSimulationService.h"
#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStageBAnisotropySanityTest,
    "PlanetaryCreation.StageB.AnisotropySanity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStageBAnisotropySanityTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
    {
        return false;
    }

    IConsoleVariable* AnisoCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.StageBEnableAnisotropy"));
    IConsoleVariable* GPUCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.UseGPUAmplification"));
    if (!AnisoCVar || !GPUCVar)
    {
        AddError(TEXT("Required anisotropy/GPU CVars were not found."));
        return false;
    }

    const int32 OriginalAniso = AnisoCVar->GetInt();
    const int32 OriginalGPU = GPUCVar->GetInt();
    const bool bOriginalSkipCPU = Service->GetParameters().bSkipCPUAmplification;

    ON_SCOPE_EXIT
    {
        AnisoCVar->Set(OriginalAniso, ECVF_SetByCode);
        GPUCVar->Set(OriginalGPU, ECVF_SetByCode);
        Service->SetSkipCPUAmplification(bOriginalSkipCPU);
    };

    const TArray<EOrogenyClass>& OrogenyClasses = Service->GetVertexOrogenyClass();
    const TArray<FVector3f>& FoldDirections = Service->GetVertexFoldDirection();
    int32 TargetIndex = INDEX_NONE;
    for (int32 Index = 0; Index < OrogenyClasses.Num(); ++Index)
    {
        if (OrogenyClasses[Index] == EOrogenyClass::Active &&
            FoldDirections.IsValidIndex(Index) &&
            FoldDirections[Index].SizeSquared() > 1.0e-6f)
        {
            TargetIndex = Index;
            break;
        }
    }

    if (TargetIndex == INDEX_NONE)
    {
        AddWarning(TEXT("No vertex with Active orogeny class and valid fold direction was found; anisotropy sanity test skipped."));
        return true;
    }

    auto CaptureAmplifiedValue = [&](int32 AnisoValue, double& OutValue) -> bool
    {
        AnisoCVar->Set(AnisoValue, ECVF_SetByCode);
        GPUCVar->Set(0, ECVF_SetByCode);
        Service->SetSkipCPUAmplification(false);
        Service->ForceStageBAmplificationRebuild(TEXT("Automation.StageBAnisotropySanity"));
        Service->ProcessPendingOceanicGPUReadbacks(true);
        Service->ProcessPendingContinentalGPUReadbacks(true);

        const TArray<double>& Amplified = Service->GetVertexAmplifiedElevation();
        if (!Amplified.IsValidIndex(TargetIndex))
        {
            AddError(FString::Printf(TEXT("AmplifiedElevation missing at vertex %d"), TargetIndex));
            return false;
        }

        OutValue = Amplified[TargetIndex];
        return true;
    };

    double BaselineValue = 0.0;
    if (!CaptureAmplifiedValue(0, BaselineValue))
    {
        return false;
    }

    double AnisotropicValue = 0.0;
    if (!CaptureAmplifiedValue(1, AnisotropicValue))
    {
        return false;
    }

    const double Delta = FMath::Abs(AnisotropicValue - BaselineValue);
    if (Delta < 1e-3)
    {
        AddWarning(FString::Printf(TEXT("Anisotropy produced negligible delta (%.6f m) at vertex %d"), Delta, TargetIndex));
    }
    else
    {
        AddInfo(FString::Printf(TEXT("Anisotropy delta at vertex %d: %.6f m (baseline=%.6f, anisotropic=%.6f)"),
            TargetIndex, Delta, BaselineValue, AnisotropicValue));
    }

    return true;
}
