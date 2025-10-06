#include "OceanicAmplificationGPU.h"

#include "PlanetaryCreationLogging.h"
#include "TectonicSimulationService.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RenderGraphUtils.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "ShaderParameterStruct.h"
#include "ShaderParameterUtils.h"
#include "VectorTypes.h"
#include "RHIGPUReadback.h"

namespace PlanetaryCreation::GPU
{
    namespace
    {
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
                return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
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
                return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
            }
        };

        IMPLEMENT_GLOBAL_SHADER(FOceanicAmplificationPreviewCS, "/Plugin/PlanetaryCreation/Private/OceanicAmplificationPreview.usf", "MainCS", SF_Compute);

        static bool SupportsGPUAmplification()
        {
            return IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5);
        }

        static void ComputeSeamCoverageMetrics(const TArray<FVector3f>& Positions, int32 TextureWidth, int32& OutLeftCoverage, int32& OutRightCoverage)
        {
            OutLeftCoverage = 0;
            OutRightCoverage = 0;

            if (TextureWidth <= 1)
            {
                return;
            }

            const int32 LastColumn = TextureWidth - 1;

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

                const float PixelPosition = U * static_cast<float>(TextureWidth);
                const int32 PixelX = FMath::Clamp(FMath::FloorToInt(PixelPosition), 0, LastColumn);

                if (PixelX <= 1)
                {
                    ++OutLeftCoverage;
                    ++OutRightCoverage;
                }
                else if (PixelX >= (LastColumn - 1))
                {
                    ++OutRightCoverage;
                    ++OutLeftCoverage;
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

        const TArray<float>* BaselineArray = BaselineFloatPtr;
        const TArray<FVector4f>* RidgeArray = RidgeDirFloatPtr;
        const TArray<float>* CrustArray = CrustAgeFloatPtr;
        const TArray<FVector3f>* PositionArray = PositionFloatPtr;
        const TArray<uint32>* MaskArray = OceanicMaskPtr;

        const FTectonicSimulationParameters SimParams = Service.GetParameters();

        TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> Readback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("PlanetaryCreation.OceanicGPU.Readback"));

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

        Service.EnqueueOceanicGPUJob(Readback, VertexCount);

        return true;
    }

    bool ApplyContinentalAmplificationGPU(UTectonicSimulationService& Service)
    {
        UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[StageB][GPU] Continental amplification GPU path not yet implemented. Falling back to CPU."));
        return false;
    }

    bool ApplyOceanicAmplificationGPUPreview(UTectonicSimulationService& Service, FTextureRHIRef& OutHeightTexture, FIntPoint TextureSize)
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
        if (PositionArray)
        {
            ComputeSeamCoverageMetrics(*PositionArray, TextureSize.X, LeftSeamCoverage, RightSeamCoverage);
        }

        UE_LOG(LogPlanetaryCreation, Log, TEXT("[OceanicGPUPreview] Height texture written (%dx%d, %d vertices, %d seam mirrors, %d edge wraps)"),
            TextureSize.X, TextureSize.Y, VertexCount, LeftSeamCoverage, RightSeamCoverage);

        return true;
    }
}
