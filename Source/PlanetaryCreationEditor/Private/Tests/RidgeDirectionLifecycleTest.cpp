#include "Misc/AutomationTest.h"
#include "Simulation/TectonicSimulationService.h"
#include "Editor.h"

#include "Containers/Map.h"
#include "Containers/Set.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidgeDirectionLifecycleTest,
    "PlanetaryCreation.StageB.RidgeDirectionLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRidgeDirectionLifecycleTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor ? GEditor->GetEditorSubsystem<UTectonicSimulationService>() : nullptr;
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
    {
        return false;
    }

    Service->ResetSimulation();
    Service->AdvanceSteps(4);
    Service->ForceRidgeRecomputeForTest();

    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries = Service->GetBoundaries();
    TMap<int32, TSet<TPair<int32, int32>>> VertexBoundaries;
    for (const auto& BoundaryPair : Boundaries)
    {
        const FPlateBoundary& Boundary = BoundaryPair.Value;
        for (int32 SharedVertex : Boundary.SharedEdgeVertices)
        {
            VertexBoundaries.FindOrAdd(SharedVertex).Add(BoundaryPair.Key);
        }
    }

    int32 TripleVertex = INDEX_NONE;
    for (const TPair<int32, TSet<TPair<int32, int32>>>& Entry : VertexBoundaries)
    {
        if (Entry.Value.Num() >= 3)
        {
            TripleVertex = Entry.Key;
            break;
        }
    }

    TestTrue(TEXT("Found ridge triple-junction vertex"), TripleVertex != INDEX_NONE);
    if (TripleVertex == INDEX_NONE)
    {
        return false;
    }

    const TArray<int32>& PlateAssignments = Service->GetVertexPlateAssignments();
    const TArray<FVector3d>& RenderVertices = Service->GetRenderVertices();
    TestEqual(TEXT("Plate assignments sized"), PlateAssignments.Num(), RenderVertices.Num());
    if (PlateAssignments.Num() != RenderVertices.Num())
    {
        return false;
    }

    const int32 PlateId = PlateAssignments.IsValidIndex(TripleVertex) ? PlateAssignments[TripleVertex] : INDEX_NONE;
    TestTrue(TEXT("Triple vertex has valid plate"), PlateId != INDEX_NONE);
    if (PlateId == INDEX_NONE)
    {
        return false;
    }

    int32 DiscontinuityVertex = INDEX_NONE;
    for (int32 Index = 0; Index < PlateAssignments.Num(); ++Index)
    {
        if (Index == TripleVertex)
        {
            continue;
        }
        if (PlateAssignments[Index] == PlateId)
        {
            DiscontinuityVertex = Index;
            break;
        }
    }

    TestTrue(TEXT("Found companion vertex on same plate"), DiscontinuityVertex != INDEX_NONE);
    if (DiscontinuityVertex == INDEX_NONE)
    {
        return false;
    }

    Service->SetVertexCrustAgeForTest(TripleVertex, 0.0);
    Service->SetVertexCrustAgeForTest(DiscontinuityVertex, 200.0);

    TArray<int32> SeedVertices;
    SeedVertices.Add(TripleVertex);
    SeedVertices.Add(DiscontinuityVertex);
    Service->ForceRidgeRingDirtyForTest(SeedVertices, 2);
    Service->AdvanceSteps(3);

    const double CacheHitPercent = Service->GetLastRidgeCacheHitPercent();
    const double GradientPercent = Service->GetLastRidgeGradientFallbackPercent();
    const double MotionPercent = Service->GetLastRidgeMotionFallbackPercent();

    AddInfo(FString::Printf(TEXT("[RidgeLifecycle] CacheHit=%.2f Gradient=%.2f Motion=%.3f"),
        CacheHitPercent, GradientPercent, MotionPercent));

    TestTrue(TEXT("Cache hit threshold (>=99%)"), CacheHitPercent >= 99.0 - KINDA_SMALL_NUMBER);
    TestTrue(TEXT("Gradient fallback threshold (<=1%)"), GradientPercent <= 1.0 + KINDA_SMALL_NUMBER);
    TestTrue(TEXT("Motion fallback threshold (<=0.1%)"), MotionPercent <= 0.1 + KINDA_SMALL_NUMBER);

    return CacheHitPercent >= 99.0 - KINDA_SMALL_NUMBER &&
        GradientPercent <= 1.0 + KINDA_SMALL_NUMBER &&
        MotionPercent <= 0.1 + KINDA_SMALL_NUMBER;
}
