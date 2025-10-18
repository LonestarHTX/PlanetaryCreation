#include "Simulation/Triangulators/GeogramTriangulator.h"

#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopeLock.h"
#include "Misc/Char.h"
#include <string>
#include <vector>

#if WITH_GEOGRAM
#include <geogram/basic/common.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/attributes.h>
#include <geogram/basic/file_system.h>
#include <geogram/basic/logger.h>
#include <geogram/basic/process.h>
#include <geogram/basic/progress.h>
#include <geogram/delaunay/delaunay.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_convex_hull.h>
#include <geogram/numerics/predicates.h>
#endif // WITH_GEOGRAM

DEFINE_LOG_CATEGORY_STATIC(LogGeogramTriangulator, Log, All);

FGeogramTriangulator* FGeogramTriangulator::Singleton = nullptr;
FCriticalSection FGeogramTriangulator::SingletonMutex;
bool FGeogramTriangulator::bIsInitialized = false;
bool FGeogramTriangulator::bInitializeAttempted = false;

FGeogramTriangulator& FGeogramTriangulator::Get()
{
    if (!Singleton)
    {
        FScopeLock Lock(&SingletonMutex);
        if (!Singleton)
        {
            Singleton = new FGeogramTriangulator();
        }
    }
    return *Singleton;
}

void FGeogramTriangulator::Startup()
{
    EnsureGeogramInitialized();
}

void FGeogramTriangulator::Shutdown()
{
#if WITH_GEOGRAM
    FScopeLock Lock(&SingletonMutex);
    if (bIsInitialized)
    {
        GEO::terminate();
        bIsInitialized = false;
    }
#endif

    if (Singleton)
    {
        delete Singleton;
        Singleton = nullptr;
    }
}

bool FGeogramTriangulator::IsAvailable()
{
#if WITH_GEOGRAM
    return EnsureGeogramInitialized();
#else
    return false;
#endif
}

FString FGeogramTriangulator::GetName() const
{
    return TEXT("Geogram");
}

bool FGeogramTriangulator::Triangulate(const TArray<FVector3d>& Points, TArray<FSphericalDelaunay::FTriangle>& OutTriangles)
{
    OutTriangles.Reset();

#if !WITH_GEOGRAM
    UE_LOG(LogGeogramTriangulator, Warning, TEXT("Geogram backend requested but WITH_GEOGRAM=0. Install Geogram libs to enable this backend."));
    return false;
#else
    if (!EnsureGeogramInitialized())
    {
        UE_LOG(LogGeogramTriangulator, Warning, TEXT("Failed to initialize Geogram runtime. Falling back to other backends."));
        return false;
    }

    if (Points.Num() < 3)
    {
        return false;
    }

    const double StartTime = FPlatformTime::Seconds();

    std::vector<double> Packed;
    Packed.reserve(static_cast<size_t>(Points.Num()) * 3ull);

    for (const FVector3d& Point : Points)
    {
        Packed.push_back(Point.X);
        Packed.push_back(Point.Y);
        Packed.push_back(Point.Z);
    }

    const double PackEnd = FPlatformTime::Seconds();

    const bool bSuccess = RunTriangulation(Points, Packed, OutTriangles);

    const double HullEnd = FPlatformTime::Seconds();

    const double PackMs = (PackEnd - StartTime) * 1000.0;
    const double HullMs = (HullEnd - PackEnd) * 1000.0;
    const double TotalMs = (HullEnd - StartTime) * 1000.0;
    const double PackedBytes = static_cast<double>(Packed.size() * sizeof(double));

    UE_LOG(LogGeogramTriangulator, Display,
        TEXT("Geogram Triangulate: Points=%d Tris=%d Pack=%.2f ms Hull=%.2f ms Total=%.2f ms Packed=%.1f MB"),
        Points.Num(), OutTriangles.Num(), PackMs, HullMs, TotalMs, PackedBytes / (1024.0 * 1024.0));

    return bSuccess;
#endif // WITH_GEOGRAM
}

bool FGeogramTriangulator::EnsureGeogramInitialized()
{
#if !WITH_GEOGRAM
    return false;
#else
    if (bIsInitialized)
    {
        return true;
    }

    if (bInitializeAttempted && !bIsInitialized)
    {
        return false;
    }

    FScopeLock Lock(&SingletonMutex);
    if (bIsInitialized)
    {
        return true;
    }

    bInitializeAttempted = true;

    GEO::initialize(GEO::GEOGRAM_INSTALL_NONE);
    // Force single-threaded execution for determinism in tests
    GEO::Process::set_thread_manager(new GEO::MonoThreadingThreadManager());
    GEO::CmdLine::set_arg("geogram:log_to_stderr", "false");
    GEO::CmdLine::set_arg("geogram:log_file", ""); // disable file redirection
    bIsInitialized = true;

    UE_LOG(LogGeogramTriangulator, Log, TEXT("Geogram runtime initialized"));

    return true;
#endif // WITH_GEOGRAM
}

bool FGeogramTriangulator::RunTriangulation(const TArray<FVector3d>& Points, const std::vector<double>& Packed, TArray<FSphericalDelaunay::FTriangle>& OutTriangles)
{
#if !WITH_GEOGRAM
    return false;
#else
    GEO::Mesh Mesh;
    Mesh.vertices.set_dimension(3);
    Mesh.vertices.assign_points(Packed.data(), 3, static_cast<GEO::index_t>(Points.Num()));

    GEO::Attribute<GEO::index_t> SourceIndex(Mesh.vertices.attributes(), "PlanetarySourceIndex");
    for (GEO::index_t Index = 0; Index < Mesh.vertices.nb(); ++Index)
    {
        SourceIndex[Index] = Index;
    }

    GEO::compute_convex_hull_3d(Mesh);

    const GEO::index_t FacetCount = Mesh.facets.nb();
    OutTriangles.Reserve(static_cast<int32>(FacetCount));

    for (GEO::index_t FacetIndex = 0; FacetIndex < FacetCount; ++FacetIndex)
    {
        const GEO::index_t VertexCount = Mesh.facets.nb_vertices(FacetIndex);
        if (VertexCount != 3)
        {
            continue;
        }

        FSphericalDelaunay::FTriangle Triangle;
        bool bValid = true;

        for (int32 LocalVertex = 0; LocalVertex < 3; ++LocalVertex)
        {
            const GEO::index_t MeshVertex = Mesh.facets.vertex(FacetIndex, static_cast<GEO::index_t>(LocalVertex));
            if (MeshVertex == GEO::NO_VERTEX || MeshVertex >= Mesh.vertices.nb())
            {
                bValid = false;
                break;
            }

            const GEO::index_t OriginalIndex = SourceIndex[MeshVertex];
            if (OriginalIndex == GEO::NO_VERTEX || OriginalIndex >= static_cast<GEO::index_t>(Points.Num()))
            {
                bValid = false;
                break;
            }

            switch (LocalVertex)
            {
            case 0: Triangle.V0 = static_cast<int32>(OriginalIndex); break;
            case 1: Triangle.V1 = static_cast<int32>(OriginalIndex); break;
            case 2: Triangle.V2 = static_cast<int32>(OriginalIndex); break;
            default: break;
            }
        }

        if (bValid)
        {
            OutTriangles.Add(Triangle);
        }
    }

    return OutTriangles.Num() > 0;
#endif // WITH_GEOGRAM
}
