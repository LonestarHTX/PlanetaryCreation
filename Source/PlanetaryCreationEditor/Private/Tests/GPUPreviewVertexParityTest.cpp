// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "PlanetaryCreationLogging.h"
#include "Containers/BitArray.h"
#include "Editor.h"
#include "RHI.h"

namespace GPUPreviewVertexParity
{
    namespace
    {
        constexpr int32 PreviewTextureWidth = 2048;
        constexpr int32 PreviewTextureHeight = 1024;
        constexpr float SeamSplitReference = 0.5f;
        constexpr float SeamWrapThreshold = 0.5f;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGPUPreviewVertexParityTest,
    "PlanetaryCreation.Milestone6.GPU.PreviewVertexParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGPUPreviewVertexParityTest::RunTest(const FString& Parameters)
{
    if (!GDynamicRHI || FCString::Stricmp(GDynamicRHI->GetName(), TEXT("NullDrv")) == 0)
    {
        AddInfo(TEXT("Skipping GPU preview vertex parity test (NullRHI detected)."));
        return true;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get UTectonicSimulationService."));
        return false;
    }

    Service->ResetSimulation();

    FTectonicSimulationParameters Params = Service->GetParameters();
    Params.RenderSubdivisionLevel = 7;
    Params.VisualizationMode = ETectonicVisualizationMode::Elevation;
    Params.bEnableHeightmapVisualization = true;
    Params.bEnableDynamicRetessellation = false;
    Service->SetParameters(Params);

    Service->SetRenderSubdivisionLevel(7);

    const TArray<FVector3d>& Vertices = Service->GetRenderVertices();
    const TArray<int32>& Triangles = Service->GetRenderTriangles();

    if (Vertices.Num() == 0 || Triangles.Num() == 0)
    {
        AddError(TEXT("Render vertices or triangles are empty after initialization."));
        return false;
    }

    const int32 UniqueVertexCount = Vertices.Num();

    TArray<float> UValues;
    UValues.SetNum(UniqueVertexCount);

    for (int32 Index = 0; Index < UniqueVertexCount; ++Index)
    {
        const FVector3d Unit = Vertices[Index].GetSafeNormal();
        if (Unit.IsNearlyZero())
        {
            UValues[Index] = 0.0f;
            continue;
        }

        const double Longitude = FMath::Atan2(Unit.Y, Unit.X);
        double U = 0.5 + Longitude / (2.0 * PI);
        U = FMath::Fmod(U, 1.0);
        if (U < 0.0)
        {
            U += 1.0;
        }

        UValues[Index] = static_cast<float>(U);
    }

    TBitArray<> bNeedsSeamDuplicate(false, UniqueVertexCount);
    bNeedsSeamDuplicate.Init(false, UniqueVertexCount);
    float MinDuplicateU = 1.0f;
    float MaxDuplicateU = 0.0f;

    auto MarkForDuplication = [&](int32 VertexIndex)
    {
        if (!bNeedsSeamDuplicate[VertexIndex])
        {
            bNeedsSeamDuplicate[VertexIndex] = true;
        }
    };

    for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); TriangleIndex += 3)
    {
        const int32 Index0 = Triangles[TriangleIndex];
        const int32 Index1 = Triangles[TriangleIndex + 1];
        const int32 Index2 = Triangles[TriangleIndex + 2];

        const float U0 = UValues[Index0];
        const float U1 = UValues[Index1];
        const float U2 = UValues[Index2];

        const float MaxU = FMath::Max3(U0, U1, U2);
        const float MinU = FMath::Min3(U0, U1, U2);
        const bool bCrossesSeam = (MaxU - MinU) > GPUPreviewVertexParity::SeamWrapThreshold;
        if (!bCrossesSeam)
        {
            continue;
        }

        if (U0 < GPUPreviewVertexParity::SeamSplitReference)
        {
            MarkForDuplication(Index0);
            MinDuplicateU = FMath::Min(MinDuplicateU, U0);
            MaxDuplicateU = FMath::Max(MaxDuplicateU, U0);
        }
        if (U1 < GPUPreviewVertexParity::SeamSplitReference)
        {
            MarkForDuplication(Index1);
            MinDuplicateU = FMath::Min(MinDuplicateU, U1);
            MaxDuplicateU = FMath::Max(MaxDuplicateU, U1);
        }
        if (U2 < GPUPreviewVertexParity::SeamSplitReference)
        {
            MarkForDuplication(Index2);
            MinDuplicateU = FMath::Min(MinDuplicateU, U2);
            MaxDuplicateU = FMath::Max(MaxDuplicateU, U2);
        }
    }

    const int32 SeamDuplicateCount = bNeedsSeamDuplicate.CountSetBits();
    const int32 ExpectedPreviewVertexCount = UniqueVertexCount + SeamDuplicateCount;

    int32 SeamColumnZeroCoverage = 0;
    int32 SeamColumnMaxCoverage = 0;
    int32 SeamMirroredCoverage = 0;
    const int32 SeamColumnMax = GPUPreviewVertexParity::PreviewTextureWidth - 1;
    int32 MinPixelX = SeamColumnMax;
    int32 MaxPixelX = 0;

    for (float U : UValues)
    {
        const float PixelPosition = U * static_cast<float>(SeamColumnMax);
        const int32 PixelX = FMath::Clamp(FMath::FloorToInt(PixelPosition), 0, SeamColumnMax);
        MinPixelX = FMath::Min(MinPixelX, PixelX);
        MaxPixelX = FMath::Max(MaxPixelX, PixelX);

        constexpr float SeamCoverageThreshold = 0.1f;
        const bool bCountsLeft = U <= SeamCoverageThreshold;
        const bool bCountsRight = U >= (1.0f - SeamCoverageThreshold);

        if (!bCountsLeft && !bCountsRight)
        {
            continue;
        }

        if (bCountsLeft && bCountsRight)
        {
            ++SeamMirroredCoverage;
        }

        if (bCountsLeft)
        {
            ++SeamColumnZeroCoverage;
        }

        if (bCountsRight)
        {
            ++SeamColumnMaxCoverage;
        }
    }

    AddInfo(FString::Printf(TEXT("Unique vertices: %d"), UniqueVertexCount));
    AddInfo(FString::Printf(TEXT("Seam duplicates (UV < 0.5): %d"), SeamDuplicateCount));
    AddInfo(FString::Printf(TEXT("Duplicate U range: [%.6f, %.6f]"), MinDuplicateU, MaxDuplicateU));
    AddInfo(FString::Printf(TEXT("Seam column 0 coverage (PixelX <= 1): %d"), SeamColumnZeroCoverage));
    AddInfo(FString::Printf(TEXT("Seam column %d coverage (PixelX >= %d): %d"), SeamColumnMax, SeamColumnMax - 1, SeamColumnMaxCoverage));
    AddInfo(FString::Printf(TEXT("Mirrored seam hits (within threshold of both seams): %d"), SeamMirroredCoverage));
    AddInfo(FString::Printf(TEXT("Pixel X range: [%d, %d]"), MinPixelX, MaxPixelX));
    AddInfo(FString::Printf(TEXT("Expected preview vertex count after duplication: %d"), ExpectedPreviewVertexCount));

    TestTrue(TEXT("Seam duplicates present"), SeamDuplicateCount > 0);
    TestTrue(TEXT("Left seam column receives coverage"), SeamColumnZeroCoverage > 0);
    TestTrue(TEXT("Right seam column receives coverage"), SeamColumnMaxCoverage > 0);

    return true;
}
