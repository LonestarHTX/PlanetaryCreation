#include "Simulation/FibonacciSampling.h"

#include "Math/UnrealMathUtility.h"

namespace
{
    constexpr double GoldenAngle = 2.39996322972865332; // PI * (3 - sqrt(5))
}

void FFibonacciSampling::GenerateSamples(int32 N, TArray<FVector3d>& OutUnitPoints)
{
    OutUnitPoints.Reset();

    if (N <= 0)
    {
        return;
    }

    OutUnitPoints.Reserve(N);

    const double Count = static_cast<double>(N);

    for (int32 Index = 0; Index < N; ++Index)
    {
        const double K = static_cast<double>(Index);
        const double Offset = (K + 0.5) / Count;
        const double Z = 1.0 - 2.0 * Offset;
        const double Radius = FMath::Sqrt(FMath::Max(0.0, 1.0 - (Z * Z)));
        const double Phi = GoldenAngle * K;

        const double X = FMath::Cos(Phi) * Radius;
        const double Y = FMath::Sin(Phi) * Radius;

        OutUnitPoints.Emplace(X, Y, Z);
        OutUnitPoints.Last().Normalize();
    }
}

void FFibonacciSampling::GenerateSamplesScaled(int32 N, double RadiusMeters, TArray<FVector3d>& OutPointsMeters)
{
    GenerateSamples(N, OutPointsMeters);

    if (RadiusMeters <= 0.0)
    {
        for (FVector3d& Sample : OutPointsMeters)
        {
            Sample = FVector3d::ZeroVector;
        }

        return;
    }

    for (FVector3d& Sample : OutPointsMeters)
    {
        Sample *= RadiusMeters;
    }
}

int32 FFibonacciSampling::ComputeSampleCount(double PlanetRadiusKm, double TargetResolutionKm)
{
    if (PlanetRadiusKm <= 0.0 || TargetResolutionKm <= 0.0)
    {
        return 0;
    }

    const double Area = 4.0 * UE_DOUBLE_PI * PlanetRadiusKm * PlanetRadiusKm;
    const double ResolutionArea = TargetResolutionKm * TargetResolutionKm;
    const double SampleCount = Area / ResolutionArea;
    const int64 Rounded = FMath::RoundToInt64(SampleCount);

    return static_cast<int32>(FMath::Clamp<int64>(Rounded, 0, TNumericLimits<int32>::Max()));
}

double FFibonacciSampling::ComputeResolution(double PlanetRadiusKm, int32 SampleCount)
{
    if (PlanetRadiusKm <= 0.0 || SampleCount <= 0)
    {
        return 0.0;
    }

    const double Area = 4.0 * UE_DOUBLE_PI * PlanetRadiusKm * PlanetRadiusKm;
    const double Resolution = FMath::Sqrt(Area / static_cast<double>(SampleCount));

    return Resolution;
}
