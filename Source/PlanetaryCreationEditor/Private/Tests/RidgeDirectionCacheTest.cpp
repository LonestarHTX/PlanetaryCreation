// Milestone 6: Ridge direction cache regression test

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidgeDirectionCacheTest,
    "PlanetaryCreation.Milestone6.RidgeDirectionCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRidgeDirectionCacheTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
    {
        return false;
    }

    FTectonicSimulationParameters Params;
    Params.Seed = 1337;
    Params.SubdivisionLevel = 0;
    Params.RenderSubdivisionLevel = 5;
    Params.bEnableDynamicRetessellation = false;
    Params.bEnableOceanicAmplification = true;
    Params.MinAmplificationLOD = 5;
    Params.RidgeDirectionDirtyRingDepth = 1;
    Service->SetParameters(Params);

    Service->ForceRidgeRecomputeForTest();

    const TArray<FVector3d>& RenderVertices = Service->GetRenderVertices();
    const int32 VertexCount = RenderVertices.Num();
    TestTrue(TEXT("Render mesh should contain vertices at L5"), VertexCount > 0);

    if (VertexCount <= 0)
    {
        return false;
    }

    TestEqual(TEXT("Initial ridge compute touches all vertices"), Service->GetLastRidgeDirectionUpdateCount(), VertexCount);

    Service->ForceRidgeRecomputeForTest();
    TestEqual(TEXT("No new dirty vertices leads to zero ridge updates"), Service->GetLastRidgeDirectionUpdateCount(), 0);

    TArray<int32> Seeds;
    Seeds.Add(0);
    Service->MarkRidgeRingDirty(Seeds, 0);
    Service->ForceRidgeRecomputeForTest();
    TestEqual(TEXT("Single dirty vertex updates once"), Service->GetLastRidgeDirectionUpdateCount(), 1);

    Service->MarkRidgeRingDirty(Seeds, Params.RidgeDirectionDirtyRingDepth);
    Service->ForceRidgeRecomputeForTest();
    const int32 RingUpdateCount = Service->GetLastRidgeDirectionUpdateCount();

    const TArray<int32>& Offsets = Service->GetRenderVertexAdjacencyOffsets();
    const TArray<int32>& Adjacency = Service->GetRenderVertexAdjacency();
    int32 ExpectedMax = 1;
    if (Offsets.Num() >= 2)
    {
        ExpectedMax += Offsets[1] - Offsets[0];
    }

    TestTrue(TEXT("Ring dirty propagation remains bounded"),
        RingUpdateCount > 0 && RingUpdateCount <= ExpectedMax);

    // Ensure clearing works for subsequent frames
    Service->ForceRidgeRecomputeForTest();
    TestEqual(TEXT("Dirty mask clears after recompute"), Service->GetLastRidgeDirectionUpdateCount(), 0);

    return true;
}
