#pragma once

#include "CoreMinimal.h"

namespace PlanetaryCreation::Heightmap
{

/**
 * Hypsometric tint gradient definition matching physical relief map conventions.
 * Shared between in-editor visualization and heightmap PNG export.
 *
 * Color zones:
 * - Deep ocean (-6000m to -4000m): Midnight navy → Dark cobalt
 * - Mid ocean (-4000m to -1000m): Blue shades
 * - Shallow water (-1000m to 0m): Teal → Cyan → Cyan-green
 * - Shoreline (0m to +500m): Lime-green → Forest green [CRITICAL TRANSITION]
 * - Rising terrain (+500m to +2000m): Yellow-green → Yellow → Orange
 * - Mountains (+2000m to +6000m): Red-orange → Vivid red → Deep crimson
 */
struct FHypsometricStop
{
    double ElevationMeters;
    FLinearColor Color;
};

static const FHypsometricStop HypsometricGradient[] = {
    // Zone 1: Abyssal ocean (-6000m to -4000m)
    { -6000.0, FLinearColor(0.000f, 0.059f, 0.196f) },  // Midnight navy RGB(0, 15, 50)
    { -4000.0, FLinearColor(0.078f, 0.196f, 0.471f) },  // Dark cobalt RGB(20, 50, 120)

    // Zone 2: Deep ocean to shelf (-4000m to -1000m)
    { -3000.0, FLinearColor(0.118f, 0.314f, 0.588f) },  // Mid-ocean blue RGB(30, 80, 150)
    { -2000.0, FLinearColor(0.196f, 0.431f, 0.706f) },  // Mid-ocean blue RGB(50, 110, 180)
    { -1000.0, FLinearColor(0.275f, 0.549f, 0.784f) },  // Teal RGB(70, 140, 200)

    // Zone 3: Shallow water (-1000m to 0m)
    { -500.0,  FLinearColor(0.392f, 0.706f, 0.863f) },  // Light turquoise RGB(100, 180, 220)
    { -200.0,  FLinearColor(0.471f, 0.824f, 0.922f) },  // Cyan RGB(120, 210, 235)
    { -100.0,  FLinearColor(0.510f, 0.843f, 0.843f) },  // Light cyan RGB(130, 215, 215)
    {  -50.0,  FLinearColor(0.549f, 0.863f, 0.784f) },  // Cyan-aqua RGB(140, 220, 200)
    {  -25.0,  FLinearColor(0.588f, 0.882f, 0.706f) },  // Aqua-green RGB(150, 225, 180)
    {  -10.0,  FLinearColor(0.627f, 0.902f, 0.627f) },  // Pale green RGB(160, 230, 160)

    // Zone 4: Shoreline emergence (0m to +500m) - CRITICAL TRANSITION
    {    0.0,  FLinearColor(0.667f, 0.922f, 0.588f) },  // Spring green RGB(170, 235, 150)
    {  100.0,  FLinearColor(0.392f, 0.784f, 0.314f) },  // Grass-green RGB(100, 200, 80)
    {  500.0,  FLinearColor(0.235f, 0.588f, 0.235f) },  // Forest green RGB(60, 150, 60)

    // Zone 5: Rising terrain (+500m to +2000m)
    { 1000.0,  FLinearColor(0.471f, 0.706f, 0.275f) },  // Yellow-green RGB(120, 180, 70)
    { 1500.0,  FLinearColor(0.863f, 0.784f, 0.235f) },  // Bright yellow RGB(220, 200, 60)
    { 2000.0,  FLinearColor(0.902f, 0.588f, 0.196f) },  // Burnt orange RGB(230, 150, 50)

    // Zone 6: Mountain peaks (+2000m to +5000m+)
    { 3000.0,  FLinearColor(0.941f, 0.392f, 0.157f) },  // Red-orange RGB(240, 100, 40)
    { 4000.0,  FLinearColor(0.863f, 0.196f, 0.118f) },  // Vivid red RGB(220, 50, 30)
    { 5000.0,  FLinearColor(0.706f, 0.078f, 0.078f) },  // Deep crimson RGB(180, 20, 20)
    { 6000.0,  FLinearColor(0.549f, 0.039f, 0.039f) }   // Blood red RGB(140, 10, 10)
};

/**
 * Convert an absolute elevation value (in meters) to a hypsometric tint color.
 * Uses absolute elevation breakpoints rather than normalization so that
 * mountains always appear red and oceans always appear blue.
 */
inline FColor MakeElevationColor(double ElevationMeters)
{
    constexpr int32 StopCount = UE_ARRAY_COUNT(HypsometricGradient);

    // Clamp to gradient range
    if (ElevationMeters <= HypsometricGradient[0].ElevationMeters)
    {
        return HypsometricGradient[0].Color.ToFColor(false);
    }
    if (ElevationMeters >= HypsometricGradient[StopCount - 1].ElevationMeters)
    {
        return HypsometricGradient[StopCount - 1].Color.ToFColor(false);
    }

    // Find bracketing stops
    for (int32 i = 0; i < StopCount - 1; ++i)
    {
        const double ElevLow = HypsometricGradient[i].ElevationMeters;
        const double ElevHigh = HypsometricGradient[i + 1].ElevationMeters;

        if (ElevationMeters >= ElevLow && ElevationMeters <= ElevHigh)
        {
            const double Range = ElevHigh - ElevLow;
            const double Alpha = (Range > KINDA_SMALL_NUMBER)
                ? (ElevationMeters - ElevLow) / Range
                : 0.0;

            // Use LINEAR RGB interpolation (not HSV) to preserve saturation
            const FLinearColor Interpolated = FMath::Lerp(
                HypsometricGradient[i].Color,
                HypsometricGradient[i + 1].Color,
                static_cast<float>(Alpha)
            );

            return Interpolated.ToFColor(false);
        }
    }

    // Fallback (should never hit)
    return FColor::Magenta;
}

} // namespace PlanetaryCreation::Heightmap

