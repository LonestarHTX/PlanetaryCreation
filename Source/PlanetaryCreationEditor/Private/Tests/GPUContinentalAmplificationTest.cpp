// Milestone 6 GPU: Continental Amplification GPU vs CPU Parity Test
// Scaffolded placeholder until continental shader is ready (AddExpectedError to suppress failures)

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "HAL/IConsoleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGPUContinentalAmplificationTest,
    "PlanetaryCreation.Milestone6.GPU.ContinentalParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGPUContinentalAmplificationTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
        return false;

    // Setup: High LOD with continental amplification enabled
    FTectonicSimulationParameters Params;
    Params.Seed = 67890;  // Fixed seed for reproducibility
    Params.SubdivisionLevel = 0;  // 20 plates
    Params.RenderSubdivisionLevel = 7;  // Level 7 for GPU stress test
    Params.bEnableContinentalAmplification = true;
    Params.MinAmplificationLOD = 5;
    Service->SetParameters(Params);

    // Advance to create continental terrain
    Service->AdvanceSteps(10);  // 20 My

#if WITH_EDITOR
    IConsoleVariable* CVarGPUAmplification = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.UseGPUAmplification"));
    if (CVarGPUAmplification)
    {
        const int32 OriginalValue = CVarGPUAmplification->GetInt();

        // ============================================================================
        // Run 1: CPU Baseline
        // ============================================================================

        CVarGPUAmplification->Set(0, ECVF_SetByCode);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity] Running CPU baseline"));

        Service->AdvanceSteps(1);
        const TArray<double>& CPUAmplifiedElevation = Service->GetVertexAmplifiedElevation();
        TArray<double> CPUResults = CPUAmplifiedElevation;  // Deep copy

        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity] CPU baseline: %d vertices"), CPUResults.Num());

        // ============================================================================
        // Run 2: GPU Path (Will Fall Back to CPU Until Shader Ready)
        // ============================================================================

        Service->Undo();  // Reset to same state

        CVarGPUAmplification->Set(1, ECVF_SetByCode);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity] Attempting GPU path (expected to fall back to CPU)"));

        Service->AdvanceSteps(1);
        const TArray<double>& GPUAmplifiedElevation = Service->GetVertexAmplifiedElevation();
        TArray<double> GPUResults = GPUAmplifiedElevation;  // Deep copy

        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity] GPU results: %d vertices"), GPUResults.Num());

        CVarGPUAmplification->Set(OriginalValue, ECVF_SetByCode);

        // ============================================================================
        // Validation (Will Pass Once Shader Implemented)
        // ============================================================================

        TestEqual(TEXT("CPU and GPU produce same vertex count"), CPUResults.Num(), GPUResults.Num());

        if (CPUResults.Num() != GPUResults.Num())
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("[GPUContinentalParity] Vertex count mismatch"));
            return false;
        }

        // Tolerance: 0.1 m (same as oceanic test)
        const double Tolerance_m = 0.1;
        int32 WithinToleranceCount = 0;
        int32 TotalContinentalVertices = 0;
        double MaxDelta_m = 0.0;
        int32 MaxDeltaIdx = INDEX_NONE;
        double MeanAbsoluteDelta_m = 0.0;

        const TArray<int32>& VertexPlateAssignments = Service->GetVertexPlateAssignments();
        const TArray<FTectonicPlate>& Plates = Service->GetPlates();

        int32 LoggedMismatches = 0;
        const TArray<double>& BaselineElevation = Service->GetVertexElevationValues();

        for (int32 VertexIdx = 0; VertexIdx < CPUResults.Num(); ++VertexIdx)
        {
            const int32 PlateID = VertexPlateAssignments[VertexIdx];
            if (PlateID == INDEX_NONE || !Plates.IsValidIndex(PlateID))
                continue;

            // Only validate continental vertices
            if (Plates[PlateID].CrustType != ECrustType::Continental)
                continue;

            TotalContinentalVertices++;

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

#if UE_BUILD_DEVELOPMENT
            if (Delta > 1.0 && LoggedMismatches < 5)
            {
                const double BaselineValue = BaselineElevation.IsValidIndex(VertexIdx) ? BaselineElevation[VertexIdx] : 0.0;
                UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity][Diff] Vtx=%d Plate=%d Base=%.2f CPU=%.2f GPU=%.2f Delta=%.2f"),
                    VertexIdx, PlateID, BaselineValue, CPUElevation, GPUElevation, Delta);
                ++LoggedMismatches;
            }
#endif
        }

        if (TotalContinentalVertices > 0)
        {
            MeanAbsoluteDelta_m /= TotalContinentalVertices;

            const double ParityRatio = static_cast<double>(WithinToleranceCount) / static_cast<double>(TotalContinentalVertices);

            UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity] Validation Results:"));
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  Total continental vertices: %d"), TotalContinentalVertices);
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  Within tolerance (±%.2f m): %d (%.2f%%)"),
                Tolerance_m, WithinToleranceCount, ParityRatio * 100.0);
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  Max delta: %.4f m (vertex %d)"), MaxDelta_m, MaxDeltaIdx);
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  Mean absolute delta: %.4f m"), MeanAbsoluteDelta_m);

            TestTrue(TEXT("GPU matches CPU within 0.1 m tolerance (>99% parity)"), ParityRatio >= 0.99);
            TestTrue(TEXT("Max GPU-CPU delta stays under 1.0 m"), MaxDelta_m < 1.0);
            TestTrue(TEXT("Mean GPU-CPU delta < 0.05 m"), MeanAbsoluteDelta_m < 0.05);
        }
        else
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[GPUContinentalParity] No continental vertices found"));
        }
#else
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[GPUContinentalParity] Skipped - WITH_EDITOR not defined"));
#endif
    }
    else
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("[GPUContinentalParity] CVar not found"));
        return false;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity] Test complete"));
    return true;
}
