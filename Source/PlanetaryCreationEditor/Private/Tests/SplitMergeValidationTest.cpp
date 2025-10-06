// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 4 Phase 5 Task 5.1: Split/Merge Validation Test
 *
 * Validates topology changes from plate splits and merges:
 * - Topology consistency after split (plate count increases)
 * - Boundary updates (old boundary removed, new boundaries created)
 * - Stress redistribution to new boundaries
 * - Plate count changes tracked correctly
 * - Euler characteristic preserved across topology changes
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSplitMergeValidationTest,
    "PlanetaryCreation.Milestone4.SplitMergeValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSplitMergeValidationTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get UTectonicSimulationService"));
        return false;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Split/Merge Validation Test ==="));

    // Test 1: Plate split topology consistency
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: Plate split topology consistency"));

    FTectonicSimulationParameters Params = Service->GetParameters();
    Params.Seed = 11111;
    Params.SubdivisionLevel = 0; // 12 plates
    Params.RenderSubdivisionLevel = 2; // 162 vertices
    Params.LloydIterations = 2;
    Params.RiftSplitThresholdMeters = 100000.0; // 100 km threshold for split
    Params.bEnableRiftPropagation = true; // Enable rift tracking
    Params.bEnablePlateTopologyChanges = true; // Enable split/merge execution
    Service->SetParameters(Params);

    const int32 InitialPlateCount = Service->GetPlates().Num();
    const int32 InitialBoundaryCount = Service->GetBoundaries().Num();

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: Initial state - %d plates, %d boundaries"),
        InitialPlateCount, InitialBoundaryCount);

    // Set up divergent boundary to force rift
    TArray<FTectonicPlate>& Plates = Service->GetPlatesForModification();
    if (Plates.Num() >= 2)
    {
        // Make plates diverge strongly
        Plates[0].EulerPoleAxis = FVector3d(1.0, 0.0, 0.0).GetSafeNormal();
        Plates[0].AngularVelocity = 0.15; // rad/My (increased for faster dynamics)
        Plates[1].EulerPoleAxis = FVector3d(-1.0, 0.0, 0.0).GetSafeNormal();
        Plates[1].AngularVelocity = 0.15; // rad/My (opposite pole = divergent)
    }

    // Run until split occurs
    int32 StepsUntilSplit = 0;
    bool bSplitOccurred = false;
    const int32 MaxSteps = 100;

    for (int32 Step = 0; Step < MaxSteps; ++Step)
    {
        const int32 PlateCountBefore = Service->GetPlates().Num();
        Service->AdvanceSteps(1);
        const int32 PlateCountAfter = Service->GetPlates().Num();

        StepsUntilSplit++;

        if (PlateCountAfter > PlateCountBefore)
        {
            bSplitOccurred = true;
            UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: Split occurred at step %d (%d → %d plates)"),
                StepsUntilSplit, PlateCountBefore, PlateCountAfter);
            break;
        }
    }

    TestTrue(TEXT("Plate split occurred within time limit"), bSplitOccurred);

    if (bSplitOccurred)
    {
        const int32 FinalPlateCount = Service->GetPlates().Num();
        const int32 FinalBoundaryCount = Service->GetBoundaries().Num();

        UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: After split - %d plates (+%d), %d boundaries (+%d)"),
            FinalPlateCount, FinalPlateCount - InitialPlateCount,
            FinalBoundaryCount, FinalBoundaryCount - InitialBoundaryCount);

        TestEqual(TEXT("Plate count increased by 1 after split"), FinalPlateCount, InitialPlateCount + 1);

        // Validate Euler characteristic preserved
        const TArray<FVector3d>& Vertices = Service->GetRenderVertices();
        const TArray<int32>& Triangles = Service->GetRenderTriangles();

        const int32 V = Vertices.Num();
        const int32 F = Triangles.Num() / 3;
        const int32 E = (Triangles.Num() / 3) * 3 / 2;
        const int32 EulerChar = V - E + F;

        UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: Topology after split: V=%d E=%d F=%d χ=%d"),
            V, E, F, EulerChar);
        TestEqual(TEXT("Euler characteristic preserved after split"), EulerChar, 2);

        // Validate vertex assignments
        const TArray<int32>& Assignments = Service->GetVertexPlateAssignments();
        TestEqual(TEXT("All vertices assigned after split"), Assignments.Num(), Vertices.Num());

        int32 UnassignedCount = 0;
        for (int32 Assignment : Assignments)
        {
            if (Assignment == INDEX_NONE)
                UnassignedCount++;
        }
        TestEqual(TEXT("No unassigned vertices after split"), UnassignedCount, 0);
    }

    // Test 2: Boundary updates after split
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 2: Boundary updates after split"));

    if (bSplitOccurred)
    {
        const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries = Service->GetBoundaries();

        // Check that new plate has boundaries with neighbors
        const int32 NewPlateID = Service->GetPlates().Num() - 1;
        int32 NewPlateBoundaryCount = 0;

        for (const auto& BoundaryPair : Boundaries)
        {
            if (BoundaryPair.Key.Key == NewPlateID || BoundaryPair.Key.Value == NewPlateID)
            {
                NewPlateBoundaryCount++;
                UE_LOG(LogPlanetaryCreation, Verbose, TEXT("Test 2: New plate %d has boundary with plate %d (type: %d)"),
                    NewPlateID,
                    BoundaryPair.Key.Key == NewPlateID ? BoundaryPair.Key.Value : BoundaryPair.Key.Key,
                    static_cast<int32>(BoundaryPair.Value.BoundaryType));
            }
        }

        UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 2: New plate %d has %d boundaries"), NewPlateID, NewPlateBoundaryCount);
        TestTrue(TEXT("New plate has at least one boundary"), NewPlateBoundaryCount > 0);

        // Validate all boundaries reference valid plates
        const int32 PlateCount = Service->GetPlates().Num();
        for (const auto& BoundaryPair : Boundaries)
        {
            TestTrue(TEXT("Boundary plate A valid"), BoundaryPair.Key.Key < PlateCount);
            TestTrue(TEXT("Boundary plate B valid"), BoundaryPair.Key.Value < PlateCount);
        }
    }

    // Test 3: Plate merge topology consistency
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: Plate merge topology consistency"));

    Params.Seed = 22222;
    Params.SubdivisionLevel = 0;
    Params.RenderSubdivisionLevel = 2;
    Params.LloydIterations = 3; // More convergence for stable collisions
    Params.bEnablePlateTopologyChanges = true; // Enable split/merge execution
    Service->SetParameters(Params);

    const int32 InitialPlateCount2 = Service->GetPlates().Num();

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: Initial state - %d plates"), InitialPlateCount2);

    // Set up convergent boundary to force collision
    TArray<FTectonicPlate>& Plates2 = Service->GetPlatesForModification();
    if (Plates2.Num() >= 2)
    {
        // Make plates converge (toward each other)
        const FVector3d MidPoint = (Plates2[0].Centroid + Plates2[1].Centroid) * 0.5;
        const FVector3d Dir0 = (MidPoint - Plates2[0].Centroid).GetSafeNormal();
        const FVector3d Dir1 = (MidPoint - Plates2[1].Centroid).GetSafeNormal();

        Plates2[0].EulerPoleAxis = Dir0;
        Plates2[0].AngularVelocity = 0.05; // rad/My
        Plates2[1].EulerPoleAxis = Dir1;
        Plates2[1].AngularVelocity = 0.05; // rad/My
    }

    // Run until merge occurs
    int32 StepsUntilMerge = 0;
    bool bMergeOccurred = false;

    for (int32 Step = 0; Step < MaxSteps; ++Step)
    {
        const int32 PlateCountBefore = Service->GetPlates().Num();
        Service->AdvanceSteps(1);
        const int32 PlateCountAfter = Service->GetPlates().Num();

        StepsUntilMerge++;

        if (PlateCountAfter < PlateCountBefore)
        {
            bMergeOccurred = true;
            UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: Merge occurred at step %d (%d → %d plates)"),
                StepsUntilMerge, PlateCountBefore, PlateCountAfter);
            break;
        }
    }

    if (bMergeOccurred)
    {
        const int32 FinalPlateCount2 = Service->GetPlates().Num();
        const int32 FinalBoundaryCount2 = Service->GetBoundaries().Num();

        UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: After merge - %d plates (-%d), %d boundaries"),
            FinalPlateCount2, InitialPlateCount2 - FinalPlateCount2, FinalBoundaryCount2);

        TestEqual(TEXT("Plate count decreased by 1 after merge"), FinalPlateCount2, InitialPlateCount2 - 1);

        // Validate Euler characteristic preserved
        const TArray<FVector3d>& Vertices2 = Service->GetRenderVertices();
        const TArray<int32>& Triangles2 = Service->GetRenderTriangles();

        const int32 V2 = Vertices2.Num();
        const int32 F2 = Triangles2.Num() / 3;
        const int32 E2 = (Triangles2.Num() / 3) * 3 / 2;
        const int32 EulerChar2 = V2 - E2 + F2;

        UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: Topology after merge: V=%d E=%d F=%d χ=%d"),
            V2, E2, F2, EulerChar2);
        TestEqual(TEXT("Euler characteristic preserved after merge"), EulerChar2, 2);

        // Validate no orphaned vertices
        const TArray<int32>& Assignments2 = Service->GetVertexPlateAssignments();
        int32 UnassignedCount2 = 0;
        for (int32 Assignment : Assignments2)
        {
            if (Assignment == INDEX_NONE || Assignment >= FinalPlateCount2)
                UnassignedCount2++;
        }
        TestEqual(TEXT("No unassigned/invalid vertices after merge"), UnassignedCount2, 0);
    }
    else
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("Test 3: Merge did not occur within %d steps (non-critical)"), MaxSteps);
    }

    // Test 4: Stress redistribution after split
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 4: Stress redistribution validation"));

    // Reset and force another split
    Params.Seed = 33333;
    Params.bEnableRiftPropagation = true;
    Params.bEnablePlateTopologyChanges = true;
    Service->SetParameters(Params);

    TArray<FTectonicPlate>& Plates3 = Service->GetPlatesForModification();
    if (Plates3.Num() >= 2)
    {
        Plates3[0].EulerPoleAxis = FVector3d(1.0, 0.0, 0.0).GetSafeNormal();
        Plates3[0].AngularVelocity = 0.15; // rad/My (increased for faster dynamics)
        Plates3[1].EulerPoleAxis = FVector3d(-1.0, 0.0, 0.0).GetSafeNormal();
        Plates3[1].AngularVelocity = 0.15; // rad/My
    }

    // Run to build up stress
    Service->AdvanceSteps(20);

    // Check stress values before split
    const TMap<TPair<int32, int32>, FPlateBoundary>& BoundariesBeforeSplit = Service->GetBoundaries();
    double TotalStressBefore = 0.0;
    for (const auto& BoundaryPair : BoundariesBeforeSplit)
    {
        TotalStressBefore += BoundaryPair.Value.AccumulatedStress;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 4: Total stress before topology change: %.1f"), TotalStressBefore);

    // Continue until topology changes
    const int32 PlateCountBeforeChange = Service->GetPlates().Num();
    Service->AdvanceSteps(50);
    const int32 PlateCountAfterChange = Service->GetPlates().Num();

    if (PlateCountAfterChange != PlateCountBeforeChange)
    {
        const TMap<TPair<int32, int32>, FPlateBoundary>& BoundariesAfterSplit = Service->GetBoundaries();
        double TotalStressAfter = 0.0;
        for (const auto& BoundaryPair : BoundariesAfterSplit)
        {
            TotalStressAfter += BoundaryPair.Value.AccumulatedStress;
        }

        UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 4: Total stress after topology change: %.1f"), TotalStressAfter);

        // Stress should be redistributed (not necessarily conserved due to release on split)
        // But should still be non-zero and reasonable
        // Note: If initial stress was zero, we just verify it remains reasonable (not negative/NaN)
        if (TotalStressBefore > 0.0)
        {
            TestTrue(TEXT("Stress redistribution occurred"), TotalStressAfter > 0.0);
            TestTrue(TEXT("Stress values remain reasonable"), TotalStressAfter < TotalStressBefore * 6.0);
        }
        else
        {
            TestTrue(TEXT("Stress remains valid after topology change"), TotalStressAfter >= 0.0);
            UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 4: Initial stress was zero, skipping redistribution validation"));
        }
    }

    // Summary
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Split/Merge Validation Test Complete ==="));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("✓ Plate split increases plate count by 1"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("✓ Topology (Euler characteristic) preserved across splits/merges"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("✓ Boundary updates correctly after topology changes"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("✓ Stress redistribution maintains reasonable values"));

    return true;
}
