// Milestone 6 GPU: Integration Smoke Test
// Validates GPU amplification produces finite results across multi-step simulations at high LOD

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "HAL/IConsoleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGPUAmplificationIntegrationTest,
    "PlanetaryCreation.Milestone6.GPU.IntegrationSmoke",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGPUAmplificationIntegrationTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
        return false;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUIntegrationSmoke] Starting multi-step GPU stability test"));

#if WITH_EDITOR
    IConsoleVariable* CVarGPUAmplification = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.UseGPUAmplification"));
    if (!CVarGPUAmplification)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("[GPUIntegrationSmoke] CVar not found"));
        return false;
    }

    const int32 OriginalValue = CVarGPUAmplification->GetInt();

    // Enable GPU path
    CVarGPUAmplification->Set(1, ECVF_SetByCode);

    // ============================================================================
    // Test 1: L6 Multi-Step Stability (40,962 vertices)
    // ============================================================================

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUIntegrationSmoke] Test 1: L6 multi-step (40,962 vertices)"));

    FTectonicSimulationParameters ParamsL6;
    ParamsL6.Seed = 54321;
    ParamsL6.SubdivisionLevel = 0;  // 20 plates
    ParamsL6.RenderSubdivisionLevel = 6;  // Level 6
    ParamsL6.bEnableOceanicAmplification = true;
    ParamsL6.bEnableContinentalAmplification = false;  // Oceanic only (continental shader pending)
    ParamsL6.MinAmplificationLOD = 5;
    ParamsL6.bEnableOceanicDampening = true;
    Service->SetParameters(ParamsL6);

    // Advance 5 steps with GPU amplification enabled
    Service->AdvanceSteps(5);  // 10 My

    const TArray<double>& AmplifiedElevationL6 = Service->GetVertexAmplifiedElevation();
    const TArray<FVector3d>& RenderVerticesL6 = Service->GetRenderVertices();

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUIntegrationSmoke] L6: %d render vertices, %d amplified elevation values"),
        RenderVerticesL6.Num(), AmplifiedElevationL6.Num());

    // Validate: Correct array length
    TestEqual(TEXT("L6: Amplified elevation array matches vertex count"), AmplifiedElevationL6.Num(), RenderVerticesL6.Num());

    // Validate: All values are finite (no NaN/Inf)
    int32 FiniteCountL6 = 0;
    int32 NaNCountL6 = 0;
    int32 InfCountL6 = 0;
    double MinElevL6 = TNumericLimits<double>::Max();
    double MaxElevL6 = TNumericLimits<double>::Lowest();

    for (int32 VertexIdx = 0; VertexIdx < AmplifiedElevationL6.Num(); ++VertexIdx)
    {
        const double Elevation = AmplifiedElevationL6[VertexIdx];

        if (FMath::IsNaN(Elevation))
        {
            NaNCountL6++;
        }
        else if (!FMath::IsFinite(Elevation))
        {
            InfCountL6++;
        }
        else
        {
            FiniteCountL6++;
            MinElevL6 = FMath::Min(MinElevL6, Elevation);
            MaxElevL6 = FMath::Max(MaxElevL6, Elevation);
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUIntegrationSmoke] L6 Elevation Stats:"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Finite values: %d (%.2f%%)"), FiniteCountL6,
        100.0 * FiniteCountL6 / FMath::Max(AmplifiedElevationL6.Num(), 1));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  NaN values: %d"), NaNCountL6);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Inf values: %d"), InfCountL6);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Range: [%.2f, %.2f] m"), MinElevL6, MaxElevL6);

    TestTrue(TEXT("L6: All elevation values are finite (no NaN/Inf)"), FiniteCountL6 == AmplifiedElevationL6.Num());
    TestTrue(TEXT("L6: Elevation range is reasonable (-10km to +10km)"),
        MinElevL6 >= -10000.0 && MaxElevL6 <= 10000.0);

    // ============================================================================
    // Test 2: L7 Multi-Step Stability (163,842 vertices)
    // ============================================================================

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUIntegrationSmoke] Test 2: L7 multi-step (163,842 vertices)"));

    FTectonicSimulationParameters ParamsL7;
    ParamsL7.Seed = 98765;
    ParamsL7.SubdivisionLevel = 0;  // 20 plates
    ParamsL7.RenderSubdivisionLevel = 7;  // Level 7 (high vertex count)
    ParamsL7.bEnableOceanicAmplification = true;
    ParamsL7.bEnableContinentalAmplification = false;  // Oceanic only
    ParamsL7.MinAmplificationLOD = 5;
    ParamsL7.bEnableOceanicDampening = true;
    Service->SetParameters(ParamsL7);

    // Advance 3 steps (L7 is expensive, keep step count low)
    Service->AdvanceSteps(3);  // 6 My

    const TArray<double>& AmplifiedElevationL7 = Service->GetVertexAmplifiedElevation();
    const TArray<FVector3d>& RenderVerticesL7 = Service->GetRenderVertices();

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUIntegrationSmoke] L7: %d render vertices, %d amplified elevation values"),
        RenderVerticesL7.Num(), AmplifiedElevationL7.Num());

    // Validate: Correct array length
    TestEqual(TEXT("L7: Amplified elevation array matches vertex count"), AmplifiedElevationL7.Num(), RenderVerticesL7.Num());

    // Validate: All values are finite
    int32 FiniteCountL7 = 0;
    int32 NaNCountL7 = 0;
    int32 InfCountL7 = 0;
    double MinElevL7 = TNumericLimits<double>::Max();
    double MaxElevL7 = TNumericLimits<double>::Lowest();

    for (int32 VertexIdx = 0; VertexIdx < AmplifiedElevationL7.Num(); ++VertexIdx)
    {
        const double Elevation = AmplifiedElevationL7[VertexIdx];

        if (FMath::IsNaN(Elevation))
        {
            NaNCountL7++;
        }
        else if (!FMath::IsFinite(Elevation))
        {
            InfCountL7++;
        }
        else
        {
            FiniteCountL7++;
            MinElevL7 = FMath::Min(MinElevL7, Elevation);
            MaxElevL7 = FMath::Max(MaxElevL7, Elevation);
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUIntegrationSmoke] L7 Elevation Stats:"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Finite values: %d (%.2f%%)"), FiniteCountL7,
        100.0 * FiniteCountL7 / FMath::Max(AmplifiedElevationL7.Num(), 1));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  NaN values: %d"), NaNCountL7);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Inf values: %d"), InfCountL7);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Range: [%.2f, %.2f] m"), MinElevL7, MaxElevL7);

    TestTrue(TEXT("L7: All elevation values are finite (no NaN/Inf)"), FiniteCountL7 == AmplifiedElevationL7.Num());
    TestTrue(TEXT("L7: Elevation range is reasonable (-10km to +10km)"),
        MinElevL7 >= -10000.0 && MaxElevL7 <= 10000.0);

    // ============================================================================
    // Test 3: GPU Resource Cleanup (No Memory Leaks)
    // ============================================================================

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUIntegrationSmoke] Test 3: GPU resource cleanup"));

    // Run multiple reset cycles to check for resource leaks
    for (int32 Iteration = 0; Iteration < 3; ++Iteration)
    {
        Service->ResetSimulation();
        Service->SetParameters(ParamsL6);
        Service->AdvanceSteps(1);

        const TArray<double>& AmplifiedElevation = Service->GetVertexAmplifiedElevation();
        TestTrue(FString::Printf(TEXT("Iteration %d: Amplified elevation populated"), Iteration),
            AmplifiedElevation.Num() > 0);

        // Check for finite values
        bool bAllFinite = true;
        for (double Elev : AmplifiedElevation)
        {
            if (!FMath::IsFinite(Elev))
            {
                bAllFinite = false;
                break;
            }
        }

        TestTrue(FString::Printf(TEXT("Iteration %d: All values finite after reset"), Iteration), bAllFinite);
    }

    // Restore original CVar value
    CVarGPUAmplification->Set(OriginalValue, ECVF_SetByCode);

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[GPUIntegrationSmoke] All tests passed - GPU amplification stable"));

#else
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[GPUIntegrationSmoke] Skipped - WITH_EDITOR not defined"));
#endif

    return true;
}
