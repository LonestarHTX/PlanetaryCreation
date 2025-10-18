#pragma once

#include "CoreMinimal.h"
#include "Simulation/ISphericalTriangulator.h"
#include "Simulation/GeogramConfig.h"

#include <vector>

class FOutputDevice;

/**
 * Geogram-backed spherical triangulation. Available when WITH_GEOGRAM is true.
 */
class FGeogramTriangulator final : public ISphericalTriangulator
{
public:
    static FGeogramTriangulator& Get();

    static void Startup();
    static void Shutdown();
    static bool IsAvailable();

    // ISphericalTriangulator interface
    virtual FString GetName() const override;
    virtual bool Triangulate(const TArray<FVector3d>& Points, TArray<FSphericalDelaunay::FTriangle>& OutTriangles) override;

private:
    FGeogramTriangulator() = default;

    static FGeogramTriangulator* Singleton;
    static FCriticalSection SingletonMutex;
    static bool bIsInitialized;
    static bool bInitializeAttempted;

    static bool EnsureGeogramInitialized();

    bool RunTriangulation(const TArray<FVector3d>& Points, const std::vector<double>& Packed, TArray<FSphericalDelaunay::FTriangle>& OutTriangles);
};
