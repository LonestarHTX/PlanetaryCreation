// Milestone 6 Task 1.3: Terrane Reattachment Test

#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 6 Task 1.3: Terrane Reattachment Test
 *
 * Validates terrane reattachment and suturing mechanics:
 * 1. Automatic reattachment when terrane enters Colliding state
 * 2. Vertex reassignment to target continental plate
 * 3. Topology preservation (Euler characteristic, manifold edges)
 * 4. Terrane removal from active list after suturing
 * 5. Performance (<10ms for reattachment at L3)
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraneReattachmentTest,
    "PlanetaryCreation.Milestone6.TerraneReattachment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraneReattachmentTest::RunTest(const FString& Parameters)
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
    UE_LOG(LogTemp, Log, TEXT("=== Milestone 6 Task 1.3: Terrane Reattachment Test ==="));
    UE_LOG(LogTemp, Log, TEXT(""));

    // Initialize simulation with multiple continental plates
    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.SubdivisionLevel = 0; // 20 plates
    Params.RenderSubdivisionLevel = 3; // 642 vertices
    Params.LloydIterations = 2;
    Params.bEnableDynamicRetessellation = false;
    Service->SetParameters(Params);

    // Ensure we have multiple continental plates for testing
    TArray<FTectonicPlate>& Plates = Service->GetPlatesForModification();
    int32 ContinentalCount = 0;
    for (FTectonicPlate& Plate : Plates)
    {
        if (Plate.CrustType == ECrustType::Continental)
        {
            ContinentalCount++;
        }
    }

    // Ensure at least 2 continental plates
    if (ContinentalCount < 2)
    {
        Plates[0].CrustType = ECrustType::Continental;
        Plates[1].CrustType = ECrustType::Continental;
        ContinentalCount = 2;
    }

    UE_LOG(LogTemp, Log, TEXT("Plate configuration: %d continental plates"), ContinentalCount);

    // ========================================
    // TEST 1: Extract Terrane from Source Plate
    // ========================================
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("--- Test 1: Terrane Extraction ---"));

    // Find first continental plate
    int32 SourcePlateID = INDEX_NONE;
    for (const FTectonicPlate& Plate : Plates)
    {
        if (Plate.CrustType == ECrustType::Continental)
        {
            SourcePlateID = Plate.PlateID;
            break;
        }
    }

    TestTrue(TEXT("Source continental plate found"), SourcePlateID != INDEX_NONE);

    // Select contiguous terrane region
    const TArray<int32>& VertexAssignments = Service->GetVertexPlateAssignments();
    const TArray<int32>& RenderTriangles = Service->GetRenderTriangles();

    // Find seed vertex
    int32 SeedVertex = INDEX_NONE;
    for (int32 i = 0; i < VertexAssignments.Num(); ++i)
    {
        if (VertexAssignments[i] == SourcePlateID)
        {
            SeedVertex = i;
            break;
        }
    }

    TestTrue(TEXT("Seed vertex found"), SeedVertex != INDEX_NONE);

    // Grow contiguous region
    TArray<int32> TerraneVertices;
    TSet<int32> TerraneVertexSet;
    TerraneVertices.Add(SeedVertex);
    TerraneVertexSet.Add(SeedVertex);

    const int32 TargetSize = 10;
    for (int32 GrowthIter = 0; GrowthIter < 100 && TerraneVertices.Num() < TargetSize; ++GrowthIter)
    {
        bool bAdded = false;
        for (int32 i = 0; i < RenderTriangles.Num(); i += 3)
        {
            int32 V0 = RenderTriangles[i];
            int32 V1 = RenderTriangles[i + 1];
            int32 V2 = RenderTriangles[i + 2];

            if (TerraneVertexSet.Contains(V0) || TerraneVertexSet.Contains(V1) || TerraneVertexSet.Contains(V2))
            {
                if (!TerraneVertexSet.Contains(V0) && VertexAssignments[V0] == SourcePlateID)
                {
                    TerraneVertices.Add(V0);
                    TerraneVertexSet.Add(V0);
                    bAdded = true;
                }
                if (!TerraneVertexSet.Contains(V1) && VertexAssignments[V1] == SourcePlateID)
                {
                    TerraneVertices.Add(V1);
                    TerraneVertexSet.Add(V1);
                    bAdded = true;
                }
                if (!TerraneVertexSet.Contains(V2) && VertexAssignments[V2] == SourcePlateID)
                {
                    TerraneVertices.Add(V2);
                    TerraneVertexSet.Add(V2);
                    bAdded = true;
                }

                if (TerraneVertices.Num() >= TargetSize)
                {
                    break;
                }
            }
        }

        if (!bAdded)
        {
            break;
        }
    }

    TestTrue(TEXT("Sufficient terrane vertices selected"), TerraneVertices.Num() >= 10);
    const double TerraneArea = Service->ComputeTerraneArea(TerraneVertices);
    UE_LOG(LogTemp, Log, TEXT("  Selected %d vertices, area: %.2f km²"), TerraneVertices.Num(), TerraneArea);

    // Extract terrane
    int32 TerraneID = INDEX_NONE;
    const bool bExtracted = Service->ExtractTerrane(SourcePlateID, TerraneVertices, TerraneID);
    TestTrue(TEXT("Terrane extraction succeeded"), bExtracted);

    if (!bExtracted)
    {
        UE_LOG(LogTemp, Error, TEXT("  ❌ FAIL: Could not extract terrane"));
        return false;
    }

    const TArray<FContinentalTerrane>& TerranesAfterExtraction = Service->GetTerranes();
    TestEqual(TEXT("One terrane exists after extraction"), TerranesAfterExtraction.Num(), 1);

    UE_LOG(LogTemp, Log, TEXT("  ✅ PASS: Terrane %d extracted successfully"), TerraneID);
    UE_LOG(LogTemp, Log, TEXT(""));

    // ========================================
    // TEST 2: Manual Reattachment to Different Continental Plate
    // ========================================
    UE_LOG(LogTemp, Log, TEXT("--- Test 2: Manual Reattachment ---"));

    // Find second continental plate (different from source)
    int32 TargetPlateID = INDEX_NONE;
    for (const FTectonicPlate& Plate : Plates)
    {
        if (Plate.CrustType == ECrustType::Continental && Plate.PlateID != SourcePlateID)
        {
            TargetPlateID = Plate.PlateID;
            break;
        }
    }

    TestTrue(TEXT("Target continental plate found"), TargetPlateID != INDEX_NONE);

    // Capture pre-reattachment state
    const TArray<int32>& PreReattachmentAssignments = Service->GetVertexPlateAssignments();
    int32 VerticesOnTargetBefore = 0;
    for (int32 Idx : TerraneVertices)
    {
        if (PreReattachmentAssignments[Idx] == TargetPlateID)
        {
            VerticesOnTargetBefore++;
        }
    }

    UE_LOG(LogTemp, Log, TEXT("  Vertices on target plate before reattachment: %d"), VerticesOnTargetBefore);

    // Perform reattachment
    const double ReattachmentStartTime = FPlatformTime::Seconds();
    const bool bReattached = Service->ReattachTerrane(TerraneID, TargetPlateID);
    const double ReattachmentTimeMs = (FPlatformTime::Seconds() - ReattachmentStartTime) * 1000.0;

    TestTrue(TEXT("Reattachment succeeded"), bReattached);

    if (!bReattached)
    {
        UE_LOG(LogTemp, Error, TEXT("  ❌ FAIL: Reattachment failed"));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("  Reattachment time: %.2f ms (target: <10ms)"), ReattachmentTimeMs);
    TestTrue(TEXT("Reattachment performance <10ms"), ReattachmentTimeMs < 10.0);

    // Verify terrane removed from active list
    const TArray<FContinentalTerrane>& TerranesAfterReattachment = Service->GetTerranes();
    TestEqual(TEXT("No terranes after reattachment"), TerranesAfterReattachment.Num(), 0);

    // Verify all terrane vertices now assigned to target plate
    const TArray<int32>& PostReattachmentAssignments = Service->GetVertexPlateAssignments();
    int32 VerticesOnTargetAfter = 0;
    for (int32 Idx : TerraneVertices)
    {
        if (PostReattachmentAssignments[Idx] == TargetPlateID)
        {
            VerticesOnTargetAfter++;
        }
    }

    UE_LOG(LogTemp, Log, TEXT("  Vertices on target plate after reattachment: %d"), VerticesOnTargetAfter);
    TestEqual(TEXT("All terrane vertices reassigned to target"), VerticesOnTargetAfter, TerraneVertices.Num());

    UE_LOG(LogTemp, Log, TEXT("  ✅ PASS: Terrane vertices sutured to target plate"));
    UE_LOG(LogTemp, Log, TEXT(""));

    // ========================================
    // TEST 3: Topology Validation After Reattachment
    // ========================================
    UE_LOG(LogTemp, Log, TEXT("--- Test 3: Topology Validation ---"));

    const TArray<FVector3d>& RenderVertices = Service->GetRenderVertices();

    // Compute Euler characteristic
    TSet<TPair<int32, int32>> UniqueEdges;
    for (int32 i = 0; i < RenderTriangles.Num(); i += 3)
    {
        int32 V0 = RenderTriangles[i];
        int32 V1 = RenderTriangles[i + 1];
        int32 V2 = RenderTriangles[i + 2];

        UniqueEdges.Add(MakeTuple(FMath::Min(V0, V1), FMath::Max(V0, V1)));
        UniqueEdges.Add(MakeTuple(FMath::Min(V1, V2), FMath::Max(V1, V2)));
        UniqueEdges.Add(MakeTuple(FMath::Min(V2, V0), FMath::Max(V2, V0)));
    }

    int32 V = RenderVertices.Num();
    int32 E = UniqueEdges.Num();
    int32 F = RenderTriangles.Num() / 3;
    int32 EulerChar = V - E + F;

    UE_LOG(LogTemp, Log, TEXT("  V = %d, E = %d, F = %d"), V, E, F);
    UE_LOG(LogTemp, Log, TEXT("  V - E + F = %d (should be 2 for sphere)"), EulerChar);

    TestEqual(TEXT("Euler characteristic = 2"), EulerChar, 2);

    // Check manifold edges
    TMap<TPair<int32, int32>, int32> EdgeCounts;
    for (int32 i = 0; i < RenderTriangles.Num(); i += 3)
    {
        int32 V0 = RenderTriangles[i];
        int32 V1 = RenderTriangles[i + 1];
        int32 V2 = RenderTriangles[i + 2];

        auto Edge01 = MakeTuple(FMath::Min(V0, V1), FMath::Max(V0, V1));
        auto Edge12 = MakeTuple(FMath::Min(V1, V2), FMath::Max(V1, V2));
        auto Edge20 = MakeTuple(FMath::Min(V2, V0), FMath::Max(V2, V0));

        EdgeCounts.FindOrAdd(Edge01)++;
        EdgeCounts.FindOrAdd(Edge12)++;
        EdgeCounts.FindOrAdd(Edge20)++;
    }

    int32 NonManifoldEdges = 0;
    for (const auto& Pair : EdgeCounts)
    {
        if (Pair.Value != 2)
        {
            NonManifoldEdges++;
        }
    }

    TestEqual(TEXT("Non-manifold edge count"), NonManifoldEdges, 0);

    UE_LOG(LogTemp, Log, TEXT("  ✅ PASS: Topology valid (Euler = 2, all edges manifold)"));
    UE_LOG(LogTemp, Log, TEXT(""));

    // ========================================
    // TEST 4: Automatic Reattachment via Simulation Loop
    // ========================================
    UE_LOG(LogTemp, Log, TEXT("--- Test 4: Automatic Reattachment via Collision ---"));

    // Reset simulation to test automatic reattachment
    Service->SetParameters(Params);

    // Extract new terrane
    int32 NewTerraneID = INDEX_NONE;
    const bool bExtracted2 = Service->ExtractTerrane(SourcePlateID, TerraneVertices, NewTerraneID);
    TestTrue(TEXT("Second terrane extraction succeeded"), bExtracted2);

    const TArray<FContinentalTerrane>& TerranesBeforeCollision = Service->GetTerranes();
    TestEqual(TEXT("One terrane exists before collision"), TerranesBeforeCollision.Num(), 1);

    // Force terrane into Colliding state by manually setting it
    TArray<FContinentalTerrane>& TerranesToModify = const_cast<TArray<FContinentalTerrane>&>(Service->GetTerranes());
    if (TerranesToModify.Num() > 0)
    {
        TerranesToModify[0].State = ETerraneState::Colliding;
        TerranesToModify[0].TargetPlateID = TargetPlateID;

        UE_LOG(LogTemp, Log, TEXT("  Manually set terrane %d to Colliding state with target plate %d"),
            NewTerraneID, TargetPlateID);

        // Advance one step to trigger automatic reattachment
        Service->AdvanceSteps(1);

        // Verify terrane was automatically reattached
        const TArray<FContinentalTerrane>& TerranesAfterStep = Service->GetTerranes();
        TestEqual(TEXT("Terrane automatically reattached (removed from list)"), TerranesAfterStep.Num(), 0);

        UE_LOG(LogTemp, Log, TEXT("  ✅ PASS: Automatic reattachment triggered by Colliding state"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("  ❌ FAIL: Terrane not found for collision test"));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT(""));

    // ========================================
    // Summary
    // ========================================
    UE_LOG(LogTemp, Log, TEXT("=== Terrane Reattachment Test Summary ==="));
    UE_LOG(LogTemp, Log, TEXT("  ✅ Manual reattachment: PASS"));
    UE_LOG(LogTemp, Log, TEXT("  ✅ Vertex reassignment: PASS"));
    UE_LOG(LogTemp, Log, TEXT("  ✅ Topology preservation: PASS"));
    UE_LOG(LogTemp, Log, TEXT("  ✅ Performance (<10ms): PASS"));
    UE_LOG(LogTemp, Log, TEXT("  ✅ Automatic reattachment: PASS"));
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Terrane Reattachment Test PASSED"));

    return true;
}
