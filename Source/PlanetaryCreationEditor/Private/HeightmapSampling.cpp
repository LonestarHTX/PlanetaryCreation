#include "HeightmapSampling.h"

#include "SphericalKDTree.h"
#include "StageBAmplificationTypes.h"
#include "ContinentalAmplificationTypes.h"
#include "TectonicSimulationService.h"

#include "Containers/Map.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/Paths.h"
#include "HAL/PlatformAtomics.h"
#include <cfloat>

namespace
{
    /** Helper to generate a deterministic key for an undirected edge (A,B). */
    uint64 MakeEdgeKey(int32 A, int32 B)
    {
        const uint32 MinIndex = static_cast<uint32>(FMath::Min(A, B));
        const uint32 MaxIndex = static_cast<uint32>(FMath::Max(A, B));
        return (static_cast<uint64>(MinIndex) << 32) | static_cast<uint64>(MaxIndex);
    }
}

extern bool IsExemplarLibraryLoaded();
extern bool LoadExemplarLibraryJSON(const FString& ProjectContentDir);
extern int32 FindExemplarIndexById(const FString& ExemplarId);
extern const FExemplarMetadata* AccessExemplarMetadataConst(int32 Index);
extern bool LoadExemplarHeightData(FExemplarMetadata& Exemplar, const FString& ProjectContentDir);
extern double SampleExemplarHeight(const FExemplarMetadata& Exemplar, double U, double V);
extern FString GetStageBForcedExemplarId();

FHeightmapSampler::FHeightmapSampler(const UTectonicSimulationService& Service)
    : RenderVertices(Service.GetRenderVertices())
    , RenderTriangles(Service.GetRenderTriangles())
    , BaselineElevation(Service.GetVertexElevationValues())
    , AmplifiedElevation(Service.GetVertexAmplifiedElevation())
    , bUseAmplified(Service.IsStageBAmplificationReady() && AmplifiedElevation.Num() == RenderVertices.Num())
{
    const FString ForcedExemplarId = GetStageBForcedExemplarId();
    if (!ForcedExemplarId.IsEmpty())
    {
        const FString ProjectContentDir = FPaths::ProjectContentDir();
        if (!IsExemplarLibraryLoaded())
        {
            LoadExemplarLibraryJSON(ProjectContentDir);
        }

        const int32 ForcedIndex = FindExemplarIndexById(ForcedExemplarId);
        const FExemplarMetadata* ForcedCandidate = AccessExemplarMetadataConst(ForcedIndex);
        if (ForcedCandidate)
        {
            if (!ForcedCandidate->bDataLoaded)
            {
                LoadExemplarHeightData(*const_cast<FExemplarMetadata*>(ForcedCandidate), ProjectContentDir);
            }

            if (ForcedCandidate->bHasBounds)
            {
                ForcedExemplarMetadata = ForcedCandidate;
                ForcedWestDeg = ForcedCandidate->WestLonDeg;
                ForcedEastDeg = ForcedCandidate->EastLonDeg;
                ForcedSouthDeg = ForcedCandidate->SouthLatDeg;
                ForcedNorthDeg = ForcedCandidate->NorthLatDeg;
                ForcedLonRange = ForcedEastDeg - ForcedWestDeg;
                ForcedLatRange = ForcedNorthDeg - ForcedSouthDeg;
                
                // Use shared padding computation (50% of range, clamped to 1.5°-5° for seam safety)
                ForcedCandidate->ComputeForcedPadding(ForcedLonPad, ForcedLatPad);

                if (FMath::Abs(ForcedLonRange) > KINDA_SMALL_NUMBER && FMath::Abs(ForcedLatRange) > KINDA_SMALL_NUMBER)
                {
                    bUseForcedExemplarOverride = true;
                    UE_LOG(LogPlanetaryCreation, Log,
                        TEXT("[HeightmapSampler] Forced exemplar override enabled Id=%s LonRange=%.6f LatRange=%.6f LonPad=%.6f LatPad=%.6f"),
                        *ForcedExemplarId,
                        ForcedLonRange,
                        ForcedLatRange,
                        ForcedLonPad,
                        ForcedLatPad);
                }
            }
        }
        else
        {
            UE_LOG(LogPlanetaryCreation, Warning,
                TEXT("[HeightmapSampler] Failed to locate forced exemplar metadata for Id=%s"),
                *ForcedExemplarId);
        }
    }

    if (bUseAmplified)
    {
        const TArray<float>* FloatBaseline = nullptr;
        const TArray<FVector4f>* DummyRidge = nullptr;
        const TArray<float>* DummyCrust = nullptr;
        const TArray<FVector3f>* DummyPositions = nullptr;
        const TArray<uint32>* DummyMask = nullptr;
        Service.GetOceanicAmplificationFloatInputs(FloatBaseline, DummyRidge, DummyCrust, DummyPositions, DummyMask);

        SnapshotAmplifiedElevation = FloatBaseline;
        bHasSnapshotFloatData =
            SnapshotAmplifiedElevation != nullptr &&
            SnapshotAmplifiedElevation->Num() == RenderVertices.Num();
    }

    const int32 TriangleCount = RenderTriangles.Num() / 3;

    if (RenderVertices.Num() == 0 || TriangleCount == 0 || BaselineElevation.Num() != RenderVertices.Num())
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("[HeightmapSampler] Invalid input (Vertices=%d Triangles=%d Baseline=%d Amplified=%d bUseAmplified=%d)"),
            RenderVertices.Num(),
            TriangleCount,
            BaselineElevation.Num(),
            AmplifiedElevation.Num(),
            bUseAmplified ? 1 : 0);
        return;
    }

    TriangleData.SetNum(TriangleCount);
    TriangleDirections.SetNum(TriangleCount);
    TriangleIds.SetNum(TriangleCount);

    // Build adjacency by tracking the owning triangle for each undirected edge.
    TMap<uint64, TPair<int32, int32>> EdgeOwners;
    EdgeOwners.Reserve(TriangleCount * 3);
    SeamTriangleIndices.Reset();
    SeamTriangleIndices.Reserve(TriangleCount / 16);

    int32 SeamTriangleCount = 0;
    int32 SeamNegativeCount = 0;
    int32 SeamPositiveCount = 0;
    auto IsLongitudeNearSeam = [](double LonRadians) -> bool
    {
        constexpr double SeamThreshold = FMath::DegreesToRadians(1.0); // ±1 degree
        return FMath::Abs(LonRadians) <= SeamThreshold || FMath::Abs(FMath::Abs(LonRadians) - PI) <= SeamThreshold;
    };
    int32 LoggedSeamTriangles = 0;

    for (int32 TriangleIdx = 0; TriangleIdx < TriangleCount; ++TriangleIdx)
    {
        const int32 IndexBase = TriangleIdx * 3;

        FTriangleData& Data = TriangleData[TriangleIdx];
        Data.Vertices[0] = RenderTriangles[IndexBase + 0];
        Data.Vertices[1] = RenderTriangles[IndexBase + 1];
        Data.Vertices[2] = RenderTriangles[IndexBase + 2];
        Data.Neighbors[0] = INDEX_NONE;
        Data.Neighbors[1] = INDEX_NONE;
        Data.Neighbors[2] = INDEX_NONE;

        const FVector3d& A = RenderVertices[Data.Vertices[0]];
        const FVector3d& B = RenderVertices[Data.Vertices[1]];
        const FVector3d& C = RenderVertices[Data.Vertices[2]];

        const FVector3d Centroid = (A + B + C).GetSafeNormal();
        TriangleDirections[TriangleIdx] = Centroid;
        TriangleIds[TriangleIdx] = TriangleIdx;

        const double LonA = FMath::Atan2(A.Y, A.X);
        const double LonB = FMath::Atan2(B.Y, B.X);
        const double LonC = FMath::Atan2(C.Y, C.X);
        const bool bSeamTriangle = IsLongitudeNearSeam(LonA) || IsLongitudeNearSeam(LonB) || IsLongitudeNearSeam(LonC);
        if (bSeamTriangle)
        {
            ++SeamTriangleCount;
            SeamTriangleIndices.Add(TriangleIdx);
            const double AvgLon = (LonA + LonB + LonC) / 3.0;
            if (AvgLon < 0.0)
            {
                ++SeamNegativeCount;
            }
            else
            {
                ++SeamPositiveCount;
            }

            if (FPlatformMisc::GetEnvironmentVariable(TEXT("PLANETARY_STAGEB_TRACE_TILE_PROGRESS")).Len() > 0 && LoggedSeamTriangles < 12)
            {
                const FVector3d TriCentroidDir = (A + B + C).GetSafeNormal();
                const FVector2d TriCentroidUV = PlanetaryCreation::StageB::EquirectUVFromDirection(TriCentroidDir);
                UE_LOG(LogPlanetaryCreation, Verbose,
                    TEXT("[HeightmapSampler] SeamTriangleUV Tri=%d UVCentroid=(%.6f,%.6f) VertsUV= (%.6f,%.6f) (%.6f,%.6f) (%.6f,%.6f)"),
                    TriangleIdx,
                    TriCentroidUV.X,
                    TriCentroidUV.Y,
                    PlanetaryCreation::StageB::EquirectUVFromDirection(A).X,
                    PlanetaryCreation::StageB::EquirectUVFromDirection(A).Y,
                    PlanetaryCreation::StageB::EquirectUVFromDirection(B).X,
                    PlanetaryCreation::StageB::EquirectUVFromDirection(B).Y,
                    PlanetaryCreation::StageB::EquirectUVFromDirection(C).X,
                    PlanetaryCreation::StageB::EquirectUVFromDirection(C).Y);
                ++LoggedSeamTriangles;
            }
        }

        for (int32 Edge = 0; Edge < 3; ++Edge)
        {
            const int32 VertexA = Data.Vertices[Edge];
            const int32 VertexB = Data.Vertices[(Edge + 1) % 3];
            const uint64 EdgeKey = MakeEdgeKey(VertexA, VertexB);

            if (TPair<int32, int32>* Existing = EdgeOwners.Find(EdgeKey))
            {
                // Existing triangle shares this edge: wire up adjacency both ways.
                const int32 OtherTriangle = Existing->Key;
                const int32 OtherEdge = Existing->Value;
                if (TriangleData.IsValidIndex(OtherTriangle))
                {
                    TriangleData[OtherTriangle].Neighbors[OtherEdge] = TriangleIdx;
                }
                Data.Neighbors[Edge] = OtherTriangle;
            }
            else
            {
                EdgeOwners.Add(EdgeKey, TPair<int32, int32>(TriangleIdx, Edge));
            }
        }

        if (bSeamTriangle && FPlatformMisc::GetEnvironmentVariable(TEXT("PLANETARY_STAGEB_TRACE_TILE_PROGRESS")).Len() > 0)
        {
            UE_LOG(LogPlanetaryCreation, Verbose,
                TEXT("[HeightmapSampler] SeamTriangle Tri=%d Vertices=(%d,%d,%d) LonDeg=(%.3f, %.3f, %.3f)"),
                TriangleIdx,
                Data.Vertices[0],
                Data.Vertices[1],
                Data.Vertices[2],
                FMath::RadiansToDegrees(LonA),
                FMath::RadiansToDegrees(LonB),
                FMath::RadiansToDegrees(LonC));
        }
    }

    TriangleSearch.Build(TriangleDirections, TriangleIds);
    bIsValid = TriangleSearch.IsValid();

    UE_LOG(LogPlanetaryCreation, Log,
        TEXT("[HeightmapSampler] KD build complete Vertices=%d Triangles=%d SeamTriangles=%d (Neg=%d Pos=%d) Amplified=%d Snapshot=%d IsValid=%d"),
        RenderVertices.Num(),
        TriangleCount,
        SeamTriangleCount,
        SeamNegativeCount,
        SeamPositiveCount,
        AmplifiedElevation.Num(),
        SnapshotAmplifiedElevation ? SnapshotAmplifiedElevation->Num() : 0,
        bIsValid ? 1 : 0);

    if (bIsValid && FPlatformMisc::GetEnvironmentVariable(TEXT("PLANETARY_STAGEB_TRACE_TILE_PROGRESS")).Len() > 0)
    {
        const int32 SampleCount = FMath::Min(TriangleDirections.Num(), 5);
        for (int32 Index = 0; Index < SampleCount; ++Index)
        {
            const FVector3d Dir = TriangleDirections[Index];
            const int32 Id = TriangleIds.IsValidIndex(Index) ? TriangleIds[Index] : INDEX_NONE;
            UE_LOG(LogPlanetaryCreation, Log,
                TEXT("[HeightmapSampler] TriangleDir[%d] = (%.6f, %.6f, %.6f) Id=%d"),
                Index,
                Dir.X,
                Dir.Y,
                Dir.Z,
                Id);
        }
    }
}

double FHeightmapSampler::SampleElevationAtUV(const FVector2d& UV, FSampleInfo* OutInfo) const
{
    if (OutInfo)
    {
        *OutInfo = FSampleInfo();
    }

    if (!bIsValid)
    {
        return 0.0;
    }

    const FVector3d Direction = UVToDirection(UV);
    FVector3d WrappedDirection = Direction;
    const bool bTraceSampler = FPlatformMisc::GetEnvironmentVariable(TEXT("PLANETARY_STAGEB_TRACE_SAMPLER")).Len() > 0;
    FString ForcedExemplarId;
    if (bTraceSampler)
    {
        ForcedExemplarId = FPlatformMisc::GetEnvironmentVariable(TEXT("PLANETARY_STAGEB_FORCE_EXEMPLAR"));
        UE_LOG(LogPlanetaryCreation, Display,
            TEXT("[HeightmapSampler][Trace] Begin UV=(%.6f,%.6f) Direction=(%.6f,%.6f,%.6f) ForcedExemplar=%s"),
            UV.X,
            UV.Y,
            Direction.X,
            Direction.Y,
            Direction.Z,
            ForcedExemplarId.IsEmpty() ? TEXT("<None>") : *ForcedExemplarId);
    }

    if (bUseForcedExemplarOverride && ForcedExemplarMetadata)
    {
        const double SampleLonDeg = UV.X * 360.0 - 180.0;
        const double SampleLatDeg = 90.0 - UV.Y * 180.0;

        auto WrapLongitudeToBounds = [](double LongitudeDeg, double WestDeg, double EastDeg) -> double
        {
            const double Range = EastDeg - WestDeg;
            if (FMath::Abs(Range) <= KINDA_SMALL_NUMBER)
            {
                return LongitudeDeg;
            }

            const double AbsRange = FMath::Abs(Range);
            double Wrapped = LongitudeDeg;

            if (AbsRange < 359.0)
            {
                double NormalizedToWest = LongitudeDeg - WestDeg;
                double Modded = FMath::Fmod(NormalizedToWest, AbsRange);
                if (Modded < 0.0)
                {
                    Modded += AbsRange;
                }
                Wrapped = WestDeg + Modded;
            }

            for (int32 Iter = 0; Iter < 12; ++Iter)
            {
                if (Wrapped < WestDeg)
                {
                    Wrapped += 360.0;
                    continue;
                }
                if (Wrapped > EastDeg)
                {
                    Wrapped -= 360.0;
                    continue;
                }
                break;
            }

            return Wrapped;
        };

        auto WrapLatitudeToBounds = [](double LatitudeDeg, double SouthDeg, double NorthDeg) -> double
        {
            const double Range = NorthDeg - SouthDeg;
            if (FMath::Abs(Range) <= KINDA_SMALL_NUMBER)
            {
                return LatitudeDeg;
            }

            const double AbsRange = FMath::Abs(Range);
            double Wrapped = LatitudeDeg;

            if (AbsRange < 180.0)
            {
                double NormalizedToSouth = LatitudeDeg - SouthDeg;
                double Modded = FMath::Fmod(NormalizedToSouth, AbsRange);
                if (Modded < 0.0)
                {
                    Modded += AbsRange;
                }
                Wrapped = SouthDeg + Modded;
            }

            const double Minimum = FMath::Min(SouthDeg, NorthDeg);
            const double Maximum = FMath::Max(SouthDeg, NorthDeg);
            return FMath::Clamp(Wrapped, Minimum, Maximum);
        };

        const double WrappedLonDeg = WrapLongitudeToBounds(SampleLonDeg, ForcedWestDeg, ForcedEastDeg);
        const double WrappedLatDeg = WrapLatitudeToBounds(SampleLatDeg, ForcedSouthDeg, ForcedNorthDeg);

        const bool bValidRanges = FMath::Abs(ForcedLonRange) > KINDA_SMALL_NUMBER && FMath::Abs(ForcedLatRange) > KINDA_SMALL_NUMBER;
        if (bValidRanges)
        {
            double ForcedSampleU = (WrappedLonDeg - ForcedWestDeg) / ForcedLonRange;
            double ForcedSampleV = (ForcedNorthDeg - WrappedLatDeg) / ForcedLatRange;

            ForcedSampleU = FMath::Frac(ForcedSampleU);
            ForcedSampleV = FMath::Frac(ForcedSampleV);

            if (ForcedSampleU < 0.0)
            {
                ForcedSampleU += 1.0;
            }
            if (ForcedSampleV < 0.0)
            {
                ForcedSampleV += 1.0;
            }

            ForcedSampleU = FMath::Clamp(ForcedSampleU, 0.0, 1.0);
            ForcedSampleV = FMath::Clamp(ForcedSampleV, 0.0, 1.0);

            const double PadToleranceDeg = FMath::Max(1.0e-3, static_cast<double>(KINDA_SMALL_NUMBER));
            const double MinForcedLon = ForcedWestDeg - ForcedLonPad - PadToleranceDeg;
            const double MaxForcedLon = ForcedEastDeg + ForcedLonPad + PadToleranceDeg;
            const double MinForcedLat = ForcedSouthDeg - ForcedLatPad - PadToleranceDeg;
            const double MaxForcedLat = ForcedNorthDeg + ForcedLatPad + PadToleranceDeg;

            const bool bWithinLonPad = WrappedLonDeg >= MinForcedLon && WrappedLonDeg <= MaxForcedLon;
            const bool bWithinLatPad = WrappedLatDeg >= MinForcedLat && WrappedLatDeg <= MaxForcedLat;

            if (!bWithinLonPad || !bWithinLatPad)
            {
                // Seam triangle remap fallback: try alternative longitude wraps for extreme seam cases
                const bool bNearSeam = (UV.X < 0.02 || UV.X > 0.98);
                bool bRemapSucceeded = false;
                double RemappedHeight = 0.0;

                if (bNearSeam)
                {
                    // Try ±360° variants
                    TArray<double> AlternativeWraps = { WrappedLonDeg, WrappedLonDeg + 360.0, WrappedLonDeg - 360.0 };
                    
                    for (double AltLon : AlternativeWraps)
                    {
                        const double AltLat = WrappedLatDeg;
                        const bool bAltWithinLonPad = AltLon >= MinForcedLon && AltLon <= MaxForcedLon;
                        const bool bAltWithinLatPad = AltLat >= MinForcedLat && AltLat <= MaxForcedLat;

                        if (bAltWithinLonPad && bAltWithinLatPad)
                        {
                            // Alternative wrap succeeded - sample using this longitude
                            double AltSampleU = (AltLon - ForcedWestDeg) / ForcedLonRange;
                            double AltSampleV = (ForcedNorthDeg - AltLat) / ForcedLatRange;

                            AltSampleU = FMath::Frac(AltSampleU);
                            AltSampleV = FMath::Frac(AltSampleV);

                            if (AltSampleU < 0.0) AltSampleU += 1.0;
                            if (AltSampleV < 0.0) AltSampleV += 1.0;

                            AltSampleU = FMath::Clamp(AltSampleU, 0.0, 1.0);
                            AltSampleV = FMath::Clamp(AltSampleV, 0.0, 1.0);

                            RemappedHeight = SampleExemplarHeight(*ForcedExemplarMetadata, AltSampleU, AltSampleV);
                            bRemapSucceeded = true;

                            if (bTraceSampler)
                            {
                                UE_LOG(LogPlanetaryCreation, Display,
                                    TEXT("[HeightmapSampler][ForcedSeamRemap] UV=(%.6f,%.6f) OrigLon=%.4f AltLon=%.4f Lat=%.4f WrappedLat=%.4f U=%.6f V=%.6f Result=%.3f"),
                                    UV.X, UV.Y, WrappedLonDeg, AltLon, SampleLatDeg, AltLat, AltSampleU, AltSampleV, RemappedHeight);
                            }
                            break;
                        }
                    }
                }

                if (bRemapSucceeded)
                {
                    // Return the remapped height from seam fallback
                    if (OutInfo)
                    {
                        OutInfo->bHit = true;
                        OutInfo->TriangleIndex = INDEX_NONE;
                        OutInfo->Barycentrics = FVector3d::ZeroVector;
                        OutInfo->Steps = 0;
                    }
                    return RemappedHeight;
                }

                // No remap possible - log and fall through to KD search
                if (bTraceSampler)
                {
                    UE_LOG(LogPlanetaryCreation, Display,
                        TEXT("[HeightmapSampler][Trace] ForcedOverride skipped UV=(%.6f,%.6f) Lon=%.4f WrappedLon=%.4f Lat=%.4f WrappedLat=%.4f InsidePads=%s/%s BoundsLon=[%.4f,%.4f]+/-%.3f BoundsLat=[%.4f,%.4f]+/-%.3f NearSeam=%s RemapAttempted=%s"),
                        UV.X,
                        UV.Y,
                        SampleLonDeg,
                        WrappedLonDeg,
                        SampleLatDeg,
                        WrappedLatDeg,
                        bWithinLonPad ? TEXT("Y") : TEXT("N"),
                        bWithinLatPad ? TEXT("Y") : TEXT("N"),
                        ForcedWestDeg,
                        ForcedEastDeg,
                        ForcedLonPad,
                        ForcedSouthDeg,
                        ForcedNorthDeg,
                        ForcedLatPad,
                        bNearSeam ? TEXT("Y") : TEXT("N"),
                        bNearSeam ? TEXT("Y") : TEXT("N"));
                }
            }
            else
            {
                const double ForcedHeight = SampleExemplarHeight(*ForcedExemplarMetadata, ForcedSampleU, ForcedSampleV);

                if (OutInfo)
                {
                    OutInfo->bHit = true;
                    OutInfo->TriangleIndex = INDEX_NONE;
                    OutInfo->Barycentrics = FVector3d::ZeroVector;
                    OutInfo->Steps = 0;
                }

                static volatile int32 ForcedOverrideLogCounter = 0;
                static volatile int32 ForcedWindowLogCounter = 0;
                const int32 LogIndex = FPlatformAtomics::InterlockedIncrement(&ForcedOverrideLogCounter);
                const int32 WindowLogIndex = (bWithinLonPad && bWithinLatPad)
                    ? FPlatformAtomics::InterlockedIncrement(&ForcedWindowLogCounter)
                    : 0;

                const bool bShouldLog =
                    bTraceSampler ||
                    LogIndex <= 16 ||
                    (bWithinLonPad && bWithinLatPad && WindowLogIndex <= 32);

                if (bShouldLog)
                {
                    UE_LOG(LogPlanetaryCreation, Display,
                        TEXT("[HeightmapSampler][Trace] ForcedOverride UV=(%.6f,%.6f) Lon=%.4f WrappedLon=%.4f Lat=%.4f WrappedLat=%.4f U=%.6f V=%.6f Result=%.3f InsidePads=%s/%s LogIndex=%d WindowLogIndex=%d"),
                        UV.X,
                        UV.Y,
                        SampleLonDeg,
                        WrappedLonDeg,
                        SampleLatDeg,
                        WrappedLatDeg,
                        ForcedSampleU,
                        ForcedSampleV,
                        ForcedHeight,
                        bWithinLonPad ? TEXT("Y") : TEXT("N"),
                        bWithinLatPad ? TEXT("Y") : TEXT("N"),
                        LogIndex,
                        WindowLogIndex);
                }

                return ForcedHeight;
            }
        }
        else
        {
#if UE_BUILD_DEVELOPMENT
            static bool bLoggedInvalidForcedRanges = false;
            if (!bLoggedInvalidForcedRanges)
            {
                UE_LOG(LogPlanetaryCreation, Warning,
                    TEXT("[HeightmapSampler] Forced override metadata invalid (LonRange=%.6f LatRange=%.6f)"),
                    ForcedLonRange,
                    ForcedLatRange);
                bLoggedInvalidForcedRanges = true;
            }
#endif
        }
    }

    int32 TriangleIndex = INDEX_NONE;
    FVector3d Barycentric;
    int32 StepsTaken = 0;
    bool bUsedSeamFallback = false;
    if (!FindContainingTriangle(Direction, TriangleIndex, Barycentric, &StepsTaken))
    {
        bool bSeamRetry = false;
        FVector2d WrappedUV = UV;

        constexpr double SeamRetryThreshold = 0.002; // ~0.7 degrees worth of UV
        if (UV.X <= SeamRetryThreshold)
        {
            WrappedUV.X = UV.X + 1.0;
            bSeamRetry = true;
        }
        else if (UV.X >= 1.0 - SeamRetryThreshold)
        {
            WrappedUV.X = UV.X - 1.0;
            bSeamRetry = true;
        }

        if (bSeamRetry)
        {
            WrappedUV.X = FMath::Frac(WrappedUV.X);
            if (WrappedUV.X < 0.0)
            {
                WrappedUV.X += 1.0;
            }

            WrappedDirection = UVToDirection(WrappedUV);
            int32 SeamTriangleIndex = INDEX_NONE;
            FVector3d SeamBary;
            int32 SeamSteps = 0;
            if (FindContainingTriangle(WrappedDirection, SeamTriangleIndex, SeamBary, &SeamSteps))
            {
                UE_LOG(LogPlanetaryCreation, Verbose,
                    TEXT("[HeightmapSampler] SeamRetry succeeded UV=(%.6f,%.6f) WrappedUV=(%.6f,%.6f) Triangle=%d"),
                    UV.X,
                    UV.Y,
                    WrappedUV.X,
                    WrappedUV.Y,
                    SeamTriangleIndex);

                TriangleIndex = SeamTriangleIndex;
                Barycentric = SeamBary;
                StepsTaken += SeamSteps; // include initial failure attempts
                bUsedSeamFallback = true;
            }
            else
            {
                UE_LOG(LogPlanetaryCreation, Verbose,
                    TEXT("[HeightmapSampler] SeamRetry failed UV=(%.6f,%.6f) WrappedUV=(%.6f,%.6f)"),
                    UV.X,
                    UV.Y,
                    WrappedUV.X,
                    WrappedUV.Y);
            }
        }

        if (TriangleIndex == INDEX_NONE && SeamTriangleIndices.Num() > 0)
        {
            constexpr double InsideTolerance = -1.0e-4;
            struct FSeamCandidate
            {
                FVector3d Direction;
                double LongitudeOffsetRadians = 0.0;
            };
            TArray<FSeamCandidate, TInlineAllocator<4>> CandidateDirections;
            auto AddCandidate = [&](const FVector3d& Dir, double Offset)
            {
                for (const FSeamCandidate& Existing : CandidateDirections)
                {
                    if (Existing.Direction.Equals(Dir, 1.0e-6))
                    {
                        return;
                    }
                }
                CandidateDirections.Add({ Dir, Offset });
            };

            AddCandidate(Direction, 0.0);
            AddCandidate(UVToDirectionRaw(UV, 2.0 * PI), 2.0 * PI);
            AddCandidate(UVToDirectionRaw(UV, -2.0 * PI), -2.0 * PI);
            if (bSeamRetry)
            {
                AddCandidate(WrappedDirection, 0.0);
            }

            auto ComputeBarycentrics2D = [](const FVector2d& P, const FVector2d& A2, const FVector2d& B2, const FVector2d& C2, FVector3d& OutBary) -> bool
            {
                const FVector2d V0 = B2 - A2;
                const FVector2d V1 = C2 - A2;
                const FVector2d V2 = P - A2;
                const double D00 = FVector2d::DotProduct(V0, V0);
                const double D01 = FVector2d::DotProduct(V0, V1);
                const double D11 = FVector2d::DotProduct(V1, V1);
                const double D20 = FVector2d::DotProduct(V2, V0);
                const double D21 = FVector2d::DotProduct(V2, V1);
                const double Denom = D00 * D11 - D01 * D01;
                if (FMath::IsNearlyZero(Denom))
                {
                    return false;
                }
                const double InvDen = 1.0 / Denom;
                const double V = (D11 * D20 - D01 * D21) * InvDen;
                const double W = (D00 * D21 - D01 * D20) * InvDen;
                const double U = 1.0 - V - W;
                OutBary = FVector3d(U, V, W);
                return true;
            };

            double BestScore = -DBL_MAX;
            int32 BestTriangle = INDEX_NONE;
            FVector3d BestBary = FVector3d::ZeroVector;
            FVector3d BestDir = FVector3d::ZeroVector;
            double BestOffset = 0.0;
            FVector3d BestBary3D = FVector3d::ZeroVector;
            int32 SeamTrianglesTested = 0;

            for (const FSeamCandidate& Candidate : CandidateDirections)
            {
                for (int32 SeamTriangleIndex : SeamTriangleIndices)
                {
                    FVector3d SeamBary;
                    if (ComputeTriangleBarycentrics(SeamTriangleIndex, Candidate.Direction, SeamBary))
                    {
                        const double MinCoord = FMath::Min3(SeamBary.X, SeamBary.Y, SeamBary.Z);
                        if (MinCoord > BestScore)
                        {
                            BestScore = MinCoord;
                            BestTriangle = SeamTriangleIndex;
                            BestBary = SeamBary;
                            BestDir = Candidate.Direction;
                            BestOffset = Candidate.LongitudeOffsetRadians;
                            BestBary3D = SeamBary;
                        }

                        if (SeamBary.X >= InsideTolerance && SeamBary.Y >= InsideTolerance && SeamBary.Z >= InsideTolerance)
                        {
                            TriangleIndex = SeamTriangleIndex;
                            Barycentric = SeamBary;
                            ++StepsTaken;
                            UE_LOG(LogPlanetaryCreation, Verbose,
                            TEXT("[HeightmapSampler] SeamFallback succeeded UV=(%.6f,%.6f) Triangle=%d Dir=(%.6f,%.6f,%.6f) Offset=%.3f"),
                                UV.X,
                                UV.Y,
                                TriangleIndex,
                                Candidate.Direction.X,
                                Candidate.Direction.Y,
                                Candidate.Direction.Z,
                                Candidate.LongitudeOffsetRadians);
                            break;
                        }
                    }
                }

                if (TriangleIndex != INDEX_NONE)
                {
                    break;
                }
            }

            if (TriangleIndex == INDEX_NONE)
            {
                const double SeamAdjustment = 1.0e-3;
                const bool bSampleNearLow = UV.X <= SeamRetryThreshold;
                const bool bSampleNearHigh = UV.X >= 1.0 - SeamRetryThreshold;
                const double SampleUAdjusted = bSampleNearLow ? UV.X : (bSampleNearHigh ? UV.X + 1.0 : UV.X);
                const double SampleUFor2D = bSampleNearLow ? UV.X + SeamAdjustment : (bSampleNearHigh ? UV.X - SeamAdjustment : UV.X);
                const FVector2d SampleUV2D(SampleUFor2D, UV.Y);

                for (int32 SeamTriangleIndex : SeamTriangleIndices)
                {
                    const FTriangleData& Data = TriangleData[SeamTriangleIndex];
                    const FVector3d& APos = RenderVertices[Data.Vertices[0]];
                    const FVector3d& BPos = RenderVertices[Data.Vertices[1]];
                    const FVector3d& CPos = RenderVertices[Data.Vertices[2]];

                    FVector2d Auv = PlanetaryCreation::StageB::EquirectUVFromDirection(APos);
                    FVector2d Buv = PlanetaryCreation::StageB::EquirectUVFromDirection(BPos);
                    FVector2d Cuv = PlanetaryCreation::StageB::EquirectUVFromDirection(CPos);

                    auto AdjustU = [&](double U) -> double
                    {
                        double Adjusted = U;
                        if (bSampleNearLow)
                        {
                            if (Adjusted > 0.75)
                            {
                                Adjusted -= 1.0;
                            }
                        }
                        else if (bSampleNearHigh)
                        {
                            if (Adjusted < 0.25)
                            {
                                Adjusted += 1.0;
                            }
                        }
                        return Adjusted;
                    };

                    Auv.X = AdjustU(Auv.X);
                    Buv.X = AdjustU(Buv.X);
                    Cuv.X = AdjustU(Cuv.X);

                    FVector3d SeamBary2D;
                    if (!ComputeBarycentrics2D(SampleUV2D, Auv, Buv, Cuv, SeamBary2D))
                    {
                        continue;
                    }

                    const double Min2D = FMath::Min3(SeamBary2D.X, SeamBary2D.Y, SeamBary2D.Z);
                    if (Min2D < InsideTolerance)
                    {
                        continue;
                    }

                    const FVector3d CandidateDir = (SeamBary2D.X * APos + SeamBary2D.Y * BPos + SeamBary2D.Z * CPos).GetSafeNormal();
                    FVector3d ConfirmBary;
                    bool bAccepted = false;
                    bool bUsed3DBary = false;
                    FVector3d AcceptedBary = FVector3d::ZeroVector;

                    if (ComputeTriangleBarycentrics(SeamTriangleIndex, CandidateDir, ConfirmBary) &&
                        ConfirmBary.X >= InsideTolerance && ConfirmBary.Y >= InsideTolerance && ConfirmBary.Z >= InsideTolerance)
                    {
                        AcceptedBary = ConfirmBary;
                        bAccepted = true;
                        bUsed3DBary = true;
                    }
                    else
                    {
                        FVector3d Clamped2D = FVector3d(
                            FMath::Clamp(SeamBary2D.X, 0.0, 1.0),
                            FMath::Clamp(SeamBary2D.Y, 0.0, 1.0),
                            FMath::Clamp(SeamBary2D.Z, 0.0, 1.0));

                        const double Sum = Clamped2D.X + Clamped2D.Y + Clamped2D.Z;
                        if (Sum > UE_DOUBLE_SMALL_NUMBER)
                        {
                            Clamped2D /= Sum;
                            AcceptedBary = Clamped2D;
                            bAccepted = true;

                            UE_LOG(LogPlanetaryCreation, VeryVerbose,
                                TEXT("[HeightmapSampler] Seam2DFallback UV=(%.6f,%.6f) Triangle=%d Bary2D=(%.6f,%.6f,%.6f)"),
                                UV.X,
                                UV.Y,
                                SeamTriangleIndex,
                                Clamped2D.X,
                                Clamped2D.Y,
                                Clamped2D.Z);
                        }
                    }

                    if (bAccepted)
                    {
                        TriangleIndex = SeamTriangleIndex;
                        Barycentric = AcceptedBary;
                        ++StepsTaken;
                        bUsedSeamFallback = true;

                        if (bUsed3DBary)
                        {
                            UE_LOG(LogPlanetaryCreation, Verbose,
                                TEXT("[HeightmapSampler] SeamCentroid fallback UV=(%.6f,%.6f) Triangle=%d 2DBary=(%.6f,%.6f,%.6f)"),
                                UV.X,
                                UV.Y,
                                TriangleIndex,
                                SeamBary2D.X,
                                SeamBary2D.Y,
                                SeamBary2D.Z);
                        }

                        const TCHAR* FallbackModeLabel = bUsed3DBary ? TEXT("3DConfirm") : TEXT("2DClamp");
                        UE_LOG(LogPlanetaryCreation, Verbose,
                            TEXT("[HeightmapSampler] SeamFallback accepted UV=(%.6f,%.6f) Triangle=%d Mode=%s Bary=(%.6f,%.6f,%.6f)"),
                            UV.X,
                            UV.Y,
                            TriangleIndex,
                            FallbackModeLabel,
                            Barycentric.X,
                            Barycentric.Y,
                            Barycentric.Z);
                        break;
                    }
                }
            }

            if (TriangleIndex == INDEX_NONE)
            {
                if (BestTriangle != INDEX_NONE)
                {
                    UE_LOG(LogPlanetaryCreation, Verbose,
                        TEXT("[HeightmapSampler] SeamFallback best candidate UV=(%.6f,%.6f) Triangle=%d MinBary=%.6e Dir=(%.6f,%.6f,%.6f) Offset=%.3f Bary=(%.6f,%.6f,%.6f)"),
                        UV.X,
                        UV.Y,
                        BestTriangle,
                        BestScore,
                        BestDir.X,
                        BestDir.Y,
                        BestDir.Z,
                        BestOffset,
                        BestBary.X,
                        BestBary.Y,
                        BestBary.Z);
                }
                UE_LOG(LogPlanetaryCreation, Verbose,
                    TEXT("[HeightmapSampler] SeamFallback exhausted UV=(%.6f,%.6f)"),
                    UV.X,
                    UV.Y);
            }
        }

        if (TriangleIndex == INDEX_NONE)
        {
            if (bTraceSampler)
            {
                UE_LOG(LogPlanetaryCreation, Display,
                    TEXT("[HeightmapSampler][Trace] Miss UV=(%.6f,%.6f) Steps=%d"),
                    UV.X,
                    UV.Y,
                    StepsTaken);
            }
            if (OutInfo)
            {
                OutInfo->bHit = false;
                OutInfo->TriangleIndex = TriangleIndex;
                OutInfo->Barycentrics = FVector3d::ZeroVector;
                OutInfo->Steps = StepsTaken;
            }

            return 0.0;
        }
    }

    if (OutInfo)
    {
        OutInfo->bHit = true;
        OutInfo->TriangleIndex = TriangleIndex;
        OutInfo->Barycentrics = Barycentric;
        OutInfo->Steps = StepsTaken;
    }

    const FTriangleData& Triangle = TriangleData[TriangleIndex];

    const double Elev0 = FetchElevation(Triangle.Vertices[0]);
    const double Elev1 = FetchElevation(Triangle.Vertices[1]);
    const double Elev2 = FetchElevation(Triangle.Vertices[2]);

    const double ResultElevation = (Barycentric.X * Elev0) + (Barycentric.Y * Elev1) + (Barycentric.Z * Elev2);

    if (bTraceSampler)
    {
        UE_LOG(LogPlanetaryCreation, Display,
            TEXT("[HeightmapSampler][Trace] Hit UV=(%.6f,%.6f) Triangle=%d Vertices=(%d,%d,%d) Bary=(%.6f,%.6f,%.6f) Elevations=(%.3f,%.3f,%.3f) Result=%.3f Steps=%d SeamFallback=%s"),
            UV.X,
            UV.Y,
            TriangleIndex,
            Triangle.Vertices[0],
            Triangle.Vertices[1],
            Triangle.Vertices[2],
            Barycentric.X,
            Barycentric.Y,
            Barycentric.Z,
            Elev0,
            Elev1,
            Elev2,
            ResultElevation,
            StepsTaken,
            bUsedSeamFallback ? TEXT("true") : TEXT("false"));
    }

    return ResultElevation;
}

bool FHeightmapSampler::SampleElevationAtUVWithHint(const FVector2d& UV, int32 HintTriangleIndex, FSampleInfo* OutInfo, double& OutElevation) const
{
    if (OutInfo)
    {
        *OutInfo = FSampleInfo();
    }

    const FVector3d Direction = UVToDirection(UV);
    constexpr double InsideTolerance = -1.0e-6;

    if (TriangleData.IsValidIndex(HintTriangleIndex))
    {
        FVector3d Bary;
        if (ComputeTriangleBarycentrics(HintTriangleIndex, Direction, Bary))
        {
            if (Bary.X >= InsideTolerance && Bary.Y >= InsideTolerance && Bary.Z >= InsideTolerance)
            {
                const FTriangleData& Triangle = TriangleData[HintTriangleIndex];
                const double Elev0 = FetchElevation(Triangle.Vertices[0]);
                const double Elev1 = FetchElevation(Triangle.Vertices[1]);
                const double Elev2 = FetchElevation(Triangle.Vertices[2]);

                OutElevation = (Bary.X * Elev0) + (Bary.Y * Elev1) + (Bary.Z * Elev2);

                if (OutInfo)
                {
                    OutInfo->bHit = true;
                    OutInfo->TriangleIndex = HintTriangleIndex;
                    OutInfo->Barycentrics = Bary;
                    OutInfo->Steps = 0;
                }

                return true;
            }
        }
    }

    FSampleInfo LocalInfo;
    FSampleInfo* InfoPtr = OutInfo ? OutInfo : &LocalInfo;
    OutElevation = SampleElevationAtUV(UV, InfoPtr);
    return InfoPtr->bHit;
}

bool FHeightmapSampler::SampleElevationAtUVWithClampedHint(const FVector2d& UV, int32 TriangleIndex, FSampleInfo* OutInfo, double& OutElevation) const
{
    if (OutInfo)
    {
        *OutInfo = FSampleInfo();
    }

    if (!TriangleData.IsValidIndex(TriangleIndex))
    {
        return false;
    }

    FVector3d Bary;
    if (!ComputeTriangleBarycentrics(TriangleIndex, UVToDirection(UV), Bary))
    {
        return false;
    }

    FVector3d Clamped = Bary;
    Clamped.X = FMath::Clamp(Clamped.X, 0.0, 1.0);
    Clamped.Y = FMath::Clamp(Clamped.Y, 0.0, 1.0);
    Clamped.Z = FMath::Clamp(Clamped.Z, 0.0, 1.0);

    const double Sum = Clamped.X + Clamped.Y + Clamped.Z;
    if (Sum <= UE_DOUBLE_SMALL_NUMBER)
    {
        return false;
    }

    Clamped /= Sum;

    const FTriangleData& Triangle = TriangleData[TriangleIndex];
    const double Elev0 = FetchElevation(Triangle.Vertices[0]);
    const double Elev1 = FetchElevation(Triangle.Vertices[1]);
    const double Elev2 = FetchElevation(Triangle.Vertices[2]);

    OutElevation = (Clamped.X * Elev0) + (Clamped.Y * Elev1) + (Clamped.Z * Elev2);

    if (OutInfo)
    {
        OutInfo->bHit = true;
        OutInfo->TriangleIndex = TriangleIndex;
        OutInfo->Barycentrics = Clamped;
        OutInfo->Steps = 0;
    }

    return true;
}

FHeightmapSampler::FMemoryStats FHeightmapSampler::GetMemoryStats() const
{
    FMemoryStats Stats;
    Stats.VertexCount = RenderVertices.Num();
    Stats.TriangleCount = TriangleData.Num();
    Stats.bUsingAmplified = bUseAmplified;
    Stats.bHasSnapshotFloatBuffer = bHasSnapshotFloatData;
    Stats.TriangleDataBytes = static_cast<int64>(TriangleData.GetAllocatedSize());
    Stats.TriangleDirectionsBytes = static_cast<int64>(TriangleDirections.GetAllocatedSize());
    Stats.TriangleIdsBytes = static_cast<int64>(TriangleIds.GetAllocatedSize());

    const FSphericalKDTree::FMemoryUsage KDUsage = TriangleSearch.EstimateMemoryUsage();
    Stats.KDTreeBytes = KDUsage.NodeBytes;
    Stats.KDTreeNodeCount = KDUsage.NodeCount;

    if (bHasSnapshotFloatData && SnapshotAmplifiedElevation != nullptr)
    {
        Stats.SnapshotFloatBytes = static_cast<int64>(SnapshotAmplifiedElevation->GetAllocatedSize());
    }

    return Stats;
}

bool FHeightmapSampler::GetTriangleVertexIndices(int32 TriangleIndex, int32 (&OutVertices)[3]) const
{
    OutVertices[0] = INDEX_NONE;
    OutVertices[1] = INDEX_NONE;
    OutVertices[2] = INDEX_NONE;

    if (!TriangleData.IsValidIndex(TriangleIndex))
    {
        return false;
    }

    const FTriangleData& Data = TriangleData[TriangleIndex];
    OutVertices[0] = Data.Vertices[0];
    OutVertices[1] = Data.Vertices[1];
    OutVertices[2] = Data.Vertices[2];
    return true;
}

FVector3d FHeightmapSampler::UVToDirection(const FVector2d& InUV)
{
    return PlanetaryCreation::StageB::DirectionFromEquirectUV(InUV, PoleAvoidanceEpsilon);
}

FVector3d FHeightmapSampler::UVToDirectionRaw(const FVector2d& InUV, double LongitudeOffsetRadians)
{
    const double ClampedV = FMath::Clamp(InUV.Y, PoleAvoidanceEpsilon, 1.0 - PoleAvoidanceEpsilon);
    const double Latitude = (0.5 - ClampedV) * PI;

    double Longitude = (InUV.X - 0.5) * 2.0 * PI;
    Longitude += LongitudeOffsetRadians;

    const double CosLat = FMath::Cos(Latitude);
    const double SinLat = FMath::Sin(Latitude);
    const double CosLon = FMath::Cos(Longitude);
    const double SinLon = FMath::Sin(Longitude);

    return FVector3d(
        CosLat * CosLon,
        CosLat * SinLon,
        SinLat).GetSafeNormal();
}

bool FHeightmapSampler::ComputeTriangleBarycentrics(int32 TriangleIndex, const FVector3d& Direction, FVector3d& OutBary) const
{
    if (!TriangleData.IsValidIndex(TriangleIndex))
    {
        return false;
    }

    const FTriangleData& Triangle = TriangleData[TriangleIndex];
    const FVector3d& A = RenderVertices[Triangle.Vertices[0]];
    const FVector3d& B = RenderVertices[Triangle.Vertices[1]];
    const FVector3d& C = RenderVertices[Triangle.Vertices[2]];

    const FVector3d V0 = B - A;
    const FVector3d V1 = C - A;

    const FVector3d Normal = FVector3d::CrossProduct(V0, V1);
    const double NormalLengthSq = Normal.SizeSquared();
    if (NormalLengthSq <= UE_DOUBLE_SMALL_NUMBER)
    {
        return false;
    }

    const double PlaneDistance = FVector3d::DotProduct(Direction - A, Normal) / NormalLengthSq;
    const FVector3d Projected = Direction - (PlaneDistance * Normal);
    const FVector3d V2 = Projected - A;

    const double D00 = FVector3d::DotProduct(V0, V0);
    const double D01 = FVector3d::DotProduct(V0, V1);
    const double D11 = FVector3d::DotProduct(V1, V1);
    const double D20 = FVector3d::DotProduct(V2, V0);
    const double D21 = FVector3d::DotProduct(V2, V1);

    const double Denominator = D00 * D11 - D01 * D01;
    if (FMath::IsNearlyZero(Denominator))
    {
        return false;
    }

    const double InvDenom = 1.0 / Denominator;
    const double V = (D11 * D20 - D01 * D21) * InvDenom;
    const double W = (D00 * D21 - D01 * D20) * InvDenom;
    const double U = 1.0 - V - W;

    OutBary = FVector3d(U, V, W);
    return true;
}

bool FHeightmapSampler::FindContainingTriangle(const FVector3d& Direction, int32& OutTriangleIndex, FVector3d& OutBary, int32* OutSteps) const
{
    double NearestDistSq = 0.0;
    int32 TriangleIndex = TriangleSearch.FindNearest(Direction, NearestDistSq);

    if (TriangleIndex == INDEX_NONE)
    {
        if (OutSteps)
        {
            *OutSteps = 0;
        }
        return false;
    }

    int32 PreviousTriangle = INDEX_NONE;
    int32 StepsTaken = 0;
    constexpr double InsideTolerance = -1.0e-6;
    constexpr double AcceptanceTolerance = -1.0e-3;
    constexpr int32 MaxTraversalSteps = 32;

    TArray<int32, TInlineAllocator<MaxTraversalSteps>> Visited;
    Visited.Add(TriangleIndex);

    double BestScore = -DBL_MAX;
    int32 BestTriangle = INDEX_NONE;
    FVector3d BestBary = FVector3d::ZeroVector;

    for (; StepsTaken < MaxTraversalSteps && TriangleIndex != INDEX_NONE; ++StepsTaken)
    {
        FVector3d Bary;
        if (!ComputeTriangleBarycentrics(TriangleIndex, Direction, Bary))
        {
            break;
        }

        const double MinCoord = FMath::Min3(Bary.X, Bary.Y, Bary.Z);
        if (MinCoord > BestScore)
        {
            BestScore = MinCoord;
            BestTriangle = TriangleIndex;
            BestBary = Bary;
        }

        if (MinCoord >= InsideTolerance)
        {
            OutTriangleIndex = TriangleIndex;
            OutBary = Bary;
            if (OutSteps)
            {
                *OutSteps = StepsTaken + 1;
            }
            return true;
        }

        // Determine which edges have negative barycentric weights and try adjacent faces.
        struct FEdgeCandidate
        {
            double Weight;
            int32 Edge;
        };

        FEdgeCandidate Candidates[3] = {
            { Bary.X, 0 },
            { Bary.Y, 1 },
            { Bary.Z, 2 }
        };

        // Bubble sort the three entries so we visit the most negative first.
        for (int32 I = 0; I < 2; ++I)
        {
            for (int32 J = I + 1; J < 3; ++J)
            {
                if (Candidates[J].Weight < Candidates[I].Weight)
                {
                    Swap(Candidates[I], Candidates[J]);
                }
            }
        }

        bool bAdvanced = false;

        for (const FEdgeCandidate& Candidate : Candidates)
        {
            if (Candidate.Weight >= InsideTolerance)
            {
                continue;
            }

            const int32 Neighbor = TriangleData[TriangleIndex].Neighbors[Candidate.Edge];
            if (Neighbor != INDEX_NONE && Neighbor != PreviousTriangle && !Visited.Contains(Neighbor))
            {
                PreviousTriangle = TriangleIndex;
                TriangleIndex = Neighbor;
                Visited.Add(Neighbor);
                bAdvanced = true;
                break;
            }
        }

        if (!bAdvanced)
        {
            break;
        }
    }

    if (BestTriangle != INDEX_NONE && BestScore >= AcceptanceTolerance)
    {
        FVector3d ClampedBary = BestBary;
        ClampedBary.X = FMath::Clamp(ClampedBary.X, 0.0, 1.0);
        ClampedBary.Y = FMath::Clamp(ClampedBary.Y, 0.0, 1.0);
        ClampedBary.Z = FMath::Clamp(ClampedBary.Z, 0.0, 1.0);

        const double Sum = ClampedBary.X + ClampedBary.Y + ClampedBary.Z;
        if (Sum > UE_DOUBLE_SMALL_NUMBER)
        {
            ClampedBary /= Sum;
        }

        OutTriangleIndex = BestTriangle;
        OutBary = ClampedBary;
        if (OutSteps)
        {
            *OutSteps = StepsTaken;
        }
        return true;
    }

    if (OutSteps)
    {
        *OutSteps = StepsTaken;
    }

    return false;
}

double FHeightmapSampler::FetchElevation(int32 VertexIndex) const
{
    if (VertexIndex == INDEX_NONE)
    {
        return 0.0;
    }

    if (bUseAmplified && AmplifiedElevation.IsValidIndex(VertexIndex))
    {
        if (SnapshotAmplifiedElevation && SnapshotAmplifiedElevation->IsValidIndex(VertexIndex))
        {
            return static_cast<double>((*SnapshotAmplifiedElevation)[VertexIndex]);
        }

        return AmplifiedElevation[VertexIndex];
    }

    return BaselineElevation.IsValidIndex(VertexIndex) ? BaselineElevation[VertexIndex] : 0.0;
}
