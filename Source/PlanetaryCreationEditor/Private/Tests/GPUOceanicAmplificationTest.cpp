// Milestone 6 GPU: Oceanic Amplification GPU vs CPU Parity Test
// Validates GPU compute path produces identical results to CPU baseline within tolerance

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "HAL/IConsoleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGPUOceanicAmplificationTest,
    "PlanetaryCreation.Milestone6.GPU.OceanicParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGPUOceanicAmplificationTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
        return false;

    // ============================================================================
    // Test 1: GPU vs CPU Parity - Single L7 Step
    // ============================================================================

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUOceanicParity] Starting GPU vs CPU comparison at LOD Level 7"));

    // Setup: High LOD (Level 7 = 163,842 vertices) with oceanic amplification
    FTectonicSimulationParameters Params;
    Params.Seed = 12345;  // Fixed seed for reproducibility
    Params.SubdivisionLevel = 0;  // 20 plates
    Params.RenderSubdivisionLevel = 7;  // Level 7 for GPU stress test
    Params.bEnableOceanicAmplification = true;
    Params.MinAmplificationLOD = 5;
    Params.bEnableOceanicDampening = true;  // Required for crust age
    Service->SetParameters(Params);

    // Advance 5 steps to create oceanic crust age variation
    Service->AdvanceSteps(5);  // 10 My

    // ============================================================================
    // Run 1: CPU Baseline
    // ============================================================================

#if WITH_EDITOR
    // Disable GPU amplification via CVar
    IConsoleVariable* CVarGPUAmplification = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.UseGPUAmplification"));
    if (CVarGPUAmplification)
    {
        const int32 OriginalValue = CVarGPUAmplification->GetInt();

        // Force CPU path
        CVarGPUAmplification->Set(0, ECVF_SetByCode);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUOceanicParity] Running CPU baseline (GPU disabled)"));

        // Measure CPU performance
        const double CPUStartTime = FPlatformTime::Seconds();

        // Capture CPU-generated amplified elevation
        Service->AdvanceSteps(1);  // One more step with CPU amplification

        Service->ProcessPendingOceanicGPUReadbacks(true);

        const double CPUTime_ms = (FPlatformTime::Seconds() - CPUStartTime) * 1000.0;

        const TArray<double>& CPUAmplifiedElevation = Service->GetVertexAmplifiedElevation();
        TArray<double> CPUResults = CPUAmplifiedElevation;  // Deep copy

        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUOceanicParity] CPU baseline: %d vertices"), CPUResults.Num());

        // ============================================================================
        // Run 2: GPU Path
        // ============================================================================

        // Reset to same state (undo the single step)
        Service->ProcessPendingOceanicGPUReadbacks(true);
        Service->Undo();

        // Enable GPU path
        CVarGPUAmplification->Set(1, ECVF_SetByCode);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUOceanicParity] Running GPU compute path (GPU enabled)"));

        // Measure GPU performance
        const double GPUStartTime = FPlatformTime::Seconds();

        // Run same step with GPU amplification
        Service->AdvanceSteps(1);

        Service->ProcessPendingOceanicGPUReadbacks(true);

        const double GPUTime_ms = (FPlatformTime::Seconds() - GPUStartTime) * 1000.0;

        const TArray<double>& GPUAmplifiedElevation = Service->GetVertexAmplifiedElevation();
        TArray<double> GPUResults = GPUAmplifiedElevation;  // Deep copy

        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUOceanicParity] GPU results: %d vertices"), GPUResults.Num());

        // ============================================================================
        // Performance Comparison
        // ============================================================================

        const double Speedup = CPUTime_ms / GPUTime_ms;

        // NOTE: The current GPU implementation performs synchronous readback and therefore
        // runs slower than the CPU baseline. This test only enforces numerical parity so the
        // async/perf work can follow later (see Docs/GPU_Compute_Plan.md).
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUOceanicParity] Performance: CPU=%.2f ms, GPU=%.2f ms, Speedup=%.1fx"),
            CPUTime_ms, GPUTime_ms, Speedup);

        // Restore original CVar value
        CVarGPUAmplification->Set(OriginalValue, ECVF_SetByCode);

        // ============================================================================
        // Validation: GPU vs CPU Delta
        // ============================================================================

        TestEqual(TEXT("CPU and GPU produce same vertex count"), CPUResults.Num(), GPUResults.Num());

        if (CPUResults.Num() != GPUResults.Num())
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("[GPUOceanicParity] Vertex count mismatch! CPU=%d GPU=%d"),
                CPUResults.Num(), GPUResults.Num());
            return false;
        }

        // Tolerance: 0.1 m (per requirements)
        const double Tolerance_m = 0.1;
        int32 WithinToleranceCount = 0;
        int32 TotalOceanicVertices = 0;
        double MaxDelta_m = 0.0;
        int32 MaxDeltaIdx = INDEX_NONE;
        double MeanAbsoluteDelta_m = 0.0;

        const TArray<int32>& VertexPlateAssignments = Service->GetVertexPlateAssignments();
        const TArray<FTectonicPlate>& Plates = Service->GetPlates();

        for (int32 VertexIdx = 0; VertexIdx < CPUResults.Num(); ++VertexIdx)
        {
            const int32 PlateID = VertexPlateAssignments[VertexIdx];
            if (PlateID == INDEX_NONE || !Plates.IsValidIndex(PlateID))
                continue;

            // Only validate oceanic vertices (CPU and GPU both skip continental in oceanic amplification)
            if (Plates[PlateID].CrustType != ECrustType::Oceanic)
                continue;

            TotalOceanicVertices++;

            const double CPUElevation = CPUResults[VertexIdx];
            const double GPUElevation = GPUResults[VertexIdx];
            const double Delta = FMath::Abs(CPUElevation - GPUElevation);

            MeanAbsoluteDelta_m += Delta;

            if (Delta <= Tolerance_m)
            {
                WithinToleranceCount++;
            }

            if (Delta > MaxDelta_m)
            {
                MaxDelta_m = Delta;
                MaxDeltaIdx = VertexIdx;
            }
        }

        if (TotalOceanicVertices > 0)
        {
            MeanAbsoluteDelta_m /= TotalOceanicVertices;

            const double ParityRatio = static_cast<double>(WithinToleranceCount) / static_cast<double>(TotalOceanicVertices);

            UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUOceanicParity] Validation Results:"));
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  Total oceanic vertices: %d"), TotalOceanicVertices);
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  Within tolerance (±%.2f m): %d (%.2f%%)"),
                Tolerance_m, WithinToleranceCount, ParityRatio * 100.0);
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  Max delta: %.4f m (vertex %d)"), MaxDelta_m, MaxDeltaIdx);
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  Mean absolute delta: %.4f m"), MeanAbsoluteDelta_m);

            // Test: >99% of oceanic vertices within 0.1 m tolerance
            TestTrue(TEXT("GPU matches CPU within 0.1 m tolerance (>99% parity)"), ParityRatio >= 0.99);

            // Test: Max delta stays reasonable (< 1.0 m, allowing for minor float precision drift)
            TestTrue(TEXT("Max GPU-CPU delta stays under 1.0 m"), MaxDelta_m < 1.0);

            // Test: Mean delta is very tight (< 0.05 m)
            TestTrue(TEXT("Mean GPU-CPU delta < 0.05 m"), MeanAbsoluteDelta_m < 0.05);

            if (ParityRatio < 0.99)
            {
                UE_LOG(LogPlanetaryCreation, Error, TEXT("[GPUOceanicParity] FAILED: Only %.2f%% parity (need >99%%)"), ParityRatio * 100.0);
            }
        }
        else
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[GPUOceanicParity] No oceanic vertices found for validation"));
        }
#else
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[GPUOceanicParity] Skipped - WITH_EDITOR not defined"));
#endif
    }
    else
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("[GPUOceanicParity] CVar 'r.PlanetaryCreation.UseGPUAmplification' not found"));
        return false;
    }

    return true;
}
