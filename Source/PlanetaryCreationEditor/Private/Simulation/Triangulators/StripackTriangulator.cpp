#include "Simulation/Triangulators/StripackTriangulator.h"

#include "Logging/LogMacros.h"
#include "Misc/ScopeLock.h"
#include "Simulation/StripackWrapper.h"

DEFINE_LOG_CATEGORY_STATIC(LogStripackTriangulator, Log, All);

FStripackTriangulator* FStripackTriangulator::Singleton = nullptr;
FCriticalSection FStripackTriangulator::SingletonMutex;

FStripackTriangulator& FStripackTriangulator::Get()
{
    if (!Singleton)
    {
        FScopeLock Lock(&SingletonMutex);
        if (!Singleton)
        {
            Singleton = new FStripackTriangulator();
        }
    }
    return *Singleton;
}

FString FStripackTriangulator::GetName() const
{
    return TEXT("Stripack");
}

bool FStripackTriangulator::IsAvailable()
{
#if WITH_STRIPACK
    return true;
#else
    return false;
#endif
}

bool FStripackTriangulator::Triangulate(const TArray<FVector3d>& Points, TArray<FSphericalDelaunay::FTriangle>& OutTriangles)
{
    OutTriangles.Reset();

#if WITH_STRIPACK
    return Stripack::ComputeTriangulation(Points, OutTriangles);
#else
    UE_LOG(LogStripackTriangulator, Warning, TEXT("STRIPACK backend requested but WITH_STRIPACK=0. Install STRIPACK to enable this backend."));
    return false;
#endif
}
