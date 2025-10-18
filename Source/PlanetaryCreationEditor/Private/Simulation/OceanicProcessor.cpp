#include "Simulation/OceanicProcessor.h"

#include "Simulation/PaperConstants.h"
#include "Simulation/PaperProfiling.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

namespace Oceanic
{
    using namespace PaperConstants;

    static inline void UpdateRidgeCacheForVertex(int32 Index, const FVector3d& P, const FVector3d& NearestRidgeQ, FRidgeCache& Cache, int32 RequiredSize)
    {
        const FVector3d R = FVector3d::CrossProduct(P - NearestRidgeQ, P);
        const double len = R.Size();
        if (len > 0.0)
        {
            if (RequiredSize > 0 && Cache.RidgeDirections.Num() != RequiredSize)
            {
                Cache.RidgeDirections.SetNumZeroed(RequiredSize);
            }
            Cache.RidgeDirections[Index] = (FVector3f)(R / len);
        }
    }

    static inline double AngularDistance(const FVector3d& A, const FVector3d& B)
    {
        const double dot = FMath::Clamp(A.Dot(B), -1.0, 1.0);
        return FMath::Acos(dot);
    }

    static inline FVector3d TangentFromPQ(const FVector3d& P, const FVector3d& Q)
    {
        // r = (p - q) x p (projected tangent and normalized)
        FVector3d R = FVector3d::CrossProduct(P - Q, P);
        const double len = R.Size();
        return (len > 0.0) ? (R / len) : FVector3d::ZeroVector;
    }

    void BuildRidgeCache(
        const TArray<FVector3d>& Points,
        const TArray<int32>& CSR_Offsets,
        const TArray<int32>& CSR_Adj,
        const BoundaryField::FBoundaryFieldResults& Boundary,
        FRidgeCache& InOutCache)
    {
        const int32 N = Points.Num();
        InOutCache.RidgeDirections.SetNumZeroed(N);
        InOutCache.Version++;

        // Collect divergent edge midpoints
        TArray<FVector3d> RidgeMidpoints;
        for (int32 e = 0; e < Boundary.Edges.Num(); ++e)
        {
            if (Boundary.Classifications.IsValidIndex(e) && Boundary.Classifications[e] == BoundaryField::EBoundaryClass::Divergent)
            {
                const int32 a = Boundary.Edges[e].Key;
                const int32 b = Boundary.Edges[e].Value;
                if (Points.IsValidIndex(a) && Points.IsValidIndex(b))
                {
                    RidgeMidpoints.Add((Points[a] + Points[b]).GetSafeNormal());
                }
            }
        }
        if (RidgeMidpoints.Num() == 0) return;

        // For each vertex, assign a ridge direction if close to a ridge midpoint
        const double MaxR_km = 1000.0; // within 1000 km of ridge
        const double MaxR_ang = KmToGeodesicRadians(MaxR_km);
        for (int32 i = 0; i < N; ++i)
        {
            const FVector3d& P = Points[i];
            double bestDang = TNumericLimits<double>::Max();
            FVector3d bestQ = FVector3d::ZeroVector;
            for (const FVector3d& Q : RidgeMidpoints)
            {
                const double dang = AngularDistance(P, Q);
                if (dang < bestDang)
                {
                    bestDang = dang; bestQ = Q;
                }
            }
            if (bestDang <= MaxR_ang)
            {
                UpdateRidgeCacheForVertex(i, P, bestQ, InOutCache, N);
            }
        }
    }

    static inline double RidgeTemplateElevation_m(double dGamma_km)
    {
        // Quadratic blend between ridge crest and abyssal plain
        const double zr = RidgeElevation_m;   // e.g., -1000 m
        const double za = AbyssalElevation_m; // e.g., -6000 m
        const double t = FMath::Clamp(dGamma_km / 1000.0, 0.0, 1.0);
        const double s = t * t; // quadratic smooth falloff
        return FMath::Lerp(zr, za, s);
    }

    static inline double EdgeLengthKm(const FVector3d& A, const FVector3d& B)
    {
        const double dang = AngularDistance(A, B);
        return GeodesicRadiansToKm(dang);
    }

    FOceanicMetrics ApplyOceanicCrust(
        const TArray<FVector3d>& Points,
        const TArray<int32>& CSR_Offsets,
        const TArray<int32>& CSR_Adj,
        const BoundaryField::FBoundaryFieldResults& Boundary,
        const TArray<int32>& PlateIdPerVertex,
        const TArray<uint8>& PlateCrustTypePerPlate,
        const TArray<double>& PlateBaselineElevation_m,
        TArray<double>& InOutElevation_m,
        FRidgeCache* OptionalRidgeCacheOrNull)
    {
        const double t0 = FPlatformTime::Seconds();
        FOceanicMetrics M;
        const int32 N = Points.Num();
        if (N == 0 || InOutElevation_m.Num() != N) return M;

        // Precompute ridge midpoints and ridge lengths
        TArray<FVector3d> RidgeMidpoints;
        double RidgeLen_km = 0.0;
        for (int32 e = 0; e < Boundary.Edges.Num(); ++e)
        {
            if (Boundary.Classifications.IsValidIndex(e) && Boundary.Classifications[e] == BoundaryField::EBoundaryClass::Divergent)
            {
                const int32 a = Boundary.Edges[e].Key;
                const int32 b = Boundary.Edges[e].Value;
                if (Points.IsValidIndex(a) && Points.IsValidIndex(b))
                {
                    RidgeMidpoints.Add((Points[a] + Points[b]).GetSafeNormal());
                    RidgeLen_km += EdgeLengthKm(Points[a], Points[b]);
                }
            }
        }
        M.RidgeLength_km = RidgeLen_km;
        int32 NumInterpolated = 0;
        int32 NumFallback = 0;

        // Update elevations
        const double Eps = 1e-9;
        double AlphaSum = 0.0; int32 AlphaCount = 0;
        for (int32 i = 0; i < N; ++i)
        {
            const FVector3d& P = Points[i];
            const double dGamma_km = Boundary.DistanceToRidge_km.IsValidIndex(i) ? Boundary.DistanceToRidge_km[i] : 1.0e9;
            const double dP_km = Boundary.DistanceToPlateBoundary_km.IsValidIndex(i) ? Boundary.DistanceToPlateBoundary_km[i] : 1.0e9;

            const double denom = FMath::Max(dGamma_km + dP_km, Eps);
            double alpha = dGamma_km / denom; // [0,1]
            alpha = FMath::Clamp(alpha, 0.0, 1.0);

            M.MinAlpha = FMath::Min(M.MinAlpha, alpha);
            M.MaxAlpha = FMath::Max(M.MaxAlpha, alpha);
            AlphaSum += alpha; AlphaCount++;

            // Strict oceanic mask: only modify if the vertex's plate is oceanic
            int32 pid = PlateIdPerVertex.IsValidIndex(i) ? PlateIdPerVertex[i] : INDEX_NONE;
            const bool bIsOceanic = (pid != INDEX_NONE && PlateCrustTypePerPlate.IsValidIndex(pid) && PlateCrustTypePerPlate[pid] == 0);
            if (bIsOceanic)
            {
                const double zGamma = RidgeTemplateElevation_m(dGamma_km);
                double zBar = PlateBaselineElevation_m.IsValidIndex(i) ? PlateBaselineElevation_m[i] : InOutElevation_m[i];

                // Interpolate baseline across nearest divergent edge when near boundary
                if (alpha < 0.999)
                {
                    // Find nearest divergent edge via ridge midpoints
                    double bestDang = TNumericLimits<double>::Max();
                    int32 bestEdge = -1;
                    for (int32 e = 0; e < Boundary.Edges.Num(); ++e)
                    {
                        if (Boundary.Classifications.IsValidIndex(e) && Boundary.Classifications[e] == BoundaryField::EBoundaryClass::Divergent)
                        {
                            const int32 a = Boundary.Edges[e].Key;
                            const int32 b = Boundary.Edges[e].Value;
                            if (!Points.IsValidIndex(a) || !Points.IsValidIndex(b)) continue;
                            const FVector3d Q = (Points[a] + Points[b]).GetSafeNormal();
                            const double dang = AngularDistance(P, Q);
                            if (dang < bestDang)
                            {
                                bestDang = dang; bestEdge = e;
                            }
                        }
                    }

                    bool gotI = false, gotJ = false; double di = 0.0, dj = 0.0; double z_i = zBar, z_j = zBar;
                    if (bestEdge >= 0)
                    {
                        const int32 a = Boundary.Edges[bestEdge].Key;
                        const int32 b = Boundary.Edges[bestEdge].Value;
                        const int32 pid_a = PlateIdPerVertex.IsValidIndex(a) ? PlateIdPerVertex[a] : INDEX_NONE;
                        const int32 pid_b = PlateIdPerVertex.IsValidIndex(b) ? PlateIdPerVertex[b] : INDEX_NONE;
                        if (pid_a != INDEX_NONE && pid_b != INDEX_NONE && pid_a != pid_b)
                        {
                            auto findNearestOnPlate = [&](int32 plateId, int32 center, int maxRing, int32& outIdx, double& outDistKm)
                            {
                                outIdx = -1; outDistKm = TNumericLimits<double>::Max();
                                TSet<int32> visited; visited.Add(center);
                                TArray<int32> frontier; frontier.Add(center);
                                for (int ring = 0; ring < maxRing && frontier.Num() > 0; ++ring)
                                {
                                    frontier.Sort();
                                    TArray<int32> next; next.Reserve(frontier.Num() * 6);
                                    for (int32 v : frontier)
                                    {
                                        if (!(v >= 0 && v + 1 < CSR_Offsets.Num())) continue;
                                        const int32 start = CSR_Offsets[v];
                                        const int32 end = CSR_Offsets[v + 1];
                                        for (int32 k = start; k < end; ++k)
                                        {
                                            const int32 nb = CSR_Adj[k];
                                            if (visited.Contains(nb)) continue;
                                            visited.Add(nb);
                                            next.Add(nb);
                                            if (PlateIdPerVertex.IsValidIndex(nb) && PlateIdPerVertex[nb] == plateId)
                                            {
                                                const double dkm = GeodesicRadiansToKm(AngularDistance(Points[nb], P));
                                                if (dkm < outDistKm || (FMath::IsNearlyEqual(dkm, outDistKm) && nb < outIdx))
                                                {
                                                    outDistKm = dkm; outIdx = nb;
                                                }
                                            }
                                        }
                                    }
                                    frontier = MoveTemp(next);
                                }
                            };

                            int32 idxI = -1, idxJ = -1; double dI = TNumericLimits<double>::Max(), dJ = TNumericLimits<double>::Max();
                            findNearestOnPlate(pid_a, i, 2, idxI, dI);
                            findNearestOnPlate(pid_b, i, 2, idxJ, dJ);
                            if (idxI >= 0) { z_i = PlateBaselineElevation_m[idxI]; di = dI; gotI = true; }
                            if (idxJ >= 0) { z_j = PlateBaselineElevation_m[idxJ]; dj = dJ; gotJ = true; }
                        }
                    }
                    if (gotI && gotJ)
                    {
                        const double sum = FMath::Max(di + dj, Eps);
                        const double wi = dj / sum; const double wj = di / sum;
                        zBar = wi * z_i + wj * z_j;
                        ++NumInterpolated;
                    }
                    else
                    {
                        ++NumFallback;
                    }
                }

                const double zNew = alpha * zBar + (1.0 - alpha) * zGamma;
                if (zNew != InOutElevation_m[i])
                {
                    InOutElevation_m[i] = zNew;
                    M.VerticesUpdated++;
                }
            }

            // Optional ridge direction update near ridge
            if (OptionalRidgeCacheOrNull)
            {
                if (dGamma_km <= 1000.0 && RidgeMidpoints.Num() > 0)
                {
                    // Nearest ridge midpoint
                    double bestDang = TNumericLimits<double>::Max();
                    FVector3d bestQ = FVector3d::ZeroVector;
                    for (const FVector3d& Q : RidgeMidpoints)
                    {
                        const double dang = AngularDistance(P, Q);
                        if (dang < bestDang) { bestDang = dang; bestQ = Q; }
                    }
                    UpdateRidgeCacheForVertex(i, P, bestQ, *OptionalRidgeCacheOrNull, N);
                }
            }
        }
        M.MeanAlpha = (AlphaCount > 0) ? (AlphaSum / (double)AlphaCount) : 0.0;
        M.ApplyMs = (FPlatformTime::Seconds() - t0) * 1000.0;
        if (IsPaperProfilingEnabled())
        {
            UE_LOG(LogTemp, Log, TEXT("[Phase5] Oceanic baseline: interpolated=%d, fallback=%d"), NumInterpolated, NumFallback);
        }
        return M;
    }

    FString WritePhase5MetricsJson(
        const FString& BackendName,
        int32 SampleCount,
        int32 Seed,
        const FOceanicMetrics& Metrics)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("phase"), TEXT("5-oceanic"));
        Root->SetStringField(TEXT("backend"), BackendName);
        Root->SetNumberField(TEXT("sample_count"), SampleCount);
        Root->SetNumberField(TEXT("seed"), Seed);

        // Git commit (short hash) if available
        FString GitHash;
        {
            int32 ExitCode = 1; FString StdOut, StdErr;
            FPlatformProcess::ExecProcess(TEXT("git"), TEXT("rev-parse --short HEAD"), &ExitCode, &StdOut, &StdErr);
            if (ExitCode == 0) GitHash = StdOut.TrimStartAndEnd(); else GitHash = TEXT("");
        }
        Root->SetStringField(TEXT("git_commit"), GitHash);

        TSharedRef<FJsonObject> MetricsObj = MakeShared<FJsonObject>();
        MetricsObj->SetNumberField(TEXT("vertices_updated"), Metrics.VerticesUpdated);
        MetricsObj->SetNumberField(TEXT("mean_alpha"), Metrics.MeanAlpha);
        MetricsObj->SetNumberField(TEXT("min_alpha"), Metrics.MinAlpha);
        MetricsObj->SetNumberField(TEXT("max_alpha"), Metrics.MaxAlpha);
        MetricsObj->SetNumberField(TEXT("ridge_length_km"), Metrics.RidgeLength_km);
        MetricsObj->SetNumberField(TEXT("cadence_steps"), Metrics.CadenceSteps);
        Root->SetObjectField(TEXT("metrics"), MetricsObj);

        TSharedRef<FJsonObject> Timing = MakeShared<FJsonObject>();
        Timing->SetNumberField(TEXT("apply"), Metrics.ApplyMs);
        Root->SetObjectField(TEXT("timing_ms"), Timing);

        const FString Dir = FPaths::ProjectDir() / TEXT("Docs/Automation/Validation/Phase5");
        IFileManager::Get().MakeDirectory(*Dir, true);
        const FDateTime Now = FDateTime::UtcNow();
        const FString Timestamp = FString::Printf(TEXT("%04d%02d%02d_%02d%02d%02d"), Now.GetYear(), Now.GetMonth(), Now.GetDay(), Now.GetHour(), Now.GetMinute(), Now.GetSecond());
        const FString Path = Dir / FString::Printf(TEXT("summary_%s.json"), *Timestamp);

        FString Output;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        FJsonSerializer::Serialize(Root, Writer);
        FFileHelper::SaveStringToFile(Output, *Path);
        if (IsPaperProfilingEnabled())
        {
            UE_LOG(LogTemp, Display, TEXT("[Phase5] Metrics JSON written: %s"), *Path);
        }
        return Path;
    }
}
