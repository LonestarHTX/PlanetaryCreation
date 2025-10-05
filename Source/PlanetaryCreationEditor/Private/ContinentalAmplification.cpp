// Milestone 6 Task 2.2: Exemplar-Based Amplification (Continental)
// Paper Section 5: "Continental points sampling the crust falling in an orogeny zone are
// assigned specific x_T depending on the recorded endogenous factor σ, i.e. subduction or
// continental collision. The resulting terrain type is either Andean or Himalayan."

#include "TectonicSimulationService.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

/**
 * Milestone 6 Task 2.2: Terrain Type Classification
 *
 * Based on paper Section 5:
 * - Plains: Low elevation, no orogeny
 * - Old Mountains: Orogeny age >100 My (eroded ranges like Appalachians)
 * - Andean: Subduction orogeny (volcanic arc, active mountain building)
 * - Himalayan: Continental collision orogeny (fold/thrust belt, extreme uplift)
 */
enum class ETerrainType : uint8
{
    Plain,              // Low elevation, no orogeny
    OldMountains,       // Orogeny age >100 My (eroded ranges)
    AndeanMountains,    // Subduction orogeny (volcanic arc)
    HimalayanMountains  // Continental collision orogeny (fold/thrust belt)
};

/**
 * Exemplar metadata loaded from ExemplarLibrary.json
 */
struct FExemplarMetadata
{
    FString ID;
    FString Name;
    FString Region;  // "Himalayan", "Andean", or "Ancient"
    FString Feature;
    FString PNG16Path;
    double ElevationMin_m;
    double ElevationMax_m;
    double ElevationMean_m;
    double ElevationStdDev_m;
    int32 Width_px;
    int32 Height_px;

    // Cached texture data (loaded once, reused)
    TArray<uint16> HeightData;  // 16-bit elevation values [0, 65535]
    bool bDataLoaded = false;
};

/**
 * Global exemplar library (loaded once at startup)
 */
static TArray<FExemplarMetadata> ExemplarLibrary;
static bool bExemplarLibraryLoaded = false;

/**
 * Load exemplar library JSON from Content/PlanetaryCreation/Exemplars/ExemplarLibrary.json
 */
bool LoadExemplarLibraryJSON(const FString& ProjectContentDir)
{
    if (bExemplarLibraryLoaded)
        return true;

    const FString JsonPath = ProjectContentDir / TEXT("PlanetaryCreation/Exemplars/ExemplarLibrary.json");

    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load ExemplarLibrary.json from: %s"), *JsonPath);
        return false;
    }

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse ExemplarLibrary.json"));
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* ExemplarsArray = nullptr;
    if (!JsonObject->TryGetArrayField(TEXT("exemplars"), ExemplarsArray))
    {
        UE_LOG(LogTemp, Error, TEXT("ExemplarLibrary.json missing 'exemplars' array"));
        return false;
    }

    ExemplarLibrary.Empty();

    for (const TSharedPtr<FJsonValue>& ExemplarValue : *ExemplarsArray)
    {
        const TSharedPtr<FJsonObject>& ExemplarObj = ExemplarValue->AsObject();
        if (!ExemplarObj.IsValid())
            continue;

        FExemplarMetadata Exemplar;
        Exemplar.ID = ExemplarObj->GetStringField(TEXT("id"));
        Exemplar.Name = ExemplarObj->GetStringField(TEXT("name"));
        Exemplar.Region = ExemplarObj->GetStringField(TEXT("region"));
        Exemplar.Feature = ExemplarObj->GetStringField(TEXT("feature"));
        Exemplar.PNG16Path = ExemplarObj->GetStringField(TEXT("png16_path"));
        Exemplar.ElevationMin_m = ExemplarObj->GetNumberField(TEXT("elevation_min_m"));
        Exemplar.ElevationMax_m = ExemplarObj->GetNumberField(TEXT("elevation_max_m"));
        Exemplar.ElevationMean_m = ExemplarObj->GetNumberField(TEXT("elevation_mean_m"));
        Exemplar.ElevationStdDev_m = ExemplarObj->GetNumberField(TEXT("elevation_stddev_m"));

        const TSharedPtr<FJsonObject>& ResolutionObj = ExemplarObj->GetObjectField(TEXT("resolution"));
        Exemplar.Width_px = static_cast<int32>(ResolutionObj->GetNumberField(TEXT("width_px")));
        Exemplar.Height_px = static_cast<int32>(ResolutionObj->GetNumberField(TEXT("height_px")));

        ExemplarLibrary.Add(Exemplar);
    }

    bExemplarLibraryLoaded = true;
    UE_LOG(LogTemp, Log, TEXT("Loaded %d exemplars from ExemplarLibrary.json"), ExemplarLibrary.Num());

    return true;
}

/**
 * Load PNG16 heightfield data for a single exemplar
 * PNG16 format: 16-bit unsigned integer scaled from [elevation_min, elevation_max] to [0, 65535]
 */
bool LoadExemplarHeightData(FExemplarMetadata& Exemplar, const FString& ProjectContentDir)
{
    if (Exemplar.bDataLoaded)
        return true;

    const FString PNG16Path = ProjectContentDir / Exemplar.PNG16Path;

    // Load PNG file
    TArray<uint8> RawFileData;
    if (!FFileHelper::LoadFileToArray(RawFileData, *PNG16Path))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load PNG16: %s"), *PNG16Path);
        return false;
    }

    // Decode PNG using Unreal's image wrapper
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

    if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to decode PNG16: %s"), *PNG16Path);
        return false;
    }

    // Get raw 16-bit data
    TArray64<uint8> RawData;
    if (!ImageWrapper->GetRaw(ERGBFormat::Gray, 16, RawData))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to extract 16-bit data from PNG16: %s"), *PNG16Path);
        return false;
    }

    // Convert uint8 array to uint16 array
    const int32 PixelCount = Exemplar.Width_px * Exemplar.Height_px;
    Exemplar.HeightData.SetNum(PixelCount);

    const uint16* SourceData = reinterpret_cast<const uint16*>(RawData.GetData());
    for (int32 i = 0; i < PixelCount; ++i)
    {
        Exemplar.HeightData[i] = SourceData[i];
    }

    Exemplar.bDataLoaded = true;
    UE_LOG(LogTemp, Log, TEXT("Loaded PNG16 data for exemplar %s (%dx%d pixels)"),
        *Exemplar.ID, Exemplar.Width_px, Exemplar.Height_px);

    return true;
}

/**
 * Classify terrain type based on paper Section 5 criteria
 */
ETerrainType ClassifyTerrainType(
    const FVector3d& Position,
    int32 PlateID,
    double BaseElevation_m,
    const TArray<FTectonicPlate>& Plates,
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries,
    double OrogenyAge_My,
    EBoundaryType NearestBoundaryType)
{
    // Continental crust only (oceanic handled by Task 2.1)
    bool bIsContinental = false;
    for (const FTectonicPlate& Plate : Plates)
    {
        if (Plate.PlateID == PlateID)
        {
            bIsContinental = (Plate.CrustType == ECrustType::Continental);
            break;
        }
    }

    if (!bIsContinental)
        return ETerrainType::Plain; // Oceanic vertices skip continental amplification

    // Not in orogeny zone → Plain
    if (NearestBoundaryType != EBoundaryType::Convergent && BaseElevation_m < 500.0)
        return ETerrainType::Plain;

    // Old orogeny (>100 My) → Old Mountains (eroded)
    if (OrogenyAge_My > 100.0)
        return ETerrainType::OldMountains;

    // Recent subduction → Andean (volcanic arc)
    // Detect subduction by checking if one plate is oceanic, one continental
    bool bIsSubduction = false;
    for (const auto& BoundaryPair : Boundaries)
    {
        const TPair<int32, int32>& BoundaryKey = BoundaryPair.Key;
        const FPlateBoundary& Boundary = BoundaryPair.Value;

        if (Boundary.BoundaryType != EBoundaryType::Convergent)
            continue;

        if (BoundaryKey.Key != PlateID && BoundaryKey.Value != PlateID)
            continue;

        // Check if plates have different crust types (subduction)
        const FTectonicPlate& Plate1 = Plates[BoundaryKey.Key];
        const FTectonicPlate& Plate2 = Plates[BoundaryKey.Value];

        if (Plate1.CrustType != Plate2.CrustType)
        {
            bIsSubduction = true;
            break;
        }
    }

    if (bIsSubduction)
        return ETerrainType::AndeanMountains;

    // Recent continental collision → Himalayan (fold/thrust)
    return ETerrainType::HimalayanMountains;
}

/**
 * Sample heightfield from exemplar at given UV coordinates
 * Returns elevation in meters (remapped from [0, 65535] to [elevation_min, elevation_max])
 */
double SampleExemplarHeight(const FExemplarMetadata& Exemplar, double U, double V)
{
    if (!Exemplar.bDataLoaded || Exemplar.HeightData.Num() == 0)
        return 0.0;

    // Wrap UV coordinates to [0, 1] range (for tiling/repetition mitigation)
    U = FMath::Frac(U);
    V = FMath::Frac(V);

    // Convert UV to pixel coordinates
    const int32 X = FMath::Clamp(static_cast<int32>(U * Exemplar.Width_px), 0, Exemplar.Width_px - 1);
    const int32 Y = FMath::Clamp(static_cast<int32>(V * Exemplar.Height_px), 0, Exemplar.Height_px - 1);
    const int32 PixelIndex = Y * Exemplar.Width_px + X;

    if (!Exemplar.HeightData.IsValidIndex(PixelIndex))
        return 0.0;

    // Get 16-bit value [0, 65535]
    const uint16 RawValue = Exemplar.HeightData[PixelIndex];

    // Remap to [elevation_min, elevation_max]
    const double NormalizedHeight = static_cast<double>(RawValue) / 65535.0;
    const double ElevationRange = Exemplar.ElevationMax_m - Exemplar.ElevationMin_m;
    const double SampledElevation = Exemplar.ElevationMin_m + (NormalizedHeight * ElevationRange);

    return SampledElevation;
}

/**
 * Get exemplars matching a specific terrain type (for blending)
 */
TArray<FExemplarMetadata*> GetExemplarsForTerrainType(ETerrainType TerrainType)
{
    TArray<FExemplarMetadata*> MatchingExemplars;

    for (FExemplarMetadata& Exemplar : ExemplarLibrary)
    {
        // Map regions to terrain types
        bool bMatches = false;

        switch (TerrainType)
        {
        case ETerrainType::HimalayanMountains:
            bMatches = (Exemplar.Region == TEXT("Himalayan"));
            break;
        case ETerrainType::AndeanMountains:
            bMatches = (Exemplar.Region == TEXT("Andean"));
            break;
        case ETerrainType::OldMountains:
            bMatches = (Exemplar.Region == TEXT("Ancient"));
            break;
        case ETerrainType::Plain:
            // Use Ancient (low relief) for plains
            bMatches = (Exemplar.Region == TEXT("Ancient"));
            break;
        }

        if (bMatches)
            MatchingExemplars.Add(&Exemplar);
    }

    return MatchingExemplars;
}

/**
 * Milestone 6 Task 2.2: Compute continental amplification for a single vertex.
 *
 * Paper Section 5 approach:
 * - Classify terrain type based on orogeny history
 * - Select 2-3 matching exemplars
 * - Sample and blend heightfields
 * - Align with fold direction (TODO: requires fold direction field)
 * - Add to base elevation from coarse simulation
 */
double ComputeContinentalAmplification(
    const FVector3d& Position,
    int32 PlateID,
    double BaseElevation_m,
    const TArray<FTectonicPlate>& Plates,
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries,
    double OrogenyAge_My,
    EBoundaryType NearestBoundaryType,
    const FString& ProjectContentDir,
    int32 Seed)
{
    // Start with base elevation from M5 system
    double AmplifiedElevation = BaseElevation_m;

    // Only amplify continental crust
    bool bIsContinental = false;
    for (const FTectonicPlate& Plate : Plates)
    {
        if (Plate.PlateID == PlateID)
        {
            bIsContinental = (Plate.CrustType == ECrustType::Continental);
            break;
        }
    }

    if (!bIsContinental)
        return AmplifiedElevation; // Skip oceanic vertices

    // Load exemplar library if not already loaded
    if (!bExemplarLibraryLoaded)
    {
        if (!LoadExemplarLibraryJSON(ProjectContentDir))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to load exemplar library, skipping continental amplification"));
            return AmplifiedElevation;
        }
    }

    // Classify terrain type
    const ETerrainType TerrainType = ClassifyTerrainType(
        Position, PlateID, BaseElevation_m, Plates, Boundaries, OrogenyAge_My, NearestBoundaryType);

    // Get matching exemplars
    TArray<FExemplarMetadata*> MatchingExemplars = GetExemplarsForTerrainType(TerrainType);

    if (MatchingExemplars.Num() == 0)
    {
        // No matching exemplars, return base elevation
        return AmplifiedElevation;
    }

    // Load exemplar height data (lazy loading)
    for (FExemplarMetadata* Exemplar : MatchingExemplars)
    {
        if (!Exemplar->bDataLoaded)
        {
            LoadExemplarHeightData(*Exemplar, ProjectContentDir);
        }
    }

    // Sample and blend heightfields
    // Use position as UV coordinates (simple projection to avoid repetition)
    // Add small random offset per-vertex using seed (repetition mitigation)
    FRandomStream RandomStream(Seed + static_cast<int32>(Position.X * 1000.0 + Position.Y * 1000.0));
    const double RandomOffsetU = RandomStream.FRand() * 0.1;
    const double RandomOffsetV = RandomStream.FRand() * 0.1;

    // Simple spherical UV mapping
    const FVector3d NormalizedPos = Position.GetSafeNormal();
    const double U = 0.5 + (FMath::Atan2(NormalizedPos.Y, NormalizedPos.X) / (2.0 * PI)) + RandomOffsetU;
    const double V = 0.5 - (FMath::Asin(NormalizedPos.Z) / PI) + RandomOffsetV;

    // Blend multiple exemplars
    double BlendedHeight = 0.0;
    double TotalWeight = 0.0;
    const int32 MaxExemplarsToBlend = FMath::Min(3, MatchingExemplars.Num());

    for (int32 i = 0; i < MaxExemplarsToBlend; ++i)
    {
        FExemplarMetadata* Exemplar = MatchingExemplars[i];
        if (!Exemplar->bDataLoaded)
            continue;

        // Sample heightfield
        const double SampledHeight = SampleExemplarHeight(*Exemplar, U, V);

        // Weight by inverse index (first exemplar has highest weight)
        const double Weight = 1.0 / (i + 1.0);

        BlendedHeight += SampledHeight * Weight;
        TotalWeight += Weight;
    }

    if (TotalWeight > 0.0)
        BlendedHeight /= TotalWeight;

    // Scale blended detail to match base elevation range
    // Use exemplar's elevation range as reference
    if (MatchingExemplars.Num() > 0 && MatchingExemplars[0]->bDataLoaded)
    {
        const FExemplarMetadata& RefExemplar = *MatchingExemplars[0];
        const double ExemplarRange = RefExemplar.ElevationMax_m - RefExemplar.ElevationMin_m;
        const double DetailScale = (BaseElevation_m > 1000.0) ? (BaseElevation_m / RefExemplar.ElevationMean_m) : 0.5;

        // Add scaled detail to base elevation
        const double Detail = (BlendedHeight - RefExemplar.ElevationMean_m) * DetailScale;
        AmplifiedElevation += Detail;
    }

    return AmplifiedElevation;
}
