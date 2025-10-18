#include "Simulation/StripackWrapper.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"
#include "Math/RandomStream.h"

#if WITH_STRIPACK

extern "C" void stripack_triangulate(int32 n, const double* xyz, int32* ntri, int32* tri);

static TAutoConsoleVariable<int32> CVarPaperTriangulationShuffle(
    TEXT("r.PaperTriangulation.Shuffle"),
    1,
    TEXT("Enable deterministic shuffle before STRIPACK triangulation (1 = enabled, 0 = disabled)."),
    ECVF_Default);

static TAutoConsoleVariable<int32> CVarPaperTriangulationShuffleSeed(
    TEXT("r.PaperTriangulation.ShuffleSeed"),
    42,
    TEXT("Seed used for deterministic shuffle of STRIPACK input points."),
    ECVF_Default);

namespace Stripack
{
    static uint64 HashIntArray(const TArray<int32>& Values)
    {
        constexpr uint64 FnvOffset = 14695981039346656037ull;
        constexpr uint64 FnvPrime = 1099511628211ull;
        uint64 Hash = FnvOffset;

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

        return Hash;
    }

    struct FShuffledMapping
    {
        TArray<int32> ShuffledToOriginal;
        TArray<int32> OriginalToShuffled;
    };

    static FShuffledMapping BuildShuffleMapping(int32 N, int32 Seed)
    {
        FShuffledMapping Mapping;
        Mapping.ShuffledToOriginal.SetNumUninitialized(N);
        Mapping.OriginalToShuffled.SetNumUninitialized(N);

        for (int32 Index = 0; Index < N; ++Index)
        {
            Mapping.ShuffledToOriginal[Index] = Index;
        }

        FRandomStream RandomStream(Seed);
        for (int32 Index = N - 1; Index > 0; --Index)
        {
            const int32 SwapIndex = RandomStream.RandRange(0, Index);
            if (SwapIndex != Index)
            {
                Swap(Mapping.ShuffledToOriginal[Index], Mapping.ShuffledToOriginal[SwapIndex]);
            }
        }

        for (int32 ShuffledIndex = 0; ShuffledIndex < N; ++ShuffledIndex)
        {
            const int32 OriginalIndex = Mapping.ShuffledToOriginal[ShuffledIndex];
            Mapping.OriginalToShuffled[OriginalIndex] = ShuffledIndex;
        }

#if DO_CHECK
        static bool bHasPrevious = false;
        static int32 PreviousSeed = 0;
        static int32 PreviousCount = 0;
        static TArray<int32> PreviousShuffledToOriginal;
        if (bHasPrevious && Seed == PreviousSeed && PreviousCount == N && PreviousShuffledToOriginal.Num() == N)
        {
            bool bMatches = true;
            for (int32 Index = 0; Index < N; ++Index)
            {
                if (PreviousShuffledToOriginal[Index] != Mapping.ShuffledToOriginal[Index])
                {
                    bMatches = false;
                    break;
                }
            }
            if (!bMatches)
            {
                UE_LOG(LogTemp, Warning, TEXT("STRIPACK: shuffle mapping mismatch across runs (Seed=%d N=%d)"), Seed, N);
            }
        }

        PreviousShuffledToOriginal = Mapping.ShuffledToOriginal;
        PreviousSeed = Seed;
        PreviousCount = N;
        bHasPrevious = true;
#endif

        return Mapping;
    }

    bool ComputeTriangulation(const TArray<FVector3d>& SpherePoints, TArray<FSphericalDelaunay::FTriangle>& OutTriangles)
    {
        const int32 Num = SpherePoints.Num();
        OutTriangles.Reset();
        if (Num < 3)
        {
            return false;
        }

        const bool bEnableShuffle = CVarPaperTriangulationShuffle.GetValueOnAnyThread() != 0;
        const int32 ShuffleSeed = CVarPaperTriangulationShuffleSeed.GetValueOnAnyThread();

        FShuffledMapping Mapping;
        if (bEnableShuffle)
        {
            Mapping = BuildShuffleMapping(Num, ShuffleSeed);
            const uint64 MappingHash = HashIntArray(Mapping.ShuffledToOriginal);
            UE_LOG(LogTemp, Verbose, TEXT("STRIPACK: shuffle seed=%d count=%d hash=%016llX"),
                ShuffleSeed, Num, static_cast<unsigned long long>(MappingHash));
        }

        // Build column-major xyz(3,n) buffer for Fortran side.
        TArray<double> XYZ;
        XYZ.SetNumUninitialized(3 * Num);
        for (int32 i = 0; i < Num; ++i)
        {
            const int32 SourceIndex = bEnableShuffle ? Mapping.ShuffledToOriginal[i] : i;
            const FVector3d& P = SpherePoints[SourceIndex];
            XYZ[3 * i + 0] = P.X;
            XYZ[3 * i + 1] = P.Y;
            XYZ[3 * i + 2] = P.Z;
        }

        // Triangle list upper bound ~ 2N per Euler (for closed sphere without boundary).
        const int32 MaxTri = FMath::Max(2 * Num, 16);
        TArray<int32> TriBuf;
        TriBuf.SetNumUninitialized(3 * MaxTri);

        UE_LOG(LogTemp, Display, TEXT("STRIPACK: calling stripack_triangulate(N=%d, bShuffle=%d, MaxTri=%d)"),
            Num, bEnableShuffle ? 1 : 0, MaxTri);
        const double TriangulateStartTime = FPlatformTime::Seconds();

        int32 NTri = 0;
        stripack_triangulate(Num, XYZ.GetData(), &NTri, TriBuf.GetData());

        const double TriangulateDuration = FPlatformTime::Seconds() - TriangulateStartTime;
        UE_LOG(LogTemp, Display, TEXT("STRIPACK: stripack_triangulate returned in %.3f s (NTri=%d)"),
            TriangulateDuration, NTri);

        if (NTri <= 0 || NTri > MaxTri)
        {
            UE_LOG(LogTemp, Warning, TEXT("STRIPACK: invalid NTri=%d (expected > 0 and <= %d)"), NTri, MaxTri);
            return false;
        }

        OutTriangles.Reserve(NTri);
        for (int32 i = 0; i < NTri; ++i)
        {
            const int32 a = TriBuf[3 * i + 0];
            const int32 b = TriBuf[3 * i + 1];
            const int32 c = TriBuf[3 * i + 2];

            if (a < 0 || a >= Num || b < 0 || b >= Num || c < 0 || c >= Num)
            {
                continue;
            }

            const int32 OriginalA = bEnableShuffle ? Mapping.ShuffledToOriginal[a] : a;
            const int32 OriginalB = bEnableShuffle ? Mapping.ShuffledToOriginal[b] : b;
            const int32 OriginalC = bEnableShuffle ? Mapping.ShuffledToOriginal[c] : c;

            if (SpherePoints.IsValidIndex(OriginalA) && SpherePoints.IsValidIndex(OriginalB) && SpherePoints.IsValidIndex(OriginalC))
            {
                OutTriangles.Add({OriginalA, OriginalB, OriginalC});
            }
        }

        return OutTriangles.Num() > 0;
    }
}

#endif // WITH_STRIPACK
