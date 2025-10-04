#include "SphericalKDTree.h"

void FSphericalKDTree::Build(const TArray<FVector3d>& Points, const TArray<int32>& PointIDs)
{
	Clear();

	if (Points.Num() != PointIDs.Num() || Points.Num() == 0)
	{
		return;
	}

	// Create pairs of points and IDs for building
	TArray<TPair<FVector3d, int32>> PointPairs;
	PointPairs.Reserve(Points.Num());
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		PointPairs.Add(TPair<FVector3d, int32>(Points[i], PointIDs[i]));
	}

	RootNode = BuildRecursive(PointPairs, 0);
}

int32 FSphericalKDTree::FindNearest(const FVector3d& Query, double& OutDistanceSq) const
{
	if (!RootNode)
	{
		OutDistanceSq = TNumericLimits<double>::Max();
		return INDEX_NONE;
	}

	int32 BestID = INDEX_NONE;
	double BestDistSq = TNumericLimits<double>::Max();

	FindNearestRecursive(RootNode.Get(), Query, BestID, BestDistSq);

	OutDistanceSq = BestDistSq;
	return BestID;
}

void FSphericalKDTree::Clear()
{
	RootNode.Reset();
}

TUniquePtr<FSphericalKDTree::FKDNode> FSphericalKDTree::BuildRecursive(TArray<TPair<FVector3d, int32>>& Points, int32 Depth)
{
	if (Points.Num() == 0)
	{
		return nullptr;
	}

	// Choose split axis (cycle through X, Y, Z)
	const int32 Axis = Depth % 3;

	// Sort points along the split axis
	Points.Sort([Axis](const TPair<FVector3d, int32>& A, const TPair<FVector3d, int32>& B)
	{
		return A.Key[Axis] < B.Key[Axis];
	});

	// Choose median as split point
	const int32 MedianIndex = Points.Num() / 2;

	TUniquePtr<FKDNode> Node = MakeUnique<FKDNode>();
	Node->Point = Points[MedianIndex].Key;
	Node->PointID = Points[MedianIndex].Value;
	Node->SplitAxis = Axis;

	// Recursively build left and right subtrees
	TArray<TPair<FVector3d, int32>> LeftPoints(Points.GetData(), MedianIndex);
	TArray<TPair<FVector3d, int32>> RightPoints(Points.GetData() + MedianIndex + 1, Points.Num() - MedianIndex - 1);

	Node->Left = BuildRecursive(LeftPoints, Depth + 1);
	Node->Right = BuildRecursive(RightPoints, Depth + 1);

	return Node;
}

void FSphericalKDTree::FindNearestRecursive(const FKDNode* Node, const FVector3d& Query, int32& BestID, double& BestDistSq) const
{
	if (!Node)
	{
		return;
	}

	// Calculate distance to current node
	const double DistSq = FVector3d::DistSquared(Query, Node->Point);
	if (DistSq < BestDistSq)
	{
		BestDistSq = DistSq;
		BestID = Node->PointID;
	}

	// Determine which side to search first based on split axis
	const int32 Axis = Node->SplitAxis;
	const double AxisDiff = Query[Axis] - Node->Point[Axis];

	const FKDNode* NearSide = (AxisDiff < 0.0) ? Node->Left.Get() : Node->Right.Get();
	const FKDNode* FarSide = (AxisDiff < 0.0) ? Node->Right.Get() : Node->Left.Get();

	// Search near side first
	FindNearestRecursive(NearSide, Query, BestID, BestDistSq);

	// For small datasets on a sphere, always search both sides for correctness
	// Standard KD-tree pruning assumes Euclidean space and axis-aligned bounds,
	// which doesn't work correctly for points constrained to a spherical surface.
	// For 20 plates, the overhead of searching both branches is negligible (~40 checks total)
	// vs the O(N) brute force (642*20 = 12,840 checks).
	// TODO: For larger datasets, implement proper spherical cap intersection test.
	FindNearestRecursive(FarSide, Query, BestID, BestDistSq);
}
