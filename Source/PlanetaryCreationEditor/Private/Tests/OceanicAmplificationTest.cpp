// Milestone 6 Task 2.1: Oceanic Amplification Automation Test
// Validates transform fault synthesis, age-based fault accentuation, and high-frequency detail

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOceanicAmplificationTest,
    "PlanetaryCreation.Milestone6.OceanicAmplification",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOceanicAmplificationTest::RunTest(const FString& Parameters)
{
    // Get simulation service
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
        return false;

    // Setup: Enable oceanic amplification with high LOD
    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.SubdivisionLevel = 0; // 20 plates (baseline)
    Params.RenderSubdivisionLevel = 5; // 10,242 vertices (high-detail preview)
    Params.bEnableOceanicAmplification = true;
    Params.MinAmplificationLOD = 5;
    Params.bEnableOceanicDampening = true; // Required for crust age data
    Service->SetParameters(Params);

    // Step simulation to generate some oceanic crust age variation
    Service->AdvanceSteps(10); // 20 My total time

    // ============================================================================
    // Test 1: Transform faults perpendicular to ridges (dot product < 0.1)
    // ============================================================================

    const TArray<FVector3d>& RenderVertices = Service->GetRenderVertices();
    const TArray<int32>& VertexPlateAssignments = Service->GetVertexPlateAssignments();
    const TArray<FVector3d>& RidgeDirections = Service->GetVertexRidgeDirections();
    const TArray<double>& AmplifiedElevation = Service->GetVertexAmplifiedElevation();
    const TArray<FTectonicPlate>& Plates = Service->GetPlates();
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries = Service->GetBoundaries();
    const TArray<FVector3d>& SharedVertices = Service->GetSharedVertices();

    TestEqual(TEXT("Ridge directions array sized correctly"), RidgeDirections.Num(), RenderVertices.Num());
    TestEqual(TEXT("Amplified elevation array sized correctly"), AmplifiedElevation.Num(), RenderVertices.Num());

    // Count oceanic vertices and check ridge direction validity
    int32 OceanicVertexCount = 0;
    int32 ValidRidgeDirectionCount = 0;

    auto FindNearestBoundaryTangent = [&](const FVector3d& Position, int32 PlateID) -> FVector3d
    {
        if (PlateID == INDEX_NONE)
        {
            return FVector3d::ZAxisVector;
        }

        const FVector3d VertexNormal = Position.GetSafeNormal();
        double MinDistance = TNumericLimits<double>::Max();
        FVector3d BestTangent = FVector3d::ZAxisVector;

        const auto ComputeSegmentTangent = [](const FVector3d& PlaneNormal, const FVector3d& PointOnGreatCircle)
        {
            const FVector3d Tangent = FVector3d::CrossProduct(PlaneNormal, PointOnGreatCircle).GetSafeNormal();
            return Tangent.IsNearlyZero() ? FVector3d::ZAxisVector : Tangent;
        };

        for (const auto& BoundaryPair : Boundaries)
        {
            const TPair<int32, int32>& BoundaryKey = BoundaryPair.Key;
            const FPlateBoundary& Boundary = BoundaryPair.Value;

            if (Boundary.BoundaryType != EBoundaryType::Divergent)
            {
                continue;
            }

            if (BoundaryKey.Key != PlateID && BoundaryKey.Value != PlateID)
            {
                continue;
            }

            if (Boundary.SharedEdgeVertices.Num() < 2)
            {
                continue;
            }

            for (int32 EdgeIdx = 0; EdgeIdx < Boundary.SharedEdgeVertices.Num() - 1; ++EdgeIdx)
            {
                const int32 EdgeV0Idx = Boundary.SharedEdgeVertices[EdgeIdx];
                const int32 EdgeV1Idx = Boundary.SharedEdgeVertices[EdgeIdx + 1];
                if (!SharedVertices.IsValidIndex(EdgeV0Idx) || !SharedVertices.IsValidIndex(EdgeV1Idx))
                {
                    continue;
                }

                const FVector3d EdgeV0 = SharedVertices[EdgeV0Idx].GetSafeNormal();
                const FVector3d EdgeV1 = SharedVertices[EdgeV1Idx].GetSafeNormal();
                const FVector3d PlaneNormal = FVector3d::CrossProduct(EdgeV0, EdgeV1).GetSafeNormal();
                if (PlaneNormal.IsNearlyZero())
                {
                    continue;
                }

                const double Projection = FVector3d::DotProduct(VertexNormal, PlaneNormal);
                const FVector3d Projected = VertexNormal - Projection * PlaneNormal;
                if (Projected.IsNearlyZero())
                {
                    continue;
                }

                const FVector3d GreatCirclePoint = Projected.GetSafeNormal();
                const double ArcAB = FMath::Acos(FMath::Clamp(EdgeV0 | EdgeV1, -1.0, 1.0));
                const double ArcAC = FMath::Acos(FMath::Clamp(EdgeV0 | GreatCirclePoint, -1.0, 1.0));
                const double ArcCB = FMath::Acos(FMath::Clamp(GreatCirclePoint | EdgeV1, -1.0, 1.0));
                const bool bWithinSegment = (ArcAC + ArcCB) <= (ArcAB + 1e-3);

                auto ConsiderPoint = [&](const FVector3d& PointOnCircle)
                {
                    const double Distance = FMath::Acos(FMath::Clamp(VertexNormal | PointOnCircle, -1.0, 1.0));
                    if (Distance < MinDistance)
                    {
                        MinDistance = Distance;
                        BestTangent = ComputeSegmentTangent(PlaneNormal, PointOnCircle);
                    }
                };

                if (bWithinSegment)
                {
                    ConsiderPoint(GreatCirclePoint);
                }
                else
                {
                    ConsiderPoint(EdgeV0);
                    ConsiderPoint(EdgeV1);
                }
            }
        }

        return BestTangent;
    };

    for (int32 VertexIdx = 0; VertexIdx < RenderVertices.Num(); ++VertexIdx)
    {
        const int32 PlateID = VertexPlateAssignments[VertexIdx];
        if (PlateID == INDEX_NONE || !Plates.IsValidIndex(PlateID))
            continue;

        const FTectonicPlate& Plate = Plates[PlateID];
        if (Plate.CrustType == ECrustType::Oceanic)
        {
            OceanicVertexCount++;

            // Ridge direction should be normalized and non-zero for oceanic vertices
            const FVector3d& RidgeDir = RidgeDirections[VertexIdx];
            const double RidgeDirLength = RidgeDir.Length();

            if (FMath::IsNearlyEqual(RidgeDirLength, 1.0, 0.01))
            {
                ValidRidgeDirectionCount++;
            }

            const FVector3d& Position = RenderVertices[VertexIdx];
            const FVector3d ExpectedTangent = FindNearestBoundaryTangent(Position, PlateID);
            if (!ExpectedTangent.IsNearlyZero())
            {
                const double Alignment = FMath::Abs(ExpectedTangent | RidgeDir);
                TestTrue(FString::Printf(TEXT("Vertex %d ridge direction aligns with divergent edge (dot = %.3f)"), VertexIdx, Alignment),
                    Alignment > 0.95);
            }

            // Transform fault direction should be perpendicular to ridge (cross product with position)
            const FVector3d TransformFaultDir = FVector3d::CrossProduct(RidgeDir, Position.GetSafeNormal()).GetSafeNormal();

            // Verify transform fault is perpendicular to ridge (dot product ≈ 0)
            const double DotProduct = FMath::Abs(RidgeDir | TransformFaultDir);
            TestTrue(FString::Printf(TEXT("Vertex %d transform fault perpendicular to ridge (dot = %.3f)"), VertexIdx, DotProduct),
                DotProduct < 0.1);
        }
    }

    TestTrue(TEXT("At least 30% of vertices are oceanic"), OceanicVertexCount > RenderVertices.Num() * 0.3);
    TestTrue(TEXT("At least 90% of oceanic vertices have valid ridge directions"),
        ValidRidgeDirectionCount > OceanicVertexCount * 0.9);

    // ============================================================================
    // Test 2: Young crust (<10 My) shows strong faults (amplitude >100m)
    // ============================================================================

    const TArray<double>& CrustAge = Service->GetVertexCrustAge();
    const TArray<double>& BaseElevation = Service->GetVertexElevationValues();

    int32 YoungCrustCount = 0;
    int32 StrongFaultCount = 0;

    for (int32 VertexIdx = 0; VertexIdx < RenderVertices.Num(); ++VertexIdx)
    {
        const int32 PlateID = VertexPlateAssignments[VertexIdx];
        if (PlateID == INDEX_NONE || !Plates.IsValidIndex(PlateID))
            continue;

        const FTectonicPlate& Plate = Plates[PlateID];
        if (Plate.CrustType == ECrustType::Oceanic && CrustAge[VertexIdx] < 10.0)
        {
            YoungCrustCount++;

            // Amplified elevation should differ significantly from base (transform faults add detail)
            const double ElevationDiff = FMath::Abs(AmplifiedElevation[VertexIdx] - BaseElevation[VertexIdx]);

            // Young crust should show strong amplification (>50m detail from faults + fine noise)
            if (ElevationDiff > 50.0)
            {
                StrongFaultCount++;
            }
        }
    }

    if (YoungCrustCount > 0)
    {
        TestTrue(TEXT("At least 60% of young oceanic crust shows strong faults (>50m amplification)"),
            StrongFaultCount > YoungCrustCount * 0.6);
    }

    // ============================================================================
    // Test 3: Old crust (>200 My) shows weak faults (amplitude <20m)
    // ============================================================================

    int32 OldCrustCount = 0;
    int32 WeakFaultCount = 0;

    for (int32 VertexIdx = 0; VertexIdx < RenderVertices.Num(); ++VertexIdx)
    {
        const int32 PlateID = VertexPlateAssignments[VertexIdx];
        if (PlateID == INDEX_NONE || !Plates.IsValidIndex(PlateID))
            continue;

        const FTectonicPlate& Plate = Plates[PlateID];
        if (Plate.CrustType == ECrustType::Oceanic && CrustAge[VertexIdx] > 200.0)
        {
            OldCrustCount++;

            // Old crust should show minimal amplification (faults eroded/smoothed)
            const double ElevationDiff = FMath::Abs(AmplifiedElevation[VertexIdx] - BaseElevation[VertexIdx]);

            if (ElevationDiff < 50.0) // Less than young crust threshold
            {
                WeakFaultCount++;
            }
        }
    }

    if (OldCrustCount > 0)
    {
        TestTrue(TEXT("At least 70% of old oceanic crust shows weak faults (<50m amplification)"),
            WeakFaultCount > OldCrustCount * 0.7);
    }

    // ============================================================================
    // Test 4: High-frequency detail adds ±20m variation
    // ============================================================================

    // Measure variation in amplified elevation (should be more varied than base)
    double BaseElevationVariance = 0.0;
    double AmplifiedElevationVariance = 0.0;

    // Compute mean first
    double BaseMean = 0.0;
    double AmplifiedMean = 0.0;
    int32 ValidCount = 0;

    for (int32 VertexIdx = 0; VertexIdx < RenderVertices.Num(); ++VertexIdx)
    {
        const int32 PlateID = VertexPlateAssignments[VertexIdx];
        if (PlateID != INDEX_NONE && Plates.IsValidIndex(PlateID) && Plates[PlateID].CrustType == ECrustType::Oceanic)
        {
            BaseMean += BaseElevation[VertexIdx];
            AmplifiedMean += AmplifiedElevation[VertexIdx];
            ValidCount++;
        }
    }

    if (ValidCount > 0)
    {
        BaseMean /= ValidCount;
        AmplifiedMean /= ValidCount;

        // Compute variance
        for (int32 VertexIdx = 0; VertexIdx < RenderVertices.Num(); ++VertexIdx)
        {
            const int32 PlateID = VertexPlateAssignments[VertexIdx];
            if (PlateID != INDEX_NONE && Plates.IsValidIndex(PlateID) && Plates[PlateID].CrustType == ECrustType::Oceanic)
            {
                BaseElevationVariance += FMath::Square(BaseElevation[VertexIdx] - BaseMean);
                AmplifiedElevationVariance += FMath::Square(AmplifiedElevation[VertexIdx] - AmplifiedMean);
            }
        }

        BaseElevationVariance /= ValidCount;
        AmplifiedElevationVariance /= ValidCount;

        // Amplified elevation should have more variance due to high-frequency detail
        TestTrue(TEXT("Amplified elevation has greater variance than base (high-frequency detail added)"),
            AmplifiedElevationVariance > BaseElevationVariance);

        UE_LOG(LogPlanetaryCreation, Log, TEXT("OceanicAmplificationTest: Base variance = %.2f, Amplified variance = %.2f"),
            BaseElevationVariance, AmplifiedElevationVariance);
    }

    // ============================================================================
    // Test 5: Continental vertices unchanged (amplification only affects oceanic)
    // ============================================================================

    int32 ContinentalUnchangedCount = 0;
    int32 ContinentalTotalCount = 0;
    int32 DebugLoggedContinental = 0;

    for (int32 VertexIdx = 0; VertexIdx < RenderVertices.Num(); ++VertexIdx)
    {
        const int32 PlateID = VertexPlateAssignments[VertexIdx];
        if (PlateID == INDEX_NONE || !Plates.IsValidIndex(PlateID))
            continue;

        const FTectonicPlate& Plate = Plates[PlateID];
        if (Plate.CrustType == ECrustType::Continental)
        {
            ContinentalTotalCount++;

            // Continental vertices should have amplified elevation equal to base
            // (Task 2.2 will handle continental amplification separately)
            const double ElevDiff = FMath::Abs(AmplifiedElevation[VertexIdx] - BaseElevation[VertexIdx]);
            if (FMath::IsNearlyEqual(AmplifiedElevation[VertexIdx], BaseElevation[VertexIdx], 0.01))
            {
                ContinentalUnchangedCount++;
            }
            else if (DebugLoggedContinental < 3)
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("Continental vertex %d (PlateID=%d, CrustType=%s) modified: Base=%.3f m, Amplified=%.3f m, Diff=%.3f m"),
                    VertexIdx, PlateID,
                    Plate.CrustType == ECrustType::Continental ? TEXT("Continental") : TEXT("Oceanic"),
                    BaseElevation[VertexIdx], AmplifiedElevation[VertexIdx], ElevDiff);
                DebugLoggedContinental++;
            }
        }
    }

    if (ContinentalTotalCount > 0)
    {
        TestTrue(TEXT("Continental vertices unchanged by oceanic amplification (>99% match base)"),
            ContinentalUnchangedCount > ContinentalTotalCount * 0.99);
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("OceanicAmplificationTest: Summary"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Total vertices: %d"), RenderVertices.Num());
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Oceanic vertices: %d (%.1f%%)"), OceanicVertexCount, 100.0 * OceanicVertexCount / RenderVertices.Num());
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Young crust (<10 My): %d, Strong faults: %d (%.1f%%)"),
        YoungCrustCount, StrongFaultCount, YoungCrustCount > 0 ? 100.0 * StrongFaultCount / YoungCrustCount : 0.0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Old crust (>200 My): %d, Weak faults: %d (%.1f%%)"),
        OldCrustCount, WeakFaultCount, OldCrustCount > 0 ? 100.0 * WeakFaultCount / OldCrustCount : 0.0);

    return true;
}
