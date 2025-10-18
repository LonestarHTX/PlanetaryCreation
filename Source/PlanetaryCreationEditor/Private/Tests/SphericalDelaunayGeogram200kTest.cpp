#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"
#include "Misc/AutomationTest.h"
#include "Containers/Set.h"
#include "Simulation/FibonacciSampling.h"
#include "Simulation/SphericalDelaunay.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSphericalDelaunayGeogram200kTest, "PlanetaryCreation.Geogram.Delaunay200k",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

static TAutoConsoleVariable<int32> CVarGeogram200kTestEnabled(
	TEXT("r.Geogram.Test200k.Enabled"),
	0,
	TEXT("Enable 200k vertex Geogram triangulation test (0 = skip, 1 = run). This is a stress test."),
	ECVF_Default);

namespace
{
	int64 EncodeEdge(int32 A, int32 B)
	{
		const int32 MinIndex = FMath::Min(A, B);
		const int32 MaxIndex = FMath::Max(A, B);
		return (static_cast<int64>(MinIndex) << 32) | static_cast<uint32>(MaxIndex);
	}
}

bool FSphericalDelaunayGeogram200kTest::RunTest(const FString& Parameters)
{
#if !WITH_GEOGRAM
	AddInfo(TEXT("WITH_GEOGRAM=0; skipping 200k test. Build Geogram to enable."));
	return true;
#else
	if (CVarGeogram200kTestEnabled.GetValueOnAnyThread() == 0)
	{
		UE_LOG(LogTemp, Display, TEXT("Geogram200k: skipping (r.Geogram.Test200k.Enabled = 0)"));
		AddInfo(TEXT("Skipping 200k test (r.Geogram.Test200k.Enabled = 0). Enable to run stress test."));
		return true;
	}

	// Validate that Geogram backend is selected
	IConsoleVariable* BackendVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperTriangulation.Backend"));
	const FString ConfiguredBackend = BackendVar ? BackendVar->GetString() : TEXT("Unknown");
	
	UE_LOG(LogTemp, Display, TEXT("Geogram200k: Configured backend: %s"), *ConfiguredBackend);
	AddInfo(FString::Printf(TEXT("Triangulation backend: %s"), *ConfiguredBackend));

	// Generate 200k points
	constexpr int32 PointCount = 200000;
	UE_LOG(LogTemp, Display, TEXT("Geogram200k: Generating %d Fibonacci samples..."), PointCount);
	AddInfo(FString::Printf(TEXT("Generating %d Fibonacci samples..."), PointCount));

	TArray<FVector3d> Points;
	Points.Reserve(PointCount);
	
	const double SampleStart = FPlatformTime::Seconds();
	FFibonacciSampling::GenerateSamples(PointCount, Points);
	const double SampleEnd = FPlatformTime::Seconds();
	const double SampleDuration = SampleEnd - SampleStart;

	if (!TestEqual(TEXT("Generated correct number of points"), Points.Num(), PointCount))
	{
		return false;
	}

	UE_LOG(LogTemp, Display, TEXT("Geogram200k: Sampling completed in %.3f s"), SampleDuration);
	AddInfo(FString::Printf(TEXT("Sampling: %.3f s"), SampleDuration));

	// Triangulate
	UE_LOG(LogTemp, Display, TEXT("Geogram200k: Starting triangulation of %d points..."), PointCount);
	AddInfo(TEXT("Starting triangulation (this may take 30-60 seconds)..."));

	TArray<FSphericalDelaunay::FTriangle> Triangles;
	const double TriangulateStart = FPlatformTime::Seconds();
	FSphericalDelaunay::Triangulate(Points, Triangles);
	const double TriangulateEnd = FPlatformTime::Seconds();
	const double TriangulateDuration = TriangulateEnd - TriangulateStart;

	UE_LOG(LogTemp, Display, TEXT("Geogram200k: Triangulation completed in %.3f s (%d triangles)"),
		TriangulateDuration, Triangles.Num());
	AddInfo(FString::Printf(TEXT("Triangulation: %.3f s (%d triangles)"), TriangulateDuration, Triangles.Num()));

	if (!TestTrue(TEXT("Triangles generated"), Triangles.Num() > 0))
	{
		return false;
	}

	// Expected triangles for a sphere: ~2N for uniformly distributed points
	const int32 ExpectedTriangles = 2 * PointCount - 4; // Euler formula: F = 2V - 4
	const int32 TriangleTolerance = PointCount / 100; // 1% tolerance
	const bool bTriangleCountReasonable = FMath::Abs(Triangles.Num() - ExpectedTriangles) < TriangleTolerance;
	
	TestTrue(
		*FString::Printf(TEXT("Triangle count reasonable (expected ~%d, got %d)"), ExpectedTriangles, Triangles.Num()),
		bTriangleCountReasonable);

	// Validate Euler characteristic (V - E + F = 2 for sphere)
	UE_LOG(LogTemp, Display, TEXT("Geogram200k: Validating topology..."));
	AddInfo(TEXT("Validating Euler characteristic..."));

	TSet<int64> UniqueEdges;
	UniqueEdges.Reserve(Triangles.Num() * 3);

	for (const FSphericalDelaunay::FTriangle& Triangle : Triangles)
	{
		const int32 Indices[3] = {Triangle.V0, Triangle.V1, Triangle.V2};
		for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
		{
			const int32 A = Indices[EdgeIndex];
			const int32 B = Indices[(EdgeIndex + 1) % 3];
			UniqueEdges.Add(EncodeEdge(A, B));
		}
	}

	const int32 VertexCount = Points.Num();
	const int32 FaceCount = Triangles.Num();
	const int32 EdgeCount = UniqueEdges.Num();
	const int32 EulerCharacteristic = VertexCount - EdgeCount + FaceCount;

	UE_LOG(LogTemp, Display, TEXT("Geogram200k: V=%d, E=%d, F=%d, χ=%d"), 
		VertexCount, EdgeCount, FaceCount, EulerCharacteristic);
	AddInfo(FString::Printf(TEXT("Topology: V=%d, E=%d, F=%d, χ=%d"), 
		VertexCount, EdgeCount, FaceCount, EulerCharacteristic));

	TestEqual(TEXT("Euler characteristic == 2 (sphere topology)"), EulerCharacteristic, 2);

	// Compute statistics
	TArray<int32> Degrees;
	Degrees.Init(0, Points.Num());

	for (const FSphericalDelaunay::FTriangle& Triangle : Triangles)
	{
		const int32 Indices[3] = {Triangle.V0, Triangle.V1, Triangle.V2};
		for (int32 i = 0; i < 3; ++i)
		{
			++Degrees[Indices[i]];
		}
	}

	double SumDegrees = 0.0;
	int32 MinDegree = INT32_MAX;
	int32 MaxDegree = 0;

	for (int32 Degree : Degrees)
	{
		SumDegrees += static_cast<double>(Degree);
		MinDegree = FMath::Min(MinDegree, Degree);
		MaxDegree = FMath::Max(MaxDegree, Degree);
	}

	const double AverageDegree = SumDegrees / static_cast<double>(VertexCount);
	
	UE_LOG(LogTemp, Display, TEXT("Geogram200k: Degree statistics: min=%d, avg=%.3f, max=%d"), 
		MinDegree, AverageDegree, MaxDegree);
	AddInfo(FString::Printf(TEXT("Vertex degree: min=%d, avg=%.3f, max=%d"), 
		MinDegree, AverageDegree, MaxDegree));

	// For a uniform sphere triangulation, average degree should be ~6
	TestTrue(TEXT("Average degree near 6 (5.5-6.5)"), AverageDegree >= 5.5 && AverageDegree <= 6.5);
	TestTrue(TEXT("Minimum degree >= 3"), MinDegree >= 3);

	// Performance summary
	const double TotalTime = SampleDuration + TriangulateDuration;
	const double PointsPerSecond = static_cast<double>(PointCount) / TriangulateDuration;
	
	UE_LOG(LogTemp, Display, TEXT("========================================"));
	UE_LOG(LogTemp, Display, TEXT("Geogram 200k Test PASSED"));
	UE_LOG(LogTemp, Display, TEXT("========================================"));
	UE_LOG(LogTemp, Display, TEXT("Total time:        %.3f s"), TotalTime);
	UE_LOG(LogTemp, Display, TEXT("Triangulation:     %.3f s"), TriangulateDuration);
	UE_LOG(LogTemp, Display, TEXT("Throughput:        %.0f points/sec"), PointsPerSecond);
	UE_LOG(LogTemp, Display, TEXT("Backend:           %s"), *ConfiguredBackend);
	UE_LOG(LogTemp, Display, TEXT("========================================"));

	AddInfo(TEXT("========================================"));
	AddInfo(FString::Printf(TEXT("SUCCESS: 200k vertices in %.3f seconds"), TriangulateDuration));
	AddInfo(FString::Printf(TEXT("Throughput: %.0f points/sec"), PointsPerSecond));
	AddInfo(FString::Printf(TEXT("Backend: %s"), *ConfiguredBackend));
	AddInfo(TEXT("========================================"));

	return true;
#endif // WITH_GEOGRAM
}

