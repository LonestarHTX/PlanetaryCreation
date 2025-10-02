#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

// Helper function to compute hash of plate state for determinism testing
static uint32 HashPlateState(const TArray<FTectonicPlate>& Plates)
{
    uint32 Hash = 0;
    for (const FTectonicPlate& Plate : Plates)
    {
        // Hash centroid (truncated to avoid FP jitter)
        Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Plate.Centroid.X * 1000000.0)));
        Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Plate.Centroid.Y * 1000000.0)));
        Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Plate.Centroid.Z * 1000000.0)));
        Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Plate.CrustType)));
    }
    return Hash;
}

/**
 * Test: Same seed produces identical plate layout across multiple runs.
 * Validates deterministic initialization per Milestone 2 Phase 4.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTectonicDeterminismSameSeedTest,
    "PlanetaryCreation.Tectonics.Determinism.SameSeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTectonicDeterminismSameSeedTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("Simulation service should be available"), Service);
    if (!Service)
    {
        return false;
    }

    // Configure parameters with fixed seed
    FTectonicSimulationParameters Params;
    Params.Seed = 12345;

    // First run
    Service->SetParameters(Params);
    const uint32 Hash1 = HashPlateState(Service->GetPlates());
    const int32 PlateCount1 = Service->GetPlates().Num();

    // Second run with same seed
    Service->SetParameters(Params);
    const uint32 Hash2 = HashPlateState(Service->GetPlates());
    const int32 PlateCount2 = Service->GetPlates().Num();

    // Third run with same seed
    Service->SetParameters(Params);
    const uint32 Hash3 = HashPlateState(Service->GetPlates());

    TestEqual(TEXT("Same seed should produce identical plate count"), PlateCount1, PlateCount2);
    TestEqual(TEXT("Same seed should produce identical plate state hash (run 1 vs 2)"), Hash1, Hash2);
    TestEqual(TEXT("Same seed should produce identical plate state hash (run 2 vs 3)"), Hash2, Hash3);

    return true;
}

/**
 * Test: Different seeds produce different plate layouts.
 * Validates that seed actually affects generation.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTectonicDeterminismDifferentSeedTest,
    "PlanetaryCreation.Tectonics.Determinism.DifferentSeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTectonicDeterminismDifferentSeedTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("Simulation service should be available"), Service);
    if (!Service)
    {
        return false;
    }

    // Generate with seed 42
    FTectonicSimulationParameters Params1;
    Params1.Seed = 42;
    Service->SetParameters(Params1);
    const uint32 Hash1 = HashPlateState(Service->GetPlates());

    // Generate with seed 999
    FTectonicSimulationParameters Params2;
    Params2.Seed = 999;
    Service->SetParameters(Params2);
    const uint32 Hash2 = HashPlateState(Service->GetPlates());

    TestNotEqual(TEXT("Different seeds should produce different plate layouts"), Hash1, Hash2);

    return true;
}

/**
 * Test: Stepping N times matches expected time accumulation.
 * Validates timestep integration per paper spec (2 My per step).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTectonicTimeAccumulationTest,
    "PlanetaryCreation.Tectonics.Determinism.TimeAccumulation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTectonicTimeAccumulationTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("Simulation service should be available"), Service);
    if (!Service)
    {
        return false;
    }

    // Reset to known state
    Service->ResetSimulation();
    TestEqual(TEXT("Reset should zero time"), Service->GetCurrentTimeMy(), 0.0);

    // Step 10 times
    const int32 StepCount = 10;
    const double ExpectedTime = StepCount * 2.0; // 2 My per step from paper
    Service->AdvanceSteps(StepCount);

    TestEqual(TEXT("10 steps should advance 20 My"), Service->GetCurrentTimeMy(), ExpectedTime);

    // Step 5 more times
    Service->AdvanceSteps(5);
    TestEqual(TEXT("15 total steps should advance 30 My"), Service->GetCurrentTimeMy(), 30.0);

    return true;
}

/**
 * Test: Plate count is conserved across steps.
 * Validates no plates are created/destroyed during simulation.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTectonicPlateConservationTest,
    "PlanetaryCreation.Tectonics.Determinism.PlateConservation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTectonicPlateConservationTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("Simulation service should be available"), Service);
    if (!Service)
    {
        return false;
    }

    Service->ResetSimulation();
    const int32 InitialPlateCount = Service->GetPlates().Num();

    // Run 100 steps
    Service->AdvanceSteps(100);
    const int32 FinalPlateCount = Service->GetPlates().Num();

    TestEqual(TEXT("Plate count should be conserved across 100 steps"), InitialPlateCount, FinalPlateCount);
    TestTrue(TEXT("Should have at least some plates"), FinalPlateCount > 0);

    return true;
}

/**
 * Test: Solid angle coverage remains valid across steps.
 * Validates sphere coverage conservation with tolerance for FP drift.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTectonicSolidAngleDriftTest,
    "PlanetaryCreation.Tectonics.Determinism.SolidAngleDrift",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTectonicSolidAngleDriftTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("Simulation service should be available"), Service);
    if (!Service)
    {
        return false;
    }

    Service->ResetSimulation();

    // Helper to calculate total solid angle
    auto CalculateTotalSolidAngle = [&]() -> double
    {
        double Total = 0.0;
        for (const FTectonicPlate& Plate : Service->GetPlates())
        {
            if (Plate.VertexIndices.Num() == 3)
            {
                const TArray<FVector3d>& Verts = Service->GetSharedVertices();
                const FVector3d& V0 = Verts[Plate.VertexIndices[0]];
                const FVector3d& V1 = Verts[Plate.VertexIndices[1]];
                const FVector3d& V2 = Verts[Plate.VertexIndices[2]];

                const double A = FMath::Acos(FVector3d::DotProduct(V1, V2));
                const double B = FMath::Acos(FVector3d::DotProduct(V2, V0));
                const double C = FMath::Acos(FVector3d::DotProduct(V0, V1));
                const double S = (A + B + C) / 2.0;

                const double TanQuarter = FMath::Sqrt(
                    FMath::Tan(S / 2.0) *
                    FMath::Tan((S - A) / 2.0) *
                    FMath::Tan((S - B) / 2.0) *
                    FMath::Tan((S - C) / 2.0)
                );

                Total += 4.0 * FMath::Atan(TanQuarter);
            }
        }
        return Total;
    };

    const double ExpectedSolidAngle = 4.0 * PI;
    const double InitialSolidAngle = CalculateTotalSolidAngle();
    const double InitialError = FMath::Abs(InitialSolidAngle - ExpectedSolidAngle) / ExpectedSolidAngle;

    TestTrue(TEXT("Initial solid angle should be close to 4π (within 1%)"), InitialError < 0.01);

    // Run 100 steps and check drift
    Service->AdvanceSteps(100);
    const double FinalSolidAngle = CalculateTotalSolidAngle();
    const double FinalError = FMath::Abs(FinalSolidAngle - ExpectedSolidAngle) / ExpectedSolidAngle;

    TestTrue(TEXT("Solid angle after 100 steps should remain within 1% of 4π"), FinalError < 0.01);

    // Log drift for monitoring
    AddInfo(FString::Printf(TEXT("Solid angle drift over 100 steps: %.6f%%"), FinalError * 100.0));

    return true;
}

/**
 * Test: Plate centroids remain on unit sphere across steps.
 * Validates Rodrigues rotation normalization.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTectonicCentroidNormalizationTest,
    "PlanetaryCreation.Tectonics.Determinism.CentroidNormalization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTectonicCentroidNormalizationTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("Simulation service should be available"), Service);
    if (!Service)
    {
        return false;
    }

    Service->ResetSimulation();

    // Run 100 steps
    Service->AdvanceSteps(100);

    // Check all centroids are normalized (length ≈ 1.0)
    const double Tolerance = 0.0001; // 0.01% tolerance
    for (const FTectonicPlate& Plate : Service->GetPlates())
    {
        const double Length = Plate.Centroid.Length();
        TestTrue(FString::Printf(TEXT("Plate %d centroid should remain on unit sphere (length=%.6f)"), Plate.PlateID, Length),
            FMath::IsNearlyEqual(Length, 1.0, Tolerance));
    }

    return true;
}
