#include "HeightmapSampling.h"

#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "TectonicSimulationController.h"

#include "Editor.h"

namespace
{
    /** Convert a unit direction vector to the exporter UV convention. */
    FVector2d DirectionToUV(const FVector3d& Direction)
    {
        const FVector3d Normalized = Direction.GetSafeNormal();
        const double Longitude = FMath::Atan2(Normalized.Y, Normalized.X);
        const double Latitude = FMath::Asin(FMath::Clamp(Normalized.Z, -1.0, 1.0));

        const double U = 0.5 + Longitude / (2.0 * PI);
        const double V = 0.5 - Latitude / PI;
        return FVector2d(U, V);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeightmapSamplerInterpolationTest,
    "PlanetaryCreation.Heightmap.SampleInterpolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHeightmapSamplerInterpolationTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor ? GEditor->GetEditorSubsystem<UTectonicSimulationService>() : nullptr;
    TestNotNull(TEXT("Simulation service must exist"), Service);
    if (!Service)
    {
        return false;
    }

    const FTectonicSimulationParameters OriginalParams = Service->GetParameters();
    const TArray<double> OriginalAmplified = Service->GetVertexAmplifiedElevation();

    FTectonicSimulationParameters TestParams = OriginalParams;
    TestParams.Seed = 2024;
    TestParams.RenderSubdivisionLevel = 3;
    TestParams.SubdivisionLevel = 0;
    TestParams.bEnableOceanicAmplification = true;
    TestParams.bEnableContinentalAmplification = true;
    TestParams.MinAmplificationLOD = 3;

    Service->SetParameters(TestParams);

    // Disable GPU amplification during automation to prevent GPU power spikes/crashes
    // Test only validates CPU sampling logic, not GPU compute correctness
    IConsoleVariable* CVarGPU = IConsoleManager::Get().FindConsoleVariable(
        TEXT("r.PlanetaryCreation.UseGPUAmplification"));
    const int32 OriginalGPUSetting = CVarGPU ? CVarGPU->GetInt() : 1;
    if (CVarGPU)
    {
        CVarGPU->Set(0, ECVF_SetByCode);
    }

    Service->AdvanceSteps(4);

    const TArray<FVector3d>& Vertices = Service->GetRenderVertices();
    const TArray<int32>& Triangles = Service->GetRenderTriangles();
    const TArray<double>& Baseline = Service->GetVertexElevationValues();
    TArray<double>& AmplifiedMutable = Service->GetMutableVertexAmplifiedElevation();

    TestTrue(TEXT("Sampler requires populated mesh"), Vertices.Num() > 0 && Triangles.Num() >= 3);
    if (Vertices.Num() == 0 || Triangles.Num() < 3)
    {
        Service->SetParameters(OriginalParams);
        Service->GetMutableVertexAmplifiedElevation() = OriginalAmplified;
        return false;
    }

    const int32 TriangleIndex = 0;
    const int32 IndexBase = TriangleIndex * 3;
    const int32 VertexA = Triangles[IndexBase + 0];
    const int32 VertexB = Triangles[IndexBase + 1];
    const int32 VertexC = Triangles[IndexBase + 2];

    TestTrue(TEXT("Triangle indices must be valid"),
        Vertices.IsValidIndex(VertexA) &&
        Vertices.IsValidIndex(VertexB) &&
        Vertices.IsValidIndex(VertexC) &&
        Baseline.IsValidIndex(VertexA) &&
        Baseline.IsValidIndex(VertexB) &&
        Baseline.IsValidIndex(VertexC));

    if (!Vertices.IsValidIndex(VertexA) ||
        !Vertices.IsValidIndex(VertexB) ||
        !Vertices.IsValidIndex(VertexC) ||
        !Baseline.IsValidIndex(VertexA) ||
        !Baseline.IsValidIndex(VertexB) ||
        !Baseline.IsValidIndex(VertexC))
    {
        Service->SetParameters(OriginalParams);
        Service->GetMutableVertexAmplifiedElevation() = OriginalAmplified;
        return false;
    }

    const FVector3d& A = Vertices[VertexA];
    const FVector3d& B = Vertices[VertexB];
    const FVector3d& C = Vertices[VertexC];

    // Sample the centroid of the first triangle (should return triangle 0).
    const FVector3d TriangleCentroid = ((A + B + C) / 3.0).GetSafeNormal();
    const FVector2d SampleUV = DirectionToUV(TriangleCentroid);

    // Ensure amplified data is absent to force baseline path.
    AmplifiedMutable.Reset();

    FHeightmapSampler BaselineSampler(*Service);
    TestTrue(TEXT("Baseline sampler should be valid"), BaselineSampler.IsValid());

    FHeightmapSampler::FSampleInfo BaselineInfo;
    const double BaselineSample = BaselineSampler.SampleElevationAtUV(SampleUV, &BaselineInfo);
    TestTrue(TEXT("Baseline sample should locate a containing triangle"), BaselineInfo.bHit);

    // Verify we hit the expected triangle
    TestEqual(TEXT("Baseline sample should hit triangle 0"), BaselineInfo.TriangleIndex, TriangleIndex);

    // With actual barycentrics from the sampler, compute expected value
    if (BaselineInfo.bHit)
    {
        const double ExpectedBaseline =
            BaselineInfo.Barycentrics.X * Baseline[VertexA] +
            BaselineInfo.Barycentrics.Y * Baseline[VertexB] +
            BaselineInfo.Barycentrics.Z * Baseline[VertexC];

        TestTrue(TEXT("Baseline sample should match barycentric interpolation"),
            FMath::IsNearlyEqual(BaselineSample, ExpectedBaseline, 1.0e-6));
    }

    // Prepare amplified data with a deterministic offset.
    AmplifiedMutable = Service->GetVertexElevationValues();
    for (int32 Index = 0; Index < AmplifiedMutable.Num(); ++Index)
    {
        AmplifiedMutable[Index] += 50.0;
    }

    FHeightmapSampler AmplifiedSampler(*Service);
    TestTrue(TEXT("Amplified sampler should be valid"), AmplifiedSampler.IsValid());

    FHeightmapSampler::FSampleInfo AmplifiedInfo;
    const double AmplifiedSample = AmplifiedSampler.SampleElevationAtUV(SampleUV, &AmplifiedInfo);
    TestTrue(TEXT("Amplified sample should locate a containing triangle"), AmplifiedInfo.bHit);

    // Verify amplified data is used (should be baseline + 50m)
    if (AmplifiedInfo.bHit)
    {
        const double ExpectedAmplified =
            AmplifiedInfo.Barycentrics.X * AmplifiedMutable[VertexA] +
            AmplifiedInfo.Barycentrics.Y * AmplifiedMutable[VertexB] +
            AmplifiedInfo.Barycentrics.Z * AmplifiedMutable[VertexC];

        TestTrue(TEXT("Amplified sample should match amplified interpolation"),
            FMath::IsNearlyEqual(AmplifiedSample, ExpectedAmplified, 1.0e-6));

        // Also verify it's different from baseline (by ~50m)
        TestTrue(TEXT("Amplified sample should differ from baseline by ~50m"),
            FMath::IsNearlyEqual(AmplifiedSample - BaselineSample, 50.0, 1.0));
    }

    // Restore prior state.
    Service->SetParameters(OriginalParams);
    Service->GetMutableVertexAmplifiedElevation() = OriginalAmplified;

    // Restore GPU setting
    if (CVarGPU)
    {
        CVarGPU->Set(OriginalGPUSetting, ECVF_SetByCode);
    }

    return true;
}
