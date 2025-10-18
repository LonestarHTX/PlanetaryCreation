#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Simulation/SphericalDelaunay.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSphericalDelaunayAdjacencyTest, "PlanetaryCreation.Paper.SphericalDelaunayAdjacency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSphericalDelaunayAdjacencyTest::RunTest(const FString& Parameters)
{
    TArray<FVector3d> Vertices;
    Vertices.Add(FVector3d(1.0, 1.0, 1.0).GetSafeNormal());
    Vertices.Add(FVector3d(1.0, -1.0, -1.0).GetSafeNormal());
    Vertices.Add(FVector3d(-1.0, 1.0, -1.0).GetSafeNormal());
    Vertices.Add(FVector3d(-1.0, -1.0, 1.0).GetSafeNormal());

    TArray<FSphericalDelaunay::FTriangle> Triangles;
    Triangles.Add({0, 1, 2});
    Triangles.Add({0, 1, 3});
    Triangles.Add({0, 2, 3});
    Triangles.Add({1, 2, 3});

    TArray<TArray<int32>> Neighbors;
    FSphericalDelaunay::ComputeVoronoiNeighbors(Vertices, Triangles, Neighbors);

    TestEqual(TEXT("neighbor array size"), Neighbors.Num(), Vertices.Num());

    const TArray<int32> ExpectedNeighbors[4] = {
        {1, 2, 3},
        {0, 2, 3},
        {0, 1, 3},
        {0, 1, 2}
    };

    for (int32 VertexIndex = 0; VertexIndex < Vertices.Num(); ++VertexIndex)
    {
        const FString Label = FString::Printf(TEXT("vertex %d neighbor count"), VertexIndex);
        TestEqual(*Label, Neighbors[VertexIndex].Num(), 3);
        TestTrue(*(Label + TEXT(" order")), Neighbors[VertexIndex] == ExpectedNeighbors[VertexIndex]);
    }

    TArray<int32> Offsets;
    TArray<int32> Adjacency;
    FSphericalDelaunay::BuildCSR(Neighbors, Offsets, Adjacency);

    TestEqual(TEXT("csr offsets size"), Offsets.Num(), Vertices.Num() + 1);
    TestEqual(TEXT("csr adjacency size"), Offsets.Last(), Adjacency.Num());

    for (int32 Index = 0; Index < Adjacency.Num(); ++Index)
    {
        TestTrue(TEXT("adjacency indices in range"), Adjacency[Index] >= 0 && Adjacency[Index] < Vertices.Num());
    }

    for (int32 VertexIndex = 0; VertexIndex < Vertices.Num(); ++VertexIndex)
    {
        for (int32 Offset = Offsets[VertexIndex]; Offset < Offsets[VertexIndex + 1]; ++Offset)
        {
            const int32 Neighbor = Adjacency[Offset];
            const FString SymmetryLabel = FString::Printf(TEXT("symmetry %d-%d"), VertexIndex, Neighbor);
            TestTrue(*SymmetryLabel, Neighbors[Neighbor].Contains(VertexIndex));
        }
    }

    return true;
}
