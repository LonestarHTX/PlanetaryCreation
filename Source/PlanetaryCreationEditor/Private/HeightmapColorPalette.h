#pragma once

#include "CoreMinimal.h"

namespace PlanetaryCreation::Heightmap
{

/**
 * Convert a normalized height value in the range [0, 1] to a debug color.
 * Matches the gradient used by the heightmap exporter so in-editor previews
 * line up with exported PNG diagnostics.
 */
inline FColor MakeElevationColor(double NormalizedHeight)
{
    NormalizedHeight = FMath::Clamp(NormalizedHeight, 0.0, 1.0);

    // Multi-stop gradient: Blue → Cyan → Green → Yellow → Red.
    if (NormalizedHeight < 0.25)
    {
        const double T = NormalizedHeight / 0.25;
        return FColor(
            0,
            static_cast<uint8>(FMath::Lerp(0.0, 255.0, T)),
            255);
    }

    if (NormalizedHeight < 0.5)
    {
        const double T = (NormalizedHeight - 0.25) / 0.25;
        return FColor(
            0,
            255,
            static_cast<uint8>(FMath::Lerp(255.0, 0.0, T)));
    }

    if (NormalizedHeight < 0.75)
    {
        const double T = (NormalizedHeight - 0.5) / 0.25;
        return FColor(
            static_cast<uint8>(FMath::Lerp(0.0, 255.0, T)),
            255,
            0);
    }

    const double T = (NormalizedHeight - 0.75) / 0.25;
    return FColor(
        255,
        static_cast<uint8>(FMath::Lerp(255.0, 0.0, T)),
        0);
}

} // namespace PlanetaryCreation::Heightmap

