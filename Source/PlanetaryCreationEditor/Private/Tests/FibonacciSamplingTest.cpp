#include "CoreMinimal.h"
#include "Simulation/FibonacciSampling.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFibonacciSamplingTest, "PlanetaryCreation.Paper.FibonacciSampling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFibonacciSamplingTest::RunTest(const FString& Parameters)
{
    constexpr int32 SampleCount = 10000;
    TArray<FVector3d> UnitSamples;
    FFibonacciSampling::GenerateSamples(SampleCount, UnitSamples);

    TestEqual(TEXT("unit sample count"), UnitSamples.Num(), SampleCount);

    TArray<FVector3d> UnitSamplesRepeat;
    FFibonacciSampling::GenerateSamples(SampleCount, UnitSamplesRepeat);
    TestTrue(TEXT("deterministic first"), UnitSamples[0] == UnitSamplesRepeat[0]);
    TestTrue(TEXT("deterministic middle"), UnitSamples[SampleCount / 2] == UnitSamplesRepeat[SampleCount / 2]);
    TestTrue(TEXT("deterministic last"), UnitSamples[SampleCount - 1] == UnitSamplesRepeat[SampleCount - 1]);

    double MinLength = TNumericLimits<double>::Max();
    double MaxLength = 0.0;

    for (const FVector3d& Sample : UnitSamples)
    {
        const double Length = Sample.Length();
        MinLength = FMath::Min(MinLength, Length);
        MaxLength = FMath::Max(MaxLength, Length);
    }

    TestTrue(TEXT("unit samples normalized"), MinLength >= 0.999999 && MaxLength <= 1.000001);

    int32 OctantCounts[8] = {0};
    for (const FVector3d& Sample : UnitSamples)
    {
        const int32 Octant =
            (Sample.X >= 0.0 ? 4 : 0) |
            (Sample.Y >= 0.0 ? 2 : 0) |
            (Sample.Z >= 0.0 ? 1 : 0);
        ++OctantCounts[Octant];
    }

    const double ExpectedPerOctant = static_cast<double>(SampleCount) / 8.0;
    double ChiSquare = 0.0;
    for (int32 Index = 0; Index < 8; ++Index)
    {
        const double Delta = static_cast<double>(OctantCounts[Index]) - ExpectedPerOctant;
        ChiSquare += (Delta * Delta) / ExpectedPerOctant;
    }

    TestTrue(TEXT("chi-square < 14.07"), ChiSquare < 14.07);

    TArray<FVector3d> ScaledSamples;
    FFibonacciSampling::GenerateSamplesScaled(64, 1000.0, ScaledSamples);
    TestEqual(TEXT("scaled sample count"), ScaledSamples.Num(), 64);

    double MinScaledLength = TNumericLimits<double>::Max();
    double MaxScaledLength = 0.0;
    for (const FVector3d& Sample : ScaledSamples)
    {
        const double Length = Sample.Length();
        MinScaledLength = FMath::Min(MinScaledLength, Length);
        MaxScaledLength = FMath::Max(MaxScaledLength, Length);
    }

    TestTrue(TEXT("scaled samples normalized"), MinScaledLength >= 999.999 && MaxScaledLength <= 1000.001);

    const double PlanetRadiusKm = 6370.0;
    const int32 BaseSamples = 500000;
    const double ResolutionKm = FFibonacciSampling::ComputeResolution(PlanetRadiusKm, BaseSamples);
    TestTrue(TEXT("resolution 500k in range"), ResolutionKm >= 31.5 && ResolutionKm <= 36.0);

    const int32 DerivedSamples = FFibonacciSampling::ComputeSampleCount(PlanetRadiusKm, 35.0);
    TestTrue(TEXT("inverse resolution near 500k"), FMath::Abs(DerivedSamples - 500000) <= 85000);

    return true;
}
