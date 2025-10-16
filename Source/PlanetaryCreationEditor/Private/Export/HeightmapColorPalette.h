#pragma once

#include "CoreMinimal.h"

enum class EHeightmapPaletteMode : uint8;

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

struct FNormalizedStop
{
    double Position01;
    FLinearColor Color;
};

static const FNormalizedStop NormalizedGradient[] = {
    { 0.0,  FLinearColor(0.000f, 0.059f, 0.196f) }, // Deep blue
    { 0.25, FLinearColor(0.078f, 0.392f, 0.706f) }, // Mid ocean blue
    { 0.50, FLinearColor(0.431f, 0.784f, 0.392f) }, // Coastal green
    { 0.75, FLinearColor(0.863f, 0.784f, 0.235f) }, // Highlands yellow
    { 1.0,  FLinearColor(0.706f, 0.078f, 0.078f) }  // Peak red
};

namespace Detail
{
    inline FColor SampleHypsometric(double ElevationMeters)
    {
        constexpr int32 StopCount = UE_ARRAY_COUNT(HypsometricGradient);

        if (ElevationMeters <= HypsometricGradient[0].ElevationMeters)
        {
            return HypsometricGradient[0].Color.ToFColor(false);
        }
        if (ElevationMeters >= HypsometricGradient[StopCount - 1].ElevationMeters)
        {
            return HypsometricGradient[StopCount - 1].Color.ToFColor(false);
        }

        for (int32 Index = 0; Index < StopCount - 1; ++Index)
        {
            const double ElevLow = HypsometricGradient[Index].ElevationMeters;
            const double ElevHigh = HypsometricGradient[Index + 1].ElevationMeters;

            if (ElevationMeters >= ElevLow && ElevationMeters <= ElevHigh)
            {
                const double Range = ElevHigh - ElevLow;
                const double Alpha = (Range > KINDA_SMALL_NUMBER)
                    ? (ElevationMeters - ElevLow) / Range
                    : 0.0;

                const FLinearColor Interpolated = FMath::Lerp(
                    HypsometricGradient[Index].Color,
                    HypsometricGradient[Index + 1].Color,
                    static_cast<float>(Alpha));

                return Interpolated.ToFColor(false);
            }
        }

        return FColor::Magenta;
    }

    inline FColor SampleNormalized(double NormalizedValue)
    {
        constexpr int32 StopCount = UE_ARRAY_COUNT(NormalizedGradient);

        const double Clamped = FMath::Clamp(NormalizedValue, 0.0, 1.0);

        if (Clamped <= NormalizedGradient[0].Position01)
        {
            return NormalizedGradient[0].Color.ToFColor(false);
        }
        if (Clamped >= NormalizedGradient[StopCount - 1].Position01)
        {
            return NormalizedGradient[StopCount - 1].Color.ToFColor(false);
        }

        for (int32 Index = 0; Index < StopCount - 1; ++Index)
        {
            const double Low = NormalizedGradient[Index].Position01;
            const double High = NormalizedGradient[Index + 1].Position01;

            if (Clamped >= Low && Clamped <= High)
            {
                const double Range = High - Low;
                const double Alpha = (Range > KINDA_SMALL_NUMBER)
                    ? (Clamped - Low) / Range
                    : 0.0;

                const FLinearColor Interpolated = FMath::Lerp(
                    NormalizedGradient[Index].Color,
                    NormalizedGradient[Index + 1].Color,
                    static_cast<float>(Alpha));

                return Interpolated.ToFColor(false);
            }
        }

        return FColor::Magenta;
    }
} // namespace Detail

/**
 * Palette wrapper shared by editor visualization and exporters. Construct per-frame with
 * the currently selected mode and elevation window, then call Sample() for each vertex/pixel.
 */
struct FHeightmapPalette
{
    FHeightmapPalette() = default;

    FHeightmapPalette(EHeightmapPaletteMode InMode, double InMinElevation, double InMaxElevation)
        : Mode(InMode)
        , MinElevation(InMinElevation)
        , MaxElevation(InMaxElevation)
    {
    }

    static FHeightmapPalette Absolute()
    {
        return FHeightmapPalette(EHeightmapPaletteMode::AbsoluteHypsometric, 0.0, 0.0);
    }

    static FHeightmapPalette Absolute(double MinElevation, double MaxElevation)
    {
        return FHeightmapPalette(EHeightmapPaletteMode::AbsoluteHypsometric, MinElevation, MaxElevation);
    }

    static FHeightmapPalette Normalized(double MinElevation, double MaxElevation)
    {
        return FHeightmapPalette(EHeightmapPaletteMode::NormalizedRange, MinElevation, MaxElevation);
    }

    static FHeightmapPalette FromMode(EHeightmapPaletteMode InMode, double MinElevation, double MaxElevation)
    {
        if (InMode == EHeightmapPaletteMode::NormalizedRange)
        {
            return Normalized(MinElevation, MaxElevation);
        }
        return Absolute(MinElevation, MaxElevation);
    }

    FColor Sample(double ElevationMeters) const
    {
        if (UsesNormalizedSampling())
        {
            const double Normalized = (ElevationMeters - MinElevation) / GetRange();
            return Detail::SampleNormalized(Normalized);
        }

        return Detail::SampleHypsometric(ElevationMeters);
    }

    bool UsesNormalizedSampling() const
    {
        return Mode == EHeightmapPaletteMode::NormalizedRange && CanSampleNormalized();
    }

    bool IsNormalizedRequested() const
    {
        return Mode == EHeightmapPaletteMode::NormalizedRange;
    }

    bool CanSampleNormalized() const
    {
        return GetRange() > KINDA_SMALL_NUMBER;
    }

    double GetMinElevation() const { return MinElevation; }
    double GetMaxElevation() const { return MaxElevation; }
    double GetRange() const { return MaxElevation - MinElevation; }
    EHeightmapPaletteMode GetMode() const { return Mode; }

private:
    EHeightmapPaletteMode Mode = EHeightmapPaletteMode::AbsoluteHypsometric;
    double MinElevation = 0.0;
    double MaxElevation = 0.0;
};

} // namespace PlanetaryCreation::Heightmap
