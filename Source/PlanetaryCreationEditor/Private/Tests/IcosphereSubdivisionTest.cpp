#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 3 Task 1.1: Test icosphere subdivision topology correctness.
 * Validates Euler characteristic (V - E + F = 2) at each subdivision level.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIcosphereSubdivisionTopologyTest,
    "PlanetaryCreation.Milestone3.IcosphereSubdivision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIcosphereSubdivisionTopologyTest::RunTest(const FString& Parameters)
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

    // Test each subdivision level 0-6
    for (int32 Level = 0; Level <= 6; ++Level)
    {
        FTectonicSimulationParameters Params;
        Params.Seed = 42;
        Params.RenderSubdivisionLevel = Level;
        Service->SetParameters(Params);

        const TArray<FVector3d>& RenderVertices = Service->GetRenderVertices();
        const TArray<int32>& RenderTriangles = Service->GetRenderTriangles();

        const int32 V = RenderVertices.Num();
        const int32 F = RenderTriangles.Num() / 3;
        const int32 E = (F * 3) / 2; // Each edge shared by 2 faces in closed mesh

        // Validate Euler characteristic for closed polyhedron: V - E + F = 2
        const int32 EulerChar = V - E + F;

        TestEqual(FString::Printf(TEXT("Subdivision Level %d: Euler characteristic"), Level), EulerChar, 2);

        // Validate expected face count: Level N has 20 * 4^N faces
        const int32 ExpectedFaces = 20 * FMath::Pow(4.0f, static_cast<float>(Level));
        TestEqual(FString::Printf(TEXT("Subdivision Level %d: Face count"), Level), F, ExpectedFaces);

        // Validate all vertices are on unit sphere (within epsilon)
        const double Epsilon = 1e-6;
        int32 InvalidVertexCount = 0;
        for (const FVector3d& Vertex : RenderVertices)
        {
            const double Length = Vertex.Length();
            if (FMath::Abs(Length - 1.0) > Epsilon)
            {
                InvalidVertexCount++;
            }
        }

        TestEqual(FString::Printf(TEXT("Subdivision Level %d: Vertices on unit sphere"), Level), InvalidVertexCount, 0);

        AddInfo(FString::Printf(TEXT("Level %d: %d vertices, %d faces, Euler χ=%d"), Level, V, F, EulerChar));
    }

    return true;
#else
    AddError(TEXT("Test requires WITH_EDITOR"));
    return false;
#endif
}
