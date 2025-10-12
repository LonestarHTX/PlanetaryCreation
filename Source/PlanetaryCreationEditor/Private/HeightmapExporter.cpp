// Milestone 6: Heightmap Visualization Exporter
// Generates color-coded PNG using hypsometric or normalized palette selections

#include "PlanetaryCreationLogging.h"
#include "TectonicSimulationService.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Async/ParallelFor.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "RHI.h"

#include "HeightmapColorPalette.h"
#include "HeightmapSampling.h"
#include "Misc/ScopeExit.h"

#include <limits>

namespace
{
    constexpr double HeightmapSamplingBudgetMs = 200.0;
    constexpr double HeightmapExportTotalBudgetMs = 350.0;

    constexpr uint64 HeightmapPNGExtraBytes = 8ull * 1024ull * 1024ull;         // 8 MiB cushion for encoder scratch
    constexpr uint64 HeightmapPreflightSafetyHeadroomBytes = 512ull * 1024ull * 1024ull; // 512 MiB safety margin

    constexpr double BytesToMiB(uint64 Bytes)
    {
        return static_cast<double>(Bytes) / (1024.0 * 1024.0);
    }

    struct FHeightmapPreflightInfo
    {
        uint64 PixelBytes = 0;
        uint64 SamplerBytes = 0;
        uint64 ScratchBytes = HeightmapPNGExtraBytes;
        uint64 SafetyBytes = HeightmapPreflightSafetyHeadroomBytes;
        uint64 RequiredBytes = 0;
        uint64 AvailablePhysicalBytes = 0;
        bool bPass = false;
        FString Details;
    };

    static FHeightmapPreflightInfo PreflightHeightmapExport(int32 Width, int32 Height, const FHeightmapSampler::FMemoryStats& SamplerStats)
    {
        FHeightmapPreflightInfo Info;

        const uint64 PixelCount = static_cast<uint64>(Width) * static_cast<uint64>(Height);
        Info.PixelBytes = PixelCount * 4ull;

        auto ToPositive = [](int64 Value) -> uint64
        {
            return Value > 0 ? static_cast<uint64>(Value) : 0ull;
        };

        Info.SamplerBytes =
            ToPositive(SamplerStats.TriangleDataBytes) +
            ToPositive(SamplerStats.TriangleDirectionsBytes) +
            ToPositive(SamplerStats.TriangleIdsBytes) +
            ToPositive(SamplerStats.KDTreeBytes) +
            ToPositive(SamplerStats.SnapshotFloatBytes);

        const FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
        Info.AvailablePhysicalBytes = MemoryStats.AvailablePhysical;
        Info.RequiredBytes = Info.PixelBytes + Info.SamplerBytes + Info.ScratchBytes + Info.SafetyBytes;
        Info.bPass = Info.RequiredBytes <= Info.AvailablePhysicalBytes;

        Info.Details = FString::Printf(
            TEXT("Need≈%.1f MiB (Pixels %.1f + Sampler %.1f + Scratch %.1f + Safety %.1f) Free≈%.1f MiB"),
            BytesToMiB(Info.RequiredBytes),
            BytesToMiB(Info.PixelBytes),
            BytesToMiB(Info.SamplerBytes),
            BytesToMiB(Info.ScratchBytes),
            BytesToMiB(Info.SafetyBytes),
            BytesToMiB(Info.AvailablePhysicalBytes));

        return Info;
    }
}

/**
 * Export heightmap as color-coded PNG with elevation gradient
 * Returns path to exported file, or empty string on failure
 */
FString UTectonicSimulationService::ExportHeightmapVisualization(int32 ImageWidth, int32 ImageHeight)
{
    LastHeightmapExportMetrics = FHeightmapExportMetrics();

    const double ExportStartSeconds = FPlatformTime::Seconds();
    double SamplerSetupMs = 0.0;
    double SamplingMs = 0.0;
    double EncodeMs = 0.0;

#if !UE_BUILD_SHIPPING
    auto ToMegabytesSigned = [](int64 Bytes) -> double
    {
        return static_cast<double>(Bytes) / (1024.0 * 1024.0);
    };
    auto ToMegabytesUnsigned = [](uint64 Bytes) -> double
    {
        return static_cast<double>(Bytes) / (1024.0 * 1024.0);
    };

    auto LogBufferTelemetry = [&](const TCHAR* Label, int64 ElementCount, int64 ElementSizeBytes, int64 AllocatedBytes, const TCHAR* AllocatorLabel)
    {
        UE_LOG(LogPlanetaryCreation, Log,
            TEXT("[HeightmapExport][Buffer] %s Elements=%lld ElementSize=%lldB Allocated=%.2f MB Allocator=%s"),
            Label,
            ElementCount,
            ElementSizeBytes,
            ToMegabytesSigned(AllocatedBytes),
            AllocatorLabel);
    };

    auto LogMemoryCheckpoint = [&](const TCHAR* Label, const FPlatformMemoryStats* PreviousStats) -> FPlatformMemoryStats
    {
        const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();

        const double UsedPhysicalMB = ToMegabytesUnsigned(Stats.UsedPhysical);
        const double PeakPhysicalMB = ToMegabytesUnsigned(Stats.PeakUsedPhysical);
        const double UsedVirtualMB = ToMegabytesUnsigned(Stats.UsedVirtual);
        const double PeakVirtualMB = ToMegabytesUnsigned(Stats.PeakUsedVirtual);
        const double FreePhysicalMB = ToMegabytesUnsigned(Stats.AvailablePhysical);
        const double FreeVirtualMB = ToMegabytesUnsigned(Stats.AvailableVirtual);

        double DeltaPhysicalMB = 0.0;
        double DeltaVirtualMB = 0.0;
        if (PreviousStats)
        {
            DeltaPhysicalMB = ToMegabytesSigned(static_cast<int64>(Stats.UsedPhysical) - static_cast<int64>(PreviousStats->UsedPhysical));
            DeltaVirtualMB = ToMegabytesSigned(static_cast<int64>(Stats.UsedVirtual) - static_cast<int64>(PreviousStats->UsedVirtual));
        }

        UE_LOG(LogPlanetaryCreation, Log,
            TEXT("[HeightmapExport][Memory] %s UsedPhys=%.2f MB (Δ%.2f) PeakPhys=%.2f MB UsedVirt=%.2f MB (Δ%.2f) PeakVirt=%.2f MB FreePhys=%.2f MB FreeVirt=%.2f MB"),
            Label,
            UsedPhysicalMB, DeltaPhysicalMB,
            PeakPhysicalMB,
            UsedVirtualMB, DeltaVirtualMB,
            PeakVirtualMB,
            FreePhysicalMB, FreeVirtualMB);

        return Stats;
    };

    const FPlatformMemoryStats MemoryStatsPreExport = LogMemoryCheckpoint(TEXT("PreExport"), nullptr);
    FPlatformMemoryStats PreviousMemoryStats = MemoryStatsPreExport;
    int64 TotalTrackedBufferBytes = 0;
#endif // !UE_BUILD_SHIPPING

    if (ImageWidth <= 0 || ImageHeight <= 0)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Cannot export heightmap: Invalid dimensions %dx%d"), ImageWidth, ImageHeight);
        return FString();
    }

    const bool bNullRHIActive = (GDynamicRHI == nullptr) || (FCString::Stristr(GDynamicRHI->GetName(), TEXT("Null")) != nullptr);
    const int64 PixelCount64 = static_cast<int64>(ImageWidth) * static_cast<int64>(ImageHeight);
    constexpr int32 SafeBaselineWidth = 512;
    constexpr int32 SafeBaselineHeight = 256;
    constexpr int64 SafeBaselinePixels = static_cast<int64>(SafeBaselineWidth) * SafeBaselineHeight;

    const bool bUnsafeOverride = bAllowUnsafeHeightmapExport;
    ON_SCOPE_EXIT
    {
        bAllowUnsafeHeightmapExport = false;
    };

    if (!bUnsafeOverride)
    {
        if (ImageWidth > SafeBaselineWidth || ImageHeight > SafeBaselineHeight || PixelCount64 > SafeBaselinePixels)
        {
            UE_LOG(LogPlanetaryCreation, Error,
                TEXT("[HeightmapExport][Safety] Dimensions %dx%d exceed safe baseline %dx%d (use --force-large-export on dedicated hardware)."),
                ImageWidth, ImageHeight, SafeBaselineWidth, SafeBaselineHeight);
            return FString();
        }
    }
    else if (bNullRHIActive && PixelCount64 > SafeBaselinePixels)
    {
        UE_LOG(LogPlanetaryCreation, Error,
            TEXT("[HeightmapExport][Safety] NullRHI export %dx%d exceeds safe pixel budget %lld; run with a real RHI or reduce dimensions."),
            ImageWidth, ImageHeight, SafeBaselinePixels);
        return FString();
    }

    if (RenderVertices.Num() == 0)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Cannot export heightmap: No render data available"));
        return FString();
    }

#if WITH_AUTOMATION_TESTS
    if (bForceHeightmapModuleFailure)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Cannot export heightmap: Image wrapper module forced offline (test override)"));
        return FString();
    }
#endif

#if !UE_BUILD_SHIPPING
    if (GDynamicRHI != nullptr)
    {
        const TCHAR* const RHIName = GDynamicRHI->GetName();
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[HeightmapExport][RHI] %s"), RHIName);
        ensureMsgf(FCString::Stristr(RHIName, TEXT("Null")) != nullptr, TEXT("Heightmap export expected NullRHI; got %s"), RHIName);
    }
#endif

	// Decide which elevation source to use
	// Prefer amplified (Stage B with transform faults), fallback to base elevation (erosion system)
    const bool bStageBReady = IsStageBAmplificationReady();
    const EStageBAmplificationReadyReason ReadyReason = GetStageBAmplificationNotReadyReason();
    if (!bStageBReady)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("[StageB][Ready] Heightmap export using baseline elevations because Stage B is not ready (%s: %s)."),
            PlanetaryCreation::StageB::GetReadyReasonLabel(ReadyReason),
            PlanetaryCreation::StageB::GetReadyReasonDescription(ReadyReason));
    }

    const bool bAmplifiedArrayValid = (VertexAmplifiedElevation.Num() == RenderVertices.Num());
    const bool bAmplifiedAvailable = bStageBReady && bAmplifiedArrayValid;
    if (bStageBReady && !bAmplifiedArrayValid)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("Stage B amplification reports ready but amplified elevation array is invalid (vertices=%d, amplified=%d)."),
            RenderVertices.Num(), VertexAmplifiedElevation.Num());
    }

	const TArray<double>& ElevationSource = bAmplifiedAvailable ? VertexAmplifiedElevation : VertexElevationValues;

	if (ElevationSource.Num() != RenderVertices.Num())
	{
		UE_LOG(LogPlanetaryCreation, Error, TEXT("Cannot export heightmap: Elevation data mismatch (vertices=%d, elevations=%d)"),
			RenderVertices.Num(), ElevationSource.Num());
		return FString();
	}

    const double SamplerSetupStartSeconds = FPlatformTime::Seconds();
    FHeightmapSampler Sampler(*this);
    SamplerSetupMs = (FPlatformTime::Seconds() - SamplerSetupStartSeconds) * 1000.0;
    if (!Sampler.IsValid())
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Cannot export heightmap: Sampler initialization failed (vertices=%d, triangles=%d)"),
            RenderVertices.Num(), RenderTriangles.Num() / 3);
        return FString();
    }

    const FHeightmapSampler::FMemoryStats SamplerMemoryStats = Sampler.GetMemoryStats();
#if !UE_BUILD_SHIPPING
    {
        UE_LOG(LogPlanetaryCreation, Log,
            TEXT("[HeightmapExport][SamplerMemory] Vertices=%d Triangles=%d UsingAmplified=%s SnapshotFloat=%s TriangleData=%.2f MB[FDefaultAllocator] TriangleDirections=%.2f MB[FDefaultAllocator] TriangleIds=%.2f MB[FDefaultAllocator] KDTreeNodes=%d (%.2f MB[TUniquePtr]) SnapshotFloat=%.2f MB[FDefaultAllocator]"),
            SamplerMemoryStats.VertexCount,
            SamplerMemoryStats.TriangleCount,
            SamplerMemoryStats.bUsingAmplified ? TEXT("true") : TEXT("false"),
            SamplerMemoryStats.bHasSnapshotFloatBuffer ? TEXT("true") : TEXT("false"),
            ToMegabytesSigned(SamplerMemoryStats.TriangleDataBytes),
            ToMegabytesSigned(SamplerMemoryStats.TriangleDirectionsBytes),
            ToMegabytesSigned(SamplerMemoryStats.TriangleIdsBytes),
            SamplerMemoryStats.KDTreeNodeCount,
            ToMegabytesSigned(SamplerMemoryStats.KDTreeBytes),
            ToMegabytesSigned(SamplerMemoryStats.SnapshotFloatBytes));
    }
#endif // !UE_BUILD_SHIPPING

    const FHeightmapPreflightInfo PreflightInfo = PreflightHeightmapExport(ImageWidth, ImageHeight, SamplerMemoryStats);
    if (!PreflightInfo.bPass)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("[HeightmapExport][Preflight][Abort] %s"), *PreflightInfo.Details);
        return FString();
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[HeightmapExport][Preflight] %s"), *PreflightInfo.Details);

    const bool bSamplerUsingAmplified = Sampler.UsesAmplifiedElevation();
    if (bSamplerUsingAmplified != bAmplifiedAvailable)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("Heightmap sampler Stage B usage mismatch (SamplerAmplified=%s, StageBReady=%s, AmplifiedArrayValid=%s)."),
            bSamplerUsingAmplified ? TEXT("true") : TEXT("false"),
            bStageBReady ? TEXT("true") : TEXT("false"),
            bAmplifiedArrayValid ? TEXT("true") : TEXT("false"));
    }

	// Find min/max elevation
	double MinElevation = std::numeric_limits<double>::max();
	double MaxElevation = std::numeric_limits<double>::lowest();

	for (const double Elevation : ElevationSource)
	{
		MinElevation = FMath::Min(MinElevation, Elevation);
		MaxElevation = FMath::Max(MaxElevation, Elevation);
	}

    const EHeightmapPaletteMode RequestedPaletteMode = GetHeightmapPaletteMode();
    const PlanetaryCreation::Heightmap::FHeightmapPalette Palette =
        PlanetaryCreation::Heightmap::FHeightmapPalette::FromMode(RequestedPaletteMode, MinElevation, MaxElevation);

    if (Palette.IsNormalizedRequested() && !Palette.UsesNormalizedSampling())
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("Heightmap export requested normalized palette but elevation range is degenerate (%.6f). Falling back to hypsometric colors."),
            Palette.GetRange());
    }

    UE_LOG(LogPlanetaryCreation, Verbose, TEXT("Heightmap export using %s elevation with %s palette, range: %.1f m to %.1f m"),
        bSamplerUsingAmplified ? TEXT("amplified") : TEXT("baseline"),
        Palette.UsesNormalizedSampling() ? TEXT("normalized") : TEXT("hypsometric"),
        MinElevation, MaxElevation);

    FHeightmapExportMetrics Metrics;
    Metrics.Width = ImageWidth;
    Metrics.Height = ImageHeight;
    Metrics.bSamplerUsedAmplified = bSamplerUsingAmplified;
    Metrics.bStageBReadyAtExport = bStageBReady;
    Metrics.MinElevation = MinElevation;
    Metrics.MaxElevation = MaxElevation;

    const int32 PixelCount = ImageWidth * ImageHeight;

    struct FRowSeam
    {
        float LeftElevation = 0.0f;
        float RightElevation = 0.0f;
        uint8 bLeftHit = 0;
        uint8 bRightHit = 0;
    };

    TArray<uint8> RawData;
    RawData.SetNumUninitialized(static_cast<int64>(PixelCount) * 4);

    TArray<FRowSeam> RowSeams;
    RowSeams.SetNumZeroed(ImageHeight);

    TArray<uint32> RowSuccessCounts;
    RowSuccessCounts.SetNumZeroed(ImageHeight);

    TArray<uint64> RowTraversalSums;
    RowTraversalSums.SetNumZeroed(ImageHeight);

    TArray<uint8> RowMaxTraversalSteps;
    RowMaxTraversalSteps.SetNumZeroed(ImageHeight);

#if !UE_BUILD_SHIPPING
    {
        const TCHAR* const DefaultAllocatorLabel = TEXT("FDefaultAllocator");

        const int64 RawDataBytes = static_cast<int64>(RawData.GetAllocatedSize());
        LogBufferTelemetry(TEXT("RawData<uint8>"), static_cast<int64>(RawData.Num()), static_cast<int64>(sizeof(uint8)), RawDataBytes, DefaultAllocatorLabel);
        TotalTrackedBufferBytes += RawDataBytes;

        const int64 RowSeamsBytes = static_cast<int64>(RowSeams.GetAllocatedSize());
        LogBufferTelemetry(TEXT("RowSeams<FRowSeam>"), static_cast<int64>(RowSeams.Num()), static_cast<int64>(sizeof(FRowSeam)), RowSeamsBytes, DefaultAllocatorLabel);
        TotalTrackedBufferBytes += RowSeamsBytes;

        const int64 RowSuccessBytes = static_cast<int64>(RowSuccessCounts.GetAllocatedSize());
        LogBufferTelemetry(TEXT("RowSuccess<uint32>"), static_cast<int64>(RowSuccessCounts.Num()), static_cast<int64>(sizeof(uint32)), RowSuccessBytes, DefaultAllocatorLabel);
        TotalTrackedBufferBytes += RowSuccessBytes;

        const int64 RowTraversalBytes = static_cast<int64>(RowTraversalSums.GetAllocatedSize());
        LogBufferTelemetry(TEXT("RowTraversal<uint64>"), static_cast<int64>(RowTraversalSums.Num()), static_cast<int64>(sizeof(uint64)), RowTraversalBytes, DefaultAllocatorLabel);
        TotalTrackedBufferBytes += RowTraversalBytes;

        const int64 RowMaxStepsBytes = static_cast<int64>(RowMaxTraversalSteps.GetAllocatedSize());
        LogBufferTelemetry(TEXT("RowMaxSteps<uint8>"), static_cast<int64>(RowMaxTraversalSteps.Num()), static_cast<int64>(sizeof(uint8)), RowMaxStepsBytes, DefaultAllocatorLabel);
        TotalTrackedBufferBytes += RowMaxStepsBytes;

        const FPlatformMemoryStats PixelBufferStats = LogMemoryCheckpoint(TEXT("AfterPixelBufferAlloc"), &PreviousMemoryStats);
        PreviousMemoryStats = PixelBufferStats;
    }
#endif // !UE_BUILD_SHIPPING

    const double InvWidth = 1.0 / static_cast<double>(ImageWidth);
    const double InvHeight = 1.0 / static_cast<double>(ImageHeight);

    const double SamplingStartSeconds = FPlatformTime::Seconds();

    {
        TRACE_CPUPROFILER_EVENT_SCOPE(HeightmapSampling);

        ParallelFor(ImageHeight, [&Sampler, &RawData, &RowSeams, &RowSuccessCounts, &RowTraversalSums, &RowMaxTraversalSteps, ImageWidth, InvWidth, InvHeight, Palette](int32 Y)
        {
            const double V = (static_cast<double>(Y) + 0.5) * InvHeight;
            const int32 RowBase = Y * ImageWidth;

            FRowSeam LocalSeam;
            uint32 LocalSuccessCount = 0;
            uint64 LocalStepSum = 0;
            uint8 LocalMaxSteps = 0;

            for (int32 X = 0; X < ImageWidth; ++X)
            {
                const double U = (static_cast<double>(X) + 0.5) * InvWidth;
                const FVector2d UV(U, V);
                FHeightmapSampler::FSampleInfo SampleInfo;
                const double Elevation = Sampler.SampleElevationAtUV(UV, &SampleInfo);
                const uint8 ClampedSteps = static_cast<uint8>(FMath::Clamp(SampleInfo.Steps, 0, 255));

                LocalMaxSteps = FMath::Max<uint8>(LocalMaxSteps, ClampedSteps);
                LocalStepSum += ClampedSteps;

                if (SampleInfo.bHit)
                {
                    ++LocalSuccessCount;
                }

                if (X == 0)
                {
                    LocalSeam.LeftElevation = static_cast<float>(Elevation);
                    LocalSeam.bLeftHit = SampleInfo.bHit ? 1 : 0;
                }

                if (X == ImageWidth - 1)
                {
                    LocalSeam.RightElevation = static_cast<float>(Elevation);
                    LocalSeam.bRightHit = SampleInfo.bHit ? 1 : 0;
                }

                const FColor PixelColor = Palette.Sample(Elevation);
                const int32 RawIndex = (RowBase + X) * 4;

                RawData[RawIndex + 0] = PixelColor.R;
                RawData[RawIndex + 1] = PixelColor.G;
                RawData[RawIndex + 2] = PixelColor.B;
                RawData[RawIndex + 3] = 255;
            }

            RowSeams[Y] = LocalSeam;
            RowSuccessCounts[Y] = LocalSuccessCount;
            RowTraversalSums[Y] = LocalStepSum;
            RowMaxTraversalSteps[Y] = LocalMaxSteps;
        }, EParallelForFlags::None);
    }

#if !UE_BUILD_SHIPPING
    const FPlatformMemoryStats PostSamplingStats = LogMemoryCheckpoint(TEXT("PostSampling"), &PreviousMemoryStats);
    PreviousMemoryStats = PostSamplingStats;
#endif

    uint64 SuccessfulSamples = 0;
    uint64 TraversalStepSum = 0;
    uint8 MaxTraversalSteps = 0;

    for (int32 Y = 0; Y < ImageHeight; ++Y)
    {
        SuccessfulSamples += RowSuccessCounts[Y];
        TraversalStepSum += RowTraversalSums[Y];
        MaxTraversalSteps = FMath::Max<uint8>(MaxTraversalSteps, RowMaxTraversalSteps[Y]);
    }

    const int64 PixelCount64 = static_cast<int64>(PixelCount);
    const int64 SuccessfulSamples64 = static_cast<int64>(SuccessfulSamples);
    const int64 FailedSamples = PixelCount64 - SuccessfulSamples64;
    const double CoveragePercent = PixelCount64 > 0
        ? (static_cast<double>(SuccessfulSamples64) / static_cast<double>(PixelCount64)) * 100.0
        : 0.0;
    const double AverageTraversalSteps = PixelCount64 > 0
        ? static_cast<double>(TraversalStepSum) / static_cast<double>(PixelCount64)
        : 0.0;

    Metrics.PixelCount = PixelCount64;
    Metrics.SuccessfulSamples = SuccessfulSamples64;
    Metrics.FailedSamples = FailedSamples;
    Metrics.CoveragePercent = CoveragePercent;
    Metrics.AverageTraversalSteps = AverageTraversalSteps;
    Metrics.MaxTraversalSteps = static_cast<int32>(MaxTraversalSteps);

    double SeamAbsAccum = 0.0;
    double SeamRmsAccum = 0.0;
    double SeamMaxAbsDelta = 0.0;
    int32 SeamSamples = 0;
    int32 RowsAboveHalfMeter = 0;
    int32 SeamRowsWithFailures = 0;

    if (ImageWidth >= 2)
    {
        for (int32 Y = 0; Y < ImageHeight; ++Y)
        {
            const FRowSeam& Seam = RowSeams[Y];
            if (!Seam.bLeftHit || !Seam.bRightHit)
            {
                ++SeamRowsWithFailures;
                continue;
            }

            const double LeftElevation = static_cast<double>(Seam.LeftElevation);
            const double RightElevation = static_cast<double>(Seam.RightElevation);
            const double Delta = LeftElevation - RightElevation;
            const double AbsDelta = FMath::Abs(Delta);

            SeamAbsAccum += AbsDelta;
            SeamRmsAccum += Delta * Delta;
            SeamMaxAbsDelta = FMath::Max(SeamMaxAbsDelta, AbsDelta);
            ++SeamSamples;

            if (AbsDelta > 0.5)
            {
                ++RowsAboveHalfMeter;
            }
        }
    }

    const double MeanSeamAbsDelta = SeamSamples > 0 ? SeamAbsAccum / static_cast<double>(SeamSamples) : 0.0;
    const double RmsSeamDelta = SeamSamples > 0 ? FMath::Sqrt(SeamRmsAccum / static_cast<double>(SeamSamples)) : 0.0;

    SamplingMs = (FPlatformTime::Seconds() - SamplingStartSeconds) * 1000.0;

    Metrics.SeamRowsEvaluated = SeamSamples;
    Metrics.SeamRowsAboveHalfMeter = RowsAboveHalfMeter;
    Metrics.SeamRowsWithFailures = SeamRowsWithFailures;
    Metrics.SeamMeanAbsDelta = MeanSeamAbsDelta;
    Metrics.SeamRmsDelta = RmsSeamDelta;
    Metrics.SeamMaxAbsDelta = SeamMaxAbsDelta;

    UE_LOG(LogPlanetaryCreation, Log,
        TEXT("[HeightmapExport][Coverage] Pixels=%lld Success=%lld (%.3f%%) Failures=%lld AvgSteps=%.2f MaxSteps=%d StageBReady=%s UsingAmplified=%s SnapshotFloat=%s SampleMs=%.2f"),
        Metrics.PixelCount, Metrics.SuccessfulSamples, Metrics.CoveragePercent, Metrics.FailedSamples, Metrics.AverageTraversalSteps, Metrics.MaxTraversalSteps,
        Metrics.bStageBReadyAtExport ? TEXT("true") : TEXT("false"),
        Metrics.bSamplerUsedAmplified ? TEXT("true") : TEXT("false"),
        Sampler.UsesSnapshotFloatBuffer() ? TEXT("true") : TEXT("false"),
        SamplingMs);

    if (Metrics.SeamRowsEvaluated > 0)
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[HeightmapExport][SeamDelta] Rows=%d Mean=%.3f m RMS=%.3f m Max=%.3f m RowsAbove0.5m=%d RowFailures=%d"),
            Metrics.SeamRowsEvaluated, Metrics.SeamMeanAbsDelta, Metrics.SeamRmsDelta, Metrics.SeamMaxAbsDelta,
            Metrics.SeamRowsAboveHalfMeter, Metrics.SeamRowsWithFailures);
    }
    else if (Metrics.SeamRowsWithFailures > 0)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[HeightmapExport][SeamDelta] No valid seam samples recorded; rows with failures=%d"),
            Metrics.SeamRowsWithFailures);
    }

	// Encode as PNG

    const double EncodeStartSeconds = FPlatformTime::Seconds();

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

    if (!ImageWrapper.IsValid())
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to create PNG image wrapper"));
        return FString();
    }

    const TArray64<uint8>* CompressedDataPtr = nullptr;
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(HeightmapPNGEncode);

        if (!ImageWrapper->SetRaw(RawData.GetData(), RawData.Num(), ImageWidth, ImageHeight, ERGBFormat::RGBA, 8))
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to set raw image data for PNG encoding"));
            return FString();
        }

        const TArray64<uint8>& CompressedDataRef = ImageWrapper->GetCompressed();
        CompressedDataPtr = &CompressedDataRef;
        if (CompressedDataRef.Num() == 0)
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to compress PNG image"));
            return FString();
        }
#if !UE_BUILD_SHIPPING
        {
            const int64 CompressedBytes = static_cast<int64>(CompressedDataRef.GetAllocatedSize());
            LogBufferTelemetry(TEXT("CompressedPNG<uint8>"), static_cast<int64>(CompressedDataRef.Num()), static_cast<int64>(sizeof(uint8)), CompressedBytes, TEXT("FDefaultAllocator64"));
            TotalTrackedBufferBytes += CompressedBytes;

            const FPlatformMemoryStats CompressionStats = LogMemoryCheckpoint(TEXT("AfterPNGCompression"), &PreviousMemoryStats);
            PreviousMemoryStats = CompressionStats;
        }
#endif
    }

    check(CompressedDataPtr != nullptr);
    const TArray64<uint8>& CompressedData = *CompressedDataPtr;

    FString OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PlanetaryCreation/Heightmaps"));
#if WITH_AUTOMATION_TESTS
    if (!HeightmapExportOverrideDirectory.IsEmpty())
    {
        OutputDirectory = HeightmapExportOverrideDirectory;
    }
#endif

    if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to create output directory: %s"), *OutputDirectory);
        return FString();
    }

    const FString OutputPath = FPaths::Combine(OutputDirectory, TEXT("Heightmap_Visualization.png"));

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (PlatformFile.FileExists(*OutputPath))
    {
        if (PlatformFile.IsReadOnly(*OutputPath))
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to overwrite heightmap: %s is read-only"), *OutputPath);
            return FString();
        }

        if (!PlatformFile.DeleteFile(*OutputPath))
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to delete existing heightmap: %s"), *OutputPath);
            return FString();
        }
    }

    if (bForceHeightmapWriteFailure)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Forced heightmap write failure (test override)"));
        return FString();
    }

    if (!FFileHelper::SaveArrayToFile(CompressedData, *OutputPath))
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to write PNG to: %s"), *OutputPath);
        return FString();
    }
#if !UE_BUILD_SHIPPING
    {
        const FPlatformMemoryStats PostWriteStats = LogMemoryCheckpoint(TEXT("AfterFileWrite"), &PreviousMemoryStats);
        PreviousMemoryStats = PostWriteStats;
    }
#endif

    EncodeMs = (FPlatformTime::Seconds() - EncodeStartSeconds) * 1000.0;
    const double TotalMs = (FPlatformTime::Seconds() - ExportStartSeconds) * 1000.0;

    Metrics.SamplerSetupMs = SamplerSetupMs;
    Metrics.SamplingMs = SamplingMs;
    Metrics.EncodeMs = EncodeMs;
    Metrics.TotalMs = TotalMs;
    Metrics.bUsedSnapshotFloatBuffer = Sampler.UsesSnapshotFloatBuffer();

    const bool bSamplingExceeded = SamplingMs > HeightmapSamplingBudgetMs;
    const bool bTotalExceeded = TotalMs > HeightmapExportTotalBudgetMs;
    Metrics.bPerformanceBudgetExceeded = bSamplingExceeded || bTotalExceeded;

    Metrics.bValid = true;

    if (Metrics.bPerformanceBudgetExceeded)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("[HeightmapExport][PerformanceBudgetExceeded] Sample=%.2f ms (Budget=%.2f) Total=%.2f ms (Budget=%.2f) Size=%dx%d StageB=%s SnapshotFloat=%s"),
            SamplingMs, HeightmapSamplingBudgetMs,
            TotalMs, HeightmapExportTotalBudgetMs,
            ImageWidth, ImageHeight,
            Metrics.bSamplerUsedAmplified ? TEXT("true") : TEXT("false"),
            Metrics.bUsedSnapshotFloatBuffer ? TEXT("true") : TEXT("false"));
    }

    // Update performance history (ring buffer).
    FHeightmapExportPerformanceSample PerformanceSample;
    PerformanceSample.SamplerSetupMs = SamplerSetupMs;
    PerformanceSample.SamplingMs = SamplingMs;
    PerformanceSample.EncodeMs = EncodeMs;
    PerformanceSample.TotalMs = TotalMs;
    PerformanceSample.Width = ImageWidth;
    PerformanceSample.Height = ImageHeight;
    PerformanceSample.bUsedAmplified = Metrics.bSamplerUsedAmplified;
    PerformanceSample.bUsedSnapshotFloatBuffer = Metrics.bUsedSnapshotFloatBuffer;
    PerformanceSample.bBudgetExceeded = Metrics.bPerformanceBudgetExceeded;

    HeightmapExportPerformanceHistory.Add(PerformanceSample);
    while (HeightmapExportPerformanceHistory.Num() > MaxHeightmapPerformanceSamples)
    {
        HeightmapExportPerformanceHistory.RemoveAt(0);
    }

#if !UE_BUILD_SHIPPING
    UE_LOG(LogPlanetaryCreation, Log,
        TEXT("[HeightmapExport][BufferTotals] TrackedAlloc=%.2f MB Width=%d Height=%d"),
        ToMegabytesSigned(TotalTrackedBufferBytes),
        ImageWidth,
        ImageHeight);

    RowSeams.Reset();
    RowSuccessCounts.Reset();
    RowTraversalSums.Reset();
    RowMaxTraversalSteps.Reset();
    RawData.Reset();

    const FPlatformMemoryStats PostBufferCleanupStats = LogMemoryCheckpoint(TEXT("PostBufferCleanup"), &PreviousMemoryStats);
    PreviousMemoryStats = PostBufferCleanupStats;

    const FPlatformMemoryStats FinalDeltaStats = LogMemoryCheckpoint(TEXT("FinalDeltaFromStart"), &MemoryStatsPreExport);
    PreviousMemoryStats = FinalDeltaStats;
#endif

    LastHeightmapExportMetrics = Metrics;

    // Patch in total time to log emitted earlier.
    UE_LOG(LogPlanetaryCreation, Log,
        TEXT("[HeightmapExport][Timing] SamplerSetup=%.2f ms Sample=%.2f ms Encode=%.2f ms Total=%.2f ms"),
        SamplerSetupMs, SamplingMs, EncodeMs, TotalMs);

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Exported heightmap visualization (%dx%d): %s"), ImageWidth, ImageHeight, *OutputPath);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Elevation range: %.1f m (blue) to %.1f m (red)"), MinElevation, MaxElevation);

    return OutputPath;
}

void UTectonicSimulationService::SetAllowUnsafeHeightmapExport(bool bInAllowUnsafe)
{
    bAllowUnsafeHeightmapExport = bInAllowUnsafe;
}

#if WITH_AUTOMATION_TESTS
void UTectonicSimulationService::SetHeightmapExportTestOverrides(bool bInForceModuleFailure, bool bInForceWriteFailure, const FString& InOverrideOutputDirectory)
{
    bForceHeightmapModuleFailure = bInForceModuleFailure;
    bForceHeightmapWriteFailure = bInForceWriteFailure;
    HeightmapExportOverrideDirectory = InOverrideOutputDirectory;
}
#endif
