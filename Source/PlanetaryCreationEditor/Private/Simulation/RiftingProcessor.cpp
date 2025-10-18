#include "Simulation/RiftingProcessor.h"

#include "Simulation/PaperConstants.h"
#include "Simulation/PaperProfiling.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

namespace Rifting
{
    static inline uint32 HashMix(uint32 a)
    {
        // xorshift32
        a ^= a << 13; a ^= a >> 17; a ^= a << 5; return a;
    }

    static inline double Clamp01(double x) { return FMath::Clamp(x, 0.0, 1.0); }

    bool EvaluateRiftingProbability(
        int32 PlateId,
        double PlateArea_km2,
        double ContinentalRatio,
        double LambdaBase,
        double A0_km2,
        FRiftingEvent& OutEvent)
    {
        // λ0 = λ_base * f(cont_ratio) * (A/A0), with f linear and biased to continental
        const double f = FMath::Clamp(ContinentalRatio, 0.25, 1.0);
        const double AreaScale = (A0_km2 > 0.0) ? (PlateArea_km2 / A0_km2) : 0.0;
        const double lambda0 = FMath::Max(0.0, LambdaBase * f * AreaScale);
        const double P = lambda0 * FMath::Exp(-lambda0);

        // Deterministic RNG based on PlateId and a fixed salt
        uint32 seed = HashMix((uint32)(1460023u ^ (uint32)PlateId ^ (uint32)FMath::FloorToInt(PlateArea_km2)));
        FRandomStream Rng((int32)seed);
        const double U = Rng.GetFraction();

        const bool bRift = (U < Clamp01(P));
        if (IsPaperProfilingEnabled())
        {
            UE_LOG(LogTemp, Log, TEXT("[Rifting] Plate %d: Area=%.3g km^2, ContRatio=%.2f, lambda0=%.4f, P=%.4f, U=%.4f -> %s"),
                PlateId, PlateArea_km2, ContinentalRatio, lambda0, P, U, bRift ? TEXT("RIFT") : TEXT("skip"));
        }
        if (!bRift)
        {
            return false;
        }

        // Fill event deterministically
        OutEvent.PlateId = PlateId;
        OutEvent.PlateArea_km2 = PlateArea_km2;
        OutEvent.ContinentalRatio = FMath::Clamp(ContinentalRatio, 0.0, 1.0);
        OutEvent.Seed = (int32)seed;
        // Fragment count in [2,4]
        OutEvent.FragmentCount = 2 + (int32)(Rng.RandRange(0, 2));
        return true;
    }

    static FVector3d AnyTangent(const FVector3d& Unit)
    {
        const FVector3d Up = (FMath::Abs(Unit.Z) < 0.9) ? FVector3d::UnitZ() : FVector3d::UnitX();
        FVector3d T = FVector3d::CrossProduct(Unit, Up);
        const double len = T.Size();
        return (len > 0.0) ? (T / len) : FVector3d::UnitY();
    }

    bool PerformRifting(
        const FRiftingEvent& Event,
        const TArray<FVector3d>& Points,
        const TArray<int32>& CSR_Offsets,
        const TArray<int32>& CSR_Adj,
        const TArray<int32>& PlateIdPerVertexIn,
        TArray<int32>& PlateIdPerVertexOut,
        TArray<FVector3d>& OutFragmentDriftDirections,
        FRiftingMetrics& InOutMetrics,
        TArray<TPair<int32,double>>* OutFragmentPlateRatiosOrNull)
    {
        const double t0 = FPlatformTime::Seconds();
        const int32 N = Points.Num();
        if (Event.PlateId == INDEX_NONE || Event.FragmentCount < 2 || N == 0)
        {
            return false;
        }

        PlateIdPerVertexOut = PlateIdPerVertexIn;

        // Collect vertices belonging to the plate
        TArray<int32> PlateVerts;
        PlateVerts.Reserve(N / 4);
        for (int32 i = 0; i < N; ++i)
        {
            if (PlateIdPerVertexIn.IsValidIndex(i) && PlateIdPerVertexIn[i] == Event.PlateId)
            {
                PlateVerts.Add(i);
            }
        }
        if (PlateVerts.Num() < Event.FragmentCount)
        {
            return false;
        }

        // Seed fragment centroids deterministically from plate vertex set
        FRandomStream Rng(Event.Seed);
        TArray<int32> SeedIdx;
        SeedIdx.SetNum(Event.FragmentCount);
        const int32 Pn = PlateVerts.Num();
        for (int32 k = 0; k < Event.FragmentCount; ++k)
        {
            const int32 pick = (Pn > 0) ? PlateVerts[Rng.RandRange(0, Pn - 1)] : 0;
            SeedIdx[k] = pick;
        }

        // Assign by nearest seed (geodesic)
        TArray<int32> FragIdPerVertex;
        FragIdPerVertex.Init(-1, N);
        for (int32 idx : PlateVerts)
        {
            const FVector3d& P = Points[idx];
            int32 best = 0; double bestAngle = TNumericLimits<double>::Max();
            for (int32 k = 0; k < Event.FragmentCount; ++k)
            {
                const FVector3d& S = Points[SeedIdx[k]];
                const double dot = FMath::Clamp(P.Dot(S), -1.0, 1.0);
                const double ang = FMath::Acos(dot);
                if (ang < bestAngle) { bestAngle = ang; best = k; }
            }
            FragIdPerVertex[idx] = best;
        }

        // Assign new plate ids for fragments [1..n-1]; keep fragment 0 as original plate id
        int32 maxPlateId = -1;
        for (int32 v = 0; v < N; ++v)
        {
            maxPlateId = FMath::Max(maxPlateId, PlateIdPerVertexIn.IsValidIndex(v) ? PlateIdPerVertexIn[v] : -1);
        }
        TArray<int32> NewPlateIdForFrag;
        NewPlateIdForFrag.SetNum(Event.FragmentCount);
        NewPlateIdForFrag[0] = Event.PlateId;
        for (int32 k = 1; k < Event.FragmentCount; ++k)
        {
            NewPlateIdForFrag[k] = ++maxPlateId;
        }

        for (int32 idx : PlateVerts)
        {
            const int32 f = FragIdPerVertex[idx];
            if (f >= 0)
            {
                PlateIdPerVertexOut[idx] = NewPlateIdForFrag[f];
            }
        }

        // Propagate continental ratio to fragments via mapping (service will apply to metadata)
        if (OutFragmentPlateRatiosOrNull)
        {
            OutFragmentPlateRatiosOrNull->Reset();
            OutFragmentPlateRatiosOrNull->Reserve(Event.FragmentCount);
            for (int32 k = 0; k < Event.FragmentCount; ++k)
            {
                OutFragmentPlateRatiosOrNull->Add(TPair<int32,double>(NewPlateIdForFrag[k], Event.ContinentalRatio));
            }
        }

        // Compute simple drift directions per fragment (unit tangent at fragment centroid)
        OutFragmentDriftDirections.SetNum(Event.FragmentCount);
        for (int32 k = 0; k < Event.FragmentCount; ++k)
        {
            FVector3d Sum = FVector3d::ZeroVector; int32 Count = 0;
            for (int32 idx : PlateVerts) { if (FragIdPerVertex[idx] == k) { Sum += Points[idx]; ++Count; } }
            FVector3d Centroid = (Count > 0) ? (Sum / (double)Count).GetSafeNormal() : Points[SeedIdx[k]].GetSafeNormal();
            FVector3d T = AnyTangent(Centroid);
            // Small deterministic rotation from RNG for variety
            const double ang = (Rng.GetFraction() * 2.0 - 1.0) * 0.25 * PI; // ±45°
            FVector3d B = FVector3d::CrossProduct(Centroid, T).GetSafeNormal();
            FVector3d Dir = (T * FMath::Cos(ang) + B * FMath::Sin(ang)).GetSafeNormal();
            OutFragmentDriftDirections[k] = Dir;
        }

        // Metrics
        InOutMetrics.RiftingCount += 1;
        InOutMetrics.MeanFragments = ((InOutMetrics.MeanFragments * (InOutMetrics.RiftingCount - 1)) + Event.FragmentCount) / (double)InOutMetrics.RiftingCount;
        InOutMetrics.ApplyMs += (FPlatformTime::Seconds() - t0) * 1000.0;
        return true;
    }

    FString WritePhase4MetricsJsonAppendRifting(
        const FString& ExistingPhase4JsonPath,
        const FRiftingMetrics& Metrics)
    {
        // Load or create base JSON
        FString Path = ExistingPhase4JsonPath;
        TSharedPtr<FJsonObject> Root;
        if (!Path.IsEmpty() && FPlatformFileManager::Get().GetPlatformFile().FileExists(*Path))
        {
            FString Content; FFileHelper::LoadFileToString(Content, *Path);
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
            FJsonSerializer::Deserialize(Reader, Root);
        }
        if (!Root.IsValid())
        {
            Root = MakeShared<FJsonObject>();
            Root->SetStringField(TEXT("phase"), TEXT("4-collision"));
            Root->SetStringField(TEXT("backend"), TEXT(""));
            Root->SetNumberField(TEXT("sample_count"), 0);
            Root->SetNumberField(TEXT("seed"), 0);
            Root->SetStringField(TEXT("git_commit"), TEXT(""));
            Root->SetObjectField(TEXT("metrics"), MakeShared<FJsonObject>());
            Root->SetObjectField(TEXT("timing_ms"), MakeShared<FJsonObject>());
            const FString Dir = FPaths::ProjectDir() / TEXT("Docs/Automation/Validation/Phase4");
            IFileManager::Get().MakeDirectory(*Dir, true);
            const FDateTime Now = FDateTime::UtcNow();
            const FString Timestamp = FString::Printf(TEXT("%04d%02d%02d_%02d%02d%02d"), Now.GetYear(), Now.GetMonth(), Now.GetDay(), Now.GetHour(), Now.GetMinute(), Now.GetSecond());
            Path = Dir / FString::Printf(TEXT("summary_%s.json"), *Timestamp);
        }

        // Update metrics block
        TSharedPtr<FJsonObject> MetricsObj = Root->GetObjectField(TEXT("metrics"));
        if (!MetricsObj.IsValid()) { MetricsObj = MakeShared<FJsonObject>(); Root->SetObjectField(TEXT("metrics"), MetricsObj.ToSharedRef()); }
        MetricsObj->SetNumberField(TEXT("rifting_count"), Metrics.RiftingCount);
        MetricsObj->SetNumberField(TEXT("fragments_per_rift"), Metrics.MeanFragments);

        // Update timing block
        TSharedPtr<FJsonObject> TimingObj = Root->GetObjectField(TEXT("timing_ms"));
        if (!TimingObj.IsValid()) { TimingObj = MakeShared<FJsonObject>(); Root->SetObjectField(TEXT("timing_ms"), TimingObj.ToSharedRef()); }
        TimingObj->SetNumberField(TEXT("rift"), Metrics.ApplyMs);
        // Best effort total accumulation
        const double prevTotal = TimingObj->HasTypedField<EJson::Number>(TEXT("total")) ? TimingObj->GetNumberField(TEXT("total")) : 0.0;
        TimingObj->SetNumberField(TEXT("total"), prevTotal + Metrics.ApplyMs);

        FString Output;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
        FFileHelper::SaveStringToFile(Output, *Path);

        if (IsPaperProfilingEnabled())
        {
            UE_LOG(LogTemp, Display, TEXT("[Phase4] Rifting metrics appended: %s"), *Path);
        }
        return Path;
    }
}
