#include "Simulation/SubductionProcessor.h"

#include "Simulation/BoundaryField.h"
#include "Simulation/SubductionFormulas.h"
#include "Simulation/PaperConstants.h"
#include "HAL/IConsoleManager.h"
#include "Simulation/PaperProfiling.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformProcess.h"

namespace Subduction
{
    static void CSRToNeighbors(const TArray<int32>& Offsets, const TArray<int32>& Adj, int32 VertexCount, TArray<TArray<int32>>& Out)
    {
        Out.SetNum(VertexCount);
        for (int32 a = 0; a < VertexCount; ++a)
        {
            const int32 Start = Offsets[a];
            const int32 End = Offsets[a + 1];
            const int32 Count = End - Start;
            Out[a].SetNumUninitialized(Count);
            for (int32 k = 0; k < Count; ++k)
            {
                Out[a][k] = Adj[Start + k];
            }
        }
    }

    FSubductionMetrics ApplyUplift(
        const TArray<FVector3d>& Points,
        const TArray<int32>& CSR_Offsets,
        const TArray<int32>& CSR_Adj,
        const TArray<int32>& PlateIdPerVertex,
        const TArray<FVector3d>& OmegaPerPlate,
        TArray<double>& InOutElevation_m)
    {
        using namespace PaperConstants;
        using namespace SubductionFormulas;

        const double BlockStart = FPlatformTime::Seconds();

        FSubductionMetrics M;

        const int32 N = Points.Num();
        if (N == 0 || CSR_Offsets.Num() != N + 1 || CSR_Adj.Num() == 0)
        {
            return M;
        }

        // Build neighbor list for BoundaryField
        TArray<TArray<int32>> Neighbors;
        CSRToNeighbors(CSR_Offsets, CSR_Adj, N, Neighbors);

        // Classify and get subduction distance field
        BoundaryField::FBoundaryFieldResults BF;
        BoundaryField::ComputeBoundaryFields(Points, Neighbors, PlateIdPerVertex, OmegaPerPlate, BF);

        const TArray<double>& DistToSubduction_km = BF.DistanceToSubductionFront_km;

        // Apply û = u0·f·g·h scaled by delta t (2 My per step per paper)
        const double dt_My = TimeStep_My;

        int32 touched = 0;
        double total = 0.0;
        double maxVal = 0.0;

        auto FindOpposingPlate = [&](int32 v, int32 plateI) -> int32
        {
            // Ring 1
            int32 start = CSR_Offsets[v];
            int32 end = CSR_Offsets[v + 1];
            for (int32 k = start; k < end; ++k)
            {
                const int32 nb = CSR_Adj[k];
                const int32 pj = PlateIdPerVertex.IsValidIndex(nb) ? PlateIdPerVertex[nb] : INDEX_NONE;
                if (pj != INDEX_NONE && pj != plateI)
                {
                    return pj;
                }
            }
            // Ring 2
            for (int32 k = start; k < end; ++k)
            {
                const int32 nb = CSR_Adj[k];
                const int32 nstart = CSR_Offsets[nb];
                const int32 nend = CSR_Offsets[nb + 1];
                for (int32 kk = nstart; kk < nend; ++kk)
                {
                    const int32 nb2 = CSR_Adj[kk];
                    const int32 pj2 = PlateIdPerVertex.IsValidIndex(nb2) ? PlateIdPerVertex[nb2] : INDEX_NONE;
                    if (pj2 != INDEX_NONE && pj2 != plateI)
                    {
                        return pj2;
                    }
                }
            }
            return plateI;
        };

        for (int32 i = 0; i < N; ++i)
        {
            const double d_km = (DistToSubduction_km.IsValidIndex(i)) ? DistToSubduction_km[i] : TNumericLimits<double>::Max();
            const double f = F_DistanceKernel(d_km);
            if (f <= 0.0)
            {
                continue;
            }

            const FVector3d& P = Points[i];
            const int32 PlateI = PlateIdPerVertex.IsValidIndex(i) ? PlateIdPerVertex[i] : INDEX_NONE;
            if (PlateI == INDEX_NONE || !OmegaPerPlate.IsValidIndex(PlateI))
            {
                continue;
            }

            int32 PlateJ = FindOpposingPlate(i, PlateI);

            const FVector3d OmegaI = OmegaPerPlate[PlateI];
            const FVector3d OmegaJ = (OmegaPerPlate.IsValidIndex(PlateJ)) ? OmegaPerPlate[PlateJ] : FVector3d::ZeroVector;
            const double v_rel = ComputeRelativeSurfaceSpeedKmPerMy(OmegaI, OmegaJ, P);
            const double g = G_RelativeSpeedRatio(v_rel);
            const double h = H_ElevationFactor(InOutElevation_m.IsValidIndex(i) ? InOutElevation_m[i] : 0.0);

            const double upliftRate_m_per_My = SubductionUplift_m_per_My * f * g * h;
            const double uplift_m = upliftRate_m_per_My * dt_My;

            if (uplift_m > 0.0 && InOutElevation_m.IsValidIndex(i))
            {
                InOutElevation_m[i] += uplift_m;
                touched++;
                total += uplift_m;
                if (uplift_m > maxVal) maxVal = uplift_m;
            }
        }

        M.VerticesTouched = touched;
        M.TotalUplift_m = total;
        M.MaxUplift_m = maxVal;
        M.ApplyMs = (FPlatformTime::Seconds() - BlockStart) * 1000.0;

        if (IsPaperProfilingEnabled())
        {
            UE_LOG(LogTemp, Display, TEXT("[Subduction] ApplyUplift: Touched=%d Total=%.3f m Max=%.3f m Time=%.2f ms"),
                M.VerticesTouched, M.TotalUplift_m, M.MaxUplift_m, M.ApplyMs);
        }

        return M;
    }

    static FVector3d ProjectToTangent(const FVector3d& V, const FVector3d& PUnit)
    {
        return V - (V.Dot(PUnit)) * PUnit;
    }

    FFoldMetrics UpdateFoldDirections(
        const TArray<FVector3d>& Points,
        const TArray<int32>& CSR_Offsets,
        const TArray<int32>& CSR_Adj,
        const TArray<int32>& PlateIdPerVertex,
        const TArray<FVector3d>& OmegaPerPlate,
        const BoundaryField::FBoundaryFieldResults& Boundary,
        TArray<FVector3d>& InOutFoldVectors)
    {
        using namespace PaperConstants;
        const double t0 = FPlatformTime::Seconds();
        const int32 N = Points.Num();
        InOutFoldVectors.SetNum(N);

        int32 updated = 0;
        double sumDelta = 0.0;
        double maxDelta = 0.0;
        double sumCoherence = 0.0;

        // Build quick lookup of incident convergent edges for each vertex
        TArray<TArray<int32>> IncidentConvEdges; // indices into Boundary.Edges
        IncidentConvEdges.SetNum(N);
        for (int32 e = 0; e < Boundary.Edges.Num(); ++e)
        {
            if (!Boundary.Classifications.IsValidIndex(e)) continue;
            if (Boundary.Classifications[e] != BoundaryField::EBoundaryClass::Convergent) continue;
            const int32 a = Boundary.Edges[e].Key;
            const int32 b = Boundary.Edges[e].Value;
            if (a >= 0 && a < N) IncidentConvEdges[a].Add(e);
            if (b >= 0 && b < N) IncidentConvEdges[b].Add(e);
        }

        for (int32 i = 0; i < N; ++i)
        {
            const FVector3d& P = Points[i];
            const int32 PlateI = PlateIdPerVertex.IsValidIndex(i) ? PlateIdPerVertex[i] : INDEX_NONE;
            if (PlateI == INDEX_NONE || !OmegaPerPlate.IsValidIndex(PlateI))
            {
                continue;
            }

            // Skip outside influence band
            const double dfront = Boundary.DistanceToSubductionFront_km.IsValidIndex(i) ? Boundary.DistanceToSubductionFront_km[i] : TNumericLimits<double>::Max();
            if (dfront > SubductionDistance_km)
            {
                continue;
            }

            // Find nearest convergent edge touching this vertex or a neighbor
            int32 UseEdge = INDEX_NONE;
            if (IncidentConvEdges[i].Num() > 0)
            {
                UseEdge = IncidentConvEdges[i][0];
            }
            else
            {
                const int32 start = CSR_Offsets[i];
                const int32 end = CSR_Offsets[i + 1];
                double bestD = TNumericLimits<double>::Max();
                for (int32 k = start; k < end; ++k)
                {
                    const int32 nb = CSR_Adj[k];
                    for (int32 idx : IncidentConvEdges[nb])
                    {
                        const int32 a = Boundary.Edges[idx].Key;
                        const int32 b = Boundary.Edges[idx].Value;
                        const FVector3d q = (Points[a] + Points[b]).GetSafeNormal();
                        const double dot = FMath::Clamp(q.Dot(P), -1.0, 1.0);
                        const double theta = FMath::Acos(dot);
                        if (theta < bestD)
                        {
                            bestD = theta;
                            UseEdge = idx;
                        }
                    }
                }
            }

            if (UseEdge == INDEX_NONE)
            {
                continue;
            }

            // Decide subducting vs overriding per edge orientation
            const int32 a = Boundary.Edges[UseEdge].Key;
            const int32 b = Boundary.Edges[UseEdge].Value;
            const int32 pa = PlateIdPerVertex[a];
            const int32 pb = PlateIdPerVertex[b];
            if (pa == INDEX_NONE || pb == INDEX_NONE || pa == pb) continue;

            const FVector3d M = (Points[a] + Points[b]).GetSafeNormal();
            const FVector3d t = ((Points[b] - Points[a]) - ((Points[b] - Points[a]).Dot(M)) * M).GetSafeNormal();
            const FVector3d Nb = FVector3d::CrossProduct(M, t);

            const FVector3d Si_edge = FVector3d::CrossProduct(OmegaPerPlate[pa], M) * PlanetRadius_km;
            const FVector3d Sj_edge = FVector3d::CrossProduct(OmegaPerPlate[pb], M) * PlanetRadius_km;
            const double pi = Si_edge.Dot(Nb);
            const double pj = Sj_edge.Dot(Nb);

            int32 SubPlate = pa;
            int32 OverPlate = pb;
            if (pj < pi)
            {
                SubPlate = pb;
                OverPlate = pa;
            }

            const FVector3d Si = FVector3d::CrossProduct(OmegaPerPlate[SubPlate], P) * PlanetRadius_km;
            const FVector3d Sj = FVector3d::CrossProduct(OmegaPerPlate[PlateI], P) * PlanetRadius_km;
            FVector3d Rel = Si - Sj;
            Rel = ProjectToTangent(Rel, P);

            const FVector3d Delta = Rel * (FoldDirectionBeta * TimeStep_My);
            FVector3d& F = InOutFoldVectors[i];

            // Initialize if near-zero
            if (!F.IsNearlyZero())
            {
                F = ProjectToTangent(F, P);
            }
            if (F.IsNearlyZero())
            {
                F = Delta;
            }
            else
            {
                F += Delta;
            }

            const double d = Delta.Size();
            if (d > 0.0)
            {
                updated++;
                sumDelta += d;
                if (d > maxDelta) maxDelta = d;
            }

            // Normalize and keep tangent
            const double len = F.Size();
            if (len > 0.0)
            {
                F /= len;
            }

            // Coherence: dot between updated fold and relative direction (both tangent-unit)
            const double relLen = Rel.Size();
            if (relLen > 0.0)
            {
                const FVector3d RelDir = Rel / relLen;
                sumCoherence += FMath::Abs(F.Dot(RelDir));
            }
        }

        FFoldMetrics Out;
        Out.VerticesUpdated = updated;
        Out.MeanDelta = (updated > 0) ? (sumDelta / updated) : 0.0;
        Out.MaxDelta = maxDelta;
        Out.MeanCoherence = (updated > 0) ? (sumCoherence / updated) : 0.0;
        Out.ApplyMs = (FPlatformTime::Seconds() - t0) * 1000.0;

        if (IsPaperProfilingEnabled())
        {
            UE_LOG(LogTemp, Display, TEXT("[Subduction] UpdateFoldDirections: Updated=%d MeanΔ=%.6f MaxΔ=%.6f Coherence=%.4f Time=%.2f ms"),
                Out.VerticesUpdated, Out.MeanDelta, Out.MaxDelta, Out.MeanCoherence, Out.ApplyMs);
        }
        return Out;
    }

    FSlabPullMetrics ApplySlabPull(
        const TArray<FVector3d>& PlateCentroids,
        const TArray<FConvergentEdge>& ConvergentEdges,
        const TArray<FVector3d>& Points,
        TArray<FVector3d>& InOutOmegaPerPlate)
    {
        using namespace PaperConstants;
        const double t0 = FPlatformTime::Seconds();

        const int32 PlateCount = InOutOmegaPerPlate.Num();
        TArray<FVector3d> Accel;
        Accel.Init(FVector3d::ZeroVector, PlateCount);

        // Deterministic: iterate edges in stored order
        for (const FConvergentEdge& E : ConvergentEdges)
        {
            const int32 a = E.A;
            const int32 b = E.B;
            if (!Points.IsValidIndex(a) || !Points.IsValidIndex(b))
            {
                continue;
            }
            const int32 SubPlate = E.SubductingPlateId;
            if (!PlateCentroids.IsValidIndex(SubPlate) || !InOutOmegaPerPlate.IsValidIndex(SubPlate))
            {
                continue;
            }

            const FVector3d q = (Points[a] + Points[b]).GetSafeNormal();
            const FVector3d& Ci = PlateCentroids[SubPlate];
            FVector3d Cross = FVector3d::CrossProduct(Ci, q);
            const double len = Cross.Size();
            if (len > 0.0)
            {
                Cross /= len; // unit
                Accel[SubPlate] += Cross;
            }
        }

        // Apply accelerations to omegas; compute metrics
        int32 platesUpdated = 0;
        double sumDelta = 0.0;
        double maxDelta = 0.0;
        for (int32 p = 0; p < PlateCount; ++p)
        {
            const FVector3d Delta = Accel[p] * (SlabPullEpsilon * TimeStep_My);
            const double mag = Delta.Size();
            if (mag > 0.0)
            {
                InOutOmegaPerPlate[p] += Delta;
                platesUpdated++;
                sumDelta += mag;
                if (mag > maxDelta) maxDelta = mag;
            }
        }

        FSlabPullMetrics Out;
        Out.PlatesUpdated = platesUpdated;
        Out.MeanDeltaOmega = (platesUpdated > 0) ? (sumDelta / platesUpdated) : 0.0;
        Out.MaxDeltaOmega = maxDelta;
        Out.ApplyMs = (FPlatformTime::Seconds() - t0) * 1000.0;

        if (IsPaperProfilingEnabled())
        {
            UE_LOG(LogTemp, Display, TEXT("[Subduction] ApplySlabPull: Plates=%d Mean|Δω|=%.6g Max|Δω|=%.6g Time=%.2f ms"),
                Out.PlatesUpdated, Out.MeanDeltaOmega, Out.MaxDeltaOmega, Out.ApplyMs);
        }
        return Out;
    }

    FString WritePhase3MetricsJson(
        const FString& TestName,
        const FString& Backend,
        int32 SampleCount,
        int32 Seed,
        int32 SimulationSteps,
        int32 ConvergentCount,
        int32 DivergentCount,
        int32 TransformCount,
        const FSubductionMetrics& Uplift,
        const FFoldMetrics& Fold,
        double ClassifyMs,
        const FSlabPullMetrics& Slab)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("test_name"), TestName);
        Root->SetStringField(TEXT("phase"), TEXT("3-subduction"));
        Root->SetStringField(TEXT("backend"), Backend);
        Root->SetNumberField(TEXT("sample_count"), SampleCount);
        Root->SetNumberField(TEXT("seed"), Seed);
        Root->SetNumberField(TEXT("simulation_steps"), SimulationSteps);
        // Try to populate git commit (short hash); leave empty on failure
        FString GitHash;
        {
            int32 ExitCode = 1;
            FString StdOut, StdErr;
            FPlatformProcess::ExecProcess(TEXT("git"), TEXT("rev-parse --short HEAD"), &ExitCode, &StdOut, &StdErr);
            if (ExitCode == 0)
            {
                GitHash = StdOut.TrimStartAndEnd();
            }
            else
            {
                GitHash = TEXT("");
            }
        }
        Root->SetStringField(TEXT("git_commit"), GitHash);

        TSharedRef<FJsonObject> Boundary = MakeShared<FJsonObject>();
        Boundary->SetNumberField(TEXT("convergent"), ConvergentCount);
        Boundary->SetNumberField(TEXT("divergent"), DivergentCount);
        Boundary->SetNumberField(TEXT("transform"), TransformCount);
        Root->SetObjectField(TEXT("boundary_counts"), Boundary);

        TSharedRef<FJsonObject> UpliftObj = MakeShared<FJsonObject>();
        const double meanUplift = (Uplift.VerticesTouched > 0) ? (Uplift.TotalUplift_m / Uplift.VerticesTouched) : 0.0;
        const double upliftPercent = (SampleCount > 0) ? (100.0 * Uplift.VerticesTouched / SampleCount) : 0.0;
        UpliftObj->SetNumberField(TEXT("vertices_uplifted"), Uplift.VerticesTouched);
        UpliftObj->SetNumberField(TEXT("vertices_uplifted_percent"), upliftPercent);
        UpliftObj->SetNumberField(TEXT("mean_uplift_m"), meanUplift);
        UpliftObj->SetNumberField(TEXT("max_uplift_m"), Uplift.MaxUplift_m);
        Root->SetObjectField(TEXT("uplift_stats"), UpliftObj);

        TSharedRef<FJsonObject> FoldObj = MakeShared<FJsonObject>();
        FoldObj->SetNumberField(TEXT("mean_dot_product"), Fold.MeanCoherence);
        Root->SetObjectField(TEXT("fold_coherence"), FoldObj);

        TSharedRef<FJsonObject> Timing = MakeShared<FJsonObject>();
        Timing->SetNumberField(TEXT("classify"), ClassifyMs);
        Timing->SetNumberField(TEXT("uplift"), Uplift.ApplyMs);
        Timing->SetNumberField(TEXT("slab_pull"), Slab.ApplyMs);
        Timing->SetNumberField(TEXT("total"), ClassifyMs + Uplift.ApplyMs + Fold.ApplyMs + Slab.ApplyMs);
        Root->SetObjectField(TEXT("timing_ms"), Timing);

        const FString Dir = FPaths::ProjectDir() / TEXT("Docs/Automation/Validation/Phase3");
        IFileManager::Get().MakeDirectory(*Dir, true);
        // Use UTC timestamp for filename to avoid local timezone variance
        const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
        const FString Path = Dir / FString::Printf(TEXT("summary_%s.json"), *Timestamp);

        FString Output;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        FJsonSerializer::Serialize(Root, Writer);
        FFileHelper::SaveStringToFile(Output, *Path);

        if (IsPaperProfilingEnabled())
        {
            UE_LOG(LogTemp, Display, TEXT("[Phase3] Metrics JSON written: %s | boundary(con=%d,div=%d,tr=%d) uplift(touched=%d, max=%.3fm) fold(coh=%.3f)"),
                *Path, ConvergentCount, DivergentCount, TransformCount, Uplift.VerticesTouched, Uplift.MaxUplift_m, Fold.MeanCoherence);
        }

        return Path;
    }
}
