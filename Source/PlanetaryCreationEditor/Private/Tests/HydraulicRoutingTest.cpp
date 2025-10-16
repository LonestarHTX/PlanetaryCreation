// Milestone 6 Task 3.1: Hydraulic Routing / Stage B erosion test

#include "Utilities/PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "Simulation/TectonicSimulationService.h"

#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHydraulicRoutingTest,
    "PlanetaryCreation.Milestone6.HydraulicRouting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    void ConfigurePlateRotation(TArray<FTectonicPlate>& Plates)
    {
        for (int32 PlateIdx = 0; PlateIdx < Plates.Num(); ++PlateIdx)
        {
            const double Angle = PlateIdx * 0.37;
            Plates[PlateIdx].EulerPoleAxis = FVector3d(
                FMath::Sin(Angle),
                FMath::Cos(Angle * 1.3),
                FMath::Sin(Angle * 0.7)).GetSafeNormal();
            Plates[PlateIdx].AngularVelocity = 0.02 + 0.002 * PlateIdx;
        }
    }
}

bool FHydraulicRoutingTest::RunTest(const FString& Parameters)
{
    if (!GEditor)
    {
        AddError(TEXT("Hydraulic routing test requires editor context."));
        return false;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to acquire UTectonicSimulationService."));
        return false;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Hydraulic Routing Test ==="));

    FTectonicSimulationParameters Params;
    Params.Seed = 24680;
    Params.SubdivisionLevel = 0;
    Params.RenderSubdivisionLevel = 3;
    Params.MinAmplificationLOD = 3;
    Params.LloydIterations = 0;
    Params.bEnableOceanicAmplification = true;
    Params.bEnableContinentalAmplification = true;
    Params.bEnableHydraulicErosion = false; // baseline first
    Params.bSkipCPUAmplification = false;
    Params.bEnableHotspots = true;
    Params.bEnableDynamicRetessellation = false;
    Params.bEnableAutomaticLOD = false;

    Service->SetParameters(Params);
    ConfigurePlateRotation(Service->GetPlatesForModification());
    Service->AdvanceSteps(6);

    const TArray<double>& BaselineAmplified = Service->GetVertexAmplifiedElevation();
    TestTrue(TEXT("Baseline amplified array populated"), BaselineAmplified.Num() > 0);
    TArray<double> BaselineCopy = BaselineAmplified;

    Params.bEnableHydraulicErosion = true;
    Service->SetParameters(Params);
    ConfigurePlateRotation(Service->GetPlatesForModification());
    Service->AdvanceSteps(1);

    const TArray<double>& HydraulicAmplifiedStep1 = Service->GetVertexAmplifiedElevation();
    TestEqual(TEXT("Amplified array size stable after hydraulic pass"), BaselineCopy.Num(), HydraulicAmplifiedStep1.Num());

    auto ComputeDelta = [&BaselineCopy](const TArray<double>& Sample)
    {
        double Sum = 0.0;
        double MaxAbs = 0.0;
        for (int32 Index = 0; Index < Sample.Num(); ++Index)
        {
            const double Delta = BaselineCopy[Index] - Sample[Index];
            const double AbsDelta = FMath::Abs(Delta);
            Sum += AbsDelta;
            MaxAbs = FMath::Max(MaxAbs, AbsDelta);
        }
        return TPair<double, double>(Sum, MaxAbs);
    };

    const TPair<double, double> Step1Delta = ComputeDelta(HydraulicAmplifiedStep1);

    TestTrue(TEXT("Hydraulic pass modifies Stage B elevations"), Step1Delta.Value > 0.1);
    TestTrue(TEXT("Hydraulic pass produces global elevation changes"), Step1Delta.Key > 1.0);

    Service->AdvanceSteps(1);
    const TArray<double>& HydraulicAmplifiedStep2 = Service->GetVertexAmplifiedElevation();
    const TPair<double, double> Step2Delta = ComputeDelta(HydraulicAmplifiedStep2);

    TestTrue(TEXT("Hydraulic erosion accumulates across multiple steps"), Step2Delta.Key > Step1Delta.Key + 0.5);

    const double TotalEroded = Service->GetLastHydraulicTotalEroded();
    const double TotalDeposited = Service->GetLastHydraulicTotalDeposited();
    const double LostToOcean = Service->GetLastHydraulicLostToOcean();

    TestTrue(TEXT("Hydraulic erosion removes material"), TotalEroded > 0.0);
    TestTrue(TEXT("Hydraulic erosion deposits or exports material"), (TotalDeposited + LostToOcean) > 0.0);

    const double Balance = FMath::Abs(TotalEroded - (TotalDeposited + LostToOcean));
    const double BalanceRatio = (TotalEroded > 1e-3) ? Balance / TotalEroded : 0.0;
    TestTrue(TEXT("Hydraulic mass conservation within 5%"), BalanceRatio <= 0.05 + KINDA_SMALL_NUMBER);

    UE_LOG(LogPlanetaryCreation, Log,
        TEXT("  Hydraulic summary: Eroded=%.3f m | Deposited=%.3f m | Lost=%.3f m | Balance=%.4f (%.2f%%) | Step1 Σ|Δ|=%.3f m | Step2 Σ|Δ|=%.3f m"),
        TotalEroded,
        TotalDeposited,
        LostToOcean,
        Balance,
        BalanceRatio * 100.0,
        Step1Delta.Key,
        Step2Delta.Key);

    return true;
}
