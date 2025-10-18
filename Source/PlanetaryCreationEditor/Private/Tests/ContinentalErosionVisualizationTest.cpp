// Phase 6: Continental Erosion Visualization Test
// Exports CSV artifacts for validation plotting

#include "Misc/AutomationTest.h"
#include "Simulation/TectonicSimulationService.h"
#include "Editor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContinentalErosionVisualizationTest,
    "PlanetaryCreation.Paper.ContinentalErosionVisualization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContinentalErosionVisualizationTest::RunTest(const FString& Parameters)
{
    if (!GEditor)
    {
        AddError(TEXT("Test requires editor context"));
        return false;
    }

    // Check CVar to enable CSV export
    static const IConsoleVariable* CVarWriteCSVs = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperPhase6.WriteCSVs"));
    const bool bWriteCSVs = CVarWriteCSVs ? (CVarWriteCSVs->GetInt() != 0) : true;

    if (!bWriteCSVs)
    {
        AddInfo(TEXT("CSV export disabled (r.PaperPhase6.WriteCSVs=0)"));
        return true;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get TectonicSimulationService"));
        return false;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Phase 6 Continental Erosion Visualization ==="));

    // Setup simulation with moderate resolution
    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.SubdivisionLevel = 0; // 20 plates
    Params.RenderSubdivisionLevel = 4; // ~5120 faces (enough detail)
    Params.LloydIterations = 2;
    Params.bEnableContinentalErosion = true;
    Params.bEnableHotspots = true;
    Params.ErosionConstant = 0.02; // 0.02 m/My (moderate erosion)
    Params.SeaLevel = 0.0;
    Params.ElevationScale = 10000.0;
    Params.bEnableDynamicRetessellation = false;

    Service->SetParameters(Params);

    // Timestep constant (2 My per step)
    constexpr double TimeStepMy = 2.0;

    // Initialize plate motion for stress buildup
    TArray<FTectonicPlate>& Plates = Service->GetPlatesForModification();
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        Plates[i].EulerPoleAxis = FVector3d(
            FMath::Sin(i * 0.7),
            FMath::Cos(i * 0.9),
            FMath::Sin(i * 1.1)
        ).GetSafeNormal();
        Plates[i].AngularVelocity = 0.04; // rad/My (moderate velocity)
    }

    // Run simulation: 20 steps to build mountains and apply erosion
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Running 20-step simulation..."));
    Service->AdvanceSteps(20);

    const TArray<FVector3d>& Vertices = Service->GetRenderVertices();
    const TArray<double>& Elevations = Service->GetVertexElevationValues();
    const TArray<double>& ErosionRates = Service->GetVertexErosionRates();
    const TArray<double>& StressValues = Service->GetVertexStressValues();
    const TArray<int32>& PlateAssignments = Service->GetVertexPlateAssignments();
    const TArray<FTectonicPlate>& PlatesArray = Service->GetPlates();

    const int32 N = Vertices.Num();
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Exporting %d vertices..."), N);

    // Create output directory
    const FString OutputDir = FPaths::ProjectDir() / TEXT("Docs/Automation/Validation/Phase6");
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*OutputDir))
    {
        if (!PlatformFile.CreateDirectoryTree(*OutputDir))
        {
            AddError(FString::Printf(TEXT("Failed to create directory: %s"), *OutputDir));
            return false;
        }
    }

    // === CSV 1: Erosion Profile (elevation vs erosion rate) ===
    FString ErosionProfileCSV;
    ErosionProfileCSV += TEXT("vertex_id,lat_deg,lon_deg,elevation_m,erosion_rate_m_per_My,stress_MPa,plate_id,crust_type\n");

    for (int32 i = 0; i < N; ++i)
    {
        const FVector3d& P = Vertices[i];
        const double Lat = FMath::Asin(FMath::Clamp(P.Z, -1.0, 1.0)) * 180.0 / PI;
        const double Lon = FMath::Atan2(P.Y, P.X) * 180.0 / PI;
        const double Elev = Elevations.IsValidIndex(i) ? Elevations[i] : 0.0;
        const double ErosionRate = ErosionRates.IsValidIndex(i) ? ErosionRates[i] : 0.0;
        const double Stress = StressValues.IsValidIndex(i) ? StressValues[i] : 0.0;
        const int32 PlateID = PlateAssignments.IsValidIndex(i) ? PlateAssignments[i] : INDEX_NONE;

        FString CrustType = TEXT("Unknown");
        if (PlateID != INDEX_NONE && PlatesArray.IsValidIndex(PlateID))
        {
            CrustType = (PlatesArray[PlateID].CrustType == ECrustType::Continental) ? TEXT("Continental") : TEXT("Oceanic");
        }

        ErosionProfileCSV += FString::Printf(TEXT("%d,%.6f,%.6f,%.3f,%.6f,%.3f,%d,%s\n"),
            i, Lat, Lon, Elev, ErosionRate, Stress, PlateID, *CrustType);
    }

    const FString ErosionProfilePath = OutputDir / TEXT("erosion_profile.csv");
    if (FFileHelper::SaveStringToFile(ErosionProfileCSV, *ErosionProfilePath))
    {
        UE_LOG(LogPlanetaryCreation, Display, TEXT("[Phase6] Erosion profile CSV: %s"), *ErosionProfilePath);
    }
    else
    {
        AddError(FString::Printf(TEXT("Failed to write: %s"), *ErosionProfilePath));
    }

    // === CSV 2: Time Series (elevation over time for sample vertices) ===
    // Run a fresh simulation with snapshot at each step
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Generating time series data..."));
    Service->SetParameters(Params); // Reset

    TArray<FTectonicPlate>& PlatesTS = Service->GetPlatesForModification();
    for (int32 i = 0; i < PlatesTS.Num(); ++i)
    {
        PlatesTS[i].EulerPoleAxis = FVector3d(
            FMath::Sin(i * 0.7),
            FMath::Cos(i * 0.9),
            FMath::Sin(i * 1.1)
        ).GetSafeNormal();
        PlatesTS[i].AngularVelocity = 0.04;
    }

    // Pick 50 sample vertices (continental only)
    TArray<int32> SampleVertices;
    for (int32 i = 0; i < N && SampleVertices.Num() < 50; i += N / 50)
    {
        const int32 PlateID = PlateAssignments.IsValidIndex(i) ? PlateAssignments[i] : INDEX_NONE;
        if (PlateID != INDEX_NONE && PlatesArray.IsValidIndex(PlateID))
        {
            if (PlatesArray[PlateID].CrustType == ECrustType::Continental)
            {
                SampleVertices.Add(i);
            }
        }
    }

    FString TimeSeriesCSV;
    TimeSeriesCSV += TEXT("step,time_My,vertex_id,elevation_m,erosion_rate_m_per_My\n");

    for (int32 Step = 0; Step <= 20; ++Step)
    {
        if (Step > 0)
        {
            Service->AdvanceSteps(1);
        }

        const TArray<double>& ElevTS = Service->GetVertexElevationValues();
        const TArray<double>& ErosionTS = Service->GetVertexErosionRates();
        const double TimeMy = Step * TimeStepMy;

        for (int32 VertexIdx : SampleVertices)
        {
            const double Elev = ElevTS.IsValidIndex(VertexIdx) ? ElevTS[VertexIdx] : 0.0;
            const double ErosionRate = ErosionTS.IsValidIndex(VertexIdx) ? ErosionTS[VertexIdx] : 0.0;

            TimeSeriesCSV += FString::Printf(TEXT("%d,%.1f,%d,%.3f,%.6f\n"),
                Step, TimeMy, VertexIdx, Elev, ErosionRate);
        }
    }

    const FString TimeSeriesPath = OutputDir / TEXT("erosion_timeseries.csv");
    if (FFileHelper::SaveStringToFile(TimeSeriesCSV, *TimeSeriesPath))
    {
        UE_LOG(LogPlanetaryCreation, Display, TEXT("[Phase6] Time series CSV: %s"), *TimeSeriesPath);
    }
    else
    {
        AddError(FString::Printf(TEXT("Failed to write: %s"), *TimeSeriesPath));
    }

    // === CSV 3: Elevation Histogram (distribution before/after erosion) ===
    // Compare initial (step 0) vs final (step 20)
    Service->SetParameters(Params); // Reset again

    TArray<FTectonicPlate>& PlatesHist = Service->GetPlatesForModification();
    for (int32 i = 0; i < PlatesHist.Num(); ++i)
    {
        PlatesHist[i].EulerPoleAxis = FVector3d(
            FMath::Sin(i * 0.7),
            FMath::Cos(i * 0.9),
            FMath::Sin(i * 1.1)
        ).GetSafeNormal();
        PlatesHist[i].AngularVelocity = 0.04;
    }

    // Initial state (after a few steps to build elevation)
    Service->AdvanceSteps(5);
    TArray<double> InitialElevations = Service->GetVertexElevationValues();

    // Final state (after erosion)
    Service->AdvanceSteps(15);
    TArray<double> FinalElevations = Service->GetVertexElevationValues();

    FString HistogramCSV;
    HistogramCSV += TEXT("vertex_id,initial_elevation_m,final_elevation_m,elevation_change_m,crust_type\n");

    for (int32 i = 0; i < N; ++i)
    {
        const int32 PlateID = PlateAssignments.IsValidIndex(i) ? PlateAssignments[i] : INDEX_NONE;
        FString CrustType = TEXT("Unknown");
        if (PlateID != INDEX_NONE && PlatesArray.IsValidIndex(PlateID))
        {
            CrustType = (PlatesArray[PlateID].CrustType == ECrustType::Continental) ? TEXT("Continental") : TEXT("Oceanic");
        }

        const double InitElev = InitialElevations.IsValidIndex(i) ? InitialElevations[i] : 0.0;
        const double FinalElev = FinalElevations.IsValidIndex(i) ? FinalElevations[i] : 0.0;
        const double Change = FinalElev - InitElev;

        HistogramCSV += FString::Printf(TEXT("%d,%.3f,%.3f,%.3f,%s\n"),
            i, InitElev, FinalElev, Change, *CrustType);
    }

    const FString HistogramPath = OutputDir / TEXT("elevation_histogram.csv");
    if (FFileHelper::SaveStringToFile(HistogramCSV, *HistogramPath))
    {
        UE_LOG(LogPlanetaryCreation, Display, TEXT("[Phase6] Elevation histogram CSV: %s"), *HistogramPath);
    }
    else
    {
        AddError(FString::Printf(TEXT("Failed to write: %s"), *HistogramPath));
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Phase 6 Visualization Complete ==="));
    AddInfo(TEXT("Phase 6 CSVs exported successfully"));
    AddInfo(FString::Printf(TEXT("Output directory: %s"), *OutputDir));
    AddInfo(FString::Printf(TEXT("Vertices: %d | Sample vertices: %d"), N, SampleVertices.Num()));

    return true;
}
