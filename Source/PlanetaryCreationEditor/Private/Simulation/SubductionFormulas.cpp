#include "Simulation/SubductionFormulas.h"

namespace SubductionFormulas
{
    static inline double SmoothStep01(double t)
    {
        // Cubic smoothstep: C1 continuous with zero slope at ends, monotone on [0,1]
        t = FMath::Clamp(t, 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    double F_DistanceKernel(double d_km)
    {
        using namespace PaperConstants;

        const double rs = SubductionDistance_km;
        const double rc = SubductionControlDistance_km;

        if (d_km <= 0.0)
        {
            return 0.0;
        }
        if (d_km >= rs)
        {
            return 0.0;
        }

        if (d_km <= rc)
        {
            // Rise segment: f = S(d/rc)
            const double t = d_km / rc;
            return SmoothStep01(t);
        }
        else
        {
            // Fall segment: f = 1 - S((d-rc)/(rs-rc))
            const double u = (d_km - rc) / (rs - rc);
            return 1.0 - SmoothStep01(u);
        }
    }

    double G_RelativeSpeedRatio(double v_km_per_My)
    {
        using namespace PaperConstants;
        const double v = FMath::Max(0.0, v_km_per_My);
        return v / MaxPlateSpeed_km_per_My;
    }

    double H_ElevationFactor(double elevation_m)
    {
        using namespace PaperConstants;
        const double ztilde = FMath::Clamp(NormalizedElevationForSubduction(elevation_m), 0.0, 1.0);
        return ztilde * ztilde;
    }

    double ComputeRelativeSurfaceSpeedKmPerMy(
        const FVector3d& OmegaI_rad_per_My,
        const FVector3d& OmegaJ_rad_per_My,
        const FVector3d& PUnit)
    {
        using namespace PaperConstants;

        // Assumes PUnit is on the unit sphere; velocity v = (omega x R*P) = R * (omega x P)
        const FVector3d Vi = FVector3d::CrossProduct(OmegaI_rad_per_My, PUnit) * PlanetRadius_km;
        const FVector3d Vj = FVector3d::CrossProduct(OmegaJ_rad_per_My, PUnit) * PlanetRadius_km;
        const double Speed = (Vi - Vj).Length();
        return FMath::Max(0.0, Speed);
    }

    double EvaluateSubductionUpliftMetersPerMy(
        double d_km,
        const FVector3d& OmegaI_rad_per_My,
        const FVector3d& OmegaJ_rad_per_My,
        const FVector3d& PUnit,
        double elevation_m,
        double u0_m_per_My)
    {
        const double f = F_DistanceKernel(d_km);
        if (f <= 0.0)
        {
            return 0.0;
        }

        const double v_rel = ComputeRelativeSurfaceSpeedKmPerMy(OmegaI_rad_per_My, OmegaJ_rad_per_My, PUnit);
        const double g = G_RelativeSpeedRatio(v_rel);
        const double h = H_ElevationFactor(elevation_m);
        return u0_m_per_My * f * g * h;
    }
}

