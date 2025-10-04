#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 3 Task 2.3: Validate stress field accumulation (cosmetic model).
 * Tests convergent accumulation, divergent decay, and stress interpolation.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStressFieldValidation,
    "PlanetaryCreation.Milestone3.StressField",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStressFieldValidation::RunTest(const FString& Parameters)
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

    // Test at subdivision level 3 (642 vertices)
    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.RenderSubdivisionLevel = 3;
    Service->SetParameters(Params);

    const TArray<FVector3d>& RenderVertices = Service->GetRenderVertices();
    const TArray<double>& VertexStressValues = Service->GetVertexStressValues();
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries = Service->GetBoundaries();

    TestEqual(TEXT("Stress array size matches vertex count"), VertexStressValues.Num(), RenderVertices.Num());

    // ====================
    // Test 1: Initial state (no stress)
    // ====================
    double InitialMaxStress = 0.0;
    for (const auto& BoundaryPair : Boundaries)
    {
        InitialMaxStress = FMath::Max(InitialMaxStress, BoundaryPair.Value.AccumulatedStress);
    }

    TestEqual(TEXT("Initial boundary stress is zero"), InitialMaxStress, 0.0);

    int32 InitialNonZeroVertices = 0;
    for (double Stress : VertexStressValues)
    {
        if (FMath::Abs(Stress) > 1e-9)
        {
            InitialNonZeroVertices++;
        }
    }

    TestEqual(TEXT("Initial vertex stress is zero"), InitialNonZeroVertices, 0);

    // ====================
    // Test 2: Stress accumulation after steps
    // ====================
    constexpr int32 StepCount = 10; // 20 My total
    Service->AdvanceSteps(StepCount);

    const TArray<double>& UpdatedStressValues = Service->GetVertexStressValues();

    // Count boundary types and check stress behavior
    int32 ConvergentCount = 0;
    int32 DivergentCount = 0;
    int32 TransformCount = 0;
    double MaxConvergentStress = 0.0;
    double MaxDivergentStress = 0.0;

    for (const auto& BoundaryPair : Boundaries)
    {
        const FPlateBoundary& Boundary = BoundaryPair.Value;

        switch (Boundary.BoundaryType)
        {
        case EBoundaryType::Convergent:
            ConvergentCount++;
            MaxConvergentStress = FMath::Max(MaxConvergentStress, Boundary.AccumulatedStress);
            break;
        case EBoundaryType::Divergent:
            DivergentCount++;
            MaxDivergentStress = FMath::Max(MaxDivergentStress, Boundary.AccumulatedStress);
            break;
        case EBoundaryType::Transform:
            TransformCount++;
            break;
        }
    }

    AddInfo(FString::Printf(TEXT("Boundary types: %d convergent, %d divergent, %d transform"),
        ConvergentCount, DivergentCount, TransformCount));

    // Convergent boundaries should have accumulated stress
    if (ConvergentCount > 0)
    {
        TestTrue(TEXT("Convergent boundaries accumulate stress"), MaxConvergentStress > 0.0);
        AddInfo(FString::Printf(TEXT("Max convergent stress: %.2f MPa"), MaxConvergentStress));
    }

    // Divergent boundaries should have minimal stress (decay)
    AddInfo(FString::Printf(TEXT("Max divergent stress: %.2f MPa"), MaxDivergentStress));

    // ====================
    // Test 3: Stress cap (100 MPa)
    // ====================
    constexpr double MaxStressCap = 100.0; // MPa

    int32 ExceedCapCount = 0;
    for (const auto& BoundaryPair : Boundaries)
    {
        if (BoundaryPair.Value.AccumulatedStress > MaxStressCap + 1e-6)
        {
            ExceedCapCount++;
            if (ExceedCapCount <= 3)
            {
                AddError(FString::Printf(TEXT("Boundary stress exceeds cap: %.2f MPa"),
                    BoundaryPair.Value.AccumulatedStress));
            }
        }
    }

    TestEqual(TEXT("Stress capped at 100 MPa"), ExceedCapCount, 0);

    // ====================
    // Test 4: Vertex stress interpolation
    // ====================
    int32 NonZeroStressVertices = 0;
    double MaxVertexStress = 0.0;
    double TotalStress = 0.0;

    for (double Stress : UpdatedStressValues)
    {
        if (FMath::Abs(Stress) > 1e-9)
        {
            NonZeroStressVertices++;
            TotalStress += Stress;
        }
        MaxVertexStress = FMath::Max(MaxVertexStress, Stress);
    }

    TestTrue(TEXT("Some vertices have interpolated stress"), NonZeroStressVertices > 0);
    AddInfo(FString::Printf(TEXT("Vertices with stress: %d/%d (%.1f%%)"),
        NonZeroStressVertices, UpdatedStressValues.Num(),
        100.0 * NonZeroStressVertices / UpdatedStressValues.Num()));
    AddInfo(FString::Printf(TEXT("Max vertex stress: %.2f MPa, Average: %.2f MPa"),
        MaxVertexStress, TotalStress / NonZeroStressVertices));

    // Vertex stress should not exceed boundary stress (due to Gaussian falloff)
    TestTrue(TEXT("Vertex stress <= max boundary stress"), MaxVertexStress <= MaxConvergentStress + 1e-6);

    // ====================
    // Test 5: Determinism
    // ====================
    Service->SetParameters(Params); // Reset
    Service->AdvanceSteps(StepCount); // Same steps

    const TArray<double>& RegenStressValues = Service->GetVertexStressValues();

    int32 Mismatches = 0;
    for (int32 i = 0; i < UpdatedStressValues.Num(); ++i)
    {
        if (FMath::Abs(UpdatedStressValues[i] - RegenStressValues[i]) > 1e-9)
        {
            Mismatches++;
        }
    }

    TestEqual(TEXT("Stress field deterministic (same seed)"), Mismatches, 0);

    // ====================
    // Test 6: Exponential decay for divergent boundaries
    // ====================
    // Set up a boundary with initial stress and verify decay
    Service->SetParameters(Params); // Reset

    // Manually set stress on first divergent boundary for decay test
    FPlateBoundary* TestBoundary = nullptr;
    for (auto& BoundaryPair : const_cast<TMap<TPair<int32, int32>, FPlateBoundary>&>(Service->GetBoundaries()))
    {
        if (BoundaryPair.Value.BoundaryType == EBoundaryType::Divergent)
        {
            BoundaryPair.Value.AccumulatedStress = 50.0; // Initial stress
            TestBoundary = &BoundaryPair.Value;
            break;
        }
    }

    if (TestBoundary)
    {
        const double InitialStress = 50.0;
        Service->AdvanceSteps(5); // 10 My (τ = 10 My, so expect ~63% decay)

        const double DecayedStress = TestBoundary->AccumulatedStress;
        const double ExpectedStress = InitialStress * FMath::Exp(-10.0 / 10.0); // ≈ 18.4 MPa

        AddInfo(FString::Printf(TEXT("Divergent decay test: %.2f MPa → %.2f MPa (expected ≈%.2f MPa)"),
            InitialStress, DecayedStress, ExpectedStress));

        TestTrue(TEXT("Divergent stress decays exponentially"), DecayedStress < InitialStress * 0.5);
    }

    AddInfo(TEXT("=== Stress Field Validation Complete ==="));

    return true;
#else
    AddError(TEXT("Test requires WITH_EDITOR"));
    return false;
#endif
}
