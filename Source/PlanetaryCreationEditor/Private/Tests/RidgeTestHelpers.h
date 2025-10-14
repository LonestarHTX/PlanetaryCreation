#pragma once

#include "CoreMinimal.h"

class UTectonicSimulationService;

namespace PlanetaryCreation::Tests
{
struct FRidgeTripleJunctionFixture
{
    int32 VertexIndex = INDEX_NONE;
    TArray<int32> OpposingPlates;
    double CrustAgeMy = 0.0;
};

struct FRidgeCrustAgeDiscontinuityFixture
{
    int32 YoungVertexIndex = INDEX_NONE;
    int32 OldVertexIndex = INDEX_NONE;
    double YoungAgeMy = 0.0;
    double OldAgeMy = 0.0;
    double AgeDeltaMy = 0.0;
    int32 PlateID = INDEX_NONE;
};

/** Locate a deterministic ridge triple-junction (young oceanic vertex with ≥3 divergent neighbours). */
bool BuildRidgeTripleJunctionFixture(const UTectonicSimulationService& Service, FRidgeTripleJunctionFixture& OutFixture);

/**
 * Locate a deterministic crust-age discontinuity pair on the same oceanic plate.
 * Returns the youngest/oldest vertex pair whose age delta exceeds the configured guard.
 */
bool BuildRidgeCrustAgeDiscontinuityFixture(
    const UTectonicSimulationService& Service,
    FRidgeCrustAgeDiscontinuityFixture& OutFixture,
    double MinAgeDeltaMy = 12.0);

/** Grow a contiguous vertex set on PlateID starting from SeedVertex until TargetCount is reached. */
bool BuildContiguousPlateRegion(
    const UTectonicSimulationService& Service,
    int32 PlateID,
    int32 SeedVertex,
    int32 TargetCount,
    TArray<int32>& OutVertices);
} // namespace PlanetaryCreation::Tests

