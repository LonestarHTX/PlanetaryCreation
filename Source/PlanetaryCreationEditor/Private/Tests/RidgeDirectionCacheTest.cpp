#include "Misc/AutomationTest.h"
#include "Utilities/PlanetaryCreationLogging.h"
#include "Simulation/TectonicSimulationService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidgeDirectionCacheTest,
    "PlanetaryCreation.StageB.RidgeDirectionCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRidgeDirectionCacheTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
    {
        return false;
    }

    Service->ResetSimulation();
    Service->ProcessPendingOceanicGPUReadbacks(true);
    Service->ProcessPendingContinentalGPUReadbacks(true);

    Service->BuildRenderVertexBoundaryCache();
    Service->MarkAllRidgeDirectionsDirty();
    Service->ForceRidgeRecomputeForTest();

    const TArray<FVector3d>& RidgeDirections = Service->GetVertexRidgeDirections();
    const TArray<UTectonicSimulationService::FRenderVertexBoundaryInfo>& BoundaryCache = Service->GetRenderVertexBoundaryCache();
    const TArray<int32>& PlateAssignments = Service->GetVertexPlateAssignments();
    const TArray<double>& CrustAges = Service->GetVertexCrustAge();
    const TArray<FTectonicPlate>& Plates = Service->GetPlates();
    const FTectonicSimulationParameters Params = Service->GetParameters();

    const double InfluenceRadians = FMath::Max(Params.RidgeBoundaryInfluenceRadians, 0.0);

    TestEqual(TEXT("Ridge cache and ridge direction array sizes match"),
        BoundaryCache.Num(), RidgeDirections.Num());

    int32 CandidateCount = 0;
    int32 AlignmentCount = 0;
    int32 LoggedMismatches = 0;

    for (int32 VertexIdx = 0; VertexIdx < BoundaryCache.Num(); ++VertexIdx)
    {
        if (!PlateAssignments.IsValidIndex(VertexIdx) || !RidgeDirections.IsValidIndex(VertexIdx))
        {
            continue;
        }

        const UTectonicSimulationService::FRenderVertexBoundaryInfo& Info = BoundaryCache[VertexIdx];
        if (!Info.bHasBoundary || !Info.bIsDivergent || Info.SourcePlateID != PlateAssignments[VertexIdx])
        {
            continue;
        }

        if (Info.BoundaryTangent.IsNearlyZero())
        {
            continue;
        }

        const double DistanceRad = FMath::Max(static_cast<double>(Info.DistanceRadians), 0.0);
        if (InfluenceRadians > UE_DOUBLE_SMALL_NUMBER && DistanceRad > InfluenceRadians)
        {
            continue;
        }

        const int32 PlateId = PlateAssignments[VertexIdx];
        if (!Plates.IsValidIndex(PlateId) || Plates[PlateId].CrustType != ECrustType::Oceanic)
        {
            continue;
        }

        if (!CrustAges.IsValidIndex(VertexIdx) || CrustAges[VertexIdx] > 15.0)
        {
            continue;
        }

        ++CandidateCount;

        const FVector3d CachedTangent = Info.BoundaryTangent.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZeroVector);
        const FVector3d RidgeDir = RidgeDirections[VertexIdx].GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZeroVector);
        const double CosTheta = FMath::Abs(CachedTangent | RidgeDir);

        if (CosTheta >= 0.9)
        {
            ++AlignmentCount;
        }
        else if (LoggedMismatches < 5)
        {
            AddError(FString::Printf(TEXT("Ridge cache tangent misaligned at vertex %d (cos=%.3f, dist=%.2f°)"),
                VertexIdx,
                CosTheta,
                FMath::RadiansToDegrees(DistanceRad)));
            ++LoggedMismatches;
        }
    }

    TestTrue(TEXT("Ridge cache produced candidates"), CandidateCount > 0);

    if (CandidateCount > 0)
    {
        const double AlignmentRatio = static_cast<double>(AlignmentCount) / static_cast<double>(CandidateCount);
        TestTrue(TEXT("Cached ridge tangents align with computed ridge directions"), AlignmentRatio >= 0.9);

        AddInfo(FString::Printf(TEXT("[RidgeDirectionCacheTest] Candidates=%d WellAligned=%d (%.1f%%) Influence=%.2f°"),
            CandidateCount,
            AlignmentCount,
            AlignmentRatio * 100.0,
            FMath::RadiansToDegrees(InfluenceRadians)));
    }

    return true;
}
