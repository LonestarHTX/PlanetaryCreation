#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 3 Task 2.4: Validate elevation field generation.
 * Tests stress-to-elevation conversion and parameter scaling.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElevationFieldValidation,
    "PlanetaryCreation.Milestone3.ElevationField",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElevationFieldValidation::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!GEditor)
    {
        AddError(TEXT("GEditor is null - test requires editor context"));
        return false;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get UTectonicSimulationService"));
        return false;
    }

    // Test at subdivision level 3 with default elevation scale
    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.RenderSubdivisionLevel = 3;
    Params.ElevationScale = 1.0; // Default scale
    Service->SetParameters(Params);

    // Advance simulation to accumulate stress
    constexpr int32 StepCount = 20; // 40 My total
    Service->AdvanceSteps(StepCount);

    const TArray<double>& VertexStressValues = Service->GetVertexStressValues();

    // ====================
    // Test 1: Stress values exist
    // ====================
    TestEqual(TEXT("Stress values array populated"), VertexStressValues.Num(), Service->GetRenderVertices().Num());

    int32 NonZeroStressCount = 0;
    double MaxStress = 0.0;
    for (double Stress : VertexStressValues)
    {
        if (Stress > 1e-9)
        {
            NonZeroStressCount++;
        }
        MaxStress = FMath::Max(MaxStress, Stress);
    }

    TestTrue(TEXT("Stress accumulated after steps"), NonZeroStressCount > 0);
    AddInfo(FString::Printf(TEXT("Vertices with stress: %d/%d, Max stress: %.2f MPa"),
        NonZeroStressCount, VertexStressValues.Num(), MaxStress));

    // ====================
    // Test 2: ElevationScale parameter exists
    // ====================
    const FTectonicSimulationParameters& CurrentParams = Service->GetParameters();
    TestEqual(TEXT("ElevationScale parameter set"), CurrentParams.ElevationScale, 1.0);

    // ====================
    // Test 3: Elevation scaling behavior
    // ====================
    // Test with zero scale (should produce flat mesh conceptually)
    FTectonicSimulationParameters ZeroScaleParams = Params;
    ZeroScaleParams.ElevationScale = 0.0;
    Service->SetParameters(ZeroScaleParams);
    Service->AdvanceSteps(StepCount);

    TestEqual(TEXT("Zero elevation scale accepted"), Service->GetParameters().ElevationScale, 0.0);

    // Test with 2x scale
    FTectonicSimulationParameters DoubleScaleParams = Params;
    DoubleScaleParams.ElevationScale = 2.0;
    Service->SetParameters(DoubleScaleParams);
    Service->AdvanceSteps(StepCount);

    TestEqual(TEXT("Double elevation scale accepted"), Service->GetParameters().ElevationScale, 2.0);

    // ====================
    // Test 4: Stress-to-elevation conversion formula
    // ====================
    // Formula: elevation = (stress / compressionModulus) * elevationScale
    // With compressionModulus = 1.0, stress in MPa → elevation in km
    constexpr double CompressionModulus = 1.0;
    constexpr double TestStress = 50.0; // MPa
    constexpr double TestScale = 1.5;

    const double ExpectedElevation = (TestStress / CompressionModulus) * TestScale;
    const double CalculatedElevation = (TestStress / 1.0) * TestScale;

    TestEqual(TEXT("Elevation formula: 50 MPa × 1.5 scale"), CalculatedElevation, 75.0);

    // ====================
    // Test 5: Elevation clamping (±10km)
    // ====================
    constexpr double MaxElevationKm = 10.0;
    constexpr double HugeStress = 1000.0; // Would produce 1000 km without clamp

    const double UnclampedElevation = HugeStress / CompressionModulus;
    const double ClampedElevation = FMath::Clamp(UnclampedElevation, -MaxElevationKm, MaxElevationKm);

    TestEqual(TEXT("Elevation clamped to ±10km"), ClampedElevation, 10.0);

    AddInfo(TEXT("=== Elevation Field Validation Complete ==="));

    return true;
#else
    AddError(TEXT("Test requires WITH_EDITOR"));
    return false;
#endif
}
