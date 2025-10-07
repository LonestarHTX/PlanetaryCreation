#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

#include "Containers/Queue.h"
#include "Containers/Set.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraneMeshSurgeryTest,
    "PlanetaryCreation.Milestone6.Terrane.MeshSurgery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraneMeshSurgeryTest::RunTest(const FString& Parameters)
{
    if (!GEditor)
    {
        AddError(TEXT("GEditor unavailable"));
        return false;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to acquire TectonicSimulationService"));
        return false;
    }

    Service->ResetSimulation();

    FTectonicSimulationParameters Params = Service->GetParameters();
    Params.RenderSubdivisionLevel = 3; // Level 3 mesh (642 verts)
    Params.bEnableDynamicRetessellation = false;
    Params.bEnableAutomaticLOD = false;
    Service->SetParameters(Params);
    Service->SetRenderSubdivisionLevel(3);
    Service->BuildRenderVertexAdjacency();

    const TArray<int32>& PlateAssignments = Service->GetVertexPlateAssignments();
    const TArray<FTectonicPlate>& Plates = Service->GetPlates();

    int32 ContinentalPlateID = INDEX_NONE;
    for (const FTectonicPlate& Plate : Plates)
    {
        if (Plate.CrustType == ECrustType::Continental)
        {
            ContinentalPlateID = Plate.PlateID;
            break;
        }
    }

    TestTrue(TEXT("Found continental plate"), ContinentalPlateID != INDEX_NONE);
    if (ContinentalPlateID == INDEX_NONE)
    {
        return false;
    }

    TArray<int32> CandidateVertices;
    CandidateVertices.Reserve(64);

    const TArray<int32>& AdjacencyOffsets = Service->GetRenderVertexAdjacencyOffsets();
    const TArray<int32>& Adjacency = Service->GetRenderVertexAdjacency();
    const int32 VertexCount = Service->GetRenderVertices().Num();

    TestTrue(TEXT("Adjacency offsets size"), AdjacencyOffsets.Num() == VertexCount + 1);
    if (AdjacencyOffsets.Num() != VertexCount + 1)
    {
        return false;
    }

    int32 SeedIndex = INDEX_NONE;
    for (int32 Index = 0; Index < PlateAssignments.Num(); ++Index)
    {
        if (PlateAssignments[Index] == ContinentalPlateID)
        {
            SeedIndex = Index;
            break;
        }
    }

    TestTrue(TEXT("Found terrane seed on continental plate"), SeedIndex != INDEX_NONE);
    if (SeedIndex == INDEX_NONE)
    {
        return false;
    }

    TSet<int32> Visited;
    const int32 DesiredVertexCount = 24;

    TQueue<int32> Frontier;
    Frontier.Enqueue(SeedIndex);
    Visited.Add(SeedIndex);

    while (!Frontier.IsEmpty() && CandidateVertices.Num() < DesiredVertexCount)
    {
        int32 CurrentIndex;
        Frontier.Dequeue(CurrentIndex);
        CandidateVertices.Add(CurrentIndex);

        const int32 Start = AdjacencyOffsets[CurrentIndex];
        const int32 End = AdjacencyOffsets[CurrentIndex + 1];
        for (int32 NeighborIdx = Start; NeighborIdx < End; ++NeighborIdx)
        {
            const int32 Neighbor = Adjacency[NeighborIdx];
            if (!Visited.Contains(Neighbor) && PlateAssignments.IsValidIndex(Neighbor) && PlateAssignments[Neighbor] == ContinentalPlateID)
            {
                Visited.Add(Neighbor);
                Frontier.Enqueue(Neighbor);
            }
        }
    }

    TestTrue(TEXT("Collected sufficient connected vertices"), CandidateVertices.Num() >= 16);
    if (CandidateVertices.Num() < 16)
    {
        return false;
    }

    int32 TerraneID = INDEX_NONE;
    const bool bExtracted = Service->ExtractTerrane(ContinentalPlateID, CandidateVertices, TerraneID);
    TestTrue(TEXT("Terrane extraction succeeded"), bExtracted);
    if (!bExtracted)
    {
        return false;
    }

    const TArray<FContinentalTerrane>& Terranes = Service->GetTerranes();
    TestEqual(TEXT("Terrane count"), Terranes.Num(), 1);
    if (Terranes.Num() != 1)
    {
        return false;
    }

    const FContinentalTerrane& Terrane = Terranes[0];
    TestEqual(TEXT("Terrane payload size"), Terrane.VertexPayload.Num(), CandidateVertices.Num());
    TestTrue(TEXT("Terrane has extracted triangles"), Terrane.ExtractedTriangles.Num() > 0);
    TestTrue(TEXT("Terrane has patch vertices"), Terrane.PatchVertexIndices.Num() > 0);
    TestTrue(TEXT("Terrane has cap triangles"), Terrane.PatchTriangles.Num() > 0);

    // Immediately reattach to the original plate
    const bool bReattached = Service->ReattachTerrane(TerraneID, ContinentalPlateID);
    TestTrue(TEXT("Terrane reattachment succeeded"), bReattached);
    if (!bReattached)
    {
        return false;
    }

    TestEqual(TEXT("Terrane list empty after reattachment"), Service->GetTerranes().Num(), 0);

    const TArray<int32>& UpdatedAssignments = Service->GetVertexPlateAssignments();
    int32 NumUnassigned = 0;
    for (int32 Assignment : UpdatedAssignments)
    {
        if (Assignment == INDEX_NONE)
        {
            NumUnassigned++;
        }
    }
    TestEqual(TEXT("No unassigned render vertices remain"), NumUnassigned, 0);

    Service->ResetSimulation();

    return true;
}
