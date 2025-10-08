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

        // Helper lambda for parity analysis
        auto CompareAgainstBaseline = [&](const TArray<double>& Baseline, const TArray<double>& Candidate, const TCHAR* Label, bool bExpectParity)
        {
            TestEqual(FString::Printf(TEXT("[%s] Vertex count matches baseline"), Label), Candidate.Num(), Baseline.Num());
            if (Candidate.Num() != Baseline.Num())
            {
                return;
            }

            const double Tolerance_m = 0.1;
            const TArray<int32>& VertexPlateAssignments = Service->GetVertexPlateAssignments();
            const TArray<FTectonicPlate>& Plates = Service->GetPlates();
            const TArray<double>& BaselineElevation = Service->GetVertexElevationValues();

            int32 TotalContinentalVertices = 0;
            int32 WithinToleranceCount = 0;
            double MaxDelta_m = 0.0;
            int32 MaxDeltaIdx = INDEX_NONE;
            double MeanAbsoluteDelta_m = 0.0;
            int32 LoggedMismatches = 0;

            for (int32 VertexIdx = 0; VertexIdx < Candidate.Num(); ++VertexIdx)
            {
                const int32 PlateID = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;
                if (PlateID == INDEX_NONE || !Plates.IsValidIndex(PlateID) || Plates[PlateID].CrustType != ECrustType::Continental)
                {
                    continue;
                }

                ++TotalContinentalVertices;

                const double CPUElevation = Baseline[VertexIdx];
                const double CandidateElevation = Candidate[VertexIdx];
                const double Delta = FMath::Abs(CPUElevation - CandidateElevation);
                MeanAbsoluteDelta_m += Delta;

                if (Delta <= Tolerance_m)
                {
                    ++WithinToleranceCount;
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
                    UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity][%s][Diff] Vtx=%d Plate=%d Base=%.2f CPU=%.2f Candidate=%.2f Delta=%.2f"),
                        Label,
                        VertexIdx,
                        PlateID,
                        BaselineValue,
                        CPUElevation,
                        CandidateElevation,
                        Delta);
                    ++LoggedMismatches;
                }
#endif
            }

            if (TotalContinentalVertices > 0)
            {
                MeanAbsoluteDelta_m /= TotalContinentalVertices;
                const double ParityRatio = static_cast<double>(WithinToleranceCount) / static_cast<double>(TotalContinentalVertices);

                UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity][%s] Total continental vertices: %d"), Label, TotalContinentalVertices);
                UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity][%s] Within ±%.2f m: %d (%.2f%%)"),
                    Label, Tolerance_m, WithinToleranceCount, ParityRatio * 100.0);
                UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity][%s] Max delta: %.4f m (vertex %d)"),
                    Label, MaxDelta_m, MaxDeltaIdx);
                UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity][%s] Mean absolute delta: %.4f m"),
                    Label, MeanAbsoluteDelta_m);

                if (bExpectParity)
                {
                    TestTrue(FString::Printf(TEXT("[%s] Parity ratio >= 99%%"), Label), ParityRatio >= 0.99);
                    TestTrue(FString::Printf(TEXT("[%s] Max delta < 1.0 m"), Label), MaxDelta_m < 1.0);
                    TestTrue(FString::Printf(TEXT("[%s] Mean delta < 0.05 m"), Label), MeanAbsoluteDelta_m < 0.05);
                }
                else
                {
                    TestTrue(FString::Printf(TEXT("[%s] Drift fallback produced non-trivial deltas"), Label), MaxDelta_m > Tolerance_m);
                    TestTrue(FString::Printf(TEXT("[%s] Mean delta is finite"), Label), FMath::IsFinite(MeanAbsoluteDelta_m));
                }
            }
            else
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[GPUContinentalParity][%s] No continental vertices found"), Label);
            }
        };

        // ========================================================================
        // Baseline: CPU only
        // ========================================================================
        CVarGPUAmplification->Set(0, ECVF_SetByCode);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity] Running CPU baseline pass"));
        Service->AdvanceSteps(1);
        const TArray<double> CPUResults = Service->GetVertexAmplifiedElevation();
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity] CPU baseline captured (%d vertices)"), CPUResults.Num());
        Service->Undo();
        Service->ResetAmplifiedElevationForTests();
        Service->ResetAmplifiedElevationForTests();

        // ========================================================================
        // Snapshot-backed GPU path
        // ========================================================================
        CVarGPUAmplification->Set(1, ECVF_SetByCode);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity] Dispatching GPU continental amplification (snapshot path)"));

        const bool bSnapshotDispatch = Service->ApplyContinentalAmplificationGPU();
        TestTrue(TEXT("ApplyContinentalAmplificationGPU (snapshot) succeeded"), bSnapshotDispatch);
        if (!bSnapshotDispatch)
        {
            CVarGPUAmplification->Set(OriginalValue, ECVF_SetByCode);
            return false;
        }

        Service->ProcessPendingContinentalGPUReadbacks(true);
        const TArray<double> SnapshotResults = Service->GetVertexAmplifiedElevation();
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity] Snapshot-backed results captured (%d vertices)"), SnapshotResults.Num());

        CompareAgainstBaseline(CPUResults, SnapshotResults, TEXT("Snapshot"), /*bExpectParity*/ true);

        Service->Undo();

        // ========================================================================
        // Drift scenario: force snapshot hash mismatch and ensure fallback works
        // ========================================================================
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity] Dispatching GPU continental amplification (drift fallback)"));

        Service->ResetAmplifiedElevationForTests();
        CVarGPUAmplification->Set(0, ECVF_SetByCode);
        Service->AdvanceSteps(1);
        Service->ProcessPendingContinentalGPUReadbacks(true);
        const TArray<double> FallbackBaseline = Service->GetVertexAmplifiedElevation();
        Service->Undo();

        CVarGPUAmplification->Set(1, ECVF_SetByCode);

        const bool bFallbackDispatch = Service->ApplyContinentalAmplificationGPU();
        TestTrue(TEXT("ApplyContinentalAmplificationGPU (fallback) succeeded"), bFallbackDispatch);
        if (!bFallbackDispatch)
        {
            CVarGPUAmplification->Set(OriginalValue, ECVF_SetByCode);
            return false;
        }

        // Simulate drift between dispatch and readback so the snapshot path is rejected.
        Service->ForceContinentalSnapshotSerialDrift();

        // Reinitialize amplified elevations so fallback recomputes from the proper baseline.
        Service->ProcessPendingContinentalGPUReadbacks(true);
        const TArray<double> FallbackResults = Service->GetVertexAmplifiedElevation();
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUContinentalParity] Fallback results captured (%d vertices)"), FallbackResults.Num());

        CompareAgainstBaseline(FallbackBaseline, FallbackResults, TEXT("Fallback"), /*bExpectParity*/ true);

        CVarGPUAmplification->Set(OriginalValue, ECVF_SetByCode);
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
