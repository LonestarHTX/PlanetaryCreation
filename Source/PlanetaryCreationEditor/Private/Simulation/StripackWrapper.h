#pragma once

#include "Simulation/StripackConfig.h"

#if WITH_STRIPACK
#include "Simulation/SphericalDelaunay.h"

namespace Stripack
{
    bool ComputeTriangulation(const TArray<FVector3d>& SpherePoints, TArray<FSphericalDelaunay::FTriangle>& OutTriangles);
}
#endif
