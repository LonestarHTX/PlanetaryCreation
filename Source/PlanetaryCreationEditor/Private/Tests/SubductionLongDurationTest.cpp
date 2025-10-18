#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformProcess.h"

#include "Simulation/FibonacciSampling.h"
#include "Simulation/SphericalDelaunay.h"
#include "Simulation/SphericalTriangulatorFactory.h"
#include "Simulation/BoundaryField.h"
#include "Simulation/SubductionProcessor.h"
#include "Simulation/PaperConstants.h"

using namespace PaperConstants;

// CVars for long-run test
static TAutoConsoleVariable<int32> CVarPaperLongRun(
    TEXT("r.PaperLong.Run"),
    0,
    TEXT("Enable long-duration Phase 3 subduction test (0=skip,1=run)."),
    ECVF_Default);

static TAutoConsoleVariable<int32> CVarPaperLongSteps(
    TEXT("r.PaperLong.Steps"),
    50,
    TEXT("Number of steps to run for long-duration subduction test (Δt=2 My per step)."),
    ECVF_Default);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSubductionLongDurationTest, "PlanetaryCreation.Paper.SubductionLongDuration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace
{
    static void BuildCSRFromNeighbors(const TArray<TArray<int32>>& Neighbors, TArray<int32>& OutOffsets, TArray<int32>& OutAdj)
    {
        const int32 N = Neighbors.Num();
        OutOffsets.SetNum(N + 1);
        int32 cursor = 0; OutOffsets[0] = 0;
        for (int32 i = 0; i < N; ++i)
        {
            cursor += Neighbors[i].Num();
            OutOffsets[i + 1] = cursor;
        }
        OutAdj.SetNum(cursor);
        int32 w = 0;
        for (int32 i = 0; i < N; ++i)
        {
            for (int32 nb : Neighbors[i]) { OutAdj[w++] = nb; }
        }
    }

    static void BuildConvergentEdges(
        const TArray<FVector3d>& Points,
        const BoundaryField::FBoundaryFieldResults& BF,
        const TArray<int32>& PlateAssign,
        const TArray<FVector3d>& OmegaPerPlate,
        TArray<Subduction::FConvergentEdge>& Out)
    {
        Out.Reset();
        for (int32 e = 0; e < BF.Edges.Num(); ++e)
        {
            if (!BF.Classifications.IsValidIndex(e) || BF.Classifications[e] != BoundaryField::EBoundaryClass::Convergent)
                continue;
            const int32 a = BF.Edges[e].Key;
            const int32 b = BF.Edges[e].Value;
            const int32 pa = PlateAssign[a];
            const int32 pb = PlateAssign[b];
            if (pa == INDEX_NONE || pb == INDEX_NONE || pa == pb) continue;
            const FVector3d& A = Points[a];
            const FVector3d& B = Points[b];
            const FVector3d M = (A + B).GetSafeNormal();
            const FVector3d t = ((B - A) - ((B - A).Dot(M)) * M).GetSafeNormal();
            const FVector3d Nb = FVector3d::CrossProduct(M, t);
            const FVector3d Si = FVector3d::CrossProduct(OmegaPerPlate[pa], M) * PlanetRadius_km;
            const FVector3d Sj = FVector3d::CrossProduct(OmegaPerPlate[pb], M) * PlanetRadius_km;
            const double projA = Si.Dot(Nb);
            const double projB = Sj.Dot(Nb);
            Subduction::FConvergentEdge CE; CE.A = a; CE.B = b;
            if (projA < projB) { CE.SubductingPlateId = pa; CE.OverridingPlateId = pb; }
            else { CE.SubductingPlateId = pb; CE.OverridingPlateId = pa; }
            Out.Add(CE);
        }
    }
}

bool FSubductionLongDurationTest::RunTest(const FString& Parameters)
{
    const bool bRun = CVarPaperLongRun.GetValueOnAnyThread() != 0;
    if (!bRun)
    {
        AddInfo(TEXT("Skipping long-run test; set r.PaperLong.Run=1 to enable"));
        return true;
    }

    // Config
    const int32 Steps = FMath::Max(1, CVarPaperLongSteps.GetValueOnAnyThread());
    int32 N = 50000; // default for this test
    if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperBoundary.TestPointCount")))
    {
        N = FMath::Max(1000, Var->GetInt());
    }

    FString Backend;
    bool bUsedFallback = false;
    FSphericalTriangulatorFactory::Resolve(Backend, bUsedFallback);
    UE_LOG(LogTemp, Display, TEXT("[SubductionLongRun] EffectiveN=%d Backend=%s Steps=%d dt=%.1f My"), N, *Backend, Steps, TimeStep_My);

    // Geometry
    TArray<FVector3d> Points; Points.Reserve(N);
    FFibonacciSampling::GenerateSamples(N, Points);

    TArray<FSphericalDelaunay::FTriangle> Tris;
    FSphericalDelaunay::Triangulate(Points, Tris);
    TArray<TArray<int32>> Neighbors;
    FSphericalDelaunay::ComputeVoronoiNeighbors(Points, Tris, Neighbors);

    // CSR adjacency
    TArray<int32> Offsets, Adj; BuildCSRFromNeighbors(Neighbors, Offsets, Adj);

    // 2 plates by hemisphere
    TArray<int32> PlateAssign; PlateAssign.SetNumUninitialized(N);
    for (int32 i = 0; i < N; ++i) PlateAssign[i] = (Points[i].Z >= 0.0) ? 0 : 1;

    // Angular velocities (rad/My) about X-axis, convergent at equator
    const double w = 0.02;
    TArray<FVector3d> OmegaPerPlate; OmegaPerPlate.SetNum(2);
    OmegaPerPlate[0] = FVector3d(w, 0, 0);
    OmegaPerPlate[1] = FVector3d(-w, 0, 0);

    // Plate centroids for slab pull
    TArray<FVector3d> PlateCentroids; PlateCentroids.Init(FVector3d::ZeroVector, 2);
    TArray<int32> PlateCounts; PlateCounts.Init(0, 2);
    for (int32 i = 0; i < N; ++i)
    {
        const int32 p = PlateAssign[i];
        PlateCentroids[p] += Points[i];
        PlateCounts[p]++;
    }
    for (int32 p = 0; p < 2; ++p)
    {
        if (PlateCounts[p] > 0) PlateCentroids[p] = PlateCentroids[p].GetSafeNormal();
        else PlateCentroids[p] = FVector3d::ZeroVector;
    }

    // Elevations and folds
    TArray<double> Elev_m; Elev_m.Init(0.0, N);
    TArray<FVector3d> FoldD; FoldD.Init(FVector3d::ZeroVector, N);

    auto RunOnce = [&](double& OutFinalMax, double& OutFinalUpliftedPct, FString& OutCSVPath, FString& OutJSONPath)
    {
        TArray<double> Elev = Elev_m; // start at zeros
        TArray<FVector3d> Folds = FoldD; // zeros

        const double t0 = FPlatformTime::Seconds();

        // CSV accumulation
        FString CSV;
        CSV += TEXT("step,sim_time_my,max_elev_m,mean_elev_band_rc_m,mean_elev_band_rs_m,uplifted_count,uplifted_percent\n");

        for (int32 s = 1; s <= Steps; ++s)
        {
            BoundaryField::FBoundaryFieldResults BF;
            BoundaryField::ComputeBoundaryFields(Points, Neighbors, PlateAssign, OmegaPerPlate, BF);

            // Uplift
            Subduction::ApplyUplift(Points, Offsets, Adj, PlateAssign, OmegaPerPlate, Elev);

            // Fold
            Subduction::UpdateFoldDirections(Points, Offsets, Adj, PlateAssign, OmegaPerPlate, BF, Folds);

            // Slab pull
            TArray<Subduction::FConvergentEdge> ConvEdges;
            BuildConvergentEdges(Points, BF, PlateAssign, OmegaPerPlate, ConvEdges);
            Subduction::ApplySlabPull(PlateCentroids, ConvEdges, Points, OmegaPerPlate);

            // Sample cadence: every 5 steps and final
            const bool bSample = (s % 5 == 0) || (s == Steps);
            if (bSample)
            {
                // Metrics
                double maxElev = -1e100;
                int32 uplifted = 0;
                double sumRc = 0.0; int32 countRc = 0;
                double sumRs = 0.0; int32 countRs = 0;
                for (int32 i = 0; i < N; ++i)
                {
                    const double e = Elev[i];
                    if (e > maxElev) maxElev = e;
                    if (e > 0.0) uplifted++;
                    const double d = BF.DistanceToSubductionFront_km.IsValidIndex(i) ? BF.DistanceToSubductionFront_km[i] : TNumericLimits<double>::Max();
                    if (d > 0.0 && d <= SubductionControlDistance_km) { sumRc += e; countRc++; }
                    if (d > 0.0 && d <= SubductionDistance_km) { sumRs += e; countRs++; }
                }
                const double meanRc = (countRc > 0) ? (sumRc / countRc) : 0.0;
                const double meanRs = (countRs > 0) ? (sumRs / countRs) : 0.0;
                const double pct = (N > 0) ? (100.0 * uplifted / N) : 0.0;
                const double simTime = s * TimeStep_My;
                CSV += FString::Printf(TEXT("%d,%.1f,%.6f,%.6f,%.6f,%d,%.6f\n"), s, simTime, maxElev, meanRc, meanRs, uplifted, pct);

                if (s == Steps)
                {
                    OutFinalMax = maxElev;
                    OutFinalUpliftedPct = pct;
                }
            }
        }

        const double totalMs = (FPlatformTime::Seconds() - t0) * 1000.0;

        // Emit CSV and JSON
        const FString Dir = FPaths::ProjectDir() / TEXT("Docs/Automation/Validation/Phase3/LongRun");
        IFileManager::Get().MakeDirectory(*Dir, true);
        const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("yyyyMMdd_HHmmss"));
        OutCSVPath = Dir / FString::Printf(TEXT("uplift_timeseries_%s.csv"), *Timestamp);
        FFileHelper::SaveStringToFile(CSV, *OutCSVPath);

        // JSON summary
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("phase"), TEXT("3-subduction"));
        Root->SetStringField(TEXT("test_name"), TEXT("SubductionLongDuration"));
        Root->SetNumberField(TEXT("steps"), Steps);
        Root->SetNumberField(TEXT("sim_time_my"), Steps * TimeStep_My);
        Root->SetStringField(TEXT("backend"), Backend);
        Root->SetNumberField(TEXT("sample_count"), N);
        Root->SetNumberField(TEXT("seed"), 42);
        // git commit
        FString GitHash; { int32 Exit=1; FString Out, Err; FPlatformProcess::ExecProcess(TEXT("git"), TEXT("rev-parse --short HEAD"), &Exit, &Out, &Err); if (Exit==0) GitHash=Out.TrimStartAndEnd(); else GitHash=TEXT(""); }
        Root->SetStringField(TEXT("git_commit"), GitHash);
        Root->SetNumberField(TEXT("final_max_elev_m"), OutFinalMax);
        Root->SetNumberField(TEXT("final_uplifted_percent"), OutFinalUpliftedPct);
        TSharedRef<FJsonObject> Timing = MakeShared<FJsonObject>();
        Timing->SetNumberField(TEXT("total"), totalMs);
        Root->SetObjectField(TEXT("timing_ms"), Timing);
        Root->SetStringField(TEXT("timeseries_path"), OutCSVPath);

        OutJSONPath = Dir / FString::Printf(TEXT("summary_%s.json"), *Timestamp);
        FString JsonOut; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&JsonOut);
        FJsonSerializer::Serialize(Root, W);
        FFileHelper::SaveStringToFile(JsonOut, *OutJSONPath);
    };

    double Max1=0.0, Pct1=0.0; FString CSV1, JSON1;
    RunOnce(Max1, Pct1, CSV1, JSON1);

    // Repeat run for determinism
    double Max2=0.0, Pct2=0.0; FString CSV2, JSON2;
    RunOnce(Max2, Pct2, CSV2, JSON2);

    // Assertions
    TestTrue(TEXT("sanity: uplift occurred"), Max1 > 0.0);
    TestTrue(TEXT("sanity: some uplifted vertices"), Pct1 > 0.0);
    TestTrue(TEXT("deterministic final max"), FMath::Abs(Max1 - Max2) <= 1e-9);

    UE_LOG(LogTemp, Display, TEXT("[SubductionLongRun] Final Max Elevation=%.6f m, Uplifted=%.3f%%"), Max1, Pct1);
    UE_LOG(LogTemp, Display, TEXT("[SubductionLongRun] CSV: %s"), *CSV1);
    UE_LOG(LogTemp, Display, TEXT("[SubductionLongRun] JSON: %s"), *JSON1);

    return true;
}

