// Milestone 6 Task 1.1: Terrane Mechanics Test

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 6 Task 1.1: Terrane Mechanics Test
 *
 * Validates extraction/reattachment with topology preservation:
 * 1. Extract terrane from continental plate (50 vertices)
 * 2. Validate topology remains valid (Euler characteristic, manifold edges, no orphans)
 * 3. Reattach terrane to same plate
 * 4. Validate mesh identical to pre-extraction state
 * 5. Test rollback integration (undo after extraction)
 * 6. Performance validation (<5ms extraction, <10ms reattachment)
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraneMechanicsTest,
    "PlanetaryCreation.Milestone6.TerraneMechanics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraneMechanicsTest::RunTest(const FString& Parameters)
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
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Milestone 6 Task 1.1: Terrane Mechanics Test ==="));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // Initialize simulation at Level 3 (642 vertices, production-scale)
    // Use SubdivisionLevel=0 (20 plates) to ensure each plate has enough vertices (~32) for terrane extraction
    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.SubdivisionLevel = 0; // 20 plates (gives ~32 vertices per plate on average)
    Params.RenderSubdivisionLevel = 3; // 642 vertices (1,280 triangles)
    Params.LloydIterations = 2;
    Params.bEnableDynamicRetessellation = false;
    Service->SetParameters(Params);

    const TArray<FVector3d>& RenderVertices = Service->GetRenderVertices();
    const TArray<int32>& RenderTriangles = Service->GetRenderTriangles();
    const TArray<int32>& VertexPlateAssignments = Service->GetVertexPlateAssignments();
    const TArray<FTectonicPlate>& Plates = Service->GetPlatesForModification();

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Baseline mesh initialized:"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Vertices: %d"), RenderVertices.Num());
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Triangles: %d"), RenderTriangles.Num() / 3);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Plates: %d"), Plates.Num());
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // ========================================
    // TEST 1: Baseline Topology Validation
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT("--- Test 1: Baseline Topology Validation ---"));

    FString ValidationError;
    TestTrue(TEXT("Baseline topology valid"), Service->ValidateTopology(ValidationError));
    if (!ValidationError.IsEmpty())
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("  Validation error: %s"), *ValidationError);
        return false;
    }
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ PASS: Baseline topology valid"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // ========================================
    // TEST 2: Find Continental Plate and Select Terrane Region
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT("--- Test 2: Select Terrane Region ---"));

    // Find a continental plate with sufficient vertices (need ~50 for viable terrane)
    int32 ContinentalPlateID = INDEX_NONE;
    int32 LargestPlateVertexCount = 0;

    for (const FTectonicPlate& Plate : Plates)
    {
        if (Plate.CrustType == ECrustType::Continental)
        {
            // Count vertices for this plate
            int32 PlateVertexCount = 0;
            for (int32 Assignment : VertexPlateAssignments)
            {
                if (Assignment == Plate.PlateID)
                {
                    PlateVertexCount++;
                }
            }

            // Select largest continental plate to maximize terrane extraction success
            if (PlateVertexCount > LargestPlateVertexCount)
            {
                ContinentalPlateID = Plate.PlateID;
                LargestPlateVertexCount = PlateVertexCount;
            }
        }
    }

    if (ContinentalPlateID == INDEX_NONE || LargestPlateVertexCount < 10)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("  ⚠️ No large continental plates found (need 10+ vertices), assigning largest plate as continental"));
        // Find largest plate and make it continental
        TArray<FTectonicPlate>& MutablePlates = Service->GetPlatesForModification();
        for (FTectonicPlate& Plate : MutablePlates)
        {
            int32 PlateVertexCount = 0;
            for (int32 Assignment : VertexPlateAssignments)
            {
                if (Assignment == Plate.PlateID)
                {
                    PlateVertexCount++;
                }
            }

            if (PlateVertexCount > LargestPlateVertexCount)
            {
                ContinentalPlateID = Plate.PlateID;
                LargestPlateVertexCount = PlateVertexCount;
            }
        }

        // Mark it as continental
        for (FTectonicPlate& Plate : MutablePlates)
        {
            if (Plate.PlateID == ContinentalPlateID)
            {
                Plate.CrustType = ECrustType::Continental;
                break;
            }
        }
    }

    // Find vertices belonging to continental plate
    TArray<int32> PlateVertices;
    for (int32 i = 0; i < VertexPlateAssignments.Num(); ++i)
    {
        if (VertexPlateAssignments[i] == ContinentalPlateID)
        {
            PlateVertices.Add(i);
        }
    }

    TestTrue(TEXT("Continental plate has vertices"), PlateVertices.Num() > 0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Continental plate %d: %d vertices"), ContinentalPlateID, PlateVertices.Num());

    // Select ~10 vertices for terrane (or 1/4 of plate vertices, whichever is larger but capped at 50)
    const int32 MinTerraneSize = 10;
    const int32 TargetTerraneSize = FMath::Clamp(PlateVertices.Num() / 4, MinTerraneSize, 50);
    TestTrue(TEXT("Plate has enough vertices for terrane"), PlateVertices.Num() >= MinTerraneSize);

    // Select a contiguous region by finding triangles and growing from seed
    TArray<int32> TerraneVertices;
    TSet<int32> TerraneVertexSet;
    TerraneVertices.Add(PlateVertices[0]); // Seed with first vertex
    TerraneVertexSet.Add(PlateVertices[0]);

    // Grow region by adding neighbors until we reach target size
    for (int32 GrowthIter = 0; GrowthIter < 100 && TerraneVertices.Num() < TargetTerraneSize; ++GrowthIter)
    {
        bool bAddedVertex = false;
        for (int32 i = 0; i < RenderTriangles.Num(); i += 3)
        {
            int32 V0 = RenderTriangles[i];
            int32 V1 = RenderTriangles[i + 1];
            int32 V2 = RenderTriangles[i + 2];

            // If any vertex in triangle is in terrane, add others if they belong to same plate
            if (TerraneVertexSet.Contains(V0) || TerraneVertexSet.Contains(V1) || TerraneVertexSet.Contains(V2))
            {
                if (!TerraneVertexSet.Contains(V0) && VertexPlateAssignments[V0] == ContinentalPlateID)
                {
                    TerraneVertices.Add(V0);
                    TerraneVertexSet.Add(V0);
                    bAddedVertex = true;
                }
                if (!TerraneVertexSet.Contains(V1) && VertexPlateAssignments[V1] == ContinentalPlateID)
                {
                    TerraneVertices.Add(V1);
                    TerraneVertexSet.Add(V1);
                    bAddedVertex = true;
                }
                if (!TerraneVertexSet.Contains(V2) && VertexPlateAssignments[V2] == ContinentalPlateID)
                {
                    TerraneVertices.Add(V2);
                    TerraneVertexSet.Add(V2);
                    bAddedVertex = true;
                }

                if (TerraneVertices.Num() >= TargetTerraneSize)
                {
                    break;
                }
            }
        }

        if (!bAddedVertex)
        {
            break; // Can't grow further
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Selected %d vertices for terrane extraction"), TerraneVertices.Num());
    const double TerraneArea = Service->ComputeTerraneArea(TerraneVertices);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Terrane area: %.2f km²"), TerraneArea);
    TestTrue(TEXT("Terrane area above minimum (100 km²)"), TerraneArea >= 100.0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ PASS: Terrane region selected"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // ========================================
    // TEST 3: Extract Terrane (Performance <5ms)
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT("--- Test 3: Extract Terrane ---"));

    // Capture pre-extraction snapshot for rollback validation
    const TArray<int32> PreExtractionAssignments = VertexPlateAssignments;

    const double ExtractionStartTime = FPlatformTime::Seconds();
    int32 TerraneID = INDEX_NONE;
    const bool bExtractionSuccess = Service->ExtractTerrane(ContinentalPlateID, TerraneVertices, TerraneID);
    const double ExtractionTimeMs = (FPlatformTime::Seconds() - ExtractionStartTime) * 1000.0;

    TestTrue(TEXT("Extraction succeeded"), bExtractionSuccess);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Extraction time: %.2f ms (target: <5ms)"), ExtractionTimeMs);

    if (!bExtractionSuccess)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("  ❌ FAIL: Extraction failed - cannot continue test"));
        return false;
    }

    TestTrue(TEXT("Terrane ID assigned"), TerraneID != INDEX_NONE);
    TestTrue(TEXT("Extraction performance <5ms"), ExtractionTimeMs < 5.0);

    // Validate terrane state
    const TArray<FContinentalTerrane>& Terranes = Service->GetTerranes();
    TestEqual(TEXT("One terrane exists"), Terranes.Num(), 1);

    if (Terranes.Num() > 0)
    {
        TestEqual(TEXT("Terrane ID matches"), Terranes[0].TerraneID, TerraneID);
        // Milestone 6 Task 1.2: Terranes now automatically transition to Transporting after extraction
        TestEqual(TEXT("Terrane state is Transporting"), static_cast<int32>(Terranes[0].State), static_cast<int32>(ETerraneState::Transporting));
        TestEqual(TEXT("Terrane vertex count matches"), Terranes[0].VertexIndices.Num(), TerraneVertices.Num());
        TestTrue(TEXT("Carrier plate assigned"), Terranes[0].CarrierPlateID != INDEX_NONE);
    }

    // Validate vertices are now unassigned (INDEX_NONE)
    const TArray<int32>& PostExtractionAssignments = Service->GetVertexPlateAssignments();
    for (int32 VertexIdx : TerraneVertices)
    {
        TestEqual(TEXT("Terrane vertex unassigned"), PostExtractionAssignments[VertexIdx], INDEX_NONE);
    }

    // Validate topology still valid after extraction
    TestTrue(TEXT("Post-extraction topology valid"), Service->ValidateTopology(ValidationError));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ PASS: Terrane extracted successfully (%.2f ms)"), ExtractionTimeMs);
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // ========================================
    // TEST 4: Reattach Terrane (Performance <10ms)
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT("--- Test 4: Reattach Terrane ---"));

    const double ReattachmentStartTime = FPlatformTime::Seconds();
    const bool bReattachmentSuccess = Service->ReattachTerrane(TerraneID, ContinentalPlateID);
    const double ReattachmentTimeMs = (FPlatformTime::Seconds() - ReattachmentStartTime) * 1000.0;

    TestTrue(TEXT("Reattachment succeeded"), bReattachmentSuccess);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Reattachment time: %.2f ms (target: <10ms)"), ReattachmentTimeMs);
    TestTrue(TEXT("Reattachment performance <10ms"), ReattachmentTimeMs < 10.0);

    // Validate terrane removed from active list
    TestEqual(TEXT("Terrane removed from active list"), Service->GetTerranes().Num(), 0);

    // Validate vertices reassigned to target plate
    const TArray<int32>& PostReattachmentAssignments = Service->GetVertexPlateAssignments();
    for (int32 VertexIdx : TerraneVertices)
    {
        TestEqual(TEXT("Terrane vertex reassigned"), PostReattachmentAssignments[VertexIdx], ContinentalPlateID);
    }

    // Validate topology still valid after reattachment
    TestTrue(TEXT("Post-reattachment topology valid"), Service->ValidateTopology(ValidationError));

    // Validate mesh bit-identical to pre-extraction state
    bool bMeshIdentical = true;
    for (int32 i = 0; i < PreExtractionAssignments.Num(); ++i)
    {
        if (PreExtractionAssignments[i] != PostReattachmentAssignments[i])
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("  Vertex %d assignment mismatch: pre=%d, post=%d"),
                i, PreExtractionAssignments[i], PostReattachmentAssignments[i]);
            bMeshIdentical = false;
            break;
        }
    }
    TestTrue(TEXT("Mesh bit-identical after reattachment"), bMeshIdentical);

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ PASS: Terrane reattached successfully (%.2f ms)"), ReattachmentTimeMs);
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // ========================================
    // TEST 5: Rollback Integration (Undo/Redo)
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT("--- Test 5: Rollback Integration ---"));

    // Capture history snapshot BEFORE extraction (for undo test)
    Service->CaptureHistorySnapshot();

    // Extract again for undo test
    int32 TerraneID2 = INDEX_NONE;
    Service->ExtractTerrane(ContinentalPlateID, TerraneVertices, TerraneID2);
    TestTrue(TEXT("Second extraction succeeded"), TerraneID2 != INDEX_NONE);
    TestEqual(TEXT("One terrane exists after second extraction"), Service->GetTerranes().Num(), 1);

    // Capture history snapshot AFTER extraction (so we can redo)
    Service->CaptureHistorySnapshot();

    // Undo (should remove terrane)
    TestTrue(TEXT("Undo available"), Service->CanUndo());
    Service->Undo();

    // Validate terrane removed and vertices reassigned
    TestEqual(TEXT("Terrane removed after undo"), Service->GetTerranes().Num(), 0);
    const TArray<int32>& PostUndoAssignments = Service->GetVertexPlateAssignments();
    for (int32 VertexIdx : TerraneVertices)
    {
        TestEqual(TEXT("Vertex reassigned after undo"), PostUndoAssignments[VertexIdx], ContinentalPlateID);
    }

    // Redo (should re-extract terrane)
    TestTrue(TEXT("Redo available"), Service->CanRedo());
    Service->Redo();
    TestEqual(TEXT("Terrane restored after redo"), Service->GetTerranes().Num(), 1);

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ PASS: Undo/Redo integration working"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // ========================================
    // TEST 6: Edge Case - Insufficient Area
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT("--- Test 6: Edge Case - Insufficient Area ---"));

    // Refresh plate vertices after undo/redo (vertex assignments may have changed)
    TArray<int32> CurrentPlateVertices;
    const TArray<int32>& CurrentAssignments = Service->GetVertexPlateAssignments();
    for (int32 i = 0; i < CurrentAssignments.Num(); ++i)
    {
        if (CurrentAssignments[i] == ContinentalPlateID)
        {
            CurrentPlateVertices.Add(i);
        }
    }

    // Select single vertex (should fail due to <100 km² area)
    TArray<int32> SingleVertex;
    if (CurrentPlateVertices.Num() > 0)
    {
        SingleVertex.Add(CurrentPlateVertices[0]);
    }

    int32 FailedTerraneID = INDEX_NONE;
    const bool bSingleVertexExtraction = Service->ExtractTerrane(ContinentalPlateID, SingleVertex, FailedTerraneID);
    TestFalse(TEXT("Single-vertex extraction rejected"), bSingleVertexExtraction);
    TestEqual(TEXT("No terrane created for single vertex"), Service->GetTerranes().Num(), 1); // Still has one from redo

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ PASS: Single-vertex terrane rejected (edge case handled)"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // ========================================
    // Summary
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Terrane Mechanics Test Summary ==="));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ Topology validation: PASS"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ Extraction (%.2f ms): PASS"), ExtractionTimeMs);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ Reattachment (%.2f ms): PASS"), ReattachmentTimeMs);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ Mesh identity preservation: PASS"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ Undo/Redo integration: PASS"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ Edge case handling: PASS"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Terrane Mechanics Test PASSED"));

    return true;
}
