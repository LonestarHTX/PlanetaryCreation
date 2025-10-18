#include "Simulation/TriangulationCache.h"

#include "Algo/Sort.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"

DEFINE_LOG_CATEGORY_STATIC(LogTriangulationCache, Log, All);

namespace
{
    constexpr uint32 CacheMagic = 0x41524954; // 'TRIA'
    constexpr uint32 CacheVersion = 1;

    FString GetDefaultCacheDir()
    {
        return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tests"), TEXT("TriangulationCache"));
    }

    FString BuildCacheFilename(const TriCache::FTriangulationMeta& Meta)
    {
        return FString::Printf(TEXT("Fibonacci_%d_seed%d_shuffle%d.bin"), Meta.N, Meta.Seed, Meta.bShuffle ? 1 : 0);
    }

    FString BuildCachePath(const FString& CacheDir, const TriCache::FTriangulationMeta& Meta)
    {
        const FString BaseDir = CacheDir.IsEmpty() ? GetDefaultCacheDir() : CacheDir;
        const FString AbsoluteDir = FPaths::ConvertRelativePathToFull(BaseDir);
        return FPaths::Combine(AbsoluteDir, BuildCacheFilename(Meta));
    }

    void MakeCanonicalOrdering(FSphericalDelaunay::FTriangle& Triangle)
    {
        const int32 Values[3] = {Triangle.V0, Triangle.V1, Triangle.V2};

        int32 MinIndex = 0;
        int32 MinValue = Values[0];
        for (int32 Index = 1; Index < 3; ++Index)
        {
            if (Values[Index] < MinValue)
            {
                MinValue = Values[Index];
                MinIndex = Index;
            }
        }

        if (MinIndex == 0)
        {
            return;
        }

        int32 Ordered[3];
        Ordered[0] = Values[MinIndex];
        Ordered[1] = Values[(MinIndex + 1) % 3];
        Ordered[2] = Values[(MinIndex + 2) % 3];

        Triangle.V0 = Ordered[0];
        Triangle.V1 = Ordered[1];
        Triangle.V2 = Ordered[2];
    }

    int32 TriangleMin(const FSphericalDelaunay::FTriangle& Triangle)
    {
        return FMath::Min3(Triangle.V0, Triangle.V1, Triangle.V2);
    }

    int32 TriangleMax(const FSphericalDelaunay::FTriangle& Triangle)
    {
        return FMath::Max3(Triangle.V0, Triangle.V1, Triangle.V2);
    }

    int32 TriangleMid(const FSphericalDelaunay::FTriangle& Triangle)
    {
        return Triangle.V0 + Triangle.V1 + Triangle.V2 - TriangleMin(Triangle) - TriangleMax(Triangle);
    }

    void SortCanonicalTriangles(TArray<FSphericalDelaunay::FTriangle>& InOutTriangles)
    {
        InOutTriangles.Sort([](const FSphericalDelaunay::FTriangle& A, const FSphericalDelaunay::FTriangle& B)
        {
            const int32 MinA = TriangleMin(A);
            const int32 MinB = TriangleMin(B);
            if (MinA != MinB)
            {
                return MinA < MinB;
            }

            const int32 MidA = TriangleMid(A);
            const int32 MidB = TriangleMid(B);
            if (MidA != MidB)
            {
                return MidA < MidB;
            }

            const int32 MaxA = TriangleMax(A);
            const int32 MaxB = TriangleMax(B);
            if (MaxA != MaxB)
            {
                return MaxA < MaxB;
            }

            if (A.V0 != B.V0)
            {
                return A.V0 < B.V0;
            }

            if (A.V1 != B.V1)
            {
                return A.V1 < B.V1;
            }

            return A.V2 < B.V2;
        });
    }

    void ResetOutputs(TArray<FVector3d>& OutPoints, TArray<FSphericalDelaunay::FTriangle>& OutTris, TriCache::FTriangulationMeta& OutMeta)
    {
        OutPoints.Reset();
        OutTris.Reset();
        OutMeta = {};
    }
}

namespace TriCache
{
    static TAutoConsoleVariable<int32> CVarPaperTriangulationUseCache(
        TEXT("r.PaperTriangulation.UseCache"),
        1,
        TEXT("Enable loading and saving STRIPACK triangulations to disk for re-use in tests."),
        ECVF_Default);

    static FString GPaperTriangulationCacheDir = GetDefaultCacheDir();
    static FAutoConsoleVariableRef CVarPaperTriangulationCacheDir(
        TEXT("r.PaperTriangulation.CacheDir"),
        GPaperTriangulationCacheDir,
        TEXT("Directory used for STRIPACK triangulation cache files."),
        ECVF_Default);

    void CanonicalizeTriangles(TArray<FSphericalDelaunay::FTriangle>& InOutTriangles)
    {
        for (FSphericalDelaunay::FTriangle& Triangle : InOutTriangles)
        {
            MakeCanonicalOrdering(Triangle);
        }

        SortCanonicalTriangles(InOutTriangles);
    }

    uint64 ComputeTriangleSetHash(const TArray<FSphericalDelaunay::FTriangle>& CanonicalTris)
    {
        constexpr uint64 FnvOffset = 14695981039346656037ull;
        constexpr uint64 FnvPrime = 1099511628211ull;
        uint64 Hash = FnvOffset;

        TArray<FIntVector> CanonicalTriples;
        CanonicalTriples.Reserve(CanonicalTris.Num());

        for (const FSphericalDelaunay::FTriangle& Triangle : CanonicalTris)
        {
            const int32 MinIndex = TriangleMin(Triangle);
            const int32 MaxIndex = TriangleMax(Triangle);
            const int32 MidIndex = TriangleMid(Triangle);
            CanonicalTriples.Emplace(MinIndex, MidIndex, MaxIndex);
        }

        CanonicalTriples.Sort([](const FIntVector& A, const FIntVector& B)
        {
            if (A.X != B.X)
            {
                return A.X < B.X;
            }

            if (A.Y != B.Y)
            {
                return A.Y < B.Y;
            }

            return A.Z < B.Z;
        });

        for (const FIntVector& Triple : CanonicalTriples)
        {
            const int32 Values[3] = {Triple.X, Triple.Y, Triple.Z};
            for (int32 Value : Values)
            {
                uint32 Bytes = static_cast<uint32>(Value);
                for (int32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
                {
                    const uint8 Byte = static_cast<uint8>((Bytes >> (ByteIndex * 8)) & 0xFF);
                    Hash ^= static_cast<uint64>(Byte);
                    Hash *= FnvPrime;
                }
            }
        }

        return Hash;
    }

    bool Load(const FString& CacheDir,
        const FTriangulationMeta& Key,
        TArray<FVector3d>& OutPoints,
        TArray<FSphericalDelaunay::FTriangle>& OutTris,
        FTriangulationMeta& OutMeta,
        double& OutLoadSeconds)
    {
        ResetOutputs(OutPoints, OutTris, OutMeta);
        OutLoadSeconds = 0.0;

        if (CVarPaperTriangulationUseCache.GetValueOnAnyThread() == 0)
        {
            UE_LOG(LogTriangulationCache, Verbose, TEXT("Triangulation cache disabled via r.PaperTriangulation.UseCache"));
            return false;
        }

        const FString CachePath = BuildCachePath(CacheDir, Key);
        if (!FPaths::FileExists(CachePath))
        {
            return false;
        }

        const double StartTime = FPlatformTime::Seconds();

        TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*CachePath));
        if (!Reader)
        {
            UE_LOG(LogTriangulationCache, Warning, TEXT("Failed to open triangulation cache file for reading: %s"), *CachePath);
            return false;
        }

        auto FailWithCleanup = [&OutPoints, &OutTris, &OutMeta, &Reader]()
        {
            if (Reader)
            {
                Reader->Close();
            }
            ResetOutputs(OutPoints, OutTris, OutMeta);
            return false;
        };

        uint32 Magic = 0;
        (*Reader) << Magic;
        if (Magic != CacheMagic)
        {
            UE_LOG(LogTriangulationCache, Warning, TEXT("Triangulation cache magic mismatch in %s"), *CachePath);
            return FailWithCleanup();
        }

        uint32 Version = 0;
        (*Reader) << Version;
        if (Version != CacheVersion)
        {
            UE_LOG(LogTriangulationCache, Warning, TEXT("Triangulation cache version mismatch in %s (found %u expected %u)"),
                *CachePath, Version, CacheVersion);
            return FailWithCleanup();
        }

        int32 FileN = 0;
        int32 FileSeed = 0;
        int32 FileShuffleInt = 0;
        uint64 FileSignature = 0;
        (*Reader) << FileN;
        (*Reader) << FileSeed;
        (*Reader) << FileShuffleInt;
        (*Reader) << FileSignature;

        if (FileN <= 0)
        {
            UE_LOG(LogTriangulationCache, Warning, TEXT("Triangulation cache has invalid point count (%d) in %s"), FileN, *CachePath);
            return FailWithCleanup();
        }

        int32 NumPoints = 0;
        int32 NumTriangles = 0;
        (*Reader) << NumPoints;
        (*Reader) << NumTriangles;

        if (NumPoints != FileN)
        {
            UE_LOG(LogTriangulationCache, Warning, TEXT("Triangulation cache mismatch: header N=%d but NumPoints=%d in %s"),
                FileN, NumPoints, *CachePath);
            return FailWithCleanup();
        }

        if (NumPoints != Key.N || FileSeed != Key.Seed || (FileShuffleInt != 0) != Key.bShuffle)
        {
            UE_LOG(LogTriangulationCache, Warning, TEXT("Triangulation cache key mismatch for %s (requested N=%d Seed=%d Shuffle=%d, file N=%d Seed=%d Shuffle=%d)"),
                *CachePath,
                Key.N, Key.Seed, Key.bShuffle ? 1 : 0,
                FileN, FileSeed, FileShuffleInt);
            return FailWithCleanup();
        }

        if (NumTriangles < 0)
        {
            UE_LOG(LogTriangulationCache, Warning, TEXT("Triangulation cache reported negative triangle count (%d) in %s"),
                NumTriangles, *CachePath);
            return FailWithCleanup();
        }

        OutPoints.SetNum(NumPoints);
        for (FVector3d& Point : OutPoints)
        {
            double Components[3] = {0.0, 0.0, 0.0};
            Reader->Serialize(Components, sizeof(double) * 3);
            Point.X = Components[0];
            Point.Y = Components[1];
            Point.Z = Components[2];
        }

        OutTris.SetNum(NumTriangles);
        for (FSphericalDelaunay::FTriangle& Triangle : OutTris)
        {
            int32 Indices[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};
            Reader->Serialize(Indices, sizeof(int32) * 3);
            Triangle.V0 = Indices[0];
            Triangle.V1 = Indices[1];
            Triangle.V2 = Indices[2];
        }

        if (Reader->IsError())
        {
            UE_LOG(LogTriangulationCache, Warning, TEXT("Error while reading triangulation cache file %s"), *CachePath);
            return FailWithCleanup();
        }

        Reader->Close();
        CanonicalizeTriangles(OutTris);

        const uint64 ComputedSignature = ComputeTriangleSetHash(OutTris);
        if (FileSignature != 0 && ComputedSignature != FileSignature)
        {
            UE_LOG(LogTriangulationCache, Warning, TEXT("Loaded triangulation signature mismatch for %s (file=0x%016llX computed=0x%016llX)"),
                *CachePath,
                static_cast<unsigned long long>(FileSignature),
                static_cast<unsigned long long>(ComputedSignature));
        }

        OutMeta.N = FileN;
        OutMeta.Seed = FileSeed;
        OutMeta.bShuffle = (FileShuffleInt != 0);
        OutMeta.Signature = (FileSignature != 0) ? FileSignature : ComputedSignature;

        OutLoadSeconds = FPlatformTime::Seconds() - StartTime;
        UE_LOG(LogTriangulationCache, Display, TEXT("Loaded triangulation cache (%d pts, %d tris) from %s in %.3f s"),
            NumPoints, NumTriangles, *CachePath, OutLoadSeconds);

        return true;
    }

    bool Save(const FString& CacheDir,
        const FTriangulationMeta& Meta,
        const TArray<FVector3d>& Points,
        const TArray<FSphericalDelaunay::FTriangle>& Tris,
        FString& OutPath,
        double& OutSaveSeconds)
    {
        OutPath.Reset();
        OutSaveSeconds = 0.0;

        if (CVarPaperTriangulationUseCache.GetValueOnAnyThread() == 0)
        {
            UE_LOG(LogTriangulationCache, Verbose, TEXT("Skipping triangulation cache save (cache disabled)"));
            return false;
        }

        if (Points.Num() <= 0 || Tris.Num() <= 0)
        {
            UE_LOG(LogTriangulationCache, Warning, TEXT("Cannot save empty triangulation cache (points=%d tris=%d)"),
                Points.Num(), Tris.Num());
            return false;
        }

        FTriangulationMeta MetaToWrite = Meta;
        MetaToWrite.N = Points.Num();

        TArray<FSphericalDelaunay::FTriangle> CanonicalTris = Tris;
        CanonicalizeTriangles(CanonicalTris);

        const uint64 ComputedSignature = ComputeTriangleSetHash(CanonicalTris);
        if (MetaToWrite.Signature != 0 && MetaToWrite.Signature != ComputedSignature)
        {
            UE_LOG(LogTriangulationCache, Warning, TEXT("Supplied triangulation signature (0x%016llX) does not match computed signature (0x%016llX); using computed value."),
                static_cast<unsigned long long>(MetaToWrite.Signature),
                static_cast<unsigned long long>(ComputedSignature));
        }
        MetaToWrite.Signature = ComputedSignature;

        OutPath = BuildCachePath(CacheDir, MetaToWrite);
        const FString CacheDirectory = FPaths::GetPath(OutPath);
        IFileManager::Get().MakeDirectory(*CacheDirectory, true);

        const double StartTime = FPlatformTime::Seconds();
        TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*OutPath));
        if (!Writer)
        {
            UE_LOG(LogTriangulationCache, Warning, TEXT("Failed to create triangulation cache file for writing: %s"), *OutPath);
            return false;
        }

        uint32 Magic = CacheMagic;
        (*Writer) << Magic;

        uint32 Version = CacheVersion;
        (*Writer) << Version;

        int32 NValue = MetaToWrite.N;
        (*Writer) << NValue;

        int32 SeedValue = MetaToWrite.Seed;
        (*Writer) << SeedValue;

        int32 ShuffleValue = MetaToWrite.bShuffle ? 1 : 0;
        (*Writer) << ShuffleValue;

        uint64 SignatureValue = MetaToWrite.Signature;
        (*Writer) << SignatureValue;

        int32 NumPoints = Points.Num();
        int32 NumTris = CanonicalTris.Num();
        (*Writer) << NumPoints;
        (*Writer) << NumTris;

        for (const FVector3d& Point : Points)
        {
            double Components[3] = {Point.X, Point.Y, Point.Z};
            Writer->Serialize(Components, sizeof(double) * 3);
        }

        for (const FSphericalDelaunay::FTriangle& Triangle : CanonicalTris)
        {
            int32 Indices[3] = {Triangle.V0, Triangle.V1, Triangle.V2};
            Writer->Serialize(Indices, sizeof(int32) * 3);
        }

        Writer->Close();
        if (Writer->IsError())
        {
            UE_LOG(LogTriangulationCache, Warning, TEXT("Error encountered while writing triangulation cache file %s"), *OutPath);
            IFileManager::Get().Delete(*OutPath);
            return false;
        }

        OutSaveSeconds = FPlatformTime::Seconds() - StartTime;
        UE_LOG(LogTriangulationCache, Display, TEXT("Saved triangulation cache (%d pts, %d tris) to %s in %.3f s"),
            NumPoints, NumTris, *OutPath, OutSaveSeconds);

        return true;
    }
}
