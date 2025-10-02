#include "TectonicSimulationService.h"

void UTectonicSimulationService::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ResetSimulation();
}

void UTectonicSimulationService::Deinitialize()
{
    BaseSphereSamples.Reset();
    Super::Deinitialize();
}

void UTectonicSimulationService::ResetSimulation()
{
    CurrentTimeMy = 0.0;
    GenerateDefaultSphereSamples();
}

void UTectonicSimulationService::AdvanceSteps(int32 StepCount)
{
    if (StepCount <= 0)
    {
        return;
    }

    constexpr double StepDurationMy = 2.0; // Paper defines delta t = 2 My per iteration
    CurrentTimeMy += StepDurationMy * static_cast<double>(StepCount);
}

void UTectonicSimulationService::GenerateDefaultSphereSamples()
{
    BaseSphereSamples.Reset();

    // Minimal placeholder: an octahedron on the unit sphere
    const TArray<FVector3d> SampleSeeds = {
        FVector3d(1.0, 0.0, 0.0),
        FVector3d(-1.0, 0.0, 0.0),
        FVector3d(0.0, 1.0, 0.0),
        FVector3d(0.0, -1.0, 0.0),
        FVector3d(0.0, 0.0, 1.0),
        FVector3d(0.0, 0.0, -1.0)
    };

    BaseSphereSamples.Reserve(SampleSeeds.Num());
    for (const FVector3d& Seed : SampleSeeds)
    {
        BaseSphereSamples.Add(Seed.GetSafeNormal());
    }
}
