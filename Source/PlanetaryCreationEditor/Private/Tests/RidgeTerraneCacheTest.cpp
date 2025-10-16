#include "Utilities/PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "Simulation/TectonicSimulationService.h"
#include "Export/HeightmapSampling.h"
#include "Tests/RidgeTestHelpers.h"

#include "Editor.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
constexpr int32 RidgeFixtureSeed = 42;
constexpr int32 RidgeRenderSubdivisionLevel = 4;
constexpr int32 TerraneTargetVertexCount = 24;
constexpr double CrustAgeDiscontinuityThreshold = 12.0;
constexpr int32 HeatmapWidth = 512;
constexpr int32 HeatmapHeight = 256;

double ComputeCoveragePercent(const UTectonicSimulationService& Service)
{
    const double Coverage = Service.GetLastRidgeTangentCoveragePercent();
    if (Coverage > 0.0)
    {
        return Coverage;
    }

    const int32 Divergent = Service.GetLastRidgeOceanicVertexCount();
    const int32 Valid = Service.GetLastRidgeValidTangentCount();
    if (Divergent <= 0)
    {
        return 100.0;
    }
    return (static_cast<double>(Valid) / static_cast<double>(Divergent)) * 100.0;
}

bool ExportRidgeTangentHeatmap(
    UTectonicSimulationService& Service,
    const FString& Label,
    FString& OutPath,
    FAutomationTestBase& Test)
{
    FHeightmapSampler Sampler(Service);
    if (!Sampler.IsValid())
    {
        Test.AddError(FString::Printf(TEXT("FHeightmapSampler invalid when exporting ridge tangent heatmap '%s'"), *Label));
        return false;
    }

    const TArray<FVector3f>& Tangents = Service.GetVertexRidgeTangents();
    const TArray<FVector3d>& RenderVertices = Service.GetRenderVertices();
    if (Tangents.Num() != RenderVertices.Num())
    {
        Test.AddError(TEXT("Ridge tangent array size mismatch during heatmap export"));
        return false;
    }

    TArray<FColor> Pixels;
    Pixels.SetNumZeroed(HeatmapWidth * HeatmapHeight);

    int32 HintTriangle = INDEX_NONE;
    for (int32 Y = 0; Y < HeatmapHeight; ++Y)
    {
        const double V = (static_cast<double>(Y) + 0.5) / static_cast<double>(HeatmapHeight);
        for (int32 X = 0; X < HeatmapWidth; ++X)
        {
            const double U = (static_cast<double>(X) + 0.5) / static_cast<double>(HeatmapWidth);
            FHeightmapSampler::FSampleInfo Info;
            double DummyElevation = 0.0;

            bool bHit = Sampler.SampleElevationAtUVWithHint(FVector2d(U, V), HintTriangle, &Info, DummyElevation);
            if (!bHit)
            {
                bHit = Sampler.SampleElevationAtUV(FVector2d(U, V), &Info) != 0.0 && Info.bHit;
            }

            const int32 PixelIndex = Y * HeatmapWidth + X;
            if (!bHit || Info.TriangleIndex == INDEX_NONE)
            {
                Pixels[PixelIndex] = FColor::Black;
                continue;
            }

            HintTriangle = Info.TriangleIndex;
            int32 TriangleVertices[3];
            if (!Sampler.GetTriangleVertexIndices(Info.TriangleIndex, TriangleVertices))
            {
                Pixels[PixelIndex] = FColor::Black;
                continue;
            }

            FVector3f Interpolated = FVector3f::ZeroVector;
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const int32 VertexIndex = TriangleVertices[Corner];
                if (!Tangents.IsValidIndex(VertexIndex))
                {
                    Interpolated = FVector3f::ZeroVector;
                    break;
                }
                Interpolated += static_cast<float>(Info.Barycentrics[Corner]) * Tangents[VertexIndex];
            }

            const float Magnitude = Interpolated.Length();
            FVector3f Direction = Magnitude > KINDA_SMALL_NUMBER ? Interpolated / Magnitude : FVector3f::ZeroVector;
            const float Strength = FMath::Clamp(Magnitude, 0.0f, 1.0f);

            auto EncodeChannel = [Strength](float Component) -> uint8
            {
                const float Adjusted = (Component * 0.5f + 0.5f) * Strength;
                return static_cast<uint8>(FMath::Clamp(Adjusted, 0.0f, 1.0f) * 255.0f + 0.5f);
            };

            const uint8 R = EncodeChannel(Direction.X);
            const uint8 G = EncodeChannel(Direction.Y);
            const uint8 B = EncodeChannel(Direction.Z);
            Pixels[PixelIndex] = FColor(R, G, B, 255);
        }
    }

    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
    if (!ImageWrapper.IsValid())
    {
        Test.AddError(TEXT("Failed to create PNG image wrapper for ridge tangent heatmap"));
        return false;
    }

    if (!ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), HeatmapWidth, HeatmapHeight, ERGBFormat::BGRA, 8))
    {
        Test.AddError(TEXT("Failed to encode ridge tangent heatmap as PNG"));
        return false;
    }

    const FString OutputDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Automation/Validation/ParityFigures"));
    IFileManager::Get().MakeDirectory(*OutputDir, true);

    const FString OutputPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("ridge_tangent_%s.png"), *Label));
    const TArray64<uint8>& Compressed = ImageWrapper->GetCompressed(100);
    if (!FFileHelper::SaveArrayToFile(Compressed, *OutputPath))
    {
        Test.AddError(FString::Printf(TEXT("Failed to save ridge tangent heatmap to '%s'"), *OutputPath));
        return false;
    }

    OutPath = OutputPath;
    return true;
}

double ComputePercentage(double Numerator, double Denominator)
{
    if (Denominator <= 0.0)
    {
        return 0.0;
    }
    return (Numerator / Denominator) * 100.0;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidgeTerraneCacheTest,
    "PlanetaryCreation.StageB.RidgeTerraneCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRidgeTerraneCacheTest::RunTest(const FString& Parameters)
{
    if (!GEditor)
    {
        AddError(TEXT("Editor context is required for ridge terrane cache test"));
        return false;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
    {
        return false;
    }

    FTectonicSimulationParameters Params = Service->GetParameters();
    Params.Seed = RidgeFixtureSeed;
    Params.SubdivisionLevel = 1;
    Params.RenderSubdivisionLevel = RidgeRenderSubdivisionLevel;
    Params.MinAmplificationLOD = 0;
    Params.bSkipCPUAmplification = false;
    Params.bEnableContinentalErosion = false;
    Params.bEnableSedimentTransport = false;
    Params.bEnableOceanicDampening = true;
    Params.bEnableHydraulicErosion = false;
    Service->SetParameters(Params);

    Service->AdvanceSteps(24);
    Service->ProcessPendingOceanicGPUReadbacks(true);
    Service->ProcessPendingContinentalGPUReadbacks(true);
    Service->BuildRenderVertexBoundaryCache();
    Service->MarkAllRidgeDirectionsDirty();
    Service->ForceRidgeRecomputeForTest();

    const double BaselineCoverage = ComputeCoveragePercent(*Service);
    TestTrue(TEXT("Baseline ridge tangent coverage ≥99%"), BaselineCoverage >= 99.0 - KINDA_SMALL_NUMBER);

    const double DirtyCount = static_cast<double>(FMath::Max(1, Service->GetLastRidgeDirtyVertexCount()));
    const double GradientFallbackPct = ComputePercentage(static_cast<double>(Service->GetLastRidgeGradientFallbackCount()), DirtyCount);
    const double MotionFallbackPct = ComputePercentage(static_cast<double>(Service->GetLastRidgeMotionFallbackCount()), DirtyCount);

    TestTrue(TEXT("Baseline gradient fallback ≤1%"), GradientFallbackPct <= 1.0 + KINDA_SMALL_NUMBER);
    TestTrue(TEXT("Baseline motion fallback ≤0.1%"), MotionFallbackPct <= 0.1 + KINDA_SMALL_NUMBER);

    PlanetaryCreation::Tests::FRidgeTripleJunctionFixture TripleFixture;
    TestTrue(TEXT("Found ridge triple-junction fixture"), PlanetaryCreation::Tests::BuildRidgeTripleJunctionFixture(*Service, TripleFixture));
    if (TripleFixture.VertexIndex != INDEX_NONE)
    {
        AddInfo(FString::Printf(TEXT("[RidgeFixture] TripleJunction vertex=%d plates=%s age=%.2f My"),
            TripleFixture.VertexIndex,
            *FString::JoinBy(TripleFixture.OpposingPlates, TEXT(","), [](int32 PlateID) { return FString::FromInt(PlateID); }),
            TripleFixture.CrustAgeMy));
    }

    PlanetaryCreation::Tests::FRidgeCrustAgeDiscontinuityFixture AgeFixture;
    TestTrue(TEXT("Found crust-age discontinuity fixture"),
        PlanetaryCreation::Tests::BuildRidgeCrustAgeDiscontinuityFixture(*Service, AgeFixture, CrustAgeDiscontinuityThreshold));
    if (AgeFixture.YoungVertexIndex != INDEX_NONE)
    {
        AddInfo(FString::Printf(TEXT("[RidgeFixture] AgeDiscontinuity plate=%d young=%d (%.2f My) old=%d (%.2f My) delta=%.2f My"),
            AgeFixture.PlateID,
            AgeFixture.YoungVertexIndex,
            AgeFixture.YoungAgeMy,
            AgeFixture.OldVertexIndex,
            AgeFixture.OldAgeMy,
            AgeFixture.AgeDeltaMy));
    }

    FString HeatmapBeforePath;
    TestTrue(TEXT("Exported ridge tangent heatmap (before terrane op)"),
        ExportRidgeTangentHeatmap(*Service, TEXT("before_reattach"), HeatmapBeforePath, *this));
    if (!HeatmapBeforePath.IsEmpty())
    {
        AddInfo(FString::Printf(TEXT("Ridge tangent heatmap (before): %s"), *HeatmapBeforePath));
    }

    // Select a continental plate and build a terrane candidate.
    const TArray<FTectonicPlate>& Plates = Service->GetPlates();
    const TArray<int32>& PlateAssignments = Service->GetVertexPlateAssignments();

    int32 SourcePlateID = INDEX_NONE;
    for (const FTectonicPlate& Plate : Plates)
    {
        if (Plate.CrustType == ECrustType::Continental)
        {
            SourcePlateID = Plate.PlateID;
            break;
        }
    }
    TestTrue(TEXT("Found continental plate for terrane extraction"), SourcePlateID != INDEX_NONE);
    if (SourcePlateID == INDEX_NONE)
    {
        return false;
    }

    int32 TerraneSeedVertex = INDEX_NONE;
    for (int32 VertexIdx = 0; VertexIdx < PlateAssignments.Num(); ++VertexIdx)
    {
        if (PlateAssignments[VertexIdx] == SourcePlateID)
        {
            TerraneSeedVertex = VertexIdx;
            break;
        }
    }
    TestTrue(TEXT("Found seed vertex for terrane extraction"), TerraneSeedVertex != INDEX_NONE);
    if (TerraneSeedVertex == INDEX_NONE)
    {
        return false;
    }

    TArray<int32> TerraneVertices;
    TestTrue(TEXT("Built contiguous terrane candidate"),
        PlanetaryCreation::Tests::BuildContiguousPlateRegion(*Service, SourcePlateID, TerraneSeedVertex, TerraneTargetVertexCount, TerraneVertices));

    const double TerraneAreaKm2 = Service->ComputeTerraneArea(TerraneVertices);
    TestTrue(TEXT("Terrane candidate area ≥ 100 km²"), TerraneAreaKm2 >= 100.0);

    int32 TerraneID = INDEX_NONE;
    TestTrue(TEXT("Terrane extraction succeeded"), Service->ExtractTerrane(SourcePlateID, TerraneVertices, TerraneID));
    TestTrue(TEXT("Valid terrane ID returned"), TerraneID != INDEX_NONE);

    Service->BuildRenderVertexBoundaryCache();
    Service->MarkAllRidgeDirectionsDirty();
    Service->ForceRidgeRecomputeForTest();

    const double PostExtractCoverage = ComputeCoveragePercent(*Service);
    const double PostExtractGradientPct = ComputePercentage(static_cast<double>(Service->GetLastRidgeGradientFallbackCount()),
        static_cast<double>(FMath::Max(1, Service->GetLastRidgeDirtyVertexCount())));
    const double PostExtractMotionPct = ComputePercentage(static_cast<double>(Service->GetLastRidgeMotionFallbackCount()),
        static_cast<double>(FMath::Max(1, Service->GetLastRidgeDirtyVertexCount())));

    TestTrue(TEXT("Post-extraction ridge tangent coverage ≥99%"), PostExtractCoverage >= 99.0 - KINDA_SMALL_NUMBER);
    TestTrue(TEXT("Post-extraction gradient fallback ≤1%"), PostExtractGradientPct <= 1.0 + KINDA_SMALL_NUMBER);
    TestTrue(TEXT("Post-extraction motion fallback ≤0.1%"), PostExtractMotionPct <= 0.1 + KINDA_SMALL_NUMBER);
    AddInfo(FString::Printf(TEXT("[RidgeTerraneCache] PostExtract Coverage=%.3f%% GradientFallback=%.3f%% MotionFallback=%.3f%%"),
        PostExtractCoverage, PostExtractGradientPct, PostExtractMotionPct));

    // Identify a different continental plate for reattachment.
    int32 TargetPlateID = INDEX_NONE;
    for (const FTectonicPlate& Plate : Plates)
    {
        if (Plate.CrustType == ECrustType::Continental && Plate.PlateID != SourcePlateID)
        {
            TargetPlateID = Plate.PlateID;
            break;
        }
    }
    TestTrue(TEXT("Found target plate for terrane reattachment"), TargetPlateID != INDEX_NONE);
    if (TargetPlateID == INDEX_NONE)
    {
        return false;
    }

    TestTrue(TEXT("Terrane reattachment succeeded"), Service->ReattachTerrane(TerraneID, TargetPlateID));

    Service->BuildRenderVertexBoundaryCache();
    Service->MarkAllRidgeDirectionsDirty();
    Service->ForceRidgeRecomputeForTest();

    const double PostReattachCoverage = ComputeCoveragePercent(*Service);
    const double PostReattachGradientPct = ComputePercentage(static_cast<double>(Service->GetLastRidgeGradientFallbackCount()),
        static_cast<double>(FMath::Max(1, Service->GetLastRidgeDirtyVertexCount())));
    const double PostReattachMotionPct = ComputePercentage(static_cast<double>(Service->GetLastRidgeMotionFallbackCount()),
        static_cast<double>(FMath::Max(1, Service->GetLastRidgeDirtyVertexCount())));

    TestTrue(TEXT("Post-reattachment ridge tangent coverage ≥99%"), PostReattachCoverage >= 99.0 - KINDA_SMALL_NUMBER);
    TestTrue(TEXT("Post-reattachment gradient fallback ≤1%"), PostReattachGradientPct <= 1.0 + KINDA_SMALL_NUMBER);
    TestTrue(TEXT("Post-reattachment motion fallback ≤0.1%"), PostReattachMotionPct <= 0.1 + KINDA_SMALL_NUMBER);
    AddInfo(FString::Printf(TEXT("[RidgeTerraneCache] PostReattach Coverage=%.3f%% GradientFallback=%.3f%% MotionFallback=%.3f%%"),
        PostReattachCoverage, PostReattachGradientPct, PostReattachMotionPct));

    FString HeatmapAfterPath;
    TestTrue(TEXT("Exported ridge tangent heatmap (after terrane op)"),
        ExportRidgeTangentHeatmap(*Service, TEXT("after_reattach"), HeatmapAfterPath, *this));
    if (!HeatmapAfterPath.IsEmpty())
    {
        AddInfo(FString::Printf(TEXT("Ridge tangent heatmap (after): %s"), *HeatmapAfterPath));
    }

    Service->ResetSimulation();
    return !HasAnyErrors();
}
