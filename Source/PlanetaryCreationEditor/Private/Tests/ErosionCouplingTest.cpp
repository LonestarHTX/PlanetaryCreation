// Milestone 6 Task 3.2: Hydraulic erosion coupling (age-based) test

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"

#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHydraulicErosionCouplingTest,
    "PlanetaryCreation.Milestone6.HydraulicErosionCoupling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    void ConfigurePlatesForCoupling(TArray<FTectonicPlate>& Plates)
    {
        for (int32 PlateIdx = 0; PlateIdx < Plates.Num(); ++PlateIdx)
        {
            const double Angle = PlateIdx * 0.51;
            Plates[PlateIdx].EulerPoleAxis = FVector3d(
                FMath::Cos(Angle),
                FMath::Sin(Angle * 0.9),
                FMath::Cos(Angle * 0.4)).GetSafeNormal();
            Plates[PlateIdx].AngularVelocity = 0.025 + 0.0015 * PlateIdx;
        }
    }

    void AssignAgeGroups(UTectonicSimulationService& Service, const TArray<int32>& YoungGroup, const TArray<int32>& OldGroup)
    {
        TArray<double>& MutableCrustAge = Service.GetMutableVertexCrustAge();
        const double DefaultAge = 60.0;
        for (double& Age : MutableCrustAge)
        {
            Age = DefaultAge;
        }

        for (int32 Index : YoungGroup)
        {
            if (MutableCrustAge.IsValidIndex(Index))
            {
                MutableCrustAge[Index] = 10.0;
            }
        }

        for (int32 Index : OldGroup)
        {
            if (MutableCrustAge.IsValidIndex(Index))
            {
                MutableCrustAge[Index] = 150.0;
            }
        }
    }
}

bool FHydraulicErosionCouplingTest::RunTest(const FString& Parameters)
{
    if (!GEditor)
    {
        AddError(TEXT("Hydraulic erosion coupling test requires editor context."));
        return false;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to acquire UTectonicSimulationService."));
        return false;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Hydraulic Erosion Coupling Test ==="));

    FTectonicSimulationParameters Params;
    Params.Seed = 13579;
    Params.SubdivisionLevel = 0;
    Params.RenderSubdivisionLevel = 3;
    Params.MinAmplificationLOD = 3;
    Params.LloydIterations = 0;
    Params.bEnableOceanicAmplification = true;
    Params.bEnableContinentalAmplification = true;
    Params.bSkipCPUAmplification = false;
    Params.bEnableHydraulicErosion = false; // baseline pass first
    Params.HydraulicErosionConstant = 0.05; // exaggerate differences for test visibility
    Params.HydraulicDownstreamDepositRatio = 0.5;
    Params.bEnableDynamicRetessellation = false;
    Params.bEnableAutomaticLOD = false;

    Service->SetParameters(Params);
    ConfigurePlatesForCoupling(Service->GetPlatesForModification());

    const TArray<int32>& PlateAssignments = Service->GetVertexPlateAssignments();
    const TArray<FTectonicPlate>& Plates = Service->GetPlates();
    TArray<double>& MutableCrustAge = Service->GetMutableVertexCrustAge();

    TArray<int32> YoungIndices;
    TArray<int32> OldIndices;
    YoungIndices.Reserve(128);
    OldIndices.Reserve(128);

    for (int32 VertexIdx = 0; VertexIdx < PlateAssignments.Num(); ++VertexIdx)
    {
        const int32 PlateIdx = PlateAssignments[VertexIdx];
        if (!Plates.IsValidIndex(PlateIdx) || Plates[PlateIdx].CrustType != ECrustType::Continental)
        {
            MutableCrustAge[VertexIdx] = 60.0;
            continue;
        }

        if (YoungIndices.Num() < 96)
        {
            MutableCrustAge[VertexIdx] = 10.0;
            YoungIndices.Add(VertexIdx);
        }
        else if (OldIndices.Num() < 96)
        {
            MutableCrustAge[VertexIdx] = 150.0;
            OldIndices.Add(VertexIdx);
        }
        else
        {
            MutableCrustAge[VertexIdx] = 60.0;
        }
    }

    TestTrue(TEXT("Hydraulic coupling test gathered young sample size"), YoungIndices.Num() >= 32);
    TestTrue(TEXT("Hydraulic coupling test gathered old sample size"), OldIndices.Num() >= 32);

    Service->AdvanceSteps(3);
    const TArray<double>& AmplifiedWithoutHydraulic = Service->GetVertexAmplifiedElevation();
    TArray<double> BaselineAmplified = AmplifiedWithoutHydraulic;

    Params.bEnableHydraulicErosion = true;
    Service->SetParameters(Params);
    ConfigurePlatesForCoupling(Service->GetPlatesForModification());
    AssignAgeGroups(*Service, YoungIndices, OldIndices);

    Service->AdvanceSteps(1);
    const TArray<double>& AmplifiedWithHydraulicStep1 = Service->GetVertexAmplifiedElevation();

    TestEqual(TEXT("Amplified arrays size unchanged with hydraulic enabled"),
        BaselineAmplified.Num(), AmplifiedWithHydraulicStep1.Num());

    auto ComputeAverageDelta = [&BaselineAmplified](const TArray<double>& Sample, const TArray<int32>& Indices)
    {
        double Sum = 0.0;
        for (int32 Index : Indices)
        {
            if (BaselineAmplified.IsValidIndex(Index) && Sample.IsValidIndex(Index))
            {
                Sum += (BaselineAmplified[Index] - Sample[Index]);
            }
        }
        return (Indices.Num() > 0) ? Sum / static_cast<double>(Indices.Num()) : 0.0;
    };

    const double YoungDeltaStep1 = ComputeAverageDelta(AmplifiedWithHydraulicStep1, YoungIndices);
    const double OldDeltaStep1 = ComputeAverageDelta(AmplifiedWithHydraulicStep1, OldIndices);

    UE_LOG(LogPlanetaryCreation, Log,
        TEXT("  Step 1: Young avg delta %.3f m | Old avg delta %.3f m"),
        YoungDeltaStep1, OldDeltaStep1);

    Service->AdvanceSteps(1);
    const TArray<double>& AmplifiedWithHydraulicStep2 = Service->GetVertexAmplifiedElevation();
    const double YoungDeltaStep2 = ComputeAverageDelta(AmplifiedWithHydraulicStep2, YoungIndices);
    const double OldDeltaStep2 = ComputeAverageDelta(AmplifiedWithHydraulicStep2, OldIndices);

    UE_LOG(LogPlanetaryCreation, Log,
        TEXT("  Step 2: Young avg delta %.3f m | Old avg delta %.3f m"),
        YoungDeltaStep2, OldDeltaStep2);

    TestTrue(TEXT("Young mountain group remains near baseline"), YoungDeltaStep2 > -1.0);
    TestTrue(TEXT("Old mountain group erodes more than young group"), OldDeltaStep2 > YoungDeltaStep2 + 0.5);
    TestTrue(TEXT("Hydraulic erosion accumulates over multiple steps for old terrains"), OldDeltaStep2 > OldDeltaStep1 + 0.5);

    return true;
}
