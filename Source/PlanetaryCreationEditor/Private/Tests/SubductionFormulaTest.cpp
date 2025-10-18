#include "Misc/AutomationTest.h"
#include "Simulation/SubductionFormulas.h"
#include "Simulation/PaperConstants.h"

using namespace SubductionFormulas;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSubductionFormulasTest, "PlanetaryCreation.Paper.SubductionFormulas",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSubductionFormulasTest::RunTest(const FString& Parameters)
{
    using namespace PaperConstants;

    // f(d) basic values
    {
        const double f0 = F_DistanceKernel(0.0);
        TestTrue(TEXT("f(0) = 0"), FMath::IsNearlyZero(f0, 1e-12));

        const double f_rc = F_DistanceKernel(SubductionControlDistance_km);
        TestTrue(TEXT("f(rc) > 0"), f_rc > 0.0);

        const double f_rs = F_DistanceKernel(SubductionDistance_km);
        TestTrue(TEXT("f(rs) = 0"), FMath::IsNearlyZero(f_rs, 1e-12));

        // Monotone rise on [0, rc]
        const double rc = SubductionControlDistance_km;
        const double f_q1 = F_DistanceKernel(rc * 0.25);
        const double f_q2 = F_DistanceKernel(rc * 0.50);
        const double f_q3 = F_DistanceKernel(rc * 0.75);
        TestTrue(TEXT("f rise monotone"), f0 <= f_q1 && f_q1 <= f_q2 && f_q2 <= f_q3 && f_q3 <= f_rc + 1e-12);

        // Monotone fall on [rc, rs]
        const double rs = SubductionDistance_km;
        const double midFall = rc + (rs - rc) * 0.5;
        const double f_f1 = F_DistanceKernel(rc + (rs - rc) * 0.25);
        const double f_f2 = F_DistanceKernel(midFall);
        const double f_f3 = F_DistanceKernel(rc + (rs - rc) * 0.75);
        TestTrue(TEXT("f fall monotone"), f_rc >= f_f1 && f_f1 >= f_f2 && f_f2 >= f_f3 && f_f3 >= f_rs - 1e-12);
    }

    // g(v) and relative speed symmetry
    {
        const double g0 = G_RelativeSpeedRatio(0.0);
        TestTrue(TEXT("g(0) = 0"), FMath::IsNearlyZero(g0, 1e-12));

        const double g1 = G_RelativeSpeedRatio(MaxPlateSpeed_km_per_My);
        TestTrue(TEXT("g(v0) = 1"), FMath::IsNearlyEqual(g1, 1.0, 1e-12));

        // Symmetry under swapping omegas
        const FVector3d P(1.0, 0.0, 0.0); // unit sphere point
        const FVector3d O1(0.0, 0.0, 0.01); // rad/My
        const FVector3d O2(0.0, 0.005, 0.0); // rad/My
        const double v12 = ComputeRelativeSurfaceSpeedKmPerMy(O1, O2, P);
        const double v21 = ComputeRelativeSurfaceSpeedKmPerMy(O2, O1, P);
        TestTrue(TEXT("relative speed symmetry"), FMath::IsNearlyEqual(v12, v21, 1e-12));
    }

    // h(z~)
    {
        const double h_zt = H_ElevationFactor(TrenchDepth_m);
        TestTrue(TEXT("h(zt) = 0"), FMath::IsNearlyZero(h_zt, 1e-12));

        const double ztilde_mid = FMath::Clamp(NormalizedElevationForSubduction(SeaLevel_m), 0.0, 1.0);
        const double h_mid_expected = ztilde_mid * ztilde_mid;
        const double h_mid = H_ElevationFactor(SeaLevel_m);
        TestTrue(TEXT("h(mid) matches"), FMath::IsNearlyEqual(h_mid, h_mid_expected, 1e-12));

        const double h_zc = H_ElevationFactor(MaxContinentalAltitude_m);
        TestTrue(TEXT("h(zc) = 1"), FMath::IsNearlyEqual(h_zc, 1.0, 1e-12));

        // Clamps
        TestTrue(TEXT("h below clamp"), FMath::IsNearlyEqual(H_ElevationFactor(TrenchDepth_m - 1000.0), 0.0, 1e-12));
        TestTrue(TEXT("h above clamp"), FMath::IsNearlyEqual(H_ElevationFactor(MaxContinentalAltitude_m + 1000.0), 1.0, 1e-12));
    }

    // Full uplift û
    {
        const FVector3d P(1.0, 0.0, 0.0);

        // Zero velocities -> zero uplift
        {
            const double u = EvaluateSubductionUpliftMetersPerMy(
                SubductionControlDistance_km, FVector3d::ZeroVector, FVector3d::ZeroVector, P, SeaLevel_m);
            TestTrue(TEXT("uplift zero when omegas are zero"), FMath::IsNearlyZero(u, 1e-12));
        }

        // v = v0 at d=rc, mid elevation -> positive uplift
        {
            // Choose Omega so that |(Omega x P)| * R = v0 => |Omega| = v0 / R with axis ⟂ P
            const double omega_mag = MaxPlateSpeed_km_per_My / PlanetRadius_km; // rad/My
            const FVector3d O1(0.0, 0.0, omega_mag);
            const FVector3d O2(0.0, 0.0, 0.0);

            const double v_rel = ComputeRelativeSurfaceSpeedKmPerMy(O1, O2, P);
            TestTrue(TEXT("v_rel = v0"), FMath::IsNearlyEqual(v_rel, MaxPlateSpeed_km_per_My, 1e-12));

            const double u = EvaluateSubductionUpliftMetersPerMy(SubductionControlDistance_km, O1, O2, P, SeaLevel_m);
            TestTrue(TEXT("uplift positive at rc, v0, mid z"), u > 0.0);
        }

        // Zero outside d >= rs
        {
            const FVector3d O(0.0, 0.0, 0.01);
            const double u_at_rs = EvaluateSubductionUpliftMetersPerMy(SubductionDistance_km, O, FVector3d::ZeroVector, P, SeaLevel_m);
            const double u_beyond = EvaluateSubductionUpliftMetersPerMy(SubductionDistance_km + 100.0, O, FVector3d::ZeroVector, P, SeaLevel_m);
            TestTrue(TEXT("uplift at rs is zero"), FMath::IsNearlyZero(u_at_rs, 1e-12));
            TestTrue(TEXT("uplift beyond rs is zero"), FMath::IsNearlyZero(u_beyond, 1e-12));
        }
    }

    // Determinism: repeat-eval identical
    {
        const FVector3d P(0.0, 1.0, 0.0);
        const FVector3d O1(0.003, 0.004, 0.0);
        const FVector3d O2(-0.001, 0.002, 0.0);
        const double d = SubductionControlDistance_km * 0.8;
        const double z = 2000.0; // 2 km

        const double u1 = EvaluateSubductionUpliftMetersPerMy(d, O1, O2, P, z);
        const double u2 = EvaluateSubductionUpliftMetersPerMy(d, O1, O2, P, z);
        TestTrue(TEXT("deterministic uplift"), FMath::IsNearlyEqual(u1, u2, 1e-12));
    }

    return true;
}
