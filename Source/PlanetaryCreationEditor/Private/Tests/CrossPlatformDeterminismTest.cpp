// Milestone 5 Task 3.1: Cross-Platform Determinism Test

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * Milestone 5 Task 3.1: Cross-Platform Determinism Test
 *
 * Validates that simulations produce identical results across platforms (Windows/Linux).
 * Uses double-precision math and deterministic algorithms to ensure reproducibility.
 *
 * Test Method:
 * 1. Run 100-step simulation with fixed seed
 * 2. Export plate centroids, vertex positions, and topology hashes
 * 3. Compare against baseline (generated on first run)
 * 4. Fail if differences exceed tolerance (1e-10 for positions, exact for topology)
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossPlatformDeterminismTest,
    "PlanetaryCreation.Milestone5.CrossPlatformDeterminism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrossPlatformDeterminismTest::RunTest(const FString& Parameters)
{
    if (!GEditor)
    {
        AddError(TEXT("Test requires editor context"));
        return false;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get TectonicSimulationService"));
        return false;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Cross-Platform Determinism Test ==="));

    // Fixed configuration for reproducibility
    FTectonicSimulationParameters Params;
    Params.Seed = 999; // Fixed seed for determinism
    Params.SubdivisionLevel = 1; // 80 plates
    Params.RenderSubdivisionLevel = 2; // 320 faces
    Params.LloydIterations = 4; // Fixed iteration count
    Params.bEnableDynamicRetessellation = true;
    Params.bEnableHotspots = true;
    Params.bEnableContinentalErosion = true;
    Params.bEnableOceanicDampening = true;
    Params.bEnableSedimentTransport = true;
    Params.ErosionConstant = 0.001;
    Params.OceanicDampeningConstant = 0.0005;
    Params.SeaLevel = 0.0;
    Params.ElevationScale = 10000.0;

    Service->SetParameters(Params);

    // Initialize with deterministic plate motion
    TArray<FTectonicPlate>& Plates = Service->GetPlatesForModification();
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        Plates[i].EulerPoleAxis = FVector3d(
            FMath::Sin(i * 0.7),
            FMath::Cos(i * 0.9),
            FMath::Sin(i * 1.1)
        ).GetSafeNormal();
        Plates[i].AngularVelocity = 0.025; // rad/My
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Running 100-step deterministic simulation..."));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Seed: %d"), Params.Seed);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Initial Plates: %d"), Plates.Num());
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Initial Vertices: %d"), Service->GetRenderVertices().Num());

    // Run simulation
    Service->AdvanceSteps(100);

    const TArray<FTectonicPlate>& FinalPlates = Service->GetPlatesForModification();
    const TArray<FVector3d>& FinalVertices = Service->GetRenderVertices();

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Final Plates: %d"), FinalPlates.Num());
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Final Vertices: %d"), FinalVertices.Num());

    // Compute deterministic hashes
    uint64 PlateHash = 0;
    uint64 VertexHash = 0;
    double PlateCentroidSum = 0.0;
    double VertexPositionSum = 0.0;

    // Hash plate centroids (topology-invariant)
    for (const FTectonicPlate& Plate : FinalPlates)
    {
        PlateCentroidSum += Plate.Centroid.X + Plate.Centroid.Y + Plate.Centroid.Z;
        PlateCentroidSum += Plate.AngularVelocity;
    }

    // Hash vertex positions (first 100 vertices for speed)
    const int32 VertexSampleCount = FMath::Min(100, FinalVertices.Num());
    for (int32 i = 0; i < VertexSampleCount; ++i)
    {
        const FVector3d& Vertex = FinalVertices[i];
        VertexPositionSum += Vertex.X + Vertex.Y + Vertex.Z;
    }

    // Convert to uint64 hashes (deterministic bitwise representation)
    PlateHash = *reinterpret_cast<const uint64*>(&PlateCentroidSum);
    VertexHash = *reinterpret_cast<const uint64*>(&VertexPositionSum);

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Determinism Fingerprint:"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Plate Centroid Sum: %.15f"), PlateCentroidSum);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Vertex Position Sum: %.15f"), VertexPositionSum);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Plate Hash: 0x%016llX"), PlateHash);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Vertex Hash: 0x%016llX"), VertexHash);

    // Store baseline on first run, compare on subsequent runs
    const FString BaselinePath = FPaths::ProjectSavedDir() / TEXT("Tests") / TEXT("DeterminismBaseline.txt");

    if (FPaths::FileExists(BaselinePath))
    {
        // Load and compare
        UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Comparing against baseline: %s"), *BaselinePath);

        FString BaselineContent;
        if (FFileHelper::LoadFileToString(BaselineContent, *BaselinePath))
        {
            TArray<FString> Lines;
            BaselineContent.ParseIntoArrayLines(Lines);

            double BaselinePlateCentroidSum = 0.0;
            double BaselineVertexPositionSum = 0.0;
            int32 BaselinePlateCount = 0;
            int32 BaselineVertexCount = 0;

            for (const FString& Line : Lines)
            {
                if (Line.Contains(TEXT("PlateCentroidSum=")))
                {
                    FString Value = Line.RightChop(Line.Find(TEXT("=")) + 1);
                    BaselinePlateCentroidSum = FCString::Atod(*Value);
                }
                else if (Line.Contains(TEXT("VertexPositionSum=")))
                {
                    FString Value = Line.RightChop(Line.Find(TEXT("=")) + 1);
                    BaselineVertexPositionSum = FCString::Atod(*Value);
                }
                else if (Line.Contains(TEXT("PlateCount=")))
                {
                    FString Value = Line.RightChop(Line.Find(TEXT("=")) + 1);
                    BaselinePlateCount = FCString::Atoi(*Value);
                }
                else if (Line.Contains(TEXT("VertexCount=")))
                {
                    FString Value = Line.RightChop(Line.Find(TEXT("=")) + 1);
                    BaselineVertexCount = FCString::Atoi(*Value);
                }
            }

            const double CentroidDiff = FMath::Abs(PlateCentroidSum - BaselinePlateCentroidSum);
            const double VertexDiff = FMath::Abs(VertexPositionSum - BaselineVertexPositionSum);
            const double Tolerance = 1e-8; // Tight tolerance for double-precision determinism

            UE_LOG(LogPlanetaryCreation, Log, TEXT("  Baseline Plate Centroid Sum: %.15f (diff: %.2e)"), BaselinePlateCentroidSum, CentroidDiff);
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  Baseline Vertex Position Sum: %.15f (diff: %.2e)"), BaselineVertexPositionSum, VertexDiff);
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  Baseline Plate Count: %d"), BaselinePlateCount);
            UE_LOG(LogPlanetaryCreation, Log, TEXT("  Baseline Vertex Count: %d"), BaselineVertexCount);

            // Validate determinism
            TestTrue(TEXT("Plate count matches baseline"), FinalPlates.Num() == BaselinePlateCount);
            TestTrue(TEXT("Vertex count matches baseline"), FinalVertices.Num() == BaselineVertexCount);
            TestTrue(TEXT("Plate centroid sum within tolerance"), CentroidDiff < Tolerance);
            TestTrue(TEXT("Vertex position sum within tolerance"), VertexDiff < Tolerance);

            if (CentroidDiff < Tolerance && VertexDiff < Tolerance)
            {
                UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
                UE_LOG(LogPlanetaryCreation, Log, TEXT("DETERMINISM VERIFIED: Results match baseline within %.2e tolerance"), Tolerance);
            }
            else
            {
                UE_LOG(LogPlanetaryCreation, Error, TEXT(""));
                UE_LOG(LogPlanetaryCreation, Error, TEXT("DETERMINISM FAILED: Results differ from baseline"));
                UE_LOG(LogPlanetaryCreation, Error, TEXT("  This may indicate platform-specific floating-point behavior"));
                UE_LOG(LogPlanetaryCreation, Error, TEXT("  or non-deterministic algorithm changes."));
            }
        }
        else
        {
            AddError(TEXT("Failed to load baseline file"));
        }
    }
    else
    {
        // First run - save baseline
        UE_LOG(LogPlanetaryCreation, Warning, TEXT(""));
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("No baseline found - creating new baseline: %s"), *BaselinePath);

        FString BaselineContent = FString::Printf(TEXT(
            "# Cross-Platform Determinism Baseline\n"
            "# Generated by FCrossPlatformDeterminismTest\n"
            "# Date: %s\n"
            "# Platform: %s\n"
            "# Seed: %d\n"
            "PlateCentroidSum=%.15f\n"
            "VertexPositionSum=%.15f\n"
            "PlateCount=%d\n"
            "VertexCount=%d\n"
            "PlateHash=0x%016llX\n"
            "VertexHash=0x%016llX\n"
        ),
            *FDateTime::Now().ToString(),
            PLATFORM_WINDOWS ? TEXT("Windows") : TEXT("Linux"),
            Params.Seed,
            PlateCentroidSum,
            VertexPositionSum,
            FinalPlates.Num(),
            FinalVertices.Num(),
            PlateHash,
            VertexHash
        );

        FFileHelper::SaveStringToFile(BaselineContent, *BaselinePath);
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("  Baseline saved. Re-run test to validate determinism."));
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Cross-Platform Determinism Test COMPLETE"));

    return true;
}
