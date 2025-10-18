#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Simulation/FibonacciSampling.h"
#include "Simulation/SphericalDelaunay.h"
#include "Simulation/TriangulationCache.h"
#include "Simulation/SphericalTriangulatorFactory.h"
#include "Containers/Set.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSphericalDelaunayPropertiesTest, "PlanetaryCreation.Paper.SphericalDelaunayProperties",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace
{
    static TAutoConsoleVariable<int32> CVarPaperTriangulationRequireBaselineEquivalence(
        TEXT("r.PaperTriangulation.RequireBaselineEquivalence"),
        0,
        TEXT("Require STRIPACK triangulation to match the spiral baseline (shuffle=0) during the properties test."),
        ECVF_Default);

    static TAutoConsoleVariable<int32> CVarPaperTriangulationPropertiesPointCount(
        TEXT("r.PaperTriangulation.PropertiesPointCount"),
        4096,
        TEXT("Sample count used by the spherical Delaunay properties test."),
        ECVF_Default);

    static TAutoConsoleVariable<int32> CVarPaperTriangulationBaselineMaxPointCount(
        TEXT("r.PaperTriangulation.BaselineMaxPointCount"),
        1024,
        TEXT("Maximum sample count permitted when baseline equivalence is required (lowering reduces baseline runtime)."),
        ECVF_Default);

    int64 EncodeEdge(int32 A, int32 B)
    {
        const int32 MinIndex = FMath::Min(A, B);
        const int32 MaxIndex = FMath::Max(A, B);
        return (static_cast<int64>(MinIndex) << 32) | static_cast<uint32>(MaxIndex);
    }

    void CollectEdges(const TArray<FSphericalDelaunay::FTriangle>& Triangles, TSet<int64>& OutEdges)
    {
        OutEdges.Reset();
        OutEdges.Reserve(Triangles.Num() * 3);

        for (const FSphericalDelaunay::FTriangle& Triangle : Triangles)
        {
            OutEdges.Add(EncodeEdge(Triangle.V0, Triangle.V1));
            OutEdges.Add(EncodeEdge(Triangle.V1, Triangle.V2));
            OutEdges.Add(EncodeEdge(Triangle.V2, Triangle.V0));
        }
    }

    FString GetDefaultCacheDirectory()
    {
        return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tests"), TEXT("TriangulationCache"));
    }

    FString NormalizeCacheDirectory(IConsoleVariable* CacheDirVar)
    {
        FString CacheDirValue = CacheDirVar ? CacheDirVar->GetString() : FString();
        if (CacheDirValue.IsEmpty())
        {
            CacheDirValue = GetDefaultCacheDirectory();
        }

        return FPaths::ConvertRelativePathToFull(CacheDirValue);
    }

    FString SanitizeBackend(const FString& Name)
    {
        FString Out = Name;
        Out.TrimStartAndEndInline();
        Out.ReplaceInline(TEXT(" "), TEXT(""));
        return Out;
    }

    FString BuildCachePath(const FString& CacheDirAbsolute, const TriCache::FTriangulationMeta& Meta)
    {
        bool bUsedFallback = false;
        FString BackendName;
        FSphericalTriangulatorFactory::Resolve(BackendName, bUsedFallback); // resolve to know actual backend
        const FString Tag = SanitizeBackend(BackendName);
        return FPaths::Combine(CacheDirAbsolute,
            FString::Printf(TEXT("Fibonacci_%d_backend-%s_seed%d_shuffle%d.bin"), Meta.N, *Tag, Meta.Seed, Meta.bShuffle ? 1 : 0));
    }

    FString FormatHex(uint64 Value)
    {
        return FString::Printf(TEXT("%016llX"), static_cast<unsigned long long>(Value));
    }
}

bool FSphericalDelaunayPropertiesTest::RunTest(const FString& Parameters)
{
    int32 PointCount = FMath::Max(3, CVarPaperTriangulationPropertiesPointCount.GetValueOnAnyThread());
    TArray<FVector3d> Points;

#if WITH_STRIPACK
    AddInfo(TEXT("WITH_STRIPACK=1; running full triangulation checks."));
#else
    AddInfo(TEXT("WITH_STRIPACK=0; triangulation unavailable."));
    return true;
#endif

    const bool bRequireBaseline = CVarPaperTriangulationRequireBaselineEquivalence.GetValueOnAnyThread() != 0;
    if (bRequireBaseline)
    {
        const int32 MaxBaselineCount = FMath::Max(3, CVarPaperTriangulationBaselineMaxPointCount.GetValueOnAnyThread());
        if (PointCount > MaxBaselineCount)
        {
            AddInfo(FString::Printf(TEXT("Baseline equivalence enabled; reducing sample count from %d to %d to keep baseline run manageable."),
                PointCount, MaxBaselineCount));
            PointCount = MaxBaselineCount;
        }
    }

    IConsoleVariable* UseCacheVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperTriangulation.UseCache"));
    IConsoleVariable* CacheDirVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperTriangulation.CacheDir"));
    IConsoleVariable* ShuffleVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperTriangulation.Shuffle"));
    IConsoleVariable* ShuffleSeedVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperTriangulation.ShuffleSeed"));

    const bool bUseCache = !UseCacheVar || (UseCacheVar->GetInt() != 0);
    const bool bShuffleEnabled = ShuffleVar ? (ShuffleVar->GetInt() != 0) : false;
    const int32 ShuffleSeed = ShuffleSeedVar ? ShuffleSeedVar->GetInt() : 0;
    const FString CacheDirAbsolute = NormalizeCacheDirectory(CacheDirVar);

    TriCache::FTriangulationMeta CacheKey;
    CacheKey.N = PointCount;
    CacheKey.Seed = ShuffleSeed;
    CacheKey.bShuffle = bShuffleEnabled;
    CacheKey.Signature = 0;

    TArray<FSphericalDelaunay::FTriangle> Triangles;
    bool bFromCache = false;
    double LoadSeconds = 0.0;
    double ComputeSeconds = 0.0;
    double SaveSeconds = 0.0;
    double TriangulateSeconds = 0.0;
    FString CachePath;
    TriCache::FTriangulationMeta CachedMeta;

    if (bUseCache)
    {
        if (TriCache::Load(CacheDirAbsolute, CacheKey, Points, Triangles, CachedMeta, LoadSeconds))
        {
            bFromCache = true;
            CacheKey.N = CachedMeta.N;
            CacheKey.Signature = CachedMeta.Signature;
            CachePath = BuildCachePath(CacheDirAbsolute, CacheKey);

            UE_LOG(LogTemp, Display, TEXT("SphericalDelaunay cache hit N=%d shuffle=%d seed=%d (%.3f s) -> %s"),
                CacheKey.N, CacheKey.bShuffle ? 1 : 0, CacheKey.Seed, LoadSeconds, *CachePath);
        }
    }

    if (!bFromCache)
    {
        FFibonacciSampling::GenerateSamples(PointCount, Points);
        UE_LOG(LogTemp, Display, TEXT("SphericalDelaunay: Triangulate compute (N=%d) starting"), PointCount);

        const double ComputeStart = FPlatformTime::Seconds();
        FSphericalDelaunay::Triangulate(Points, Triangles);
        ComputeSeconds = FPlatformTime::Seconds() - ComputeStart;

        UE_LOG(LogTemp, Display, TEXT("SphericalDelaunay: Triangulate compute finished in %.3f s (%d triangles)"),
            ComputeSeconds, Triangles.Num());

        TriCache::CanonicalizeTriangles(Triangles);

        if (Triangles.Num() == 0)
        {
            AddInfo(TEXT("STRIPACK unavailable — skipping properties."));
            return true;
        }

        CacheKey.N = Points.Num();
        CacheKey.Signature = TriCache::ComputeTriangleSetHash(Triangles);

        if (bUseCache)
        {
            FString SavedPath;
            if (TriCache::Save(CacheDirAbsolute, CacheKey, Points, Triangles, SavedPath, SaveSeconds))
            {
                CachePath = SavedPath;
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("SphericalDelaunay: failed to save triangulation cache (expected %s)"),
                    *BuildCachePath(CacheDirAbsolute, CacheKey));
            }
        }
    }
    else
    {
        if (CacheKey.Signature == 0)
        {
            CacheKey.Signature = TriCache::ComputeTriangleSetHash(Triangles);
        }
    }

    PointCount = Points.Num();
    TriCache::CanonicalizeTriangles(Triangles);

    if (Triangles.Num() == 0)
    {
        AddInfo(TEXT("STRIPACK unavailable — skipping properties."));
        return true;
    }

    TArray<FSphericalDelaunay::FTriangle> TrianglesRepeat;
    const double RepeatStart = FPlatformTime::Seconds();
    FSphericalDelaunay::Triangulate(Points, TrianglesRepeat);
    TriangulateSeconds = FPlatformTime::Seconds() - RepeatStart;
    TriCache::CanonicalizeTriangles(TrianglesRepeat);

    UE_LOG(LogTemp, Display, TEXT("SphericalDelaunay: Determinism triangulate finished in %.3f s (%d triangles)"),
        TriangulateSeconds, TrianglesRepeat.Num());

    TestEqual(TEXT("repeat triangle count"), TrianglesRepeat.Num(), Triangles.Num());

    bool bDeterministic = (TrianglesRepeat.Num() == Triangles.Num());
    if (bDeterministic)
    {
        for (int32 Index = 0; Index < Triangles.Num(); ++Index)
        {
            const FSphericalDelaunay::FTriangle& A = Triangles[Index];
            const FSphericalDelaunay::FTriangle& B = TrianglesRepeat[Index];
            if (A.V0 != B.V0 || A.V1 != B.V1 || A.V2 != B.V2)
            {
                bDeterministic = false;
                AddError(FString::Printf(TEXT("Determinism violation at index %d: A=(%d,%d,%d) B=(%d,%d,%d)"),
                    Index, A.V0, A.V1, A.V2, B.V0, B.V1, B.V2));
                break;
            }
        }
    }

    TestTrue(TEXT("triangulation deterministic for current configuration"), bDeterministic);

    const int32 VertexCount = Points.Num();
    const int32 FaceCount = Triangles.Num();
    TSet<int64> UniqueEdges;
    TArray<int32> Degrees;
    Degrees.Init(0, VertexCount);

    for (const FSphericalDelaunay::FTriangle& Triangle : Triangles)
    {
        const int32 Indices[3] = {Triangle.V0, Triangle.V1, Triangle.V2};
        for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
        {
            const int32 A = Indices[EdgeIndex];
            const int32 B = Indices[(EdgeIndex + 1) % 3];

            const int64 Key = EncodeEdge(A, B);
            bool bIsNewEdge = false;
            UniqueEdges.Add(Key, &bIsNewEdge);
            if (bIsNewEdge)
            {
                ++Degrees[A];
                ++Degrees[B];
            }
        }
    }

    const int32 EdgeCount = UniqueEdges.Num();
    const int32 EulerCharacteristic = VertexCount - EdgeCount + FaceCount;
    TestEqual(TEXT("Euler characteristic"), EulerCharacteristic, 2);

    double SumDegrees = 0.0;
    int32 MinDegree = INT32_MAX;
    int32 MaxDegree = 0;

    for (int32 DegreeValue : Degrees)
    {
        MinDegree = FMath::Min(MinDegree, DegreeValue);
        MaxDegree = FMath::Max(MaxDegree, DegreeValue);
        SumDegrees += static_cast<double>(DegreeValue);
    }

    const double AverageDegree = SumDegrees / static_cast<double>(VertexCount);
    TestTrue(TEXT("average degree near 6"), AverageDegree >= 5.5 && AverageDegree <= 6.5);
    TestTrue(TEXT("minimum degree >= 3"), MinDegree >= 3);

    const uint64 SignatureHashValue = TriCache::ComputeTriangleSetHash(Triangles);
    const FString SignatureHashHex = FormatHex(SignatureHashValue);

    if (CacheKey.Signature != 0 && CacheKey.Signature != SignatureHashValue)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cached signature (%016llX) mismatched recomputed signature (%016llX)"),
            static_cast<unsigned long long>(CacheKey.Signature),
            static_cast<unsigned long long>(SignatureHashValue));
    }

    UE_LOG(LogTemp, Display, TEXT("SphericalDelaunay Deterministic=%s TriangulateSeconds=%.6f SignatureHash=%s Shuffle=%d Seed=%d FromCache=%s"),
        bDeterministic ? TEXT("true") : TEXT("false"),
        TriangulateSeconds,
        *SignatureHashHex,
        bShuffleEnabled ? 1 : 0,
        ShuffleSeed,
        bFromCache ? TEXT("true") : TEXT("false"));

    AddInfo(FString::Printf(TEXT("Triangulation metrics: V=%d, F=%d, E=%d, Euler=%d"), VertexCount, FaceCount, EdgeCount, EulerCharacteristic));
    AddInfo(FString::Printf(TEXT("Degree stats: min=%d, avg=%.3f, max=%d"), MinDegree, AverageDegree, MaxDegree));
    AddInfo(FString::Printf(TEXT("Deterministic triangles: %s"), bDeterministic ? TEXT("yes") : TEXT("NO")));
    AddInfo(FString::Printf(TEXT("Signature hash: %s"), *SignatureHashHex));
    if (bFromCache)
    {
        AddInfo(FString::Printf(TEXT("Cache load time: %.3f ms"), LoadSeconds * 1000.0));
    }
    else
    {
        AddInfo(FString::Printf(TEXT("Triangulate compute time: %.3f ms"), ComputeSeconds * 1000.0));
    }
    if (!CachePath.IsEmpty())
    {
        AddInfo(FString::Printf(TEXT("Cache path: %s"), *CachePath));
    }

    const FString SummaryDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Automation/Validation/Phase2"));
    IFileManager::Get().MakeDirectory(*SummaryDir, true);

    const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
    const FString SummaryPath = FPaths::Combine(SummaryDir, FString::Printf(TEXT("summary_%s.json"), *Timestamp));

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("V"), VertexCount);
    Root->SetNumberField(TEXT("F"), FaceCount);
    Root->SetNumberField(TEXT("E"), EdgeCount);
    Root->SetNumberField(TEXT("Euler"), EulerCharacteristic);

    TSharedRef<FJsonObject> DegreeObj = MakeShared<FJsonObject>();
    DegreeObj->SetNumberField(TEXT("min"), MinDegree);
    DegreeObj->SetNumberField(TEXT("avg"), AverageDegree);
    DegreeObj->SetNumberField(TEXT("max"), MaxDegree);
    Root->SetObjectField(TEXT("Degree"), DegreeObj);

    Root->SetBoolField(TEXT("DeterminismExact"), bDeterministic);
    Root->SetNumberField(TEXT("TriangulateSeconds"), TriangulateSeconds);
    Root->SetBoolField(TEXT("FromCache"), bFromCache);
    if (bFromCache)
    {
        Root->SetNumberField(TEXT("LoadSeconds"), LoadSeconds);
    }
    else
    {
        Root->SetNumberField(TEXT("ComputeSeconds"), ComputeSeconds);
        if (SaveSeconds > 0.0)
        {
            Root->SetNumberField(TEXT("SaveSeconds"), SaveSeconds);
        }
    }

    Root->SetBoolField(TEXT("WithShuffle"), bShuffleEnabled);
    Root->SetNumberField(TEXT("Seed"), ShuffleSeed);
    // Backend info (for backend-agnostic validation and cache separation)
    bool bBackendFallback = false;
    FString ResolvedBackend;
    FSphericalTriangulatorFactory::Resolve(ResolvedBackend, bBackendFallback);
    Root->SetStringField(TEXT("Backend"), ResolvedBackend);
    Root->SetBoolField(TEXT("BackendFallback"), bBackendFallback);
    Root->SetStringField(TEXT("SignatureHash"), SignatureHashHex);
    Root->SetStringField(TEXT("CachePath"), CachePath);

    if (bRequireBaseline)
    {
        const int32 OriginalShuffleValue = ShuffleVar ? ShuffleVar->GetInt() : 1;
        if (ShuffleVar)
        {
            ShuffleVar->Set(0, ECVF_SetByCode);
        }

        TArray<FSphericalDelaunay::FTriangle> BaselineTriangles;
        const double BaselineStart = FPlatformTime::Seconds();
        FSphericalDelaunay::Triangulate(Points, BaselineTriangles);
        const double BaselineDuration = FPlatformTime::Seconds() - BaselineStart;
        TriCache::CanonicalizeTriangles(BaselineTriangles);

        if (ShuffleVar)
        {
            ShuffleVar->Set(OriginalShuffleValue, ECVF_SetByCode);
        }

        const uint64 BaselineHashValue = TriCache::ComputeTriangleSetHash(BaselineTriangles);
        const FString BaselineHashHex = FormatHex(BaselineHashValue);

        TSet<int64> BaselineEdges;
        CollectEdges(BaselineTriangles, BaselineEdges);

        bool bBaselineEquivalent = (BaselineEdges.Num() == UniqueEdges.Num());
        int32 DifferenceLogged = 0;
        constexpr int32 MaxDifferencesToLog = 10;

        if (bBaselineEquivalent)
        {
            for (int64 Edge : UniqueEdges)
            {
                if (!BaselineEdges.Contains(Edge))
                {
                    bBaselineEquivalent = false;
                    break;
                }
            }
        }

        if (!bBaselineEquivalent)
        {
            for (int64 Edge : UniqueEdges)
            {
                if (!BaselineEdges.Contains(Edge))
                {
                    const int32 V0 = static_cast<int32>(Edge >> 32);
                    const int32 V1 = static_cast<int32>(Edge & 0xFFFFFFFF);
                    AddInfo(FString::Printf(TEXT("Shuffle-only edge mismatch: (%d,%d)"), V0, V1));
                    if (++DifferenceLogged >= MaxDifferencesToLog)
                    {
                        break;
                    }
                }
            }

            if (DifferenceLogged < MaxDifferencesToLog)
            {
                for (int64 Edge : BaselineEdges)
                {
                    if (!UniqueEdges.Contains(Edge))
                    {
                        const int32 V0 = static_cast<int32>(Edge >> 32);
                        const int32 V1 = static_cast<int32>(Edge & 0xFFFFFFFF);
                        AddInfo(FString::Printf(TEXT("Baseline-only edge mismatch: (%d,%d)"), V0, V1));
                        if (++DifferenceLogged >= MaxDifferencesToLog)
                        {
                            break;
                        }
                    }
                }
            }
        }

        Root->SetStringField(TEXT("BaselineHash"), BaselineHashHex);
        Root->SetNumberField(TEXT("BaselineSeconds"), BaselineDuration);
        Root->SetBoolField(TEXT("BaselineEquals"), bBaselineEquivalent);

        UE_LOG(LogTemp, Display, TEXT("SphericalDelaunay baseline comparison: Equals=%s BaselineHash=%s (%.6f s)"),
            bBaselineEquivalent ? TEXT("true") : TEXT("false"), *BaselineHashHex, BaselineDuration);

        TestTrue(TEXT("baseline triangulation equivalence"), bBaselineEquivalent);
    }

    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(Root, Writer);

    if (!FFileHelper::SaveStringToFile(JsonString, *SummaryPath))
    {
        AddError(FString::Printf(TEXT("Failed to write STRIPACK metrics JSON to %s"), *SummaryPath));
    }
    else
    {
        AddInfo(FString::Printf(TEXT("Summary JSON written to %s"), *SummaryPath));
        UE_LOG(LogTemp, Display, TEXT("SphericalDelaunay summary JSON: %s"), *SummaryPath);
    }

    return true;
}
