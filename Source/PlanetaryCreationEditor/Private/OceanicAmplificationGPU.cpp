#include "OceanicAmplificationGPU.h"

#include "PlanetaryCreationLogging.h"
#include "TectonicSimulationService.h"
#include "ExemplarTextureArray.h"
#include "Engine/Texture2DArray.h"

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

namespace PlanetaryCreation::GPU
{
    namespace
    {
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
            Hash = HashSnapshotMemory(Hash, &Snapshot.VertexCount, sizeof(int32));
            return Hash;
        }

        class FOceanicAmplificationCS : public FGlobalShader
        {
        public:
            DECLARE_GLOBAL_SHADER(FOceanicAmplificationCS);
            SHADER_USE_PARAMETER_STRUCT(FOceanicAmplificationCS, FGlobalShader);

            BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
                SHADER_PARAMETER(uint32, VertexCount)
                SHADER_PARAMETER(float, RidgeAmplitude)
                SHADER_PARAMETER(float, FaultFrequency)
                SHADER_PARAMETER(float, AgeFalloff)
                SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InBaseline)
                SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, InRidgeDirection)
                SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InCrustAge)
                SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector3f>, InRenderPosition)
                SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, InOceanicMask)
                SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutAmplified)
            END_SHADER_PARAMETER_STRUCT()

            static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
            {
                const ERHIFeatureLevel::Type MaxFeatureLevel = FDataDrivenShaderPlatformInfo::GetMaxFeatureLevel(Parameters.Platform);
                return MaxFeatureLevel == ERHIFeatureLevel::SM5 || MaxFeatureLevel == ERHIFeatureLevel::SM6;
            }
        };

        IMPLEMENT_GLOBAL_SHADER(FOceanicAmplificationCS, "/Plugin/PlanetaryCreation/Private/OceanicAmplification.usf", "MainCS", SF_Compute);

        // GPU Preview shader - writes to equirectangular texture instead of buffer
        class FOceanicAmplificationPreviewCS : public FGlobalShader
        {
        public:
            DECLARE_GLOBAL_SHADER(FOceanicAmplificationPreviewCS);
            SHADER_USE_PARAMETER_STRUCT(FOceanicAmplificationPreviewCS, FGlobalShader);

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

        IMPLEMENT_GLOBAL_SHADER(FOceanicAmplificationPreviewCS, "/Plugin/PlanetaryCreation/Private/OceanicAmplificationPreview.usf", "MainCS", SF_Compute);

        class FContinentalAmplificationCS : public FGlobalShader
        {
        public:
            DECLARE_GLOBAL_SHADER(FContinentalAmplificationCS);
            SHADER_USE_PARAMETER_STRUCT(FContinentalAmplificationCS, FGlobalShader);

            BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
                SHADER_PARAMETER(uint32, VertexCount)
                SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InBaseline)
                SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector3f>, InRenderPosition)
                SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, InPackedTerrainInfo)
                SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FUintVector4>, InExemplarIndices)
                SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, InExemplarWeights)
                SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector2f>, InRandomUV)
                SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector2f>, InWrappedUV)
                SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, InExemplarMetadata)
                SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray<float>, ExemplarTextures)
                SHADER_PARAMETER_SAMPLER(SamplerState, ExemplarSampler)
                SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutAmplified)
            END_SHADER_PARAMETER_STRUCT()

            static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
            {
                const ERHIFeatureLevel::Type MaxFeatureLevel = FDataDrivenShaderPlatformInfo::GetMaxFeatureLevel(Parameters.Platform);
                return MaxFeatureLevel == ERHIFeatureLevel::SM5 || MaxFeatureLevel == ERHIFeatureLevel::SM6;
            }
        };

        IMPLEMENT_GLOBAL_SHADER(FContinentalAmplificationCS, "/Plugin/PlanetaryCreation/Private/ContinentalAmplification.usf", "MainCS", SF_Compute);

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

    }

    bool ApplyOceanicAmplificationGPU(UTectonicSimulationService& Service)
    {
        if (!SupportsGPUAmplification())
        {
            return false;
        }

        const TArray<double>& Baseline = Service.GetVertexAmplifiedElevation();
        const TArray<FVector3d>& RidgeDirections = Service.GetVertexRidgeDirections();
        const TArray<double>& CrustAges = Service.GetVertexCrustAge();
        const TArray<FVector3d>& RenderVertices = Service.GetRenderVertices();

        const int32 VertexCount = Baseline.Num();
        if (VertexCount == 0 || RidgeDirections.Num() != VertexCount || CrustAges.Num() != VertexCount || RenderVertices.Num() != VertexCount)
        {
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
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[OceanicGPU] Float cache unavailable"));
            return false;
        }

        const TArray<float>& BaselineFloat = *BaselineFloatPtr;
        const TArray<FVector4f>& RidgeDirFloat = *RidgeDirFloatPtr;
        const TArray<float>& CrustAgeFloat = *CrustAgeFloatPtr;
        const TArray<FVector3f>& PositionFloat = *PositionFloatPtr;
        const TArray<uint32>& OceanicMask = *OceanicMaskPtr;

        if (BaselineFloat.Num() != VertexCount || RidgeDirFloat.Num() != VertexCount ||
            CrustAgeFloat.Num() != VertexCount || PositionFloat.Num() != VertexCount || OceanicMask.Num() != VertexCount)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[OceanicGPU] Float cache size mismatch (baseline=%d ridge=%d age=%d pos=%d mask=%d expected=%d)"),
                BaselineFloat.Num(), RidgeDirFloat.Num(), CrustAgeFloat.Num(), PositionFloat.Num(), OceanicMask.Num(), VertexCount);
            return false;
        }

        const FTectonicSimulationParameters SimParams = Service.GetParameters();
        const TArray<int32>& PlateAssignments = Service.GetVertexPlateAssignments();
        if (PlateAssignments.Num() != VertexCount)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[OceanicGPU] Plate assignment size mismatch (%d vs expected %d)"), PlateAssignments.Num(), VertexCount);
            return false;
        }

        FOceanicAmplificationSnapshot Snapshot;
        Snapshot.VertexCount = VertexCount;
        Snapshot.Parameters = SimParams;
        Snapshot.RenderLOD = SimParams.RenderSubdivisionLevel;
        Snapshot.TopologyVersion = Service.GetTopologyVersion();
        Snapshot.SurfaceVersion = Service.GetSurfaceDataVersion();
        Snapshot.SnapshotId = Service.AllocateOceanicSnapshotId();
        Snapshot.DataSerial = Service.GetOceanicAmplificationDataSerial();
        Snapshot.BaselineElevation = BaselineFloat;
        Snapshot.RidgeDirections = RidgeDirFloat;
        Snapshot.CrustAge = CrustAgeFloat;
        Snapshot.RenderPositions = PositionFloat;
        Snapshot.OceanicMask = OceanicMask;
        Snapshot.PlateAssignments = PlateAssignments;
        Snapshot.InputHash = HashOceanicSnapshot(Snapshot);
        if (!Snapshot.InputHash)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[OceanicGPU] Snapshot input hash is zero; validation safeguards may be limited this run."));
        }

        const TArray<float>* BaselineArray = BaselineFloatPtr;
        const TArray<FVector4f>* RidgeArray = RidgeDirFloatPtr;
        const TArray<float>* CrustArray = CrustAgeFloatPtr;
        const TArray<FVector3f>* PositionArray = PositionFloatPtr;
        const TArray<uint32>* MaskArray = OceanicMaskPtr;

        TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> Readback = Service.AcquireOceanicGPUReadbackBuffer();
        if (!Readback.IsValid())
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[OceanicGPU] Unable to acquire readback buffer; falling back to CPU amplification."));
            return false;
        }

        // Execute GPU compute on render thread
        ENQUEUE_RENDER_COMMAND(OceanicAmplificationGPU)(
            [BaselineArray, RidgeArray, CrustArray, PositionArray, MaskArray, Readback, VertexCount, SimParams](FRHICommandListImmediate& RHICmdList)
            {
                FRDGBuilder GraphBuilder(RHICmdList);

                FRDGBufferRef BaselineBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.OceanicGPU.Baseline"), *BaselineArray);
                FRDGBufferRef RidgeBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.OceanicGPU.Ridge"), *RidgeArray);
                FRDGBufferRef AgeBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.OceanicGPU.Age"), *CrustArray);
                FRDGBufferRef PositionBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.OceanicGPU.Position"), *PositionArray);
                FRDGBufferRef MaskBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.OceanicGPU.OceanicMask"), *MaskArray);
                FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), VertexCount), TEXT("PlanetaryCreation.OceanicGPU.Output"));

                FOceanicAmplificationCS::FParameters* Parameters = GraphBuilder.AllocParameters<FOceanicAmplificationCS::FParameters>();
                Parameters->VertexCount = static_cast<uint32>(VertexCount);
                Parameters->RidgeAmplitude = FMath::Max(static_cast<float>(SimParams.OceanicFaultAmplitude), 0.0f);
                Parameters->FaultFrequency = FMath::Max(static_cast<float>(SimParams.OceanicFaultFrequency), 0.0001f);
                Parameters->AgeFalloff = FMath::Max(static_cast<float>(SimParams.OceanicAgeFalloff), 0.0f);
                Parameters->InBaseline = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(BaselineBuffer));
                Parameters->InRidgeDirection = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RidgeBuffer));
                Parameters->InCrustAge = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(AgeBuffer));
                Parameters->InRenderPosition = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(PositionBuffer));
                Parameters->InOceanicMask = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(MaskBuffer));
                Parameters->OutAmplified = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutputBuffer));

                const int32 GroupCountX = FMath::DivideAndRoundUp(VertexCount, 64);

                GraphBuilder.AddPass(
                    RDG_EVENT_NAME("PlanetaryCreation::OceanicAmplificationGPU"),
                    Parameters,
                    ERDGPassFlags::Compute,
                    [Parameters, GroupCountX](FRHICommandList& RHICmdListInner)
                    {
                        TShaderMapRef<FOceanicAmplificationCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                        FComputeShaderUtils::Dispatch(RHICmdListInner, ComputeShader, *Parameters, FIntVector(GroupCountX, 1, 1));
                    });

                // Extract output buffer for readback
                TRefCountPtr<FRDGPooledBuffer> OutputPooledBuffer;
                GraphBuilder.QueueBufferExtraction(OutputBuffer, &OutputPooledBuffer);
                GraphBuilder.Execute();

                if (!OutputPooledBuffer.IsValid())
                {
                    UE_LOG(LogPlanetaryCreation, Error, TEXT("[OceanicGPU] Output buffer extraction failed"));
                    return;
                }

                FRHIBuffer* OutputRHI = OutputPooledBuffer->GetRHI();
                if (!OutputRHI)
                {
                    UE_LOG(LogPlanetaryCreation, Error, TEXT("[OceanicGPU] Failed to get RHI buffer"));
                    return;
                }

                if (Readback.IsValid())
                {
                    Readback->EnqueueCopy(RHICmdList, OutputRHI, VertexCount * sizeof(float));
                }
            });

        FlushRenderingCommands();

        Service.EnqueueOceanicGPUJob(Readback, VertexCount, MoveTemp(Snapshot));

        return true;
    }

    bool ApplyContinentalAmplificationGPU(UTectonicSimulationService& Service)
    {
        if (!SupportsGPUAmplification())
        {
            return false;
        }

        const FContinentalAmplificationGPUInputs& Inputs = Service.GetContinentalAmplificationGPUInputs();
        const int32 VertexCount = Inputs.BaselineElevation.Num();
        if (VertexCount == 0)
        {
            return false;
        }

        FExemplarTextureArray& ExemplarArray = GetExemplarTextureArray();
        if (!ExemplarArray.IsInitialized())
        {
            Service.InitializeGPUExemplarResources();
        }

        if (!ExemplarArray.IsInitialized())
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[ContinentalGPU] Exemplar texture array unavailable; falling back to CPU."));
            return false;
        }

        UTexture2DArray* TextureArray = ExemplarArray.GetTextureArray();
        if (!TextureArray || !TextureArray->GetResource())
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[ContinentalGPU] Texture2DArray resource is invalid."));
            return false;
        }

        const TArray<float>* BaselineArray = &Inputs.BaselineElevation;
        const TArray<FVector3f>* PositionArray = &Inputs.RenderPositions;
        const TArray<uint32>* PackedInfoArray = &Inputs.PackedTerrainInfo;
        const TArray<FUintVector4>* ExemplarIndexArray = &Inputs.ExemplarIndices;
        const TArray<FVector4f>* ExemplarWeightArray = &Inputs.ExemplarWeights;
        const TArray<FVector2f>* RandomUVArray = &Inputs.RandomUVOffsets;
        const TArray<FVector2f>* WrappedUVArray = &Inputs.WrappedUVs;

        if (BaselineArray->Num() != VertexCount || PositionArray->Num() != VertexCount ||
            PackedInfoArray->Num() != VertexCount || ExemplarIndexArray->Num() != VertexCount ||
            ExemplarWeightArray->Num() != VertexCount || RandomUVArray->Num() != VertexCount ||
            WrappedUVArray->Num() != VertexCount)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[ContinentalGPU] Input array size mismatch (baseline=%d position=%d packed=%d indices=%d weights=%d uv=%d expected=%d)"),
                BaselineArray->Num(), PositionArray->Num(), PackedInfoArray->Num(),
                ExemplarIndexArray->Num(), ExemplarWeightArray->Num(), RandomUVArray->Num(), VertexCount);
            return false;
        }

        const int32 ExemplarCount = ExemplarArray.GetExemplarCount();
        if (ExemplarCount == 0)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[ContinentalGPU] No exemplar layers available for GPU amplification."));
            return false;
        }

        TSharedRef<TArray<FVector4f>, ESPMode::ThreadSafe> ExemplarMetadataRef = MakeShared<TArray<FVector4f>, ESPMode::ThreadSafe>();
        ExemplarMetadataRef->SetNumZeroed(ExemplarCount);
        for (const FExemplarTextureArray::FExemplarInfo& Info : ExemplarArray.GetExemplarInfo())
        {
            if (ExemplarMetadataRef->IsValidIndex(Info.ArrayIndex))
            {
                (*ExemplarMetadataRef)[Info.ArrayIndex] = FVector4f(Info.ElevationMin_m, Info.ElevationMax_m, Info.ElevationMean_m, 0.0f);
            }
        }

        const FTextureRHIRef TextureRHI = TextureArray->GetResource()->TextureRHI;
        if (!TextureRHI.IsValid())
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[ContinentalGPU] Texture RHI is invalid."));
            return false;
        }

        FContinentalAmplificationSnapshot Snapshot;
        if (!Service.CreateContinentalAmplificationSnapshot(Snapshot))
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[ContinentalGPU] Failed to capture snapshot; falling back to CPU path."));
            return false;
        }

        TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> Readback = Service.AcquireContinentalGPUReadbackBuffer();
        if (!Readback.IsValid())
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[ContinentalGPU] Unable to acquire readback buffer; falling back to CPU amplification."));
            return false;
        }

        ENQUEUE_RENDER_COMMAND(ContinentalAmplificationGPU)(
            [BaselineArray, PositionArray, PackedInfoArray, ExemplarIndexArray, ExemplarWeightArray,
             RandomUVArray, WrappedUVArray, ExemplarMetadataRef, TextureRHI, Readback, VertexCount](FRHICommandListImmediate& RHICmdList)
            {
                FRDGBuilder GraphBuilder(RHICmdList);

                FRDGBufferRef BaselineBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.ContinentalGPU.Baseline"), *BaselineArray);
                FRDGBufferRef PositionBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.ContinentalGPU.Position"), *PositionArray);
                FRDGBufferRef PackedInfoBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.ContinentalGPU.PackedInfo"), *PackedInfoArray);
                FRDGBufferRef IndexBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.ContinentalGPU.Indices"), *ExemplarIndexArray);
                FRDGBufferRef WeightBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.ContinentalGPU.Weights"), *ExemplarWeightArray);
                FRDGBufferRef RandomUVBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.ContinentalGPU.RandomUV"), *RandomUVArray);
                FRDGBufferRef WrappedUVBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.ContinentalGPU.WrappedUV"), *WrappedUVArray);
                FRDGBufferRef MetadataBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.ContinentalGPU.Metadata"), *ExemplarMetadataRef);
                FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), VertexCount), TEXT("PlanetaryCreation.ContinentalGPU.Output"));

                FRDGTextureRef ExemplarTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(TextureRHI, TEXT("PlanetaryCreation.ContinentalGPU.ExemplarArray")));

                FContinentalAmplificationCS::FParameters* Parameters = GraphBuilder.AllocParameters<FContinentalAmplificationCS::FParameters>();
                Parameters->VertexCount = static_cast<uint32>(VertexCount);
                Parameters->InBaseline = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(BaselineBuffer));
                Parameters->InRenderPosition = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(PositionBuffer));
                Parameters->InPackedTerrainInfo = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(PackedInfoBuffer));
                Parameters->InExemplarIndices = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(IndexBuffer));
                Parameters->InExemplarWeights = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(WeightBuffer));
                Parameters->InRandomUV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RandomUVBuffer));
                Parameters->InWrappedUV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(WrappedUVBuffer));
                Parameters->InExemplarMetadata = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(MetadataBuffer));
                Parameters->ExemplarTextures = ExemplarTexture;
                Parameters->ExemplarSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp>::GetRHI();
                Parameters->OutAmplified = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutputBuffer));

                const int32 GroupCountX = FMath::DivideAndRoundUp(VertexCount, 64);

                GraphBuilder.AddPass(
                    RDG_EVENT_NAME("PlanetaryCreation::ContinentalAmplificationGPU"),
                    Parameters,
                    ERDGPassFlags::Compute,
                    [Parameters, GroupCountX](FRHICommandList& RHICmdListInner)
                    {
                        TShaderMapRef<FContinentalAmplificationCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                        FComputeShaderUtils::Dispatch(RHICmdListInner, ComputeShader, *Parameters, FIntVector(GroupCountX, 1, 1));
                    });

                TRefCountPtr<FRDGPooledBuffer> OutputPooledBuffer;
                GraphBuilder.QueueBufferExtraction(OutputBuffer, &OutputPooledBuffer);
                GraphBuilder.Execute();

                if (!OutputPooledBuffer.IsValid())
                {
                    UE_LOG(LogPlanetaryCreation, Error, TEXT("[ContinentalGPU] Output buffer extraction failed"));
                    return;
                }

                FRHIBuffer* OutputRHI = OutputPooledBuffer->GetRHI();
                if (!OutputRHI)
                {
                    UE_LOG(LogPlanetaryCreation, Error, TEXT("[ContinentalGPU] Failed to retrieve output buffer"));
                    return;
                }

                if (Readback.IsValid())
                {
                    Readback->EnqueueCopy(RHICmdList, OutputRHI, VertexCount * sizeof(float));
                }
            });

        FlushRenderingCommands();

        Service.EnqueueContinentalGPUJob(Readback, VertexCount, MoveTemp(Snapshot));

        return true;
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

        const TArray<double>& Baseline = Service.GetVertexAmplifiedElevation();
        const TArray<FVector3d>& RidgeDirections = Service.GetVertexRidgeDirections();
        const TArray<double>& CrustAges = Service.GetVertexCrustAge();
        const TArray<FVector3d>& RenderVertices = Service.GetRenderVertices();

        const int32 VertexCount = Baseline.Num();
        if (VertexCount == 0 || RidgeDirections.Num() != VertexCount || CrustAges.Num() != VertexCount || RenderVertices.Num() != VertexCount)
        {
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
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[OceanicGPUPreview] Float cache unavailable"));
            return false;
        }

        const TArray<float>& BaselineFloat = *BaselineFloatPtr;
        const TArray<FVector4f>& RidgeDirFloat = *RidgeDirFloatPtr;
        const TArray<float>& CrustAgeFloat = *CrustAgeFloatPtr;
        const TArray<FVector3f>& PositionFloat = *PositionFloatPtr;
        const TArray<uint32>& OceanicMask = *OceanicMaskPtr;

        if (BaselineFloat.Num() != VertexCount || RidgeDirFloat.Num() != VertexCount ||
            CrustAgeFloat.Num() != VertexCount || PositionFloat.Num() != VertexCount || OceanicMask.Num() != VertexCount)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[OceanicGPUPreview] Float cache size mismatch"));
            return false;
        }

        const TArray<float>* BaselineArray = BaselineFloatPtr;
        const TArray<FVector4f>* RidgeArray = RidgeDirFloatPtr;
        const TArray<float>* CrustArray = CrustAgeFloatPtr;
        const TArray<FVector3f>* PositionArray = PositionFloatPtr;
        const TArray<uint32>* MaskArray = OceanicMaskPtr;

        const FTectonicSimulationParameters SimParams = Service.GetParameters();

        // Execute GPU compute on render thread
        ENQUEUE_RENDER_COMMAND(OceanicAmplificationGPUPreview)(
            [BaselineArray, RidgeArray, CrustArray, PositionArray, MaskArray, &OutHeightTexture, TextureSize, VertexCount, SimParams](FRHICommandListImmediate& RHICmdList)
            {
                FRDGBuilder GraphBuilder(RHICmdList);

                // Create or validate height texture (PF_R16F format for compact storage)
                FRDGTextureRef HeightTexture;
                if (!OutHeightTexture.IsValid() || OutHeightTexture->GetSizeXY() != TextureSize)
                {
                    FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
                        TextureSize,
                        PF_R16F,
                        FClearValueBinding::Black,
                        TexCreate_ShaderResource | TexCreate_UAV);

                    HeightTexture = GraphBuilder.CreateTexture(Desc, TEXT("PlanetaryCreation.OceanicGPUPreview.HeightTexture"));
                }
                else
                {
                    HeightTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(OutHeightTexture, TEXT("PlanetaryCreation.OceanicGPUPreview.ExternalHeightTexture")));
                }

                FRDGBufferRef BaselineBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.OceanicGPUPreview.Baseline"), *BaselineArray);
                FRDGBufferRef RidgeBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.OceanicGPUPreview.Ridge"), *RidgeArray);
                FRDGBufferRef AgeBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.OceanicGPUPreview.Age"), *CrustArray);
                FRDGBufferRef PositionBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.OceanicGPUPreview.Position"), *PositionArray);
                FRDGBufferRef MaskBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.OceanicGPUPreview.OceanicMask"), *MaskArray);

                FOceanicAmplificationPreviewCS::FParameters* Parameters = GraphBuilder.AllocParameters<FOceanicAmplificationPreviewCS::FParameters>();
                Parameters->VertexCount = static_cast<uint32>(VertexCount);
                Parameters->TextureSize = FUintVector2(static_cast<uint32>(TextureSize.X), static_cast<uint32>(TextureSize.Y));
                Parameters->RidgeAmplitude = FMath::Max(static_cast<float>(SimParams.OceanicFaultAmplitude), 0.0f);
                Parameters->FaultFrequency = FMath::Max(static_cast<float>(SimParams.OceanicFaultFrequency), 0.0001f);
                Parameters->AgeFalloff = FMath::Max(static_cast<float>(SimParams.OceanicAgeFalloff), 0.0f);
                Parameters->InBaseline = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(BaselineBuffer));
                Parameters->InRidgeDirection = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RidgeBuffer));
                Parameters->InCrustAge = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(AgeBuffer));
                Parameters->InRenderPosition = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(PositionBuffer));
                Parameters->InOceanicMask = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(MaskBuffer));
                Parameters->OutHeightTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(HeightTexture));

                const int32 GroupCountX = FMath::DivideAndRoundUp(VertexCount, 64);

                GraphBuilder.AddPass(
                    RDG_EVENT_NAME("PlanetaryCreation::OceanicAmplificationGPUPreview"),
                    Parameters,
                    ERDGPassFlags::Compute,
                    [Parameters, GroupCountX](FRHICommandList& RHICmdListInner)
                    {
                        TShaderMapRef<FOceanicAmplificationPreviewCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                        FComputeShaderUtils::Dispatch(RHICmdListInner, ComputeShader, *Parameters, FIntVector(GroupCountX, 1, 1));
                    });

                // Extract height texture for persistent reference
                TRefCountPtr<IPooledRenderTarget> OutputRenderTarget;
                GraphBuilder.QueueTextureExtraction(HeightTexture, &OutputRenderTarget);
                GraphBuilder.Execute();

                if (OutputRenderTarget.IsValid())
                {
                    OutHeightTexture = OutputRenderTarget->GetRHI();
                }
            });

        FlushRenderingCommands();

        int32 LeftSeamCoverage = 0;
        int32 RightSeamCoverage = 0;
        int32 MirroredCoverage = 0;
        if (PositionArray)
        {
            ComputeSeamCoverageMetrics(*PositionArray, TextureSize.X, LeftSeamCoverage, RightSeamCoverage, MirroredCoverage);
        }

        if (OutLeftCoverage)
        {
            *OutLeftCoverage = LeftSeamCoverage;
        }
        if (OutRightCoverage)
        {
            *OutRightCoverage = RightSeamCoverage;
        }
        if (OutMirroredCoverage)
        {
            *OutMirroredCoverage = MirroredCoverage;
        }

        UE_LOG(LogPlanetaryCreation, Log, TEXT("[OceanicGPUPreview] Height texture written (%dx%d, %d vertices, SeamLeft=%d, SeamRight=%d, Mirrored=%d)"),
            TextureSize.X, TextureSize.Y, VertexCount, LeftSeamCoverage, RightSeamCoverage, MirroredCoverage);

        return true;
    }
}
