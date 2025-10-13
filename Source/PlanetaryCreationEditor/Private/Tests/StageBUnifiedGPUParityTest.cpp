#include "PlanetaryCreationLogging.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TectonicSimulationService.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Tests/PlanetaryCreationAutomationGPU.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStageBUnifiedGPUParityTest,
    "PlanetaryCreation.StageB.UnifiedGPUParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStageBUnifiedGPUParityTest::RunTest(const FString& Parameters)
{
    using namespace PlanetaryCreation::Automation;
    if (!ShouldRunGPUAmplificationAutomation(*this, TEXT("StageB.UnifiedGPUParity")))
    {
        return true;
    }

    FScopedStageBThrottleGuard ThrottleGuard(*this, 50.0f);
    if (ThrottleGuard.ShouldSkipTest())
    {
        return true;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
    {
        return false;
    }

#if UE_BUILD_DEVELOPMENT
    const int32 OriginalUnifiedDebugVertex = Service->GetStageBUnifiedDebugVertexIndex();
    int32 SelectedDebugVertex = INDEX_NONE;
    double SelectedDebugAge = 0.0;
    double SelectedDebugOriginalAge = 0.0;
    bool bRestoreDebugAge = false;
    ON_SCOPE_EXIT
    {
        Service->SetStageBUnifiedDebugVertexIndex(OriginalUnifiedDebugVertex);
        if (bRestoreDebugAge && SelectedDebugVertex != INDEX_NONE)
        {
            Service->SetVertexCrustAgeForTest(SelectedDebugVertex, SelectedDebugOriginalAge);
        }
    };
#endif

#if WITH_EDITOR
    IConsoleVariable* CVarGPUAmplification = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.UseGPUAmplification"));
    if (!CVarGPUAmplification)
    {
        AddError(TEXT("r.PlanetaryCreation.UseGPUAmplification cvar missing"));
        return false;
    }

    const int32 OriginalGPUValue = CVarGPUAmplification->GetInt();
    const FTectonicSimulationParameters OriginalParams = Service->GetParameters();
    const bool bOriginalSkipCPU = OriginalParams.bSkipCPUAmplification;

    ON_SCOPE_EXIT
    {
        CVarGPUAmplification->Set(OriginalGPUValue, ECVF_SetByCode);
        Service->SetParameters(OriginalParams);
        Service->SetSkipCPUAmplification(bOriginalSkipCPU);
    };

    FTectonicSimulationParameters BaselineParams = OriginalParams;
    BaselineParams.Seed = 24680;
    BaselineParams.SubdivisionLevel = 0;
    BaselineParams.RenderSubdivisionLevel = 6;
    BaselineParams.MinAmplificationLOD = 5;
    BaselineParams.bEnableOceanicAmplification = true;
    BaselineParams.bEnableContinentalAmplification = true;
    BaselineParams.bEnableOceanicDampening = true;
    BaselineParams.bSkipCPUAmplification = false;

    auto RunScenario = [&](bool bUseGPU, TArray<double>& OutElevations) -> bool
    {
        if (bUseGPU)
        {
            Service->ResetAmplifiedElevationForTests();
            Service->ResetContinentalGPUDispatchStats();
            CVarGPUAmplification->Set(1, ECVF_SetByCode);
            Service->SetSkipCPUAmplification(true);

#if UE_BUILD_DEVELOPMENT
            Service->SetForceStageBGPUReplayForTests(true);
            ON_SCOPE_EXIT
            {
                Service->SetForceStageBGPUReplayForTests(false);
            };
#endif

            Service->ForceStageBAmplificationRebuild(TEXT("StageBUnifiedGPUParity.GPU"));
            Service->ProcessPendingOceanicGPUReadbacks(true);
            Service->ProcessPendingContinentalGPUReadbacks(true);

            const FContinentalGPUDispatchStats& Stats = Service->GetContinentalGPUDispatchStats();
            AddInfo(FString::Printf(TEXT("Unified Stage B GPU stats: Attempted=%d Succeeded=%d HashChecks=%d Matches=%d SnapshotMatched=%d"),
                Stats.bDispatchAttempted ? 1 : 0,
                Stats.bDispatchSucceeded ? 1 : 0,
                Stats.HashCheckCount,
                Stats.HashMatchCount,
                Stats.bSnapshotMatched ? 1 : 0));
#if UE_BUILD_DEVELOPMENT
            const FContinentalAmplificationGPUInputs& DebugInputs = Service->GetContinentalAmplificationGPUInputs();
            const int32 WrappedDebugVertex = Service->GetStageBUnifiedDebugVertexIndex();
            if (WrappedDebugVertex != INDEX_NONE && DebugInputs.WrappedUVs.IsValidIndex(WrappedDebugVertex))
            {
                const FVector2f WrappedUV = DebugInputs.WrappedUVs[WrappedDebugVertex];
                AddInfo(FString::Printf(TEXT("UnifiedGPUParity debug wrapped UV: Vertex %d WrappedUV=(%.4f,%.4f)"),
                    WrappedDebugVertex,
                    WrappedUV.X,
                    WrappedUV.Y));
            }
            if (WrappedDebugVertex != INDEX_NONE && DebugInputs.ExemplarWeights.IsValidIndex(WrappedDebugVertex))
            {
                const FVector4f GPUWeights = DebugInputs.ExemplarWeights[WrappedDebugVertex];
                const uint32 PackedInfoValue = DebugInputs.PackedTerrainInfo.IsValidIndex(WrappedDebugVertex)
                    ? DebugInputs.PackedTerrainInfo[WrappedDebugVertex]
                    : 0u;
                AddInfo(FString::Printf(TEXT("UnifiedGPUParity debug weights: Vertex %d Weights=(%.3f,%.3f,%.3f,%.3f) PackedInfo=0x%08x"),
                    WrappedDebugVertex,
                    GPUWeights.X,
                    GPUWeights.Y,
                    GPUWeights.Z,
                    GPUWeights.W,
                    PackedInfoValue));
            }
#endif
            TestTrue(TEXT("Unified Stage B continental dispatch succeeded"), Stats.bDispatchAttempted && Stats.bDispatchSucceeded);
            Service->SetSkipCPUAmplification(false);
        }
        else
        {
            FTectonicSimulationParameters ScenarioParams = BaselineParams;
            ScenarioParams.bSkipCPUAmplification = false;
            Service->SetParameters(ScenarioParams);
            Service->SetSkipCPUAmplification(false);

#if UE_BUILD_DEVELOPMENT
            if (SelectedDebugVertex != INDEX_NONE)
            {
                Service->SetVertexCrustAgeForTest(SelectedDebugVertex, SelectedDebugAge);
            }
#endif

            CVarGPUAmplification->Set(0, ECVF_SetByCode);
            Service->AdvanceSteps(3);
            Service->ResetAmplifiedElevationForTests();

            Service->ForceStageBAmplificationRebuild(TEXT("StageBUnifiedGPUParity.CPU"));
            Service->ProcessPendingOceanicGPUReadbacks(true);
            Service->ProcessPendingContinentalGPUReadbacks(true);
        }

        OutElevations = Service->GetVertexAmplifiedElevation();
        return OutElevations.Num() > 0;
    };

    TArray<double> CpuElevations;
    if (!RunScenario(false, CpuElevations))
    {
        AddError(TEXT("Failed to capture CPU Stage B baseline"));
        return false;
    }

#if UE_BUILD_DEVELOPMENT
    {
        const TArray<FContinentalAmplificationCacheEntry>& CacheEntries = Service->GetContinentalAmplificationCacheEntries();
        const TArray<double>& CrustAges = Service->GetVertexCrustAge();
        const PlanetaryCreation::StageB::FStageB_UnifiedParameters UnifiedParams = Service->GetStageBUnifiedParameters();
        const int32 SharedCount = FMath::Min(CacheEntries.Num(), CrustAges.Num());

        SelectedDebugVertex = INDEX_NONE;
        SelectedDebugAge = 0.0;

        const int32 PreferredDebugVertex = 23949;
        bool bPreferredMeetsThreshold = false;
        if (CacheEntries.IsValidIndex(PreferredDebugVertex))
        {
            const FContinentalAmplificationCacheEntry& PreferredEntry = CacheEntries[PreferredDebugVertex];
            if (PreferredEntry.bHasCachedData && PreferredEntry.ExemplarCount > 0)
            {
                SelectedDebugVertex = PreferredDebugVertex;
                SelectedDebugAge = CrustAges.IsValidIndex(PreferredDebugVertex) ? CrustAges[PreferredDebugVertex] : 0.0;
                bPreferredMeetsThreshold = (SelectedDebugAge >= (UnifiedParams.TransitionAgeMy + 0.5));
            }
        }

        if (!bPreferredMeetsThreshold)
        {
            for (int32 Index = 0; Index < SharedCount; ++Index)
            {
                const FContinentalAmplificationCacheEntry& Entry = CacheEntries[Index];
                if (!Entry.bHasCachedData || Entry.ExemplarCount == 0)
                {
                    continue;
                }

                const double AgeMy = CrustAges.IsValidIndex(Index) ? CrustAges[Index] : 0.0;
                const bool bMeetsAgeThreshold = AgeMy >= (UnifiedParams.TransitionAgeMy + 0.5);
                const bool bFirstCandidate = (SelectedDebugVertex == INDEX_NONE);
                if (bMeetsAgeThreshold || bFirstCandidate)
                {
                    SelectedDebugVertex = Index;
                    SelectedDebugAge = AgeMy;
                    if (bMeetsAgeThreshold)
                    {
                        break;
                    }
                }
            }
        }

        if (SelectedDebugVertex != INDEX_NONE)
        {
            SelectedDebugOriginalAge = SelectedDebugAge;
            const double DesiredAge = FMath::Max(SelectedDebugAge, UnifiedParams.TransitionAgeMy + 1.0);
            if (!FMath::IsNearlyEqual(SelectedDebugAge, DesiredAge))
            {
                Service->SetVertexCrustAgeForTest(SelectedDebugVertex, DesiredAge);
                SelectedDebugAge = DesiredAge;
                bRestoreDebugAge = true;
            }

            Service->SetStageBUnifiedDebugVertexIndex(SelectedDebugVertex);

            const uint32 ExemplarCount = CacheEntries.IsValidIndex(SelectedDebugVertex)
                ? CacheEntries[SelectedDebugVertex].ExemplarCount
                : 0u;
            AddInfo(FString::Printf(
                TEXT("UnifiedGPUParity debug vertex set to %d (Age=%.2f My, ExemplarCount=%u, TransitionAge=%.2f My, AgeAdjusted=%s)"),
                SelectedDebugVertex,
                SelectedDebugAge,
                ExemplarCount,
                UnifiedParams.TransitionAgeMy,
                bRestoreDebugAge ? TEXT("Yes") : TEXT("No")));
        }
        else
        {
            Service->SetStageBUnifiedDebugVertexIndex(INDEX_NONE);
            AddWarning(TEXT("UnifiedGPUParity: No continental vertex with exemplar cache data found; debug instrumentation disabled."));
        }
    }
#endif

#if UE_BUILD_DEVELOPMENT
    if (SelectedDebugVertex != INDEX_NONE)
    {
        if (!RunScenario(false, CpuElevations))
        {
            AddError(TEXT("Failed to recapture CPU Stage B baseline after debug adjustments"));
            return false;
        }
    }
#endif

    TArray<double> GpuElevations;
    if (!RunScenario(true, GpuElevations))
    {
        AddError(TEXT("Failed to capture GPU Stage B output"));
        return false;
    }

    const int32 OceanicDebugVertex = 40283;
    if (CpuElevations.IsValidIndex(OceanicDebugVertex) && GpuElevations.IsValidIndex(OceanicDebugVertex))
    {
        AddInfo(FString::Printf(TEXT("[UnifiedGPUParity][Debug] Vertex %d CPU=%.4f GPU=%.4f"),
            OceanicDebugVertex,
            CpuElevations[OceanicDebugVertex],
            GpuElevations[OceanicDebugVertex]));
    }

#if UE_BUILD_DEVELOPMENT
    if (SelectedDebugVertex != INDEX_NONE &&
        CpuElevations.IsValidIndex(SelectedDebugVertex) &&
        GpuElevations.IsValidIndex(SelectedDebugVertex))
    {
        AddInfo(FString::Printf(TEXT("[UnifiedGPUParity][Debug] Selected continental vertex %d CPU=%.4f GPU=%.4f (Age=%.2f My)"),
            SelectedDebugVertex,
            CpuElevations[SelectedDebugVertex],
            GpuElevations[SelectedDebugVertex],
            SelectedDebugAge));
    }
#endif

    TestEqual(TEXT("Unified GPU parity vertex count matches CPU baseline"), CpuElevations.Num(), GpuElevations.Num());
    const int32 VertexCount = CpuElevations.Num();
    if (VertexCount == 0)
    {
        AddError(TEXT("No amplified vertices available for parity comparison"));
        return false;
    }

    double MaxDelta = 0.0;
    double SumDelta = 0.0;
    int32 MaxIndex = INDEX_NONE;
    double MaxCpuValue = 0.0;
    double MaxGpuValue = 0.0;
    for (int32 Index = 0; Index < VertexCount; ++Index)
    {
        const double Delta = FMath::Abs(GpuElevations[Index] - CpuElevations[Index]);
        if (Delta > MaxDelta)
        {
            MaxDelta = Delta;
            MaxIndex = Index;
            MaxCpuValue = CpuElevations[Index];
            MaxGpuValue = GpuElevations[Index];
        }
        SumDelta += Delta;
    }

    const double MeanDelta = SumDelta / static_cast<double>(VertexCount);
    UE_LOG(LogTemp, Display,
        TEXT("[UnifiedGPUParity] VertexCount=%d | MaxDelta=%.4f m (Index=%d CPU=%.4f GPU=%.4f) | MeanDelta=%.4f m"),
        VertexCount,
        MaxDelta,
        MaxIndex,
        MaxCpuValue,
        MaxGpuValue,
        MeanDelta);

    FString MetricsLine = FString::Printf(
        TEXT("UnifiedGPUParity metrics: VertexCount=%d MaxDelta=%.4f m MeanDelta=%.4f m MaxIndex=%d Cpu=%.4f m Gpu=%.4f m"),
        VertexCount,
        MaxDelta,
        MeanDelta,
        MaxIndex,
        MaxCpuValue,
        MaxGpuValue);
    AddInfo(MetricsLine);

#if UE_BUILD_DEVELOPMENT
    if (MaxIndex != INDEX_NONE)
    {
        const FContinentalAmplificationGPUInputs& DebugInputs = Service->GetContinentalAmplificationGPUInputs();
        const TArray<FContinentalAmplificationCacheEntry>& DebugCacheEntries = Service->GetContinentalAmplificationCacheEntries();
        const FVector4f SampleHeights = DebugInputs.SampleHeights.IsValidIndex(MaxIndex)
            ? DebugInputs.SampleHeights[MaxIndex]
            : FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
        const FVector4f WeightVector = DebugInputs.ExemplarWeights.IsValidIndex(MaxIndex)
            ? DebugInputs.ExemplarWeights[MaxIndex]
            : FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
        const uint32 PackedInfo = DebugInputs.PackedTerrainInfo.IsValidIndex(MaxIndex)
            ? DebugInputs.PackedTerrainInfo[MaxIndex]
            : 0u;
        const bool bCacheHasData = DebugCacheEntries.IsValidIndex(MaxIndex)
            ? DebugCacheEntries[MaxIndex].bHasCachedData
            : false;
        const TArray<double>& AllCrustAges = Service->GetVertexCrustAge();
        const double DebugCrustAge = AllCrustAges.IsValidIndex(MaxIndex) ? AllCrustAges[MaxIndex] : 0.0;
        const TArray<int32>& AllPlateAssignments = Service->GetVertexPlateAssignments();
        const int32 DebugPlateId = AllPlateAssignments.IsValidIndex(MaxIndex)
            ? AllPlateAssignments[MaxIndex]
            : INDEX_NONE;

        AddInfo(FString::Printf(
            TEXT("[UnifiedGPUParity][MaxDelta] Index=%d CacheHasData=%s PackedInfo=0x%08x SampleHeights=(%.3f,%.3f,%.3f,%.3f) Weights=(%.3f,%.3f,%.3f,%.3f) CrustAge=%.3f Plate=%d"),
            MaxIndex,
            bCacheHasData ? TEXT("Yes") : TEXT("No"),
            PackedInfo,
            SampleHeights.X,
            SampleHeights.Y,
            SampleHeights.Z,
            SampleHeights.W,
            WeightVector.X,
            WeightVector.Y,
            WeightVector.Z,
            WeightVector.W,
            DebugCrustAge,
            DebugPlateId));
    }
#endif

    if (UTectonicSimulationService* DebugService = Service)
    {
        const TArray<int32>& PlateAssignments = DebugService->GetVertexPlateAssignments();
        const TArray<double>& BaselineElevation = DebugService->GetVertexElevationValues();
        const int32 PlateId = PlateAssignments.IsValidIndex(MaxIndex) ? PlateAssignments[MaxIndex] : INDEX_NONE;
        const double BaselineValue = BaselineElevation.IsValidIndex(MaxIndex) ? BaselineElevation[MaxIndex] : 0.0;
        const TArray<FContinentalAmplificationCacheEntry>& CacheEntries = DebugService->GetContinentalAmplificationCacheEntries();
        const bool bHasCacheEntry = CacheEntries.IsValidIndex(MaxIndex);
        const FContinentalAmplificationCacheEntry CacheEntry = bHasCacheEntry ? CacheEntries[MaxIndex] : FContinentalAmplificationCacheEntry();

        UE_LOG(LogTemp, Display,
            TEXT("[UnifiedGPUParity] MaxIndex %d | Plate=%d | Baseline=%.4f m | CacheHasData=%d Count=%d Terrain=%d"),
            MaxIndex,
            PlateId,
            BaselineValue,
            bHasCacheEntry && CacheEntry.bHasCachedData ? 1 : 0,
            bHasCacheEntry ? CacheEntry.ExemplarCount : 0,
            bHasCacheEntry ? static_cast<int32>(CacheEntry.TerrainType) : -1);
    }

    const FString MetricsFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("UnifiedGPUParityMetrics.txt"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(MetricsFile), true);
    AddInfo(FString::Printf(TEXT("UnifiedGPUParity metrics path: %s"), *MetricsFile));
    const bool bMetricsWritten = FFileHelper::SaveStringToFile(MetricsLine + LINE_TERMINATOR, *MetricsFile, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
    TestTrue(TEXT("UnifiedGPUParity metrics file write succeeded"), bMetricsWritten);

    TestTrue(TEXT("Unified Stage B GPU parity within 0.1 m"), MaxDelta <= 0.1);
    return true;
#else
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[UnifiedGPUParity] Skipped (WITH_EDITOR not defined)"));
    return true;
#endif
}
