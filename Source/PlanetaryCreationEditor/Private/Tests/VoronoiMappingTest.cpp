#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 3 Task 2.1: Test Voronoi mapping coverage and correctness.
 * Validates that all render vertices are assigned to valid plates.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiMappingCoverageTest,
    "PlanetaryCreation.Milestone3.VoronoiMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoronoiMappingCoverageTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!GEditor)
    {
        AddError(TEXT("GEditor is null - test requires editor context"));
        return false;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get UTectonicSimulationService"));
        return false;
    }

    // Test at subdivision level 6 (81,920 faces, stress test)
    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.RenderSubdivisionLevel = 6;
    Params.bEnableVoronoiWarping = false;
    Params.VoronoiWarpingAmplitude = 0.0;
    Params.VoronoiWarpingFrequency = 0.0;
    Service->SetParameters(Params);

    const TArray<FVector3d>& RenderVertices = Service->GetRenderVertices();
    const TArray<int32>& VertexPlateAssignments = Service->GetVertexPlateAssignments();
    const TArray<FTectonicPlate>& Plates = Service->GetPlates();

    // Validate array sizes match
    TestEqual(TEXT("VertexPlateAssignments size matches RenderVertices"),
        VertexPlateAssignments.Num(), RenderVertices.Num());

    // Validate all vertices assigned to valid plates
    int32 UnassignedCount = 0;
    int32 InvalidPlateIDCount = 0;

    for (int32 i = 0; i < VertexPlateAssignments.Num(); ++i)
    {
        const int32 PlateID = VertexPlateAssignments[i];

        if (PlateID == INDEX_NONE)
        {
            UnassignedCount++;
            continue;
        }

        // Verify plate ID exists
        bool bPlateIDExists = false;
        for (const FTectonicPlate& Plate : Plates)
        {
            if (Plate.PlateID == PlateID)
            {
                bPlateIDExists = true;
                break;
            }
        }

        if (!bPlateIDExists)
        {
            InvalidPlateIDCount++;
        }
    }

    TestEqual(TEXT("All vertices assigned (no INDEX_NONE)"), UnassignedCount, 0);
    TestEqual(TEXT("All plate IDs valid"), InvalidPlateIDCount, 0);

    // Validate Voronoi property: each vertex is closest to its assigned plate
    int32 VoronoiViolations = 0;
    const double Epsilon = 1e-9; // Tolerance for floating point comparison

    for (int32 i = 0; i < FMath::Min(100, RenderVertices.Num()); ++i) // Sample 100 vertices for performance
    {
        const FVector3d& Vertex = RenderVertices[i];
        const int32 AssignedPlateID = VertexPlateAssignments[i];

        // Find assigned plate
        const FTectonicPlate* AssignedPlate = nullptr;
        for (const FTectonicPlate& Plate : Plates)
        {
            if (Plate.PlateID == AssignedPlateID)
            {
                AssignedPlate = &Plate;
                break;
            }
        }

        if (!AssignedPlate)
        {
            continue; // Already counted in InvalidPlateIDCount
        }

        const double AssignedDist = FVector3d::Distance(Vertex, AssignedPlate->Centroid);

        // Check if any other plate is closer
        for (const FTectonicPlate& Plate : Plates)
        {
            if (Plate.PlateID == AssignedPlateID)
            {
                continue;
            }

            const double OtherDist = FVector3d::Distance(Vertex, Plate.Centroid);
            if (OtherDist < AssignedDist - Epsilon)
            {
                VoronoiViolations++;
                AddWarning(FString::Printf(TEXT("Vertex %d assigned to plate %d (dist %.6f) but closer to plate %d (dist %.6f)"),
                    i, AssignedPlateID, AssignedDist, Plate.PlateID, OtherDist));
                break;
            }
        }
    }

    TestEqual(TEXT("Voronoi property satisfied (vertices assigned to nearest plate)"), VoronoiViolations, 0);

    // Validate coverage: all plates should have at least one vertex assigned
    TSet<int32> AssignedPlateIDs;
    for (int32 PlateID : VertexPlateAssignments)
    {
        if (PlateID != INDEX_NONE)
        {
            AssignedPlateIDs.Add(PlateID);
        }
    }

    const int32 PlatesWithVertices = AssignedPlateIDs.Num();
    const int32 TotalPlates = Plates.Num();

    if (PlatesWithVertices < TotalPlates)
    {
        AddWarning(FString::Printf(TEXT("Only %d/%d plates have assigned vertices"), PlatesWithVertices, TotalPlates));
    }

    AddInfo(FString::Printf(TEXT("Voronoi mapping: %d vertices → %d plates, %d plates covered"),
        RenderVertices.Num(), TotalPlates, PlatesWithVertices));

    return true;
#else
    AddError(TEXT("Test requires WITH_EDITOR"));
    return false;
#endif
}
