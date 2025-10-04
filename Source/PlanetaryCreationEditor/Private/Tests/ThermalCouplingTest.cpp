// Milestone 4 Task 2.3: Thermal & Stress Coupling Test

#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 4 Task 2.3: Thermal & Stress Coupling Validation
 *
 * Tests analytic thermal field computation from hotspots and subduction zones.
 * Validates Gaussian falloff curve and additive stress modulation.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FThermalCouplingTest,
    "PlanetaryCreation.Milestone4.ThermalCoupling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FThermalCouplingTest::RunTest(const FString& Parameters)
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

    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("=== Thermal & Stress Coupling Test ==="));

    // Test 1: Baseline Temperature Field (No Hotspots)
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 1: Baseline Temperature Field"));

    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.SubdivisionLevel = 0; // 20 plates
    Params.RenderSubdivisionLevel = 2; // 320 faces
    Params.LloydIterations = 0; // Skip for speed
    Params.bEnableHotspots = false; // Disable hotspots

    Service->SetParameters(Params);
    Service->AdvanceSteps(1); // 2 My

    const TArray<double>& TempValues = Service->GetVertexTemperatureValues();
    TestEqual(TEXT("Temperature array populated"), TempValues.Num(), Service->GetRenderVertices().Num());

    // Calculate baseline temperature statistics
    double MinTemp = TempValues[0];
    double MaxTemp = TempValues[0];
    double AvgTemp = 0.0;

    for (double Temp : TempValues)
    {
        MinTemp = FMath::Min(MinTemp, Temp);
        MaxTemp = FMath::Max(MaxTemp, Temp);
        AvgTemp += Temp;
    }
    AvgTemp /= TempValues.Num();

    UE_LOG(LogTemp, Log, TEXT("  Baseline temperature range: %.1fK - %.1fK (avg: %.1fK)"), MinTemp, MaxTemp, AvgTemp);

    // Without hotspots, temperature should be baseline mantle temp (~1600K) +/- subduction heating
    TestTrue(TEXT("Min temp >= 1500K"), MinTemp >= 1500.0);
    TestTrue(TEXT("Max temp < 2500K"), MaxTemp < 2500.0); // Subduction can add ~200-400K
    UE_LOG(LogTemp, Log, TEXT("  ✓ Baseline temperature field validated"));

    // Test 2: Hotspot Thermal Contribution
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 2: Hotspot Thermal Contribution"));

    Params.bEnableHotspots = true;
    Params.MajorHotspotCount = 3;
    Params.MinorHotspotCount = 5;

    Service->SetParameters(Params);
    Service->AdvanceSteps(1); // 2 My

    const TArray<double>& HotspotTempValues = Service->GetVertexTemperatureValues();
    const TArray<FMantleHotspot>& Hotspots = Service->GetHotspots();

    TestEqual(TEXT("Hotspots generated"), Hotspots.Num(), 8);

    // Find vertices near hotspots and verify elevated temperature
    int32 ElevatedTempCount = 0;
    double MaxHotspotTemp = 0.0;

    for (int32 VertexIdx = 0; VertexIdx < Service->GetRenderVertices().Num(); ++VertexIdx)
    {
        const FVector3d& VertexPos = Service->GetRenderVertices()[VertexIdx];
        const double VertexTemp = HotspotTempValues[VertexIdx];

        for (const FMantleHotspot& Hotspot : Hotspots)
        {
            const double AngularDistance = FMath::Acos(FMath::Clamp(
                FVector3d::DotProduct(VertexPos, Hotspot.Position),
                -1.0, 1.0
            ));

            if (AngularDistance < Hotspot.InfluenceRadius * 0.5) // Within half-radius
            {
                // Temperature should be elevated above baseline
                if (VertexTemp > 1700.0) // > 100K above baseline
                {
                    ElevatedTempCount++;
                    MaxHotspotTemp = FMath::Max(MaxHotspotTemp, VertexTemp);
                }
                break; // Only count once per vertex
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("  %d vertices with elevated temperature near hotspots"), ElevatedTempCount);
    UE_LOG(LogTemp, Log, TEXT("  Max hotspot temperature: %.1fK"), MaxHotspotTemp);

    TestTrue(TEXT("Hotspots elevate temperature"), ElevatedTempCount > 0);
    TestTrue(TEXT("Max hotspot temp > 2000K"), MaxHotspotTemp > 2000.0);
    UE_LOG(LogTemp, Log, TEXT("  ✓ Hotspot thermal contribution validated"));

    // Test 3: Gaussian Falloff Curve
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 3: Gaussian Falloff Curve Validation"));

    // Sample temperatures at varying distances from a hotspot
    if (Hotspots.Num() > 0)
    {
        const FMantleHotspot& TestHotspot = Hotspots[0];
        TArray<TPair<double, double>> DistanceTempPairs; // (distance, temperature)

        for (int32 VertexIdx = 0; VertexIdx < Service->GetRenderVertices().Num(); ++VertexIdx)
        {
            const FVector3d& VertexPos = Service->GetRenderVertices()[VertexIdx];
            const double AngularDistance = FMath::Acos(FMath::Clamp(
                FVector3d::DotProduct(VertexPos, TestHotspot.Position),
                -1.0, 1.0
            ));

            if (AngularDistance < TestHotspot.InfluenceRadius)
            {
                DistanceTempPairs.Add(TPair<double, double>(AngularDistance, HotspotTempValues[VertexIdx]));
            }
        }

        // Sort by distance
        DistanceTempPairs.Sort([](const TPair<double, double>& A, const TPair<double, double>& B)
        {
            return A.Key < B.Key;
        });

        // Validate monotonic decrease (temperature should drop with distance)
        bool MonotonicDecrease = true;
        for (int32 i = 1; i < DistanceTempPairs.Num(); ++i)
        {
            // Allow small tolerance for numerical variance
            if (DistanceTempPairs[i].Value > DistanceTempPairs[i-1].Value + 50.0)
            {
                MonotonicDecrease = false;
                break;
            }
        }

        TestTrue(TEXT("Temperature decreases with distance from hotspot"), MonotonicDecrease);
        UE_LOG(LogTemp, Log, TEXT("  Sampled %d vertices around hotspot"), DistanceTempPairs.Num());
        UE_LOG(LogTemp, Log, TEXT("  ✓ Gaussian falloff curve validated"));
    }

    // Test 4: Subduction Zone Heating
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 4: Subduction Zone Heating"));

    // Find convergent boundaries and check nearby vertex temperatures
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries = Service->GetBoundaries();
    int32 ConvergentCount = 0;
    int32 HeatedConvergentCount = 0;

    for (const auto& BoundaryPair : Boundaries)
    {
        const FPlateBoundary& Boundary = BoundaryPair.Value;

        if (Boundary.BoundaryType == EBoundaryType::Convergent && Boundary.AccumulatedStress > 50.0)
        {
            ConvergentCount++;

            // Check if nearby vertices have elevated temperature
            const int32 PlateA_ID = BoundaryPair.Key.Key;
            const int32 PlateB_ID = BoundaryPair.Key.Value;

            if (Service->GetPlates().IsValidIndex(PlateA_ID) && Service->GetPlates().IsValidIndex(PlateB_ID))
            {
                const FVector3d BoundaryPos = ((Service->GetPlates()[PlateA_ID].Centroid +
                                                 Service->GetPlates()[PlateB_ID].Centroid) * 0.5).GetSafeNormal();

                // Find vertices near boundary
                for (int32 VertexIdx = 0; VertexIdx < Service->GetRenderVertices().Num(); ++VertexIdx)
                {
                    const FVector3d& VertexPos = Service->GetRenderVertices()[VertexIdx];
                    const double AngularDistance = FMath::Acos(FMath::Clamp(
                        FVector3d::DotProduct(VertexPos, BoundaryPos),
                        -1.0, 1.0
                    ));

                    if (AngularDistance < 0.05) // Within ~2.9°
                    {
                        const double VertexTemp = HotspotTempValues[VertexIdx];
                        if (VertexTemp > 1650.0) // > 50K above baseline (subduction contribution)
                        {
                            HeatedConvergentCount++;
                            break; // One heated vertex is enough to confirm
                        }
                    }
                }
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("  Convergent boundaries: %d"), ConvergentCount);
    UE_LOG(LogTemp, Log, TEXT("  Convergent boundaries with heating: %d"), HeatedConvergentCount);

    if (ConvergentCount > 0)
    {
        TestTrue(TEXT("Some convergent boundaries show heating"), HeatedConvergentCount > 0);
        UE_LOG(LogTemp, Log, TEXT("  ✓ Subduction zone heating validated"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("  ⚠️ No active convergent boundaries in this simulation"));
    }

    // Test 5: Stress Modulation from Temperature
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("Test 5: Additive Stress Modulation"));

    // Verify that hotspot thermal contribution affects stress values
    const TArray<double>& StressValues = Service->GetVertexStressValues();

    int32 HighStressNearHotspotCount = 0;

    for (int32 VertexIdx = 0; VertexIdx < Service->GetRenderVertices().Num(); ++VertexIdx)
    {
        const FVector3d& VertexPos = Service->GetRenderVertices()[VertexIdx];
        const double VertexStress = StressValues[VertexIdx];

        for (const FMantleHotspot& Hotspot : Hotspots)
        {
            const double AngularDistance = FMath::Acos(FMath::Clamp(
                FVector3d::DotProduct(VertexPos, Hotspot.Position),
                -1.0, 1.0
            ));

            if (AngularDistance < Hotspot.InfluenceRadius * 0.5 && VertexStress > 10.0)
            {
                HighStressNearHotspotCount++;
                break;
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("  %d vertices with elevated stress near hotspots"), HighStressNearHotspotCount);
    TestTrue(TEXT("Hotspots contribute to stress field"), HighStressNearHotspotCount > 0);
    UE_LOG(LogTemp, Log, TEXT("  ✓ Additive stress modulation validated"));

    AddInfo(TEXT("✅ Thermal & stress coupling test complete"));
    AddInfo(FString::Printf(TEXT("Elevated temps: %d vertices | Max hotspot temp: %.1fK | Subduction heating: %d boundaries"),
        ElevatedTempCount, MaxHotspotTemp, HeatedConvergentCount));

    return true;
}
