#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"
#include "Misc/AutomationTest.h"
#include "Containers/Set.h"
#include "Simulation/FibonacciSampling.h"
#include "Simulation/SphericalDelaunay.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSphericalDelaunay10kTest, "PlanetaryCreation.Paper.SphericalDelaunay10k",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace
{
    int64 EncodeEdge(int32 A, int32 B)
    {
        const int32 MinIndex = FMath::Min(A, B);
        const int32 MaxIndex = FMath::Max(A, B);
        return (static_cast<int64>(MinIndex) << 32) | static_cast<uint32>(MaxIndex);
    }
}

bool FSphericalDelaunay10kTest::RunTest(const FString& Parameters)
{
#if !WITH_STRIPACK
    AddInfo(TEXT("WITH_STRIPACK=0; skipping 10k test."));
    return true;
#else
    const int32 PointCount = 10000;
    UE_LOG(LogTemp, Display, TEXT("=== 10k Delaunay Test Starting ==="));
    UE_LOG(LogTemp, Display, TEXT("Generating Fibonacci samples (N=%d)"), PointCount);

    TArray<FVector3d> Points;
    Points.Reserve(PointCount);
    FFibonacciSampling::GenerateSamples(PointCount, Points);
    UE_LOG(LogTemp, Display, TEXT("✓ Generated %d points"), Points.Num());

    // First run
    UE_LOG(LogTemp, Display, TEXT("Triangulation Run #1 starting..."));
    const double StartTime1 = FPlatformTime::Seconds();
    TArray<FSphericalDelaunay::FTriangle> Triangles1;
    FSphericalDelaunay::Triangulate(Points, Triangles1);
    const double Duration1 = FPlatformTime::Seconds() - StartTime1;
    UE_LOG(LogTemp, Display, TEXT("✓ Run #1 completed in %.3f s (%d triangles)"),
        Duration1, Triangles1.Num());

    if (Triangles1.Num() == 0)
    {
        AddError(TEXT("Run #1: No triangles generated"));
        return false;
    }

    // Second run (test determinism/geometric equivalence)
    UE_LOG(LogTemp, Display, TEXT("Triangulation Run #2 starting..."));
    const double StartTime2 = FPlatformTime::Seconds();
    TArray<FSphericalDelaunay::FTriangle> Triangles2;
    FSphericalDelaunay::Triangulate(Points, Triangles2);
    const double Duration2 = FPlatformTime::Seconds() - StartTime2;
    UE_LOG(LogTemp, Display, TEXT("✓ Run #2 completed in %.3f s (%d triangles)"),
        Duration2, Triangles2.Num());

    // Validate topological properties
    UE_LOG(LogTemp, Display, TEXT("Validating topological properties..."));

    TSet<int64> UniqueEdges1, UniqueEdges2;
    UniqueEdges1.Reserve(Triangles1.Num() * 3);
    UniqueEdges2.Reserve(Triangles2.Num() * 3);

    // Build edge sets
    for (const FSphericalDelaunay::FTriangle& Triangle : Triangles1)
    {
        UniqueEdges1.Add(EncodeEdge(Triangle.V0, Triangle.V1));
        UniqueEdges1.Add(EncodeEdge(Triangle.V1, Triangle.V2));
        UniqueEdges1.Add(EncodeEdge(Triangle.V2, Triangle.V0));
    }

    for (const FSphericalDelaunay::FTriangle& Triangle : Triangles2)
    {
        UniqueEdges2.Add(EncodeEdge(Triangle.V0, Triangle.V1));
        UniqueEdges2.Add(EncodeEdge(Triangle.V1, Triangle.V2));
        UniqueEdges2.Add(EncodeEdge(Triangle.V2, Triangle.V0));
    }

    // Euler characteristic
    const int32 VertexCount = Points.Num();
    const int32 FaceCount1 = Triangles1.Num();
    const int32 EdgeCount1 = UniqueEdges1.Num();
    const int32 Euler1 = VertexCount - EdgeCount1 + FaceCount1;

    TestEqual(TEXT("Run #1: Euler characteristic == 2"), Euler1, 2);
    UE_LOG(LogTemp, Display, TEXT("  Run #1: V=%d, E=%d, F=%d, Euler=%d"),
        VertexCount, EdgeCount1, FaceCount1, Euler1);

    const int32 FaceCount2 = Triangles2.Num();
    const int32 EdgeCount2 = UniqueEdges2.Num();
    const int32 Euler2 = VertexCount - EdgeCount2 + FaceCount2;

    TestEqual(TEXT("Run #2: Euler characteristic == 2"), Euler2, 2);
    UE_LOG(LogTemp, Display, TEXT("  Run #2: V=%d, E=%d, F=%d, Euler=%d"),
        VertexCount, EdgeCount2, FaceCount2, Euler2);

    // Check edge set equality (geometric equivalence)
    bool bEdgeSetsEqual = (EdgeCount1 == EdgeCount2);
    if (bEdgeSetsEqual)
    {
        for (int64 Edge : UniqueEdges1)
        {
            if (!UniqueEdges2.Contains(Edge))
            {
                bEdgeSetsEqual = false;
                break;
            }
        }
    }

    TestTrue(TEXT("Edge sets geometrically equivalent"), bEdgeSetsEqual);
    UE_LOG(LogTemp, Display, TEXT("  Edges Run #1: %d, Run #2: %d, Equal: %s"),
        EdgeCount1, EdgeCount2, bEdgeSetsEqual ? TEXT("YES") : TEXT("NO"));

    // Check degree distribution
    TArray<int32> Degrees1, Degrees2;
    Degrees1.Init(0, VertexCount);
    Degrees2.Init(0, VertexCount);

    for (int64 Edge : UniqueEdges1)
    {
        const int32 A = static_cast<int32>(Edge >> 32);
        const int32 B = static_cast<int32>(Edge & 0xFFFFFFFF);
        Degrees1[A]++;
        Degrees1[B]++;
    }

    for (int64 Edge : UniqueEdges2)
    {
        const int32 A = static_cast<int32>(Edge >> 32);
        const int32 B = static_cast<int32>(Edge & 0xFFFFFFFF);
        Degrees2[A]++;
        Degrees2[B]++;
    }

    double SumDegrees1 = 0, SumDegrees2 = 0;
    int32 MinDegree1 = INT32_MAX, MinDegree2 = INT32_MAX;
    int32 MaxDegree1 = 0, MaxDegree2 = 0;

    for (int32 D : Degrees1)
    {
        MinDegree1 = FMath::Min(MinDegree1, D);
        MaxDegree1 = FMath::Max(MaxDegree1, D);
        SumDegrees1 += D;
    }

    for (int32 D : Degrees2)
    {
        MinDegree2 = FMath::Min(MinDegree2, D);
        MaxDegree2 = FMath::Max(MaxDegree2, D);
        SumDegrees2 += D;
    }

    const double AvgDegree1 = SumDegrees1 / VertexCount;
    const double AvgDegree2 = SumDegrees2 / VertexCount;

    const bool bAvgInRange1 = AvgDegree1 >= 5.5 && AvgDegree1 <= 6.5;
    const bool bAvgInRange2 = AvgDegree2 >= 5.5 && AvgDegree2 <= 6.5;

    TestTrue(TEXT("Run #1: average degree near 6"), bAvgInRange1);
    TestTrue(TEXT("Run #2: average degree near 6"), bAvgInRange2);
    TestTrue(TEXT("Run #1: minimum degree >= 3"), MinDegree1 >= 3);
    TestTrue(TEXT("Run #2: minimum degree >= 3"), MinDegree2 >= 3);

    UE_LOG(LogTemp, Display, TEXT("  Run #1 Degree: min=%d, avg=%.3f, max=%d"),
        MinDegree1, AvgDegree1, MaxDegree1);
    UE_LOG(LogTemp, Display, TEXT("  Run #2 Degree: min=%d, avg=%.3f, max=%d"),
        MinDegree2, AvgDegree2, MaxDegree2);

    // Performance check
    const bool bWithinBudget = Duration1 < 30.0;  // 30 second budget for 10k
    TestTrue(TEXT("Run #1 duration < 30s"), bWithinBudget);

    UE_LOG(LogTemp, Display, TEXT("=== 10k Delaunay Test Complete ==="));
    UE_LOG(LogTemp, Display, TEXT("Performance: Run #1: %.3f s, Run #2: %.3f s"), Duration1, Duration2);
    if (bEdgeSetsEqual && Euler1 == 2 && Euler2 == 2)
    {
        UE_LOG(LogTemp, Display, TEXT("✓✓✓ All checks passed! ✓✓✓"));
    }

    return true;
#endif // WITH_STRIPACK
}
