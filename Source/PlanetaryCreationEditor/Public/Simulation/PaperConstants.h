#pragma once

// PaperConstants.h
// Single source-of-truth for Appendix A constants and unit helpers
// from Procedural Tectonic Planets (UE 5.5 project).
//
// Units (locked):
// - Distances: km; Elevations: m; Time: My; Angular: rad; Speeds: km/My
// - Unreal: 1 uu = 1 cm; convert to uu only at render boundaries.
//
// Elevation reference: Sea level is 0 m; zc (max continental altitude) = 10 km.

namespace PaperConstants
{
    // Core simulation step
    constexpr double TimeStep_My = 2.0;

    // Planet geometry
    constexpr double PlanetRadius_km = 6370.0;
    constexpr double PlanetRadius_m  = 6370000.0;

    // Ocean/continental reference elevations (m)
    constexpr double RidgeElevation_m     = -1000.0;
    constexpr double AbyssalElevation_m   = -6000.0;
    constexpr double TrenchDepth_m        = -10000.0;
    constexpr double SeaLevel_m           = 0.0;
    constexpr double MaxContinentalAltitude_m = 10000.0; // zc

    // Geodynamic distances (km)
    constexpr double SubductionDistance_km = 1800.0;
    constexpr double CollisionDistance_km  = 4200.0;

    // Interaction coefficients
    constexpr double CollisionCoefficient_per_km = 1.3e-5;

    // Plate speeds
    constexpr double MaxPlateSpeed_mm_per_yr  = 100.0;
    constexpr double MaxPlateSpeed_km_per_My  = 100.0;

    // Uplift / erosion / sedimentation rates
    constexpr double SubductionUplift_mm_per_yr = 0.6;
    constexpr double SubductionUplift_m_per_My  = 600.0; // convenience, correct conversion

    constexpr double OceanicDampening_mm_per_yr   = 0.04;
    constexpr double ContinentalErosion_mm_per_yr = 0.03;
    constexpr double SedimentAccretion_mm_per_yr  = 0.3;

    constexpr double OceanicDampening_m_per_My   = 40.0;
    constexpr double ContinentalErosion_m_per_My = 30.0;
    constexpr double SedimentAccretion_m_per_My  = 300.0;

    // Tunables (paper omissions)
    constexpr double FoldDirectionBeta       = 0.1;
    constexpr double SlabPullEpsilon         = 0.001;
    constexpr double ReferencePlateArea_km2  = 1.0e7;

    // Derived helpers (inline)
    inline double LinearSpeedKmPerMyToAngularRadPerMy(double v_km_per_My)
    {
        return v_km_per_My / PlanetRadius_km;
    }

    inline double AngularRadPerMyToLinearKmPerMy(double omega_rad_per_My)
    {
        return omega_rad_per_My * PlanetRadius_km;
    }

    inline double GeodesicRadiansToKm(double theta_rad)
    {
        return theta_rad * PlanetRadius_km;
    }

    inline double KmToGeodesicRadians(double d_km)
    {
        return d_km / PlanetRadius_km;
    }

    // Normalized elevation across [zt, zc] per paper; sea level is 0 m reference.
    inline double NormalizedElevationForSubduction(double z_m)
    {
        return (z_m - TrenchDepth_m) / (MaxContinentalAltitude_m - TrenchDepth_m);
    }

    // Erosion normalization height (10 km)
    constexpr double ErosionNormalizationHeight_m = MaxContinentalAltitude_m;

    // Compile-time sanity checks
    static_assert(TrenchDepth_m < AbyssalElevation_m, "Expected: TrenchDepth_m < AbyssalElevation_m");
    static_assert(AbyssalElevation_m < RidgeElevation_m, "Expected: AbyssalElevation_m < RidgeElevation_m");
    static_assert(RidgeElevation_m <= SeaLevel_m, "Expected: RidgeElevation_m <= SeaLevel_m");
    static_assert(SeaLevel_m <= MaxContinentalAltitude_m, "Expected: SeaLevel_m <= MaxContinentalAltitude_m");

    static_assert(SubductionDistance_km > 0.0, "SubductionDistance_km must be positive");
    static_assert(CollisionDistance_km  > 0.0, "CollisionDistance_km must be positive");
}

