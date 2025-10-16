#include "OceanicAmplificationGPU.h"

#include "Algo/Sort.h"
#include "PlanetaryCreationLogging.h"
#include "TectonicSimulationService.h"
#include "ExemplarTextureArray.h"
#include "Engine/Texture2DArray.h"

#include "HAL/PlatformTime.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RenderGraphUtils.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIShaderPlatform.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "ShaderParameterStruct.h"
#include "ShaderParameterUtils.h"
#include "VectorTypes.h"
#include "RHIGPUReadback.h"
#include "Misc/Crc.h"
#include "Templates/SharedPointer.h"

double ComputeOceanicAmplification(
    const FVector3d& Position,
    int32 PlateID,
    double CrustAge_My,
    double BaseElevation_m,
    const FVector3d& RidgeDirection,
    const TArray<FTectonicPlate>& Plates,
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries,
    const FTectonicSimulationParameters& Parameters);

namespace PlanetaryCreation::GPU
{
    namespace
    {
        constexpr uint32 GStageBThreadsPerGroup = 64u;
        static bool GStageBAnisotropyLoggedThisRun = false;

        inline uint32 HashSnapshotMemory(uint32 ExistingHash, const void* Data, SIZE_T NumBytes)
        {
            if (Data && NumBytes > 0)
            {
                return FCrc::MemCrc32(Data, NumBytes, ExistingHash);
            }
            return ExistingHash;
        }

        uint32 HashOceanicSnapshot(const FOceanicAmplificationSnapshot& Snapshot)
        {
            if (!Snapshot.IsConsistent())
            {
                return 0;
            }

            uint32 Hash = 0;
            Hash = HashSnapshotMemory(Hash, Snapshot.BaselineElevation.GetData(), Snapshot.BaselineElevation.Num() * sizeof(float));
            Hash = HashSnapshotMemory(Hash, Snapshot.RidgeDirections.GetData(), Snapshot.RidgeDirections.Num() * sizeof(FVector4f));
            Hash = HashSnapshotMemory(Hash, Snapshot.CrustAge.GetData(), Snapshot.CrustAge.Num() * sizeof(float));
            Hash = HashSnapshotMemory(Hash, Snapshot.RenderPositions.GetData(), Snapshot.RenderPositions.Num() * sizeof(FVector3f));
            Hash = HashSnapshotMemory(Hash, Snapshot.OceanicMask.GetData(), Snapshot.OceanicMask.Num() * sizeof(uint32));
            Hash = HashSnapshotMemory(Hash, Snapshot.PlateAssignments.GetData(), Snapshot.PlateAssignments.Num() * sizeof(int32));
            Hash = HashSnapshotMemory(Hash, &Snapshot.Parameters, sizeof(FTectonicSimulationParameters));
            Hash = HashSnapshotMemory(Hash, &Snapshot.UnifiedParameters, sizeof(PlanetaryCreation::StageB::FStageB_UnifiedParameters));
            Hash = HashSnapshotMemory(Hash, &Snapshot.VertexCount, sizeof(int32));
            return Hash;
        }

        static bool SupportsGPUAmplification()
        {
            const ERHIFeatureLevel::Type MaxFeatureLevel = FDataDrivenShaderPlatformInfo::GetMaxFeatureLevel(GMaxRHIShaderPlatform);
            return MaxFeatureLevel == ERHIFeatureLevel::SM5 || MaxFeatureLevel == ERHIFeatureLevel::SM6;
        }

        static void ComputeSeamCoverageMetrics(const TArray<FVector3f>& Positions, int32 TextureWidth, int32& OutLeftCoverage, int32& OutRightCoverage, int32& OutMirroredCoverage)
        {
            OutLeftCoverage = 0;
            OutRightCoverage = 0;
            OutMirroredCoverage = 0;

            if (TextureWidth <= 1)
            {
                return;
            }

            const int32 LastColumn = TextureWidth - 1;
            const float LastColumnFloat = static_cast<float>(LastColumn);

            for (const FVector3f& Position : Positions)
            {
                const FVector3f Unit = Position.GetSafeNormal();
                if (Unit.IsNearlyZero())
                {
                    continue;
                }

                const float Longitude = FMath::Atan2(Unit.Y, Unit.X);
                float U = 0.5f + Longitude / (2.0f * PI);
                U = FMath::Fmod(U, 1.0f);
                if (U < 0.0f)
                {
                    U += 1.0f;
                }

                const float ColumnPosition = U * LastColumnFloat;
                const float NormalizedU = LastColumnFloat > 0.0f
                    ? FMath::Clamp(ColumnPosition / LastColumnFloat, 0.0f, 1.0f)
                    : 0.0f;

                constexpr float SeamCoverageThreshold = 0.1f;
                const bool bCountsLeft = NormalizedU <= SeamCoverageThreshold;
                const bool bCountsRight = NormalizedU >= (1.0f - SeamCoverageThreshold);

                if (!bCountsLeft && !bCountsRight)
                {
                    continue;
                }

                if (bCountsLeft && bCountsRight)
                {
                    ++OutMirroredCoverage;
                }

                if (bCountsLeft)
                {
                    ++OutLeftCoverage;
                }

                if (bCountsRight)
                {
                    ++OutRightCoverage;
                }
            }
        }
    } // namespace

    class FStageBUnifiedOceanicCS : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FStageBUnifiedOceanicCS);
        SHADER_USE_PARAMETER_STRUCT(FStageBUnifiedOceanicCS, FGlobalShader);

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(uint32, OceanicWorkCount)
            SHADER_PARAMETER(uint32, OceanicGroupCountX)
            SHADER_PARAMETER(uint32, OceanicGroupCountY)
            SHADER_PARAMETER(float, OceanicRidgeAmplitude)
            SHADER_PARAMETER(float, OceanicFaultFrequency)
            SHADER_PARAMETER(float, OceanicAgeFalloff)
            SHADER_PARAMETER(float, OceanicVarianceScale)
            SHADER_PARAMETER(float, OceanicExtraVarianceAmplitude)
            SHADER_PARAMETER(uint32, bWriteDebug)
            SHADER_PARAMETER(uint32, DebugVertexIndex)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, OceanicWorkIndices)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, OceanicBaseline)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, OceanicRidgeDirection)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, OceanicCrustAge)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector3f>, OceanicRenderPosition)
            SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OceanicDebugOutput)
            SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OceanicOutAmplified)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            const ERHIFeatureLevel::Type MaxFeatureLevel = FDataDrivenShaderPlatformInfo::GetMaxFeatureLevel(Parameters.Platform);
            return MaxFeatureLevel == ERHIFeatureLevel::SM5 || MaxFeatureLevel == ERHIFeatureLevel::SM6;
        }
    };

    IMPLEMENT_GLOBAL_SHADER(FStageBUnifiedOceanicCS, "/Plugin/PlanetaryCreation/Private/StageB_Unified_V2.usf", "OceanicMainCS", SF_Compute);

    class FStageBUnifiedContinentalCS : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FStageBUnifiedContinentalCS);
        SHADER_USE_PARAMETER_STRUCT(FStageBUnifiedContinentalCS, FGlobalShader);

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(uint32, ContinentalWorkCount)
            SHADER_PARAMETER(uint32, ContinentalGroupCountX)
            SHADER_PARAMETER(uint32, ContinentalGroupCountY)
            SHADER_PARAMETER(float, ContinentalRidgeAmplitude)
            SHADER_PARAMETER(float, ContinentalFaultFrequency)
            SHADER_PARAMETER(float, ContinentalAgeFalloff)
            SHADER_PARAMETER(float, ContinentalVarianceScale)
            SHADER_PARAMETER(float, ContinentalExtraVarianceAmplitude)
            SHADER_PARAMETER(float, TransitionAgeMy)
            SHADER_PARAMETER(float, ContinentalMinDetailScale)
            SHADER_PARAMETER(float, ContinentalNormalizationEpsilon)
            SHADER_PARAMETER(uint32, bWriteDebug)
            SHADER_PARAMETER(uint32, DebugVertexIndex)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, ContinentalWorkIndices)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ContinentalBaseline)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector3f>, ContinentalRenderPosition)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, ContinentalPackedTerrainInfo)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FUintVector4>, ContinentalExemplarIndices)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, ContinentalExemplarWeights)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector2f>, ContinentalRandomUV)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, ContinentalWrappedUV)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, ContinentalSampleHeights)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector3f>, ContinentalFoldDirection)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, ContinentalOrogenyClass)
            SHADER_PARAMETER(uint32, bEnableAnisotropy)
            SHADER_PARAMETER(float, ContinentalAnisoAlong)
            SHADER_PARAMETER(float, ContinentalAnisoAcross)
            SHADER_PARAMETER(FVector4f, AnisoClassWeights)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray, ContinentalExemplarTexture)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ContinentalCrustAge)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, ContinentalRidgeDirection)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, ContinentalExemplarMetadata)
            SHADER_PARAMETER(uint32, ContinentalTextureWidth)
            SHADER_PARAMETER(uint32, ContinentalTextureHeight)
            SHADER_PARAMETER(uint32, ContinentalLayerCount)
            SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ContinentalDebugOutput)
            SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, ContinentalOutAmplified)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            const ERHIFeatureLevel::Type MaxFeatureLevel = FDataDrivenShaderPlatformInfo::GetMaxFeatureLevel(Parameters.Platform);
            return MaxFeatureLevel == ERHIFeatureLevel::SM5 || MaxFeatureLevel == ERHIFeatureLevel::SM6;
        }
    };

    IMPLEMENT_GLOBAL_SHADER(FStageBUnifiedContinentalCS, "/Plugin/PlanetaryCreation/Private/StageB_Unified_V2.usf", "ContinentalMainCS", SF_Compute);

    class FStageBUnifiedOceanicPreviewCS : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FStageBUnifiedOceanicPreviewCS);
        SHADER_USE_PARAMETER_STRUCT(FStageBUnifiedOceanicPreviewCS, FGlobalShader);

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(uint32, VertexCount)
            SHADER_PARAMETER(FUintVector2, TextureSize)
            SHADER_PARAMETER(float, RidgeAmplitude)
            SHADER_PARAMETER(float, FaultFrequency)
            SHADER_PARAMETER(float, AgeFalloff)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InBaseline)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, InRidgeDirection)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InCrustAge)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector3f>, InRenderPosition)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, InOceanicMask)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeightTexture)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            const ERHIFeatureLevel::Type MaxFeatureLevel = FDataDrivenShaderPlatformInfo::GetMaxFeatureLevel(Parameters.Platform);
            return MaxFeatureLevel == ERHIFeatureLevel::SM5 || MaxFeatureLevel == ERHIFeatureLevel::SM6;
        }
    };

    IMPLEMENT_GLOBAL_SHADER(FStageBUnifiedOceanicPreviewCS, "/Plugin/PlanetaryCreation/Private/OceanicAmplificationPreview.usf", "MainCS", SF_Compute);

    bool ApplyStageBUnifiedGPU(
        UTectonicSimulationService& Service,
        bool bDispatchOceanic,
        bool bDispatchContinental,
        FStageBUnifiedDispatchResult& OutResult)
    {
        OutResult = FStageBUnifiedDispatchResult();
        OutResult.bOceanicRequested = bDispatchOceanic;
        OutResult.bContinentalRequested = bDispatchContinental;

        if (!bDispatchOceanic && !bDispatchContinental)
        {
            return false;
        }

        if (!SupportsGPUAmplification())
        {
            UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[StageB][GPU] Unified dispatch skipped: RHI feature level insufficient."));
            return false;
        }

        const TArray<float>* BaselineFloatPtr = nullptr;
        const TArray<FVector4f>* RidgeDirFloatPtr = nullptr;
        const TArray<float>* CrustAgeFloatPtr = nullptr;
        const TArray<FVector3f>* PositionFloatPtr = nullptr;
        const TArray<uint32>* OceanicMaskPtr = nullptr;
        Service.GetOceanicAmplificationFloatInputs(BaselineFloatPtr, RidgeDirFloatPtr, CrustAgeFloatPtr, PositionFloatPtr, OceanicMaskPtr);

        if (!BaselineFloatPtr || !RidgeDirFloatPtr || !CrustAgeFloatPtr || !PositionFloatPtr || !OceanicMaskPtr)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[StageB][GPU] Unified dispatch aborted: Float caches unavailable."));
            return false;
        }

        const int32 VertexCount = BaselineFloatPtr->Num();
        if (VertexCount <= 0 ||
            RidgeDirFloatPtr->Num() != VertexCount ||
            CrustAgeFloatPtr->Num() != VertexCount ||
            PositionFloatPtr->Num() != VertexCount ||
            OceanicMaskPtr->Num() != VertexCount)
        {
            UE_LOG(LogPlanetaryCreation, Warning,
                TEXT("[StageB][GPU] Unified dispatch aborted: Float cache size mismatch (Baseline=%d Ridge=%d Crust=%d Position=%d Mask=%d)."),
                BaselineFloatPtr->Num(),
                RidgeDirFloatPtr->Num(),
                CrustAgeFloatPtr->Num(),
                PositionFloatPtr->Num(),
                OceanicMaskPtr->Num());
            return false;
        }

        PlanetaryCreation::StageB::FStageB_UnifiedParameters UnifiedParams = Service.GetStageBUnifiedParameters();
        const FTectonicSimulationParameters CurrentParams = Service.GetParameters();
        UE_LOG(LogPlanetaryCreation, Display,
            TEXT("[UnifiedGPU][Params] UnifiedFaultAmp=%.2f UnifiedFaultFreq=%.3f UnifiedAgeFalloff=%.3f UnifiedVarianceScale=%.2f UnifiedExtraVariance=%.2f | CpuFaultAmp=%.2f CpuFaultFreq=%.3f CpuAgeFalloff=%.3f"),
            UnifiedParams.OceanicFaultAmplitude,
            UnifiedParams.OceanicFaultFrequency,
            UnifiedParams.OceanicAgeFalloff,
            UnifiedParams.OceanicVarianceScale,
            UnifiedParams.ExtraVarianceAmplitude,
            CurrentParams.OceanicFaultAmplitude,
            CurrentParams.OceanicFaultFrequency,
            CurrentParams.OceanicAgeFalloff);

        float AnisoCoveragePercent = 0.0f;
        int32 AnisoValidCount = 0;
        TArray<FVector3f> ContinentalFoldDirectionData;
        TArray<uint32> ContinentalOrogenyClassData;

        if (UnifiedParams.bEnableAnisotropy)
        {
            const bool bCoverageOk = Service.EvaluateAnisotropyCoverage(AnisoCoveragePercent, AnisoValidCount);
            const TArray<FVector3f>& FoldDirections = Service.GetVertexFoldDirection();
            const TArray<EOrogenyClass>& OrogenyClasses = Service.GetVertexOrogenyClass();
            const bool bSizeMatches = FoldDirections.Num() == VertexCount && OrogenyClasses.Num() == VertexCount;

            if (!bCoverageOk || !bSizeMatches)
            {
                if (!bSizeMatches)
                {
                    UE_LOG(LogPlanetaryCreation, Warning,
                        TEXT("[Aniso] Coverage check skipped: Fold/Orogeny size mismatch (Fold=%d Orogeny=%d VertexCount=%d)"),
                        FoldDirections.Num(),
                        OrogenyClasses.Num(),
                        VertexCount);
                }
                else
                {
                    UE_LOG(LogPlanetaryCreation, Warning,
                        TEXT("[Aniso] CoverageLow=%.1f%%, skipping anisotropy this pass"),
                        AnisoCoveragePercent);
                }
                UnifiedParams.bEnableAnisotropy = false;
            }
            else
            {
                ContinentalFoldDirectionData = FoldDirections;
                ContinentalOrogenyClassData.SetNum(VertexCount);
                for (int32 Index = 0; Index < VertexCount; ++Index)
                {
                    ContinentalOrogenyClassData[Index] = static_cast<uint32>(OrogenyClasses[Index]);
                }

                if (!GStageBAnisotropyLoggedThisRun)
                {
                    UE_LOG(LogPlanetaryCreation, Log,
                        TEXT("[Aniso] Enabled=1 Mode=ClassOnly Along=%.2f Across=%.2f ClassWeights=[None=%.2f Nascent=%.2f Active=%.2f Dormant=%.2f] Coverage=%.1f%%"),
                        UnifiedParams.ContinentalAnisoAlong,
                        UnifiedParams.ContinentalAnisoAcross,
                        UnifiedParams.AnisoClassWeights[0],
                        UnifiedParams.AnisoClassWeights[1],
                        UnifiedParams.AnisoClassWeights[2],
                        UnifiedParams.AnisoClassWeights[3],
                        AnisoCoveragePercent);
                    GStageBAnisotropyLoggedThisRun = true;
                }
            }
        }

        if (!UnifiedParams.bEnableAnisotropy)
        {
            GStageBAnisotropyLoggedThisRun = false;
            ContinentalFoldDirectionData.SetNum(1);
            ContinentalFoldDirectionData[0] = FVector3f::ZeroVector;
            ContinentalOrogenyClassData.SetNum(1);
            ContinentalOrogenyClassData[0] = 0u;
        }

        int32 DebugVertexIndex = Service.GetStageBUnifiedDebugVertexIndex();
        if (DebugVertexIndex == INDEX_NONE)
        {
            DebugVertexIndex = 23949;
        }

        // Oceanic compaction
        TSharedPtr<TArray<uint32>, ESPMode::ThreadSafe> OceanicIndexData;
        uint32 OceanicWorkCount = 0;
        uint32 OceanicGroupCountX = 0;

        if (bDispatchOceanic)
        {
            TArray<uint32> LocalIndices;
            LocalIndices.Reserve(VertexCount);
            for (int32 Index = 0; Index < VertexCount; ++Index)
            {
                if ((*OceanicMaskPtr)[Index] != 0u)
                {
                    LocalIndices.Add(static_cast<uint32>(Index));
                }
            }

            if (LocalIndices.Num() > 0)
            {
                OceanicWorkCount = static_cast<uint32>(LocalIndices.Num());
                OceanicGroupCountX = FMath::Max(1u, FMath::DivideAndRoundUp(OceanicWorkCount, GStageBThreadsPerGroup));
                OceanicIndexData = MakeShared<TArray<uint32>, ESPMode::ThreadSafe>(MoveTemp(LocalIndices));

                const bool bOceanicContainsDebug = OceanicIndexData->Contains(static_cast<uint32>(DebugVertexIndex));
                UE_LOG(LogPlanetaryCreation, Display,
                    TEXT("[UnifiedGPU][PreDispatch] OceanicWorkCount=%u ContainsVertex40283=%d OceanicMask[%d]=%u"),
                    OceanicWorkCount,
                    bOceanicContainsDebug ? 1 : 0,
                    DebugVertexIndex,
                    OceanicMaskPtr->IsValidIndex(DebugVertexIndex) ? (*OceanicMaskPtr)[DebugVertexIndex] : 0u);
            }
            else
            {
                UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[StageB][GPU] Oceanic compaction produced no work items (mask filtered all vertices)."));
                bDispatchOceanic = false;
            }
        }

        UE_LOG(LogPlanetaryCreation, Log, TEXT("[StageB][GPU] Oceanic work count: %u"), OceanicWorkCount);

        // Continental compaction
        const FContinentalAmplificationGPUInputs& ContinentalInputs = Service.GetContinentalAmplificationGPUInputs();
        const TArray<uint32>& ContinentalPackedInfo = ContinentalInputs.PackedTerrainInfo;
        const TArray<FUintVector4>& ContinentalExemplarIndices = ContinentalInputs.ExemplarIndices;
        const TArray<FVector4f>& ContinentalExemplarWeights = ContinentalInputs.ExemplarWeights;
        const TArray<FVector2f>& ContinentalRandomUV = ContinentalInputs.RandomUVOffsets;
        const TArray<FVector2f>& ContinentalWrappedUV = ContinentalInputs.WrappedUVs;
        const TArray<FVector4f>& ContinentalSampleHeights = ContinentalInputs.SampleHeights;
        const TArray<float>& ContinentalBaseline = ContinentalInputs.BaselineElevation;
        const TArray<FVector3f>& ContinentalRenderPositions = ContinentalInputs.RenderPositions;

        if (const TArray<double>& AmplifiedElevations = Service.GetVertexAmplifiedElevation(); AmplifiedElevations.IsValidIndex(DebugVertexIndex))
        {
            const TArray<int32>& PlateAssignments = Service.GetVertexPlateAssignments();
            const double DebugBaseline = AmplifiedElevations[DebugVertexIndex];
            const int32 DebugPlate = PlateAssignments.IsValidIndex(DebugVertexIndex) ? PlateAssignments[DebugVertexIndex] : INDEX_NONE;
            const float DebugCrustAge = (CrustAgeFloatPtr && (*CrustAgeFloatPtr).IsValidIndex(DebugVertexIndex))
                ? (*CrustAgeFloatPtr)[DebugVertexIndex]
                : 0.0f;
            const FVector4f DebugRidgeDir = (RidgeDirFloatPtr && (*RidgeDirFloatPtr).IsValidIndex(DebugVertexIndex))
                ? (*RidgeDirFloatPtr)[DebugVertexIndex]
                : FVector4f::Zero();
            const FVector3f DebugRenderPos = (PositionFloatPtr && (*PositionFloatPtr).IsValidIndex(DebugVertexIndex))
                ? (*PositionFloatPtr)[DebugVertexIndex]
                : FVector3f::ZeroVector;
            const uint32 DebugMask = (OceanicMaskPtr && (*OceanicMaskPtr).IsValidIndex(DebugVertexIndex))
                ? (*OceanicMaskPtr)[DebugVertexIndex]
                : 0u;
            double CpuExpected = 0.0;
            if (DebugPlate != INDEX_NONE)
            {
                const TArray<FTectonicPlate>& DebugPlates = Service.GetPlates();
                const TMap<TPair<int32, int32>, FPlateBoundary>& DebugBoundaries = Service.GetBoundaries();
                CpuExpected = ComputeOceanicAmplification(
                    FVector3d(DebugRenderPos.X, DebugRenderPos.Y, DebugRenderPos.Z),
                    DebugPlate,
                    static_cast<double>(DebugCrustAge),
                    DebugBaseline,
                    FVector3d(DebugRidgeDir.X, DebugRidgeDir.Y, DebugRidgeDir.Z),
                    DebugPlates,
                    DebugBoundaries,
                    Service.GetParameters());
            }
            UE_LOG(LogPlanetaryCreation, Display,
                TEXT("[UnifiedGPU][PreDispatch] Vertex %d Baseline=%.2f Plate=%d OceanicMask=%u CrustAge=%.2f RidgeDir=(%.3f,%.3f,%.3f,%.3f) RenderPos=(%.3f,%.3f,%.3f) ContinentalInputsBaselineValid=%d ContinentalBaselineValue=%.2f CPUAmplified=%.2f AmplifiedCount=%d"),
                DebugVertexIndex,
                DebugBaseline,
                DebugPlate,
                DebugMask,
                DebugCrustAge,
                DebugRidgeDir.X, DebugRidgeDir.Y, DebugRidgeDir.Z, DebugRidgeDir.W,
                DebugRenderPos.X, DebugRenderPos.Y, DebugRenderPos.Z,
                ContinentalBaseline.IsValidIndex(DebugVertexIndex) ? 1 : 0,
                ContinentalBaseline.IsValidIndex(DebugVertexIndex) ? ContinentalBaseline[DebugVertexIndex] : 0.0f,
                CpuExpected,
                AmplifiedElevations.Num());
            if (ContinentalWrappedUV.IsValidIndex(DebugVertexIndex))
            {
                const FVector2f& WrappedUV = ContinentalWrappedUV[DebugVertexIndex];
                UE_LOG(LogPlanetaryCreation, Display,
                    TEXT("[UnifiedGPU][PreDispatch] Vertex %d WrappedUV=(%.4f,%.4f)"),
                    DebugVertexIndex,
                    WrappedUV.X,
                    WrappedUV.Y);
            }
        }

        TSharedPtr<TArray<uint32>, ESPMode::ThreadSafe> ContinentalIndexData;
        uint32 ContinentalWorkCount = 0;
        uint32 ContinentalGroupCountX = 0;

#if UE_BUILD_DEVELOPMENT
        if (bDispatchContinental && ContinentalExemplarIndices.IsValidIndex(DebugVertexIndex) && ContinentalExemplarWeights.IsValidIndex(DebugVertexIndex))
        {
            const FUintVector4 DebugPackedIndices = ContinentalExemplarIndices[DebugVertexIndex];
            const FVector4f DebugWeights = ContinentalExemplarWeights[DebugVertexIndex];
            UE_LOG(LogPlanetaryCreation, Display,
                TEXT("[UnifiedGPU][PreDispatch] DebugIndices=(%u,%u,%u) DebugWeights=(%.3f,%.3f,%.3f)"),
                DebugPackedIndices.X,
                DebugPackedIndices.Y,
                DebugPackedIndices.Z,
                DebugWeights.X,
                DebugWeights.Y,
                DebugWeights.Z);
        }
#endif

        const TArray<int32>& PlateAssignments = Service.GetVertexPlateAssignments();

        if (bDispatchContinental)
        {
            const bool bInputsValid =
                ContinentalPackedInfo.Num() == VertexCount &&
                ContinentalExemplarIndices.Num() == VertexCount &&
                ContinentalExemplarWeights.Num() == VertexCount &&
                ContinentalRandomUV.Num() == VertexCount &&
                ContinentalWrappedUV.Num() == VertexCount &&
                ContinentalBaseline.Num() == VertexCount &&
                ContinentalRenderPositions.Num() == VertexCount;

            if (!bInputsValid)
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[StageB][GPU] Continental dispatch aborted: GPU cache size mismatch."));
                bDispatchContinental = false;
            }
            else
            {
                const TArray<FTectonicPlate>& Plates = Service.GetPlates();

                auto FindPlateByID = [&Plates](int32 PlateID) -> const FTectonicPlate*
                {
                    if (PlateID == INDEX_NONE)
                    {
                        return nullptr;
                    }

                    for (const FTectonicPlate& Plate : Plates)
                    {
                        if (Plate.PlateID == PlateID)
                        {
                            return &Plate;
                        }
                    }
                    return nullptr;
                };

                TArray<uint32> LocalIndices;
                LocalIndices.Reserve(VertexCount);
                for (int32 Index = 0; Index < VertexCount; ++Index)
                {
                    const int32 PlateId = PlateAssignments.IsValidIndex(Index) ? PlateAssignments[Index] : INDEX_NONE;
                    const FTectonicPlate* PlatePtr = FindPlateByID(PlateId);
                    const bool bPlateOceanic = PlatePtr ? (PlatePtr->CrustType == ECrustType::Oceanic) : false;
                    if (bPlateOceanic)
                    {
                        continue;
                    }

                    LocalIndices.Add(static_cast<uint32>(Index));
                }

                if (LocalIndices.Num() > 0)
                {
                    ContinentalWorkCount = static_cast<uint32>(LocalIndices.Num());
                    ContinentalGroupCountX = FMath::Max(1u, FMath::DivideAndRoundUp(ContinentalWorkCount, GStageBThreadsPerGroup));
                    ContinentalIndexData = MakeShared<TArray<uint32>, ESPMode::ThreadSafe>(MoveTemp(LocalIndices));

                    const bool bContainsDebug = ContinentalIndexData->Contains(static_cast<uint32>(DebugVertexIndex));
                    UE_LOG(LogPlanetaryCreation, Display,
                        TEXT("[UnifiedGPU][PreDispatch] ContinentalWorkCount=%u ContainsVertex40283=%d OceanicMask[%d]=%u"),
                        ContinentalWorkCount,
                        bContainsDebug ? 1 : 0,
                        DebugVertexIndex,
                        OceanicMaskPtr->IsValidIndex(DebugVertexIndex) ? (*OceanicMaskPtr)[DebugVertexIndex] : 0u);
#if UE_BUILD_DEVELOPMENT
                    if (DebugVertexIndex >= 0)
                    {
                        const int32 DebugWorkIndex = ContinentalIndexData->IndexOfByKey(static_cast<uint32>(DebugVertexIndex));
                        UE_LOG(LogPlanetaryCreation, Display,
                            TEXT("[UnifiedGPU][PreDispatch] DebugVertexIndex=%d WorkIndex=%d"),
                            DebugVertexIndex,
                            DebugWorkIndex);
                    }
#endif
                }
                else
                {
                    UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[StageB][GPU] Continental compaction produced no work items (all vertices classified oceanic)."));
                    bDispatchContinental = false;
                }
            }
        }

        UE_LOG(LogPlanetaryCreation, Log, TEXT("[StageB][GPU] Continental work count: %u"), ContinentalWorkCount);

        if (!bDispatchOceanic && !bDispatchContinental)
        {
            return false;
        }

        TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> OceanicReadback;
        if (bDispatchOceanic)
        {
            OceanicReadback = Service.AcquireOceanicGPUReadbackBuffer();
            if (!OceanicReadback.IsValid())
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[StageB][GPU] Oceanic dispatch aborted: Unable to acquire readback buffer."));
                bDispatchOceanic = false;
            }
        }

        TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> ContinentalReadback;
        if (bDispatchContinental)
        {
            ContinentalReadback = Service.AcquireContinentalGPUReadbackBuffer();
            if (!ContinentalReadback.IsValid())
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[StageB][GPU] Continental dispatch aborted: Unable to acquire readback buffer."));
                bDispatchContinental = false;
            }
        }

        if (!bDispatchOceanic && !bDispatchContinental)
        {
            return false;
        }

        const FTectonicSimulationParameters SimParams = Service.GetParameters();
        if (bDispatchOceanic && PlateAssignments.Num() != VertexCount)
        {
            UE_LOG(LogPlanetaryCreation, Warning,
                TEXT("[StageB][GPU] Oceanic dispatch aborted: Plate assignment size mismatch (%d vs %d)."),
                PlateAssignments.Num(),
                VertexCount);
            bDispatchOceanic = false;
        }

        TSharedPtr<FOceanicAmplificationSnapshot, ESPMode::ThreadSafe> OceanicSnapshotPtr;
        if (bDispatchOceanic)
        {
            FOceanicAmplificationSnapshot Snapshot;
            Snapshot.VertexCount = VertexCount;
            Snapshot.Parameters = SimParams;
            Snapshot.UnifiedParameters = UnifiedParams;
            Snapshot.RenderLOD = SimParams.RenderSubdivisionLevel;
            Snapshot.TopologyVersion = Service.GetTopologyVersion();
            Snapshot.SurfaceVersion = Service.GetSurfaceDataVersion();
            Snapshot.SnapshotId = Service.AllocateOceanicSnapshotId();
            Snapshot.DataSerial = Service.GetOceanicAmplificationDataSerial();
            Snapshot.BaselineElevation = *BaselineFloatPtr;
            Snapshot.RidgeDirections = *RidgeDirFloatPtr;
            Snapshot.CrustAge = *CrustAgeFloatPtr;
            Snapshot.RenderPositions = *PositionFloatPtr;
            Snapshot.OceanicMask = *OceanicMaskPtr;
            Snapshot.PlateAssignments = PlateAssignments;
            Snapshot.InputHash = HashOceanicSnapshot(Snapshot);
            OceanicSnapshotPtr = MakeShared<FOceanicAmplificationSnapshot, ESPMode::ThreadSafe>(MoveTemp(Snapshot));
        }

        TSharedPtr<FContinentalAmplificationSnapshot, ESPMode::ThreadSafe> ContinentalSnapshotPtr;
        if (bDispatchContinental)
        {
            FContinentalAmplificationSnapshot Snapshot;
            if (Service.CreateContinentalAmplificationSnapshot(Snapshot))
            {
                Snapshot.UnifiedParameters = UnifiedParams;
                ContinentalSnapshotPtr = MakeShared<FContinentalAmplificationSnapshot, ESPMode::ThreadSafe>(MoveTemp(Snapshot));
            }
            else
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[StageB][GPU] Continental dispatch aborted: Snapshot creation failed."));
                bDispatchContinental = false;
            }
        }

        if (!bDispatchOceanic && !bDispatchContinental)
        {
            return false;
        }

        TSharedPtr<TArray<FVector4f>, ESPMode::ThreadSafe> ExemplarMetadataPtr;
        FExemplarTextureArray* ExemplarArrayPtr = nullptr;
        uint32 ExemplarTextureWidth = 0u;
        uint32 ExemplarTextureHeight = 0u;
        uint32 ExemplarLayerCount = 0u;
        if (bDispatchContinental)
        {
            Service.InitializeGPUExemplarResources();
            FExemplarTextureArray& ExemplarArray = GetExemplarTextureArray();
            ExemplarArrayPtr = &ExemplarArray;
            const TArray<FExemplarTextureArray::FExemplarInfo>& InfoArray = ExemplarArray.GetExemplarInfo();
            ExemplarTextureWidth = static_cast<uint32>(ExemplarArray.GetTextureWidth());
            ExemplarTextureHeight = static_cast<uint32>(ExemplarArray.GetTextureHeight());
            ExemplarLayerCount = static_cast<uint32>(InfoArray.Num());
            TArray<FVector4f> Metadata;
            Metadata.SetNum(InfoArray.Num());
            for (const FExemplarTextureArray::FExemplarInfo& Info : InfoArray)
            {
                if (!Metadata.IsValidIndex(Info.ArrayIndex))
                {
                    continue;
                }
                const float StdDev = FMath::Max(Info.ElevationStdDev_m, 1.0e-3f);
                Metadata[Info.ArrayIndex] = FVector4f(Info.ElevationMin_m, Info.ElevationMax_m, Info.ElevationMean_m, StdDev);
            }
            ExemplarMetadataPtr = MakeShared<TArray<FVector4f>, ESPMode::ThreadSafe>(MoveTemp(Metadata));
        }

        if (!bDispatchOceanic && !bDispatchContinental)
        {
            return false;
        }

#if UE_BUILD_DEVELOPMENT
        if (DebugVertexIndex >= 0 && ContinentalSampleHeights.IsValidIndex(DebugVertexIndex))
        {
            const FVector4f& DebugSamples = ContinentalSampleHeights[DebugVertexIndex];
            UE_LOG(LogPlanetaryCreation, Log,
                TEXT("[UnifiedGPU][SampleHeights] Vertex=%d Heights=(%.3f,%.3f,%.3f) TotalWeight=%.3f"),
                DebugVertexIndex,
                DebugSamples.X,
                DebugSamples.Y,
                DebugSamples.Z,
                DebugSamples.W);
        }
#endif

        UTexture2DArray* ExemplarTextureObject = nullptr;
        if (bDispatchContinental)
        {
            if (ExemplarArrayPtr)
            {
                ExemplarTextureObject = ExemplarArrayPtr->GetTextureArray();
            }
#if UE_BUILD_DEVELOPMENT
            UE_LOG(LogPlanetaryCreation, Display,
                TEXT("[UnifiedGPU][ExemplarTexture] Width=%u Height=%u Layers=%u TextureValid=%d"),
                ExemplarTextureWidth,
                ExemplarTextureHeight,
                ExemplarLayerCount,
                ExemplarTextureObject ? 1 : 0);
#endif
            if (!ExemplarTextureObject)
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[StageB][GPU] Continental dispatch aborted: Exemplar texture unavailable."));
                bDispatchContinental = false;
            }
        }

        TSharedPtr<bool, ESPMode::ThreadSafe> OceanicDispatchSucceeded = MakeShared<bool, ESPMode::ThreadSafe>(false);
        TSharedPtr<bool, ESPMode::ThreadSafe> ContinentalDispatchSucceeded = MakeShared<bool, ESPMode::ThreadSafe>(false);
        TRefCountPtr<FRDGPooledBuffer> OceanicDebugBufferRef;
        TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> OceanicDebugReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("StageBUnified.OceanicDebug"));
        TRefCountPtr<FRDGPooledBuffer> ContinentalDebugBufferRef;
        TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> ContinentalDebugReadback;
        if (bDispatchContinental)
        {
            ContinentalDebugReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("StageBUnified.ContinentalDebug"));
        }
        const double DispatchStart = FPlatformTime::Seconds();

        ENQUEUE_RENDER_COMMAND(StageBUnifiedGPU)(
            [BaselineArray = BaselineFloatPtr,
             RidgeArray = RidgeDirFloatPtr,
             CrustArray = CrustAgeFloatPtr,
             PositionArray = PositionFloatPtr,
             OceanicIndexData,
             ContinentalIndexData,
             ContinentalPackedInfo,
             ContinentalExemplarIndices,
             ContinentalExemplarWeights,
             ContinentalRandomUV,
             ContinentalSampleHeights,
             ContinentalWrappedUV,
             ContinentalBaseline,
             ContinentalRenderPositions,
             ContinentalFoldDirectionData,
             ContinentalOrogenyClassData,
             ExemplarTextureObject,
             OceanicReadback,
             ContinentalReadback,
             ExemplarMetadataPtr,
             ExemplarTextureWidth,
             ExemplarTextureHeight,
             ExemplarLayerCount,
             OceanicWorkCount,
             OceanicGroupCountX,
             ContinentalWorkCount,
             ContinentalGroupCountX,
             UnifiedParams,
             VertexCount,
             DebugVertexIndex,
             bDispatchOceanic,
             bDispatchContinental,
             OceanicDispatchSucceeded,
             ContinentalDispatchSucceeded,
             &OceanicDebugBufferRef,
             &ContinentalDebugBufferRef,
             OceanicDebugReadback,
             &ContinentalDebugReadback](FRHICommandListImmediate& RHICmdList)
           {
               FRDGBuilder GraphBuilder(RHICmdList);

                FRDGBufferRef OceanicDebugBuffer = GraphBuilder.CreateBuffer(
                    FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), 1),
                    TEXT("PlanetaryCreation.StageBUnified.OceanicDebug"));
                FRDGBufferUAVRef OceanicDebugUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OceanicDebugBuffer));
                AddClearUAVFloatPass(GraphBuilder, OceanicDebugUAV, 0.0f);
                GraphBuilder.QueueBufferExtraction(OceanicDebugBuffer, &OceanicDebugBufferRef);

               FRDGBufferRef BaselineBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.Baseline"), *BaselineArray);
               FRDGBufferRef RidgeBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.Ridge"), *RidgeArray);
               FRDGBufferRef AgeBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.Age"), *CrustArray);
               FRDGBufferRef PositionBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.Position"), *PositionArray);

                TRefCountPtr<FRDGPooledBuffer> OceanicOutputBufferRef;
                if (bDispatchOceanic && OceanicIndexData.IsValid() && OceanicReadback.IsValid())
                {
                    FRDGBufferRef WorkIndexBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.OceanicWork"), *OceanicIndexData);
                    FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), VertexCount), TEXT("PlanetaryCreation.StageBUnified.OceanicOutput"));
                    AddCopyBufferPass(GraphBuilder, BaselineBuffer, OutputBuffer);

                    FStageBUnifiedOceanicCS::FParameters* Parameters = GraphBuilder.AllocParameters<FStageBUnifiedOceanicCS::FParameters>();
                    Parameters->OceanicWorkCount = OceanicWorkCount;
                    Parameters->OceanicGroupCountX = OceanicGroupCountX;
                    Parameters->OceanicGroupCountY = 1u;
                    Parameters->OceanicRidgeAmplitude = UnifiedParams.OceanicFaultAmplitude;
                    Parameters->OceanicFaultFrequency = UnifiedParams.OceanicFaultFrequency;
                    Parameters->OceanicAgeFalloff = UnifiedParams.OceanicAgeFalloff;
                    Parameters->OceanicVarianceScale = UnifiedParams.OceanicVarianceScale;
                    Parameters->OceanicExtraVarianceAmplitude = UnifiedParams.ExtraVarianceAmplitude;
                    Parameters->bWriteDebug = 1u;
                    Parameters->DebugVertexIndex = static_cast<uint32>(DebugVertexIndex);
                    Parameters->OceanicWorkIndices = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(WorkIndexBuffer));
                    Parameters->OceanicBaseline = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(BaselineBuffer));
                    Parameters->OceanicRidgeDirection = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RidgeBuffer));
                    Parameters->OceanicCrustAge = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(AgeBuffer));
                    Parameters->OceanicRenderPosition = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(PositionBuffer));
                    Parameters->OceanicDebugOutput = OceanicDebugUAV;
                    Parameters->OceanicOutAmplified = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutputBuffer));

                    TShaderMapRef<FStageBUnifiedOceanicCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                    FComputeShaderUtils::AddPass(
                        GraphBuilder,
                        RDG_EVENT_NAME("PlanetaryCreation.StageBUnified.Oceanic"),
                        ComputeShader,
                        Parameters,
                        FIntVector(OceanicGroupCountX, 1, 1));

                    GraphBuilder.QueueBufferExtraction(OutputBuffer, &OceanicOutputBufferRef);
                }

                TRefCountPtr<FRDGPooledBuffer> ContinentalOutputBufferRef;
                if (bDispatchContinental && ContinentalIndexData.IsValid() && ContinentalReadback.IsValid() && ExemplarMetadataPtr.IsValid())
                {
                    FRDGTextureRef ExemplarTextureRDG = nullptr;
                    if (ExemplarTextureObject)
                    {
                        if (FTextureResource* TextureResource = ExemplarTextureObject->GetResource())
                        {
                            if (TextureResource->TextureRHI.IsValid())
                            {
                                ExemplarTextureRDG = GraphBuilder.RegisterExternalTexture(
                                    CreateRenderTarget(TextureResource->TextureRHI, TEXT("PlanetaryCreation.StageBUnified.ExemplarTexture")));
                            }
                        }
                    }

                if (!ExemplarTextureRDG)
                {
                    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[StageB][GPU] Continental dispatch aborted: Unable to register exemplar texture."));
                }
                else
                {
                        FRDGBufferRef WorkIndexBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.ContinentalWork"), *ContinentalIndexData);
                        FRDGBufferRef PackedInfoBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.ContinentalPackedInfo"), ContinentalPackedInfo);
                        FRDGBufferRef ExemplarIndexBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.ContinentalExemplarIndices"), ContinentalExemplarIndices);
                        FRDGBufferRef ExemplarWeightBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.ContinentalExemplarWeights"), ContinentalExemplarWeights);
                        FRDGBufferRef RandomUVBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.ContinentalRandomUV"), ContinentalRandomUV);
                        TArray<FVector4f> ContinentalWrappedUVExpanded;
                        ContinentalWrappedUVExpanded.SetNum(ContinentalWrappedUV.Num());
                        for (int32 UVIndex = 0; UVIndex < ContinentalWrappedUV.Num(); ++UVIndex)
                        {
                            const FVector2f& Wrapped = ContinentalWrappedUV[UVIndex];
                            ContinentalWrappedUVExpanded[UVIndex] = FVector4f(Wrapped.X, Wrapped.Y, 0.0f, 0.0f);
                        }
                        FRDGBufferRef WrappedUVBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.ContinentalWrappedUV"), ContinentalWrappedUVExpanded);
                        FRDGBufferRef SampleHeightsBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.ContinentalSampleHeights"), ContinentalSampleHeights);
                        FRDGBufferRef BaselineBufferLocal = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.ContinentalBaseline"), ContinentalBaseline);
                        FRDGBufferRef RenderPositionBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.ContinentalPosition"), ContinentalRenderPositions);
                        FRDGBufferRef FoldDirectionBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.ContinentalFoldDirection"), ContinentalFoldDirectionData);
                        FRDGBufferRef OrogenyClassBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.ContinentalOrogenyClass"), ContinentalOrogenyClassData);
                        FRDGBufferRef MetadataBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.ExemplarMetadata"), *ExemplarMetadataPtr);

                        FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), VertexCount), TEXT("PlanetaryCreation.StageBUnified.ContinentalOutput"));
                        AddCopyBufferPass(GraphBuilder, BaselineBufferLocal, OutputBuffer);

                        FRDGBufferRef ContinentalDebugBuffer = GraphBuilder.CreateBuffer(
                            FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), FMath::Max<uint32>(ContinentalWorkCount, 1u)),
                            TEXT("PlanetaryCreation.StageBUnified.ContinentalDebug"));
                        FRDGBufferUAVRef ContinentalDebugUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ContinentalDebugBuffer));
                        AddClearUAVFloatPass(GraphBuilder, ContinentalDebugUAV, 0.0f);

                        GraphBuilder.QueueBufferExtraction(ContinentalDebugBuffer, &ContinentalDebugBufferRef);

                        FStageBUnifiedContinentalCS::FParameters* Parameters = GraphBuilder.AllocParameters<FStageBUnifiedContinentalCS::FParameters>();
                        Parameters->ContinentalWorkCount = ContinentalWorkCount;
                        Parameters->ContinentalGroupCountX = ContinentalGroupCountX;
                        Parameters->ContinentalGroupCountY = 1u;
                        Parameters->ContinentalRidgeAmplitude = UnifiedParams.OceanicFaultAmplitude;
                        Parameters->ContinentalFaultFrequency = UnifiedParams.OceanicFaultFrequency;
                        Parameters->ContinentalAgeFalloff = UnifiedParams.OceanicAgeFalloff;
                        Parameters->ContinentalVarianceScale = UnifiedParams.OceanicVarianceScale;
                    Parameters->ContinentalExtraVarianceAmplitude = UnifiedParams.ExtraVarianceAmplitude;
                    Parameters->TransitionAgeMy = UnifiedParams.TransitionAgeMy;
                    Parameters->ContinentalMinDetailScale = UnifiedParams.ContinentalMinDetailScale;
                    Parameters->ContinentalNormalizationEpsilon = UnifiedParams.ContinentalNormalizationEpsilon;
                    Parameters->bWriteDebug = 1u;
#if UE_BUILD_DEVELOPMENT
                    UE_LOG(LogPlanetaryCreation, VeryVerbose,
                        TEXT("[UnifiedGPU] Debug parameters | bWriteDebug=%u"),
                        Parameters->bWriteDebug);
#endif
                        Parameters->DebugVertexIndex = static_cast<uint32>(DebugVertexIndex);
                        Parameters->ContinentalWorkIndices = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(WorkIndexBuffer));
                        Parameters->ContinentalBaseline = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(BaselineBufferLocal));
                        Parameters->ContinentalRenderPosition = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RenderPositionBuffer));
                        Parameters->ContinentalPackedTerrainInfo = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(PackedInfoBuffer));
                        Parameters->ContinentalExemplarIndices = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ExemplarIndexBuffer));
                        Parameters->ContinentalExemplarWeights = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ExemplarWeightBuffer));
                        Parameters->ContinentalRandomUV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RandomUVBuffer));
                        Parameters->ContinentalWrappedUV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(WrappedUVBuffer));
                        Parameters->ContinentalSampleHeights = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(SampleHeightsBuffer));
                        Parameters->ContinentalFoldDirection = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(FoldDirectionBuffer));
                        Parameters->ContinentalOrogenyClass = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(OrogenyClassBuffer));
                        Parameters->bEnableAnisotropy = UnifiedParams.bEnableAnisotropy ? 1u : 0u;
                        Parameters->ContinentalAnisoAlong = UnifiedParams.ContinentalAnisoAlong;
                        Parameters->ContinentalAnisoAcross = UnifiedParams.ContinentalAnisoAcross;
                        Parameters->AnisoClassWeights = FVector4f(
                            UnifiedParams.AnisoClassWeights[0],
                            UnifiedParams.AnisoClassWeights[1],
                            UnifiedParams.AnisoClassWeights[2],
                            UnifiedParams.AnisoClassWeights[3]);
                        Parameters->ContinentalCrustAge = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(AgeBuffer));
                        Parameters->ContinentalRidgeDirection = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RidgeBuffer));
                        Parameters->ContinentalExemplarMetadata = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(MetadataBuffer));
                        Parameters->ContinentalTextureWidth = ExemplarTextureWidth;
                        Parameters->ContinentalTextureHeight = ExemplarTextureHeight;
                        Parameters->ContinentalLayerCount = ExemplarLayerCount;
                        Parameters->ContinentalExemplarTexture = ExemplarTextureRDG;
                        Parameters->ContinentalDebugOutput = ContinentalDebugUAV;
                        Parameters->ContinentalOutAmplified = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutputBuffer));

                        TShaderMapRef<FStageBUnifiedContinentalCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                        FComputeShaderUtils::AddPass(
                            GraphBuilder,
                            RDG_EVENT_NAME("PlanetaryCreation.StageBUnified.Continental"),
                            ComputeShader,
                            Parameters,
                            FIntVector(ContinentalGroupCountX, 1, 1));

                        GraphBuilder.QueueBufferExtraction(OutputBuffer, &ContinentalOutputBufferRef);
                    }
                }


                GraphBuilder.Execute();

                if (OceanicDebugBufferRef.IsValid() && OceanicDebugReadback.IsValid())
                {
                    FRHIBuffer* DebugRHI = OceanicDebugBufferRef->GetRHI();
                    if (DebugRHI)
                    {
                        OceanicDebugReadback->EnqueueCopy(RHICmdList, DebugRHI, sizeof(FVector4f));
                    }
                }

                if (OceanicOutputBufferRef.IsValid() && OceanicReadback.IsValid())
                {
                    FRHIBuffer* OutputRHI = OceanicOutputBufferRef->GetRHI();
                    if (OutputRHI)
                    {
                        OceanicReadback->EnqueueCopy(RHICmdList, OutputRHI, VertexCount * sizeof(float));
                        *OceanicDispatchSucceeded = true;
                    }
                }

                if (ContinentalDebugBufferRef.IsValid() && ContinentalDebugReadback.IsValid() && ContinentalWorkCount > 0)
                {
                    FRHIBuffer* DebugRHI = ContinentalDebugBufferRef->GetRHI();
                    if (DebugRHI)
                    {
                        ContinentalDebugReadback->EnqueueCopy(RHICmdList, DebugRHI, ContinentalWorkCount * sizeof(FVector4f));
                    }
                }

                if (ContinentalOutputBufferRef.IsValid() && ContinentalReadback.IsValid())
                {
                    FRHIBuffer* OutputRHI = ContinentalOutputBufferRef->GetRHI();
                    if (OutputRHI)
                    {
                        ContinentalReadback->EnqueueCopy(RHICmdList, OutputRHI, VertexCount * sizeof(float));
                        *ContinentalDispatchSucceeded = true;
                    }
                }
            });

        FlushRenderingCommands();

        const double DispatchSeconds = FPlatformTime::Seconds() - DispatchStart;

        const bool bOceanicExecuted = bDispatchOceanic && OceanicReadback.IsValid() && OceanicSnapshotPtr.IsValid() && OceanicDispatchSucceeded.IsValid() && *OceanicDispatchSucceeded;
        const bool bContinentalExecuted = bDispatchContinental && ContinentalReadback.IsValid() && ContinentalSnapshotPtr.IsValid() && ContinentalDispatchSucceeded.IsValid() && *ContinentalDispatchSucceeded;

        UE_LOG(LogPlanetaryCreation, Log,
            TEXT("[StageB][GPU] Dispatch summary | Requested O:%d C:%d | Executed O:%d C:%d | Work O:%u C:%u"),
            bDispatchOceanic ? 1 : 0,
            bDispatchContinental ? 1 : 0,
            bOceanicExecuted ? 1 : 0,
            bContinentalExecuted ? 1 : 0,
            OceanicWorkCount,
            ContinentalWorkCount);

        if (bOceanicExecuted && OceanicSnapshotPtr.IsValid())
        {
            Service.EnqueueOceanicGPUJob(OceanicReadback, OceanicDebugReadback, VertexCount, MoveTemp(*OceanicSnapshotPtr), DebugVertexIndex);
        }

        if (bContinentalExecuted && ContinentalSnapshotPtr.IsValid())
        {
            Service.EnqueueContinentalGPUJob(
                ContinentalReadback,
                ContinentalDebugReadback,
                VertexCount,
                static_cast<int32>(ContinentalWorkCount),
                ContinentalIndexData,
                MoveTemp(*ContinentalSnapshotPtr));
        }

        const double TotalWork = static_cast<double>(OceanicWorkCount + ContinentalWorkCount);
        if (DispatchSeconds > 0.0 && TotalWork > 0.0)
        {
            if (bOceanicExecuted)
            {
                const double Weight = static_cast<double>(OceanicWorkCount) / TotalWork;
                OutResult.OceanicDispatchSeconds = DispatchSeconds * Weight;
            }

            if (bContinentalExecuted)
            {
                const double Weight = static_cast<double>(ContinentalWorkCount) / TotalWork;
                OutResult.ContinentalDispatchSeconds = DispatchSeconds * Weight;
            }
        }
        else if (DispatchSeconds > 0.0)
        {
            if (bOceanicExecuted)
            {
                OutResult.OceanicDispatchSeconds = DispatchSeconds;
            }
            else if (bContinentalExecuted)
            {
                OutResult.ContinentalDispatchSeconds = DispatchSeconds;
            }
        }

        OutResult.bExecutedOceanic = bOceanicExecuted;
        OutResult.bExecutedContinental = bContinentalExecuted;

        return bOceanicExecuted || bContinentalExecuted;
    }

    bool ApplyOceanicAmplificationGPUPreview(
        UTectonicSimulationService& Service,
        FTextureRHIRef& OutHeightTexture,
        FIntPoint TextureSize,
        int32* OutLeftCoverage,
        int32* OutRightCoverage,
        int32* OutMirroredCoverage)
    {
        if (OutLeftCoverage)
        {
            *OutLeftCoverage = 0;
        }
        if (OutRightCoverage)
        {
            *OutRightCoverage = 0;
        }
        if (OutMirroredCoverage)
        {
            *OutMirroredCoverage = 0;
        }

        if (!SupportsGPUAmplification())
        {
            return false;
        }

        const TArray<float>* BaselineArray = nullptr;
        const TArray<FVector4f>* RidgeArray = nullptr;
        const TArray<float>* CrustArray = nullptr;
        const TArray<FVector3f>* PositionArray = nullptr;
        const TArray<uint32>* MaskArray = nullptr;
        Service.GetOceanicAmplificationFloatInputs(BaselineArray, RidgeArray, CrustArray, PositionArray, MaskArray);

        if (!BaselineArray || !RidgeArray || !CrustArray || !PositionArray || !MaskArray)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[StageB][GPUPreview] Float caches unavailable."));
            return false;
        }

        const int32 VertexCount = BaselineArray->Num();
        if (VertexCount <= 0 ||
            RidgeArray->Num() != VertexCount ||
            CrustArray->Num() != VertexCount ||
            PositionArray->Num() != VertexCount ||
            MaskArray->Num() != VertexCount)
        {
            UE_LOG(LogPlanetaryCreation, Warning,
                TEXT("[StageB][GPUPreview] Float cache size mismatch (Baseline=%d Ridge=%d Crust=%d Position=%d Mask=%d)."),
                BaselineArray->Num(),
                RidgeArray->Num(),
                CrustArray->Num(),
                PositionArray->Num(),
                MaskArray->Num());
            return false;
        }

        const PlanetaryCreation::StageB::FStageB_UnifiedParameters UnifiedParams = Service.GetStageBUnifiedParameters();

        // Copy inputs so the render-thread job can safely consume them after we enqueue.
        TSharedRef<TArray<float>, ESPMode::ThreadSafe> BaselineData = MakeShared<TArray<float>>(*BaselineArray);
        TSharedRef<TArray<FVector4f>, ESPMode::ThreadSafe> RidgeData = MakeShared<TArray<FVector4f>>(*RidgeArray);
        TSharedRef<TArray<float>, ESPMode::ThreadSafe> AgeData = MakeShared<TArray<float>>(*CrustArray);
        TSharedRef<TArray<FVector3f>, ESPMode::ThreadSafe> PositionData = MakeShared<TArray<FVector3f>>(*PositionArray);
        TSharedRef<TArray<uint32>, ESPMode::ThreadSafe> MaskData = MakeShared<TArray<uint32>>(*MaskArray);

        bool bPreviewExecuted = false;

        ENQUEUE_RENDER_COMMAND(PlanetaryCreation_StageBUnified_Preview)(
            [VertexCount,
             TextureSize,
             UnifiedParams,
             &OutHeightTexture,
             BaselineData,
             RidgeData,
             AgeData,
             PositionData,
             MaskData,
             &bPreviewExecuted](FRHICommandListImmediate& RHICmdList)
            {
                FRDGBuilder GraphBuilder(RHICmdList);

                FRDGBufferRef BaselineBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.PreviewBaseline"), *BaselineData);
                FRDGBufferRef RidgeBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.PreviewRidge"), *RidgeData);
                FRDGBufferRef AgeBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.PreviewAge"), *AgeData);
                FRDGBufferRef PositionBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.PreviewPosition"), *PositionData);
                FRDGBufferRef MaskBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.StageBUnified.PreviewMask"), *MaskData);

                if (!OutHeightTexture.IsValid())
                {
                    FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(
                        TEXT("PlanetaryCreation.StageBUnified.PreviewTexture"),
                        TextureSize.X,
                        TextureSize.Y,
                        PF_R16F);
                    Desc.SetNumMips(1);
                    Desc.SetNumSamples(1);
                    Desc.SetFlags(ETextureCreateFlags::UAV | ETextureCreateFlags::ShaderResource);
                    OutHeightTexture = RHICreateTexture(Desc);
                }

                FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(OutHeightTexture, TEXT("PlanetaryCreation.StageBUnified.PreviewOutput")));

                FStageBUnifiedOceanicPreviewCS::FParameters* Parameters = GraphBuilder.AllocParameters<FStageBUnifiedOceanicPreviewCS::FParameters>();
                Parameters->VertexCount = static_cast<uint32>(VertexCount);
                Parameters->TextureSize = FUintVector2(TextureSize.X, TextureSize.Y);
                Parameters->RidgeAmplitude = UnifiedParams.OceanicFaultAmplitude;
                Parameters->FaultFrequency = UnifiedParams.OceanicFaultFrequency;
                Parameters->AgeFalloff = UnifiedParams.OceanicAgeFalloff;
                Parameters->InBaseline = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(BaselineBuffer));
                Parameters->InRidgeDirection = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RidgeBuffer));
                Parameters->InCrustAge = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(AgeBuffer));
                Parameters->InRenderPosition = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(PositionBuffer));
                Parameters->InOceanicMask = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(MaskBuffer));
                Parameters->OutHeightTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputTexture));

                const int32 GroupCountX = FMath::DivideAndRoundUp(VertexCount, 64);

                GraphBuilder.AddPass(
                    RDG_EVENT_NAME("PlanetaryCreation.StageBUnified.OceanicPreview"),
                    Parameters,
                    ERDGPassFlags::Compute,
                    [Parameters, GroupCountX](FRHICommandList& RHICmdListInner)
                    {
                        TShaderMapRef<FStageBUnifiedOceanicPreviewCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                        FComputeShaderUtils::Dispatch(RHICmdListInner, ComputeShader, *Parameters, FIntVector(GroupCountX, 1, 1));
                    });

                GraphBuilder.Execute();

                bPreviewExecuted = true;
            });

        // Ensure the render-thread work is complete before we proceed.
        FlushRenderingCommands();

        if (bPreviewExecuted && (OutLeftCoverage || OutRightCoverage || OutMirroredCoverage))
        {
            int32 LeftCoverage = 0;
            int32 RightCoverage = 0;
            int32 MirroredCoverage = 0;
            ComputeSeamCoverageMetrics(*PositionData, TextureSize.X, LeftCoverage, RightCoverage, MirroredCoverage);

            if (OutLeftCoverage)
            {
                *OutLeftCoverage = LeftCoverage;
            }
            if (OutRightCoverage)
            {
                *OutRightCoverage = RightCoverage;
            }
            if (OutMirroredCoverage)
            {
                *OutMirroredCoverage = MirroredCoverage;
            }
        }

        return bPreviewExecuted;
    }
}
