# Stage B Audit Findings

## OceanicAmplification.cpp – Ridge helper returns placeholder data
The `ComputeRidgeDirection` helper currently fabricates an "edge midpoint" by probing `Plates[0].VertexIndices` and returns either the zero vector or `ZAxisVector`, so the computed tangent is effectively random instead of using the shared boundary vertices.【F:Source/PlanetaryCreationEditor/Private/OceanicAmplification.cpp†L92-L109】 If this helper is ever invoked (for example, when wiring Stage B CPU amplification directly to topology data), transform-fault noise would be oriented incorrectly. Refactor it to look up the actual boundary vertices from `SharedVertices`, or remove the helper until a correct implementation is available so it cannot be called accidentally.

## PlateTopologyChanges.cpp – Topology events pick the first hash entry
Both split and merge detection accumulate candidates in arrays populated by iterating the `Boundaries` `TMap`, and then execute the first element (`CandidateSplits[0]` / `CandidateMerges[0]`).【F:Source/PlanetaryCreationEditor/Private/PlateTopologyChanges.cpp†L15-L171】 Because `TMap` iteration order depends on hash/insertion order, the chosen event can flip between runs or after a re-tessellation even when another boundary has higher stress or rift width. Introduce a deterministic priority—e.g., sort by rift width, divergent duration, or accumulated stress—before selecting a candidate so the most severe event is resolved consistently.

## OceanicAmplificationGPU.cpp – Seam coverage metrics double-count
`ComputeSeamCoverageMetrics` increments *both* the left and right counters whenever a vertex projects near either seam, so the reported counts are always identical and effectively double the real coverage.【F:Source/PlanetaryCreationEditor/Private/OceanicAmplificationGPU.cpp†L83-L124】 Adjust the logic to increment only the seam that the vertex actually maps to (and optionally track mirrored coverage separately) so the preview log reflects real seam coverage gaps.

## HeightmapExporter.cpp – Dilation copies the full image every pass
The multi-pass dilation loop clones the entire `ImageData` array on every pass before scanning for neighbors.【F:Source/PlanetaryCreationEditor/Private/HeightmapExporter.cpp†L131-L170】 On large exports (e.g., 4k×2k) with the default 50 passes this becomes several gigabytes of transient copies and significantly slows exports. Rework the dilation to reuse a scratch buffer, stream rows, or break early once no unfilled pixels remain to keep the exporter responsive for high-resolution captures.
