// Milestone 6 Task 2.2: Exemplar-Based Amplification (Continental)
// Paper Section 5: "Continental points sampling the crust falling in an orogeny zone are
// assigned specific x_T depending on the recorded endogenous factor σ, i.e. subduction or
// continental collision. The resulting terrain type is either Andean or Himalayan."

#include "Utilities/PlanetaryCreationLogging.h"
#include "Simulation/TectonicSimulationService.h"
#include "StageB/ContinentalAmplificationTypes.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

namespace
{
    static FString GStageBForceExemplarId;
    static FAutoConsoleVariableRef CVarStageBForceExemplarId(
        TEXT("r.PlanetaryCreation.StageBForceExemplarId"),
        GStageBForceExemplarId,
        TEXT("Optional exemplar ID to force Stage B to sample exclusively. Leave empty to use normal terrain-type matching."),
        ECVF_Default);

    static int32 GStageBDisableRandomOffset = 0;
    static FAutoConsoleVariableRef CVarStageBDisableRandomOffset(
        TEXT("r.PlanetaryCreation.StageBDisableRandomOffset"),
        GStageBDisableRandomOffset,
        TEXT("Set to 1 to disable random UV offsets when sampling exemplars for deterministic comparisons."),
        ECVF_Default);

    FString GetForcedExemplarId()
    {
        FString ForcedId = GStageBForceExemplarId;
        FString EnvValue = FPlatformMisc::GetEnvironmentVariable(TEXT("PLANETARY_STAGEB_FORCE_EXEMPLAR"));
        EnvValue.TrimStartAndEndInline();

        if (ForcedId.IsEmpty() && !EnvValue.IsEmpty())
        {
            ForcedId = EnvValue;
        }

        ForcedId.TrimStartAndEndInline();

#if UE_BUILD_DEVELOPMENT
        static FString LastLoggedCVarValue;
        static FString LastLoggedEnvValue;
        static FString LastLoggedEffectiveValue;
        if (!GStageBForceExemplarId.Equals(LastLoggedCVarValue, ESearchCase::CaseSensitive) ||
            !EnvValue.Equals(LastLoggedEnvValue, ESearchCase::CaseSensitive) ||
            !ForcedId.Equals(LastLoggedEffectiveValue, ESearchCase::CaseSensitive))
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[StageB][ExemplarOverride] CVar='%s' Env='%s' Effective='%s'"),
                *GStageBForceExemplarId,
                *EnvValue,
                *ForcedId);
            LastLoggedCVarValue = GStageBForceExemplarId;
            LastLoggedEnvValue = EnvValue;
            LastLoggedEffectiveValue = ForcedId;
        }
#endif

        return ForcedId;
    }

    bool ShouldDisableRandomOffset()
    {
        const FString EnvValue = FPlatformMisc::GetEnvironmentVariable(TEXT("PLANETARY_STAGEB_DISABLE_RANDOM_OFFSET"));
        FString TrimmedValue = EnvValue;
        TrimmedValue.TrimStartAndEndInline();

        const bool bEnvDisabled = TrimmedValue.Equals(TEXT("1"), ESearchCase::IgnoreCase) ||
            TrimmedValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
            TrimmedValue.Equals(TEXT("yes"), ESearchCase::IgnoreCase);

        const bool bDisabled = (GStageBDisableRandomOffset != 0) || bEnvDisabled;

#if UE_BUILD_DEVELOPMENT
        static bool bLoggedRandomOffsetState = false;
        static int32 LastLoggedCVarValue = 0;
        static FString LastLoggedEnvValue;
        static bool bLastLoggedDisabled = false;
        if (!bLoggedRandomOffsetState ||
            LastLoggedCVarValue != GStageBDisableRandomOffset ||
            !EnvValue.Equals(LastLoggedEnvValue, ESearchCase::CaseSensitive) ||
            bLastLoggedDisabled != bDisabled)
        {
            UE_LOG(LogPlanetaryCreation, Log, TEXT("[StageB][RandomOffset] CVar=%d Env='%s' Disabled=%s"),
                GStageBDisableRandomOffset,
                *EnvValue,
                bDisabled ? TEXT("true") : TEXT("false"));
            LastLoggedCVarValue = GStageBDisableRandomOffset;
            LastLoggedEnvValue = EnvValue;
            bLastLoggedDisabled = bDisabled;
            bLoggedRandomOffsetState = true;
        }
#endif

        return bDisabled;
    }

    constexpr double TwoPi = 2.0 * PI;

    FVector2d RotateVector2D(const FVector2d& Value, double AngleRadians)
    {
        const double CosAngle = FMath::Cos(AngleRadians);
        const double SinAngle = FMath::Sin(AngleRadians);
        return FVector2d(
            Value.X * CosAngle - Value.Y * SinAngle,
            Value.X * SinAngle + Value.Y * CosAngle);
    }

    void BuildLocalEastNorth(const FVector3d& Normal, FVector3d& OutEast, FVector3d& OutNorth)
    {
        const double AbsZ = FMath::Abs(Normal.Z);
        FVector3d Reference = (AbsZ < 0.99) ? FVector3d::ZAxisVector : FVector3d::XAxisVector;
        FVector3d East = FVector3d::CrossProduct(Reference, Normal);
        if (!East.Normalize())
        {
            Reference = FVector3d::YAxisVector;
            East = FVector3d::CrossProduct(Reference, Normal).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::XAxisVector);
        }

        FVector3d North = FVector3d::CrossProduct(Normal, East).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
        OutEast = East;
        OutNorth = North;
    }

    bool TryComputeFoldDirection(
        const FVector3d& Position,
        int32 PlateID,
        const TArray<FTectonicPlate>& Plates,
        const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries,
        const FPlateBoundarySummary* BoundarySummary,
        FVector3d& OutFoldDirection,
        double* OutBoundaryDistance = nullptr)
    {
        if (PlateID == INDEX_NONE || !Plates.IsValidIndex(PlateID))
        {
            return false;
        }

        const FVector3d Normal = Position.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
        const FTectonicPlate& SourcePlate = Plates[PlateID];
        const FVector3d SourceCentroid = SourcePlate.Centroid.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);

        double BestDistance = TNumericLimits<double>::Max();
        FVector3d BestFold = FVector3d::ZeroVector;

        auto ConsiderRepresentative = [&](const FVector3d& RepresentativeUnit)
        {
            FVector3d BoundaryPoint = RepresentativeUnit.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZeroVector);
            if (BoundaryPoint.IsNearlyZero())
            {
                return;
            }

            const double Distance = FMath::Acos(FMath::Clamp(Normal | BoundaryPoint, -1.0, 1.0));
            FVector3d ToBoundary = BoundaryPoint - (BoundaryPoint | Normal) * Normal;
            if (!ToBoundary.Normalize())
            {
                return;
            }

            FVector3d CandidateFold = FVector3d::CrossProduct(Normal, ToBoundary).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZeroVector);
            if (CandidateFold.IsNearlyZero())
            {
                return;
            }

            if (Distance + KINDA_SMALL_NUMBER < BestDistance)
            {
                BestDistance = Distance;
                BestFold = CandidateFold;
            }
        };

        if (BoundarySummary)
        {
            for (const FPlateBoundarySummaryEntry& Entry : BoundarySummary->Boundaries)
            {
                if (Entry.BoundaryType != EBoundaryType::Convergent || !Entry.bHasRepresentative)
                {
                    continue;
                }
                ConsiderRepresentative(Entry.RepresentativeUnit);
            }
        }

        if (BestFold.IsNearlyZero())
        {
            for (const TPair<TPair<int32, int32>, FPlateBoundary>& Pair : Boundaries)
            {
                const int32 PlateA = Pair.Key.Key;
                const int32 PlateB = Pair.Key.Value;

                if (Pair.Value.BoundaryType != EBoundaryType::Convergent)
                {
                    continue;
                }

                if (PlateA != PlateID && PlateB != PlateID)
                {
                    continue;
                }

                const int32 OtherPlateID = (PlateA == PlateID) ? PlateB : PlateA;
                if (!Plates.IsValidIndex(OtherPlateID))
                {
                    continue;
                }

                const FVector3d OtherCentroid = Plates[OtherPlateID].Centroid.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZeroVector);
                FVector3d ApproxBoundary = (SourceCentroid + OtherCentroid).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZeroVector);
                if (ApproxBoundary.IsNearlyZero())
                {
                    ApproxBoundary = OtherCentroid;
                }

                ConsiderRepresentative(ApproxBoundary);
            }
        }

        if (BestFold.IsNearlyZero())
        {
        return false;
    }

    OutFoldDirection = BestFold;
    if (OutBoundaryDistance)
    {
        *OutBoundaryDistance = BestDistance;
    }
    return true;
}
}

/**
 * Milestone 6 Task 2.2: Terrain Type Classification
 *
 * Based on paper Section 5:
 * - Plains: Low elevation, no orogeny
 * - Old Mountains: Orogeny age >100 My (eroded ranges like Appalachians)
 * - Andean: Subduction orogeny (volcanic arc, active mountain building)
 * - Himalayan: Continental collision orogeny (fold/thrust belt, extreme uplift)
 */
using ETerrainType = EContinentalTerrainType;

/**
 * Global exemplar library (loaded once at startup)
 */
static TArray<FExemplarMetadata> ExemplarLibrary;
static bool bExemplarLibraryLoaded = false;

bool IsExemplarLibraryLoaded()
{
    return bExemplarLibraryLoaded;
}

FExemplarMetadata* AccessExemplarMetadata(int32 Index)
{
    return ExemplarLibrary.IsValidIndex(Index) ? &ExemplarLibrary[Index] : nullptr;
}

const FExemplarMetadata* AccessExemplarMetadataConst(int32 Index)
{
    return ExemplarLibrary.IsValidIndex(Index) ? &ExemplarLibrary[Index] : nullptr;
}

int32 FindExemplarIndexById(const FString& ExemplarId)
{
    for (int32 Index = 0; Index < ExemplarLibrary.Num(); ++Index)
    {
        if (ExemplarLibrary[Index].ID.Equals(ExemplarId, ESearchCase::IgnoreCase))
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

#if UE_BUILD_DEVELOPMENT
static thread_local FContinentalAmplificationDebugInfo* GContinentalAmplificationDebugInfo = nullptr;

FContinentalAmplificationDebugInfo* GetContinentalAmplificationDebugInfoPtr()
{
    return GContinentalAmplificationDebugInfo;
}

void SetContinentalAmplificationDebugContext(FContinentalAmplificationDebugInfo* DebugInfo)
{
    GContinentalAmplificationDebugInfo = DebugInfo;
}
#endif

FVector2d ComputeContinentalRandomOffset(const FVector3d& Position, int32 Seed)
{
    const int32 RandomSeed = Seed + static_cast<int32>(Position.X * 1000.0 + Position.Y * 1000.0);
    FRandomStream RandomStream(RandomSeed);
    const double OffsetU = RandomStream.FRand() * 0.1;
    const double OffsetV = RandomStream.FRand() * 0.1;
    return FVector2d(OffsetU, OffsetV);
}

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
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to load ExemplarLibrary.json from: %s"), *JsonPath);
        return false;
    }

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to parse ExemplarLibrary.json"));
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* ExemplarsArray = nullptr;
    if (!JsonObject->TryGetArrayField(TEXT("exemplars"), ExemplarsArray))
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("ExemplarLibrary.json missing 'exemplars' array"));
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

        const TSharedPtr<FJsonObject>* BoundsObj = nullptr;
        if (ExemplarObj->TryGetObjectField(TEXT("bounds"), BoundsObj) && BoundsObj && (*BoundsObj).IsValid())
        {
            const TSharedPtr<FJsonObject>& Bounds = *BoundsObj;
            auto TryGetNumber = [&](const TCHAR* Key, double& OutValue) -> bool
            {
                if (!Bounds.IsValid())
                {
                    return false;
                }
                if (Bounds->HasField(Key))
                {
                    OutValue = Bounds->GetNumberField(Key);
                    return true;
                }
                return false;
            };

            double West = 0.0, East = 0.0, South = 0.0, North = 0.0;
            const bool bWest = TryGetNumber(TEXT("west"), West);
            const bool bEast = TryGetNumber(TEXT("east"), East);
            const bool bSouth = TryGetNumber(TEXT("south"), South);
            const bool bNorth = TryGetNumber(TEXT("north"), North);
            if (bWest && bEast && bSouth && bNorth)
            {
                Exemplar.WestLonDeg = West;
                Exemplar.EastLonDeg = East;
                Exemplar.SouthLatDeg = South;
                Exemplar.NorthLatDeg = North;
                Exemplar.bHasBounds = true;
            }
        }

        ExemplarLibrary.Add(Exemplar);
    }

    bExemplarLibraryLoaded = true;
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Loaded %d exemplars from ExemplarLibrary.json"), ExemplarLibrary.Num());

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
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to load PNG16: %s"), *PNG16Path);
        return false;
    }

    // Decode PNG using Unreal's image wrapper
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

    if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()))
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to decode PNG16: %s"), *PNG16Path);
        return false;
    }

    // Get raw 16-bit data
    TArray64<uint8> RawData;
    if (!ImageWrapper->GetRaw(ERGBFormat::Gray, 16, RawData))
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to extract 16-bit data from PNG16: %s"), *PNG16Path);
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
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Loaded PNG16 data for exemplar %s (%dx%d pixels)"),
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
    const FPlateBoundarySummary* BoundarySummary,
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
    if (BoundarySummary)
    {
        for (const FPlateBoundarySummaryEntry& Entry : BoundarySummary->Boundaries)
        {
            if (Entry.BoundaryType == EBoundaryType::Convergent && Entry.bIsSubduction)
            {
                bIsSubduction = true;
                break;
            }
        }
    }
    else
    {
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

    // Clamp UVs to avoid border sampling issues (matches GPU clamp addressing)
    // Use StageB_UVWrapEpsilon to keep UVs in [ε, 1-ε]
    constexpr double Eps = PlanetaryCreation::StageB::StageB_UVWrapEpsilon;
    U = FMath::Clamp(U, Eps, 1.0 - Eps);
    V = FMath::Clamp(V, Eps, 1.0 - Eps);

    // Bilinear filtering
    const double FractX = U * (Exemplar.Width_px - 1);
    const double FractY = V * (Exemplar.Height_px - 1);
    
    const int32 X0 = FMath::Clamp(FMath::FloorToInt(FractX), 0, Exemplar.Width_px - 1);
    const int32 X1 = FMath::Clamp(X0 + 1, 0, Exemplar.Width_px - 1);
    const int32 Y0 = FMath::Clamp(FMath::FloorToInt(FractY), 0, Exemplar.Height_px - 1);
    const int32 Y1 = FMath::Clamp(Y0 + 1, 0, Exemplar.Height_px - 1);
    
    const double Tx = FractX - X0;
    const double Ty = FractY - Y0;
    
    // Fetch 4 neighbors
    auto GetHeight = [&](int32 X, int32 Y) -> double {
        const int32 Idx = Y * Exemplar.Width_px + X;
        if (!Exemplar.HeightData.IsValidIndex(Idx))
            return 0.0;
        const uint16 RawValue = Exemplar.HeightData[Idx];
        const double Normalized = static_cast<double>(RawValue) / 65535.0;
        const double ElevationRange = Exemplar.ElevationMax_m - Exemplar.ElevationMin_m;
        const double DecodedElev = Exemplar.ElevationMin_m + (Normalized * ElevationRange);
        
#if UE_BUILD_DEVELOPMENT
        // Sample trace for failing exemplars (first 5 samples per exemplar)
        static TMap<FString, int32> TraceCountPerExemplar;
        int32& TraceCount = TraceCountPerExemplar.FindOrAdd(Exemplar.ID, 0);
        const bool bShouldTrace = (TraceCount < 5);
        const bool bIsFailingExemplar = (Exemplar.ID.Equals(TEXT("O01")) || Exemplar.ID.Equals(TEXT("H01")) || Exemplar.ID.Equals(TEXT("A09")));
        
        if (bShouldTrace && bIsFailingExemplar)
        {
            UE_LOG(LogPlanetaryCreation, Display,
                TEXT("[StageB][SampleTrace] Exemplar=%s Pixel=(%d,%d) RawU16=%u Norm=%.6f Range=[%.3f,%.3f] Decoded=%.3f"),
                *Exemplar.ID, X, Y, RawValue, Normalized, Exemplar.ElevationMin_m, Exemplar.ElevationMax_m, DecodedElev);
            ++TraceCount;
        }
#endif
        
        return DecodedElev;
    };
    
    const double H00 = GetHeight(X0, Y0);
    const double H10 = GetHeight(X1, Y0);
    const double H01 = GetHeight(X0, Y1);
    const double H11 = GetHeight(X1, Y1);
    
    // Bilinear interpolation
    const double H0 = FMath::Lerp(H00, H10, Tx);
    const double H1 = FMath::Lerp(H01, H11, Tx);
    return FMath::Lerp(H0, H1, Ty);
}

/**
 * Get exemplars matching a specific terrain type (for blending)
 */
TArray<FExemplarMetadata*> GetExemplarsForTerrainType(ETerrainType TerrainType)
{
    TArray<FExemplarMetadata*> MatchingExemplars;

    const FString ForcedId = GetForcedExemplarId();
    if (!ForcedId.IsEmpty())
    {
        static bool bLoggedMissingForceExemplar = false;
        for (FExemplarMetadata& Exemplar : ExemplarLibrary)
        {
            if (Exemplar.ID.Equals(ForcedId, ESearchCase::IgnoreCase))
            {
                MatchingExemplars.Add(&Exemplar);
                bLoggedMissingForceExemplar = false;
                break;
            }
        }

        if (MatchingExemplars.Num() == 0 && !bLoggedMissingForceExemplar)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("r.PlanetaryCreation.StageBForceExemplarId=\"%s\" not found in exemplar library"), *ForcedId);
            bLoggedMissingForceExemplar = true;
        }

        return MatchingExemplars;
    }

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

double BlendContinentalExemplars(
    const FVector3d& Position,
    int32 PlateID,
    double BaseElevation_m,
    const TArray<FExemplarMetadata*>& MatchingExemplars,
    const TArray<FTectonicPlate>& Plates,
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries,
    const FPlateBoundarySummary* BoundarySummary,
    const FString& ProjectContentDir,
    int32 Seed)
{
    double AmplifiedElevation = BaseElevation_m;
    const bool bTraceBlend = FPlatformMisc::GetEnvironmentVariable(TEXT("PLANETARY_STAGEB_TRACE_CONTINENTAL_BLEND")).Len() > 0;
    const FString ForcedExemplarId = GetStageBForcedExemplarId();
    const bool bForceExemplarOverride = !ForcedExemplarId.IsEmpty();

    if (MatchingExemplars.Num() == 0 && !bForceExemplarOverride)
    {
        return AmplifiedElevation;
    }

    if (bForceExemplarOverride && !IsExemplarLibraryLoaded())
    {
        LoadExemplarLibraryJSON(ProjectContentDir);
    }

    FExemplarMetadata* ForcedMetadata = nullptr;
    if (bForceExemplarOverride)
    {
        const int32 ForcedIndex = FindExemplarIndexById(ForcedExemplarId);
        ForcedMetadata = AccessExemplarMetadata(ForcedIndex);
        
#if UE_BUILD_DEVELOPMENT
        // Verify forced exemplar presence
        if (ForcedIndex == INDEX_NONE)
        {
            UE_LOG(LogPlanetaryCreation, Error,
                TEXT("[StageB][ExemplarVersion] Forced exemplar '%s' not found in library! Check stageb_manifest.json"),
                *ForcedExemplarId);
            ensureMsgf(false, TEXT("Forced exemplar ID not found: %s"), *ForcedExemplarId);
        }
        else
        {
            static bool bLoggedForcedApply = false;
            if (!bLoggedForcedApply)
            {
                UE_LOG(LogPlanetaryCreation, Display,
                    TEXT("[StageB][ForcedApply] Using forced exemplar: %s"),
                    *ForcedExemplarId);
                bLoggedForcedApply = true;
            }
        }
#endif
        
        if (bTraceBlend)
        {
            UE_LOG(LogPlanetaryCreation, Log,
                TEXT("[ContinentalBlend] Stage=Setup Plate=%d ForcedId=%s MetadataFound=%s"),
                PlateID,
                *ForcedExemplarId,
                ForcedMetadata ? TEXT("true") : TEXT("false"));
        }
    }

    for (FExemplarMetadata* Exemplar : MatchingExemplars)
    {
        if (Exemplar && !Exemplar->bDataLoaded)
        {
            LoadExemplarHeightData(*Exemplar, ProjectContentDir);
        }
    }

    const int32 RandomSeedValue = Seed + static_cast<int32>(Position.X * 1000.0 + Position.Y * 1000.0);
    FVector2d ComputedOffset = ComputeContinentalRandomOffset(Position, Seed);
    double RandomOffsetU = ComputedOffset.X;
    double RandomOffsetV = ComputedOffset.Y;

    if (ShouldDisableRandomOffset())
    {
        RandomOffsetU = 0.0;
        RandomOffsetV = 0.0;
    }

#if UE_BUILD_DEVELOPMENT
    const bool bOverrideRandomOffset = GContinentalAmplificationDebugInfo && GContinentalAmplificationDebugInfo->bUseOverrideRandomOffset;
    if (bOverrideRandomOffset)
    {
        RandomOffsetU = GContinentalAmplificationDebugInfo->OverrideRandomOffsetU;
        RandomOffsetV = GContinentalAmplificationDebugInfo->OverrideRandomOffsetV;
        if (GContinentalAmplificationDebugInfo)
        {
            GContinentalAmplificationDebugInfo->OverrideRandomSeed = RandomSeedValue;
        }
    }

    if (GContinentalAmplificationDebugInfo)
    {
        GContinentalAmplificationDebugInfo->RandomOffsetU = RandomOffsetU;
        GContinentalAmplificationDebugInfo->RandomOffsetV = RandomOffsetV;
        GContinentalAmplificationDebugInfo->RandomSeed = RandomSeedValue;
    }
#endif

    const FVector3d NormalizedPos = Position.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
    FVector2d BaseUV(
        0.5 + (FMath::Atan2(NormalizedPos.Y, NormalizedPos.X) / TwoPi),
        0.5 - (FMath::Asin(NormalizedPos.Z) / PI));

    FVector2d LocalUV = BaseUV - FVector2d(0.5, 0.5);
    LocalUV += FVector2d(RandomOffsetU, RandomOffsetV);

    FVector3d FoldDirection = FVector3d::ZeroVector;
    double FoldDistance = TNumericLimits<double>::Max();
    bool bHasFoldRotation = TryComputeFoldDirection(
        Position,
        PlateID,
        Plates,
        Boundaries,
        BoundarySummary,
        FoldDirection,
        &FoldDistance);
    double FoldAngle = 0.0;

    constexpr double FoldAlignmentMaxRadians = 0.35; // ~20 degrees
    if (!FMath::IsFinite(FoldDistance) || FoldDistance > FoldAlignmentMaxRadians)
    {
        bHasFoldRotation = false;
    }

    if (bHasFoldRotation)
    {
        FVector3d East, North;
        BuildLocalEastNorth(NormalizedPos, East, North);
        const double DotEast = FVector3d::DotProduct(FoldDirection, East);
        const double DotNorth = FVector3d::DotProduct(FoldDirection, North);
        FoldAngle = FMath::Atan2(DotNorth, DotEast);
        if (!FMath::IsFinite(FoldAngle))
        {
            bHasFoldRotation = false;
        }
    }

    FVector2d RotatedUV = bHasFoldRotation ? RotateVector2D(LocalUV, FoldAngle) : LocalUV;
    if (!GetStageBForcedExemplarId().IsEmpty())
    {
        bHasFoldRotation = false;
        RotatedUV = LocalUV;
    }
    FVector2d FinalUV = RotatedUV + FVector2d(0.5, 0.5);
    double U = FMath::Frac(FinalUV.X);
    double V = FMath::Frac(FinalUV.Y);
    if (U < 0.0)
    {
        U += 1.0;
    }
    if (V < 0.0)
    {
        V += 1.0;
    }

    if (ForcedMetadata && ForcedMetadata->bHasBounds)
    {
        const double LonDegrees = FMath::RadiansToDegrees(FMath::Atan2(NormalizedPos.Y, NormalizedPos.X));
        const double LatDegrees = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(NormalizedPos.Z, -1.0, 1.0)));
        const double LonRange = ForcedMetadata->EastLonDeg - ForcedMetadata->WestLonDeg;
        const double LatRange = ForcedMetadata->NorthLatDeg - ForcedMetadata->SouthLatDeg;
        if (FMath::Abs(LonRange) > KINDA_SMALL_NUMBER && FMath::Abs(LatRange) > KINDA_SMALL_NUMBER)
        {
            U = FMath::Clamp((LonDegrees - ForcedMetadata->WestLonDeg) / LonRange, 0.0, 1.0);
            V = FMath::Clamp((ForcedMetadata->NorthLatDeg - LatDegrees) / LatRange, 0.0, 1.0);
        }

        if (bTraceBlend)
        {
            UE_LOG(LogPlanetaryCreation, Log,
                TEXT("[ContinentalBlend] Stage=ForcedUV Plate=%d Lon=%.4f Lat=%.4f U=%.6f V=%.6f"),
                PlateID,
                LonDegrees,
                LatDegrees,
                U,
                V);
        }
    }

#if UE_BUILD_DEVELOPMENT
    if (GContinentalAmplificationDebugInfo)
    {
        GContinentalAmplificationDebugInfo->UValue = U;
        GContinentalAmplificationDebugInfo->VValue = V;
    }
#endif

    TArray<FExemplarMetadata*> EffectiveExemplars;
    if (ForcedMetadata)
    {
        EffectiveExemplars.Add(ForcedMetadata);
    }
    if (EffectiveExemplars.Num() == 0)
    {
        EffectiveExemplars = MatchingExemplars;
    }

    double BlendedHeight = 0.0;
    double TotalWeight = 0.0;
    const int32 MaxExemplarsToBlend = FMath::Min(3, EffectiveExemplars.Num());

#if UE_BUILD_DEVELOPMENT
    if (GContinentalAmplificationDebugInfo)
    {
        GContinentalAmplificationDebugInfo->ExemplarCount = static_cast<uint32>(MaxExemplarsToBlend);
        for (int32 DebugIdx = 0; DebugIdx < 3; ++DebugIdx)
        {
            GContinentalAmplificationDebugInfo->ExemplarIndices[DebugIdx] = MAX_uint32;
            GContinentalAmplificationDebugInfo->SampleHeights[DebugIdx] = 0.0;
            GContinentalAmplificationDebugInfo->Weights[DebugIdx] = 0.0;
        }
    }
#endif

    for (int32 ExemplarIndex = 0; ExemplarIndex < MaxExemplarsToBlend; ++ExemplarIndex)
    {
        FExemplarMetadata* Exemplar = EffectiveExemplars[ExemplarIndex];
        if (!Exemplar || !Exemplar->bDataLoaded)
        {
            continue;
        }

        const double SampledHeight = SampleExemplarHeight(*Exemplar, U, V);
        const double Weight = 1.0 / (ExemplarIndex + 1.0);

        BlendedHeight += SampledHeight * Weight;
        TotalWeight += Weight;

        if (bTraceBlend)
        {
            const TCHAR* ExemplarId = Exemplar->ID.IsEmpty() ? TEXT("<Unknown>") : *Exemplar->ID;
            UE_LOG(LogPlanetaryCreation, Log,
                TEXT("[ContinentalBlend] Stage=Sample Plate=%d Exemplar=%s U=%.6f V=%.6f Sample=%.3f Weight=%.3f Base=%.3f"),
                PlateID,
                ExemplarId,
                U,
                V,
                SampledHeight,
                Weight,
                BaseElevation_m);
        }

#if UE_BUILD_DEVELOPMENT
        if (GContinentalAmplificationDebugInfo)
        {
            const int32 LibraryIndex = static_cast<int32>(Exemplar - ExemplarLibrary.GetData());
            if (LibraryIndex >= 0)
            {
                GContinentalAmplificationDebugInfo->ExemplarIndices[ExemplarIndex] = static_cast<uint32>(LibraryIndex);
            }
            GContinentalAmplificationDebugInfo->SampleHeights[ExemplarIndex] = SampledHeight;
            GContinentalAmplificationDebugInfo->Weights[ExemplarIndex] = Weight;
        }
#endif
    }

    if (TotalWeight > 0.0)
    {
        BlendedHeight /= TotalWeight;
    }

#if UE_BUILD_DEVELOPMENT
    // Diagnostic for forced exemplar mode: verify weight accumulation
    if (bForceExemplarOverride)
    {
        // Log weight-sum diagnostics for sample vertices or when abnormal
        constexpr double WeightEpsilon = 1.0e-9;
        const bool bWeightTooSmall = (TotalWeight <= WeightEpsilon);
        const bool bShouldLog = bWeightTooSmall || (PlateID % 50 == 0); // Sample logging
        
        if (bShouldLog)
        {
            UE_LOG(LogPlanetaryCreation, Display,
                TEXT("[StageB][BlendTrace] Plate=%d Exemplar=%s ExemplarCount=%d AccumulatedWeights=%.6f BlendedHeight=%.3f BaseElev=%.3f"),
                PlateID,
                *ForcedExemplarId,
                MaxExemplarsToBlend,
                TotalWeight,
                BlendedHeight,
                BaseElevation_m);
        }
        
        if (bWeightTooSmall)
        {
            UE_LOG(LogPlanetaryCreation, Warning,
                TEXT("[StageB][WeightError] Plate=%d AccumulatedWeights=%.9f (too small, empty weights) - check exemplar spec"),
                PlateID,
                TotalWeight);
        }
    }
#endif

    if (EffectiveExemplars.Num() > 0 && EffectiveExemplars[0] && EffectiveExemplars[0]->bDataLoaded)
    {
        const FExemplarMetadata& RefExemplar = *EffectiveExemplars[0];
        double DetailScale = (BaseElevation_m > 1000.0) ? (BaseElevation_m / RefExemplar.ElevationMean_m) : 0.5;
        
#if UE_BUILD_DEVELOPMENT
        // DetailScale guardrails: clamp extreme values and log for diagnostics
        const double OriginalDetailScale = DetailScale;
        if (DetailScale > 100.0 || DetailScale < 0.01)
        {
            UE_LOG(LogPlanetaryCreation, Warning,
                TEXT("[StageB][DetailScale][Clamp] Plate=%d Original=%.6f Base=%.3f RefMean=%.3f Blended=%.3f - clamping to [0.01, 100.0]"),
                PlateID, DetailScale, BaseElevation_m, RefExemplar.ElevationMean_m, BlendedHeight);
            DetailScale = FMath::Clamp(DetailScale, 0.01, 100.0);
        }
#endif
        
        const double Detail = (BlendedHeight - RefExemplar.ElevationMean_m) * DetailScale;

        if (bTraceBlend)
        {
            const TCHAR* RefId = RefExemplar.ID.IsEmpty() ? TEXT("<Unknown>") : *RefExemplar.ID;
            UE_LOG(LogPlanetaryCreation, Log,
                TEXT("[ContinentalBlend] Stage=Blend Plate=%d RefExemplar=%s Base=%.3f Blended=%.3f RefMean=%.3f DetailScale=%.3f Detail=%.3f TotalWeight=%.3f"),
                PlateID,
                RefId,
                BaseElevation_m,
                BlendedHeight,
                RefExemplar.ElevationMean_m,
                DetailScale,
                Detail,
                TotalWeight);
        }

        AmplifiedElevation += Detail;

#if UE_BUILD_DEVELOPMENT
        if (GContinentalAmplificationDebugInfo)
        {
            const int32 LibraryIndex = static_cast<int32>(&RefExemplar - ExemplarLibrary.GetData());
            if (LibraryIndex >= 0)
            {
                GContinentalAmplificationDebugInfo->ExemplarIndices[0] = static_cast<uint32>(LibraryIndex);
            }
            GContinentalAmplificationDebugInfo->ReferenceMean = RefExemplar.ElevationMean_m;
        }
#endif
    }

#if UE_BUILD_DEVELOPMENT
    if (GContinentalAmplificationDebugInfo)
    {
        GContinentalAmplificationDebugInfo->TotalWeight = TotalWeight;
        GContinentalAmplificationDebugInfo->BlendedHeight = BlendedHeight;
        GContinentalAmplificationDebugInfo->CpuResult = AmplifiedElevation;
    }
#endif

    if (bTraceBlend)
    {
        UE_LOG(LogPlanetaryCreation, Log,
            TEXT("[ContinentalBlend] Stage=Result Plate=%d Base=%.3f Amplified=%.3f"),
            PlateID,
            BaseElevation_m,
            AmplifiedElevation);
    }

    return AmplifiedElevation;
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
    const FPlateBoundarySummary* BoundarySummary,
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
            UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to load exemplar library, skipping continental amplification"));
            return AmplifiedElevation;
        }
    }

    // Classify terrain type
    const ETerrainType TerrainType = ClassifyTerrainType(
        Position, PlateID, BaseElevation_m, Plates, Boundaries, BoundarySummary, OrogenyAge_My, NearestBoundaryType);

#if UE_BUILD_DEVELOPMENT
    if (GContinentalAmplificationDebugInfo)
    {
        GContinentalAmplificationDebugInfo->TerrainType = TerrainType;
    }
#endif

    const TArray<FExemplarMetadata*> MatchingExemplars = GetExemplarsForTerrainType(TerrainType);
    AmplifiedElevation = BlendContinentalExemplars(
        Position,
        PlateID,
        BaseElevation_m,
        MatchingExemplars,
        Plates,
        Boundaries,
        BoundarySummary,
        ProjectContentDir,
        Seed);

    if (TerrainType == ETerrainType::OldMountains || OrogenyAge_My > 100.0)
    {
        const double DetailDelta = AmplifiedElevation - BaseElevation_m;
        AmplifiedElevation = BaseElevation_m + (DetailDelta * 0.5);
    }

    return AmplifiedElevation;
}

FString GetStageBForcedExemplarId()
{
    return GetForcedExemplarId();
}

bool StageBShouldDisableRandomOffset()
{
    return ShouldDisableRandomOffset();
}
