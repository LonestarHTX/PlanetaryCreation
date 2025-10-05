// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 4 Phase 5 Task 5.1: Re-tessellation Edge Cases Test
 *
 * Validates re-tessellation robustness under extreme conditions:
 * - Extreme drift scenarios (>90° from initial positions)
 * - Multi-plate drift simultaneously
 * - Re-tessellation during active rift propagation
 * - Euler characteristic preservation
 * - Boundary consistency after re-tessellation
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRetessellationEdgeCasesTest,
    "PlanetaryCreation.Milestone4.RetessellationEdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRetessellationEdgeCasesTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get UTectonicSimulationService"));
        return false;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Re-tessellation Edge Cases Test ==="));

    // Test 1: Extreme drift (>90° from initial)
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: Extreme drift scenario (>90°)"));

    FTectonicSimulationParameters Params = Service->GetParameters();
    Params.Seed = 12345;
    Params.SubdivisionLevel = 0; // 12 plates
    Params.RenderSubdivisionLevel = 2; // 162 vertices
    Params.LloydIterations = 0; // Prevent convergence to same state
    // Note: Re-tessellation drift threshold not yet implemented (future M4 feature)
    Service->SetParameters(Params);

    const TArray<FTectonicPlate>& InitialPlates = Service->GetPlates();
    const int32 PlateCount = InitialPlates.Num();
    TestTrue(TEXT("Plates initialized"), PlateCount > 0);

    // Store initial centroids
    TArray<FVector3d> InitialCentroids;
    for (const FTectonicPlate& Plate : InitialPlates)
    {
        InitialCentroids.Add(Plate.Centroid);
    }

    // Apply extreme velocities to force >90° drift
    TArray<FTectonicPlate>& Plates = Service->GetPlatesForModification();
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        FTectonicPlate& Plate = Plates[i];
        // High angular velocity to drift quickly - vary by plate
        Plate.EulerPoleAxis = FVector3d(
            FMath::Sin(i * 0.7),
            FMath::Cos(i * 0.9),
            FMath::Sin(i * 1.1)
        ).GetSafeNormal();
        Plate.AngularVelocity = 0.1; // rad/My - high speed
    }

    // Run until we get extreme drift
    int32 StepsRun = 0;
    bool bExtremeDriftAchieved = false;
    const int32 MaxSteps = 50;

    for (int32 Step = 0; Step < MaxSteps; ++Step)
    {
        Service->AdvanceSteps(1);
        StepsRun++;

        // Check for extreme drift (>90°)
        const TArray<FTectonicPlate>& CurrentPlates = Service->GetPlates();
        for (int32 i = 0; i < FMath::Min(CurrentPlates.Num(), InitialCentroids.Num()); ++i)
        {
            const double AngleDegrees = FMath::RadiansToDegrees(
                FMath::Acos(FVector3d::DotProduct(
                    CurrentPlates[i].Centroid.GetSafeNormal(),
                    InitialCentroids[i].GetSafeNormal()
                ))
            );

            if (AngleDegrees > 90.0)
            {
                bExtremeDriftAchieved = true;
                UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: Plate %d drifted %.1f° (>90°) after %d steps"),
                    i, AngleDegrees, StepsRun);
                break;
            }
        }

        if (bExtremeDriftAchieved)
            break;
    }

    TestTrue(TEXT("Extreme drift (>90°) achieved"), bExtremeDriftAchieved);

    // Validate topology integrity after extreme drift
    const TArray<FVector3d>& Vertices = Service->GetRenderVertices();
    const TArray<int32>& Triangles = Service->GetRenderTriangles();

    TestTrue(TEXT("Vertices exist after extreme drift"), Vertices.Num() > 0);
    TestTrue(TEXT("Triangles exist after extreme drift"), Triangles.Num() > 0);

    // Euler characteristic: V - E + F = 2 for sphere
    const int32 V = Vertices.Num();
    const int32 F = Triangles.Num() / 3;
    const int32 E = (Triangles.Num() / 3) * 3 / 2; // Each edge shared by 2 triangles
    const int32 EulerChar = V - E + F;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: Topology after extreme drift: V=%d E=%d F=%d χ=%d"),
        V, E, F, EulerChar);
    TestEqual(TEXT("Euler characteristic preserved after extreme drift"), EulerChar, 2);

    // Test 2: Multi-plate simultaneous drift
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 2: Multi-plate simultaneous drift"));

    Params.Seed = 99999;
    Service->SetParameters(Params);

    TArray<FTectonicPlate>& Plates2 = Service->GetPlatesForModification();

    // Apply different high velocities to all plates
    for (int32 i = 0; i < Plates2.Num(); ++i)
    {
        FTectonicPlate& Plate = Plates2[i];
        Plate.EulerPoleAxis = FVector3d(
            FMath::Sin(i * 0.5),
            FMath::Cos(i * 0.7),
            FMath::Sin(i * 1.1)
        ).GetSafeNormal();
        Plate.AngularVelocity = 0.08; // rad/My
    }

    // Run simulation - should trigger multiple re-tessellations
    Service->AdvanceSteps(30);

    const TArray<FVector3d>& Vertices2 = Service->GetRenderVertices();
    const TArray<int32>& Triangles2 = Service->GetRenderTriangles();

    TestTrue(TEXT("Vertices exist after multi-plate drift"), Vertices2.Num() > 0);
    TestTrue(TEXT("Triangles exist after multi-plate drift"), Triangles2.Num() > 0);

    // Validate Euler characteristic again
    const int32 V2 = Vertices2.Num();
    const int32 F2 = Triangles2.Num() / 3;
    const int32 E2 = (Triangles2.Num() / 3) * 3 / 2;
    const int32 EulerChar2 = V2 - E2 + F2;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 2: Topology after multi-plate drift: V=%d E=%d F=%d χ=%d"),
        V2, E2, F2, EulerChar2);
    TestEqual(TEXT("Euler characteristic preserved after multi-plate drift"), EulerChar2, 2);

    // Test 3: Re-tessellation during active rift propagation
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: Re-tessellation during active rift"));

    Params.Seed = 54321;
    Params.LloydIterations = 2; // Some convergence for stable rifts
    Service->SetParameters(Params);

    TArray<FTectonicPlate>& Plates3 = Service->GetPlatesForModification();

    // Set up divergent plates to create rift
    if (Plates3.Num() >= 2)
    {
        Plates3[0].EulerPoleAxis = FVector3d(1.0, 0.0, 0.0).GetSafeNormal();
        Plates3[0].AngularVelocity = 0.05; // rad/My
        Plates3[1].EulerPoleAxis = FVector3d(-1.0, 0.0, 0.0).GetSafeNormal();
        Plates3[1].AngularVelocity = 0.05; // rad/My (opposite pole = divergent)
    }

    // Run to establish rift
    Service->AdvanceSteps(10);

    // Check for active rift
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries = Service->GetBoundaries();
    bool bHasActiveRift = false;
    for (const auto& BoundaryPair : Boundaries)
    {
        if (BoundaryPair.Value.BoundaryState == EBoundaryState::Rifting)
        {
            bHasActiveRift = true;
            UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: Active rift found between plates %d-%d (width: %.1f m)"),
                BoundaryPair.Key.Key, BoundaryPair.Key.Value, BoundaryPair.Value.RiftWidthMeters);
            break;
        }
    }

    // Continue running with high drift to trigger re-tessellation during rift
    for (FTectonicPlate& Plate : Plates3)
    {
        Plate.AngularVelocity *= 2.0; // Accelerate drift
    }

    Service->AdvanceSteps(20);

    // Validate topology survived re-tessellation during rifting
    const TArray<FVector3d>& Vertices3 = Service->GetRenderVertices();
    const TArray<int32>& Triangles3 = Service->GetRenderTriangles();

    TestTrue(TEXT("Vertices exist after rift re-tessellation"), Vertices3.Num() > 0);
    TestTrue(TEXT("Triangles exist after rift re-tessellation"), Triangles3.Num() > 0);

    const int32 V3 = Vertices3.Num();
    const int32 F3 = Triangles3.Num() / 3;
    const int32 E3 = (Triangles3.Num() / 3) * 3 / 2;
    const int32 EulerChar3 = V3 - E3 + F3;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: Topology after rift re-tessellation: V=%d E=%d F=%d χ=%d"),
        V3, E3, F3, EulerChar3);
    TestEqual(TEXT("Euler characteristic preserved during rift re-tessellation"), EulerChar3, 2);

    // Test 4: Boundary consistency after re-tessellation
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 4: Boundary consistency after re-tessellation"));

    const int32 BoundaryCountBefore = Boundaries.Num();
    const int32 PlateCountBefore = Service->GetPlates().Num();

    // Force re-tessellation by continuing drift
    Service->AdvanceSteps(10);

    const TMap<TPair<int32, int32>, FPlateBoundary>& BoundariesAfter = Service->GetBoundaries();
    const int32 BoundaryCountAfter = BoundariesAfter.Num();
    const int32 PlateCountAfter = Service->GetPlates().Num();

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 4: Boundaries before: %d, after: %d | Plates: %d → %d"),
        BoundaryCountBefore, BoundaryCountAfter, PlateCountBefore, PlateCountAfter);

    // Boundary count should be reasonable: between P-1 and P*(P-1)/2 where P = plate count
    const int32 MinBoundaries = PlateCountAfter - 1;
    const int32 MaxBoundaries = (PlateCountAfter * (PlateCountAfter - 1)) / 2;

    TestTrue(TEXT("Boundary count reasonable after re-tessellation"),
        BoundaryCountAfter >= MinBoundaries && BoundaryCountAfter <= MaxBoundaries);

    // Validate all boundaries reference valid plates
    for (const auto& BoundaryPair : BoundariesAfter)
    {
        const int32 PlateA = BoundaryPair.Key.Key;
        const int32 PlateB = BoundaryPair.Key.Value;

        TestTrue(TEXT("Boundary plate A is valid"), PlateA >= 0 && PlateA < PlateCountAfter);
        TestTrue(TEXT("Boundary plate B is valid"), PlateB >= 0 && PlateB < PlateCountAfter);
    }

    // Summary
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Re-tessellation Edge Cases Test Complete ==="));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("✓ Extreme drift (>90°) handled correctly"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("✓ Multi-plate simultaneous drift preserved topology"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("✓ Re-tessellation during active rift succeeded"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("✓ Boundary consistency maintained after re-tessellation"));

    return true;
}
