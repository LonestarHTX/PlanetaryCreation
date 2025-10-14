#include "Tests/RidgeTestHelpers.h"

#include "PlanetaryCreationLogging.h"
#include "TectonicSimulationService.h"

#include "Containers/Queue.h"

namespace PlanetaryCreation::Tests
{
namespace
{
TPair<int32, int32> MakeBoundaryKey(int32 A, int32 B)
{
    return (A < B) ? TPair<int32, int32>(A, B) : TPair<int32, int32>(B, A);
}
} // namespace

bool BuildRidgeTripleJunctionFixture(const UTectonicSimulationService& Service, FRidgeTripleJunctionFixture& OutFixture)
{
    OutFixture = FRidgeTripleJunctionFixture();

    const TArray<int32>& PlateAssignments = Service.GetVertexPlateAssignments();
    const TArray<double>& CrustAge = Service.GetVertexCrustAge();
    const TArray<FTectonicPlate>& Plates = Service.GetPlates();
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries = Service.GetBoundaries();

    if (PlateAssignments.Num() == 0)
    {
        return false;
    }

    TMap<int32, TSet<int32>> DivergentOpponentsByVertex;

    for (const auto& Pair : Boundaries)
    {
        const int32 PlateA = Pair.Key.Key;
        const int32 PlateB = Pair.Key.Value;
        const FPlateBoundary& Boundary = Pair.Value;

        if (Boundary.BoundaryType != EBoundaryType::Divergent)
        {
            continue;
        }

        for (int32 VertexIdx : Boundary.SharedEdgeVertices)
        {
            if (!PlateAssignments.IsValidIndex(VertexIdx))
            {
                continue;
            }

            const int32 AssignedPlate = PlateAssignments[VertexIdx];
            if (AssignedPlate == PlateA)
            {
                DivergentOpponentsByVertex.FindOrAdd(VertexIdx).Add(PlateB);
            }
            else if (AssignedPlate == PlateB)
            {
                DivergentOpponentsByVertex.FindOrAdd(VertexIdx).Add(PlateA);
            }
        }
    }

    TArray<int32> CandidateVertices;
    DivergentOpponentsByVertex.GetKeys(CandidateVertices);
    CandidateVertices.Sort();

    int32 BestVertex = INDEX_NONE;
    int32 MaxOpponents = 0;

    for (int32 VertexIdx : CandidateVertices)
    {
        const int32 PlateID = PlateAssignments[VertexIdx];
        if (!Plates.IsValidIndex(PlateID))
        {
            continue;
        }

        if (Plates[PlateID].CrustType != ECrustType::Oceanic)
        {
            continue;
        }

        if (!CrustAge.IsValidIndex(VertexIdx) || CrustAge[VertexIdx] > 8.0)
        {
            continue;
        }

        const TSet<int32>* OpponentSet = DivergentOpponentsByVertex.Find(VertexIdx);
        if (!OpponentSet)
        {
            continue;
        }

        if (OpponentSet->Num() > MaxOpponents)
        {
            MaxOpponents = OpponentSet->Num();
            BestVertex = VertexIdx;
        }

        if (OpponentSet->Num() >= 3)
        {
            OutFixture.VertexIndex = VertexIdx;
            OutFixture.CrustAgeMy = CrustAge[VertexIdx];
            OutFixture.OpposingPlates = OpponentSet->Array();
            OutFixture.OpposingPlates.Sort();
            return true;
        }
    }

    if (BestVertex != INDEX_NONE && MaxOpponents > 0)
    {
        const TSet<int32>& OpponentSet = DivergentOpponentsByVertex.FindChecked(BestVertex);
        OutFixture.VertexIndex = BestVertex;
        OutFixture.CrustAgeMy = CrustAge.IsValidIndex(BestVertex) ? CrustAge[BestVertex] : 0.0;
        OutFixture.OpposingPlates = OpponentSet.Array();
        OutFixture.OpposingPlates.Sort();
#if UE_BUILD_DEVELOPMENT
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("[RidgeFixture] Triple junction fallback: max divergent opponents %d at vertex %d (age %.2f My)"),
            MaxOpponents,
            BestVertex,
            OutFixture.CrustAgeMy);
#endif
        return true;
    }

    return false;
}

bool BuildRidgeCrustAgeDiscontinuityFixture(
    const UTectonicSimulationService& Service,
    FRidgeCrustAgeDiscontinuityFixture& OutFixture,
    double MinAgeDeltaMy)
{
    OutFixture = FRidgeCrustAgeDiscontinuityFixture();

    const TArray<int32>& PlateAssignments = Service.GetVertexPlateAssignments();
    const TArray<int32>& Offsets = Service.GetRenderVertexAdjacencyOffsets();
    const TArray<int32>& Adjacency = Service.GetRenderVertexAdjacency();
    const TArray<double>& CrustAge = Service.GetVertexCrustAge();
    const TArray<FTectonicPlate>& Plates = Service.GetPlates();

    if (PlateAssignments.Num() == 0 || Offsets.Num() != PlateAssignments.Num() + 1)
    {
        return false;
    }

    double BestDelta = -TNumericLimits<double>::Max();
    int32 BestYoungVertex = INDEX_NONE;
    int32 BestOldVertex = INDEX_NONE;
    double BestYoungAge = 0.0;
    double BestOldAge = 0.0;
    int32 BestPlateID = INDEX_NONE;

    for (int32 VertexIdx = 0; VertexIdx < PlateAssignments.Num(); ++VertexIdx)
    {
        const int32 PlateID = PlateAssignments[VertexIdx];
        if (!Plates.IsValidIndex(PlateID) || Plates[PlateID].CrustType != ECrustType::Oceanic)
        {
            continue;
        }

        if (!CrustAge.IsValidIndex(VertexIdx))
        {
            continue;
        }

        const double VertexAge = CrustAge[VertexIdx];
        const int32 Start = Offsets[VertexIdx];
        const int32 End = Offsets[VertexIdx + 1];

        for (int32 AdjIdx = Start; AdjIdx < End; ++AdjIdx)
        {
            if (!Adjacency.IsValidIndex(AdjIdx))
            {
                continue;
            }

            const int32 Neighbor = Adjacency[AdjIdx];
            if (!PlateAssignments.IsValidIndex(Neighbor) || PlateAssignments[Neighbor] != PlateID)
            {
                continue;
            }

            if (!CrustAge.IsValidIndex(Neighbor))
            {
                continue;
            }

            const double NeighborAge = CrustAge[Neighbor];
            const double Delta = FMath::Abs(VertexAge - NeighborAge);
            if (Delta <= BestDelta)
            {
                continue;
            }

            // Require a classic ridge profile: young crust near ridge, older crust interior.
            const double Younger = FMath::Min(VertexAge, NeighborAge);
            const double Older = FMath::Max(VertexAge, NeighborAge);
            if (Younger > 6.0 || Older < 20.0)
            {
                continue;
            }

            BestDelta = Delta;
            BestPlateID = PlateID;
            BestYoungVertex = (VertexAge <= NeighborAge) ? VertexIdx : Neighbor;
            BestOldVertex = (VertexAge > NeighborAge) ? VertexIdx : Neighbor;
            BestYoungAge = Younger;
            BestOldAge = Older;
        }
    }

    if (BestYoungVertex == INDEX_NONE)
    {
        return false;
    }

    OutFixture.PlateID = BestPlateID;
    OutFixture.YoungVertexIndex = BestYoungVertex;
    OutFixture.OldVertexIndex = BestOldVertex;
    OutFixture.YoungAgeMy = BestYoungAge;
    OutFixture.OldAgeMy = BestOldAge;
    OutFixture.AgeDeltaMy = BestDelta;
    return BestDelta > KINDA_SMALL_NUMBER;
}

bool BuildContiguousPlateRegion(
    const UTectonicSimulationService& Service,
    int32 PlateID,
    int32 SeedVertex,
    int32 TargetCount,
    TArray<int32>& OutVertices)
{
    OutVertices.Reset();

    const TArray<int32>& PlateAssignments = Service.GetVertexPlateAssignments();
    const TArray<int32>& Offsets = Service.GetRenderVertexAdjacencyOffsets();
    const TArray<int32>& Adjacency = Service.GetRenderVertexAdjacency();

    if (!PlateAssignments.IsValidIndex(SeedVertex) ||
        PlateAssignments[SeedVertex] != PlateID ||
        Offsets.Num() != PlateAssignments.Num() + 1)
    {
        return false;
    }

    TSet<int32> Visited;
    TQueue<int32> Frontier;
    Visited.Add(SeedVertex);
    Frontier.Enqueue(SeedVertex);

    while (!Frontier.IsEmpty() && Visited.Num() < TargetCount)
    {
        int32 Current = INDEX_NONE;
        Frontier.Dequeue(Current);

        if (!Offsets.IsValidIndex(Current) || !Offsets.IsValidIndex(Current + 1))
        {
            continue;
        }

        const int32 Start = Offsets[Current];
        const int32 End = Offsets[Current + 1];
        for (int32 AdjIdx = Start; AdjIdx < End; ++AdjIdx)
        {
            if (!Adjacency.IsValidIndex(AdjIdx))
            {
                continue;
            }

            const int32 Neighbor = Adjacency[AdjIdx];
            if (!PlateAssignments.IsValidIndex(Neighbor) || PlateAssignments[Neighbor] != PlateID)
            {
                continue;
            }

            if (Visited.Contains(Neighbor))
            {
                continue;
            }

            Visited.Add(Neighbor);
            Frontier.Enqueue(Neighbor);
            if (Visited.Num() >= TargetCount)
            {
                break;
            }
        }
    }

    if (Visited.Num() < TargetCount)
    {
        return false;
    }

    OutVertices = Visited.Array();
    OutVertices.Sort();
    return true;
}
} // namespace PlanetaryCreation::Tests
