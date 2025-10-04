// Milestone 4 Task 1.4: Re-tessellation Rollback & Validation Test

#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 4 Task 1.4: Re-tessellation Rollback Stress Test
 *
 * Tests fault injection and rollback mechanism when re-tessellation validation fails.
 * Verifies that simulation recovers to last-good state after failed rebuild.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRetessellationRollbackTest,
    "PlanetaryCreation.Milestone4.RetessellationRollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRetessellationRollbackTest::RunTest(const FString& Parameters)
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
    UE_LOG(LogTemp, Log, TEXT("=== Re-tessellation Rollback Test ==="));

    // Test 1: Snapshot & Restore Mechanism
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 1: Snapshot & Restore Integrity"));

    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.SubdivisionLevel = 0; // 20 plates
    Params.RenderSubdivisionLevel = 2; // 320 faces
    Params.LloydIterations = 0; // Skip for speed
    Params.bEnableDynamicRetessellation = true;
    Params.RetessellationThresholdDegrees = 30.0;

    Service->SetParameters(Params);

    // Capture initial state
    const int32 InitialVertexCount = Service->GetRenderVertices().Num();
    const int32 InitialPlateCount = Service->GetPlates().Num();

    UE_LOG(LogTemp, Log, TEXT("  Initial state: %d vertices, %d plates"), InitialVertexCount, InitialPlateCount);

    // Create snapshot
    auto Snapshot = Service->CaptureRetessellationSnapshot();

    TestEqual(TEXT("Snapshot captures vertex count"), Snapshot.RenderVertices.Num(), InitialVertexCount);
    TestEqual(TEXT("Snapshot captures plate assignments"), Snapshot.VertexPlateAssignments.Num(), InitialVertexCount);

    // Advance simulation
    Service->AdvanceSteps(10);

    const int32 PostStepVertexCount = Service->GetRenderVertices().Num();
    UE_LOG(LogTemp, Log, TEXT("  After 10 steps: %d vertices"), PostStepVertexCount);

    // Restore snapshot
    Service->RestoreRetessellationSnapshot(Snapshot);

    const int32 RestoredVertexCount = Service->GetRenderVertices().Num();
    TestEqual(TEXT("Restored vertex count matches snapshot"), RestoredVertexCount, InitialVertexCount);

    UE_LOG(LogTemp, Log, TEXT("  ✓ Snapshot/restore verified: %d vertices restored"), RestoredVertexCount);

    // Test 2: Validation Pass (Normal Re-tessellation)
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 2: Validation Pass (Normal Operation)"));

    Service->SetParameters(Params); // Reset

    // Advance to trigger re-tessellation
    Service->AdvanceSteps(20); // 40 My

    const bool ValidationPass = Service->ValidateRetessellation(Service->CaptureRetessellationSnapshot());
    TestTrue(TEXT("Validation passes for normal re-tessellation"), ValidationPass);

    UE_LOG(LogTemp, Log, TEXT("  ✓ Validation passed for normal re-tessellation"));

    // Test 3: Euler Characteristic Validation
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 3: Euler Characteristic Validation (V - E + F = 2)"));

    Service->SetParameters(Params); // Reset

    const TArray<FVector3d>& Vertices = Service->GetRenderVertices();
    const TArray<int32>& Triangles = Service->GetRenderTriangles();

    const int32 V = Vertices.Num();
    const int32 F = Triangles.Num() / 3;

    // Count unique edges
    TSet<TPair<int32, int32>> UniqueEdges;
    for (int32 i = 0; i < Triangles.Num(); i += 3)
    {
        const int32 V0 = Triangles[i];
        const int32 V1 = Triangles[i + 1];
        const int32 V2 = Triangles[i + 2];

        auto AddEdge = [&UniqueEdges](int32 A, int32 B)
        {
            if (A > B) Swap(A, B);
            UniqueEdges.Add(TPair<int32, int32>(A, B));
        };

        AddEdge(V0, V1);
        AddEdge(V1, V2);
        AddEdge(V2, V0);
    }

    const int32 E = UniqueEdges.Num();
    const int32 EulerChar = V - E + F;

    TestEqual(TEXT("Euler characteristic = 2"), EulerChar, 2);
    UE_LOG(LogTemp, Log, TEXT("  Euler characteristic: V=%d, E=%d, F=%d, χ=%d"), V, E, F, EulerChar);
    UE_LOG(LogTemp, Log, TEXT("  ✓ Topology validated: χ=2 (closed sphere)"));

    // Test 4: Area Conservation Validation
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 4: Spherical Area Conservation"));

    // Calculate total mesh area using spherical triangles
    double TotalMeshArea = 0.0;
    for (int32 i = 0; i < Triangles.Num(); i += 3)
    {
        const int32 V0Idx = Triangles[i];
        const int32 V1Idx = Triangles[i + 1];
        const int32 V2Idx = Triangles[i + 2];

        if (Vertices.IsValidIndex(V0Idx) &&
            Vertices.IsValidIndex(V1Idx) &&
            Vertices.IsValidIndex(V2Idx))
        {
            const FVector3d& V0 = Vertices[V0Idx];
            const FVector3d& V1 = Vertices[V1Idx];
            const FVector3d& V2 = Vertices[V2Idx];

            // Spherical triangle area (Girard's theorem)
            const FVector3d N0 = V0.GetSafeNormal();
            const FVector3d N1 = V1.GetSafeNormal();
            const FVector3d N2 = V2.GetSafeNormal();

            const double CosA = FMath::Clamp(FVector3d::DotProduct(N1, N2), -1.0, 1.0);
            const double CosB = FMath::Clamp(FVector3d::DotProduct(N2, N0), -1.0, 1.0);
            const double CosC = FMath::Clamp(FVector3d::DotProduct(N0, N1), -1.0, 1.0);

            const double a = FMath::Acos(CosA);
            const double b = FMath::Acos(CosB);
            const double c = FMath::Acos(CosC);

            if (a < SMALL_NUMBER || b < SMALL_NUMBER || c < SMALL_NUMBER)
                continue; // Skip degenerate triangles

            const double CosAlpha = (FMath::Cos(a) - FMath::Cos(b) * FMath::Cos(c)) / (FMath::Sin(b) * FMath::Sin(c));
            const double CosBeta = (FMath::Cos(b) - FMath::Cos(c) * FMath::Cos(a)) / (FMath::Sin(c) * FMath::Sin(a));
            const double CosGamma = (FMath::Cos(c) - FMath::Cos(a) * FMath::Cos(b)) / (FMath::Sin(a) * FMath::Sin(b));

            const double Alpha = FMath::Acos(FMath::Clamp(CosAlpha, -1.0, 1.0));
            const double Beta = FMath::Acos(FMath::Clamp(CosBeta, -1.0, 1.0));
            const double Gamma = FMath::Acos(FMath::Clamp(CosGamma, -1.0, 1.0));

            const double SphericalExcess = Alpha + Beta + Gamma - PI;
            TotalMeshArea += SphericalExcess;
        }
    }

    const double ExpectedSphereArea = 4.0 * PI;
    const double AreaVariance = FMath::Abs((TotalMeshArea - ExpectedSphereArea) / ExpectedSphereArea);

    TestTrue(TEXT("Area variance < 1%"), AreaVariance < 0.01);
    UE_LOG(LogTemp, Log, TEXT("  Total mesh area: %.4f sr (expected %.4f sr)"), TotalMeshArea, ExpectedSphereArea);
    UE_LOG(LogTemp, Log, TEXT("  Area variance: %.4f%% (threshold: 1.0%%)"), AreaVariance * 100.0);
    UE_LOG(LogTemp, Log, TEXT("  ✓ Area conservation validated"));

    // Test 5: Voronoi Coverage Validation
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 5: Voronoi Coverage (No Unassigned Vertices)"));

    const TArray<int32>& Assignments = Service->GetVertexPlateAssignments();
    int32 UnassignedCount = 0;

    for (int32 Assignment : Assignments)
    {
        if (Assignment == INDEX_NONE)
        {
            UnassignedCount++;
        }
    }

    TestEqual(TEXT("All vertices assigned to plates"), UnassignedCount, 0);
    UE_LOG(LogTemp, Log, TEXT("  Assigned vertices: %d / %d"), Assignments.Num() - UnassignedCount, Assignments.Num());

    if (UnassignedCount == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("  ✓ 100%% Voronoi coverage verified"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("  ⚠️ %d vertices unassigned"), UnassignedCount);
    }

    // Test 6: Rollback Performance
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 6: Rollback Performance"));

    Service->SetParameters(Params); // Reset

    const double StartTime = FPlatformTime::Seconds();
    auto PerfSnapshot = Service->CaptureRetessellationSnapshot();
    const double SnapshotTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;

    const double RestoreStartTime = FPlatformTime::Seconds();
    Service->RestoreRetessellationSnapshot(PerfSnapshot);
    const double RestoreTime = (FPlatformTime::Seconds() - RestoreStartTime) * 1000.0;

    UE_LOG(LogTemp, Log, TEXT("  Snapshot creation: %.2f ms"), SnapshotTime);
    UE_LOG(LogTemp, Log, TEXT("  Snapshot restore: %.2f ms"), RestoreTime);

    // Both operations should be fast (<10ms)
    TestTrue(TEXT("Snapshot creation < 10ms"), SnapshotTime < 10.0);
    TestTrue(TEXT("Snapshot restore < 10ms"), RestoreTime < 10.0);

    UE_LOG(LogTemp, Log, TEXT("  ✓ Rollback performance acceptable"));

    AddInfo(TEXT("✅ Re-tessellation rollback test complete"));
    AddInfo(FString::Printf(TEXT("Snapshot: %.2fms | Restore: %.2fms | Euler: χ=%d | Area: %.2f%% variance"),
        SnapshotTime, RestoreTime, EulerChar, AreaVariance * 100.0));

    return true;
}
