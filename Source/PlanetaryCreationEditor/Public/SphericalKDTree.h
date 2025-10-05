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

        /**
         * Find the K nearest neighbors to the query point.
         * @param Query Point to query in unit sphere coordinates.
         * @param K Number of neighbors to gather (clamped to point count).
         * @param OutIDs Filled with point IDs sorted by ascending distance.
         * @param OutDistancesSq Optional distances squared corresponding to OutIDs.
         */
        void FindKNearest(const FVector3d& Query, int32 K, TArray<int32>& OutIDs, TArray<double>* OutDistancesSq = nullptr) const;

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

        /** Recursive K-nearest neighbor search. */
        void FindKNearestRecursive(const FKDNode* Node, const FVector3d& Query, int32 K, TArray<TPair<double, int32>>& Best) const;
};
