#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"

/**
 * Deterministic Fibonacci sphere sampling utilities.
 */
class PLANETARYCREATIONEDITOR_API FFibonacciSampling
{
public:
    static void GenerateSamples(int32 N, TArray<FVector3d>& OutUnitPoints);

    static void GenerateSamplesScaled(int32 N, double RadiusMeters, TArray<FVector3d>& OutPointsMeters);

    static int32 ComputeSampleCount(double PlanetRadiusKm, double TargetResolutionKm); // N = 4πR² / res²

    static double ComputeResolution(double PlanetRadiusKm, int32 SampleCount); // res = √(4πR² / N)
};
