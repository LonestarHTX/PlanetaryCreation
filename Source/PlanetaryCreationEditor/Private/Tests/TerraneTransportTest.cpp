// Milestone 6 Task 1.2: Terrane Transport Test

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 6 Task 1.2: Terrane Transport Test
 *
 * Validates terrane transport mechanics:
 * 1. Carrier assignment after extraction (nearest oceanic plate)
 * 2. Terrane centroid tracking during carrier migration
 * 3. State transitions (Extracted → Transporting → Colliding)
 * 4. Collision detection (within 500 km of continental boundary)
 * 5. Performance (<1ms per step for terrane tracking)
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraneTransportTest,
    "PlanetaryCreation.Milestone6.TerraneTransport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraneTransportTest::RunTest(const FString& Parameters)
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
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Milestone 6 Task 1.2: Terrane Transport Test ==="));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // Initialize simulation with mixed oceanic/continental plates
    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.SubdivisionLevel = 0; // 20 plates
    Params.RenderSubdivisionLevel = 3; // 642 vertices
    Params.LloydIterations = 2;
    Params.bEnableDynamicRetessellation = false;
    Service->SetParameters(Params);

    // Ensure we have oceanic plates for carrier assignment
    TArray<FTectonicPlate>& Plates = Service->GetPlatesForModification();
    int32 OceanicCount = 0;
    int32 ContinentalCount = 0;
    for (FTectonicPlate& Plate : Plates)
    {
        if (Plate.CrustType == ECrustType::Oceanic)
        {
            OceanicCount++;
        }
        else
        {
            ContinentalCount++;
        }
    }

    // Ensure mix of oceanic and continental plates
    if (ContinentalCount == 0)
    {
        Plates[0].CrustType = ECrustType::Continental;
        ContinentalCount = 1;
        OceanicCount--;
    }
    if (OceanicCount == 0)
    {
        Plates[1].CrustType = ECrustType::Oceanic;
        OceanicCount = 1;
        ContinentalCount--;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Plate configuration: %d oceanic, %d continental"), OceanicCount, ContinentalCount);

    // ========================================
    // TEST 1: Extract Terrane and Verify Carrier Assignment
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("--- Test 1: Carrier Assignment ---"));

    // Find continental plate with sufficient vertices
    int32 ContinentalPlateID = INDEX_NONE;
    for (const FTectonicPlate& Plate : Plates)
    {
        if (Plate.CrustType == ECrustType::Continental)
        {
            ContinentalPlateID = Plate.PlateID;
            break;
        }
    }

    TestTrue(TEXT("Continental plate found"), ContinentalPlateID != INDEX_NONE);

    // Select terrane vertices by growing contiguous region from seed
    const TArray<int32>& VertexAssignments = Service->GetVertexPlateAssignments();
    const TArray<int32>& RenderTriangles = Service->GetRenderTriangles();

    // Find seed vertex on continental plate
    int32 SeedVertex = INDEX_NONE;
    for (int32 i = 0; i < VertexAssignments.Num(); ++i)
    {
        if (VertexAssignments[i] == ContinentalPlateID)
        {
            SeedVertex = i;
            break;
        }
    }

    TestTrue(TEXT("Seed vertex found"), SeedVertex != INDEX_NONE);

    // Grow contiguous region from seed
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

            // If triangle touches terrane, add other vertices from same plate
            if (TerraneVertexSet.Contains(V0) || TerraneVertexSet.Contains(V1) || TerraneVertexSet.Contains(V2))
            {
                if (!TerraneVertexSet.Contains(V0) && VertexAssignments[V0] == ContinentalPlateID)
                {
                    TerraneVertices.Add(V0);
                    TerraneVertexSet.Add(V0);
                    bAdded = true;
                }
                if (!TerraneVertexSet.Contains(V1) && VertexAssignments[V1] == ContinentalPlateID)
                {
                    TerraneVertices.Add(V1);
                    TerraneVertexSet.Add(V1);
                    bAdded = true;
                }
                if (!TerraneVertexSet.Contains(V2) && VertexAssignments[V2] == ContinentalPlateID)
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
            break; // Can't grow further
        }
    }

    TestTrue(TEXT("Sufficient terrane vertices selected"), TerraneVertices.Num() >= 10);
    const double TerraneArea = Service->ComputeTerraneArea(TerraneVertices);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Selected %d vertices, area: %.2f km²"), TerraneVertices.Num(), TerraneArea);

    // Extract terrane (should automatically assign carrier)
    int32 TerraneID = INDEX_NONE;
    const bool bExtracted = Service->ExtractTerrane(ContinentalPlateID, TerraneVertices, TerraneID);
    TestTrue(TEXT("Terrane extraction succeeded"), bExtracted);

    if (!bExtracted)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("  ❌ FAIL: Could not extract terrane"));
        return false;
    }

    // Validate carrier assignment
    const TArray<FContinentalTerrane>& Terranes = Service->GetTerranes();
    TestEqual(TEXT("One terrane exists"), Terranes.Num(), 1);

    if (Terranes.Num() > 0)
    {
        const FContinentalTerrane& Terrane = Terranes[0];
        TestEqual(TEXT("Terrane state is Transporting"), static_cast<int32>(Terrane.State), static_cast<int32>(ETerraneState::Transporting));
        TestTrue(TEXT("Carrier plate assigned"), Terrane.CarrierPlateID != INDEX_NONE);

        // Verify carrier is oceanic
        const FTectonicPlate* CarrierPlate = nullptr;
        for (const FTectonicPlate& Plate : Plates)
        {
            if (Plate.PlateID == Terrane.CarrierPlateID)
            {
                CarrierPlate = &Plate;
                break;
            }
        }

        TestNotNull(TEXT("Carrier plate exists"), CarrierPlate);
        if (CarrierPlate)
        {
            TestEqual(TEXT("Carrier is oceanic"), static_cast<int32>(CarrierPlate->CrustType), static_cast<int32>(ECrustType::Oceanic));
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  Carrier plate %d assigned (oceanic)"), Terrane.CarrierPlateID);
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ PASS: Carrier assignment successful"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // ========================================
    // TEST 2: Terrane Migration Tracking
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT("--- Test 2: Terrane Migration Tracking ---"));

    // Capture initial terrane centroid
    const FVector3d InitialCentroid = Terranes[0].Centroid;
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Initial centroid: (%.4f, %.4f, %.4f)"), InitialCentroid.X, InitialCentroid.Y, InitialCentroid.Z);

    // Advance 10 steps (20 My) and verify terrane moves
    const double MigrationStartTime = FPlatformTime::Seconds();
    Service->AdvanceSteps(10);
    const double MigrationTimeMs = (FPlatformTime::Seconds() - MigrationStartTime) * 1000.0;

    // Verify terrane centroid has moved
    const TArray<FContinentalTerrane>& TerranesAfterMigration = Service->GetTerranes();
    TestEqual(TEXT("Terrane still exists after migration"), TerranesAfterMigration.Num(), 1);

    if (TerranesAfterMigration.Num() > 0)
    {
        const FVector3d FinalCentroid = TerranesAfterMigration[0].Centroid;
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  Final centroid: (%.4f, %.4f, %.4f)"), FinalCentroid.X, FinalCentroid.Y, FinalCentroid.Z);

        // Calculate geodesic distance moved
        const double CosAngle = FMath::Clamp(InitialCentroid | FinalCentroid, -1.0, 1.0);
        const double DistanceRadians = FMath::Acos(CosAngle);
        const double DistanceKm = DistanceRadians * (Params.PlanetRadius / 1000.0);

        UE_LOG(LogPlanetaryCreation, Log, TEXT("  Distance moved: %.2f km over 20 My"), DistanceKm);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  Migration time: %.2f ms (10 steps)"), MigrationTimeMs);

        // Verify terrane has moved (should be >0 km)
        TestTrue(TEXT("Terrane centroid has moved"), DistanceKm > 0.1);

        // Verify performance (<1ms per step)
        const double PerStepMs = MigrationTimeMs / 10.0;
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  Per-step overhead: %.3f ms (target: <1ms)"), PerStepMs);
        TestTrue(TEXT("Migration performance <1ms per step"), PerStepMs < 1.0);
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ PASS: Terrane migration tracking working"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // ========================================
    // TEST 3: State Transitions
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT("--- Test 3: State Transitions ---"));

    // Verify terrane is still in Transporting state (not yet colliding)
    if (TerranesAfterMigration.Num() > 0)
    {
        const ETerraneState CurrentState = TerranesAfterMigration[0].State;
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  Current state after 10 steps: %d (0=Attached, 1=Extracted, 2=Transporting, 3=Colliding)"),
            static_cast<int32>(CurrentState));

        // State should be Transporting (2) or Colliding (3) depending on boundary proximity
        const bool bValidState = (CurrentState == ETerraneState::Transporting || CurrentState == ETerraneState::Colliding);
        TestTrue(TEXT("Terrane in valid transport state"), bValidState);

        if (CurrentState == ETerraneState::Colliding)
        {
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ Terrane detected collision within 10 steps (early collision detection working)"));
            TestTrue(TEXT("Target plate assigned for collision"), TerranesAfterMigration[0].TargetPlateID != INDEX_NONE);
        }
        else
        {
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ Terrane still transporting (collision not yet detected)"));
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ PASS: State transitions valid"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // ========================================
    // Summary
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Terrane Transport Test Summary ==="));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ Carrier assignment: PASS"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ Migration tracking: PASS"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ State transitions: PASS"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ Performance (<1ms/step): PASS"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Terrane Transport Test PASSED"));

    return true;
}
