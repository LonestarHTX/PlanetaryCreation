#include "Simulation/ErosionProcessor.h"

#include "Simulation/PaperConstants.h"
#include "Simulation/PaperProfiling.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

namespace Erosion
{
    using namespace PaperConstants;

    static inline bool IsContinental(uint8 CrustTag) { return CrustTag != 0; }
    static inline bool IsOceanic(uint8 CrustTag) { return CrustTag == 0; }

    FErosionMetrics ApplyErosionAndDampening(
        const TArray<FVector3d>& Points,
        const TArray<int32>& PlateIdPerVertex,
        const TArray<uint8>& PlateCrustTypePerPlate,
        const BoundaryField::FBoundaryFieldResults& Boundary,
        TArray<double>& InOutElevation_m,
        double TrenchBandKm)
    {
        FErosionMetrics M;
        const int32 N = InOutElevation_m.Num();
        if (N == 0) return M;

        // Feature toggles via CVars (default enabled)
        const int32 EnableContinental = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperErosion.EnableContinental"))
            ? IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperErosion.EnableContinental"))->GetInt() : 1;
        const int32 EnableOceanic = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperErosion.EnableOceanic"))
            ? IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperErosion.EnableOceanic"))->GetInt() : 1;
        const int32 EnableTrench = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperErosion.EnableTrench"))
            ? IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperErosion.EnableTrench"))->GetInt() : 1;

        const double t0 = FPlatformTime::Seconds();

        // Constants (per My); step duration δt (My)
        const double dt_My = TimeStep_My;
        const double eps_c = ContinentalErosion_m_per_My; // 30 m/My
        const double eps_o = OceanicDampening_m_per_My;   // 40 m/My
        const double eps_t = SedimentAccretion_m_per_My;  // 300 m/My
        const double zc = MaxContinentalAltitude_m;       // 10000 m
        const double zt = TrenchDepth_m;                  // -10000 m

        for (int32 i = 0; i < N; ++i)
        {
            const int32 pid = PlateIdPerVertex.IsValidIndex(i) ? PlateIdPerVertex[i] : INDEX_NONE;
            const uint8 crust = (pid != INDEX_NONE && PlateCrustTypePerPlate.IsValidIndex(pid)) ? PlateCrustTypePerPlate[pid] : 0;

            double z = InOutElevation_m[i];

            // Continental erosion: only when z > 0
            if (EnableContinental && IsContinental(crust) && z > 0.0)
            {
                const double delta = (z / zc) * eps_c * dt_My; // positive amount to subtract
                const double before = z;
                z -= delta;
                InOutElevation_m[i] = z;
                M.ErosionDelta_m += (before - z);
                M.ContinentalVertsChanged++;
            }

            // Oceanic dampening: for oceanic crust (any z). Formula drives toward zt.
            if (EnableOceanic && IsOceanic(crust))
            {
                const double term = 1.0 - (z / zt); // note z/zt is positive when z <= 0 and zt < 0
                const double delta = term * eps_o * dt_My; // positive amount to subtract
                const double before = z;
                z -= delta;
                InOutElevation_m[i] = z;
                M.DampeningDelta_m += (before - z);
                M.OceanicVertsChanged++;
            }

            // Trench accretion: within band near subduction fronts (distance field provided in km)
            if (EnableTrench)
            {
                const double d_km = Boundary.DistanceToSubductionFront_km.IsValidIndex(i) ? Boundary.DistanceToSubductionFront_km[i] : TNumericLimits<double>::Max();
                if (d_km <= TrenchBandKm)
                {
                    const double before = z;
                    z += eps_t * dt_My;
                    InOutElevation_m[i] = z;
                    M.AccretionDelta_m += (z - before);
                    M.TrenchVertsChanged++;
                }
            }
        }

        M.ApplyMs = (FPlatformTime::Seconds() - t0) * 1000.0;
        if (IsPaperProfilingEnabled())
        {
            UE_LOG(LogTemp, Log, TEXT("[Phase6] Erosion: cont=%d (Δ=%.2f m) | oceanic=%d (Δ=%.2f m) | trench=%d (Δ=%.2f m) | %.2f ms"),
                M.ContinentalVertsChanged, M.ErosionDelta_m,
                M.OceanicVertsChanged, M.DampeningDelta_m,
                M.TrenchVertsChanged, M.AccretionDelta_m,
                M.ApplyMs);
        }
        return M;
    }

    FString WritePhase6MetricsJson(
        const FString& BackendName,
        int32 SampleCount,
        int32 Seed,
        const FErosionMetrics& Metrics)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("phase"), TEXT("6-erosion"));
        Root->SetStringField(TEXT("backend"), BackendName);
        Root->SetNumberField(TEXT("sample_count"), SampleCount);
        Root->SetNumberField(TEXT("seed"), Seed);

        // Git commit (short hash) if available
        FString GitHash;
        {
            int32 ExitCode = 1; FString StdOut, StdErr;
            FPlatformProcess::ExecProcess(TEXT("git"), TEXT("rev-parse --short HEAD"), &ExitCode, &StdOut, &StdErr);
            GitHash = (ExitCode == 0) ? StdOut.TrimStartAndEnd() : TEXT("");
        }
        Root->SetStringField(TEXT("git_commit"), GitHash);

        TSharedRef<FJsonObject> MetricsObj = MakeShared<FJsonObject>();
        MetricsObj->SetNumberField(TEXT("continental_changed"), Metrics.ContinentalVertsChanged);
        MetricsObj->SetNumberField(TEXT("oceanic_changed"), Metrics.OceanicVertsChanged);
        MetricsObj->SetNumberField(TEXT("trench_changed"), Metrics.TrenchVertsChanged);
        MetricsObj->SetNumberField(TEXT("erosion_delta_m"), Metrics.ErosionDelta_m);
        MetricsObj->SetNumberField(TEXT("dampening_delta_m"), Metrics.DampeningDelta_m);
        MetricsObj->SetNumberField(TEXT("accretion_delta_m"), Metrics.AccretionDelta_m);
        Root->SetObjectField(TEXT("metrics"), MetricsObj);

        TSharedRef<FJsonObject> Timing = MakeShared<FJsonObject>();
        Timing->SetNumberField(TEXT("apply"), Metrics.ApplyMs);
        Root->SetObjectField(TEXT("timing_ms"), Timing);

        const FString Dir = FPaths::ProjectDir() / TEXT("Docs/Automation/Validation/Phase6");
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
            UE_LOG(LogTemp, Display, TEXT("[Phase6] Metrics JSON written: %s"), *Path);
        }
        return Path;
    }
}

