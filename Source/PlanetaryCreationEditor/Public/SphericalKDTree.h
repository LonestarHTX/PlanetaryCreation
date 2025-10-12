#pragma once

#include "CoreMinimal.h"

/**
 * Simple KD-tree for finding nearest neighbors on a sphere.
 * Optimized for static point sets (plate centroids) with frequent queries (render vertices).
 */
class FSphericalKDTree
{
public:
	/** Build tree from plate centroids with associated IDs. */
	void Build(const TArray<FVector3d>& Points, const TArray<int32>& PointIDs);

	/** Find the closest point to the query, returns the associated ID. */
	int32 FindNearest(const FVector3d& Query, double& OutDistanceSq) const;

	struct FMemoryUsage
	{
		int32 NodeCount = 0;
		int64 NodeBytes = 0;
	};

	/** Estimate memory used by the KD-tree nodes. */
	FMemoryUsage EstimateMemoryUsage() const;

	/** Clear the tree. */
	void Clear();

	/** Check if tree is built. */
	bool IsValid() const { return RootNode != nullptr; }

private:
	struct FKDNode
	{
		FVector3d Point;
		int32 PointID = INDEX_NONE;
		int32 SplitAxis = 0; // 0=X, 1=Y, 2=Z
		TUniquePtr<FKDNode> Left;
		TUniquePtr<FKDNode> Right;
	};

	TUniquePtr<FKDNode> RootNode;

	/** Recursive tree build. */
	TUniquePtr<FKDNode> BuildRecursive(TArray<TPair<FVector3d, int32>>& Points, int32 Depth);

	/** Recursive nearest neighbor search. */
	void FindNearestRecursive(const FKDNode* Node, const FVector3d& Query, int32& BestID, double& BestDistSq) const;
};
