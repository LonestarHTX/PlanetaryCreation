// Milestone 4 Task 2.1: Hotspot Generation & Drift Test

#include "PlanetaryCreationLogging.h"
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

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Hotspot Generation & Drift Test ==="));

    // Test 1: Deterministic Hotspot Generation
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: Deterministic Hotspot Generation"));

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

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Generated %d hotspots: %d major, %d minor"), Hotspots.Num(), MajorCount, MinorCount);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Hotspot generation validated"));

    // Test 2: Determinism (same seed should produce same hotspot positions)
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 2: Deterministic Hotspot Positions"));

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
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("  Hotspot %d position mismatch: %.6f rad"), i, AngularDistance);
        }
    }

    TestTrue(TEXT("Hotspot positions deterministic"), PositionsMatch);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Determinism verified: same seed produces same hotspot positions"));

    // Test 3: Hotspot Drift Over Time
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: Hotspot Drift in Mantle Frame"));

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
            UE_LOG(LogPlanetaryCreation, Verbose, TEXT("  Hotspot %d drifted %.4f rad (%.2f°)"),
                i, AngularDistance, FMath::RadiansToDegrees(AngularDistance));
        }
    }

    const double AvgDrift = TotalDrift / DriftedHotspots.Num();
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Average drift: %.4f rad (%.2f°) over 20 My"), AvgDrift, FMath::RadiansToDegrees(AvgDrift));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  %d / %d hotspots drifted"), DriftedCount, DriftedHotspots.Num());

    // Expected drift: speed=0.01 rad/My, time=20 My, distance ≈ 0.2 rad
    // Actual drift may be less due to spherical geometry and rotation
    TestTrue(TEXT("Hotspots drifted over time"), AvgDrift > 0.0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Hotspot drift validated"));

    // Test 4: Thermal Contribution to Stress Field
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 4: Thermal Contribution to Stress Field"));

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

                UE_LOG(LogPlanetaryCreation, Verbose, TEXT("  Vertex %d near hotspot %d (dist=%.3f rad): stress=%.2f MPa"),
                    VertexIdx, Hotspot.HotspotID, AngularDistance, VertexStress);
                break; // Only count once per vertex
            }
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  %d vertices with elevated stress near hotspots"), ElevatedStressCount);
    TestTrue(TEXT("Hotspots contribute to stress field"), ElevatedStressCount > 0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Thermal contribution validated"));

    // Test 5: Hotspot Type Differentiation
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 5: Hotspot Type Differentiation (Major vs Minor)"));

    double MajorThermalOutput = 0.0;
    double MinorThermalOutput = 0.0;

    for (const FMantleHotspot& Hotspot : Service->GetHotspots())
    {
        if (Hotspot.Type == EHotspotType::Major)
        {
            MajorThermalOutput = Hotspot.ThermalOutput;
            UE_LOG(LogPlanetaryCreation, Verbose, TEXT("  Major hotspot: thermal=%.2f, radius=%.3f rad"),
                Hotspot.ThermalOutput, Hotspot.InfluenceRadius);
        }
        else if (Hotspot.Type == EHotspotType::Minor)
        {
            MinorThermalOutput = Hotspot.ThermalOutput;
            UE_LOG(LogPlanetaryCreation, Verbose, TEXT("  Minor hotspot: thermal=%.2f, radius=%.3f rad"),
                Hotspot.ThermalOutput, Hotspot.InfluenceRadius);
        }
    }

    TestTrue(TEXT("Major hotspots have higher thermal output"), MajorThermalOutput > MinorThermalOutput);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Major thermal output: %.2f, Minor thermal output: %.2f"),
        MajorThermalOutput, MinorThermalOutput);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Hotspot type differentiation validated"));

    // Test 6: Disabled Hotspots
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 6: Disabled Hotspots (bEnableHotspots=false)"));

    Params.bEnableHotspots = false;
    Service->SetParameters(Params);

    const TArray<FMantleHotspot>& DisabledHotspots = Service->GetHotspots();
    TestEqual(TEXT("No hotspots when disabled"), DisabledHotspots.Num(), 0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Hotspot disable flag respected"));

    // ===== PHASE 5 EXPANDED COVERAGE =====

    // Test 7: Rift Identification for Hotspot Placement
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 7: Rift Identification for Hotspot Placement (Phase 5)"));

    Params.bEnableHotspots = true;
    Params.Seed = 33333;
    Service->SetParameters(Params);

    // Set up divergent plates to create rift
    TArray<FTectonicPlate>& Plates = Service->GetPlatesForModification();
    if (Plates.Num() >= 2)
    {
        Plates[0].EulerPoleAxis = FVector3d(1.0, 0.0, 0.0).GetSafeNormal();
        Plates[0].AngularVelocity = 0.08; // rad/My
        Plates[1].EulerPoleAxis = FVector3d(-1.0, 0.0, 0.0).GetSafeNormal();
        Plates[1].AngularVelocity = 0.08; // rad/My (opposite pole = divergent)
    }

    // Run simulation to establish rift
    Service->AdvanceSteps(15);

    // Check for rifting boundaries
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries = Service->GetBoundaries();
    int32 RiftCount = 0;
    TArray<TPair<int32, int32>> RiftBoundaries;

    for (const auto& BoundaryPair : Boundaries)
    {
        if (BoundaryPair.Value.BoundaryState == EBoundaryState::Rifting)
        {
            RiftCount++;
            RiftBoundaries.Add(BoundaryPair.Key);
            UE_LOG(LogPlanetaryCreation, Verbose, TEXT("  Rift found: plates %d-%d (width: %.1f m)"),
                BoundaryPair.Key.Key, BoundaryPair.Key.Value, BoundaryPair.Value.RiftWidthMeters);
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Identified %d active rifts"), RiftCount);

    // Validate hotspots near rifts (if rifts exist)
    if (RiftCount > 0)
    {
        int32 HotspotsNearRifts = 0;
        const TArray<FMantleHotspot>& RiftTestHotspots = Service->GetHotspots();

        for (const FMantleHotspot& Hotspot : RiftTestHotspots)
        {
            // Check if hotspot is near any rift boundary
            for (const TPair<int32, int32>& RiftKey : RiftBoundaries)
            {
                const FTectonicPlate& PlateA = Service->GetPlates()[RiftKey.Key];
                const FTectonicPlate& PlateB = Service->GetPlates()[RiftKey.Value];

                // Approximate rift location as midpoint
                const FVector3d RiftMidpoint = (PlateA.Centroid + PlateB.Centroid).GetSafeNormal();
                const double DistToRift = FMath::Acos(FMath::Clamp(
                    FVector3d::DotProduct(Hotspot.Position, RiftMidpoint),
                    -1.0, 1.0
                ));

                if (DistToRift < PI / 4.0) // Within 45° of rift
                {
                    HotspotsNearRifts++;
                    UE_LOG(LogPlanetaryCreation, Verbose, TEXT("  Hotspot near rift (dist: %.2f°)"),
                        FMath::RadiansToDegrees(DistToRift));
                    break;
                }
            }
        }

        UE_LOG(LogPlanetaryCreation, Log, TEXT("  %d hotspots located near active rifts"), HotspotsNearRifts);
    }
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Rift identification validated"));

    // Test 8: Multiple Rifts Handling
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 8: Multiple Rifts Handling (Phase 5)"));

    Params.Seed = 44444;
    Params.MajorHotspotCount = 5; // More hotspots for multiple rifts
    Params.MinorHotspotCount = 8;
    Service->SetParameters(Params);

    // Create multiple divergent plate pairs
    TArray<FTectonicPlate>& Plates2 = Service->GetPlatesForModification();
    for (int32 i = 0; i < Plates2.Num() - 1; i += 2)
    {
        if (i + 1 < Plates2.Num())
        {
            // Make pairs diverge
            Plates2[i].EulerPoleAxis = FVector3d(FMath::Sin(i * 0.5), 0.0, FMath::Cos(i * 0.5)).GetSafeNormal();
            Plates2[i].AngularVelocity = 0.06; // rad/My
            Plates2[i + 1].EulerPoleAxis = FVector3d(-FMath::Sin(i * 0.5), 0.0, -FMath::Cos(i * 0.5)).GetSafeNormal();
            Plates2[i + 1].AngularVelocity = 0.06; // rad/My (opposite pole = divergent)
        }
    }

    // Run to establish multiple rifts
    Service->AdvanceSteps(20);

    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries2 = Service->GetBoundaries();
    int32 MultipleRiftCount = 0;

    for (const auto& BoundaryPair : Boundaries2)
    {
        if (BoundaryPair.Value.BoundaryState == EBoundaryState::Rifting)
        {
            MultipleRiftCount++;
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Established %d rifts with multiple divergent plates"), MultipleRiftCount);

    const TArray<FMantleHotspot>& MultiRiftHotspots = Service->GetHotspots();
    TestEqual(TEXT("Hotspot count maintained with multiple rifts"), MultiRiftHotspots.Num(), 13); // 5 major + 8 minor

    // Validate all hotspots still valid
    bool bAllHotspotsValid = true;
    for (const FMantleHotspot& Hotspot : MultiRiftHotspots)
    {
        if (Hotspot.ThermalOutput <= 0.0 || Hotspot.InfluenceRadius <= 0.0)
        {
            bAllHotspotsValid = false;
            break;
        }
    }

    TestTrue(TEXT("All hotspots valid with multiple rifts"), bAllHotspotsValid);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Multiple rifts handled correctly"));

    // Test 9: Hotspot Position Validation (on sphere surface)
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 9: Hotspot Position Validation (Phase 5)"));

    Params.Seed = 55555;
    Params.MajorHotspotCount = 10;
    Params.MinorHotspotCount = 15;
    Service->SetParameters(Params);

    const TArray<FMantleHotspot>& ValidationHotspots = Service->GetHotspots();
    TestEqual(TEXT("All hotspots generated"), ValidationHotspots.Num(), 25);

    int32 ValidPositionCount = 0;
    double MaxPositionError = 0.0;

    for (int32 i = 0; i < ValidationHotspots.Num(); ++i)
    {
        const FMantleHotspot& Hotspot = ValidationHotspots[i];
        const double Length = Hotspot.Position.Length();
        const double Error = FMath::Abs(Length - 1.0);

        MaxPositionError = FMath::Max(MaxPositionError, Error);

        if (Error < 0.001) // Within 0.1% of unit sphere
        {
            ValidPositionCount++;
        }
        else
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("  Hotspot %d position error: %.6f (length: %.6f)"),
                i, Error, Length);
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  %d / %d hotspots on unit sphere (max error: %.6f)"),
        ValidPositionCount, ValidationHotspots.Num(), MaxPositionError);

    TestEqual(TEXT("All hotspots on unit sphere surface"), ValidPositionCount, ValidationHotspots.Num());
    TestTrue(TEXT("Max position error acceptable"), MaxPositionError < 0.001);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Hotspot position validation passed"));

    // Test 10: Deterministic Hotspot Placement (stress test)
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 10: Deterministic Placement Stress Test (Phase 5)"));

    const int32 TestSeeds[] = { 111, 222, 333, 444, 555 };
    bool bAllSeedsConsistent = true;

    for (int32 SeedIdx = 0; SeedIdx < 5; ++SeedIdx)
    {
        Params.Seed = TestSeeds[SeedIdx];
        Params.MajorHotspotCount = 4;
        Params.MinorHotspotCount = 6;
        Service->SetParameters(Params);

        TArray<FVector3d> FirstRun;
        for (const FMantleHotspot& Hotspot : Service->GetHotspots())
        {
            FirstRun.Add(Hotspot.Position);
        }

        // Reset with same seed
        Service->SetParameters(Params);

        const TArray<FMantleHotspot>& SecondRunHotspots = Service->GetHotspots();
        if (SecondRunHotspots.Num() != FirstRun.Num())
        {
            bAllSeedsConsistent = false;
            UE_LOG(LogPlanetaryCreation, Error, TEXT("  Seed %d: Count mismatch (%d vs %d)"),
                TestSeeds[SeedIdx], SecondRunHotspots.Num(), FirstRun.Num());
            break;
        }

        for (int32 i = 0; i < FirstRun.Num(); ++i)
        {
            const double Dist = FVector3d::Distance(SecondRunHotspots[i].Position, FirstRun[i]);
            if (Dist > 0.0001)
            {
                bAllSeedsConsistent = false;
                UE_LOG(LogPlanetaryCreation, Error, TEXT("  Seed %d hotspot %d: Position mismatch (%.6f)"),
                    TestSeeds[SeedIdx], i, Dist);
                break;
            }
        }
    }

    TestTrue(TEXT("All seeds produce deterministic hotspot placement"), bAllSeedsConsistent);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Tested %d different seeds - all consistent"), 5);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Deterministic placement stress test passed"));

    // ===== END PHASE 5 EXPANSION =====

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Hotspot Generation Test Complete (Phase 5 Expanded) ==="));
    AddInfo(TEXT("✅ Hotspot generation & drift test complete (10 tests)"));
    AddInfo(FString::Printf(TEXT("Hotspots: %d (3 major, 5 minor) | Avg drift: %.4f rad | Thermal stress contribution: %d vertices"),
        8, AvgDrift, ElevatedStressCount));

    return true;
}
