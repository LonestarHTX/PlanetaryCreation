#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Containers/Set.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 6 Task 1.5: Terrane CSV export & deterministic ID persistence.
 *
 * Verifies that:
 * 1. Extracted terranes receive a stable identifier.
 * 2. ExportTerranesToCSV creates a timestamped CSV in Saved/TectonicMetrics.
 * 3. The exported row reflects the generated Terrane ID and source plate.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerranePersistenceTest,
    "PlanetaryCreation.Milestone6.TerranePersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerranePersistenceTest::RunTest(const FString& Parameters)
{
    if (!GEditor)
    {
        AddError(TEXT("Test requires editor context"));
        return false;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to access UTectonicSimulationService"));
        return false;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT(""));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Milestone 6 Task 1.5: Terrane Persistence CSV Export ==="));

    FTectonicSimulationParameters Params;
    Params.Seed = 1337;
    Params.SubdivisionLevel = 0;
    Params.RenderSubdivisionLevel = 3;
    Params.LloydIterations = 2;
    Params.bEnableDynamicRetessellation = false;
    Service->SetParameters(Params);

    TArray<FTectonicPlate>& Plates = Service->GetPlatesForModification();
    const TArray<int32>& VertexAssignments = Service->GetVertexPlateAssignments();
    const TArray<int32>& RenderTriangles = Service->GetRenderTriangles();

    int32 ContinentalPlateID = INDEX_NONE;
    for (FTectonicPlate& Plate : Plates)
    {
        if (Plate.CrustType == ECrustType::Continental)
        {
            ContinentalPlateID = Plate.PlateID;
            break;
        }
    }

    if (ContinentalPlateID == INDEX_NONE && Plates.Num() > 0)
    {
        Plates[0].CrustType = ECrustType::Continental;
        ContinentalPlateID = Plates[0].PlateID;
    }

    TestTrue(TEXT("Continental plate available"), ContinentalPlateID != INDEX_NONE);
    if (ContinentalPlateID == INDEX_NONE)
    {
        return false;
    }

    // Collect all vertices for the continental plate
    TArray<int32> PlateVertices;
    for (int32 i = 0; i < VertexAssignments.Num(); ++i)
    {
        if (VertexAssignments[i] == ContinentalPlateID)
        {
            PlateVertices.Add(i);
        }
    }

    const int32 MinTerraneSize = 10;
    TestTrue(TEXT("Continental plate has vertices"), PlateVertices.Num() >= MinTerraneSize);
    if (PlateVertices.Num() < MinTerraneSize)
    {
        return false;
    }

    const int32 TargetSize = FMath::Clamp(PlateVertices.Num() / 4, MinTerraneSize, 50);

    auto BuildCandidate = [&](int32 SeedVertex, TArray<int32>& OutVertices) -> bool
    {
        OutVertices.Reset();
        TSet<int32> LocalSet;
        OutVertices.Add(SeedVertex);
        LocalSet.Add(SeedVertex);

        for (int32 GrowthIter = 0; GrowthIter < 100 && OutVertices.Num() < TargetSize; ++GrowthIter)
        {
            bool bAdded = false;
            for (int32 TriIdx = 0; TriIdx < RenderTriangles.Num(); TriIdx += 3)
            {
                const int32 V0 = RenderTriangles[TriIdx];
                const int32 V1 = RenderTriangles[TriIdx + 1];
                const int32 V2 = RenderTriangles[TriIdx + 2];

                if (LocalSet.Contains(V0) || LocalSet.Contains(V1) || LocalSet.Contains(V2))
                {
                    auto TryAddVertex = [&](int32 Candidate)
                    {
                        if (!LocalSet.Contains(Candidate) && VertexAssignments[Candidate] == ContinentalPlateID)
                        {
                            OutVertices.Add(Candidate);
                            LocalSet.Add(Candidate);
                            bAdded = true;
                        }
                    };

                    TryAddVertex(V0);
                    TryAddVertex(V1);
                    TryAddVertex(V2);

                    if (OutVertices.Num() >= TargetSize)
                    {
                        break;
                    }
                }
            }

            if (!bAdded)
            {
                break;
            }
        }

        const double Area = Service->ComputeTerraneArea(OutVertices);
        return Area >= 100.0;
    };

    TArray<int32> CandidateVertices;
    TArray<int32> SelectedTerraneVertices;
    bool bExtractionSucceeded = false;
    int32 TerraneID = INDEX_NONE;

    AddExpectedError(TEXT("ExtractTerrane: Triangle remap failed"), EAutomationExpectedErrorFlags::Contains, 1);

    for (int32 SeedVertex : PlateVertices)
    {
        if (!BuildCandidate(SeedVertex, CandidateVertices))
        {
            continue;
        }

        int32 CandidateTerraneID = INDEX_NONE;
        if (Service->ExtractTerrane(ContinentalPlateID, CandidateVertices, CandidateTerraneID) && CandidateTerraneID != INDEX_NONE)
        {
            TerraneID = CandidateTerraneID;
            SelectedTerraneVertices = CandidateVertices;
            bExtractionSucceeded = true;
            break;
        }
    }

    TestTrue(TEXT("Terrane extraction succeeded"), bExtractionSucceeded);
    TestTrue(TEXT("Terrane ID assigned"), TerraneID != INDEX_NONE);

    if (!bExtractionSucceeded)
    {
        return false;
    }

    const TArray<FContinentalTerrane>& TerranesAfterExtraction = Service->GetTerranes();
    TestEqual(TEXT("One terrane after extraction"), TerranesAfterExtraction.Num(), 1);
    if (!TerranesAfterExtraction.IsValidIndex(0))
    {
        return false;
    }

    const FContinentalTerrane& ExtractedTerrane = TerranesAfterExtraction[0];
    TestEqual(TEXT("Terrane ID matches extracted state"), ExtractedTerrane.TerraneID, TerraneID);
    TestEqual(TEXT("Terrane vertex payload matches selection"), ExtractedTerrane.VertexPayload.Num(), SelectedTerraneVertices.Num());

    const FString OutputDir = FPaths::ProjectSavedDir() / TEXT("TectonicMetrics");
    IFileManager& FileManager = IFileManager::Get();

    TArray<FString> ExistingFiles;
    FileManager.FindFiles(ExistingFiles, *(OutputDir / TEXT("Terranes_*.csv")), true, false);

    Service->ExportTerranesToCSV();

    // Re-scan directory for newly created CSV
    TArray<FString> UpdatedFiles;
    FileManager.FindFiles(UpdatedFiles, *(OutputDir / TEXT("Terranes_*.csv")), true, false);

    FString NewFileName;
    TSet<FString> ExistingSet(ExistingFiles);
    for (const FString& FileName : UpdatedFiles)
    {
        if (!ExistingSet.Contains(FileName))
        {
            NewFileName = FileName;
            break;
        }
    }

    if (NewFileName.IsEmpty() && UpdatedFiles.Num() > 0)
    {
        // Fallback: select latest by timestamp
        FDateTime LatestTime(0);
        for (const FString& FileName : UpdatedFiles)
        {
            const FString FullPath = OutputDir / FileName;
            const FDateTime ModifiedTime = FileManager.GetTimeStamp(*FullPath);
            if (ModifiedTime > LatestTime)
            {
                LatestTime = ModifiedTime;
                NewFileName = FileName;
            }
        }
    }

    TestTrue(TEXT("Terrane CSV file created"), !NewFileName.IsEmpty());
    if (NewFileName.IsEmpty())
    {
        return false;
    }

    const FString FullPath = OutputDir / NewFileName;
    FString CSVContent;
    const bool bLoaded = FFileHelper::LoadFileToString(CSVContent, *FullPath);
    TestTrue(TEXT("Terrane CSV readable"), bLoaded);

    if (!bLoaded)
    {
        return false;
    }

    TestTrue(TEXT("Terrane CSV header present"), CSVContent.Contains(TEXT("TerraneID,State,SourcePlateID")));

    TArray<FString> Lines;
    CSVContent.ParseIntoArrayLines(Lines);

    FString DataRow;
    for (const FString& Line : Lines)
    {
        if (!Line.IsEmpty() && !Line.StartsWith(TEXT("#")) && !Line.StartsWith(TEXT("TerraneID")))
        {
            DataRow = Line;
            break;
        }
    }

    TestTrue(TEXT("Terrane data row found"), !DataRow.IsEmpty());
    if (DataRow.IsEmpty())
    {
        return false;
    }

    TArray<FString> Columns;
    DataRow.ParseIntoArray(Columns, TEXT(","), false);

    TestEqual(TEXT("Terrane CSV column count"), Columns.Num(), 12);
    if (Columns.Num() == 12)
    {
    TestEqual(TEXT("Terrane ID round-trips to CSV"), Columns[0], FString::FromInt(TerraneID));
        TestEqual(TEXT("Source plate recorded"), Columns[2], FString::FromInt(ContinentalPlateID));
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("  ✅ PASS: Terrane CSV export captured terrane %d -> %s"), TerraneID, *FullPath);
    return true;
}
