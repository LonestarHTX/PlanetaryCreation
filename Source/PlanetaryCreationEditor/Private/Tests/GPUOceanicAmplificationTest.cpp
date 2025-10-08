// Milestone 6 GPU: Oceanic Amplification GPU vs CPU Parity Test
// Validates GPU compute path produces identical results to CPU baseline within tolerance

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

double ComputeOceanicAmplification(const FVector3d& Position, int32 PlateID, double CrustAge_My, double BaseElevation_m,
    const FVector3d& RidgeDirection, const TArray<FTectonicPlate>& Plates,
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries,
    const FTectonicSimulationParameters& Parameters);

double ComputeGaborNoiseApproximation(const FVector3d& Position, const FVector3d& FaultDirection, double Frequency);

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
        TArray<FVector3d> RidgeRun1Snapshot = Service->GetVertexRidgeDirections();

        UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUOceanicParity] CPU baseline: %d vertices"), CPUResults.Num());

#if UE_BUILD_DEVELOPMENT
        {
            const TArray<int32>& VertexPlateAssignments = Service->GetVertexPlateAssignments();
            const TArray<FTectonicPlate>& Plates = Service->GetPlates();
            const TArray<double>& BaseElevation = Service->GetVertexElevationValues();
            const TArray<double>& CrustAgeArray = Service->GetVertexCrustAge();
            const TArray<FVector3d>& RenderPositions = Service->GetRenderVertices();
            const TArray<FVector3d>& RidgeDirections = Service->GetVertexRidgeDirections();

            double MaxCpuDelta = 0.0;
            int32 MaxCpuDeltaIndex = INDEX_NONE;

            for (int32 VertexIdx = 0; VertexIdx < CPUResults.Num(); ++VertexIdx)
            {
                const int32 PlateID = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;
                if (PlateID == INDEX_NONE || !Plates.IsValidIndex(PlateID) || Plates[PlateID].CrustType != ECrustType::Oceanic)
                {
                    continue;
                }

                const double ExpectedCpu = CPUResults[VertexIdx];
                const double RecomputedCpu = ComputeOceanicAmplification(
                    RenderPositions.IsValidIndex(VertexIdx) ? RenderPositions[VertexIdx] : FVector3d::ZeroVector,
                    PlateID,
                    CrustAgeArray.IsValidIndex(VertexIdx) ? CrustAgeArray[VertexIdx] : 0.0,
                    BaseElevation.IsValidIndex(VertexIdx) ? BaseElevation[VertexIdx] : 0.0,
                    RidgeDirections.IsValidIndex(VertexIdx) ? RidgeDirections[VertexIdx] : FVector3d::ZAxisVector,
                    Plates,
                    Service->GetBoundaries(),
                    Service->GetParameters());

                const double CpuDelta = FMath::Abs(ExpectedCpu - RecomputedCpu);
                if (CpuDelta > MaxCpuDelta)
                {
                    MaxCpuDelta = CpuDelta;
                    MaxCpuDeltaIndex = VertexIdx;
                }

                if (CpuDelta > 1.0)
                {
                    UE_LOG(LogPlanetaryCreation, Warning,
                        TEXT("[GPUOceanicParity][CPUBaselineMismatch] Vertex %d Plate=%d CPUStored=%.3f Recalc=%.3f Delta=%.3f Base=%.3f Age=%.3f"),
                        VertexIdx,
                        PlateID,
                        ExpectedCpu,
                        RecomputedCpu,
                        CpuDelta,
                        BaseElevation.IsValidIndex(VertexIdx) ? BaseElevation[VertexIdx] : 0.0,
                        CrustAgeArray.IsValidIndex(VertexIdx) ? CrustAgeArray[VertexIdx] : 0.0);
                }
            }

            if (MaxCpuDeltaIndex != INDEX_NONE && MaxCpuDelta > 0.0)
            {
                UE_LOG(LogPlanetaryCreation, Log,
                    TEXT("[GPUOceanicParity] CPU baseline self-check max delta %.3f m @ vertex %d"),
                    MaxCpuDelta,
                    MaxCpuDeltaIndex);
            }
        }
#endif

        // ============================================================================
        // Run 2: GPU Path
        // ============================================================================

        // Reset to same state (undo the single step)
        Service->ProcessPendingOceanicGPUReadbacks(true);
        Service->Undo();

#if UE_BUILD_DEVELOPMENT
        // CPU determinism replay: run the same step again with CPU-only amplification and compare.
        {
            Service->ProcessPendingOceanicGPUReadbacks(true);
            const double ReplayStart = FPlatformTime::Seconds();
            Service->AdvanceSteps(1);
            Service->ProcessPendingOceanicGPUReadbacks(true);
            const double ReplayTime_ms = (FPlatformTime::Seconds() - ReplayStart) * 1000.0;

            const TArray<double>& CPUReplayAmplified = Service->GetVertexAmplifiedElevation();
            TArray<double> CPUReplayResults = CPUReplayAmplified;
            const TArray<FVector3d>& RidgeReplay = Service->GetVertexRidgeDirections();

            double MaxCpuCpuDelta = 0.0;
            int32 MaxCpuCpuIndex = INDEX_NONE;
            int32 CpuCpuMismatches = 0;

            const TArray<int32>& VertexPlateAssignments = Service->GetVertexPlateAssignments();
            const TArray<FTectonicPlate>& Plates = Service->GetPlates();

            for (int32 VertexIdx = 0; VertexIdx < CPUResults.Num() && VertexIdx < CPUReplayResults.Num(); ++VertexIdx)
            {
                const int32 PlateID = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;
                if (PlateID == INDEX_NONE || !Plates.IsValidIndex(PlateID) || Plates[PlateID].CrustType != ECrustType::Oceanic)
                {
                    continue;
                }

                const double Delta = FMath::Abs(CPUResults[VertexIdx] - CPUReplayResults[VertexIdx]);
                if (Delta > MaxCpuCpuDelta)
                {
                    MaxCpuCpuDelta = Delta;
                    MaxCpuCpuIndex = VertexIdx;
                }

                if (Delta > 1.0)
                {
                    ++CpuCpuMismatches;
                    const FVector3d* RidgeRun1Dir = RidgeRun1Snapshot.IsValidIndex(VertexIdx) ? &RidgeRun1Snapshot[VertexIdx] : nullptr;
                    const FVector3d* RidgeRun2Dir = RidgeReplay.IsValidIndex(VertexIdx) ? &RidgeReplay[VertexIdx] : nullptr;
                    UE_LOG(LogPlanetaryCreation, Warning,
                        TEXT("[GPUOceanicParity][CPUReplayMismatch] Vertex %d Plate=%d CPURun1=%.3f CPURun2=%.3f Delta=%.3f Ridge1=(%.3f,%.3f,%.3f) Ridge2=(%.3f,%.3f,%.3f)"),
                        VertexIdx,
                        PlateID,
                        CPUResults.IsValidIndex(VertexIdx) ? CPUResults[VertexIdx] : 0.0,
                        CPUReplayResults[VertexIdx],
                        Delta,
                        RidgeRun1Dir ? RidgeRun1Dir->X : 0.0,
                        RidgeRun1Dir ? RidgeRun1Dir->Y : 0.0,
                        RidgeRun1Dir ? RidgeRun1Dir->Z : 1.0,
                        RidgeRun2Dir ? RidgeRun2Dir->X : 0.0,
                        RidgeRun2Dir ? RidgeRun2Dir->Y : 0.0,
                        RidgeRun2Dir ? RidgeRun2Dir->Z : 1.0);
                }
            }

            if (MaxCpuCpuIndex != INDEX_NONE && MaxCpuCpuDelta > 0.0)
            {
                UE_LOG(LogPlanetaryCreation, Log,
                    TEXT("[GPUOceanicParity] CPU replay self-check: max delta %.3f m @ vertex %d (replay step %.2f ms, mismatches=%d)"),
                    MaxCpuCpuDelta,
                    MaxCpuCpuIndex,
                    ReplayTime_ms,
                    CpuCpuMismatches);
            }

            // Restore state for GPU run
            Service->ProcessPendingOceanicGPUReadbacks(true);
            Service->Undo();
        }
#endif

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

        const TArray<int32>& VertexPlateAssignments = Service->GetVertexPlateAssignments();
        const TArray<FTectonicPlate>& Plates = Service->GetPlates();
        const TArray<double>& BaseElevation = Service->GetVertexElevationValues();
        const TArray<double>& CrustAgeArray = Service->GetVertexCrustAge();
        const TArray<FVector3d>& RenderVertices = Service->GetRenderVertices();
        const TArray<FVector3d>& RidgeDirections = Service->GetVertexRidgeDirections();
        const TArray<float>* FloatBaseline = nullptr;
        const TArray<FVector4f>* FloatRidge = nullptr;
        const TArray<float>* FloatCrust = nullptr;
        const TArray<FVector3f>* FloatPositions = nullptr;
        const TArray<uint32>* OceanicMask = nullptr;
        Service->GetOceanicAmplificationFloatInputs(FloatBaseline, FloatRidge, FloatCrust, FloatPositions, OceanicMask);

        auto ValidateCandidate = [&](const TArray<double>& Reference, const TArray<double>& Candidate, const TCHAR* Label)
        {
            TestEqual(FString::Printf(TEXT("[%s] vertex count matches reference baseline"), Label), Candidate.Num(), Reference.Num());
            if (Candidate.Num() != Reference.Num())
            {
                return;
            }

            const double Tolerance_m = 0.1;
            int32 WithinToleranceCount = 0;
            int32 TotalOceanicVertices = 0;
            double MaxDelta_m = 0.0;
            int32 MaxDeltaIdx = INDEX_NONE;
            double MeanAbsoluteDelta_m = 0.0;

            for (int32 VertexIdx = 0; VertexIdx < CPUResults.Num(); ++VertexIdx)
            {
                const int32 PlateID = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;
                if (PlateID == INDEX_NONE || !Plates.IsValidIndex(PlateID) || Plates[PlateID].CrustType != ECrustType::Oceanic)
                {
                    continue;
                }

                ++TotalOceanicVertices;

                const double CPUElevation = Reference[VertexIdx];
                const double CandidateElevation = Candidate[VertexIdx];
                const double Delta = FMath::Abs(CPUElevation - CandidateElevation);

                MeanAbsoluteDelta_m += Delta;

                if (Delta > 1.0)
                {
                    const FVector3d Position3d = RenderVertices.IsValidIndex(VertexIdx) ? RenderVertices[VertexIdx] : FVector3d::ZeroVector;
                    const FVector3d Ridge3d = RidgeDirections.IsValidIndex(VertexIdx) ? RidgeDirections[VertexIdx] : FVector3d::ZAxisVector;
                    const double RecomputedCPU = ComputeOceanicAmplification(
                        Position3d,
                        PlateID,
                        CrustAgeArray.IsValidIndex(VertexIdx) ? CrustAgeArray[VertexIdx] : 0.0,
                        BaseElevation.IsValidIndex(VertexIdx) ? BaseElevation[VertexIdx] : 0.0,
                        Ridge3d,
                        Plates,
                        Service->GetBoundaries(),
                        Service->GetParameters());

                    const FTectonicSimulationParameters& LocalParams = Service->GetParameters();
                    const double AgeValue = CrustAgeArray.IsValidIndex(VertexIdx) ? CrustAgeArray[VertexIdx] : 0.0;
                    const double BaseValue = BaseElevation.IsValidIndex(VertexIdx) ? BaseElevation[VertexIdx] : 0.0;
                    const FVector3d UnitPosition = Position3d.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
                    const FVector3d UnitRidge = Ridge3d.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
                    FVector3d FaultDir = FVector3d::CrossProduct(UnitRidge, UnitPosition).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZeroVector);
                    if (FaultDir.IsNearlyZero(UE_DOUBLE_SMALL_NUMBER))
                    {
                        FaultDir = FVector3d::CrossProduct(UnitRidge, FVector3d::ZAxisVector).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZeroVector);
                        if (FaultDir.IsNearlyZero(UE_DOUBLE_SMALL_NUMBER))
                        {
                            FaultDir = FVector3d::CrossProduct(UnitRidge, FVector3d::YAxisVector).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZeroVector);
                            if (FaultDir.IsNearlyZero(UE_DOUBLE_SMALL_NUMBER))
                            {
                                FaultDir = FVector3d::XAxisVector;
                            }
                        }
                    }
                    const double AgeFalloff = FMath::Max(LocalParams.OceanicAgeFalloff, 0.0);
                    const double AgeFactor = (AgeFalloff > 0.0) ? FMath::Exp(-AgeValue * AgeFalloff) : 1.0;
                    const double FaultAmplitude = LocalParams.OceanicFaultAmplitude * AgeFactor;
                    const double FaultFrequency = FMath::Max(LocalParams.OceanicFaultFrequency, 0.0001);
                    const double GaborNoise = FMath::Clamp(ComputeGaborNoiseApproximation(UnitPosition, FaultDir, FaultFrequency) * 3.0, -1.0, 1.0);
                    const double FaultDetail = FaultAmplitude * GaborNoise;

                    double GradientNoise = 0.0;
                    double Frequency = 0.1;
                    double Amplitude = 1.0;
                    for (int32 Octave = 0; Octave < 4; ++Octave)
                    {
                        GradientNoise += FMath::PerlinNoise3D(FVector(UnitPosition * Frequency)) * Amplitude;
                        Frequency *= 2.0;
                        Amplitude *= 0.5;
                    }
                    const double FineDetail = 20.0 * GradientNoise;
                    const double VarianceScale = 1.5;
                    double Amplified = BaseValue + FaultDetail + FineDetail;
                    Amplified = BaseValue + (Amplified - BaseValue) * VarianceScale;
                    const double ExtraVarianceNoise = 150.0 * FMath::PerlinNoise3D(FVector(UnitPosition * 8.0) + FVector(23.17f, 42.73f, 7.91f));
                    Amplified += ExtraVarianceNoise;

                    UE_LOG(LogPlanetaryCreation, Error,
                        TEXT("[GPUOceanicParity][%s][Vertex %d] CPU=%.3f m Candidate=%.3f m Delta=%.3f m Plate=%d Age=%.3f Base=%.3f GPUModel=%.3f"),
                        Label,
                        VertexIdx,
                        CPUElevation,
                        CandidateElevation,
                        Delta,
                        PlateID,
                        CrustAgeArray.IsValidIndex(VertexIdx) ? CrustAgeArray[VertexIdx] : -1.0,
                        BaseElevation.IsValidIndex(VertexIdx) ? BaseElevation[VertexIdx] : 0.0,
                        Amplified);
                }

                if (Delta <= Tolerance_m)
                {
                    ++WithinToleranceCount;
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

                UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUOceanicParity][%s] Total oceanic vertices: %d"), Label, TotalOceanicVertices);
                UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUOceanicParity][%s] Within tolerance (±%.2f m): %d (%.2f%%)"),
                    Label, 0.1, WithinToleranceCount, ParityRatio * 100.0);
                UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUOceanicParity][%s] Max delta: %.4f m (vertex %d)"), Label, MaxDelta_m, MaxDeltaIdx);
                UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUOceanicParity][%s] Mean absolute delta: %.4f m"), Label, MeanAbsoluteDelta_m);

                TestTrue(FString::Printf(TEXT("[%s] parity ratio >= 99%%"), Label), ParityRatio >= 0.99);
                TestTrue(FString::Printf(TEXT("[%s] max delta < 1.0 m"), Label), MaxDelta_m < 1.0);
                TestTrue(FString::Printf(TEXT("[%s] mean delta < 0.05 m"), Label), MeanAbsoluteDelta_m < 0.05);

                if (ParityRatio < 0.99)
                {
                    UE_LOG(LogPlanetaryCreation, Error, TEXT("[GPUOceanicParity][%s] FAILED: Only %.2f%% parity"), Label, ParityRatio * 100.0);
                }
            }
            else
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[GPUOceanicParity][%s] No oceanic vertices found for validation"), Label);
            }
        };

        ValidateCandidate(CPUResults, GPUResults, TEXT("GPU"));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGPUOceanicDoubleDispatchTest,
    "PlanetaryCreation.Milestone6.GPU.OceanicDoubleDispatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGPUOceanicDoubleDispatchTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
    {
        return false;
    }

    Service->ResetSimulation();
    Service->ProcessPendingOceanicGPUReadbacks(true);
    Service->ProcessPendingContinentalGPUReadbacks(true);

    FTectonicSimulationParameters Params = Service->GetParameters();
    Params.RenderSubdivisionLevel = 5;
    Params.MinAmplificationLOD = 5;
    Params.bEnableOceanicAmplification = true;
    Params.bEnableContinentalAmplification = false;
    Params.bSkipCPUAmplification = false;
    Service->SetParameters(Params);

    IConsoleVariable* CVarGPUAmplification = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.UseGPUAmplification"));
    const int32 OriginalGPUValue = CVarGPUAmplification ? CVarGPUAmplification->GetInt() : 1;
    if (CVarGPUAmplification)
    {
        CVarGPUAmplification->Set(1, ECVF_SetByCode);
    }

    ON_SCOPE_EXIT
    {
        if (CVarGPUAmplification)
        {
            CVarGPUAmplification->Set(OriginalGPUValue, ECVF_SetByCode);
        }
    };

    const uint64 SerialBefore = Service->GetOceanicAmplificationDataSerial();

    Service->AdvanceSteps(1);
    const int32 PendingAfterFirst = Service->GetPendingOceanicGPUJobCount();
    TestTrue(TEXT("[GPUOceanicDoubleDispatch] Pending readback expected after first GPU dispatch"), PendingAfterFirst >= 1);

    Service->AdvanceSteps(1);
    const int32 PendingAfterSecond = Service->GetPendingOceanicGPUJobCount();
    TestTrue(TEXT("[GPUOceanicDoubleDispatch] No more than two readbacks should be live"), PendingAfterSecond <= 2);
    TestTrue(TEXT("[GPUOceanicDoubleDispatch] Readbacks should remain pending after second dispatch"), PendingAfterSecond >= 1);

    const uint64 SerialAfterDispatches = Service->GetOceanicAmplificationDataSerial();
    TestTrue(TEXT("[GPUOceanicDoubleDispatch] Oceanic data serial should advance after async CPU replay"), SerialAfterDispatches > SerialBefore);

    Service->ProcessPendingOceanicGPUReadbacks(true);
    const int32 PendingAfterDrain = Service->GetPendingOceanicGPUJobCount();
    TestEqual(TEXT("[GPUOceanicDoubleDispatch] All readbacks must drain"), PendingAfterDrain, 0);

    const uint64 SerialAfterDrain = Service->GetOceanicAmplificationDataSerial();
    TestTrue(TEXT("[GPUOceanicDoubleDispatch] Oceanic data serial remains monotonic"), SerialAfterDrain >= SerialAfterDispatches);

    return true;
}
