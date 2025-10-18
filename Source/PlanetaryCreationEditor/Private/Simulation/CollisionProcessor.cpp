#include "Simulation/CollisionProcessor.h"

#include "Simulation/PaperConstants.h"
#include "Simulation/PaperProfiling.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

namespace Collision
{
    using namespace PaperConstants;

    static inline FVector3d SurfaceVelocityKmPerMy(const FVector3d& Omega_rad_per_My, const FVector3d& P)
    {
        return FVector3d::CrossProduct(Omega_rad_per_My, P) * PlanetRadius_km;
    }

    static inline double AngularDistance(const FVector3d& A, const FVector3d& B)
    {
        const double dot = FMath::Clamp(A.Dot(B), -1.0, 1.0);
        return FMath::Acos(dot);
    }

    bool DetectCollisions(
        const TArray<FVector3d>& Points,
        const TArray<int32>& PlateIdPerVertex,
        const TArray<FVector3d>& OmegaPerPlate,
        const TArray<uint8>& PlateCrustType,
        const TArray<int32>& CSR_Offsets,
        const TArray<int32>& CSR_Adj,
        const BoundaryField::FBoundaryFieldResults& Boundary,
        TArray<FCollisionEvent>& OutEvents)
    {
        OutEvents.Reset();

        // Candidate events from convergent edges with continental-continental plates
        struct TmpEvent { FVector3d Q; int32 A; int32 B; int32 Pa; int32 Pb; double A_km2; };
        TArray<TmpEvent> Candidates;

        for (int32 e = 0; e < Boundary.Edges.Num(); ++e)
        {
            if (!Boundary.Classifications.IsValidIndex(e)) continue;
            if (Boundary.Classifications[e] != BoundaryField::EBoundaryClass::Convergent) continue;
            const int32 a = Boundary.Edges[e].Key;
            const int32 b = Boundary.Edges[e].Value;
            if (!Points.IsValidIndex(a) || !Points.IsValidIndex(b)) continue;
            const int32 pa = PlateIdPerVertex.IsValidIndex(a) ? PlateIdPerVertex[a] : INDEX_NONE;
            const int32 pb = PlateIdPerVertex.IsValidIndex(b) ? PlateIdPerVertex[b] : INDEX_NONE;
            if (pa == INDEX_NONE || pb == INDEX_NONE || pa == pb) continue;
            const bool ContA = PlateCrustType.IsValidIndex(pa) ? (PlateCrustType[pa] != 0) : false;
            const bool ContB = PlateCrustType.IsValidIndex(pb) ? (PlateCrustType[pb] != 0) : false;
            if (!(ContA && ContB)) continue; // continental-continental only

            const FVector3d Q = (Points[a] + Points[b]).GetSafeNormal();

            // Approximate terrane area deterministically (fallback): 1e6 km^2
            const double A_km2 = 1.0e6;

            Candidates.Add({Q, a, b, pa, pb, A_km2});
        }

        // Merge near-duplicates within 0.5 degrees if they refer to the same plate pair (sorted ids)
        const double MergeThresh = 0.5 * PI / 180.0; // radians
        for (const TmpEvent& C : Candidates)
        {
            const int32 lo = FMath::Min(C.Pa, C.Pb);
            const int32 hi = FMath::Max(C.Pa, C.Pb);

            bool Merged = false;
            for (FCollisionEvent& E : OutEvents)
            {
                if (!((E.CarrierPlateId == lo) && (E.TargetPlateId == hi))) continue;
                const double dang = AngularDistance(E.CenterUnit, C.Q);
                if (dang <= MergeThresh)
                {
                    // Average centers deterministically then renormalize; average area
                    const FVector3d Avg = (E.CenterUnit + C.Q).GetSafeNormal();
                    E.CenterUnit = Avg;
                    E.TerraneArea_km2 = 0.5 * (E.TerraneArea_km2 + C.A_km2);
                    Merged = true;
                    break;
                }
            }
            if (!Merged)
            {
                FCollisionEvent E;
                E.CenterUnit = C.Q;
                E.TerraneArea_km2 = C.A_km2;
                E.CarrierPlateId = lo;
                E.TargetPlateId = hi;
                E.PeakGuardrail_m = 0.0; // service may override
                OutEvents.Add(E);
            }
        }

        return OutEvents.Num() > 0;
    }

    static FVector3d ProjectToTangent(const FVector3d& V, const FVector3d& P)
    {
        return V - (V.Dot(P)) * P;
    }

    FCollisionMetrics ApplyCollisionSurge(
        const TArray<FVector3d>& Points,
        const TArray<int32>& AffectedVertexIndices,
        const FCollisionEvent& Event,
        TArray<double>& InOutElevation_m,
        TArray<FVector3d>* InOutFoldVectorsOrNull)
    {
        const double t0 = FPlatformTime::Seconds();
        FCollisionMetrics M;

        if (Event.TerraneArea_km2 <= 0.0 || AffectedVertexIndices.Num() == 0)
        {
            return M;
        }

        // Derive r_ang from affected set for quartic shape
        const FVector3d Q = Event.CenterUnit.GetSafeNormal();
        double r_ang = 0.0;
        for (int32 idx : AffectedVertexIndices)
        {
            if (!Points.IsValidIndex(idx)) continue;
            r_ang = FMath::Max(r_ang, AngularDistance(Points[idx], Q));
        }
        if (r_ang <= 0.0)
        {
            return M;
        }

        // Peak height in meters: Δz_peak = Δc[km^-1] * A[km^2] * 1000
        double Peak_m = CollisionCoefficient_per_km * Event.TerraneArea_km2 * 1000.0;
        if (Event.PeakGuardrail_m > 0.0)
        {
            Peak_m = FMath::Min(Peak_m, Event.PeakGuardrail_m);
        }
        M.CollisionCount = 1;
        M.MaxPeak_m = Peak_m;

        // Apply quartic falloff
        for (int32 idx : AffectedVertexIndices)
        {
            if (!Points.IsValidIndex(idx) || !InOutElevation_m.IsValidIndex(idx)) continue;
            const FVector3d& P = Points[idx];
            const double d = AngularDistance(P, Q);
            if (d > r_ang) continue;
            const double t = d / r_ang;
            const double w = FMath::Square(1.0 - FMath::Square(t)); // (1 - t^2)^2
            const double dz = Peak_m * w;
            InOutElevation_m[idx] += dz;

            if (InOutFoldVectorsOrNull)
            {
                TArray<FVector3d>& Folds = *InOutFoldVectorsOrNull;
                if (Folds.IsValidIndex(idx))
                {
                    FVector3d Rad = ProjectToTangent(P - Q, P);
                    const double len = Rad.Size();
                    if (len > 0.0)
                    {
                        Folds[idx] = Rad / len;
                    }
                }
            }
        }

        M.ApplyMs = (FPlatformTime::Seconds() - t0) * 1000.0;
        return M;
    }

    FString WritePhase4MetricsJson(
        const FString& BackendName,
        int32 SampleCount,
        int32 Seed,
        const FCollisionMetrics& Metrics)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("phase"), TEXT("4-collision"));
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

        // Metrics payload (keep fields aligned with Phase4 schema)
        TSharedRef<FJsonObject> MetricsObj = MakeShared<FJsonObject>();
        MetricsObj->SetNumberField(TEXT("collision_count"), Metrics.CollisionCount);
        MetricsObj->SetNumberField(TEXT("surge_peak_m"), Metrics.MaxPeak_m);
        MetricsObj->SetNumberField(TEXT("rifting_count"), 0);            // not implemented yet
        MetricsObj->SetNumberField(TEXT("fragments_per_rift"), 0.0);      // not implemented yet
        Root->SetObjectField(TEXT("metrics"), MetricsObj);

        // Timing payload (collision/fold/rift/total). Only collision available now.
        TSharedRef<FJsonObject> Timing = MakeShared<FJsonObject>();
        Timing->SetNumberField(TEXT("collision"), Metrics.ApplyMs);
        Timing->SetNumberField(TEXT("fold"), 0.0);
        Timing->SetNumberField(TEXT("rift"), 0.0);
        Timing->SetNumberField(TEXT("total"), Metrics.ApplyMs);
        Root->SetObjectField(TEXT("timing_ms"), Timing);

        const FString Dir = FPaths::ProjectDir() / TEXT("Docs/Automation/Validation/Phase4");
        IFileManager::Get().MakeDirectory(*Dir, true);
        // Clean up any stale template-named file from prior runs
        const FString StaleTemplate = Dir / TEXT("summary_yyyyMMdd_HHmmss.json");
        if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*StaleTemplate))
        {
            FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*StaleTemplate);
        }

        // Timestamped filename (UTC) — build manually to avoid token issues
        const FDateTime Now = FDateTime::UtcNow();
        const FString Timestamp = FString::Printf(TEXT("%04d%02d%02d_%02d%02d%02d"),
            Now.GetYear(), Now.GetMonth(), Now.GetDay(), Now.GetHour(), Now.GetMinute(), Now.GetSecond());
        const FString Path = Dir / FString::Printf(TEXT("summary_%s.json"), *Timestamp);

        FString Output;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        FJsonSerializer::Serialize(Root, Writer);
        FFileHelper::SaveStringToFile(Output, *Path);

        if (IsPaperProfilingEnabled())
        {
            UE_LOG(LogTemp, Display, TEXT("[Phase4] Metrics JSON written: %s"), *Path);
        }
        return Path;
    }
}
