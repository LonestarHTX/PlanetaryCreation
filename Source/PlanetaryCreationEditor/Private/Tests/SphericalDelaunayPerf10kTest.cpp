#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"
#include "Misc/AutomationTest.h"
#include "Containers/Set.h"
#include "Simulation/FibonacciSampling.h"
#include "Simulation/SphericalDelaunay.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSphericalDelaunayPerf10kTest, "PlanetaryCreation.Paper.SphericalDelaunayPerf10k",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

static TAutoConsoleVariable<int32> CVarPaperTriangulationRunPerf10k(
    TEXT("r.PaperTriangulation.RunPerf10k"),
    0,
    TEXT("Enable optional 10k spherical Delaunay performance automation test (0 = skip, 1 = run)."),
    ECVF_Default);

static TAutoConsoleVariable<int32> CVarPaperTriangulationPerfSampleCount(
    TEXT("r.PaperTriangulation.PerfSampleCount"),
    4096,
    TEXT("Sample count used by the spherical Delaunay performance automation test (minimum 3)."),
    ECVF_Default);

namespace { static inline int64 EncodeEdgePerf(int32 A, int32 B)
{
    const int32 MinIndex = FMath::Min(A, B);
    const int32 MaxIndex = FMath::Max(A, B);
    return (static_cast<int64>(MinIndex) << 32) | static_cast<uint32>(MaxIndex);
} }

bool FSphericalDelaunayPerf10kTest::RunTest(const FString& Parameters)
{
#if !WITH_STRIPACK
    AddInfo(TEXT("WITH_STRIPACK=0; skipping performance test."));
    return true;
#else
    if (CVarPaperTriangulationRunPerf10k.GetValueOnAnyThread() == 0)
    {
        UE_LOG(LogTemp, Display, TEXT("SphericalDelaunayPerf10k: skipping (r.PaperTriangulation.RunPerf10k = 0)"));
        AddInfo(TEXT("Skipping perf test (r.PaperTriangulation.RunPerf10k = 0)."));
        return true;
    }

    const int32 PointCount = FMath::Max(3, CVarPaperTriangulationPerfSampleCount.GetValueOnAnyThread());
    UE_LOG(LogTemp, Display, TEXT("SphericalDelaunayPerf10k: generating Fibonacci samples (N=%d)"), PointCount);

    TArray<FVector3d> Points;
    Points.Reserve(PointCount);
    FFibonacciSampling::GenerateSamples(PointCount, Points);

    UE_LOG(LogTemp, Display, TEXT("SphericalDelaunayPerf10k: triangulation starting"));
    TArray<FSphericalDelaunay::FTriangle> Triangles;
    const double StartTime = FPlatformTime::Seconds();
    FSphericalDelaunay::Triangulate(Points, Triangles);
    const double DurationSeconds = FPlatformTime::Seconds() - StartTime;
    UE_LOG(LogTemp, Display, TEXT("SphericalDelaunayPerf10k: triangulation finished in %.3f s (%d triangles)"),
        DurationSeconds, Triangles.Num());

    if (!TestTrue(TEXT("triangles generated"), Triangles.Num() > 0))
    {
        return false;
    }

    const double TimeBudgetSeconds = (PointCount >= 8192) ? 30.0 : 10.0;
    const bool bWithinBudget = DurationSeconds < TimeBudgetSeconds;
    TestTrue(*FString::Printf(TEXT("triangulation duration < %.1fs"), TimeBudgetSeconds), bWithinBudget);

    TSet<int64> UniqueEdges;
    UniqueEdges.Reserve(Triangles.Num() * 3);

    TArray<int32> Degrees;
    Degrees.Init(0, Points.Num());

    for (const FSphericalDelaunay::FTriangle& Triangle : Triangles)
    {
        const int32 Indices[3] = {Triangle.V0, Triangle.V1, Triangle.V2};
        for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
        {
            const int32 A = Indices[EdgeIndex];
            const int32 B = Indices[(EdgeIndex + 1) % 3];
            const int64 Key = EncodeEdgePerf(A, B);
            bool bAdded = false;
            UniqueEdges.Add(Key, &bAdded);
            if (bAdded)
            {
                ++Degrees[A];
                ++Degrees[B];
            }
        }
    }

    const int32 VertexCount = Points.Num();
    const int32 FaceCount = Triangles.Num();
    const int32 EdgeCount = UniqueEdges.Num();
    const int32 EulerCharacteristic = VertexCount - EdgeCount + FaceCount;

    TestEqual(TEXT("Euler characteristic == 2"), EulerCharacteristic, 2);

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
    const bool bAverageInRange = AverageDegree >= 5.5 && AverageDegree <= 6.5;
    TestTrue(TEXT("average degree near 6"), bAverageInRange);
    TestTrue(TEXT("minimum degree >= 3"), MinDegree >= 3);

    UE_LOG(LogTemp, Display, TEXT("SphericalDelaunayPerf10k: Euler=%d, Degree(min=%d, avg=%.3f, max=%d)"),
        EulerCharacteristic, MinDegree, AverageDegree, MaxDegree);

    return true;
#endif // WITH_STRIPACK
}
