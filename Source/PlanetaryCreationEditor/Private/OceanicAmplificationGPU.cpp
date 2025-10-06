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
                SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutAmplified)
            END_SHADER_PARAMETER_STRUCT()

            static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
            {
                return true;
            }
        };

        IMPLEMENT_GLOBAL_SHADER(FOceanicAmplificationCS, "/Plugin/PlanetaryCreation/Private/OceanicAmplification.usf", "MainCS", SF_Compute);

        static bool SupportsGPUAmplification()
        {
            return IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5);
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

        Service.GetOceanicAmplificationFloatInputs(BaselineFloatPtr, RidgeDirFloatPtr, CrustAgeFloatPtr, PositionFloatPtr);

        if (!BaselineFloatPtr || !RidgeDirFloatPtr || !CrustAgeFloatPtr || !PositionFloatPtr)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[OceanicGPU] Float cache unavailable"));
            return false;
        }

        const TArray<float>& BaselineFloat = *BaselineFloatPtr;
        const TArray<FVector4f>& RidgeDirFloat = *RidgeDirFloatPtr;
        const TArray<float>& CrustAgeFloat = *CrustAgeFloatPtr;
        const TArray<FVector3f>& PositionFloat = *PositionFloatPtr;

        if (BaselineFloat.Num() != VertexCount || RidgeDirFloat.Num() != VertexCount ||
            CrustAgeFloat.Num() != VertexCount || PositionFloat.Num() != VertexCount)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[OceanicGPU] Float cache size mismatch (baseline=%d ridge=%d age=%d pos=%d expected=%d)"),
                BaselineFloat.Num(), RidgeDirFloat.Num(), CrustAgeFloat.Num(), PositionFloat.Num(), VertexCount);
            return false;
        }

        const TArray<float>* BaselineArray = BaselineFloatPtr;
        const TArray<FVector4f>* RidgeArray = RidgeDirFloatPtr;
        const TArray<float>* CrustArray = CrustAgeFloatPtr;
        const TArray<FVector3f>* PositionArray = PositionFloatPtr;

        // Allocate output array for readback
        TArray<float> GPUResults;
        GPUResults.SetNumUninitialized(VertexCount);

        const FTectonicSimulationParameters SimParams = Service.GetParameters();

        // Execute GPU compute on render thread
        ENQUEUE_RENDER_COMMAND(OceanicAmplificationGPU)(
            [BaselineArray, RidgeArray, CrustArray, PositionArray, VertexCount, &GPUResults, SimParams](FRHICommandListImmediate& RHICmdList)
            {
                FRDGBuilder GraphBuilder(RHICmdList);

                FRDGBufferRef BaselineBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.OceanicGPU.Baseline"), *BaselineArray);
                FRDGBufferRef RidgeBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.OceanicGPU.Ridge"), *RidgeArray);
                FRDGBufferRef AgeBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.OceanicGPU.Age"), *CrustArray);
                FRDGBufferRef PositionBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("PlanetaryCreation.OceanicGPU.Position"), *PositionArray);
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

                RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);

                FRHIBuffer* OutputRHI = OutputPooledBuffer->GetRHI();
                if (!OutputRHI)
                {
                    UE_LOG(LogPlanetaryCreation, Error, TEXT("[OceanicGPU] Failed to get RHI buffer"));
                    return;
                }

                // Perform GPU readback (blocking)
                FRHIGPUBufferReadback Readback(TEXT("PlanetaryCreation.OceanicGPU.Readback"));
                Readback.EnqueueCopy(RHICmdList, OutputRHI, VertexCount * sizeof(float));
                RHICmdList.BlockUntilGPUIdle();

                const float* ReadData = static_cast<const float*>(Readback.Lock(VertexCount * sizeof(float)));
                if (!ReadData)
                {
                    Readback.Unlock();
                    UE_LOG(LogPlanetaryCreation, Error, TEXT("[OceanicGPU] Readback lock failed"));
                    return;
                }

                // Copy results to output array
                FMemory::Memcpy(GPUResults.GetData(), ReadData, VertexCount * sizeof(float));
                Readback.Unlock();
            });

        // Wait for render thread to complete
        FlushRenderingCommands();

        TArray<double>& Amplified = Service.GetMutableVertexAmplifiedElevation();
        Amplified.SetNum(VertexCount);
        for (int32 Index = 0; Index < VertexCount; ++Index)
        {
            Amplified[Index] = static_cast<double>(GPUResults[Index]);
        }

        return true;
    }

    bool ApplyContinentalAmplificationGPU(UTectonicSimulationService& Service)
    {
        UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[StageB][GPU] Continental amplification GPU path not yet implemented. Falling back to CPU."));
        return false;
    }
}
