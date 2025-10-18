#pragma once

#include "CoreMinimal.h"
#include "Simulation/ISphericalTriangulator.h"
#include "Simulation/StripackConfig.h"

/**
 * STRIPACK-backed spherical triangulation.
 */
class FStripackTriangulator final : public ISphericalTriangulator
{
public:
    static FStripackTriangulator& Get();
    static bool IsAvailable();

    // ISphericalTriangulator interface
    virtual FString GetName() const override;
    virtual bool Triangulate(const TArray<FVector3d>& Points, TArray<FSphericalDelaunay::FTriangle>& OutTriangles) override;

private:
    FStripackTriangulator() = default;

    static FStripackTriangulator* Singleton;
    static FCriticalSection SingletonMutex;
};
