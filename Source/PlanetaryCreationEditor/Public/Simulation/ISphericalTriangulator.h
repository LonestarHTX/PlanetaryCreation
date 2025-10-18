#pragma once

#include "CoreMinimal.h"
#include "Simulation/SphericalDelaunay.h"

/**
 * Lightweight interface that abstracts spherical triangulation backends.
 */
class ISphericalTriangulator
{
public:
    virtual ~ISphericalTriangulator() = default;

    /** Returns the backend display name (e.g. Geogram, Stripack). */
    virtual FString GetName() const = 0;

    /**
     * Executes the triangulation.
     * @param Points       Unit-length input sample positions.
     * @param OutTriangles Receives outward-facing, canonicalized triangles.
     * @return true on success, false on failure.
     */
    virtual bool Triangulate(const TArray<FVector3d>& Points, TArray<FSphericalDelaunay::FTriangle>& OutTriangles) = 0;
};
