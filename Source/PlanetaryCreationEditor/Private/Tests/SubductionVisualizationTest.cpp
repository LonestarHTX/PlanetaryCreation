#include "Misc/AutomationTest.h"
#include "Simulation/FibonacciSampling.h"
#include "Simulation/SphericalDelaunay.h"
#include "Simulation/SubductionProcessor.h"
#include "Simulation/BoundaryField.h"
#include "Simulation/PaperConstants.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

using namespace Subduction;

static void BuildCSRForVisualization(const TArray<TArray<int32>>& Neighbors, TArray<int32>& OutOffsets, TArray<int32>& OutAdj)
{
	const int32 N = Neighbors.Num();
	OutOffsets.SetNum(N + 1);
	int32 Accum = 0;
	OutOffsets[0] = 0;
	for (int32 i = 0; i < N; ++i)
	{
		Accum += Neighbors[i].Num();
		OutOffsets[i + 1] = Accum;
	}
	OutAdj.SetNumUninitialized(Accum);
	int32 Write = 0;
	for (int32 i = 0; i < N; ++i)
	{
		for (int32 nb : Neighbors[i])
		{
			OutAdj[Write++] = nb;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSubductionVisualizationTest,
	"PlanetaryCreation.Paper.Subduction.Visualization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSubductionVisualizationTest::RunTest(const FString& Parameters)
{
	using namespace PaperConstants;

	// Generate 10k samples for reasonable detail
	const int32 N = 10000;
	TArray<FVector3d> Points;
	FFibonacciSampling::GenerateSamples(N, Points);

	// Build Delaunay triangulation and Voronoi neighbors
	TArray<FSphericalDelaunay::FTriangle> Tris;
	FSphericalDelaunay::Triangulate(Points, Tris);
	TArray<TArray<int32>> Neighbors;
	FSphericalDelaunay::ComputeVoronoiNeighbors(Points, Tris, Neighbors);

	// Build CSR adjacency
	TArray<int32> Offsets, Adj;
	BuildCSRForVisualization(Neighbors, Offsets, Adj);

	// Create two-plate configuration: converging across equator
	// Plate 0 = northern hemisphere, Plate 1 = southern hemisphere
	TArray<int32> PlateAssign;
	PlateAssign.SetNumUninitialized(N);
	for (int32 i = 0; i < N; ++i)
	{
		PlateAssign[i] = (Points[i].Z >= 0.0) ? 0 : 1;
	}

	// Set opposing angular velocities to create convergence at equator
	// Omegas rotate around X-axis in opposite directions
	const double AngularSpeed = 0.02; // rad/My (moderate convergence rate)
	TArray<FVector3d> Omegas;
	Omegas.SetNumUninitialized(2);
	Omegas[0] = FVector3d(-AngularSpeed, 0.0, 0.0); // Northern plate rotates clockwise (from east)
	Omegas[1] = FVector3d(AngularSpeed, 0.0, 0.0);  // Southern plate rotates counter-clockwise

	// Initialize elevations at sea level
	TArray<double> Elev_m;
	Elev_m.Init(0.0, N);

	// Compute boundary fields to get distance to convergent front
	BoundaryField::FBoundaryFieldResults BF;
	BoundaryField::ComputeBoundaryFields(Points, Neighbors, PlateAssign, Omegas, BF);

	UE_LOG(LogTemp, Display, TEXT("[SubductionVisualization] Initial setup:"));
	UE_LOG(LogTemp, Display, TEXT("  Vertices: %d"), N);
	UE_LOG(LogTemp, Display, TEXT("  Convergent boundaries: %d"), BF.Metrics.NumConvergent);
	UE_LOG(LogTemp, Display, TEXT("  Divergent boundaries: %d"), BF.Metrics.NumDivergent);

	// Run 10 time steps to accumulate visible uplift (20 My total)
	FSubductionMetrics CumulativeUplift;
	for (int32 step = 0; step < 10; ++step)
	{
		const FSubductionMetrics M = ApplyUplift(Points, Offsets, Adj, PlateAssign, Omegas, Elev_m);
		CumulativeUplift.VerticesTouched += M.VerticesTouched;
		CumulativeUplift.TotalUplift_m += M.TotalUplift_m;
		if (M.MaxUplift_m > CumulativeUplift.MaxUplift_m) CumulativeUplift.MaxUplift_m = M.MaxUplift_m;
		UE_LOG(LogTemp, Display, TEXT("  Step %d: Touched=%d, MaxUplift=%.1f m"),
			step, M.VerticesTouched, M.MaxUplift_m);
	}

	// Compute statistics
	double MaxElev = 0.0;
	double MeanElev = 0.0;
	int32 UpliftedCount = 0;
	for (int32 i = 0; i < N; ++i)
	{
		if (Elev_m[i] > MaxElev)
		{
			MaxElev = Elev_m[i];
		}
		if (Elev_m[i] > 0.0)
		{
			MeanElev += Elev_m[i];
			UpliftedCount++;
		}
	}
	if (UpliftedCount > 0)
	{
		MeanElev /= UpliftedCount;
	}

	UE_LOG(LogTemp, Display, TEXT("[SubductionVisualization] After 10 steps (20 My):"));
	UE_LOG(LogTemp, Display, TEXT("  Max elevation: %.1f m"), MaxElev);
	UE_LOG(LogTemp, Display, TEXT("  Mean elevation (uplifted): %.1f m"), MeanElev);
	UE_LOG(LogTemp, Display, TEXT("  Uplifted vertices: %d / %d (%.1f%%)"),
		UpliftedCount, N, 100.0 * UpliftedCount / N);

	// Export CSV for visualization
	FString CSV;
	CSV += TEXT("Longitude,Latitude,Elevation_m,DistToFront_km\n");

	for (int32 i = 0; i < N; ++i)
	{
		const FVector3d& P = Points[i];

		// Convert to spherical coordinates
		const double Lon = FMath::Atan2(P.Y, P.X) * 180.0 / PI;
		const double Lat = FMath::Asin(FMath::Clamp(P.Z, -1.0, 1.0)) * 180.0 / PI;
		const double Elev = Elev_m[i];
		const double Dist = BF.DistanceToSubductionFront_km.IsValidIndex(i) ?
			BF.DistanceToSubductionFront_km[i] : 99999.0;

		CSV += FString::Printf(TEXT("%.6f,%.6f,%.3f,%.3f\n"), Lon, Lat, Elev, Dist);
	}

	// Save to validation directory
	const FString OutputPath = FPaths::ProjectDir() +
		TEXT("Docs/Automation/Validation/Phase3/uplift_heatmap.csv");

	if (FFileHelper::SaveStringToFile(CSV, *OutputPath))
	{
		UE_LOG(LogTemp, Display, TEXT("[SubductionVisualization] Saved CSV to: %s"), *OutputPath);
		UE_LOG(LogTemp, Display, TEXT("  Run: python Scripts/visualize_phase3.py"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[SubductionVisualization] Failed to save CSV to: %s"), *OutputPath);
	}

	// Write metrics JSON with REAL uplift data (not stub)
	const FString JsonPath = WritePhase3MetricsJson(
		TEXT("SubductionVisualization"),  // Test name for provenance
		TEXT("Geogram"),
		N,
		42,
		10,  // Simulation steps
		BF.Metrics.NumConvergent,
		BF.Metrics.NumDivergent,
		BF.Metrics.NumTransform,
		CumulativeUplift,  // Real uplift metrics from simulation
		FFoldMetrics{},    // Fold not tested here
		0.0,               // Classify time not measured separately
		FSlabPullMetrics{}); // Slab pull not tested here

	UE_LOG(LogTemp, Display, TEXT("[SubductionVisualization] Wrote metrics JSON: %s"), *JsonPath);

	// Validation assertions
	TestTrue(TEXT("Uplift occurred"), UpliftedCount > 0);
	TestTrue(TEXT("Max elevation reasonable"), MaxElev > 0.0 && MaxElev < 10000.0);
	TestTrue(TEXT("Convergent boundaries detected"), BF.Metrics.NumConvergent > 0);

	return true;
}
