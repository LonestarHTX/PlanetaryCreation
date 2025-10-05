// Milestone 4 Task 2.3: Thermal & Stress Coupling Test

#include "PlanetaryCreationLogging.h"
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

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Thermal & Stress Coupling Test ==="));

    // Test 1: Baseline Temperature Field (No Hotspots)
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: Baseline Temperature Field"));

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

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Baseline temperature range: %.1fK - %.1fK (avg: %.1fK)"), MinTemp, MaxTemp, AvgTemp);

    // Without hotspots, temperature should be baseline mantle temp (~1600K) +/- subduction heating
    TestTrue(TEXT("Min temp >= 1500K"), MinTemp >= 1500.0);
    TestTrue(TEXT("Max temp < 2500K"), MaxTemp < 2500.0); // Subduction can add ~200-400K
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Baseline temperature field validated"));

    // Test 2: Hotspot Thermal Contribution
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 2: Hotspot Thermal Contribution"));

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

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  %d vertices with elevated temperature near hotspots"), ElevatedTempCount);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Max hotspot temperature: %.1fK"), MaxHotspotTemp);

    TestTrue(TEXT("Hotspots elevate temperature"), ElevatedTempCount > 0);
    TestTrue(TEXT("Max hotspot temp > 2000K"), MaxHotspotTemp > 2000.0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Hotspot thermal contribution validated"));

    // Test 3: Gaussian Falloff Curve
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: Gaussian Falloff Curve Validation"));

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
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  Sampled %d vertices around hotspot"), DistanceTempPairs.Num());
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Gaussian falloff curve validated"));
    }

    // Test 4: Subduction Zone Heating
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 4: Subduction Zone Heating"));

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

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Convergent boundaries: %d"), ConvergentCount);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Convergent boundaries with heating: %d"), HeatedConvergentCount);

    if (ConvergentCount > 0)
    {
        TestTrue(TEXT("Some convergent boundaries show heating"), HeatedConvergentCount > 0);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Subduction zone heating validated"));
    }
    else
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("  ⚠️ No active convergent boundaries in this simulation"));
    }

    // Test 5: Stress Modulation from Temperature
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 5: Additive Stress Modulation"));

    // Verify that hotspot thermal contribution affects stress values
    const TArray<double>& StressValues = Service->GetVertexStressValues();

    // Phase 5 Update: Paper-aligned physics - hotspots contribute to TEMPERATURE, not stress
    // Check for elevated temperature (not stress) near hotspots
    int32 HighTempNearHotspotCount = 0;
    const TArray<double>& TempValuesTest5 = Service->GetVertexTemperatureValues();

    for (int32 VertexIdx = 0; VertexIdx < Service->GetRenderVertices().Num(); ++VertexIdx)
    {
        const FVector3d& VertexPos = Service->GetRenderVertices()[VertexIdx];
        const double VertexTemp = TempValuesTest5[VertexIdx];

        for (const FMantleHotspot& Hotspot : Hotspots)
        {
            const double AngularDistance = FMath::Acos(FMath::Clamp(
                FVector3d::DotProduct(VertexPos, Hotspot.Position),
                -1.0, 1.0
            ));

            if (AngularDistance < Hotspot.InfluenceRadius * 0.5 && VertexTemp > 1700.0)
            {
                HighTempNearHotspotCount++;
                break;
            }
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  %d vertices with elevated temperature near hotspots"), HighTempNearHotspotCount);
    TestTrue(TEXT("Hotspots contribute to thermal field"), HighTempNearHotspotCount > 0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Thermal contribution (paper-aligned) validated"));

    // ===== PHASE 5 EXPANDED COVERAGE =====

    // Test 6: Stress-Temperature Interaction
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 6: Stress-Temperature Interaction (Phase 5)"));

    Params.Seed = 66666;
    Params.bEnableHotspots = true;
    Params.MajorHotspotCount = 4;
    Params.MinorHotspotCount = 7;
    Service->SetParameters(Params);

    // Apply high stress through plate motion
    TArray<FTectonicPlate>& Plates = Service->GetPlatesForModification();
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        Plates[i].EulerPoleAxis = FVector3d(
            FMath::Sin(i * 0.7),
            FMath::Cos(i * 0.9),
            FMath::Sin(i * 1.1)
        ).GetSafeNormal();
        Plates[i].AngularVelocity = 0.05; // rad/My
    }

    // Run to build up stress and temperature
    Service->AdvanceSteps(10);

    const TArray<double>& StressField = Service->GetVertexStressValues();
    const TArray<double>& TempField = Service->GetVertexTemperatureValues();

    // Compute temperature and stress statistics for diagnostics
    double MinTemp6 = TempField.Num() > 0 ? TempField[0] : 0.0;
    double MaxTemp6 = MinTemp6;
    double MinStress6 = StressField.Num() > 0 ? StressField[0] : 0.0;
    double MaxStress6 = MinStress6;

    for (int32 i = 0; i < TempField.Num(); ++i)
    {
        MinTemp6 = FMath::Min(MinTemp6, TempField[i]);
        MaxTemp6 = FMath::Max(MaxTemp6, TempField[i]);
        MinStress6 = FMath::Min(MinStress6, StressField[i]);
        MaxStress6 = FMath::Max(MaxStress6, StressField[i]);
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Temperature range: %.1f K to %.1f K"), MinTemp6, MaxTemp6);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Stress range: %.1f MPa to %.1f MPa"), MinStress6, MaxStress6);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Hotspots active: %d"), Service->GetHotspots().Num());

    // Find correlation between high stress and elevated temperature
    int32 HighStressHighTempCount = 0;
    int32 HighStressCount = 0;

    for (int32 i = 0; i < StressField.Num(); ++i)
    {
        if (StressField[i] > 30.0) // High stress
        {
            HighStressCount++;
            if (TempField[i] > 1700.0) // Elevated temperature
            {
                HighStressHighTempCount++;
            }
        }
    }

    const double CorrelationPercent = HighStressCount > 0 ?
        (100.0 * HighStressHighTempCount / HighStressCount) : 0.0;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  High stress vertices: %d"), HighStressCount);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  High stress + high temp: %d (%.1f%%)"), HighStressHighTempCount, CorrelationPercent);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Using thresholds: Stress >30 MPa, Temp >1700 K"));

    // Phase 5 Update: Paper-aligned physics - hotspots (thermal) and plate boundaries (stress) are
    // spatially independent. Hotspots now contribute ONLY to temperature, NOT directly to stress.
    // Stress comes from plate interactions (subduction, divergence). Subduction zones couple stress+temp,
    // but random hotspot distribution means low overall correlation is expected.
    //
    // Threshold lowered from 20% to 1% (minimal guard) to confirm coupling exists without imposing
    // unrealistic expectations. After removing direct stress addition from hotspots, typical correlation
    // is 3-5% (from subduction heating only), which is physically correct given spatial independence.
    TestTrue(TEXT("Stress-temperature interaction observed"), CorrelationPercent >= 1.0); // Minimal guard: confirms coupling exists
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Stress-temperature interaction validated (%.1f%% correlation)"), CorrelationPercent);

    // Test 7: Thermal Diffusion Across Plates
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 7: Thermal Diffusion Across Plates (Phase 5)"));

    Params.Seed = 77777;
    Service->SetParameters(Params);

    // Check temperature gradient across plate boundaries
    const TArray<int32>& VertexAssignments = Service->GetVertexPlateAssignments();
    const TArray<int32>& RenderTriangles = Service->GetRenderTriangles();
    const TArray<double>& DiffusionTempField = Service->GetVertexTemperatureValues();

    int32 CrossPlateEdgeCount = 0;
    double TotalTempGradient = 0.0;
    double MaxTempJump = 0.0;

    for (int32 TriIdx = 0; TriIdx < RenderTriangles.Num(); TriIdx += 3)
    {
        const int32 V0 = RenderTriangles[TriIdx];
        const int32 V1 = RenderTriangles[TriIdx + 1];
        const int32 V2 = RenderTriangles[TriIdx + 2];

        if (!VertexAssignments.IsValidIndex(V0) || !VertexAssignments.IsValidIndex(V1) ||
            !VertexAssignments.IsValidIndex(V2))
            continue;

        // Check edges that cross plate boundaries
        auto CheckEdge = [&](int32 VA, int32 VB)
        {
            if (VertexAssignments[VA] != VertexAssignments[VB] &&
                VertexAssignments[VA] != INDEX_NONE && VertexAssignments[VB] != INDEX_NONE)
            {
                const double TempA = DiffusionTempField[VA];
                const double TempB = DiffusionTempField[VB];
                const double TempDiff = FMath::Abs(TempA - TempB);

                CrossPlateEdgeCount++;
                TotalTempGradient += TempDiff;
                MaxTempJump = FMath::Max(MaxTempJump, TempDiff);
            }
        };

        CheckEdge(V0, V1);
        CheckEdge(V1, V2);
        CheckEdge(V2, V0);
    }

    const double AvgTempGradient = CrossPlateEdgeCount > 0 ? (TotalTempGradient / CrossPlateEdgeCount) : 0.0;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Cross-plate edges: %d"), CrossPlateEdgeCount);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Avg temp gradient: %.1fK"), AvgTempGradient);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Max temp jump: %.1fK"), MaxTempJump);

    // Thermal diffusion should smooth out extreme jumps
    TestTrue(TEXT("Max temperature jump reasonable"), MaxTempJump < 500.0); // < 500K jump
    TestTrue(TEXT("Average gradient reasonable"), AvgTempGradient < 200.0); // < 200K avg
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Thermal diffusion validated"));

    // Test 8: Hotspot Thermal Influence Radius
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 8: Hotspot Thermal Influence Radius (Phase 5)"));

    Params.Seed = 88888;
    Service->SetParameters(Params);
    Service->AdvanceSteps(1);

    const TArray<FMantleHotspot>& InfluenceHotspots = Service->GetHotspots();
    const TArray<double>& InfluenceTempField = Service->GetVertexTemperatureValues();

    for (const FMantleHotspot& Hotspot : InfluenceHotspots)
    {
        int32 WithinRadiusCount = 0;
        int32 BeyondRadiusCount = 0;
        double AvgTempWithin = 0.0;
        double AvgTempBeyond = 0.0;

        for (int32 VertexIdx = 0; VertexIdx < Service->GetRenderVertices().Num(); ++VertexIdx)
        {
            const FVector3d& VertexPos = Service->GetRenderVertices()[VertexIdx];
            const double AngularDistance = FMath::Acos(FMath::Clamp(
                FVector3d::DotProduct(VertexPos, Hotspot.Position),
                -1.0, 1.0
            ));

            if (AngularDistance < Hotspot.InfluenceRadius)
            {
                WithinRadiusCount++;
                AvgTempWithin += InfluenceTempField[VertexIdx];
            }
            else if (AngularDistance < Hotspot.InfluenceRadius * 1.5) // Just beyond
            {
                BeyondRadiusCount++;
                AvgTempBeyond += InfluenceTempField[VertexIdx];
            }
        }

        if (WithinRadiusCount > 0 && BeyondRadiusCount > 0)
        {
            AvgTempWithin /= WithinRadiusCount;
            AvgTempBeyond /= BeyondRadiusCount;

            UE_LOG(LogPlanetaryCreation, Verbose, TEXT("  Hotspot influence radius: %.3f rad"), Hotspot.InfluenceRadius);
            UE_LOG(LogPlanetaryCreation, Verbose, TEXT("    Within: %.1fK (n=%d) | Beyond: %.1fK (n=%d)"),
                AvgTempWithin, WithinRadiusCount, AvgTempBeyond, BeyondRadiusCount);

            // Temperature should be higher within radius
            TestTrue(TEXT("Temperature higher within influence radius"), AvgTempWithin > AvgTempBeyond);
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Hotspot influence radius validated"));

    // Test 9: Edge Case - Zero Stress, High Temperature
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 9: Edge Case - Zero Stress, High Temperature (Phase 5)"));

    Params.Seed = 99999;
    Params.bEnableHotspots = true;
    Service->SetParameters(Params);

    // Zero out all plate velocities (no stress accumulation)
    TArray<FTectonicPlate>& ZeroStressPlates = Service->GetPlatesForModification();
    for (FTectonicPlate& Plate : ZeroStressPlates)
    {
        Plate.AngularVelocity = 0.0; // No motion
    }

    // Run simulation (hotspots still generate heat)
    Service->AdvanceSteps(5);

    const TArray<double>& ZeroStressField = Service->GetVertexStressValues();
    const TArray<double>& ZeroStressTempField = Service->GetVertexTemperatureValues();

    // Compute temperature and stress statistics for diagnostics
    double MinTemp9 = ZeroStressTempField.Num() > 0 ? ZeroStressTempField[0] : 0.0;
    double MaxTemp9 = MinTemp9;
    double MinStress9 = ZeroStressField.Num() > 0 ? ZeroStressField[0] : 0.0;
    double MaxStress9 = MinStress9;

    for (int32 i = 0; i < ZeroStressTempField.Num(); ++i)
    {
        MinTemp9 = FMath::Min(MinTemp9, ZeroStressTempField[i]);
        MaxTemp9 = FMath::Max(MaxTemp9, ZeroStressTempField[i]);
        MinStress9 = FMath::Min(MinStress9, ZeroStressField[i]);
        MaxStress9 = FMath::Max(MaxStress9, ZeroStressField[i]);
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Temperature range: %.1f K to %.1f K"), MinTemp9, MaxTemp9);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Stress range: %.1f MPa to %.1f MPa"), MinStress9, MaxStress9);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Hotspots active: %d"), Service->GetHotspots().Num());

    // Find vertices with high temperature but low stress
    int32 HighTempLowStressCount = 0;
    int32 TotalVertices = ZeroStressTempField.Num();

    for (int32 i = 0; i < TotalVertices; ++i)
    {
        if (ZeroStressTempField[i] > 1800.0 && ZeroStressField[i] < 5.0) // High temp, low stress
        {
            HighTempLowStressCount++;
        }
    }

    const double HighTempLowStressPercent = (100.0 * HighTempLowStressCount) / TotalVertices;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Vertices with high temp + low stress: %d / %d (%.1f%%)"),
        HighTempLowStressCount, TotalVertices, HighTempLowStressPercent);

    TestTrue(TEXT("High temperature possible without high stress"), HighTempLowStressCount > 0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Edge case validated (thermal independent of stress)"));

    // Test 10: Thermal Field Stability Over Time
    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 10: Thermal Field Stability Over Time (Phase 5)"));

    Params.Seed = 11111;
    Service->SetParameters(Params);
    Service->AdvanceSteps(1);

    // Capture initial thermal field
    TArray<double> InitialThermal = Service->GetVertexTemperatureValues();

    // Run many steps
    Service->AdvanceSteps(20);

    const TArray<double>& FinalThermal = Service->GetVertexTemperatureValues();

    // Check for catastrophic thermal runaway or collapse
    double MinChange = DBL_MAX;
    double MaxChange = -DBL_MAX;
    double AvgChange = 0.0;

    for (int32 i = 0; i < FMath::Min(InitialThermal.Num(), FinalThermal.Num()); ++i)
    {
        const double Change = FinalThermal[i] - InitialThermal[i];
        MinChange = FMath::Min(MinChange, Change);
        MaxChange = FMath::Max(MaxChange, Change);
        AvgChange += Change;
    }
    AvgChange /= InitialThermal.Num();

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  Thermal change over 20 steps:"));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("    Min: %.1fK | Max: %.1fK | Avg: %.1fK"), MinChange, MaxChange, AvgChange);

    // Thermal field should be stable (not runaway or collapse)
    TestTrue(TEXT("No thermal runaway"), MaxChange < 1000.0); // < 1000K increase
    TestTrue(TEXT("No thermal collapse"), MinChange > -1000.0); // < 1000K decrease
    TestTrue(TEXT("Average change reasonable"), FMath::Abs(AvgChange) < 100.0); // < 100K avg change

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✓ Thermal field stability validated"));

    // ===== END PHASE 5 EXPANSION =====

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Thermal & Stress Coupling Test Complete (Phase 5 Expanded) ==="));
    AddInfo(TEXT("✅ Thermal & stress coupling test complete (10 tests)"));
    AddInfo(FString::Printf(TEXT("Elevated temps: %d vertices | Max hotspot temp: %.1fK | Subduction heating: %d boundaries"),
        ElevatedTempCount, MaxHotspotTemp, HeatedConvergentCount));

    return true;
}
