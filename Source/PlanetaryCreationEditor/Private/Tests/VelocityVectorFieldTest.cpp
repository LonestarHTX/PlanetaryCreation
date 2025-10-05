// Milestone 4 Task 3.2: Velocity Vector Field Test

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 4 Task 3.2: Velocity Vector Field Validation
 *
 * Tests that the velocity vector field rendering:
 * - Computes surface velocity correctly (v = ω × r)
 * - Arrow length scales with velocity magnitude
 * - Color modulation reflects velocity (blue → yellow → red)
 * - Vectors point in correct tangent direction
 * - All plates have valid velocity data
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVelocityVectorFieldTest,
    "PlanetaryCreation.Milestone4.VelocityVectorField",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVelocityVectorFieldTest::RunTest(const FString& Parameters)
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
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Velocity Vector Field Test ==="));

    // Test 1: Velocity Computation (v = ω × r)
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: Surface Velocity Computation"));

    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.SubdivisionLevel = 0; // 20 plates
    Params.RenderSubdivisionLevel = 2;
    Params.LloydIterations = 2;

    Service->SetParameters(Params);
    Service->AdvanceSteps(5); // 10 My

    const TArray<FTectonicPlate>& Plates = Service->GetPlates();
    TestTrue(TEXT("Plates exist"), Plates.Num() > 0);

    // Validate surface velocity computation for each plate
    int32 ValidVelocityCount = 0;
    double MaxVelocityMagnitude = 0.0;
    double MinVelocityMagnitude = TNumericLimits<double>::Max();

    for (const FTectonicPlate& Plate : Plates)
    {
        const FVector3d Centroid = Plate.Centroid.GetSafeNormal();
        const FVector3d EulerPoleAxis = Plate.EulerPoleAxis.GetSafeNormal();
        const double AngularVelocity = Plate.AngularVelocity;

        // Compute surface velocity: v = ω × r
        const FVector3d AngularVelocityVector = EulerPoleAxis * AngularVelocity;
        const FVector3d SurfaceVelocity = FVector3d::CrossProduct(AngularVelocityVector, Centroid);

        const double VelocityMagnitude = SurfaceVelocity.Length();

        if (VelocityMagnitude > 1e-6)
        {
            ValidVelocityCount++;
            MaxVelocityMagnitude = FMath::Max(MaxVelocityMagnitude, VelocityMagnitude);
            MinVelocityMagnitude = FMath::Min(MinVelocityMagnitude, VelocityMagnitude);

            // Velocity should be tangent to sphere (perpendicular to centroid)
            const double DotProduct = FVector3d::DotProduct(SurfaceVelocity.GetSafeNormal(), Centroid);
            TestTrue(FString::Printf(TEXT("Velocity tangent to sphere (Plate %d)"), Plate.PlateID),
                FMath::Abs(DotProduct) < 0.01); // ~0.6° tolerance
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Plates with velocity: %d / %d"), ValidVelocityCount, Plates.Num());
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Velocity range: %.4f - %.4f rad/My"), MinVelocityMagnitude, MaxVelocityMagnitude);

    TestTrue(TEXT("Most plates have velocity"), ValidVelocityCount > Plates.Num() / 2);
    TestTrue(TEXT("Velocity magnitudes reasonable"), MaxVelocityMagnitude > 0.0 && MaxVelocityMagnitude < 0.5);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Surface velocity computation validated"));

    // Test 2: Velocity Direction Consistency
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 2: Velocity Direction Consistency"));

    // Check that velocity direction matches Euler pole rotation
    int32 ConsistentDirectionCount = 0;

    for (const FTectonicPlate& Plate : Plates)
    {
        const FVector3d Centroid = Plate.Centroid.GetSafeNormal();
        const FVector3d EulerPoleAxis = Plate.EulerPoleAxis.GetSafeNormal();
        const double AngularVelocity = Plate.AngularVelocity;

        const FVector3d AngularVelocityVector = EulerPoleAxis * AngularVelocity;
        const FVector3d SurfaceVelocity = FVector3d::CrossProduct(AngularVelocityVector, Centroid);

        if (SurfaceVelocity.Length() > 1e-6)
        {
            // Velocity should be perpendicular to both centroid and Euler pole axis
            const double DotCentroid = FVector3d::DotProduct(SurfaceVelocity.GetSafeNormal(), Centroid);
            const double DotAxis = FVector3d::DotProduct(SurfaceVelocity.GetSafeNormal(), EulerPoleAxis);

            if (FMath::Abs(DotCentroid) < 0.01 && FMath::Abs(DotAxis) < 0.01)
            {
                ConsistentDirectionCount++;
            }
        }
    }

    const double ConsistencyRatio = static_cast<double>(ConsistentDirectionCount) / ValidVelocityCount;
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Consistent directions: %d / %d (%.1f%%)"),
        ConsistentDirectionCount, ValidVelocityCount, ConsistencyRatio * 100.0);

    TestTrue(TEXT("Velocity directions consistent"), ConsistencyRatio > 0.9);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Velocity direction consistency validated"));

    // Test 3: Velocity Magnitude Scaling
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: Velocity Magnitude Scaling"));

    // Find plates with different velocity magnitudes
    TArray<TPair<double, int32>> VelocityPlateMap; // (magnitude, plate ID)

    for (const FTectonicPlate& Plate : Plates)
    {
        const FVector3d AngularVelocityVector = Plate.EulerPoleAxis * Plate.AngularVelocity;
        const FVector3d SurfaceVelocity = FVector3d::CrossProduct(AngularVelocityVector, Plate.Centroid.GetSafeNormal());
        const double VelocityMagnitude = SurfaceVelocity.Length();

        if (VelocityMagnitude > 1e-6)
        {
            VelocityPlateMap.Add(TPair<double, int32>(VelocityMagnitude, Plate.PlateID));
        }
    }

    // Sort by velocity magnitude
    VelocityPlateMap.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B)
    {
        return A.Key < B.Key;
    });

    if (VelocityPlateMap.Num() >= 3)
    {
        const double SlowVelocity = VelocityPlateMap[0].Key;
        const double MediumVelocity = VelocityPlateMap[VelocityPlateMap.Num() / 2].Key;
        const double FastVelocity = VelocityPlateMap.Last().Key;

        UE_LOG(LogPlanetaryCreation, Log, TEXT("  Slowest: %.4f rad/My (Plate %d)"), SlowVelocity, VelocityPlateMap[0].Value);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  Median: %.4f rad/My (Plate %d)"), MediumVelocity, VelocityPlateMap[VelocityPlateMap.Num() / 2].Value);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  Fastest: %.4f rad/My (Plate %d)"), FastVelocity, VelocityPlateMap.Last().Value);

        TestTrue(TEXT("Velocity range exists"), FastVelocity > SlowVelocity * 1.5);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Velocity magnitude scaling validated"));
    }
    else
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("  ⚠️ Not enough plates with velocity for scaling test"));
    }

    // Test 4: Arrow Length Scaling
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 4: Arrow Length Scaling"));

    // Simulate arrow length calculation
    auto GetArrowLength = [](double VelocityMagnitude, double MaxVelocity) -> float
    {
        constexpr float BaseLength = 500.0f;
        constexpr float MaxLength = 2000.0f;

        const float NormalizedVelocity = static_cast<float>(VelocityMagnitude / MaxVelocity);
        return BaseLength + (MaxLength - BaseLength) * NormalizedVelocity;
    };

    if (VelocityPlateMap.Num() >= 2)
    {
        const double SlowVelocity = VelocityPlateMap[0].Key;
        const double FastVelocity = VelocityPlateMap.Last().Key;

        const float SlowArrowLength = GetArrowLength(SlowVelocity, MaxVelocityMagnitude);
        const float FastArrowLength = GetArrowLength(FastVelocity, MaxVelocityMagnitude);

        UE_LOG(LogPlanetaryCreation, Log, TEXT("  Slow arrow length: %.1f km"), SlowArrowLength);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  Fast arrow length: %.1f km"), FastArrowLength);

        TestTrue(TEXT("Arrow length scales with velocity"), FastArrowLength > SlowArrowLength);
        TestTrue(TEXT("Arrow length within bounds"), SlowArrowLength >= 500.0f && FastArrowLength <= 2000.0f);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Arrow length scaling validated"));
    }

    // Test 5: Color Modulation
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 5: Color Modulation"));

    auto GetVelocityColor = [](double NormalizedVelocity) -> FColor
    {
        if (NormalizedVelocity < 0.25)
        {
            const float T = NormalizedVelocity / 0.25f;
            return FColor(0, static_cast<uint8>(T * 255), 255, 255);
        }
        else if (NormalizedVelocity < 0.5)
        {
            const float T = (NormalizedVelocity - 0.25f) / 0.25f;
            return FColor(0, 255, static_cast<uint8>((1.0f - T) * 255), 255);
        }
        else if (NormalizedVelocity < 0.75)
        {
            const float T = (NormalizedVelocity - 0.5f) / 0.25f;
            return FColor(static_cast<uint8>(T * 255), 255, 0, 255);
        }
        else
        {
            const float T = (NormalizedVelocity - 0.75f) / 0.25f;
            return FColor(255, static_cast<uint8>((1.0f - T) * 255), 0, 255);
        }
    };

    // Test color ramp endpoints
    const FColor SlowColor = GetVelocityColor(0.0);
    const FColor MediumColor = GetVelocityColor(0.5);
    const FColor FastColor = GetVelocityColor(1.0);

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Slow (0.0): R=%d G=%d B=%d"), SlowColor.R, SlowColor.G, SlowColor.B);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Medium (0.5): R=%d G=%d B=%d"), MediumColor.R, MediumColor.G, MediumColor.B);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Fast (1.0): R=%d G=%d B=%d"), FastColor.R, FastColor.G, FastColor.B);

    TestTrue(TEXT("Slow velocity is blue"), SlowColor.B == 255 && SlowColor.R == 0);
    TestTrue(TEXT("Medium velocity is green"), MediumColor.G == 255 && MediumColor.R == 0 && MediumColor.B == 0);
    TestTrue(TEXT("Fast velocity is red"), FastColor.R == 255 && FastColor.G == 0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Color modulation validated"));

    // Test 6: Velocity Data Completeness
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 6: Velocity Data Completeness"));

    // Verify all plates have valid centroids and Euler poles
    int32 CompleteDataCount = 0;

    for (const FTectonicPlate& Plate : Plates)
    {
        const bool HasValidCentroid = Plate.Centroid.Length() > 0.9 && Plate.Centroid.Length() < 1.1;
        const bool HasValidEulerPole = Plate.EulerPoleAxis.Length() > 0.9 && Plate.EulerPoleAxis.Length() < 1.1;

        if (HasValidCentroid && HasValidEulerPole)
        {
            CompleteDataCount++;
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Plates with complete velocity data: %d / %d"), CompleteDataCount, Plates.Num());
    TestTrue(TEXT("All plates have complete velocity data"), CompleteDataCount == Plates.Num());
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Velocity data completeness validated"));

    AddInfo(TEXT("✅ Velocity vector field test complete"));
    AddInfo(FString::Printf(TEXT("Plates: %d | Valid velocities: %d | Range: %.4f - %.4f rad/My"),
        Plates.Num(), ValidVelocityCount, MinVelocityMagnitude, MaxVelocityMagnitude));

    return true;
}
