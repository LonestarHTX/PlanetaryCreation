// Milestone 6 Task 1.0: Terrane Mesh Surgery Spike
// Risk mitigation: Validate mesh topology at L3 before implementing extraction/reattachment

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 6 Task 1.0: Terrane Mesh Surgery Spike
 *
 * GOAL: Validate baseline topology at production scale (L3: 642 vertices) and document
 * design requirements for Task 1.1 terrane extraction/reattachment implementation.
 *
 * This is a DESIGN SPIKE - not a full implementation. It validates:
 * 1. Baseline mesh topology is valid (Euler characteristic, manifold edges, no orphans)
 * 2. Mesh surgery preserves these properties (to be validated in Task 1.1)
 * 3. Edge cases are documented with mitigations
 *
 * Full extraction/reattachment will be implemented in Task 1.1 based on these findings.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraneMeshSurgerySpike,
    "PlanetaryCreation.Milestone6.TerraneMeshSurgerySpike",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraneMeshSurgerySpike::RunTest(const FString& Parameters)
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
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Milestone 6 Task 1.0: Terrane Mesh Surgery Spike ==="));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("GOAL: Validate topology at L3 before implementing terrane extraction"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    // Initialize simulation at Level 3 (ship-critical LOD: 642 vertices)
    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.SubdivisionLevel = 1; // 80 plates
    Params.RenderSubdivisionLevel = 3; // 642 vertices (1,280 triangles)
    Params.LloydIterations = 2;
    Params.bEnableDynamicRetessellation = false;
    Service->SetParameters(Params);

    // Get baseline mesh state
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
    // VALIDATION 1: Euler Characteristic
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT("--- Validation 1: Euler Characteristic ---"));

    // Compute edge count
    TSet<TPair<int32, int32>> UniqueEdges;
    for (int32 i = 0; i < RenderTriangles.Num(); i += 3)
    {
        int32 V0 = RenderTriangles[i];
        int32 V1 = RenderTriangles[i + 1];
        int32 V2 = RenderTriangles[i + 2];

        // Add 3 edges (canonical order: min vertex first to avoid duplicates)
        UniqueEdges.Add(MakeTuple(FMath::Min(V0, V1), FMath::Max(V0, V1)));
        UniqueEdges.Add(MakeTuple(FMath::Min(V1, V2), FMath::Max(V1, V2)));
        UniqueEdges.Add(MakeTuple(FMath::Min(V2, V0), FMath::Max(V2, V0)));
    }

    int32 V = RenderVertices.Num();
    int32 E = UniqueEdges.Num();
    int32 F = RenderTriangles.Num() / 3;
    int32 EulerChar = V - E + F;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  V = %d, E = %d, F = %d"), V, E, F);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  V - E + F = %d (should be 2 for sphere)"), EulerChar);

    bool bEulerValid = (EulerChar == 2);
    TestEqual(TEXT("Euler characteristic = 2"), EulerChar, 2);

    if (bEulerValid)
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ PASS: Euler characteristic valid"));
    }
    else
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("  ❌ FAIL: Euler characteristic invalid (mesh not a valid sphere)"));
    }

    // ========================================
    // VALIDATION 2: Manifold Edges
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("--- Validation 2: Manifold Edges ---"));

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
            if (NonManifoldEdges <= 5) // Log first 5 only
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("  Non-manifold edge: (%d, %d) appears %d times (should be 2)"),
                    Pair.Key.Key, Pair.Key.Value, Pair.Value);
            }
        }
    }

    bool bManifold = (NonManifoldEdges == 0);
    TestEqual(TEXT("Non-manifold edge count"), NonManifoldEdges, 0);

    if (bManifold)
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ PASS: All %d edges manifold (each touches exactly 2 triangles)"), E);
    }
    else
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("  ❌ FAIL: %d non-manifold edges found"), NonManifoldEdges);
    }

    // ========================================
    // VALIDATION 3: No Orphaned Vertices
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("--- Validation 3: No Orphaned Vertices ---"));

    TSet<int32> ReferencedVertices;
    for (int32 i = 0; i < RenderTriangles.Num(); ++i)
    {
        ReferencedVertices.Add(RenderTriangles[i]);
    }

    int32 OrphanedVertices = RenderVertices.Num() - ReferencedVertices.Num();
    bool bNoOrphans = (OrphanedVertices == 0);
    TestEqual(TEXT("Orphaned vertex count"), OrphanedVertices, 0);

    if (bNoOrphans)
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ PASS: All %d vertices referenced by triangles"), RenderVertices.Num());
    }
    else
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("  ❌ FAIL: %d orphaned vertices found"), OrphanedVertices);
    }

    // ========================================
    // SPIKE FINDINGS & RECOMMENDATIONS
    // ========================================
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== SPIKE FINDINGS ==="));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Baseline topology at L3 (642 vertices):"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Euler characteristic: %s"), bEulerValid ? TEXT("VALID") : TEXT("INVALID"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Manifold edges: %s"), bManifold ? TEXT("VALID") : TEXT("INVALID"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  No orphaned vertices: %s"), bNoOrphans ? TEXT("VALID") : TEXT("INVALID"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    UE_LOG(LogPlanetaryCreation, Log, TEXT("RECOMMENDATIONS FOR TASK 1.1 (Terrane Extraction System):"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    UE_LOG(LogPlanetaryCreation, Log, TEXT("1. TOPOLOGY VALIDATION (Critical):"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("   - After extraction: V_new - E_new + F_new = 2"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("   - After extraction: All edges manifold (count = 2)"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("   - After extraction: No orphaned vertices"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("   - After reattachment: Mesh identical to pre-extraction state"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    UE_LOG(LogPlanetaryCreation, Log, TEXT("2. EDGE CASE MITIGATIONS (High Priority):"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("   a. Single-vertex terrane → Merge with nearest plate (min area: 100 km²)"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("   b. Terrane spanning boundary → Snap to plate boundary (flood-fill from centroid)"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("   c. Single-terrane plate → Treat as plate split (source deleted, terrane = new plate)"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("   d. Extraction during retess → Defer until retess completes"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("   e. Reattach to subducting → Allow but trigger immediate slab breakoff"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    UE_LOG(LogPlanetaryCreation, Log, TEXT("3. PERFORMANCE TARGETS (Task 1.1 Validation):"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("   - Extraction <5ms at L3 (642 vertices)"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("   - Reattachment <10ms at L3"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("   - Benchmark at L5/L6 to document scaling"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    UE_LOG(LogPlanetaryCreation, Log, TEXT("4. INTEGRATION TESTING (Task 1.1 Automation):"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("   - Extract → Retessellate → Verify indices valid"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("   - Extract → Rollback → Verify bit-identical mesh"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("   - Extract → Advance 100 steps → Reattach → Verify collision"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== SPIKE COMPLETE ==="));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Status: %s"), (bEulerValid && bManifold && bNoOrphans) ? TEXT("READY FOR TASK 1.1") : TEXT("BLOCKING ISSUES"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Next: Implement extraction/reattachment in Task 1.1 based on these findings"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));

    return bEulerValid && bManifold && bNoOrphans;
}
