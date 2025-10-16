#include "Utilities/PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "Simulation/TectonicSimulationService.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Math/IntPoint.h"

#include "Containers/Set.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuantitativeMetricsExportTest,
    "PlanetaryCreation.QuantitativeMetrics.Export",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuantitativeMetricsExportTest::RunTest(const FString& Parameters)
{
    if (!GEditor)
    {
        AddError(TEXT("Editor context required for quantitative metrics export test"));
        return false;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to acquire UTectonicSimulationService"));
        return false;
    }

    FTectonicSimulationParameters Params = Service->GetParameters();
    Params.Seed = 42;
    Params.SubdivisionLevel = 0;
    Params.RenderSubdivisionLevel = 3;
    Params.LloydIterations = 2;
    Params.bEnableDynamicRetessellation = false;
    Service->SetParameters(Params);

    Service->AdvanceSteps(8);

    const TArray<int32>& VertexPlateAssignments = Service->GetVertexPlateAssignments();
    const TArray<int32>& RenderTriangles = Service->GetRenderTriangles();
    TArray<FTectonicPlate>& MutablePlates = Service->GetPlatesForModification();

    int32 ContinentalPlateID = INDEX_NONE;
    int32 LargestPlateVertexCount = 0;

    for (const FTectonicPlate& Plate : MutablePlates)
    {
        if (Plate.CrustType == ECrustType::Continental)
        {
            int32 PlateVertexCount = 0;
            for (int32 Assignment : VertexPlateAssignments)
            {
                if (Assignment == Plate.PlateID)
                {
                    ++PlateVertexCount;
                }
            }

            if (PlateVertexCount > LargestPlateVertexCount)
            {
                LargestPlateVertexCount = PlateVertexCount;
                ContinentalPlateID = Plate.PlateID;
            }
        }
    }

    if (ContinentalPlateID == INDEX_NONE)
    {
        if (MutablePlates.Num() == 0)
        {
            AddError(TEXT("No plates generated for quantitative metrics test"));
            return false;
        }

        MutablePlates[0].CrustType = ECrustType::Continental;
        ContinentalPlateID = MutablePlates[0].PlateID;

        LargestPlateVertexCount = 0;
        for (int32 Assignment : VertexPlateAssignments)
        {
            if (Assignment == ContinentalPlateID)
            {
                ++LargestPlateVertexCount;
            }
        }
    }

    TestTrue(TEXT("Continental plate has vertices"), LargestPlateVertexCount > 0);
    if (LargestPlateVertexCount <= 0)
    {
        return false;
    }

    TArray<int32> PlateVertices;
    for (int32 Index = 0; Index < VertexPlateAssignments.Num(); ++Index)
    {
        if (VertexPlateAssignments[Index] == ContinentalPlateID)
        {
            PlateVertices.Add(Index);
        }
    }

    if (PlateVertices.Num() == 0)
    {
        AddError(TEXT("Failed to gather continental plate vertices for quantitative metrics export"));
        return false;
    }

    const int32 MinTerraneSize = 12;
    const int32 MaxTerraneSize = FMath::Clamp(PlateVertices.Num() - 1, MinTerraneSize, 60);
    const int32 InitialTargetSize = FMath::Clamp(PlateVertices.Num() / 4, MinTerraneSize, MaxTerraneSize);

    auto BuildTerraneCandidate = [&](int32 SeedVertex, int32 DesiredCount, TArray<int32>& OutVertices, double& OutAreaKm2) -> bool
    {
        OutVertices.Reset();
        TSet<int32> LocalSet;
        LocalSet.Reserve(DesiredCount + 4);

        OutVertices.Add(SeedVertex);
        LocalSet.Add(SeedVertex);

        for (int32 GrowthIter = 0; GrowthIter < 128 && OutVertices.Num() < DesiredCount && LocalSet.Num() < PlateVertices.Num(); ++GrowthIter)
        {
            bool bAddedVertex = false;
            for (int32 TriIndex = 0; TriIndex < RenderTriangles.Num() && OutVertices.Num() < DesiredCount; TriIndex += 3)
            {
                const int32 V0 = RenderTriangles[TriIndex];
                const int32 V1 = RenderTriangles[TriIndex + 1];
                const int32 V2 = RenderTriangles[TriIndex + 2];

                if (!(LocalSet.Contains(V0) || LocalSet.Contains(V1) || LocalSet.Contains(V2)))
                {
                    continue;
                }

                auto TryAddVertex = [&](int32 Candidate)
                {
                    if (!LocalSet.Contains(Candidate) &&
                        VertexPlateAssignments.IsValidIndex(Candidate) &&
                        VertexPlateAssignments[Candidate] == ContinentalPlateID)
                    {
                        OutVertices.Add(Candidate);
                        LocalSet.Add(Candidate);
                        bAddedVertex = true;
                    }
                };

                TryAddVertex(V0);
                TryAddVertex(V1);
                TryAddVertex(V2);
            }

            if (!bAddedVertex)
            {
                break;
            }
        }

        OutAreaKm2 = Service->ComputeTerraneArea(OutVertices);
        return OutVertices.Num() >= MinTerraneSize &&
               OutVertices.Num() < PlateVertices.Num() &&
               OutAreaKm2 >= 100.0;
    };

    auto HasClosedBoundary = [&](const TArray<int32>& CandidateVertices) -> bool
    {
        if (CandidateVertices.Num() < MinTerraneSize)
        {
            return false;
        }

        TSet<int32> CandidateSet(CandidateVertices);
        TMap<FIntPoint, int32> EdgeUse;
        EdgeUse.Reserve(CandidateVertices.Num() * 3);

        auto MakeEdge = [](int32 A, int32 B) -> FIntPoint
        {
            return (A < B) ? FIntPoint(A, B) : FIntPoint(B, A);
        };

        for (int32 TriIndex = 0; TriIndex < RenderTriangles.Num(); TriIndex += 3)
        {
            const int32 TriA = RenderTriangles[TriIndex];
            const int32 TriB = RenderTriangles[TriIndex + 1];
            const int32 TriC = RenderTriangles[TriIndex + 2];

            if (!CandidateSet.Contains(TriA) || !CandidateSet.Contains(TriB) || !CandidateSet.Contains(TriC))
            {
                continue;
            }

            const int32 EdgeVerts[3][2] = { {TriA, TriB}, {TriB, TriC}, {TriC, TriA} };
            for (int32 EdgeIdx = 0; EdgeIdx < 3; ++EdgeIdx)
            {
                const FIntPoint Edge = MakeEdge(EdgeVerts[EdgeIdx][0], EdgeVerts[EdgeIdx][1]);
                EdgeUse.FindOrAdd(Edge)++;
            }
        }

        if (EdgeUse.Num() == 0)
        {
            return false;
        }

        TMap<int32, TArray<int32>> BoundaryAdjacency;
        for (const TPair<FIntPoint, int32>& Entry : EdgeUse)
        {
            if (Entry.Value == 1)
            {
                BoundaryAdjacency.FindOrAdd(Entry.Key.X).Add(Entry.Key.Y);
                BoundaryAdjacency.FindOrAdd(Entry.Key.Y).Add(Entry.Key.X);
            }
        }

        if (BoundaryAdjacency.Num() == 0)
        {
            return false;
        }

        for (const TPair<int32, TArray<int32>>& Entry : BoundaryAdjacency)
        {
            if (Entry.Value.Num() != 2)
            {
                return false;
            }
        }

        TMap<int32, TArray<int32>>::TConstIterator It = BoundaryAdjacency.CreateConstIterator();
        if (!It)
        {
            return false;
        }

        const int32 StartVertex = It.Key();
        int32 PreviousVertex = INDEX_NONE;
        int32 CurrentVertex = StartVertex;
        TSet<int32> VisitedVertices;

        for (int32 Step = 0; Step <= BoundaryAdjacency.Num(); ++Step)
        {
            VisitedVertices.Add(CurrentVertex);
            const TArray<int32>& Neighbors = BoundaryAdjacency[CurrentVertex];
            int32 NextVertex = (Neighbors[0] != PreviousVertex) ? Neighbors[0] : Neighbors[1];
            PreviousVertex = CurrentVertex;
            CurrentVertex = NextVertex;
            if (CurrentVertex == StartVertex)
            {
                break;
            }
        }

        return CurrentVertex == StartVertex && VisitedVertices.Num() == BoundaryAdjacency.Num();
    };

    TArray<int32> TerraneVertices;
    double TerraneAreaKm2 = 0.0;
    bool bFoundCandidate = false;

    for (int32 SeedVertex : PlateVertices)
    {
        int32 DesiredCount = InitialTargetSize;

        while (DesiredCount <= MaxTerraneSize)
        {
            if (BuildTerraneCandidate(SeedVertex, DesiredCount, TerraneVertices, TerraneAreaKm2) &&
                HasClosedBoundary(TerraneVertices))
            {
                bFoundCandidate = true;
                break;
            }

            if (DesiredCount >= MaxTerraneSize)
            {
                break;
            }

            DesiredCount = FMath::Min(DesiredCount + 6, MaxTerraneSize);
        }

        if (bFoundCandidate)
        {
            break;
        }
    }

    TestTrue(TEXT("Terrane vertex selection produced a valid candidate"), bFoundCandidate);
    if (!bFoundCandidate)
    {
        return false;
    }

    TestTrue(TEXT("Selected terrane area is positive"), TerraneAreaKm2 > 0.0);

    int32 TerraneID = INDEX_NONE;
    const bool bExtractionSuccess = Service->ExtractTerrane(ContinentalPlateID, TerraneVertices, TerraneID);
    TestTrue(TEXT("Terrane extraction succeeded"), bExtractionSuccess);
    if (!bExtractionSuccess)
    {
        return false;
    }

    Service->ExportQuantitativeMetrics();
    const FQuantitativeMetricsSnapshot& Metrics = Service->GetLastQuantitativeMetrics();

    TestTrue(TEXT("Quantitative metrics export produced a CSV"), Metrics.bValid);
    TestTrue(TEXT("Hypsometric distribution sums to ~100%"),
        FMath::IsNearlyEqual(Metrics.HypsometricSumPercent, 100.0, 0.5));
    TestTrue(TEXT("Velocity distribution sums to <=100%"),
        Metrics.VelocitySumPercent <= 100.5);
    TestFalse(TEXT("Ridge vs trench ratio is finite"), FMath::IsNaN(Metrics.RidgeTrench.RidgeToTrenchRatio));
    TestTrue(TEXT("Ridge vs trench ratio is non-negative"), Metrics.RidgeTrench.RidgeToTrenchRatio >= 0.0);
    TestTrue(TEXT("Ridge length stays within expected band"), Metrics.RidgeTrench.DivergentLengthKm >= 2000.0 && Metrics.RidgeTrench.DivergentLengthKm <= 3300.0);
    TestTrue(TEXT("Trench length stays within expected band"), Metrics.RidgeTrench.ConvergentLengthKm >= 1000.0 && Metrics.RidgeTrench.ConvergentLengthKm <= 2200.0);
    TestTrue(TEXT("Ridge to trench ratio within ±10% of baseline"),
        Metrics.RidgeTrench.RidgeToTrenchRatio >= 1.60 && Metrics.RidgeTrench.RidgeToTrenchRatio <= 1.90);

    const TArray<FTerraneAreaDriftSample>& TerraneSamples = Metrics.TerraneSamples;
    if (bExtractionSuccess)
    {
        TestTrue(TEXT("Terrane metrics captured sample"), TerraneSamples.Num() >= 1);
    }
    if (bExtractionSuccess && TerraneSamples.Num() > 0)
    {
        TestTrue(TEXT("Terrane area drift within 5% budget"), Metrics.TerraneMaxDriftPercent <= 5.0);
    }

    if (Metrics.bValid)
    {
        IFileManager& FileManager = IFileManager::Get();
        TestTrue(TEXT("Timestamped metrics CSV exists"), FileManager.FileExists(*Metrics.TimestampedFilePath));
        TestTrue(TEXT("Latest metrics CSV exists"), FileManager.FileExists(*Metrics.LatestFilePath));
    }

    if (bExtractionSuccess && TerraneID != INDEX_NONE)
    {
        const bool bReattachSuccess = Service->ReattachTerrane(TerraneID, ContinentalPlateID);
        TestTrue(TEXT("Terrane reattachment succeeded"), bReattachSuccess);
    }

    Service->ResetSimulation();
    return true;
}
