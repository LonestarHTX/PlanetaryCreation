#pragma once

#include "CoreMinimal.h"

class ISphericalTriangulator;

/**
 * Factory that resolves the active spherical triangulation backend.
 */
class FSphericalTriangulatorFactory
{
public:
    /**
     * Resolves the backend configured by r.PaperTriangulation.Backend.
     * @param OutBackendName Receives the backend actually returned (Geogram/Stripack/None).
     * @param OutUsedFallback True if the requested backend was unavailable and a fallback was chosen.
     * @return Reference to the resolved triangulator (never null).
     */
    static ISphericalTriangulator& Resolve(FString& OutBackendName, bool& OutUsedFallback);

    /** Returns the raw console-variable value (for diagnostics/UI). */
    static FString GetConfiguredBackend();
};
