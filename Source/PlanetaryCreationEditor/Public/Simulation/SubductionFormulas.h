#pragma once

#include "CoreMinimal.h"
#include "Simulation/PaperConstants.h"

// SubductionFormulas.h
// Core functions for paper Section 4.1 with strict units (double-precision).
// - Distances: km; Elevations: m; Time: My; Angular: rad; Speeds: km/My
// - g(v) uses linear surface speed (km/My) relative to v0 = PaperConstants::MaxPlateSpeed_km_per_My
// - Helper computes relative surface speed from angular velocity vectors at a point on the unit sphere

namespace SubductionFormulas
{
    // f(d): piecewise C1-continuous bump per paper Fig. 6 over [0, rs]
    // We use the cubic smoothstep S(t) = 3 t^2 - 2 t^3 with S(0)=0, S(1)=1, S'(0)=S'(1)=0.
    // Rise:    t = d/rc in [0,1],   f = S(t)
    // Fall:    u = (d-rc)/(rs-rc), f = 1 - S(u)
    // Outside [0, rs], f(d) = 0. This yields a C1 piecewise (zero slope at 0, rc, rs), monotone up then down.
    double F_DistanceKernel(double d_km);

    // g(v): linear speed ratio v/v0 with clamp at >= 0 (no upper clamp per spec).
    double G_RelativeSpeedRatio(double v_km_per_My);

    // h(z): elevation factor with normalized z~ in [0,1] then squared. Inputs in meters.
    double H_ElevationFactor(double elevation_m);

    // Helper: compute relative surface speed magnitude (km/My) at a point on the unit sphere.
    // Inputs: angular velocity vectors in rad/My (vector along Euler pole axis scaled by angular speed).
    double ComputeRelativeSurfaceSpeedKmPerMy(
        const FVector3d& OmegaI_rad_per_My,
        const FVector3d& OmegaJ_rad_per_My,
        const FVector3d& PUnit);

    // Evaluate uplift rate (meters per My): û = u0 · f(d) · g(v) · h(z)
    double EvaluateSubductionUpliftMetersPerMy(
        double d_km,
        const FVector3d& OmegaI_rad_per_My,
        const FVector3d& OmegaJ_rad_per_My,
        const FVector3d& PUnit,
        double elevation_m,
        double u0_m_per_My = PaperConstants::SubductionUplift_m_per_My);
}

