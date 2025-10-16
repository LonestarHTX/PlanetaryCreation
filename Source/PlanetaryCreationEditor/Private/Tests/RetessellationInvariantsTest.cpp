#include "Utilities/PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "Simulation/TectonicSimulationService.h"
#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRetessellationInvariantsTest,
    "PlanetaryCreation.Milestone4.RetessellationInvariants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRetessellationInvariantsTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("Simulation service must exist"), Service);
    if (!Service)
    {
        return false;
    }

    FTectonicSimulationParameters Params;
    Params.Seed = 31415;
    Params.RenderSubdivisionLevel = 4; // 5120 faces
    Params.SubdivisionLevel = 0;
    Params.bEnableDynamicRetessellation = false; // manual trigger

    Service->SetParameters(Params);

    // Capture initial crust types for invariance check
    const TArray<FTectonicPlate>& InitialPlates = Service->GetPlates();
    TArray<ECrustType> InitialCrustTypes;
    InitialCrustTypes.Reserve(InitialPlates.Num());
    for (const FTectonicPlate& Plate : InitialPlates)
    {
        InitialCrustTypes.Add(Plate.CrustType);
    }

    // Force drift by applying angular velocity and advancing steps
    TArray<FTectonicPlate>& Plates = Service->GetPlatesForModification();
    for (int32 PlateIdx = 0; PlateIdx < Plates.Num(); ++PlateIdx)
    {
        Plates[PlateIdx].AngularVelocity = 0.05; // rad/My
    }
    Service->AdvanceSteps(25);

    const int32 ExpectedFaceCount = 20 * static_cast<int32>(FMath::RoundToInt(FMath::Pow(4.0, Params.RenderSubdivisionLevel)));

    const bool bRetessSuccess = Service->PerformRetessellation();
    TestTrue(TEXT("PerformRetessellation should succeed"), bRetessSuccess);

    const int32 ActualTriangles = Service->GetRenderTriangles().Num() / 3;
    TestEqual(TEXT("Retessellation should preserve triangle count"), ActualTriangles, ExpectedFaceCount);

    const int32 VertexCount = Service->GetRenderVertices().Num();
    TestEqual(TEXT("Amplified elevation matches vertex count"), Service->GetVertexAmplifiedElevation().Num(), VertexCount);
    TestEqual(TEXT("Adjacency offsets size matches vertex count"), Service->GetRenderVertexAdjacencyOffsets().Num(), VertexCount + 1);

    const TArray<FTectonicPlate>& PlatesAfter = Service->GetPlates();
    TestEqual(TEXT("Plate count invariant"), PlatesAfter.Num(), InitialCrustTypes.Num());
    for (int32 PlateIdx = 0; PlateIdx < PlatesAfter.Num(); ++PlateIdx)
    {
        TestEqual(TEXT("Crust type preserved"), PlatesAfter[PlateIdx].CrustType, InitialCrustTypes[PlateIdx]);
    }

    Service->ResetSimulation();
    return true;
}
