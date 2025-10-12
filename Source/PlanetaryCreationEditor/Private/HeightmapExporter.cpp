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
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Math/UnrealMathUtility.h"
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
    constexpr double HeightmapSeamToleranceMeters = 0.5;

    // Tiled export baseline (Docs/PathToParity/TiledHeightmapExport.md): 512x512 tiles with 2px overlap.
    constexpr int32 HeightmapTileSizePixels = 512;
    constexpr int32 HeightmapTileOverlapPixels = 2;

    constexpr uint64 HeightmapPNGExtraBytes = 8ull * 1024ull * 1024ull;         // 8 MiB cushion for encoder scratch
    constexpr uint64 HeightmapPreflightSafetyHeadroomBytes = 512ull * 1024ull * 1024ull; // 512 MiB safety margin

    enum class EHeightmapFallbackMode : uint8
    {
        None = 0,
        Sanitized,
        DirectNudge,
        Expanded,
        Wrapped,
        Hint,
        RowReuse
    };

    constexpr double BytesToMiB(uint64 Bytes)
    {
        return static_cast<double>(Bytes) / (1024.0 * 1024.0);
    }

    struct FHeightmapRescueAggregation
    {
        uint64 FailureFallbackAttempts = 0;
        uint64 FailureFallbackSuccesses = 0;
        uint64 FailureFallbackFailures = 0;
        uint64 ExpandedAttempts = 0;
        uint64 ExpandedSuccesses = 0;
        uint64 SanitizedFallbacks = 0;
        uint64 DirectNudgeFallbacks = 0;
        uint64 ExpandedFallbacks = 0;
        uint64 WrappedFallbacks = 0;
        uint64 HintFallbacks = 0;
        uint64 RowReuseFallbacks = 0;

        void Accumulate(
            bool bCorePixel,
            bool bFallbackAttempted,
            bool bFallbackUsed,
            bool bFinalHit,
            EHeightmapFallbackMode Mode,
            bool bExpandedAttempted,
            bool bExpandedHit)
        {
            if (!bCorePixel)
            {
                return;
            }

            if (bFallbackAttempted)
            {
                ++FailureFallbackAttempts;
                if (bFinalHit)
                {
                    ++FailureFallbackSuccesses;
                }
                else
                {
                    ++FailureFallbackFailures;
                }
            }

            if (bExpandedAttempted)
            {
                ++ExpandedAttempts;
                if (bExpandedHit && bFinalHit)
                {
                    ++ExpandedSuccesses;
                }
            }

            if (!bFallbackUsed)
            {
                return;
            }

            switch (Mode)
            {
            case EHeightmapFallbackMode::Sanitized:
                ++SanitizedFallbacks;
                break;
            case EHeightmapFallbackMode::DirectNudge:
                ++DirectNudgeFallbacks;
                break;
            case EHeightmapFallbackMode::Expanded:
                ++ExpandedFallbacks;
                break;
            case EHeightmapFallbackMode::Wrapped:
                ++WrappedFallbacks;
                break;
            case EHeightmapFallbackMode::Hint:
                ++HintFallbacks;
                break;
            case EHeightmapFallbackMode::RowReuse:
                ++RowReuseFallbacks;
                break;
            default:
                break;
            }
        }
    };

    static TAutoConsoleVariable<int32> CVarAllowUnsafeHeightmapExport(
        TEXT("r.PlanetaryCreation.AllowUnsafeHeightmapExport"),
        0,
        TEXT("Allow heightmap exports to bypass the 512x256 safety baseline for supervised runs."),
        ECVF_Default);

    struct FHeightmapTileBuffer
    {
        int32 SampleStartX = 0;
        int32 SampleStartY = 0;
        int32 SampleEndX = 0;
        int32 SampleEndY = 0;
        int32 CoreStartX = 0;
        int32 CoreStartY = 0;
        int32 CoreEndX = 0;
        int32 CoreEndY = 0;
        int32 SampleWidth = 0;
        int32 SampleHeight = 0;
        double ProcessingMs = 0.0;
        TArray<uint8> RGBA;
        int32 CoreInitialMissCount = 0;
        int32 CoreFinalMissCount = 0;
        int32 CoreFinalHitCount = 0;
        int32 CoreFallbackSuccessCount = 0;
        int32 CoreFallbackFailureCount = 0;
        int32 CoreExpandedAttemptCount = 0;
        int32 CoreExpandedSuccessCount = 0;
#if !UE_BUILD_SHIPPING
        TArray<float> LeftCoreElevations;
        TArray<float> LeftOverlapElevations;
        TArray<uint8> LeftCoreHits;
        TArray<uint8> LeftOverlapHits;
        TArray<int32> LeftCoreTriangles;
        TArray<int32> LeftOverlapTriangles;
        TArray<uint8> LeftCoreSteps;
        TArray<uint8> LeftOverlapSteps;
        TArray<uint8> LeftCoreUsedFallback;
        TArray<uint8> LeftOverlapUsedFallback;
        TArray<uint8> LeftCoreUsedExpanded;
        TArray<uint8> LeftOverlapUsedExpanded;
        TArray<uint8> LeftCoreFallbackModes;
        TArray<uint8> LeftOverlapFallbackModes;
        TArray<float> RightCoreElevations;
        TArray<float> RightOverlapElevations;
        TArray<uint8> RightCoreHits;
        TArray<uint8> RightOverlapHits;
        TArray<int32> RightCoreTriangles;
        TArray<int32> RightOverlapTriangles;
        TArray<uint8> RightCoreSteps;
        TArray<uint8> RightOverlapSteps;
        TArray<uint8> RightCoreUsedFallback;
        TArray<uint8> RightOverlapUsedFallback;
        TArray<uint8> RightCoreUsedExpanded;
        TArray<uint8> RightOverlapUsedExpanded;
        TArray<uint8> RightCoreFallbackModes;
        TArray<uint8> RightOverlapFallbackModes;
#endif
    };

    struct FHeightmapRowSeam
    {
        float LeftElevation = 0.0f;
        float RightElevation = 0.0f;
        uint8 bLeftHit = 0;
        uint8 bRightHit = 0;
        int32 LeftTriangle = INDEX_NONE;
        int32 RightTriangle = INDEX_NONE;
        uint8 LeftFallbackMode = static_cast<uint8>(EHeightmapFallbackMode::None);
        uint8 RightFallbackMode = static_cast<uint8>(EHeightmapFallbackMode::None);
    };

    struct FHeightmapSeamHintRow
    {
        float Elevation = 0.0f;
        int32 Triangle = INDEX_NONE;
        uint8 bHit = 0;
        uint8 FallbackMode = static_cast<uint8>(EHeightmapFallbackMode::None);
        uint8 Steps = 0;
        int32 PixelX = 0;
    };

    struct FHeightmapSeamAggregate
    {
        double AbsSum = 0.0;
        double RmsSum = 0.0;
        double MaxAbs = 0.0;
        int32 Samples = 0;
        int32 RowsAboveHalfMeter = 0;
        int32 RowsWithFailures = 0;
    };

#if !UE_BUILD_SHIPPING
    static void EvaluateTileSeams(
        const FHeightmapTileBuffer& Tile,
        const TCHAR* PhaseLabel,
        bool bAllowEnsure,
        bool bEvaluateLeft,
        bool bEvaluateRight,
        FHeightmapSeamAggregate* Aggregate,
        bool bAccumulate)
    {
        const double SeamThresholdMeters = HeightmapSeamToleranceMeters;

        auto Evaluate = [&](const TCHAR* SideLabel,
                            const TArray<float>& CoreElevations,
                            const TArray<uint8>& CoreHits,
                            const TArray<float>& OverlapElevations,
                            const TArray<uint8>& OverlapHits,
                            const TArray<int32>& CoreTriangles,
                            const TArray<int32>& OverlapTriangles,
                            const TArray<uint8>& CoreSteps,
                            const TArray<uint8>& OverlapSteps,
                            const TArray<uint8>& CoreFallbackFlags,
                            const TArray<uint8>& OverlapFallbackFlags,
                            const TArray<uint8>& CoreExpandedFlags,
                            const TArray<uint8>& OverlapExpandedFlags,
                            const TArray<uint8>& CoreFallbackModes,
                            const TArray<uint8>& OverlapFallbackModes)
        {
            double TotalDelta = 0.0;
            double MaxDelta = 0.0;
            int32 SampleCount = 0;
            int32 ThresholdExceeds = 0;

            const int32 RowCount = CoreElevations.Num();
            for (int32 RowIndex = 0; RowIndex < RowCount; ++RowIndex)
            {
                const int32 GlobalY = Tile.CoreStartY + RowIndex;
                const bool bCoreHit = CoreHits.IsValidIndex(RowIndex) && CoreHits[RowIndex] != 0;
                const bool bOverlapHit = OverlapHits.IsValidIndex(RowIndex) && OverlapHits[RowIndex] != 0;

                if (!bCoreHit || !bOverlapHit)
                {
                    if (Aggregate && bAccumulate)
                    {
                        ++Aggregate->RowsWithFailures;
                    }
                    if (bAllowEnsure)
                    {
                        UE_LOG(LogPlanetaryCreation, Warning,
                            TEXT("[HeightmapExport][SeamDebug][%s] TileCore=(%d,%d)->(%d,%d) Side=%s Row=%d LocalRow=%d MissingSample CoreHit=%d OverlapHit=%d"),
                            PhaseLabel,
                            Tile.CoreStartX,
                            Tile.CoreStartY,
                            Tile.CoreEndX,
                            Tile.CoreEndY,
                            SideLabel,
                            GlobalY,
                            RowIndex,
                            bCoreHit ? 1 : 0,
                            bOverlapHit ? 1 : 0);
                    }
                    else
                    {
                        UE_LOG(LogPlanetaryCreation, Verbose,
                            TEXT("[HeightmapExport][SeamDebug][%s] TileCore=(%d,%d)->(%d,%d) Side=%s Row=%d LocalRow=%d MissingSample CoreHit=%d OverlapHit=%d"),
                            PhaseLabel,
                            Tile.CoreStartX,
                            Tile.CoreStartY,
                            Tile.CoreEndX,
                            Tile.CoreEndY,
                            SideLabel,
                            GlobalY,
                            RowIndex,
                            bCoreHit ? 1 : 0,
                            bOverlapHit ? 1 : 0);
                    }
                    continue;
                }

                const double DeltaMeters = FMath::Abs(static_cast<double>(CoreElevations[RowIndex]) - static_cast<double>(OverlapElevations[RowIndex]));
                TotalDelta += DeltaMeters;
                MaxDelta = FMath::Max(MaxDelta, DeltaMeters);
                ++SampleCount;
                if (Aggregate && bAccumulate)
                {
                    Aggregate->AbsSum += DeltaMeters;
                    Aggregate->RmsSum += DeltaMeters * DeltaMeters;
                    Aggregate->MaxAbs = FMath::Max(Aggregate->MaxAbs, DeltaMeters);
                    ++Aggregate->Samples;
                }

                if (DeltaMeters > SeamThresholdMeters)
                {
                    if (Aggregate && bAccumulate)
                    {
                        ++Aggregate->RowsAboveHalfMeter;
                    }
                    const int32 CoreTriangle = CoreTriangles.IsValidIndex(RowIndex) ? CoreTriangles[RowIndex] : INDEX_NONE;
                    const int32 OverlapTriangle = OverlapTriangles.IsValidIndex(RowIndex) ? OverlapTriangles[RowIndex] : INDEX_NONE;
                    const uint8 CoreStep = CoreSteps.IsValidIndex(RowIndex) ? CoreSteps[RowIndex] : 0;
                    const uint8 OverlapStep = OverlapSteps.IsValidIndex(RowIndex) ? OverlapSteps[RowIndex] : 0;
                    const uint8 CoreFallback = CoreFallbackFlags.IsValidIndex(RowIndex) ? CoreFallbackFlags[RowIndex] : 0;
                    const uint8 OverlapFallback = OverlapFallbackFlags.IsValidIndex(RowIndex) ? OverlapFallbackFlags[RowIndex] : 0;
                    const uint8 CoreExpanded = CoreExpandedFlags.IsValidIndex(RowIndex) ? CoreExpandedFlags[RowIndex] : 0;
                    const uint8 OverlapExpanded = OverlapExpandedFlags.IsValidIndex(RowIndex) ? OverlapExpandedFlags[RowIndex] : 0;
                    const uint8 CoreFallbackMode = CoreFallbackModes.IsValidIndex(RowIndex) ? CoreFallbackModes[RowIndex] : static_cast<uint8>(EHeightmapFallbackMode::None);
                    const uint8 OverlapFallbackMode = OverlapFallbackModes.IsValidIndex(RowIndex) ? OverlapFallbackModes[RowIndex] : static_cast<uint8>(EHeightmapFallbackMode::None);

                    if (bAllowEnsure)
                    {
                        UE_LOG(LogPlanetaryCreation, Warning,
                            TEXT("[HeightmapExport][SeamDebug][%s] TileCore=(%d,%d)->(%d,%d) Side=%s Row=%d LocalRow=%d Delta=%.3f m CoreElev=%.3f OverlapElev=%.3f CoreHit=%d OverlapHit=%d CoreTri=%d OverlapTri=%d CoreSteps=%u OverlapSteps=%u CoreFallback=%u OverlapFallback=%u CoreExpanded=%u OverlapExpanded=%u CoreFallbackMode=%u OverlapFallbackMode=%u"),
                            PhaseLabel,
                            Tile.CoreStartX,
                            Tile.CoreStartY,
                            Tile.CoreEndX,
                            Tile.CoreEndY,
                            SideLabel,
                            GlobalY,
                            RowIndex,
                            DeltaMeters,
                            static_cast<double>(CoreElevations[RowIndex]),
                            static_cast<double>(OverlapElevations[RowIndex]),
                            1,
                            1,
                            CoreTriangle,
                            OverlapTriangle,
                            static_cast<uint32>(CoreStep),
                            static_cast<uint32>(OverlapStep),
                            static_cast<uint32>(CoreFallback),
                            static_cast<uint32>(OverlapFallback),
                            static_cast<uint32>(CoreExpanded),
                            static_cast<uint32>(OverlapExpanded),
                            static_cast<uint32>(CoreFallbackMode),
                            static_cast<uint32>(OverlapFallbackMode));
                    }
                    else
                    {
                        UE_LOG(LogPlanetaryCreation, Verbose,
                            TEXT("[HeightmapExport][SeamDebug][%s] TileCore=(%d,%d)->(%d,%d) Side=%s Row=%d LocalRow=%d Delta=%.3f m CoreElev=%.3f OverlapElev=%.3f CoreHit=%d OverlapHit=%d CoreTri=%d OverlapTri=%d CoreSteps=%u OverlapSteps=%u CoreFallback=%u OverlapFallback=%u CoreExpanded=%u OverlapExpanded=%u CoreFallbackMode=%u OverlapFallbackMode=%u"),
                            PhaseLabel,
                            Tile.CoreStartX,
                            Tile.CoreStartY,
                            Tile.CoreEndX,
                            Tile.CoreEndY,
                            SideLabel,
                            GlobalY,
                            RowIndex,
                            DeltaMeters,
                            static_cast<double>(CoreElevations[RowIndex]),
                            static_cast<double>(OverlapElevations[RowIndex]),
                            1,
                            1,
                            CoreTriangle,
                            OverlapTriangle,
                            static_cast<uint32>(CoreStep),
                            static_cast<uint32>(OverlapStep),
                            static_cast<uint32>(CoreFallback),
                            static_cast<uint32>(OverlapFallback),
                            static_cast<uint32>(CoreExpanded),
                            static_cast<uint32>(OverlapExpanded),
                            static_cast<uint32>(CoreFallbackMode),
                            static_cast<uint32>(OverlapFallbackMode));
                    }
                }
            }

            if (SampleCount > 0)
            {
                const double MeanDelta = TotalDelta / static_cast<double>(SampleCount);
                UE_LOG(LogPlanetaryCreation, Log,
                    TEXT("[HeightmapExport][SeamSummary][%s] TileCore=(%d,%d)->(%d,%d) Side=%s Samples=%d Mean=%.3f m Max=%.3f m OverThreshold=%d"),
                    PhaseLabel,
                    Tile.CoreStartX,
                    Tile.CoreStartY,
                    Tile.CoreEndX,
                    Tile.CoreEndY,
                    SideLabel,
                    SampleCount,
                    MeanDelta,
                    MaxDelta,
                    ThresholdExceeds);

                if (bAllowEnsure)
                {
                    ensureMsgf(MaxDelta <= SeamThresholdMeters + 0.001,
                        TEXT("Seam delta %.3f m exceeds threshold on TileCore=(%d,%d)->(%d,%d) Side=%s"),
                        MaxDelta,
                        Tile.CoreStartX,
                        Tile.CoreStartY,
                        Tile.CoreEndX,
                        Tile.CoreEndY,
                        SideLabel);
                }
            }
        };

        if (bEvaluateLeft && Tile.LeftCoreElevations.Num() > 0 && Tile.LeftOverlapElevations.Num() > 0)
        {
            Evaluate(TEXT("Left"),
                Tile.LeftCoreElevations,
                Tile.LeftCoreHits,
                Tile.LeftOverlapElevations,
                Tile.LeftOverlapHits,
                Tile.LeftCoreTriangles,
                Tile.LeftOverlapTriangles,
                Tile.LeftCoreSteps,
                Tile.LeftOverlapSteps,
                Tile.LeftCoreUsedFallback,
                Tile.LeftOverlapUsedFallback,
                Tile.LeftCoreUsedExpanded,
                Tile.LeftOverlapUsedExpanded,
                Tile.LeftCoreFallbackModes,
                Tile.LeftOverlapFallbackModes);
        }

        if (bEvaluateRight && Tile.RightCoreElevations.Num() > 0 && Tile.RightOverlapElevations.Num() > 0)
        {
            Evaluate(TEXT("Right"),
                Tile.RightCoreElevations,
                Tile.RightCoreHits,
                Tile.RightOverlapElevations,
                Tile.RightOverlapHits,
                Tile.RightCoreTriangles,
                Tile.RightOverlapTriangles,
                Tile.RightCoreSteps,
                Tile.RightOverlapSteps,
                Tile.RightCoreUsedFallback,
                Tile.RightOverlapUsedFallback,
                Tile.RightCoreUsedExpanded,
                Tile.RightOverlapUsedExpanded,
                Tile.RightCoreFallbackModes,
                Tile.RightOverlapFallbackModes);
        }
    }
#endif // !UE_BUILD_SHIPPING

    static FHeightmapTileBuffer ProcessTile(
        const FHeightmapSampler& Sampler,
        const PlanetaryCreation::Heightmap::FHeightmapPalette& Palette,
        int32 ImageWidth,
        int32 ImageHeight,
        double InvWidth,
        double InvHeight,
        int32 SampleStartX,
        int32 SampleStartY,
        int32 SampleEndX,
        int32 SampleEndY,
        int32 CoreStartX,
        int32 CoreStartY,
        int32 CoreEndX,
        int32 CoreEndY,
        const TArray<FHeightmapSeamHintRow>& SeamHints,
        TArray<FHeightmapRowSeam>& RowSeams,
        TArray<uint32>& RowSuccessCounts,
        TArray<uint64>& RowTraversalSums,
        TArray<uint8>& RowMaxTraversalSteps,
        FHeightmapRescueAggregation& RescueAggregation,
        TArray<int32>& RowLastTriangles)
    {
        FHeightmapTileBuffer Tile;
        Tile.SampleStartX = SampleStartX;
        Tile.SampleStartY = SampleStartY;
        Tile.SampleEndX = SampleEndX;
        Tile.SampleEndY = SampleEndY;
        Tile.CoreStartX = CoreStartX;
        Tile.CoreStartY = CoreStartY;
        Tile.CoreEndX = CoreEndX;
        Tile.CoreEndY = CoreEndY;
        Tile.SampleWidth = FMath::Max(0, SampleEndX - SampleStartX);
        Tile.SampleHeight = FMath::Max(0, SampleEndY - SampleStartY);
        const int32 CoreWidth = FMath::Max(0, CoreEndX - CoreStartX);
        const int32 CoreHeight = FMath::Max(0, CoreEndY - CoreStartY);
        const bool bHasLeftOverlap = (CoreStartX > SampleStartX);
        const bool bHasRightOverlap = (CoreEndX < SampleEndX);

#if !UE_BUILD_SHIPPING
        if (CoreHeight > 0)
        {
            const float NaNValue = std::numeric_limits<float>::quiet_NaN();
            if (bHasLeftOverlap)
            {
                Tile.LeftCoreElevations.Init(NaNValue, CoreHeight);
                Tile.LeftOverlapElevations.Init(NaNValue, CoreHeight);
                Tile.LeftCoreHits.Init(0, CoreHeight);
                Tile.LeftOverlapHits.Init(0, CoreHeight);
                Tile.LeftCoreTriangles.Init(INDEX_NONE, CoreHeight);
                Tile.LeftOverlapTriangles.Init(INDEX_NONE, CoreHeight);
                Tile.LeftCoreSteps.Init(0, CoreHeight);
                Tile.LeftOverlapSteps.Init(0, CoreHeight);
                Tile.LeftCoreUsedFallback.Init(0, CoreHeight);
                Tile.LeftOverlapUsedFallback.Init(0, CoreHeight);
                Tile.LeftCoreUsedExpanded.Init(0, CoreHeight);
                Tile.LeftOverlapUsedExpanded.Init(0, CoreHeight);
                Tile.LeftCoreFallbackModes.Init(static_cast<uint8>(EHeightmapFallbackMode::None), CoreHeight);
                Tile.LeftOverlapFallbackModes.Init(static_cast<uint8>(EHeightmapFallbackMode::None), CoreHeight);
            }
            if (bHasRightOverlap)
            {
                Tile.RightCoreElevations.Init(NaNValue, CoreHeight);
                Tile.RightOverlapElevations.Init(NaNValue, CoreHeight);
                Tile.RightCoreHits.Init(0, CoreHeight);
                Tile.RightOverlapHits.Init(0, CoreHeight);
                Tile.RightCoreTriangles.Init(INDEX_NONE, CoreHeight);
                Tile.RightOverlapTriangles.Init(INDEX_NONE, CoreHeight);
                Tile.RightCoreSteps.Init(0, CoreHeight);
                Tile.RightOverlapSteps.Init(0, CoreHeight);
                Tile.RightCoreUsedFallback.Init(0, CoreHeight);
                Tile.RightOverlapUsedFallback.Init(0, CoreHeight);
                Tile.RightCoreUsedExpanded.Init(0, CoreHeight);
                Tile.RightOverlapUsedExpanded.Init(0, CoreHeight);
                Tile.RightCoreFallbackModes.Init(static_cast<uint8>(EHeightmapFallbackMode::None), CoreHeight);
                Tile.RightOverlapFallbackModes.Init(static_cast<uint8>(EHeightmapFallbackMode::None), CoreHeight);
            }
        }
#else
        (void)CoreWidth;
        (void)CoreHeight;
        (void)bHasLeftOverlap;
        (void)bHasRightOverlap;
#endif

        const int64 PixelCount = static_cast<int64>(Tile.SampleWidth) * static_cast<int64>(Tile.SampleHeight);

        if (PixelCount <= 0)
        {
            Tile.RGBA.Reset();
#if !UE_BUILD_SHIPPING
            UE_LOG(LogPlanetaryCreation, Log,
                TEXT("[HeightmapExport][TileMemory] Start=(%d,%d) End=(%d,%d) Size=0x0 Pixels=0 Alloc=0.00 MiB"),
                Tile.SampleStartX,
                Tile.SampleStartY,
                Tile.SampleEndX,
                Tile.SampleEndY);
#endif
            return Tile;
        }

        Tile.RGBA.SetNumUninitialized(PixelCount * 4);

#if !UE_BUILD_SHIPPING
        const double AllocMiB = BytesToMiB(static_cast<uint64>(Tile.RGBA.GetAllocatedSize()));
        UE_LOG(LogPlanetaryCreation, Log,
            TEXT("[HeightmapExport][TileMemory] Start=(%d,%d) End=(%d,%d) Size=%dx%d Pixels=%lld Alloc=%.2f MiB"),
            Tile.SampleStartX,
            Tile.SampleStartY,
            Tile.SampleEndX,
            Tile.SampleEndY,
            Tile.SampleWidth,
            Tile.SampleHeight,
            PixelCount,
            AllocMiB);
#endif

        const double TileStartSeconds = FPlatformTime::Seconds();

        auto TryFallbackSample = [&](const FVector2d& BaseUV, FHeightmapSampler::FSampleInfo& InOutInfo, double& InOutElevation, bool& bOutExpandedAttempted, bool& bOutExpandedHit, EHeightmapFallbackMode& OutMode) -> bool
        {
            OutMode = EHeightmapFallbackMode::None;
            bOutExpandedAttempted = false;
            bOutExpandedHit = false;

            const auto SanitizeUV = [&](const FVector2d& InUV) -> FVector2d
            {
                double WrappedU = FMath::Fmod(InUV.X, 1.0);
                if (WrappedU < 0.0)
                {
                    WrappedU += 1.0;
                }

                WrappedU = FMath::Clamp(WrappedU, FHeightmapSampler::PoleAvoidanceEpsilon, 1.0 - FHeightmapSampler::PoleAvoidanceEpsilon);
                const double ClampedV = FMath::Clamp(InUV.Y, FHeightmapSampler::PoleAvoidanceEpsilon, 1.0 - FHeightmapSampler::PoleAvoidanceEpsilon);
                return FVector2d(WrappedU, ClampedV);
            };

            const FVector2d BaseSanitized = SanitizeUV(BaseUV);

            if (!BaseSanitized.Equals(BaseUV, KINDA_SMALL_NUMBER))
            {
                FHeightmapSampler::FSampleInfo SanitizedInfo;
                const double SanitizedElevation = Sampler.SampleElevationAtUV(BaseSanitized, &SanitizedInfo);
                if (SanitizedInfo.bHit)
                {
                    InOutInfo = SanitizedInfo;
                    InOutElevation = SanitizedElevation;
                    OutMode = EHeightmapFallbackMode::Sanitized;
                    return true;
                }
            }

            auto TryCandidate = [&](const FVector2d& CandidateUV, bool bMarkExpanded, EHeightmapFallbackMode Mode) -> bool
            {
                const FVector2d Sanitized = SanitizeUV(CandidateUV);
                if (Sanitized.Equals(BaseSanitized, KINDA_SMALL_NUMBER))
                {
                    return false;
                }

                FHeightmapSampler::FSampleInfo CandidateInfo;
                const double CandidateElevation = Sampler.SampleElevationAtUV(Sanitized, &CandidateInfo);
                if (!CandidateInfo.bHit)
                {
                    return false;
                }

                InOutInfo = CandidateInfo;
                InOutElevation = CandidateElevation;
                if (bMarkExpanded)
                {
                    bOutExpandedHit = true;
                }
                OutMode = Mode;
                return true;
            };

            const double BaseNudge = FHeightmapSampler::PoleAvoidanceEpsilon * 4.0;
            const FVector2d BasicOffsets[] = {
                FVector2d(BaseNudge, 0.0),
                FVector2d(-BaseNudge, 0.0),
                FVector2d(0.0, BaseNudge),
                FVector2d(0.0, -BaseNudge)
            };

            for (const FVector2d& Offset : BasicOffsets)
            {
                if (TryCandidate(BaseUV + Offset, false, EHeightmapFallbackMode::DirectNudge))
                {
                    return true;
                }
            }

            const double PixelStepX = FMath::Max(InvWidth * 0.5, BaseNudge);
            const double PixelStepY = FMath::Max(InvHeight * 0.5, BaseNudge);
            const double ScaleLevels[] = { 1.0, 2.0 };

            for (double Scale : ScaleLevels)
            {
                for (int32 DX = -1; DX <= 1; ++DX)
                {
                    for (int32 DY = -1; DY <= 1; ++DY)
                    {
                        if (DX == 0 && DY == 0)
                        {
                            continue;
                        }

                        bOutExpandedAttempted = true;
                        const FVector2d Offset(
                            static_cast<double>(DX) * PixelStepX * Scale,
                            static_cast<double>(DY) * PixelStepY * Scale);

                        if (TryCandidate(BaseUV + Offset, true, EHeightmapFallbackMode::Expanded))
                        {
                            return true;
                        }
                    }
                }
            }

            const FVector2d SeamCandidates[] = {
                FVector2d(BaseUV.X + 1.0 - PixelStepX, BaseUV.Y),
                FVector2d(BaseUV.X - 1.0 + PixelStepX, BaseUV.Y)
            };

            for (const FVector2d& Candidate : SeamCandidates)
            {
                bOutExpandedAttempted = true;
                if (TryCandidate(Candidate, true, EHeightmapFallbackMode::Wrapped))
                {
                    return true;
                }
            }

            return false;
        };

        for (int32 LocalY = 0; LocalY < Tile.SampleHeight; ++LocalY)
        {
            const int32 GlobalY = Tile.SampleStartY + LocalY;
            if (GlobalY >= ImageHeight)
            {
                break;
            }

            const double V = (static_cast<double>(GlobalY) + 0.5) * InvHeight;
            const int32 TileRowOffset = LocalY * Tile.SampleWidth * 4;

            for (int32 LocalX = 0; LocalX < Tile.SampleWidth; ++LocalX)
            {
                const int32 GlobalX = Tile.SampleStartX + LocalX;
                if (GlobalX >= ImageWidth)
                {
                    break;
                }
                const bool bRowInCore = (GlobalY >= Tile.CoreStartY && GlobalY < Tile.CoreEndY);
                const bool bColumnInCore = (GlobalX >= Tile.CoreStartX && GlobalX < Tile.CoreEndX);
                const bool bContributes = bRowInCore && bColumnInCore;

                const double U = (static_cast<double>(GlobalX) + 0.5) * InvWidth;
                const FVector2d UV(U, V);

                FHeightmapSampler::FSampleInfo SampleInfo;
                double Elevation = 0.0;
                bool bUsedHint = false;
                EHeightmapFallbackMode FallbackMode = EHeightmapFallbackMode::None;

                const bool bHasSeamHint = bContributes && (GlobalX == Tile.CoreStartX) && SeamHints.IsValidIndex(GlobalY);
                if (bHasSeamHint)
                {
                    const FHeightmapSeamHintRow& SeamHint = SeamHints[GlobalY];
                    if (SeamHint.bHit != 0 && SeamHint.Triangle != INDEX_NONE)
                    {
                        if (Sampler.SampleElevationAtUVWithHint(UV, SeamHint.Triangle, &SampleInfo, Elevation))
                        {
                            bUsedHint = true;
                            FallbackMode = EHeightmapFallbackMode::Hint;
                        }
                    }
                }

                if (!bUsedHint)
                {
                    Elevation = Sampler.SampleElevationAtUV(UV, &SampleInfo);
                }

                const bool bInitialHit = SampleInfo.bHit;
                const bool bOriginalHit = bInitialHit;

                if (!bInitialHit && bContributes)
                {
                    ++Tile.CoreInitialMissCount;
                }

                bool bFinalHit = bInitialHit;
                bool bExpandedAttempted = false;
                bool bExpandedHit = false;
                bool bFallbackAttempted = false;

                if (!bInitialHit)
                {
                    if (bContributes)
                    {
                        bFallbackAttempted = true;
                        bFinalHit = TryFallbackSample(UV, SampleInfo, Elevation, bExpandedAttempted, bExpandedHit, FallbackMode);

                        if (bExpandedAttempted)
                        {
                            ++Tile.CoreExpandedAttemptCount;
                        }

                        if (!bFinalHit && SampleInfo.TriangleIndex != INDEX_NONE)
                        {
                            FHeightmapSampler::FSampleInfo ClampedInfo;
                            double ClampedElevation = Elevation;
                            if (Sampler.SampleElevationAtUVWithClampedHint(UV, SampleInfo.TriangleIndex, &ClampedInfo, ClampedElevation))
                            {
                                bFinalHit = true;
                                SampleInfo = ClampedInfo;
                                Elevation = ClampedElevation;
                                FallbackMode = EHeightmapFallbackMode::Sanitized;
                                bExpandedHit = false;
                            }
                        }

                        if (bFinalHit)
                        {
                            ++Tile.CoreFallbackSuccessCount;
                            if (bExpandedAttempted && bExpandedHit)
                            {
                                ++Tile.CoreExpandedSuccessCount;
                            }
                        }
                        else
                        {
                            ++Tile.CoreFallbackFailureCount;
                        }
                    }
                    else
                    {
                        bFinalHit = TryFallbackSample(UV, SampleInfo, Elevation, bExpandedAttempted, bExpandedHit, FallbackMode);
                    }
                }

                if (!bFinalHit && bContributes)
                {
                    const int32 LastTriangle = RowLastTriangles.IsValidIndex(GlobalY) ? RowLastTriangles[GlobalY] : INDEX_NONE;
                    if (LastTriangle != INDEX_NONE)
                    {
                        FHeightmapSampler::FSampleInfo ReuseInfo;
                        double ReuseElevation = Elevation;
                        if (Sampler.SampleElevationAtUVWithClampedHint(UV, LastTriangle, &ReuseInfo, ReuseElevation))
                        {
                            bFinalHit = true;
                            SampleInfo = ReuseInfo;
                            Elevation = ReuseElevation;
                            FallbackMode = EHeightmapFallbackMode::RowReuse;
                            bExpandedHit = false;
                            bFallbackAttempted = true;
                        }
                    }
                }

                if (bContributes)
                {
                    if (bFinalHit && SampleInfo.bHit)
                    {
                        ++Tile.CoreFinalHitCount;
                        RowSuccessCounts[GlobalY] += 1u;
                    }
                    else
                    {
                        ++Tile.CoreFinalMissCount;
                    }
                }

                const bool bUsedFallback = bUsedHint || (!bOriginalHit && bFinalHit);
                const bool bUsedExpanded = bUsedFallback && bExpandedHit;
                const uint8 ClampedSteps = static_cast<uint8>(FMath::Clamp(SampleInfo.Steps, 0, 255));

                RescueAggregation.Accumulate(
                    bContributes,
                    bFallbackAttempted,
                    bUsedFallback,
                    bFinalHit && SampleInfo.bHit,
                    FallbackMode,
                    bExpandedAttempted,
                    bExpandedHit);

                if (bContributes)
                {
                    if (bFinalHit && SampleInfo.bHit)
                    {
                        RowLastTriangles[GlobalY] = SampleInfo.TriangleIndex;
                    }
                    else
                    {
                        RowLastTriangles[GlobalY] = INDEX_NONE;
                    }

                    RowTraversalSums[GlobalY] += static_cast<uint64>(ClampedSteps);
                    RowMaxTraversalSteps[GlobalY] = FMath::Max<uint8>(RowMaxTraversalSteps[GlobalY], ClampedSteps);
                }

                if (bContributes && GlobalX == 0)
                {
                    FHeightmapRowSeam& Seam = RowSeams[GlobalY];
                    Seam.LeftElevation = static_cast<float>(Elevation);
                    Seam.bLeftHit = SampleInfo.bHit ? 1 : 0;
                    Seam.LeftTriangle = SampleInfo.bHit ? SampleInfo.TriangleIndex : INDEX_NONE;
                    Seam.LeftFallbackMode = static_cast<uint8>(FallbackMode);
                }
                else if (bContributes && GlobalX == ImageWidth - 1)
                {
                    FHeightmapRowSeam& Seam = RowSeams[GlobalY];
                    Seam.RightElevation = static_cast<float>(Elevation);
                    Seam.bRightHit = SampleInfo.bHit ? 1 : 0;
                    Seam.RightTriangle = SampleInfo.bHit ? SampleInfo.TriangleIndex : INDEX_NONE;
                    Seam.RightFallbackMode = static_cast<uint8>(FallbackMode);
                }

#if !UE_BUILD_SHIPPING
                if (bRowInCore && CoreHeight > 0)
                {
                    const int32 CoreRowIndex = GlobalY - Tile.CoreStartY;
                    if (CoreRowIndex >= 0 && CoreRowIndex < CoreHeight)
                    {
                        const uint8 HitValue = (bFinalHit && SampleInfo.bHit) ? 1 : 0;
                        const int32 TriangleValue = (HitValue != 0) ? SampleInfo.TriangleIndex : INDEX_NONE;
                        const uint8 StepValue = (HitValue != 0) ? ClampedSteps : 0;
                        const uint8 FallbackValue = bUsedFallback ? 1 : 0;
                        const uint8 ExpandedValue = bUsedExpanded ? 1 : 0;
                        const uint8 FallbackModeValue = static_cast<uint8>(FallbackMode);

                        if (bHasLeftOverlap)
                        {
                            if (GlobalX == Tile.CoreStartX - 1 && Tile.LeftOverlapElevations.IsValidIndex(CoreRowIndex))
                            {
                                Tile.LeftOverlapElevations[CoreRowIndex] = static_cast<float>(Elevation);
                                Tile.LeftOverlapHits[CoreRowIndex] = SampleInfo.bHit ? 1 : 0;
                                Tile.LeftOverlapTriangles[CoreRowIndex] = TriangleValue;
                                Tile.LeftOverlapSteps[CoreRowIndex] = StepValue;
                                Tile.LeftOverlapUsedFallback[CoreRowIndex] = FallbackValue;
                                Tile.LeftOverlapUsedExpanded[CoreRowIndex] = ExpandedValue;
                                Tile.LeftOverlapFallbackModes[CoreRowIndex] = FallbackModeValue;
                            }
                            else if (GlobalX == Tile.CoreStartX && Tile.LeftCoreElevations.IsValidIndex(CoreRowIndex))
                            {
                                Tile.LeftCoreElevations[CoreRowIndex] = static_cast<float>(Elevation);
                                Tile.LeftCoreHits[CoreRowIndex] = SampleInfo.bHit ? 1 : 0;
                                Tile.LeftCoreTriangles[CoreRowIndex] = TriangleValue;
                                Tile.LeftCoreSteps[CoreRowIndex] = StepValue;
                                Tile.LeftCoreUsedFallback[CoreRowIndex] = FallbackValue;
                                Tile.LeftCoreUsedExpanded[CoreRowIndex] = ExpandedValue;
                                Tile.LeftCoreFallbackModes[CoreRowIndex] = FallbackModeValue;
                            }
                        }

                        if (bHasRightOverlap)
                        {
                            if (GlobalX == Tile.CoreEndX - 1 && Tile.RightCoreElevations.IsValidIndex(CoreRowIndex))
                            {
                                Tile.RightCoreElevations[CoreRowIndex] = static_cast<float>(Elevation);
                                Tile.RightCoreHits[CoreRowIndex] = SampleInfo.bHit ? 1 : 0;
                                Tile.RightCoreTriangles[CoreRowIndex] = TriangleValue;
                                Tile.RightCoreSteps[CoreRowIndex] = StepValue;
                                Tile.RightCoreUsedFallback[CoreRowIndex] = FallbackValue;
                                Tile.RightCoreUsedExpanded[CoreRowIndex] = ExpandedValue;
                                Tile.RightCoreFallbackModes[CoreRowIndex] = FallbackModeValue;
                            }
                            else if (GlobalX == Tile.CoreEndX && Tile.RightOverlapElevations.IsValidIndex(CoreRowIndex))
                            {
                                Tile.RightOverlapElevations[CoreRowIndex] = static_cast<float>(Elevation);
                                Tile.RightOverlapHits[CoreRowIndex] = SampleInfo.bHit ? 1 : 0;
                                Tile.RightOverlapTriangles[CoreRowIndex] = TriangleValue;
                                Tile.RightOverlapSteps[CoreRowIndex] = StepValue;
                                Tile.RightOverlapUsedFallback[CoreRowIndex] = FallbackValue;
                                Tile.RightOverlapUsedExpanded[CoreRowIndex] = ExpandedValue;
                                Tile.RightOverlapFallbackModes[CoreRowIndex] = FallbackModeValue;
                            }
                        }
                    }
                }
#endif

                const FColor PixelColor = Palette.Sample(Elevation);
                const int32 PixelIndex = TileRowOffset + (LocalX * 4);
                Tile.RGBA[PixelIndex + 0] = PixelColor.R;
                Tile.RGBA[PixelIndex + 1] = PixelColor.G;
                Tile.RGBA[PixelIndex + 2] = PixelColor.B;
                Tile.RGBA[PixelIndex + 3] = 255;
            }
        }

#if !UE_BUILD_SHIPPING
        if (Tile.CoreInitialMissCount > 0 || Tile.CoreFinalMissCount > 0 || Tile.CoreFallbackSuccessCount > 0 || Tile.CoreFallbackFailureCount > 0 || Tile.CoreExpandedAttemptCount > 0)
        {
            const int64 CorePixelCount = static_cast<int64>(CoreWidth) * static_cast<int64>(CoreHeight);
            const double CoveragePercent = (CorePixelCount > 0)
                ? (static_cast<double>(Tile.CoreFinalHitCount) / static_cast<double>(CorePixelCount)) * 100.0
                : 0.0;
            const int32 ExpandedFailures = FMath::Max(0, Tile.CoreExpandedAttemptCount - Tile.CoreExpandedSuccessCount);

        UE_LOG(LogPlanetaryCreation, Log,
            TEXT("[HeightmapExport][TileCoverage] Core=(%d,%d)->(%d,%d) Size=%dx%d InitialMiss=%d FinalHit=%d FinalMiss=%d FallbackSuccess=%d FallbackFail=%d ExpandedAttempt=%d ExpandedSuccess=%d ExpandedFail=%d Coverage=%.2f%%"),
            Tile.CoreStartX,
            Tile.CoreStartY,
            Tile.CoreEndX,
                Tile.CoreEndY,
                CoreWidth,
                CoreHeight,
                Tile.CoreInitialMissCount,
                Tile.CoreFinalHitCount,
                Tile.CoreFinalMissCount,
                Tile.CoreFallbackSuccessCount,
                Tile.CoreFallbackFailureCount,
                Tile.CoreExpandedAttemptCount,
            Tile.CoreExpandedSuccessCount,
            ExpandedFailures,
            CoveragePercent);
    }

    EvaluateTileSeams(Tile, TEXT("PreFix"), false, bHasLeftOverlap, bHasRightOverlap, nullptr, false);
#endif

        Tile.ProcessingMs = (FPlatformTime::Seconds() - TileStartSeconds) * 1000.0;

        Tile.SampleStartX = SampleStartX;
        Tile.SampleStartY = SampleStartY;
        Tile.SampleEndX = SampleEndX;
        Tile.SampleEndY = SampleEndY;
        Tile.CoreStartX = CoreStartX;
        Tile.CoreStartY = CoreStartY;
        Tile.CoreEndX = CoreEndX;
        Tile.CoreEndY = CoreEndY;
        Tile.SampleWidth = FMath::Max(0, SampleEndX - SampleStartX);
        Tile.SampleHeight = FMath::Max(0, SampleEndY - SampleStartY);

        return Tile;
    }

    static void StitchTile(const FHeightmapTileBuffer& Tile, TArray<uint8>& Destination, int32 DestWidth, int32 DestHeight)
    {
        const int32 CoreWidth = Tile.CoreEndX - Tile.CoreStartX;
        const int32 CoreHeight = Tile.CoreEndY - Tile.CoreStartY;
        if (CoreWidth <= 0 || CoreHeight <= 0)
        {
            return;
        }

        const int32 CopyWidth = FMath::Min(CoreWidth, FMath::Max(0, DestWidth - Tile.CoreStartX));
        const int32 CopyHeight = FMath::Min(CoreHeight, FMath::Max(0, DestHeight - Tile.CoreStartY));

        if (CopyWidth <= 0 || CopyHeight <= 0 || Tile.RGBA.Num() < (Tile.SampleWidth * Tile.SampleHeight * 4))
        {
            return;
        }

        const int32 DestStridePixels = DestWidth;
        const int32 CoreOffsetX = Tile.CoreStartX - Tile.SampleStartX;
        const int32 CoreOffsetY = Tile.CoreStartY - Tile.SampleStartY;

        for (int32 LocalY = 0; LocalY < CopyHeight; ++LocalY)
        {
            const int32 DestRow = Tile.CoreStartY + LocalY;
            if (DestRow < 0 || DestRow >= DestHeight)
            {
                continue;
            }

            const int32 SampleRow = CoreOffsetY + LocalY;
            if (SampleRow < 0 || SampleRow >= Tile.SampleHeight)
            {
                continue;
            }

            const int32 SrcOffset = (SampleRow * Tile.SampleWidth + CoreOffsetX) * 4;
            const int32 DestOffset = (DestRow * DestStridePixels + Tile.CoreStartX) * 4;

            FMemory::Memcpy(
                Destination.GetData() + DestOffset,
                Tile.RGBA.GetData() + SrcOffset,
                static_cast<SIZE_T>(CopyWidth) * 4);
        }

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

    const bool bIsCommandletRun = IsRunningCommandlet();
    FString PreviousUsdPluginPath;
    FString PreviousPxrPluginPath;
    bool bUsdPluginPathModified = false;
    bool bPxrPluginPathModified = false;

#if !NO_LOGGING
    if (bIsCommandletRun)
    {
        const bool bUsdLogOverrideApplied = IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Log LogUsd Warning"), *GLog, nullptr);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[HeightmapExport] USD log override applied=%s"), bUsdLogOverrideApplied ? TEXT("true") : TEXT("false"));
    }
#endif

    if (bIsCommandletRun)
    {
        PreviousUsdPluginPath = FPlatformMisc::GetEnvironmentVariable(TEXT("USD_PLUGINPATH_NAME"));
        PreviousPxrPluginPath = FPlatformMisc::GetEnvironmentVariable(TEXT("PXR_PLUGINPATH_NAME"));
        FPlatformMisc::SetEnvironmentVar(TEXT("USD_PLUGINPATH_NAME"), TEXT(""));
        FPlatformMisc::SetEnvironmentVar(TEXT("PXR_PLUGINPATH_NAME"), TEXT(""));
        bUsdPluginPathModified = true;
        bPxrPluginPathModified = true;
    }

    const bool bShouldForceAutomationTesting = bIsCommandletRun && !GIsAutomationTesting;
    if (bShouldForceAutomationTesting)
    {
        GIsAutomationTesting = true;
    }
    ON_SCOPE_EXIT
    {
        if (bShouldForceAutomationTesting)
        {
            GIsAutomationTesting = false;
        }
        if (bUsdPluginPathModified)
        {
            FPlatformMisc::SetEnvironmentVar(TEXT("USD_PLUGINPATH_NAME"), *PreviousUsdPluginPath);
        }
        if (bPxrPluginPathModified)
        {
            FPlatformMisc::SetEnvironmentVar(TEXT("PXR_PLUGINPATH_NAME"), *PreviousPxrPluginPath);
        }
    };

    const bool bNullRHIActive = (GDynamicRHI == nullptr) || (FCString::Stristr(GDynamicRHI->GetName(), TEXT("Null")) != nullptr);
    const int64 PixelCount64 = static_cast<int64>(ImageWidth) * static_cast<int64>(ImageHeight);
    constexpr int32 SafeBaselineWidth = 512;
    constexpr int32 SafeBaselineHeight = 256;
    constexpr int64 SafeBaselinePixels = static_cast<int64>(SafeBaselineWidth) * SafeBaselineHeight;

    const bool bConsoleUnsafeOverride = (CVarAllowUnsafeHeightmapExport.GetValueOnAnyThread() != 0);
    const bool bUnsafeOverride = bAllowUnsafeHeightmapExport || bConsoleUnsafeOverride;
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
        if (!bUnsafeOverride)
        {
            ensureMsgf(FCString::Stristr(RHIName, TEXT("Null")) != nullptr, TEXT("Heightmap export expected NullRHI; got %s"), RHIName);
        }
    }
#endif

	// Decide which elevation source to use
	// Prefer amplified (Stage B with transform faults), fallback to base elevation (erosion system)
    const bool bInitialSkipCPUAmplification = IsSkippingCPUAmplification();
    bool bModifiedSkipForRescue = false;
    ON_SCOPE_EXIT
    {
        if (bModifiedSkipForRescue && IsSkippingCPUAmplification() != bInitialSkipCPUAmplification)
        {
            SetSkipCPUAmplification(bInitialSkipCPUAmplification);
            UE_LOG(LogPlanetaryCreation, Log,
                TEXT("[HeightmapExport][StageBRescue] Restored SkipCPU flag to %s (post-export)."),
                bInitialSkipCPUAmplification ? TEXT("true") : TEXT("false"));
        }
    };

    bool bStageBReady = IsStageBAmplificationReady();
    EStageBAmplificationReadyReason ReadyReason = GetStageBAmplificationNotReadyReason();
    const bool bEnableStageBRescue = true;
    const bool bStageBReadyAtStart = bStageBReady;
    const EStageBAmplificationReadyReason ReadyReasonAtStart = ReadyReason;
    bool bStageBRescueAttempted = false;
    bool bStageBRescueSucceeded = false;

    auto AttemptStageBRescue = [this, &ReadyReason, &bModifiedSkipForRescue, &bStageBRescueAttempted, &bStageBRescueSucceeded]() -> bool
    {
        bStageBRescueAttempted = true;
        const bool bOriginalSkip = IsSkippingCPUAmplification();
        const TCHAR* ReasonLabel = PlanetaryCreation::StageB::GetReadyReasonLabel(ReadyReason);
        const TCHAR* ReasonDesc = PlanetaryCreation::StageB::GetReadyReasonDescription(ReadyReason);

        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("[HeightmapExport][StageBRescue] Stage B not ready (Reason=%s: %s). Attempting rescue..."),
            ReasonLabel,
            ReasonDesc);

        if (bOriginalSkip)
        {
            SetSkipCPUAmplification(false);
            UE_LOG(LogPlanetaryCreation, Log,
                TEXT("[HeightmapExport][StageBRescue] Cleared SkipCPU flag (was=true)."));
            bModifiedSkipForRescue = true;
        }

        ForceStageBAmplificationRebuild(TEXT("HeightmapExport.StageBRescue"));

        bool bRescueSucceeded = IsStageBAmplificationReady();
        if (bRescueSucceeded)
        {
            bStageBRescueSucceeded = true;
            UE_LOG(LogPlanetaryCreation, Log,
                TEXT("[HeightmapExport][StageBRescue] Stage B ready after rescue (Serial=%llu)."),
                GetOceanicAmplificationDataSerial());
        }
        else
        {
            ReadyReason = GetStageBAmplificationNotReadyReason();
            UE_LOG(LogPlanetaryCreation, Warning,
                TEXT("[HeightmapExport][StageBRescue] Rescue failed (Reason=%s: %s)."),
                PlanetaryCreation::StageB::GetReadyReasonLabel(ReadyReason),
                PlanetaryCreation::StageB::GetReadyReasonDescription(ReadyReason));
        }

        return bRescueSucceeded;
    };

    if (!bStageBReady && bEnableStageBRescue)
    {
        if (AttemptStageBRescue())
        {
            bStageBReady = true;
            ReadyReason = GetStageBAmplificationNotReadyReason();
        }
    }

    const bool bAmplifiedArrayValid = (VertexAmplifiedElevation.Num() == RenderVertices.Num());
    bool bAmplifiedAvailable = bStageBReady && bAmplifiedArrayValid;
    if (bStageBReady && !bAmplifiedArrayValid)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("Stage B amplification reports ready but amplified elevation array is invalid (vertices=%d, amplified=%d)."),
            RenderVertices.Num(), VertexAmplifiedElevation.Num());
    }
    else if (!bStageBReady)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("[StageB][Ready] Heightmap export using baseline elevations because Stage B is not ready (%s: %s)."),
            PlanetaryCreation::StageB::GetReadyReasonLabel(ReadyReason),
            PlanetaryCreation::StageB::GetReadyReasonDescription(ReadyReason));
    }

	const TArray<double>& ElevationSource = bAmplifiedAvailable ? VertexAmplifiedElevation : VertexElevationValues;

	if (ElevationSource.Num() != RenderVertices.Num())
	{
		UE_LOG(LogPlanetaryCreation, Error, TEXT("Cannot export heightmap: Elevation data mismatch (vertices=%d, elevations=%d)"),
			RenderVertices.Num(), ElevationSource.Num());
		return FString();
	}

    if (bAmplifiedAvailable)
    {
        RefreshOceanicAmplificationFloatInputs();
    }

    const uint64 StageBSerialAtSnapshot = bAmplifiedAvailable ? GetOceanicAmplificationDataSerial() : 0;

#if !UE_BUILD_SHIPPING
    UE_LOG(LogPlanetaryCreation, Log,
        TEXT("[HeightmapExport][TileConfig] TileSize=%d Overlap=%d UsingAmplified=%s StageBSerial=%llu"),
        HeightmapTileSizePixels,
        HeightmapTileOverlapPixels,
        bAmplifiedAvailable ? TEXT("true") : TEXT("false"),
        StageBSerialAtSnapshot);
#endif

    const auto ValidateStageBSerial = [this, StageBSerialAtSnapshot, bAmplifiedAvailable](const TCHAR* Context) -> bool
    {
        if (!bAmplifiedAvailable)
        {
            return true;
        }

        const uint64 CurrentSerial = GetOceanicAmplificationDataSerial();
        if (CurrentSerial != StageBSerialAtSnapshot)
        {
            UE_LOG(LogPlanetaryCreation, Error,
                TEXT("[HeightmapExport][Corruption] Stage B serial changed during export (Context=%s Snapshot=%llu Current=%llu)"),
                Context,
                StageBSerialAtSnapshot,
                CurrentSerial);
            return false;
        }

        return true;
    };

    const double SamplerSetupStartSeconds = FPlatformTime::Seconds();
    FHeightmapSampler Sampler(*this);
    SamplerSetupMs = (FPlatformTime::Seconds() - SamplerSetupStartSeconds) * 1000.0;
    if (!Sampler.IsValid())
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Cannot export heightmap: Sampler initialization failed (vertices=%d, triangles=%d)"),
            RenderVertices.Num(), RenderTriangles.Num() / 3);
        return FString();
    }

    if (bAmplifiedAvailable && !ValidateStageBSerial(TEXT("AfterSamplerInit")))
    {
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

    TArray<uint8> RawData;
    RawData.SetNumUninitialized(static_cast<int64>(PixelCount) * 4);

    TArray<FHeightmapRowSeam> RowSeams;
    RowSeams.SetNumZeroed(ImageHeight);

    TArray<uint32> RowSuccessCounts;
    RowSuccessCounts.SetNumZeroed(ImageHeight);

    TArray<uint64> RowTraversalSums;
    RowTraversalSums.SetNumZeroed(ImageHeight);

    TArray<uint8> RowMaxTraversalSteps;
    RowMaxTraversalSteps.SetNumZeroed(ImageHeight);

    TArray<FHeightmapSeamHintRow> RowSeamHints;
    RowSeamHints.Init(FHeightmapSeamHintRow(), ImageHeight);

    FHeightmapSeamAggregate PostFixAggregate;
    FHeightmapRescueAggregation RescueAggregation;
    TArray<int32> RowLastTriangles;
    RowLastTriangles.Init(INDEX_NONE, ImageHeight);

#if !UE_BUILD_SHIPPING
    {
        const TCHAR* const DefaultAllocatorLabel = TEXT("FDefaultAllocator");

        const int64 RawDataBytes = static_cast<int64>(RawData.GetAllocatedSize());
        LogBufferTelemetry(TEXT("RawData<uint8>"), static_cast<int64>(RawData.Num()), static_cast<int64>(sizeof(uint8)), RawDataBytes, DefaultAllocatorLabel);
        TotalTrackedBufferBytes += RawDataBytes;

        const int64 RowSeamsBytes = static_cast<int64>(RowSeams.GetAllocatedSize());
        LogBufferTelemetry(TEXT("RowSeams<FHeightmapRowSeam>"), static_cast<int64>(RowSeams.Num()), static_cast<int64>(sizeof(FHeightmapRowSeam)), RowSeamsBytes, DefaultAllocatorLabel);
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

        const int32 TileWidth = HeightmapTileSizePixels;
        const int32 TileHeight = HeightmapTileSizePixels;
        const int32 TilesX = FMath::Max(1, FMath::DivideAndRoundUp(ImageWidth, TileWidth));
        const int32 TilesY = FMath::Max(1, FMath::DivideAndRoundUp(ImageHeight, TileHeight));
        const int32 TotalTiles = TilesX * TilesY;
        int32 TileCounter = 0;

        for (int32 TileY = 0; TileY < TilesY; ++TileY)
        {
            const int32 CoreStartY = TileY * TileHeight;
            const int32 CoreEndY = FMath::Min(CoreStartY + TileHeight, ImageHeight);
            if (CoreStartY >= CoreEndY)
            {
                continue;
            }

            const int32 SampleStartY = FMath::Max(CoreStartY - HeightmapTileOverlapPixels, 0);
            const int32 SampleEndY = FMath::Min(CoreEndY + HeightmapTileOverlapPixels, ImageHeight);

            for (int32 ClearY = CoreStartY; ClearY < CoreEndY; ++ClearY)
            {
                if (RowSeamHints.IsValidIndex(ClearY))
                {
                    RowSeamHints[ClearY] = FHeightmapSeamHintRow();
                }
                if (RowLastTriangles.IsValidIndex(ClearY))
                {
                    RowLastTriangles[ClearY] = INDEX_NONE;
                }
            }

            for (int32 TileX = 0; TileX < TilesX; ++TileX)
            {
                const int32 CoreStartX = TileX * TileWidth;
                const int32 CoreEndX = FMath::Min(CoreStartX + TileWidth, ImageWidth);

                if (CoreStartX >= CoreEndX)
                {
                    continue;
                }

                const int32 SampleStartX = FMath::Max(CoreStartX - HeightmapTileOverlapPixels, 0);
                const int32 SampleEndX = FMath::Min(CoreEndX + HeightmapTileOverlapPixels, ImageWidth);

                ++TileCounter;

                FHeightmapTileBuffer TileBuffer = ProcessTile(
                    Sampler,
                    Palette,
                    ImageWidth,
                    ImageHeight,
                    InvWidth,
                    InvHeight,
                    SampleStartX,
                    SampleStartY,
                    SampleEndX,
                    SampleEndY,
                    CoreStartX,
                    CoreStartY,
                    CoreEndX,
                    CoreEndY,
                    RowSeamHints,
                    RowSeams,
                    RowSuccessCounts,
                    RowTraversalSums,
                    RowMaxTraversalSteps,
                    RescueAggregation,
                    RowLastTriangles);

                if (TileX > 0)
                {
                    UE_LOG(LogPlanetaryCreation, Verbose,
                        TEXT("[HeightmapExport][SeamFix] Evaluating seam for TileX=%d TileY=%d"),
                        TileX,
                        TileY);
                    const int32 CoreHeight = TileBuffer.CoreEndY - TileBuffer.CoreStartY;
                    const int32 CoreOffsetX = TileBuffer.CoreStartX - TileBuffer.SampleStartX;
                    const int32 CoreOffsetY = TileBuffer.CoreStartY - TileBuffer.SampleStartY;

                    for (int32 RowIndex = 0; RowIndex < CoreHeight; ++RowIndex)
                    {
                        const int32 GlobalY = TileBuffer.CoreStartY + RowIndex;
                        if (!RowSeamHints.IsValidIndex(GlobalY))
                        {
                            continue;
                        }

                        FHeightmapSeamHintRow& LeftHint = RowSeamHints[GlobalY];
                        if (LeftHint.Triangle == INDEX_NONE || LeftHint.bHit == 0)
                        {
                        UE_LOG(LogPlanetaryCreation, Verbose,
                                TEXT("[HeightmapExport][SeamFixSkip] Missing left hint SeamX=%d Row=%d Triangle=%d Hit=%d"),
                                TileBuffer.CoreStartX,
                                GlobalY,
                                LeftHint.Triangle,
                                LeftHint.bHit);
                            continue;
                        }

                        if (!TileBuffer.LeftCoreHits.IsValidIndex(RowIndex) || TileBuffer.LeftCoreHits[RowIndex] == 0)
                        {
                        UE_LOG(LogPlanetaryCreation, Verbose,
                                TEXT("[HeightmapExport][SeamFixSkip] Left core miss SeamX=%d Row=%d"),
                                TileBuffer.CoreStartX,
                                GlobalY);
                            continue;
                        }

                        const double LeftElevation = static_cast<double>(LeftHint.Elevation);
                        const double RightElevation = static_cast<double>(TileBuffer.LeftCoreElevations.IsValidIndex(RowIndex) ? TileBuffer.LeftCoreElevations[RowIndex] : 0.0f);
                        const double DeltaBefore = FMath::Abs(LeftElevation - RightElevation);

                        const bool bLeftNeedsRetry =
                            (LeftHint.FallbackMode == static_cast<uint8>(EHeightmapFallbackMode::Expanded)) ||
                            (LeftHint.FallbackMode == static_cast<uint8>(EHeightmapFallbackMode::Hint));
                        const bool bRightNeedsRetry =
                            (TileBuffer.LeftCoreFallbackModes.IsValidIndex(RowIndex) &&
                                (TileBuffer.LeftCoreFallbackModes[RowIndex] == static_cast<uint8>(EHeightmapFallbackMode::Expanded) ||
                                 TileBuffer.LeftCoreFallbackModes[RowIndex] == static_cast<uint8>(EHeightmapFallbackMode::Hint)));

                        UE_LOG(LogPlanetaryCreation, Verbose,
                            TEXT("[HeightmapExport][SeamFixCandidate] SeamX=%d Row=%d DeltaBefore=%.3f LeftMode=%d RightMode=%d"),
                            TileBuffer.CoreStartX,
                            GlobalY,
                            DeltaBefore,
                            LeftHint.FallbackMode,
                            TileBuffer.LeftCoreFallbackModes.IsValidIndex(RowIndex) ? TileBuffer.LeftCoreFallbackModes[RowIndex] : static_cast<uint8>(EHeightmapFallbackMode::None));

                        if (!(DeltaBefore > HeightmapSeamToleranceMeters && (bLeftNeedsRetry || bRightNeedsRetry)))
                        {
                            continue;
                        }

                        const int32 LeftTriangle = LeftHint.Triangle;
                        const int32 RightTriangle = TileBuffer.LeftCoreTriangles.IsValidIndex(RowIndex) ? TileBuffer.LeftCoreTriangles[RowIndex] : INDEX_NONE;
                        if (LeftTriangle == INDEX_NONE || RightTriangle == INDEX_NONE)
                        {
                            continue;
                        }

                        const int32 LeftPixelX = LeftHint.PixelX;
                        const int32 RightPixelX = TileBuffer.CoreStartX;

                        if (!ensure(LeftPixelX >= 0 && LeftPixelX < ImageWidth))
                        {
                            continue;
                        }

                        const double SeamV = (static_cast<double>(GlobalY) + 0.5) * InvHeight;
                        const FVector2d LeftUV((static_cast<double>(LeftPixelX) + 0.5) * InvWidth, SeamV);
                        const FVector2d RightUV((static_cast<double>(RightPixelX) + 0.5) * InvWidth, SeamV);

                        FHeightmapSampler::FSampleInfo LeftResampleInfo;
                        FHeightmapSampler::FSampleInfo RightResampleInfo;
                        double LeftResampledElevation = 0.0;
                        double RightResampledElevation = 0.0;

                        const bool bLeftResampleSucceeded = Sampler.SampleElevationAtUVWithHint(LeftUV, RightTriangle, &LeftResampleInfo, LeftResampledElevation);
                        const bool bLeftResampleValid = bLeftResampleSucceeded && LeftResampleInfo.bHit;
                        if (!bLeftResampleValid)
                        {
                            LeftResampledElevation = LeftElevation;
                        }

                        const bool bRightResampleSucceeded = Sampler.SampleElevationAtUVWithHint(RightUV, LeftTriangle, &RightResampleInfo, RightResampledElevation);
                        if (!bRightResampleSucceeded || !RightResampleInfo.bHit)
                        {
                            RightResampledElevation = LeftResampledElevation;
                            if (bLeftResampleValid)
                            {
                                RightResampleInfo.TriangleIndex = LeftResampleInfo.TriangleIndex;
                                RightResampleInfo.Steps = LeftResampleInfo.Steps;
                            }
                            else
                            {
                                RightResampleInfo.TriangleIndex = LeftTriangle;
                                RightResampleInfo.Steps = 0;
                            }
                        }

                        double DeltaAfter = FMath::Abs(LeftResampledElevation - RightResampledElevation);
                        if (DeltaAfter > HeightmapSeamToleranceMeters)
                        {
                            RightResampledElevation = LeftResampledElevation;
                            DeltaAfter = 0.0;
                            if (bLeftResampleValid)
                            {
                                RightResampleInfo.TriangleIndex = LeftResampleInfo.TriangleIndex;
                                RightResampleInfo.Steps = LeftResampleInfo.Steps;
                            }
                            else
                            {
                                RightResampleInfo.TriangleIndex = LeftTriangle;
                                RightResampleInfo.Steps = 0;
                            }
                        }

                        const auto WriteRawPixel = [&](int32 PixelX, int32 PixelY, double ElevationMeters)
                        {
                            const FColor PixelColor = Palette.Sample(ElevationMeters);
                            const int64 PixelIndex = (static_cast<int64>(PixelY) * static_cast<int64>(ImageWidth) + static_cast<int64>(PixelX)) * 4;
                            if (RawData.IsValidIndex(PixelIndex + 3))
                            {
                                RawData[PixelIndex + 0] = PixelColor.R;
                                RawData[PixelIndex + 1] = PixelColor.G;
                                RawData[PixelIndex + 2] = PixelColor.B;
                                RawData[PixelIndex + 3] = 255;
                            }
                        };

                        if (bLeftResampleValid)
                        {
                            WriteRawPixel(LeftPixelX, GlobalY, LeftResampledElevation);
                        }

                        const int32 LocalRow = CoreOffsetY + RowIndex;
                        const int32 LocalCol = CoreOffsetX;
                        const int64 LocalPixelIndex = (static_cast<int64>(LocalRow) * static_cast<int64>(TileBuffer.SampleWidth) + static_cast<int64>(LocalCol)) * 4;
                        if (TileBuffer.RGBA.IsValidIndex(LocalPixelIndex + 3))
                        {
                            const FColor RightColor = Palette.Sample(RightResampledElevation);
                            TileBuffer.RGBA[LocalPixelIndex + 0] = RightColor.R;
                            TileBuffer.RGBA[LocalPixelIndex + 1] = RightColor.G;
                            TileBuffer.RGBA[LocalPixelIndex + 2] = RightColor.B;
                            TileBuffer.RGBA[LocalPixelIndex + 3] = 255;
                        }

                        LeftHint.FallbackMode = static_cast<uint8>(EHeightmapFallbackMode::Hint);
                        if (bLeftResampleValid)
                        {
                            LeftHint.Elevation = static_cast<float>(LeftResampledElevation);
                            LeftHint.Triangle = LeftResampleInfo.TriangleIndex;
                            LeftHint.bHit = 1;
                            LeftHint.Steps = static_cast<uint8>(FMath::Clamp(LeftResampleInfo.Steps, 0, 255));
                        }

                        if (TileBuffer.LeftOverlapElevations.IsValidIndex(RowIndex))
                        {
                            TileBuffer.LeftOverlapElevations[RowIndex] = LeftHint.Elevation;
                        }
                        if (TileBuffer.LeftOverlapHits.IsValidIndex(RowIndex))
                        {
                            TileBuffer.LeftOverlapHits[RowIndex] = LeftHint.bHit;
                        }
                        if (TileBuffer.LeftOverlapTriangles.IsValidIndex(RowIndex))
                        {
                            TileBuffer.LeftOverlapTriangles[RowIndex] = LeftHint.Triangle;
                        }
                        if (TileBuffer.LeftOverlapSteps.IsValidIndex(RowIndex))
                        {
                            TileBuffer.LeftOverlapSteps[RowIndex] = LeftHint.Steps;
                        }
                        if (TileBuffer.LeftOverlapUsedFallback.IsValidIndex(RowIndex))
                        {
                            TileBuffer.LeftOverlapUsedFallback[RowIndex] = 1;
                        }
                        if (TileBuffer.LeftOverlapUsedExpanded.IsValidIndex(RowIndex))
                        {
                            TileBuffer.LeftOverlapUsedExpanded[RowIndex] = 0;
                        }
                        if (TileBuffer.LeftOverlapFallbackModes.IsValidIndex(RowIndex))
                        {
                            TileBuffer.LeftOverlapFallbackModes[RowIndex] = LeftHint.FallbackMode;
                        }

                        if (TileBuffer.LeftCoreElevations.IsValidIndex(RowIndex))
                        {
                            TileBuffer.LeftCoreElevations[RowIndex] = static_cast<float>(RightResampledElevation);
                        }
                        if (TileBuffer.LeftCoreHits.IsValidIndex(RowIndex))
                        {
                            TileBuffer.LeftCoreHits[RowIndex] = 1;
                        }
                        if (TileBuffer.LeftCoreTriangles.IsValidIndex(RowIndex))
                        {
                            TileBuffer.LeftCoreTriangles[RowIndex] = RightResampleInfo.TriangleIndex;
                        }
                        if (TileBuffer.LeftCoreSteps.IsValidIndex(RowIndex))
                        {
                            TileBuffer.LeftCoreSteps[RowIndex] = static_cast<uint8>(FMath::Clamp(RightResampleInfo.Steps, 0, 255));
                        }
                        if (TileBuffer.LeftCoreUsedFallback.IsValidIndex(RowIndex))
                        {
                            TileBuffer.LeftCoreUsedFallback[RowIndex] = 1;
                        }
                        if (TileBuffer.LeftCoreUsedExpanded.IsValidIndex(RowIndex))
                        {
                            TileBuffer.LeftCoreUsedExpanded[RowIndex] = 0;
                        }
                        if (TileBuffer.LeftCoreFallbackModes.IsValidIndex(RowIndex))
                        {
                            TileBuffer.LeftCoreFallbackModes[RowIndex] = static_cast<uint8>(EHeightmapFallbackMode::Hint);
                        }

                        const int32 LeftTriForLog = bLeftResampleValid ? LeftResampleInfo.TriangleIndex : LeftHint.Triangle;

                        UE_LOG(LogPlanetaryCreation, Log,
                            TEXT("[HeightmapExport][SeamFix] SeamX=%d Row=%d DeltaBefore=%.3f DeltaAfter=%.3f LeftTri=%d RightTri=%d"),
                            TileBuffer.CoreStartX,
                            GlobalY,
                            DeltaBefore,
                            DeltaAfter,
                            LeftTriForLog,
                            RightResampleInfo.TriangleIndex);
                    }
                }

#if !UE_BUILD_SHIPPING
                const bool bPostLeft = (TileX > 0);
                const bool bPostRight = (TileX == TilesX - 1);
                if (bPostLeft || bPostRight)
                {
                    EvaluateTileSeams(TileBuffer, TEXT("PostFix"), true, bPostLeft, bPostRight, &PostFixAggregate, true);
                }
#endif

#if !UE_BUILD_SHIPPING
                TotalTrackedBufferBytes += static_cast<int64>(TileBuffer.RGBA.GetAllocatedSize());
#endif

                const double StitchStart = FPlatformTime::Seconds();
                StitchTile(TileBuffer, RawData, ImageWidth, ImageHeight);
                const double StitchMs = (FPlatformTime::Seconds() - StitchStart) * 1000.0;

                UE_LOG(LogPlanetaryCreation, Log,
                    TEXT("[HeightmapExport][TileProgress] Tile %d/%d Core=(%d,%d)-(%d,%d) Sample=(%d,%d)-(%d,%d)"),
                    TileCounter,
                    TotalTiles,
                    CoreStartX,
                    CoreStartY,
                    CoreEndX,
                    CoreEndY,
                    SampleStartX,
                    SampleStartY,
                    SampleEndX,
                    SampleEndY);

                UE_LOG(LogPlanetaryCreation, Log,
                    TEXT("[HeightmapExport][TileStats] Tile %d/%d SampleMs=%.2f CopyMs=%.2f CoreSize=%dx%d SampleSize=%dx%d"),
                    TileCounter,
                    TotalTiles,
                    TileBuffer.ProcessingMs,
                    StitchMs,
                    TileBuffer.CoreEndX - TileBuffer.CoreStartX,
                    TileBuffer.CoreEndY - TileBuffer.CoreStartY,
                    TileBuffer.SampleWidth,
                    TileBuffer.SampleHeight);

                if (bAmplifiedAvailable)
                {
                    const FString Context = FString::Printf(TEXT("Tile %d/%d"), TileCounter, TotalTiles);
                    if (!ValidateStageBSerial(*Context))
                    {
                        return FString();
                    }
                }

                const int32 TileCoreHeight = TileBuffer.CoreEndY - TileBuffer.CoreStartY;
                if (TileCoreHeight > 0)
                {
                    for (int32 RowIndex = 0; RowIndex < TileCoreHeight; ++RowIndex)
                    {
                        const int32 GlobalY = TileBuffer.CoreStartY + RowIndex;
                        if (!RowSeamHints.IsValidIndex(GlobalY))
                        {
                            continue;
                        }

                        FHeightmapSeamHintRow& Hint = RowSeamHints[GlobalY];
                        Hint.PixelX = TileBuffer.CoreEndX - 1;
                        Hint.Elevation = TileBuffer.RightCoreElevations.IsValidIndex(RowIndex) ? TileBuffer.RightCoreElevations[RowIndex] : 0.0f;
                        Hint.Triangle = TileBuffer.RightCoreTriangles.IsValidIndex(RowIndex) ? TileBuffer.RightCoreTriangles[RowIndex] : INDEX_NONE;
                        Hint.bHit = TileBuffer.RightCoreHits.IsValidIndex(RowIndex) ? TileBuffer.RightCoreHits[RowIndex] : 0;
                        Hint.FallbackMode = TileBuffer.RightCoreFallbackModes.IsValidIndex(RowIndex) ? TileBuffer.RightCoreFallbackModes[RowIndex] : static_cast<uint8>(EHeightmapFallbackMode::None);
                        Hint.Steps = TileBuffer.RightCoreSteps.IsValidIndex(RowIndex) ? TileBuffer.RightCoreSteps[RowIndex] : 0;
                    }
                }
            }
        }
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

    SamplingMs = (FPlatformTime::Seconds() - SamplingStartSeconds) * 1000.0;

    if (bAmplifiedAvailable && !ValidateStageBSerial(TEXT("PostSampling")))
    {
        return FString();
    }

    if (PostFixAggregate.Samples > 0)
    {
        Metrics.SeamRowsEvaluated = PostFixAggregate.Samples;
        Metrics.SeamRowsAboveHalfMeter = PostFixAggregate.RowsAboveHalfMeter;
        Metrics.SeamRowsWithFailures = PostFixAggregate.RowsWithFailures;
        Metrics.SeamMeanAbsDelta = PostFixAggregate.AbsSum / static_cast<double>(PostFixAggregate.Samples);
        Metrics.SeamRmsDelta = FMath::Sqrt(PostFixAggregate.RmsSum / static_cast<double>(PostFixAggregate.Samples));
        Metrics.SeamMaxAbsDelta = PostFixAggregate.MaxAbs;
    }
    else
    {
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
                const FHeightmapRowSeam& Seam = RowSeams[Y];
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

        Metrics.SeamRowsEvaluated = SeamSamples;
        Metrics.SeamRowsAboveHalfMeter = RowsAboveHalfMeter;
        Metrics.SeamRowsWithFailures = SeamRowsWithFailures;
        Metrics.SeamMeanAbsDelta = SeamSamples > 0 ? SeamAbsAccum / static_cast<double>(SeamSamples) : 0.0;
        Metrics.SeamRmsDelta = SeamSamples > 0 ? FMath::Sqrt(SeamRmsAccum / static_cast<double>(SeamSamples)) : 0.0;
        Metrics.SeamMaxAbsDelta = SeamMaxAbsDelta;
    }

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
    TArray64<uint8> CompressedData = *CompressedDataPtr;

    // Fix: Ensure PNG signature is present (89 50 4E 47 0D 0A 1A 0A)
    // Some Unreal Engine versions return compressed data without the signature
    constexpr uint8 PNGSignature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    const bool bHasPNGSignature = CompressedData.Num() >= 8 &&
        CompressedData[0] == PNGSignature[0] &&
        CompressedData[1] == PNGSignature[1] &&
        CompressedData[2] == PNGSignature[2] &&
        CompressedData[3] == PNGSignature[3];

    if (!bHasPNGSignature && CompressedData.Num() > 0)
    {
        // Find the start of actual PNG data
        // The PNG signature (8 bytes) may be replaced with NULL bytes by some ImageWrapper implementations
        // We need to skip exactly 8 NULL bytes if present, then prepend the correct signature
        int64 DataStart = 0;

        // Check if first 8 bytes are all NULL (signature replacement)
        bool bHasNullSignature = CompressedData.Num() >= 8 &&
            CompressedData[0] == 0 && CompressedData[1] == 0 &&
            CompressedData[2] == 0 && CompressedData[3] == 0 &&
            CompressedData[4] == 0 && CompressedData[5] == 0 &&
            CompressedData[6] == 0 && CompressedData[7] == 0;

        if (bHasNullSignature)
        {
            DataStart = 8;  // Skip exactly 8 NULL bytes (the corrupted signature)
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[HeightmapExport][PNGSignature] Missing PNG signature (replaced with NULL bytes), prepending correct header"));
        }
        else
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[HeightmapExport][PNGSignature] Missing PNG signature (non-NULL data at start), prepending header"));
        }

        TArray64<uint8> FixedData;
        FixedData.Reserve(CompressedData.Num() - DataStart + 8);
        FixedData.Append(PNGSignature, 8);
        FixedData.Append(CompressedData.GetData() + DataStart, CompressedData.Num() - DataStart);
        CompressedData = MoveTemp(FixedData);
    }

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

    PlanetaryCreation::StageB::FStageBRescueSummary RescueSummary;
    RescueSummary.ImageWidth = ImageWidth;
    RescueSummary.ImageHeight = ImageHeight;
    RescueSummary.TotalPixels = static_cast<uint64>(Metrics.PixelCount);
    RescueSummary.FinalHits = static_cast<uint64>(Metrics.SuccessfulSamples);
    RescueSummary.FinalMisses = static_cast<uint64>(Metrics.FailedSamples);
    RescueSummary.FallbackAttempts = RescueAggregation.FailureFallbackAttempts;
    RescueSummary.FallbackSuccesses = RescueAggregation.FailureFallbackSuccesses;
    RescueSummary.FallbackFailures = RescueAggregation.FailureFallbackFailures;
    RescueSummary.ExpandedAttempts = RescueAggregation.ExpandedAttempts;
    RescueSummary.ExpandedSuccesses = RescueAggregation.ExpandedSuccesses;
    RescueSummary.SanitizedFallbacks = RescueAggregation.SanitizedFallbacks;
    RescueSummary.DirectNudgeFallbacks = RescueAggregation.DirectNudgeFallbacks;
    RescueSummary.ExpandedFallbacks = RescueAggregation.ExpandedFallbacks;
    RescueSummary.WrappedFallbacks = RescueAggregation.WrappedFallbacks;
    RescueSummary.HintFallbacks = RescueAggregation.HintFallbacks;
    RescueSummary.RowReuseFallbacks = RescueAggregation.RowReuseFallbacks;
    RescueSummary.bStageBReadyAtStart = bStageBReadyAtStart;
    RescueSummary.bStageBReadyAtFinish = bStageBReady;
    RescueSummary.bUsedAmplifiedData = Metrics.bSamplerUsedAmplified;
    RescueSummary.bRescueAttempted = bStageBRescueAttempted;
    RescueSummary.bRescueSucceeded = bStageBRescueSucceeded;
    RescueSummary.bUsedSnapshotFloatBuffer = Metrics.bUsedSnapshotFloatBuffer;
    RescueSummary.ReadyReasonAtStart = ReadyReasonAtStart;
    RescueSummary.ReadyReasonAtFinish = ReadyReason;

    LogStageBRescueSummary(RescueSummary);

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
