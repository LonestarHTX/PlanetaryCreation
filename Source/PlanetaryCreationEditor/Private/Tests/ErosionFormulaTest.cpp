#include "Misc/AutomationTest.h"
#include "Simulation/ErosionProcessor.h"
#include "Simulation/PaperConstants.h"

using namespace PaperConstants;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FErosionFormulasTest, "PlanetaryCreation.Paper.ErosionFormulas",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FErosionFormulasTest::RunTest(const FString& Parameters)
{
    // Minimal synthetic setup: 3 vertices with plate assignments and a boundary distance
    const int32 N = 3;
    TArray<FVector3d> Points; Points.Init(FVector3d(1,0,0), N);
    TArray<int32> PlateAssign; PlateAssign.Init(0, N);
    TArray<uint8> Crust; Crust.SetNum(2); Crust[0] = 1; /* continental */ Crust[1] = 0; /* oceanic */

    BoundaryField::FBoundaryFieldResults BF;
    BF.DistanceToSubductionFront_km.Init(1e9, N);

    // 1) Continental erosion: z=5000, on continental plate, z>0
    {
        PlateAssign[0] = 0; // crust[0]=continental
        TArray<double> Z; Z.Init(5000.0, N);
        const double before = Z[0];
        const double expectedDelta = (before / MaxContinentalAltitude_m) * ContinentalErosion_m_per_My * TimeStep_My;

        const Erosion::FErosionMetrics M = Erosion::ApplyErosionAndDampening(Points, PlateAssign, Crust, BF, Z, 0.0);
        TestTrue(TEXT("continental erosion changed 1"), M.ContinentalVertsChanged >= 1);
        TestTrue(TEXT("continental z decreased"), Z[0] < before);
        TestTrue(TEXT("continental delta matches"), FMath::IsNearlyEqual(before - Z[0], expectedDelta, 1e-12));
    }

    // 2) Continental negative elevation: z=-100 -> unchanged by erosion term
    {
        PlateAssign[0] = 0; // continental
        TArray<double> Z; Z.Init(-100.0, N);
        const Erosion::FErosionMetrics M = Erosion::ApplyErosionAndDampening(Points, PlateAssign, Crust, BF, Z, 0.0);
        // Oceanic dampening off because crust[0] is continental; erosion should not apply since z<=0
        TestTrue(TEXT("continental z<=0 unchanged"), FMath::IsNearlyEqual(Z[0], -100.0, 1e-12));
    }

    // 3) Oceanic dampening: z=-6000 on oceanic plate
    {
        PlateAssign[0] = 1; // crust[1]=oceanic
        TArray<double> Z; Z.Init(-6000.0, N);
        const double before = Z[0];
        const double term = 1.0 - (before / TrenchDepth_m);
        const double expectedDelta = term * OceanicDampening_m_per_My * TimeStep_My; // positive amount subtracted
        const Erosion::FErosionMetrics M = Erosion::ApplyErosionAndDampening(Points, PlateAssign, Crust, BF, Z, 0.0);
        TestTrue(TEXT("oceanic dampening changed 1"), M.OceanicVertsChanged >= 1);
        TestTrue(TEXT("oceanic z decreased"), Z[0] < before);
        TestTrue(TEXT("oceanic delta matches"), FMath::IsNearlyEqual(before - Z[0], expectedDelta, 1e-12));
    }

    // 4) Trench accretion: d=0 -> z increases by εt·δt
    {
        PlateAssign[0] = 1; // oceanic (doesn't matter for trench term)
        BoundaryField::FBoundaryFieldResults BF2; BF2.DistanceToSubductionFront_km.Init(1e9, N);
        BF2.DistanceToSubductionFront_km[0] = 0.0; // at trench
        TArray<double> Z; Z.Init(-6000.0, N);
        const double before = Z[0];
        const double expected = before + SedimentAccretion_m_per_My * TimeStep_My;
        const Erosion::FErosionMetrics M = Erosion::ApplyErosionAndDampening(Points, PlateAssign, Crust, BF2, Z, /*band*/ 200.0);
        TestTrue(TEXT("trench accretion changed 1"), M.TrenchVertsChanged >= 1);
        TestTrue(TEXT("trench z increased"), Z[0] > before);
        TestTrue(TEXT("trench delta matches"), FMath::IsNearlyEqual(Z[0], expected, 1e-12));
    }

    // Determinism: repeat with same inputs
    {
        PlateAssign[0] = 0; // continental
        BoundaryField::FBoundaryFieldResults BF3; BF3.DistanceToSubductionFront_km.Init(1e9, N);
        TArray<double> Z1; Z1.Init(2500.0, N);
        TArray<double> Z2 = Z1;
        const Erosion::FErosionMetrics M1 = Erosion::ApplyErosionAndDampening(Points, PlateAssign, Crust, BF3, Z1, 0.0);
        const Erosion::FErosionMetrics M2 = Erosion::ApplyErosionAndDampening(Points, PlateAssign, Crust, BF3, Z2, 0.0);
        for (int32 i = 0; i < N; ++i)
        {
            TestTrue(TEXT("deterministic erosion result"), FMath::IsNearlyEqual(Z1[i], Z2[i], 1e-12));
        }
    }

    return true;
}

