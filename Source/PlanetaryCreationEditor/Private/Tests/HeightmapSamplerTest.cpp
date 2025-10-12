#include "HeightmapSampling.h"

#include "Misc/AutomationTest.h"
#include "StageBAmplificationTypes.h"
#include "TectonicSimulationService.h"
#include "TectonicSimulationController.h"

#include "Editor.h"

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
    const FVector2d SampleUV = PlanetaryCreation::StageB::EquirectUVFromDirection(TriangleCentroid);

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
    TestEqual(TEXT("Snapshot float availability matches sampler state"),
        AmplifiedSampler.UsesSnapshotFloatBuffer(),
        Service->IsStageBAmplificationReady() && Service->GetVertexAmplifiedElevation().Num() == Vertices.Num());

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

    // Seam continuity check across the U=0/1 boundary.
    {
        const double SeamV = 0.35;
        const FVector2d SeamLeft(1.0 - 1.0e-6, SeamV);
        const FVector2d SeamRight(1.0e-6, SeamV);

        FHeightmapSampler::FSampleInfo LeftInfo;
        FHeightmapSampler::FSampleInfo RightInfo;
        const double LeftSample = AmplifiedSampler.SampleElevationAtUV(SeamLeft, &LeftInfo);
        const double RightSample = AmplifiedSampler.SampleElevationAtUV(SeamRight, &RightInfo);

        TestTrue(TEXT("Seam samples should succeed"), LeftInfo.bHit && RightInfo.bHit);
        if (LeftInfo.bHit && RightInfo.bHit)
        {
            const double SeamDelta = FMath::Abs(LeftSample - RightSample);
            TestTrue(TEXT("Seam delta should stay under 1 m"), SeamDelta < 1.0);
        }
    }

    // Pole sampling resilience check near both poles.
    {
        const double PoleU = 0.42;
        const double PoleEpsilon = FHeightmapSampler::PoleAvoidanceEpsilon;

        const FVector2d NorthUV(PoleU, PoleEpsilon);
        const FVector2d SouthUV(PoleU, 1.0 - PoleEpsilon);

        FHeightmapSampler::FSampleInfo NorthInfo;
        FHeightmapSampler::FSampleInfo SouthInfo;
        const double NorthSample = AmplifiedSampler.SampleElevationAtUV(NorthUV, &NorthInfo);
        const double SouthSample = AmplifiedSampler.SampleElevationAtUV(SouthUV, &SouthInfo);

        TestTrue(TEXT("Pole samples should succeed"), NorthInfo.bHit && SouthInfo.bHit);
        if (NorthInfo.bHit)
        {
            const double SumNorthBary = NorthInfo.Barycentrics.X + NorthInfo.Barycentrics.Y + NorthInfo.Barycentrics.Z;
            TestTrue(TEXT("North pole barycentrics sum to ~1"), FMath::IsNearlyEqual(SumNorthBary, 1.0, 1.0e-3));
            TestTrue(TEXT("North pole sample finite"), FMath::IsFinite(NorthSample));
        }
        if (SouthInfo.bHit)
        {
            const double SumSouthBary = SouthInfo.Barycentrics.X + SouthInfo.Barycentrics.Y + SouthInfo.Barycentrics.Z;
            TestTrue(TEXT("South pole barycentrics sum to ~1"), FMath::IsNearlyEqual(SumSouthBary, 1.0, 1.0e-3));
            TestTrue(TEXT("South pole sample finite"), FMath::IsFinite(SouthSample));
        }
    }

    // Compare sampler interpolation against Stage B snapshot float buffer when available.
    {
        const TArray<float>* SnapshotFloats = nullptr;
        const TArray<FVector4f>* DummyRidge = nullptr;
        const TArray<float>* DummyCrust = nullptr;
        const TArray<FVector3f>* DummyPositions = nullptr;
        const TArray<uint32>* DummyMask = nullptr;
        Service->GetOceanicAmplificationFloatInputs(SnapshotFloats, DummyRidge, DummyCrust, DummyPositions, DummyMask);

        if (SnapshotFloats && SnapshotFloats->Num() == AmplifiedMutable.Num())
        {
            const double ExpectedSnapshotValue =
                AmplifiedInfo.Barycentrics.X * static_cast<double>((*SnapshotFloats)[VertexA]) +
                AmplifiedInfo.Barycentrics.Y * static_cast<double>((*SnapshotFloats)[VertexB]) +
                AmplifiedInfo.Barycentrics.Z * static_cast<double>((*SnapshotFloats)[VertexC]);

            TestTrue(TEXT("Sampler matches Stage B snapshot float interpolation"),
                FMath::IsNearlyEqual(AmplifiedSample, ExpectedSnapshotValue, 1.0e-2));
        }
        else
        {
            AddInfo(TEXT("Skipping Stage B snapshot float parity assertion (snapshot floats unavailable)."));
        }
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
