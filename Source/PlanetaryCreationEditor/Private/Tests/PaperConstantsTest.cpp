#include "Simulation/PaperConstants.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPaperConstantsTest, "PlanetaryCreation.Paper.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaperConstantsTest::RunTest(const FString& Parameters)
{
    using namespace PaperConstants;

    // Unit conversions
    TestEqual(TEXT("v0 km/My"), MaxPlateSpeed_km_per_My, 100.0);

    const double omega = LinearSpeedKmPerMyToAngularRadPerMy(MaxPlateSpeed_km_per_My);
    const double v_back = AngularRadPerMyToLinearKmPerMy(omega);
    TestTrue(TEXT("v <-> \xCF\x89R roundtrip"), FMath::IsNearlyEqual(v_back, MaxPlateSpeed_km_per_My, 1e-9));

    // Elevation ordering
    TestTrue(TEXT("elevation order"),
        TrenchDepth_m < AbyssalElevation_m &&
        AbyssalElevation_m < RidgeElevation_m &&
        RidgeElevation_m <= SeaLevel_m &&
        SeaLevel_m <= MaxContinentalAltitude_m);

    // Subduction normalization bounds (zc/zt per paper)
    TestTrue(TEXT("norm zt"), FMath::IsNearlyEqual(NormalizedElevationForSubduction(TrenchDepth_m), 0.0, 1e-12));
    TestTrue(TEXT("norm zc"), FMath::IsNearlyEqual(NormalizedElevationForSubduction(MaxContinentalAltitude_m), 1.0, 1e-12));

    // Mid-point sanity: at z=0 m, z~ ~= 0.5
    const double ztilde_mid = NormalizedElevationForSubduction(SeaLevel_m);
    TestTrue(TEXT("norm mid (z=0m) \xE2\x89\x88 0.5"), FMath::IsNearlyEqual(ztilde_mid, 0.5, 1e-12));

    // Critical rate regression guards
    TestEqual(TEXT("u0 m/My"), SubductionUplift_m_per_My, 600.0);
    TestEqual(TEXT("\xCE\xB5o m/My"), OceanicDampening_m_per_My, 40.0);
    TestEqual(TEXT("\xCE\xB5c m/My"), ContinentalErosion_m_per_My, 30.0);
    TestEqual(TEXT("\xCE\xB5t m/My"), SedimentAccretion_m_per_My, 300.0);

    return true;
}
