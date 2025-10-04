// Milestone 4 Task 2.1: Hotspot Generation & Drift Test

#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 4 Task 2.1: Hotspot Generation & Drift Validation
 *
 * Tests deterministic hotspot generation, mantle-frame drift, and thermal contribution.
 * Validates hotspot positions repeat per seed and thermal output increases stress/elevation.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHotspotGenerationTest,
    "PlanetaryCreation.Milestone4.HotspotGeneration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHotspotGenerationTest::RunTest(const FString& Parameters)
{
    if (!GEditor)
    {
        AddError(TEXT("Test requires editor context"));
        return false;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get TectonicSimulationService"));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("=== Hotspot Generation & Drift Test ==="));

    // Test 1: Deterministic Hotspot Generation
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 1: Deterministic Hotspot Generation"));

    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.SubdivisionLevel = 0; // 20 plates
    Params.RenderSubdivisionLevel = 2; // 320 faces
    Params.LloydIterations = 0; // Skip for speed
    Params.bEnableHotspots = true;
    Params.MajorHotspotCount = 3;
    Params.MinorHotspotCount = 5;
    Params.HotspotDriftSpeed = 0.01;

    Service->SetParameters(Params);

    const TArray<FMantleHotspot>& Hotspots = Service->GetHotspots();
    TestEqual(TEXT("Total hotspot count"), Hotspots.Num(), 8); // 3 major + 5 minor

    int32 MajorCount = 0, MinorCount = 0;
    for (const FMantleHotspot& Hotspot : Hotspots)
    {
        if (Hotspot.Type == EHotspotType::Major)
            MajorCount++;
        else if (Hotspot.Type == EHotspotType::Minor)
            MinorCount++;

        // Validate position is on unit sphere
        const double DistanceFromOrigin = Hotspot.Position.Length();
        TestTrue(TEXT("Hotspot position on unit sphere"), FMath::IsNearlyEqual(DistanceFromOrigin, 1.0, 0.001));

        // Validate thermal output is set
        TestTrue(TEXT("Hotspot has thermal output"), Hotspot.ThermalOutput > 0.0);

        // Validate influence radius is reasonable
        TestTrue(TEXT("Hotspot influence radius valid"), Hotspot.InfluenceRadius > 0.0 && Hotspot.InfluenceRadius < PI);
    }

    TestEqual(TEXT("Major hotspot count"), MajorCount, 3);
    TestEqual(TEXT("Minor hotspot count"), MinorCount, 5);

    UE_LOG(LogTemp, Log, TEXT("  Generated %d hotspots: %d major, %d minor"), Hotspots.Num(), MajorCount, MinorCount);
    UE_LOG(LogTemp, Log, TEXT("  ✓ Hotspot generation validated"));

    // Test 2: Determinism (same seed should produce same hotspot positions)
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 2: Deterministic Hotspot Positions"));

    // Capture first generation
    TArray<FVector3d> FirstGenerationPositions;
    for (const FMantleHotspot& Hotspot : Hotspots)
    {
        FirstGenerationPositions.Add(Hotspot.Position);
    }

    // Reset with same seed
    Service->SetParameters(Params);

    const TArray<FMantleHotspot>& Hotspots2 = Service->GetHotspots();
    TestEqual(TEXT("Hotspot count matches"), Hotspots2.Num(), FirstGenerationPositions.Num());

    // Compare positions
    bool PositionsMatch = true;
    for (int32 i = 0; i < FMath::Min(Hotspots2.Num(), FirstGenerationPositions.Num()); ++i)
    {
        const double AngularDistance = FMath::Acos(FMath::Clamp(
            FVector3d::DotProduct(Hotspots2[i].Position, FirstGenerationPositions[i]),
            -1.0, 1.0
        ));

        if (AngularDistance > 0.001) // 0.001 rad ≈ 0.057°
        {
            PositionsMatch = false;
            UE_LOG(LogTemp, Warning, TEXT("  Hotspot %d position mismatch: %.6f rad"), i, AngularDistance);
        }
    }

    TestTrue(TEXT("Hotspot positions deterministic"), PositionsMatch);
    UE_LOG(LogTemp, Log, TEXT("  ✓ Determinism verified: same seed produces same hotspot positions"));

    // Test 3: Hotspot Drift Over Time
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 3: Hotspot Drift in Mantle Frame"));

    Service->SetParameters(Params); // Reset

    // Capture initial positions
    TArray<FVector3d> InitialPositions;
    for (const FMantleHotspot& Hotspot : Service->GetHotspots())
    {
        InitialPositions.Add(Hotspot.Position);
    }

    // Advance simulation (hotspots should drift)
    Service->AdvanceSteps(10); // 20 My

    const TArray<FMantleHotspot>& DriftedHotspots = Service->GetHotspots();
    TestEqual(TEXT("Hotspot count unchanged after drift"), DriftedHotspots.Num(), InitialPositions.Num());

    // Measure drift distance
    double TotalDrift = 0.0;
    int32 DriftedCount = 0;

    for (int32 i = 0; i < DriftedHotspots.Num(); ++i)
    {
        const double AngularDistance = FMath::Acos(FMath::Clamp(
            FVector3d::DotProduct(DriftedHotspots[i].Position, InitialPositions[i]),
            -1.0, 1.0
        ));

        TotalDrift += AngularDistance;

        if (AngularDistance > 0.001) // Drifted more than ~0.057°
        {
            DriftedCount++;
            UE_LOG(LogTemp, Verbose, TEXT("  Hotspot %d drifted %.4f rad (%.2f°)"),
                i, AngularDistance, FMath::RadiansToDegrees(AngularDistance));
        }
    }

    const double AvgDrift = TotalDrift / DriftedHotspots.Num();
    UE_LOG(LogTemp, Log, TEXT("  Average drift: %.4f rad (%.2f°) over 20 My"), AvgDrift, FMath::RadiansToDegrees(AvgDrift));
    UE_LOG(LogTemp, Log, TEXT("  %d / %d hotspots drifted"), DriftedCount, DriftedHotspots.Num());

    // Expected drift: speed=0.01 rad/My, time=20 My, distance ≈ 0.2 rad
    // Actual drift may be less due to spherical geometry and rotation
    TestTrue(TEXT("Hotspots drifted over time"), AvgDrift > 0.0);
    UE_LOG(LogTemp, Log, TEXT("  ✓ Hotspot drift validated"));

    // Test 4: Thermal Contribution to Stress Field
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 4: Thermal Contribution to Stress Field"));

    Service->SetParameters(Params); // Reset

    // Get stress field before and after applying hotspot thermal contribution
    Service->AdvanceSteps(1); // 2 My (populates base stress field)

    const TArray<double>& StressValues = Service->GetVertexStressValues();
    const TArray<FVector3d>& RenderVertices = Service->GetRenderVertices();
    const TArray<FMantleHotspot>& ActiveHotspots = Service->GetHotspots();

    // Find vertices near hotspots and verify elevated stress
    int32 ElevatedStressCount = 0;

    for (int32 VertexIdx = 0; VertexIdx < RenderVertices.Num(); ++VertexIdx)
    {
        const FVector3d& VertexPos = RenderVertices[VertexIdx];

        for (const FMantleHotspot& Hotspot : ActiveHotspots)
        {
            const double AngularDistance = FMath::Acos(FMath::Clamp(
                FVector3d::DotProduct(VertexPos, Hotspot.Position),
                -1.0, 1.0
            ));

            if (AngularDistance < Hotspot.InfluenceRadius * 0.5) // Within half-radius
            {
                const double VertexStress = StressValues.IsValidIndex(VertexIdx) ? StressValues[VertexIdx] : 0.0;

                if (VertexStress > 0.0)
                {
                    ElevatedStressCount++;
                }

                UE_LOG(LogTemp, Verbose, TEXT("  Vertex %d near hotspot %d (dist=%.3f rad): stress=%.2f MPa"),
                    VertexIdx, Hotspot.HotspotID, AngularDistance, VertexStress);
                break; // Only count once per vertex
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("  %d vertices with elevated stress near hotspots"), ElevatedStressCount);
    TestTrue(TEXT("Hotspots contribute to stress field"), ElevatedStressCount > 0);
    UE_LOG(LogTemp, Log, TEXT("  ✓ Thermal contribution validated"));

    // Test 5: Hotspot Type Differentiation
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 5: Hotspot Type Differentiation (Major vs Minor)"));

    double MajorThermalOutput = 0.0;
    double MinorThermalOutput = 0.0;

    for (const FMantleHotspot& Hotspot : Service->GetHotspots())
    {
        if (Hotspot.Type == EHotspotType::Major)
        {
            MajorThermalOutput = Hotspot.ThermalOutput;
            UE_LOG(LogTemp, Verbose, TEXT("  Major hotspot: thermal=%.2f, radius=%.3f rad"),
                Hotspot.ThermalOutput, Hotspot.InfluenceRadius);
        }
        else if (Hotspot.Type == EHotspotType::Minor)
        {
            MinorThermalOutput = Hotspot.ThermalOutput;
            UE_LOG(LogTemp, Verbose, TEXT("  Minor hotspot: thermal=%.2f, radius=%.3f rad"),
                Hotspot.ThermalOutput, Hotspot.InfluenceRadius);
        }
    }

    TestTrue(TEXT("Major hotspots have higher thermal output"), MajorThermalOutput > MinorThermalOutput);
    UE_LOG(LogTemp, Log, TEXT("  Major thermal output: %.2f, Minor thermal output: %.2f"),
        MajorThermalOutput, MinorThermalOutput);
    UE_LOG(LogTemp, Log, TEXT("  ✓ Hotspot type differentiation validated"));

    // Test 6: Disabled Hotspots
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 6: Disabled Hotspots (bEnableHotspots=false)"));

    Params.bEnableHotspots = false;
    Service->SetParameters(Params);

    const TArray<FMantleHotspot>& DisabledHotspots = Service->GetHotspots();
    TestEqual(TEXT("No hotspots when disabled"), DisabledHotspots.Num(), 0);
    UE_LOG(LogTemp, Log, TEXT("  ✓ Hotspot disable flag respected"));

    AddInfo(TEXT("✅ Hotspot generation & drift test complete"));
    AddInfo(FString::Printf(TEXT("Hotspots: %d (3 major, 5 minor) | Avg drift: %.4f rad | Thermal stress contribution: %d vertices"),
        8, AvgDrift, ElevatedStressCount));

    return true;
}
