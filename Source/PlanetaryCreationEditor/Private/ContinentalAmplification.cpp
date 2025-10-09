// Milestone 6 Task 2.2: Exemplar-Based Amplification (Continental)
// Paper Section 5: "Continental points sampling the crust falling in an orogeny zone are
// assigned specific x_T depending on the recorded endogenous factor σ, i.e. subduction or
// continental collision. The resulting terrain type is either Andean or Himalayan."

#include "PlanetaryCreationLogging.h"
#include "TectonicSimulationService.h"
#include "ContinentalAmplificationTypes.h"
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

    if (MatchingExemplars.Num() == 0)
    {
        return AmplifiedElevation;
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

#if UE_BUILD_DEVELOPMENT
    if (GContinentalAmplificationDebugInfo)
    {
        GContinentalAmplificationDebugInfo->UValue = U;
        GContinentalAmplificationDebugInfo->VValue = V;
    }
#endif

    double BlendedHeight = 0.0;
    double TotalWeight = 0.0;
    const int32 MaxExemplarsToBlend = FMath::Min(3, MatchingExemplars.Num());

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
        FExemplarMetadata* Exemplar = MatchingExemplars[ExemplarIndex];
        if (!Exemplar || !Exemplar->bDataLoaded)
        {
            continue;
        }

        const double SampledHeight = SampleExemplarHeight(*Exemplar, U, V);
        const double Weight = 1.0 / (ExemplarIndex + 1.0);

        BlendedHeight += SampledHeight * Weight;
        TotalWeight += Weight;

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

    if (MatchingExemplars.Num() > 0 && MatchingExemplars[0] && MatchingExemplars[0]->bDataLoaded)
    {
        const FExemplarMetadata& RefExemplar = *MatchingExemplars[0];
        const double DetailScale = (BaseElevation_m > 1000.0) ? (BaseElevation_m / RefExemplar.ElevationMean_m) : 0.5;
        const double Detail = (BlendedHeight - RefExemplar.ElevationMean_m) * DetailScale;
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
    return BlendContinentalExemplars(
        Position,
        PlateID,
        BaseElevation_m,
        MatchingExemplars,
        Plates,
        Boundaries,
        BoundarySummary,
        ProjectContentDir,
        Seed);
}
