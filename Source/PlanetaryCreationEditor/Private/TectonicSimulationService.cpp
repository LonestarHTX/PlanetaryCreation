#include "TectonicSimulationService.h"

#include "PlanetaryCreationLogging.h"

// =====================================================================================
//  File Navigation
//  1. Console Variables & Profiling Hooks
//  2. Logging Helpers & Utilities
//  3. Service Lifecycle (Init/Reset/Shutdown)
//  4. Parameter Management & Snapshots
//  5. Simulation Step Loop (AdvanceSteps, Stage A)
//  6. Voronoi / Render Mesh Refresh
//  7. Stage B Amplification (CPU/GPU, readback)
//  8. Sediment, Dampening, Erosion (Stage A extensions)
//  9. Terrane Mechanics (Extract/Transport/Reattach)
// 10. Ridge Direction, Stress, Thermal Caches
// 11. Serialization & CSV Export
// 12. Automation/Test Helpers
// =====================================================================================
#include "Containers/BitArray.h"
#include "Containers/StaticArray.h"

DEFINE_LOG_CATEGORY(LogPlanetaryCreation);
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "Math/RandomStream.h"
#include "Algo/Sort.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SphericalKDTree.h"
#include "Trace/Trace.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "OceanicAmplificationGPU.h"
#include "ExemplarTextureArray.h"
#include "ContinentalAmplificationTypes.h"
#include <queue>
#if WITH_EDITOR
#include "Editor.h"
#endif

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/DefaultValueHelper.h"

#if WITH_EDITOR
static TAutoConsoleVariable<int32> CVarPlanetaryCreationUseGPUAmplification(
    TEXT("r.PlanetaryCreation.UseGPUAmplification"),
    0,
    TEXT("Enable GPU compute path for Stage B amplification. 0 = CPU (default), 1 = GPU when available."),
    ECVF_Default);

static TAutoConsoleVariable<int32> CVarPlanetaryCreationVisualizationMode(
    TEXT("r.PlanetaryCreation.VisualizationMode"),
    static_cast<int32>(ETectonicVisualizationMode::PlateColors),
    TEXT("Set visualization overlay: 0=Plate Colors, 1=Elevation Heatmap, 2=Velocity Field, 3=Stress Gradient."),
    ECVF_Default);

static TAutoConsoleVariable<int32> CVarPlanetaryCreationStageBProfiling(
    TEXT("r.PlanetaryCreation.StageBProfiling"),
    0,
    TEXT("Enable detailed Stage B profiling logs. 0=Off, 1=Per-step log."),
    ECVF_Default);

static void ApplyStageBProfilingCommandLineOverride()
{
    const TCHAR* CmdLine = FCommandLine::Get();
    const FString TargetCVarName = TEXT("r.PlanetaryCreation.StageBProfiling");

    const TCHAR* Search = CmdLine;
    const TCHAR* const SetCVarLiteral = TEXT("SetCVar=");
    const int32 SetCVarLiteralLen = FCString::Strlen(SetCVarLiteral);

    while ((Search = FCString::Strstr(Search, SetCVarLiteral)) != nullptr)
    {
        const TCHAR* AfterLiteral = Search + SetCVarLiteralLen;
        const TCHAR* ParseCursor = AfterLiteral;

        FString SetCVarToken;
        if (!FParse::Token(ParseCursor, SetCVarToken, false))
        {
            Search = AfterLiteral;
            continue;
        }

        Search = ParseCursor;

        SetCVarToken.TrimStartAndEndInline();
        if (SetCVarToken.StartsWith(TEXT("\"")) && SetCVarToken.EndsWith(TEXT("\"")) && SetCVarToken.Len() >= 2)
        {
            SetCVarToken.RightChopInline(1);
            SetCVarToken.LeftChopInline(1);
        }

        FString Name;
        FString ValueString;
        if (!SetCVarToken.Split(TEXT("="), &Name, &ValueString))
        {
            continue;
        }

        Name.TrimStartAndEndInline();
        ValueString.TrimStartAndEndInline();

        if (!Name.Equals(TargetCVarName, ESearchCase::IgnoreCase))
        {
            continue;
        }

        int32 ParsedValue = CVarPlanetaryCreationStageBProfiling.GetValueOnAnyThread();
        FDefaultValueHelper::ParseInt(ValueString, ParsedValue);
        CVarPlanetaryCreationStageBProfiling->Set(ParsedValue, ECVF_SetByCommandline);
        return;
    }
}

struct FStageBProfilingCommandLineInitializer
{
    FStageBProfilingCommandLineInitializer()
    {
        ApplyStageBProfilingCommandLineOverride();
    }
};

static FStageBProfilingCommandLineInitializer GStageBProfilingCommandLineInitializer;

static void HandleVisualizationModeConsoleChange(IConsoleVariable* Variable)
{
    if (!Variable)
    {
        return;
    }

    const int32 ModeValue = Variable->GetInt();
    const ETectonicVisualizationMode Mode = static_cast<ETectonicVisualizationMode>(FMath::Clamp(ModeValue, 0, 3));

    if (!GEditor)
    {
        return;
    }

    if (UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>())
    {
        Service->SetVisualizationMode(Mode);
    }
}

struct FPlanetaryCreationVisualizationModeCVarBinder
{
    FPlanetaryCreationVisualizationModeCVarBinder()
    {
        if (IConsoleVariable* Var = CVarPlanetaryCreationVisualizationMode.AsVariable())
        {
            Var->SetOnChangedCallback(FConsoleVariableDelegate::CreateStatic(&HandleVisualizationModeConsoleChange));
        }
    }
};

static FPlanetaryCreationVisualizationModeCVarBinder GPlanetaryCreationVisualizationModeCVarBinder;
#endif

#if WITH_EDITOR
struct FTectonicPlate;
struct FPlateBoundary;
double ComputeOceanicAmplification(const FVector3d& Position, int32 PlateID, double CrustAge_My, double BaseElevation_m,
    const FVector3d& RidgeDirection, const TArray<FTectonicPlate>& Plates,
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries,
    const FTectonicSimulationParameters& Parameters);
#endif

#if WITH_EDITOR
namespace
{
    inline uint32 HashMemory(uint32 ExistingHash, const void* Data, SIZE_T NumBytes)
    {
        if (Data && NumBytes > 0)
        {
            return FCrc::MemCrc32(Data, NumBytes, ExistingHash);
        }
        return ExistingHash;
    }

    uint32 HashOceanicSnapshot(const FOceanicAmplificationSnapshot& Snapshot)
    {
        if (!Snapshot.IsConsistent())
        {
            return 0;
        }

        uint32 Hash = 0;
        Hash = HashMemory(Hash, Snapshot.BaselineElevation.GetData(), Snapshot.BaselineElevation.Num() * sizeof(float));
        Hash = HashMemory(Hash, Snapshot.RidgeDirections.GetData(), Snapshot.RidgeDirections.Num() * sizeof(FVector4f));
        Hash = HashMemory(Hash, Snapshot.CrustAge.GetData(), Snapshot.CrustAge.Num() * sizeof(float));
        Hash = HashMemory(Hash, Snapshot.RenderPositions.GetData(), Snapshot.RenderPositions.Num() * sizeof(FVector3f));
        Hash = HashMemory(Hash, Snapshot.OceanicMask.GetData(), Snapshot.OceanicMask.Num() * sizeof(uint32));
        Hash = HashMemory(Hash, Snapshot.PlateAssignments.GetData(), Snapshot.PlateAssignments.Num() * sizeof(int32));
        Hash = HashMemory(Hash, &Snapshot.Parameters, sizeof(FTectonicSimulationParameters));
        Hash = HashMemory(Hash, &Snapshot.DataSerial, sizeof(uint64));
        Hash = HashMemory(Hash, &Snapshot.VertexCount, sizeof(int32));
        return Hash;
    }

    bool ComputeCurrentOceanicInputHash(const UTectonicSimulationService& Service, const FOceanicAmplificationSnapshot& Snapshot, uint32& OutHash)
    {
        const TArray<float>* LiveBaseline = nullptr;
        const TArray<FVector4f>* LiveRidge = nullptr;
        const TArray<float>* LiveCrustAge = nullptr;
        const TArray<FVector3f>* LivePositions = nullptr;
        const TArray<uint32>* LiveMask = nullptr;

        Service.GetOceanicAmplificationFloatInputs(LiveBaseline, LiveRidge, LiveCrustAge, LivePositions, LiveMask);

        if (!LiveBaseline || !LiveRidge || !LiveCrustAge || !LivePositions || !LiveMask)
        {
            return false;
        }

        if (LiveBaseline->Num() != Snapshot.VertexCount ||
            LiveRidge->Num() != Snapshot.VertexCount ||
            LiveCrustAge->Num() != Snapshot.VertexCount ||
            LivePositions->Num() != Snapshot.VertexCount ||
            LiveMask->Num() != Snapshot.VertexCount)
        {
            return false;
        }

        const TArray<int32>& LivePlateAssignments = Service.GetVertexPlateAssignments();
        if (LivePlateAssignments.Num() != Snapshot.PlateAssignments.Num())
        {
            return false;
        }

        const FTectonicSimulationParameters LiveParams = Service.GetParameters();

        uint32 Hash = 0;
        Hash = HashMemory(Hash, LiveBaseline->GetData(), LiveBaseline->Num() * sizeof(float));
        Hash = HashMemory(Hash, LiveRidge->GetData(), LiveRidge->Num() * sizeof(FVector4f));
        Hash = HashMemory(Hash, LiveCrustAge->GetData(), LiveCrustAge->Num() * sizeof(float));
        Hash = HashMemory(Hash, LivePositions->GetData(), LivePositions->Num() * sizeof(FVector3f));
        Hash = HashMemory(Hash, LiveMask->GetData(), LiveMask->Num() * sizeof(uint32));
        Hash = HashMemory(Hash, LivePlateAssignments.GetData(), LivePlateAssignments.Num() * sizeof(int32));
        Hash = HashMemory(Hash, &LiveParams, sizeof(FTectonicSimulationParameters));
        Hash = HashMemory(Hash, &Snapshot.DataSerial, sizeof(uint64));
        Hash = HashMemory(Hash, &Snapshot.VertexCount, sizeof(int32));

        OutHash = Hash;
        return true;
    }

    double EvaluateOceanicSnapshotVertex(const FOceanicAmplificationSnapshot& Snapshot, int32 Index, const FString& ProjectContentDir,
        const TArray<FTectonicPlate>& Plates, const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries)
    {
        if (!Snapshot.IsConsistent() || !Snapshot.PlateAssignments.IsValidIndex(Index))
        {
            return 0.0;
        }

        const int32 PlateID = Snapshot.PlateAssignments[Index];
        const bool bIsOceanic = Snapshot.OceanicMask.IsValidIndex(Index) ? Snapshot.OceanicMask[Index] != 0u : false;
        const double Baseline = Snapshot.BaselineElevation.IsValidIndex(Index) ? static_cast<double>(Snapshot.BaselineElevation[Index]) : 0.0;

        if (!bIsOceanic || PlateID == INDEX_NONE)
        {
            return Baseline;
        }

        const FVector3f PositionFloat = Snapshot.RenderPositions.IsValidIndex(Index) ? Snapshot.RenderPositions[Index] : FVector3f::ZeroVector;
        const FVector4f RidgeFloat = Snapshot.RidgeDirections.IsValidIndex(Index) ? Snapshot.RidgeDirections[Index] : FVector4f(0.0f, 0.0f, 1.0f, 0.0f);
        const double CrustAge = Snapshot.CrustAge.IsValidIndex(Index) ? static_cast<double>(Snapshot.CrustAge[Index]) : 0.0;

        const FVector3d Position(PositionFloat.X, PositionFloat.Y, PositionFloat.Z);
        const FVector3d RidgeDir(RidgeFloat.X, RidgeFloat.Y, RidgeFloat.Z);

        return ComputeOceanicAmplification(
            Position,
            PlateID,
            CrustAge,
            Baseline,
            RidgeDir,
            Plates,
            Boundaries,
            Snapshot.Parameters);
    }

    uint32 HashContinentalSnapshot(const FContinentalAmplificationSnapshot& Snapshot)
    {
        if (!Snapshot.IsConsistent())
        {
            return 0;
        }

        uint32 Hash = 0;
        Hash = HashMemory(Hash, Snapshot.BaselineElevation.GetData(), Snapshot.BaselineElevation.Num() * sizeof(float));
        Hash = HashMemory(Hash, Snapshot.RenderPositions.GetData(), Snapshot.RenderPositions.Num() * sizeof(FVector3f));
        Hash = HashMemory(Hash, Snapshot.CacheEntries.GetData(), Snapshot.CacheEntries.Num() * sizeof(FContinentalAmplificationCacheEntry));
        Hash = HashMemory(Hash, Snapshot.PlateAssignments.GetData(), Snapshot.PlateAssignments.Num() * sizeof(int32));
        Hash = HashMemory(Hash, Snapshot.AmplifiedElevation.GetData(), Snapshot.AmplifiedElevation.Num() * sizeof(double));
        Hash = HashMemory(Hash, &Snapshot.Parameters, sizeof(FTectonicSimulationParameters));
        Hash = HashMemory(Hash, &Snapshot.DataSerial, sizeof(uint64));
        Hash = HashMemory(Hash, &Snapshot.TopologyVersion, sizeof(int32));
        Hash = HashMemory(Hash, &Snapshot.SurfaceVersion, sizeof(int32));
        Hash = HashMemory(Hash, &Snapshot.VertexCount, sizeof(int32));
        return Hash;
    }

    bool ComputeCurrentContinentalInputHash(const UTectonicSimulationService& Service, const FContinentalAmplificationSnapshot& Snapshot, uint32& OutHash)
    {
        const FContinentalAmplificationGPUInputs& Inputs = Service.GetContinentalAmplificationGPUInputs();
        if (Inputs.BaselineElevation.Num() != Snapshot.VertexCount ||
            Inputs.RenderPositions.Num() != Snapshot.VertexCount)
        {
            return false;
        }

        const TArray<FContinentalAmplificationCacheEntry>& CacheEntries = Service.GetContinentalAmplificationCacheEntries();
        if (CacheEntries.Num() != Snapshot.VertexCount)
        {
            return false;
        }

        const TArray<int32>& PlateAssignments = Service.GetVertexPlateAssignments();
        if (PlateAssignments.Num() != Snapshot.VertexCount)
        {
            return false;
        }

        const FTectonicSimulationParameters LiveParams = Service.GetParameters();
        const uint64 DataSerial = Service.GetOceanicAmplificationDataSerial();
        const int32 TopologyVersion = Service.GetTopologyVersion();
        const int32 SurfaceVersion = Service.GetSurfaceDataVersion();
        const TArray<double>& CurrentAmplified = Service.GetVertexAmplifiedElevation();
        if (CurrentAmplified.Num() != Snapshot.VertexCount)
        {
            return false;
        }

        uint32 Hash = 0;
        Hash = HashMemory(Hash, Inputs.BaselineElevation.GetData(), Inputs.BaselineElevation.Num() * sizeof(float));
        Hash = HashMemory(Hash, Inputs.RenderPositions.GetData(), Inputs.RenderPositions.Num() * sizeof(FVector3f));
        Hash = HashMemory(Hash, CacheEntries.GetData(), CacheEntries.Num() * sizeof(FContinentalAmplificationCacheEntry));
        Hash = HashMemory(Hash, PlateAssignments.GetData(), PlateAssignments.Num() * sizeof(int32));
        Hash = HashMemory(Hash, CurrentAmplified.GetData(), CurrentAmplified.Num() * sizeof(double));
        Hash = HashMemory(Hash, &LiveParams, sizeof(FTectonicSimulationParameters));
        Hash = HashMemory(Hash, &DataSerial, sizeof(uint64));
        Hash = HashMemory(Hash, &TopologyVersion, sizeof(int32));
        Hash = HashMemory(Hash, &SurfaceVersion, sizeof(int32));
        Hash = HashMemory(Hash, &Snapshot.VertexCount, sizeof(int32));

        OutHash = Hash;
        return true;
    }

}
#endif

#if UE_BUILD_DEVELOPMENT
void UTectonicSimulationService::LogPlateElevationMismatches(const TCHAR* ContextLabel, int32 SampleCount, int32 MaxLogged) const
{
    if (RenderVertices.Num() == 0 || VertexPlateAssignments.Num() == 0)
    {
        return;
    }

    const int32 SampleLimit = FMath::Clamp(SampleCount, 0, RenderVertices.Num());
    if (SampleLimit <= 0)
    {
        return;
    }

    const TCHAR* Label = (ContextLabel && *ContextLabel) ? ContextLabel : TEXT("Unknown");
    constexpr double ContinentalThresholdMeters = -1000.0;

    int32 MismatchCount = 0;

    for (int32 VertexIdx = 0; VertexIdx < SampleLimit; ++VertexIdx)
    {
        const int32 PlateID = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;
        if (!(PlateID != INDEX_NONE && Plates.IsValidIndex(PlateID)))
        {
            continue;
        }

        const FTectonicPlate& Plate = Plates[PlateID];
        const double Elevation = VertexElevationValues.IsValidIndex(VertexIdx) ? VertexElevationValues[VertexIdx] : 0.0;
        const bool bPlateContinental = (Plate.CrustType == ECrustType::Continental);
        const bool bElevationContinental = Elevation > ContinentalThresholdMeters;

        if (bPlateContinental != bElevationContinental)
        {
            if (MismatchCount < MaxLogged)
            {
                UE_LOG(LogPlanetaryCreation, Warning,
                    TEXT("[PlateDiag:%s] Vertex %d Plate=%d Type=%s Elev=%.1f m"),
                    Label,
                    VertexIdx,
                    PlateID,
                    bPlateContinental ? TEXT("Continental") : TEXT("Oceanic"),
                    Elevation);
            }
            ++MismatchCount;
        }
    }

    if (MismatchCount > 0)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("[PlateDiag:%s] %d mismatches detected within first %d vertices"),
            Label,
            MismatchCount,
            SampleLimit);
    }
}
#endif // UE_BUILD_DEVELOPMENT

void UTectonicSimulationService::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ResetSimulation();
}

void UTectonicSimulationService::Deinitialize()
{
    // Milestone 6 GPU: Cleanup GPU resources before shutdown
    ShutdownGPUExemplarResources();

    BaseSphereSamples.Reset();
    Plates.Reset();
    SharedVertices.Reset();
    Boundaries.Reset();
    RenderVertices.Reset();
    RenderTriangles.Reset();
    VertexPlateAssignments.Reset();
    VertexVelocities.Reset();
    VertexStressValues.Reset();
    RenderVertexBoundaryCache.Reset();
    Super::Deinitialize();
}

void UTectonicSimulationService::InvalidateRidgeDirectionCache()
{
    CachedRidgeDirectionTopologyVersion = INDEX_NONE;
    CachedRidgeDirectionVertexCount = 0;
    RidgeDirectionDirtyMask.Reset();
    RidgeDirectionDirtyCount = 0;

    RidgeDirectionFloatSoA.DirX.Reset();
    RidgeDirectionFloatSoA.DirY.Reset();
    RidgeDirectionFloatSoA.DirZ.Reset();
    RidgeDirectionFloatSoA.CachedTopologyVersion = INDEX_NONE;
    RidgeDirectionFloatSoA.CachedVertexCount = 0;
    InvalidatePlateBoundarySummaries();
}

void UTectonicSimulationService::EnsureRidgeDirtyMaskSize(int32 VertexCount) const
{
    if (VertexCount <= 0)
    {
        const_cast<TBitArray<>&>(RidgeDirectionDirtyMask).Reset();
        const_cast<int32&>(RidgeDirectionDirtyCount) = 0;
        const_cast<int32&>(LastRidgeDirectionUpdateCount) = 0;
        return;
    }

    if (RidgeDirectionDirtyMask.Num() != VertexCount)
    {
        const_cast<TBitArray<>&>(RidgeDirectionDirtyMask).Init(false, VertexCount);
        const_cast<int32&>(RidgeDirectionDirtyCount) = 0;
        const_cast<int32&>(LastRidgeDirectionUpdateCount) = 0;
    }
}

bool UTectonicSimulationService::MarkRidgeDirectionVertexDirty(int32 VertexIdx)
{
    if (!RenderVertices.IsValidIndex(VertexIdx))
    {
        return false;
    }

    EnsureRidgeDirtyMaskSize(RenderVertices.Num());

    if (!RidgeDirectionDirtyMask.IsValidIndex(VertexIdx))
    {
        return false;
    }

    if (!RidgeDirectionDirtyMask[VertexIdx])
    {
        RidgeDirectionDirtyMask[VertexIdx] = true;
        ++RidgeDirectionDirtyCount;
        return true;
    }

    return false;
}

void UTectonicSimulationService::MarkAllRidgeDirectionsDirty()
{
    const int32 VertexCount = RenderVertices.Num();
    if (VertexCount <= 0)
    {
        RidgeDirectionDirtyMask.Reset();
        RidgeDirectionDirtyCount = 0;
        CachedRidgeDirectionTopologyVersion = INDEX_NONE;
        CachedRidgeDirectionVertexCount = 0;
        LastRidgeDirectionUpdateCount = 0;
        return;
    }

    RidgeDirectionDirtyMask.Init(true, VertexCount);
    RidgeDirectionDirtyCount = VertexCount;
    CachedRidgeDirectionTopologyVersion = INDEX_NONE;
    CachedRidgeDirectionVertexCount = 0;
    LastRidgeDirectionUpdateCount = 0;

#if UE_BUILD_DEVELOPMENT
    UE_LOG(LogPlanetaryCreation, VeryVerbose,
        TEXT("[MarkAllRidgeDirectionsDirty] DirtyMask.Num=%d DirtyCount=%d"),
        RidgeDirectionDirtyMask.Num(), RidgeDirectionDirtyCount);
#endif
}

void UTectonicSimulationService::MarkRidgeRingDirty(const TArray<int32>& SeedVertices, int32 RingDepth)
{
    const int32 VertexCount = RenderVertices.Num();
    if (VertexCount <= 0 || SeedVertices.Num() == 0)
    {
        return;
    }

    EnsureRidgeDirtyMaskSize(VertexCount);

    if (RenderVertexAdjacencyOffsets.Num() != VertexCount + 1 || RenderVertexAdjacency.Num() == 0)
    {
        BuildRenderVertexAdjacency();
    }

    const int32 DepthLimit = FMath::Max(0, RingDepth);

    TBitArray<> Added(false, VertexCount);
    TArray<int32> Current;
    Current.Reserve(SeedVertices.Num());

    for (int32 Seed : SeedVertices)
    {
        if (!RenderVertices.IsValidIndex(Seed) || Added[Seed])
        {
            continue;
        }

        Added[Seed] = true;
        MarkRidgeDirectionVertexDirty(Seed);
        Current.Add(Seed);
    }

    for (int32 Depth = 0; Depth < DepthLimit; ++Depth)
    {
        if (Current.Num() == 0)
        {
            break;
        }

        TArray<int32> Next;
        for (int32 VertexIdx : Current)
        {
            if (!RenderVertices.IsValidIndex(VertexIdx))
            {
                continue;
            }

            const int32 Start = RenderVertexAdjacencyOffsets.IsValidIndex(VertexIdx)
                ? RenderVertexAdjacencyOffsets[VertexIdx]
                : 0;
            const int32 End = RenderVertexAdjacencyOffsets.IsValidIndex(VertexIdx + 1)
                ? RenderVertexAdjacencyOffsets[VertexIdx + 1]
                : Start;

            for (int32 AdjIdx = Start; AdjIdx < End; ++AdjIdx)
            {
                if (!RenderVertexAdjacency.IsValidIndex(AdjIdx))
                {
                    continue;
                }

                const int32 Neighbor = RenderVertexAdjacency[AdjIdx];
                if (!RenderVertices.IsValidIndex(Neighbor) || Added[Neighbor])
                {
                    continue;
                }

                Added[Neighbor] = true;
                MarkRidgeDirectionVertexDirty(Neighbor);
                Next.Add(Neighbor);
            }
        }

        Current = MoveTemp(Next);
    }
}

void UTectonicSimulationService::EnqueueCrustAgeResetSeeds(const TArray<int32>& SeedVertices)
{
    const int32 VertexCount = RenderVertices.Num();
    if (VertexCount <= 0 || SeedVertices.Num() == 0)
    {
        return;
    }

    if (PendingCrustAgeResetMask.Num() != VertexCount)
    {
        PendingCrustAgeResetMask.Init(false, VertexCount);
        PendingCrustAgeResetSeeds.Reset();
    }

    for (int32 Seed : SeedVertices)
    {
        if (!RenderVertices.IsValidIndex(Seed))
        {
            continue;
        }

        if (!PendingCrustAgeResetMask[Seed])
        {
            PendingCrustAgeResetMask[Seed] = true;
            PendingCrustAgeResetSeeds.Add(Seed);
        }
    }
}

void UTectonicSimulationService::ResetCrustAgeForSeeds(int32 RingDepth)
{
    const int32 VertexCount = RenderVertices.Num();
    if (VertexCount <= 0 || PendingCrustAgeResetSeeds.Num() == 0)
    {
        return;
    }

    if (RenderVertexAdjacencyOffsets.Num() != VertexCount + 1 || RenderVertexAdjacency.Num() == 0)
    {
        BuildRenderVertexAdjacency();
    }

    const int32 DepthLimit = FMath::Max(0, RingDepth);
    TBitArray<> Visited(false, VertexCount);

    TArray<int32> Current;
    Current.Reserve(PendingCrustAgeResetSeeds.Num());

    for (int32 Seed : PendingCrustAgeResetSeeds)
    {
        if (!RenderVertices.IsValidIndex(Seed))
        {
            continue;
        }

        Visited[Seed] = true;
        if (VertexCrustAge.IsValidIndex(Seed))
        {
            VertexCrustAge[Seed] = 0.0;
        }
        Current.Add(Seed);
    }

    for (int32 Depth = 0; Depth < DepthLimit; ++Depth)
    {
        if (Current.Num() == 0)
        {
            break;
        }

        TArray<int32> Next;
        for (int32 VertexIdx : Current)
        {
            if (!RenderVertices.IsValidIndex(VertexIdx))
            {
                continue;
            }

            const int32 Start = RenderVertexAdjacencyOffsets[VertexIdx];
            const int32 End = RenderVertexAdjacencyOffsets[VertexIdx + 1];

            for (int32 Offset = Start; Offset < End; ++Offset)
            {
                const int32 Neighbor = RenderVertexAdjacency.IsValidIndex(Offset)
                    ? RenderVertexAdjacency[Offset]
                    : INDEX_NONE;

                if (!RenderVertices.IsValidIndex(Neighbor) || Visited[Neighbor])
                {
                    continue;
                }

                Visited[Neighbor] = true;
                if (VertexCrustAge.IsValidIndex(Neighbor))
                {
                    VertexCrustAge[Neighbor] = 0.0;
                }
                Next.Add(Neighbor);
            }
        }

        Current = MoveTemp(Next);
    }

    PendingCrustAgeResetSeeds.Reset();
    PendingCrustAgeResetMask.Init(false, VertexCount);
}

void UTectonicSimulationService::ResetSimulation()
{
    CurrentTimeMy = 0.0;
    TotalStepsSimulated = 0;
    RetessellationCadenceStats.Reset();

#if WITH_EDITOR
    PendingOceanicGPUJobs.Empty();
#endif

    // Milestone 5: Clear all per-vertex arrays for deterministic resets
    VertexElevationValues.Empty();
    VertexErosionRates.Empty();
    VertexSedimentThickness.Empty();
    VertexCrustAge.Empty();
    RenderVertexBoundaryCache.Empty();

    // Milestone 6: Clear amplification arrays
    VertexRidgeDirections.Empty();
    VertexAmplifiedElevation.Empty();

    InvalidateRidgeDirectionCache();
    PendingCrustAgeResetSeeds.Reset();
    PendingCrustAgeResetMask.Reset();
    StepsSinceLastVoronoiRefresh = 0;
    CachedVoronoiAssignments.Reset();
    bSkipNextVoronoiRefresh = true;

    // Milestone 4 Phase 5: Reset version counters for test isolation
    TopologyVersion = 0;
    SurfaceDataVersion = 0;
    RetessellationCount = 0;
    StepsSinceLastRetessellationCheck = 0;
    bRetessellationInCooldown = false;
    LastRetessellationMaxDriftDegrees = 0.0;
    LastRetessellationBadTriangleRatio = 0.0;

    // Milestone 6 Task 1.3: Clear terranes on reset
    Terranes.Empty();
    NextTerraneID = 0;

    GenerateDefaultSphereSamples();

    // Milestone 2: Generate plate simulation state
    GenerateIcospherePlates();
    InitializeEulerPoles();
    BuildBoundaryAdjacencyMap();
    ValidateSolidAngleCoverage();

    // Classify boundaries before building caches so divergent edges seed ridge tangents immediately.
    UpdateBoundaryClassifications();

    // Milestone 3: Generate high-density render mesh
    GenerateRenderMesh();

    // Milestone 3 Task 3.1: Apply Lloyd relaxation BEFORE Voronoi mapping
    // Note: Lloyd uses render mesh vertices to compute Voronoi cells, so must run after GenerateRenderMesh()
    ApplyLloydRelaxation();

    // Milestone 3 Phase 2: Build Voronoi mapping (event-driven, refreshed after Lloyd)
    BuildVoronoiMapping();

    // Milestone 3 Task 2.2: Compute velocity field (event-driven, same trigger as Voronoi)
    ComputeVelocityField();

    // Milestone 3 Task 2.3: Initialize stress field (zero at start)
    InterpolateStressToVertices();

    // Milestone 3 Task 3.3: Capture initial plate positions for re-tessellation detection
    InitialPlateCentroids.SetNum(Plates.Num());
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        InitialPlateCentroids[i] = Plates[i].Centroid;
    }

    // Milestone 4 Task 2.1: Generate hotspots (deterministic from seed)
    GenerateHotspots();

    // Milestone 4 Task 1.2: Clear topology event log
    TopologyEvents.Empty();

    // Milestone 5: Initialize per-vertex arrays to match render vertex count
    // This ensures arrays are properly sized even when feature flags are disabled
    const int32 VertexCount = RenderVertices.Num();
    VertexElevationValues.SetNumZeroed(VertexCount);
    VertexErosionRates.SetNumZeroed(VertexCount);
    VertexSedimentThickness.SetNumZeroed(VertexCount);
    VertexCrustAge.SetNumZeroed(VertexCount);

    // Milestone 6 Task 2.1: Initialize amplification arrays
    VertexRidgeDirections.SetNum(VertexCount);
    for (int32 i = 0; i < VertexCount; ++i)
    {
        VertexRidgeDirections[i] = FVector3d::ZAxisVector; // Default fallback direction
    }
    VertexAmplifiedElevation.SetNumZeroed(VertexCount);

    MarkAllRidgeDirectionsDirty();
    ComputeRidgeDirections();

    // M5 Phase 3 fix: Seed elevation baselines from plate crust type for order independence
    // This ensures oceanic dampening/erosion work regardless of execution order
    int32 OceanicCount = 0, ContinentalCount = 0;
    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const int32 PlateIdx = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;
        if (PlateIdx != INDEX_NONE && Plates.IsValidIndex(PlateIdx))
        {
            const bool bIsOceanic = (Plates[PlateIdx].CrustType == ECrustType::Oceanic);
            if (bIsOceanic)
            {
                /**
                 * Paper-compliant oceanic baseline (Appendix A):
                 * - Abyssal plains: -6000m (zᵇ)
                 * - Ridges: -1000m (zᵀ)
                 * Initialize to abyssal depth; ridges will form at divergent boundaries via oceanic crust generation.
                 * Age-subsidence formula will deepen crust from ridge depth toward abyssal depth over time.
                 */
                VertexElevationValues[VertexIdx] = PaperElevationConstants::AbyssalPlainDepth_m;
                OceanicCount++;
            }
            else
            {
                /**
                 * Paper-compliant continental baseline (Appendix A):
                 * Continents start at sea level (0m) and rise via subduction uplift, collision, and erosion.
                 * The +250m offset used previously compressed the achievable relief range.
                 */
                VertexElevationValues[VertexIdx] = PaperElevationConstants::ContinentalBaseline_m;
                ContinentalCount++;
            }
        }
    }
    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[DEBUG] ResetSimulation: Initialized %d vertices (%d oceanic @ -6000m, %d continental @ 0m)"),
        VertexCount, OceanicCount, ContinentalCount);

#if UE_BUILD_DEVELOPMENT
    LogPlateElevationMismatches(TEXT("Reset"));
#endif

    // DEBUG: Check vertex 0 specifically
    if (VertexCount > 0 && VertexPlateAssignments.Num() > 0)
    {
        const int32 Plate0 = VertexPlateAssignments[0];
        const double Elev0 = VertexElevationValues[0];
        const bool bOceanic0 = (Plate0 != INDEX_NONE && Plates.IsValidIndex(Plate0)) ?
            (Plates[Plate0].CrustType == ECrustType::Oceanic) : false;
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[DEBUG] Vertex 0: Plate=%d, Oceanic=%s, Elevation=%.2f m"),
            Plate0, bOceanic0 ? TEXT("YES") : TEXT("NO"), Elev0);
    }

    // Milestone 5 Task 1.3: Initialize history stack with initial state
    HistoryStack.Empty();
    CurrentHistoryIndex = -1;
    CaptureHistorySnapshot();
    BumpOceanicAmplificationSerial();
    UE_LOG(LogPlanetaryCreation, Log, TEXT("ResetSimulation: History stack initialized with initial state"));
}

void UTectonicSimulationService::AdvanceSteps(int32 StepCount)
{
    if (StepCount <= 0)
    {
        return;
    }

    TotalStepsSimulated += StepCount;

    // Milestone 3 Task 4.5: Track step performance
    const double StartTime = FPlatformTime::Seconds();

    constexpr double StepDurationMy = 2.0; // Paper defines delta t = 2 My per iteration
    constexpr double StageBBudgetSeconds = 2.0; // Soft budget for Stage B passes at high LOD

    ProcessPendingOceanicGPUReadbacks(false);
        ProcessPendingContinentalGPUReadbacks(false);

    for (int32 Step = 0; Step < StepCount; ++Step)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(TectonicStep);
        const double StepLoopStart = FPlatformTime::Seconds();
        const int32 AbsoluteStep = (TotalStepsSimulated - StepCount) + (Step + 1);

        double ErosionTime = 0.0;
        double SedimentTime = 0.0;
        double DampeningTime = 0.0;
        double BaselineInitTime = 0.0;
        double RidgeDirectionTime = 0.0;
        double OceanicCpuTime = 0.0;
        double OceanicGpuDispatchTime = 0.0;
        double ContinentalCpuTime = 0.0;
        double ContinentalGpuDispatchTime = 0.0;
        double GpuReadbackSeconds = 0.0;
        double CacheInvalidationSeconds = 0.0;
        bool bSurfaceDataChanged = false;
        bContinentalGPUResultWasApplied = false;
        bool bPendingOceanicGPUReadback = false;

#if WITH_EDITOR
        ProcessPendingOceanicGPUReadbacks(false, &GpuReadbackSeconds);
        ProcessPendingContinentalGPUReadbacks(false, &GpuReadbackSeconds);
#endif

#if UE_BUILD_DEVELOPMENT
        auto LogBoundaryCacheState = [this, AbsoluteStep](const TCHAR* PhaseLabel)
        {
            const int32 EntryCount = RenderVertexBoundaryCache.Num();
            int32 ValidTangents = 0;
            int32 DivergentCount = 0;
            int32 PlateMatchCount = 0;

            if (EntryCount > 0)
            {
                for (int32 VertexIdx = 0; VertexIdx < EntryCount; ++VertexIdx)
                {
                    const FRenderVertexBoundaryInfo& Info = RenderVertexBoundaryCache[VertexIdx];
                    if (!Info.bHasBoundary || Info.BoundaryTangent.IsNearlyZero())
                    {
                        continue;
                    }

                    ++ValidTangents;
                    if (Info.bIsDivergent)
                    {
                        ++DivergentCount;
                    }

                    if (VertexPlateAssignments.IsValidIndex(VertexIdx) &&
                        Info.SourcePlateID == VertexPlateAssignments[VertexIdx])
                    {
                        ++PlateMatchCount;
                    }
                }
            }

            UE_LOG(LogPlanetaryCreation, VeryVerbose,
                TEXT("[BoundaryCache][Step %d] %s: Entries=%d Valid=%d Divergent=%d PlateMatch=%d"),
                AbsoluteStep,
                PhaseLabel,
                EntryCount,
                ValidTangents,
                DivergentCount,
                PlateMatchCount);
        };

        LogBoundaryCacheState(TEXT("StartOfStep"));
#endif

        // Phase 2 Task 4: Migrate plate centroids via Euler pole rotation
        MigratePlateCentroids(StepDurationMy);

#if UE_BUILD_DEVELOPMENT
        LogBoundaryCacheState(TEXT("AfterMigratePlateCentroids"));
#endif

        // Milestone 6 Task 1.2: Update terrane positions (migrate with carrier plates)
        UpdateTerranePositions(StepDurationMy);

        // Phase 2 Task 5: Update boundary classifications based on relative velocities
        UpdateBoundaryClassifications();

#if UE_BUILD_DEVELOPMENT
        LogBoundaryCacheState(TEXT("AfterUpdateBoundaryClassifications"));
#endif

        // Milestone 6 Task 1.2: Detect terrane collisions (after boundary updates)
        DetectTerraneCollisions();

        // Milestone 6 Task 1.3: Automatically reattach colliding terranes (after collision detection)
        ProcessTerraneReattachments();

        // Milestone 3 Task 2.3: Update stress at boundaries (cosmetic visualization)
        UpdateBoundaryStress(StepDurationMy);

        // Milestone 4 Task 1.3: Update boundary lifecycle states (Nascent/Active/Dormant)
        UpdateBoundaryStates(StepDurationMy);

        // Milestone 4 Task 2.2: Update rift progression for divergent boundaries
        UpdateRiftProgression(StepDurationMy);

        // Milestone 4 Task 2.1: Update hotspot drift in mantle frame
        UpdateHotspotDrift(StepDurationMy);

        CurrentTimeMy += StepDurationMy;

        StepsSinceLastVoronoiRefresh++;

        // Milestone 3 Task 2.3: Interpolate stress to render vertices (per step for accurate snapshots)
        InterpolateStressToVertices();

        // Milestone 4 Task 2.3: Compute thermal field from hotspots + subduction
        ComputeThermalField();

        // Milestone 4 Task 2.1: Apply hotspot thermal contribution to stress field
        ApplyHotspotThermalContribution();

        // Milestone 5 Task 2.1: Apply continental erosion
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(ContinentalErosion);
            const double BlockStart = FPlatformTime::Seconds();
            ApplyContinentalErosion(StepDurationMy);
            ErosionTime += FPlatformTime::Seconds() - BlockStart;
            bSurfaceDataChanged = true;
        }
#if UE_BUILD_DEVELOPMENT
        {
            const FString Label = FString::Printf(TEXT("Step%d-AfterContinentalErosion"), AbsoluteStep);
            LogPlateElevationMismatches(*Label);
        }
#endif

        // Milestone 5 Task 2.2: Redistribute eroded sediment
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(SedimentTransport);
            const double BlockStart = FPlatformTime::Seconds();
            ApplySedimentTransport(StepDurationMy);
            SedimentTime += FPlatformTime::Seconds() - BlockStart;
            bSurfaceDataChanged = true;
        }
#if UE_BUILD_DEVELOPMENT
        {
            const FString Label = FString::Printf(TEXT("Step%d-AfterSedimentTransport"), AbsoluteStep);
            LogPlateElevationMismatches(*Label);
        }
#endif

        // Milestone 5 Task 2.3: Apply oceanic dampening
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(OceanicDampening);
            const double BlockStart = FPlatformTime::Seconds();
            ApplyOceanicDampening(StepDurationMy);
            DampeningTime += FPlatformTime::Seconds() - BlockStart;
            bSurfaceDataChanged = true;
        }
#if UE_BUILD_DEVELOPMENT
        {
            const FString Label = FString::Printf(TEXT("Step%d-AfterOceanicDampening"), AbsoluteStep);
            LogPlateElevationMismatches(*Label);
        }
#endif

        // Ensure amplified elevation starts from current base elevation before Stage B passes run (or remain disabled).
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(AmplificationBaseline);
            const double BlockStart = FPlatformTime::Seconds();
            InitializeAmplifiedElevationBaseline();
            BaselineInitTime += FPlatformTime::Seconds() - BlockStart;
        }

        // Milestone 6 Task 2.1: Apply Stage B oceanic amplification
        // Must run after erosion/dampening to use base elevations as input
        // Must run before topology changes to ensure valid vertex indices
        // Skip if controller is handling GPU preview mode (avoids redundant CPU work)
        if (Parameters.bEnableOceanicAmplification && Parameters.RenderSubdivisionLevel >= Parameters.MinAmplificationLOD)
        {
            bool bUpdatedRidgeDirections = false;
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(UpdateRidgeDirectionsStageB);
                const double BlockStart = FPlatformTime::Seconds();
                bUpdatedRidgeDirections = RefreshRidgeDirectionsIfNeeded();
                if (bUpdatedRidgeDirections)
                {
                    RidgeDirectionTime += FPlatformTime::Seconds() - BlockStart;
                }
            }

#if UE_BUILD_DEVELOPMENT
            if (bUpdatedRidgeDirections)
            {
                LogBoundaryCacheState(TEXT("AfterComputeRidgeDirections"));
            }
#endif

            if (!Parameters.bSkipCPUAmplification)
            {
                bool bUsedGPU = false;
#if WITH_EDITOR
                if (ShouldUseGPUAmplification())
                {
                    // Milestone 6 GPU: Lazy initialization on first use
                    InitializeGPUExemplarResources();

                    TRACE_CPUPROFILER_EVENT_SCOPE(OceanicAmplificationGPU);
                    const double BlockStart = FPlatformTime::Seconds();
                    bUsedGPU = ApplyOceanicAmplificationGPU();
                    if (bUsedGPU)
                    {
                        OceanicGpuDispatchTime += FPlatformTime::Seconds() - BlockStart;
                        ProcessPendingOceanicGPUReadbacks(false, &GpuReadbackSeconds);
                        bPendingOceanicGPUReadback = PendingOceanicGPUJobs.Num() > 0;
#if WITH_EDITOR
                        if (bPendingOceanicGPUReadback)
                        {
                            const bool bAppliedSnapshot = EnsureLatestOceanicSnapshotApplied();
                            if (bAppliedSnapshot)
                            {
                                bSurfaceDataChanged = true;
                            }
                            bPendingOceanicGPUReadback = PendingOceanicGPUJobs.Num() > 0;
                        }
#endif
                    }
                }
#endif
                if (!bUsedGPU)
                {
                    {
                        TRACE_CPUPROFILER_EVENT_SCOPE(OceanicAmplification);
                        const double BlockStart = FPlatformTime::Seconds();
                        ApplyOceanicAmplification();
                        OceanicCpuTime += FPlatformTime::Seconds() - BlockStart;
                        bSurfaceDataChanged = true;
                    }
                }
            }
        }

        // Milestone 6 Task 2.2: Apply Stage B continental amplification (exemplar-based)
        // Skip if controller is handling GPU preview mode (avoids redundant CPU work)
        if (Parameters.bEnableContinentalAmplification && Parameters.RenderSubdivisionLevel >= Parameters.MinAmplificationLOD && !Parameters.bSkipCPUAmplification)
        {
            if (bPendingOceanicGPUReadback)
            {
                // Continental amplification depends on oceanic outputs already being written back.
                ProcessPendingOceanicGPUReadbacks(false, &GpuReadbackSeconds);
#if WITH_EDITOR
                const bool bAppliedSnapshot = EnsureLatestOceanicSnapshotApplied();
                if (bAppliedSnapshot)
                {
                    bSurfaceDataChanged = true;
                }
                bPendingOceanicGPUReadback = PendingOceanicGPUJobs.Num() > 0;
#else
                bPendingOceanicGPUReadback = false;
#endif
            }

            bool bUsedGPU = false;
#if WITH_EDITOR
            if (ShouldUseGPUAmplification())
            {
                // Milestone 6 GPU: Lazy initialization on first use
                InitializeGPUExemplarResources();

                TRACE_CPUPROFILER_EVENT_SCOPE(ContinentalAmplificationGPU);
                const double BlockStart = FPlatformTime::Seconds();
                bUsedGPU = ApplyContinentalAmplificationGPU();
                if (bUsedGPU)
                {
                    ContinentalGpuDispatchTime += FPlatformTime::Seconds() - BlockStart;
                    ProcessPendingContinentalGPUReadbacks(false, &GpuReadbackSeconds);
                }
            }
#endif
            if (!bUsedGPU || !bContinentalGPUResultWasApplied)
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(ContinentalAmplification);
                const double BlockStart = FPlatformTime::Seconds();
                ApplyContinentalAmplification();
                const double BlockDuration = FPlatformTime::Seconds() - BlockStart;
                ContinentalCpuTime += BlockDuration;
                CacheInvalidationSeconds += LastContinentalCacheBuildSeconds;
                bSurfaceDataChanged = true;
            }
        }
#if UE_BUILD_DEVELOPMENT
        {
            const FString Label = FString::Printf(TEXT("Step%d-AfterStageB"), AbsoluteStep);
            LogPlateElevationMismatches(*Label);
        }
#endif

        const int32 VoronoiInterval = FMath::Max(1, Parameters.VoronoiRefreshIntervalSteps);
        if (StepsSinceLastVoronoiRefresh >= VoronoiInterval)
        {
            if (bSkipNextVoronoiRefresh)
            {
                bSkipNextVoronoiRefresh = false;
                bLastVoronoiForcedFullRidgeUpdate = false;
                LastVoronoiReassignedCount = 0;
                StepsSinceLastVoronoiRefresh = 0;
            }
            else
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(VoronoiRefresh);
                const double VoronoiStart = FPlatformTime::Seconds();
                BuildVoronoiMapping();
#if UE_BUILD_DEVELOPMENT
                LogBoundaryCacheState(TEXT("AfterBuildVoronoiMapping"));
#endif
                ComputeVelocityField();
                InterpolateStressToVertices();
                StepsSinceLastVoronoiRefresh = 0;
                bSurfaceDataChanged = true;
#if UE_BUILD_DEVELOPMENT
                UE_LOG(LogPlanetaryCreation, VeryVerbose,
                    TEXT("[AdvanceSteps] Recomputing ridge directions after Voronoi refresh (reassigned=%d, full=%s)"),
                    LastVoronoiReassignedCount,
                    bLastVoronoiForcedFullRidgeUpdate ? TEXT("yes") : TEXT("no"));
#endif
                {
                    TRACE_CPUPROFILER_EVENT_SCOPE(ComputeRidgeDirectionsPostVoronoi);
                    ComputeRidgeDirections();
                }

                // Re-run Stage B amplification with refreshed ridge vectors so stored elevations match final directions.
                {
                    TRACE_CPUPROFILER_EVENT_SCOPE(PostVoronoiAmplificationBaseline);
                    InitializeAmplifiedElevationBaseline();
                }

                if (Parameters.bEnableOceanicAmplification && Parameters.RenderSubdivisionLevel >= Parameters.MinAmplificationLOD && !Parameters.bSkipCPUAmplification)
                {
                    TRACE_CPUPROFILER_EVENT_SCOPE(PostVoronoiOceanicAmplification);
                    ApplyOceanicAmplification();
                }

                if (Parameters.bEnableContinentalAmplification && Parameters.RenderSubdivisionLevel >= Parameters.MinAmplificationLOD && !Parameters.bSkipCPUAmplification)
                {
                    TRACE_CPUPROFILER_EVENT_SCOPE(PostVoronoiContinentalAmplification);
                    ApplyContinentalAmplification();
                }

                bSurfaceDataChanged = true;
                UE_LOG(LogPlanetaryCreation, VeryVerbose, TEXT("[Voronoi] Refresh completed in %.2f ms (interval=%d)"),
                    (FPlatformTime::Seconds() - VoronoiStart) * 1000.0,
                    VoronoiInterval);
#if UE_BUILD_DEVELOPMENT
                {
                    const FString Label = FString::Printf(TEXT("Step%d-AfterVoronoiRefresh"), AbsoluteStep);
                    LogPlateElevationMismatches(*Label);
                }
#endif
            }
        }

        // Milestone 4 Task 1.2: Detect and execute plate splits/merges
        if (Parameters.bEnablePlateTopologyChanges)
        {
            DetectAndExecutePlateSplits();
            DetectAndExecutePlateMerges();
        }

        // Milestone 4 Task 1.1: Perform re-tessellation if plates have drifted beyond threshold
        if (Parameters.bEnableDynamicRetessellation)
        {
            MaybePerformRetessellation();
        }
        else
        {
            // M3 compatibility: Just check and log (don't rebuild)
            CheckRetessellationNeeded();
        }

        // Milestone 5 Task 1.3: Capture history snapshot after each individual step for undo/redo
        if (bSurfaceDataChanged)
        {
            SurfaceDataVersion++;
        }
        CaptureHistorySnapshot();

        const double StepElapsed = FPlatformTime::Seconds() - StepLoopStart;
        const double OceanicCombinedTime = OceanicCpuTime + OceanicGpuDispatchTime;
        const double ContinentalCombinedTime = ContinentalCpuTime + ContinentalGpuDispatchTime;
        const double StageBDuration = BaselineInitTime + RidgeDirectionTime + OceanicCombinedTime + ContinentalCombinedTime + GpuReadbackSeconds + CacheInvalidationSeconds;

        UE_LOG(LogPlanetaryCreation, Log,
            TEXT("[StepTiming] Step %d | LOD L%d | Total %.2f ms | StageB %.2f ms (Baseline %.2f | Ridge %.2f [Dirty %d | Updated %d | CacheHits %d | Missing %d | PoorAlign %d | Gradient %d] | Voronoi %d%s | Oceanic %.2f | Continental %.2f | Readback %.2f) | Erosion %.2f ms | Sediment %.2f ms | Dampening %.2f ms"),
            AbsoluteStep,
            Parameters.RenderSubdivisionLevel,
            StepElapsed * 1000.0,
            StageBDuration * 1000.0,
            BaselineInitTime * 1000.0,
            RidgeDirectionTime * 1000.0,
            LastRidgeDirtyVertexCount,
            LastRidgeDirectionUpdateCount,
            LastRidgeCacheHitCount,
            LastRidgeMissingTangentCount,
            LastRidgePoorAlignmentCount,
            LastRidgeGradientFallbackCount,
            LastVoronoiReassignedCount,
            bLastVoronoiForcedFullRidgeUpdate ? TEXT("*") : TEXT(""),
            OceanicCombinedTime * 1000.0,
            ContinentalCombinedTime * 1000.0,
            GpuReadbackSeconds * 1000.0,
            ErosionTime * 1000.0,
            SedimentTime * 1000.0,
            DampeningTime * 1000.0);

        if (StageBDuration > StageBBudgetSeconds)
        {
            UE_LOG(LogPlanetaryCreation, Warning,
                TEXT("[StageB][Perf] Step %d LOD L%d took %.2f s (StageB %.2f s | Baseline %.2f s, Ridge %.2f s, Oceanic %.2f s, Continental %.2f s, Readback %.2f s | Erosion %.2f s, Sediment %.2f s, Dampening %.2f s)"),
                Step + 1,
                Parameters.RenderSubdivisionLevel,
                StepElapsed,
                StageBDuration,
                BaselineInitTime,
                RidgeDirectionTime,
                OceanicCombinedTime,
                ContinentalCombinedTime,
                GpuReadbackSeconds,
                ErosionTime,
                SedimentTime,
                DampeningTime);
        }

        FStageBProfile Profile;
        Profile.BaselineMs = BaselineInitTime * 1000.0;
        Profile.RidgeMs = RidgeDirectionTime * 1000.0;
        Profile.OceanicCPUMs = OceanicCpuTime * 1000.0;
        Profile.OceanicGPUMs = OceanicGpuDispatchTime * 1000.0;
        Profile.ContinentalCPUMs = ContinentalCpuTime * 1000.0;
        Profile.ContinentalGPUMs = ContinentalGpuDispatchTime * 1000.0;
        Profile.GpuReadbackMs = GpuReadbackSeconds * 1000.0;
        Profile.CacheInvalidationMs = CacheInvalidationSeconds * 1000.0;
        Profile.RidgeDirtyVertices = LastRidgeDirtyVertexCount;
        Profile.RidgeUpdatedVertices = LastRidgeDirectionUpdateCount;
        Profile.RidgeCacheHits = LastRidgeCacheHitCount;
        Profile.RidgeMissingTangents = LastRidgeMissingTangentCount;
        Profile.RidgePoorAlignment = LastRidgePoorAlignmentCount;
        Profile.RidgeGradientFallbacks = LastRidgeGradientFallbackCount;
        Profile.VoronoiReassignedVertices = LastVoronoiReassignedCount;
        Profile.bVoronoiForcedFullRidge = bLastVoronoiForcedFullRidgeUpdate;
        LatestStageBProfile = Profile;

        const int32 StageBLogMode = CVarPlanetaryCreationStageBProfiling.GetValueOnAnyThread();
        if (StageBLogMode > 0)
        {
            UE_LOG(LogPlanetaryCreation, Log,
                TEXT("[StageB][Profile] Step %d | LOD L%d | Baseline %.2f ms | Ridge %.2f ms (Dirty %d | Updated %d | CacheHits %d | Missing %d | PoorAlign %d | Gradient %d) | Voronoi %d%s | OceanicCPU %.2f ms | OceanicGPU %.2f ms | ContinentalCPU %.2f ms | ContinentalGPU %.2f ms | Readback %.2f ms | Cache %.2f ms | Total %.2f ms"),
                AbsoluteStep,
                Parameters.RenderSubdivisionLevel,
                Profile.BaselineMs,
                Profile.RidgeMs,
                Profile.RidgeDirtyVertices,
                Profile.RidgeUpdatedVertices,
                Profile.RidgeCacheHits,
                Profile.RidgeMissingTangents,
                Profile.RidgePoorAlignment,
                Profile.RidgeGradientFallbacks,
                Profile.VoronoiReassignedVertices,
                Profile.bVoronoiForcedFullRidge ? TEXT("*") : TEXT(""),
                Profile.OceanicCPUMs,
                Profile.OceanicGPUMs,
                Profile.ContinentalCPUMs,
                Profile.ContinentalGPUMs,
                Profile.GpuReadbackMs,
                Profile.CacheInvalidationMs,
                Profile.TotalMs());

        }

        if (StageBLogMode > 0)
        {
            const FContinentalCacheProfileMetrics& CacheMetrics = LastContinentalCacheProfileMetrics;
            if (CacheMetrics.TotalSeconds > 0.0 || CacheMetrics.ContinentalVertexCount > 0)
            {
                UE_LOG(LogPlanetaryCreation, Log,
                    TEXT("[StageB][CacheProfile] ContinentalCache Total %.2f ms | Classification %.2f ms | Exemplar %.2f ms | ContinentalVerts %d | ExemplarVerts %d"),
                    CacheMetrics.TotalSeconds * 1000.0,
                    CacheMetrics.ClassificationSeconds * 1000.0,
                    CacheMetrics.ExemplarSelectionSeconds * 1000.0,
                    CacheMetrics.ContinentalVertexCount,
                    CacheMetrics.ExemplarAssignmentCount);
            }
        }
    }

    // Milestone 3 Task 4.5: Record step time for UI display
    const double EndTime = FPlatformTime::Seconds();
    LastStepTimeMs = (EndTime - StartTime) * 1000.0;

    ProcessPendingOceanicGPUReadbacks(false);
    ProcessPendingContinentalGPUReadbacks(false);
}

void UTectonicSimulationService::SetSkipCPUAmplification(bool bInSkip)
{
    Parameters.bSkipCPUAmplification = bInSkip;
}

void UTectonicSimulationService::SetParameters(const FTectonicSimulationParameters& NewParams)
{
    if (Parameters.VisualizationMode != NewParams.VisualizationMode)
    {
        FTectonicSimulationParameters ComparableParams = NewParams;
        ComparableParams.VisualizationMode = Parameters.VisualizationMode;
        ComparableParams.bEnableHeightmapVisualization = Parameters.bEnableHeightmapVisualization;

        if (FMemory::Memcmp(&ComparableParams, &Parameters, sizeof(FTectonicSimulationParameters)) == 0)
        {
            SetVisualizationMode(NewParams.VisualizationMode);
            return;
        }
    }
    else if (Parameters.bEnableHeightmapVisualization != NewParams.bEnableHeightmapVisualization)
    {
        FTectonicSimulationParameters ComparableParams = NewParams;
        ComparableParams.VisualizationMode = Parameters.VisualizationMode;
        ComparableParams.bEnableHeightmapVisualization = Parameters.bEnableHeightmapVisualization;

        if (FMemory::Memcmp(&ComparableParams, &Parameters, sizeof(FTectonicSimulationParameters)) == 0)
        {
            SetHeightmapVisualizationEnabled(NewParams.bEnableHeightmapVisualization);
            return;
        }
    }

    Parameters = NewParams;
    Parameters.bEnableHeightmapVisualization = (Parameters.VisualizationMode == ETectonicVisualizationMode::Elevation);

    // M5 Phase 3: Validate and clamp PlanetRadius to prevent invalid simulations
    const double MinRadius = 10000.0;   // 10 km (minimum for asteroid-like bodies)
    const double MaxRadius = 10000000.0; // 10,000 km (smaller than Jupiter)

    if (Parameters.PlanetRadius < MinRadius || Parameters.PlanetRadius > MaxRadius)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("PlanetRadius %.0f m outside valid range [%.0f, %.0f]. Clamping to valid range."),
            Parameters.PlanetRadius, MinRadius, MaxRadius);
        Parameters.PlanetRadius = FMath::Clamp(Parameters.PlanetRadius, MinRadius, MaxRadius);
    }

    Parameters.RetessellationCheckIntervalSteps = FMath::Max(1, Parameters.RetessellationCheckIntervalSteps);
    Parameters.RetessellationMinTriangleAngleDegrees = FMath::Clamp(Parameters.RetessellationMinTriangleAngleDegrees, 1.0, 60.0);
    Parameters.RetessellationBadTriangleRatioThreshold = FMath::Clamp(Parameters.RetessellationBadTriangleRatioThreshold, 0.0, 1.0);
    Parameters.RetessellationThresholdDegrees = FMath::Clamp(Parameters.RetessellationThresholdDegrees, 0.0, 179.0);
    Parameters.RidgeDirectionDirtyRingDepth = FMath::Clamp(Parameters.RidgeDirectionDirtyRingDepth, 0, 8);

    if (Parameters.RetessellationTriggerDegrees < Parameters.RetessellationThresholdDegrees)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("RetessellationTriggerDegrees %.2f° < cooldown threshold %.2f°. Clamping trigger to cooldown."),
            Parameters.RetessellationTriggerDegrees,
            Parameters.RetessellationThresholdDegrees);
        Parameters.RetessellationTriggerDegrees = Parameters.RetessellationThresholdDegrees;
    }

    Parameters.RetessellationTriggerDegrees = FMath::Clamp(Parameters.RetessellationTriggerDegrees, Parameters.RetessellationThresholdDegrees, 179.0);

    ResetSimulation();
}

void UTectonicSimulationService::SetHeightmapVisualizationEnabled(bool bEnabled)
{
    const ETectonicVisualizationMode TargetMode = bEnabled ? ETectonicVisualizationMode::Elevation : ETectonicVisualizationMode::PlateColors;
    if (Parameters.VisualizationMode == TargetMode && Parameters.bEnableHeightmapVisualization == bEnabled)
    {
        return;
    }

    SetVisualizationMode(TargetMode);
}

void UTectonicSimulationService::SetVisualizationMode(ETectonicVisualizationMode Mode)
{
    if (Parameters.VisualizationMode == Mode)
    {
        return;
    }

    Parameters.VisualizationMode = Mode;
    Parameters.bEnableHeightmapVisualization = (Mode == ETectonicVisualizationMode::Elevation);

    // Increment surface data version so cached LODs rebuild with updated vertex colors.
    SurfaceDataVersion++;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[Visualization] Mode set to %d (SurfaceVersion=%d)"),
        static_cast<int32>(Parameters.VisualizationMode), SurfaceDataVersion);

#if WITH_EDITOR
    if (IConsoleVariable* VisualizationCVar = CVarPlanetaryCreationVisualizationMode.AsVariable())
    {
        VisualizationCVar->Set(static_cast<int32>(Parameters.VisualizationMode), ECVF_SetByCode);
    }
#endif
}

void UTectonicSimulationService::SetAutomaticLODEnabled(bool bEnabled)
{
    if (Parameters.bEnableAutomaticLOD == bEnabled)
    {
        return;
    }

    Parameters.bEnableAutomaticLOD = bEnabled;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[LOD] Automatic LOD %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
}

void UTectonicSimulationService::SetRenderSubdivisionLevel(int32 NewLevel)
{
    // Milestone 4 Phase 4.1: Update only the render subdivision level without resetting simulation
    // This preserves all tectonic state (plates, stress, rifts, hotspots, etc.) while changing LOD

    if (Parameters.RenderSubdivisionLevel == NewLevel)
    {
        return; // No change needed
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[LOD] Updating render subdivision level: L%d → L%d (preserving simulation state)"),
        Parameters.RenderSubdivisionLevel, NewLevel);

    // Update parameter
    Parameters.RenderSubdivisionLevel = NewLevel;

    // Regenerate only the render mesh at new subdivision level
    GenerateRenderMesh();

    // Rebuild Voronoi mapping (render vertices changed, but plate centroids unchanged)
    BuildVoronoiMapping();

    // Recompute velocity field for new render vertices
    ComputeVelocityField();

    StepsSinceLastVoronoiRefresh = 0;

    // Reinterpolate stress field to new render vertices
    InterpolateStressToVertices();

    // Milestone 4 Task 2.3: Recompute thermal field for new render vertices
    ComputeThermalField();

    // Rebuild Stage B amplification for the updated render mesh.
    RebuildStageBForCurrentLOD();

    BumpOceanicAmplificationSerial();

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[LOD] Render mesh regenerated at L%d: %d vertices, %d triangles"),
        NewLevel, RenderVertices.Num(), RenderTriangles.Num() / 3);
}

bool UTectonicSimulationService::ShouldUseGPUAmplification() const
{
#if WITH_EDITOR
    return CVarPlanetaryCreationUseGPUAmplification.GetValueOnGameThread() != 0 &&
        Parameters.RenderSubdivisionLevel >= Parameters.MinAmplificationLOD;
#else
    return false;
#endif
}

bool UTectonicSimulationService::ApplyOceanicAmplificationGPU()
{
#if WITH_EDITOR
    const bool bResult = PlanetaryCreation::GPU::ApplyOceanicAmplificationGPU(*this);
    if (bResult)
    {
        BumpOceanicAmplificationSerial();
    }
    return bResult;
#else
    return false;
#endif
}

bool UTectonicSimulationService::ApplyContinentalAmplificationGPU()
{
#if WITH_EDITOR
    return PlanetaryCreation::GPU::ApplyContinentalAmplificationGPU(*this);
#else
    return false;
#endif
}

void UTectonicSimulationService::GenerateDefaultSphereSamples()
{
    BaseSphereSamples.Reset();

    // Minimal placeholder: an octahedron on the unit sphere
    const TArray<FVector3d> SampleSeeds = {
        FVector3d(1.0, 0.0, 0.0),
        FVector3d(-1.0, 0.0, 0.0),
        FVector3d(0.0, 1.0, 0.0),
        FVector3d(0.0, -1.0, 0.0),
        FVector3d(0.0, 0.0, 1.0),
        FVector3d(0.0, 0.0, -1.0)
    };

    BaseSphereSamples.Reserve(SampleSeeds.Num());
    for (const FVector3d& Seed : SampleSeeds)
    {
        BaseSphereSamples.Add(Seed.GetSafeNormal());
    }

    BumpOceanicAmplificationSerial();
}

void UTectonicSimulationService::GenerateIcospherePlates()
{
    Plates.Reset();
    SharedVertices.Reset();

    // Phase 1 Task 1: Generate icosphere subdivision to approximate desired plate count
    // An icosahedron has 20 faces. Each subdivision level quadruples face count:
    // Level 0: 20 faces (baseline from paper, ~Earth's 7-15 plates)
    // Level 1: 80 faces (experimental high-resolution mode)
    // Level 2: 320 faces (experimental ultra-high resolution)
    // Level 3: 1280 faces (experimental maximum resolution)

    const int32 SubdivisionLevel = FMath::Clamp(Parameters.SubdivisionLevel, 0, 3);
    SubdivideIcosphere(SubdivisionLevel);

    // Assign plate IDs and initialize properties
    FRandomStream RNG(Parameters.Seed);
    const int32 NumPlates = Plates.Num();

    // Deterministic 70/30 oceanic/continental split (M5 Phase 3 fix)
    // Pre-compute desired oceanic count to guarantee mix even with unlucky seeds
    const int32 DesiredOceanic = FMath::RoundToInt(NumPlates * 0.7);

    // Create shuffled index array for randomness
    TArray<int32> PlateIndices;
    for (int32 i = 0; i < NumPlates; ++i)
    {
        PlateIndices.Add(i);
    }

    // Shuffle with seeded RNG for deterministic but varied assignments
    for (int32 i = NumPlates - 1; i > 0; --i)
    {
        const int32 j = RNG.RandRange(0, i);
        PlateIndices.Swap(i, j);
    }

    for (int32 i = 0; i < NumPlates; ++i)
    {
        FTectonicPlate& Plate = Plates[i];
        Plate.PlateID = i;

        // Calculate centroid from vertices
        FVector3d CentroidSum = FVector3d::ZeroVector;
        for (int32 VertexIdx : Plate.VertexIndices)
        {
            CentroidSum += SharedVertices[VertexIdx];
        }
        Plate.Centroid = (CentroidSum / static_cast<double>(Plate.VertexIndices.Num())).GetSafeNormal();

        // Assign crust type: 70% oceanic, 30% continental (deterministic)
        // First DesiredOceanic plates in shuffled order become oceanic
        const bool bIsOceanic = PlateIndices[i] < DesiredOceanic;
        Plate.CrustType = bIsOceanic ? ECrustType::Oceanic : ECrustType::Continental;
        Plate.CrustThickness = bIsOceanic ? 7.0 : 35.0; // Oceanic ~7km, Continental ~35km
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Generated %d plates from icosphere subdivision level %d"), NumPlates, SubdivisionLevel);
}

void UTectonicSimulationService::SubdivideIcosphere(int32 SubdivisionLevel)
{
    // Golden ratio for icosahedron vertex positioning
    const double Phi = (1.0 + FMath::Sqrt(5.0)) / 2.0;
    const double InvNorm = 1.0 / FMath::Sqrt(1.0 + Phi * Phi);

    // Base icosahedron vertices (12 vertices)
    TArray<FVector3d> Vertices = {
        FVector3d(-1,  Phi, 0).GetSafeNormal(),
        FVector3d( 1,  Phi, 0).GetSafeNormal(),
        FVector3d(-1, -Phi, 0).GetSafeNormal(),
        FVector3d( 1, -Phi, 0).GetSafeNormal(),
        FVector3d(0, -1,  Phi).GetSafeNormal(),
        FVector3d(0,  1,  Phi).GetSafeNormal(),
        FVector3d(0, -1, -Phi).GetSafeNormal(),
        FVector3d(0,  1, -Phi).GetSafeNormal(),
        FVector3d( Phi, 0, -1).GetSafeNormal(),
        FVector3d( Phi, 0,  1).GetSafeNormal(),
        FVector3d(-Phi, 0, -1).GetSafeNormal(),
        FVector3d(-Phi, 0,  1).GetSafeNormal()
    };

    // Base icosahedron faces (20 triangular faces)
    // Using right-hand winding order (counter-clockwise when viewed from outside)
    TArray<TArray<int32>> Faces = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
    };

    // Subdivide icosphere to requested level (reuse same logic as GenerateRenderMesh)
    for (int32 Level = 0; Level < SubdivisionLevel; ++Level)
    {
        TArray<TArray<int32>> NewFaces;
        TMap<TPair<int32, int32>, int32> MidpointCache;

        for (const TArray<int32>& Face : Faces)
        {
            // Get or create midpoints for each edge
            const int32 V0 = Face[0];
            const int32 V1 = Face[1];
            const int32 V2 = Face[2];

            const int32 A = GetMidpointIndex(V0, V1, MidpointCache, Vertices);
            const int32 B = GetMidpointIndex(V1, V2, MidpointCache, Vertices);
            const int32 C = GetMidpointIndex(V2, V0, MidpointCache, Vertices);

            // Split triangle into 4 smaller triangles
            NewFaces.Add({V0, A, C});
            NewFaces.Add({V1, B, A});
            NewFaces.Add({V2, C, B});
            NewFaces.Add({A, B, C});
        }

        Faces = MoveTemp(NewFaces);
    }

    // Store vertices in shared pool
    SharedVertices = Vertices;

    // Create one plate per face
    Plates.Reserve(Faces.Num());
    for (const TArray<int32>& Face : Faces)
    {
        FTectonicPlate Plate;
        Plate.VertexIndices = Face;
        Plates.Add(Plate);
    }

    BumpOceanicAmplificationSerial();
}

void UTectonicSimulationService::InitializeEulerPoles()
{
    // Phase 1 Task 2: Assign deterministic Euler poles to each plate
    FRandomStream RNG(Parameters.Seed + 1); // Offset seed for pole generation

    for (FTectonicPlate& Plate : Plates)
    {
        // Generate random axis on unit sphere
        const double Theta = RNG.FRand() * 2.0 * PI;
        const double Phi = FMath::Acos(2.0 * RNG.FRand() - 1.0);

        Plate.EulerPoleAxis = FVector3d(
            FMath::Sin(Phi) * FMath::Cos(Theta),
            FMath::Sin(Phi) * FMath::Sin(Theta),
            FMath::Cos(Phi)
        ).GetSafeNormal();

        // Angular velocity: random magnitude 0.01-0.1 radians per My (realistic tectonic speeds)
        // Earth's plates move ~1-10 cm/year → ~0.01-0.1 radians/My on Earth-scale sphere
        Plate.AngularVelocity = RNG.FRandRange(0.01, 0.1);
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Initialized Euler poles for %d plates"), Plates.Num());
}

void UTectonicSimulationService::BuildBoundaryAdjacencyMap()
{
    // Phase 1 Task 3: Build adjacency map from shared edges in icosphere topology
    Boundaries.Reset();

    // For each pair of plates, check if they share an edge
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        const FTectonicPlate& PlateA = Plates[i];

        for (int32 j = i + 1; j < Plates.Num(); ++j)
        {
            const FTectonicPlate& PlateB = Plates[j];

            // Find shared vertices between the two plates
            TArray<int32> SharedVerts;
            for (int32 VertA : PlateA.VertexIndices)
            {
                if (PlateB.VertexIndices.Contains(VertA))
                {
                    SharedVerts.Add(VertA);
                }
            }

            // If exactly 2 shared vertices, they form a boundary edge
            if (SharedVerts.Num() == 2)
            {
                FPlateBoundary Boundary;
                Boundary.SharedEdgeVertices = SharedVerts;
                Boundary.BoundaryType = EBoundaryType::Transform; // Default, updated in Phase 2

                // Store with sorted plate IDs as key
                const TPair<int32, int32> Key(PlateA.PlateID, PlateB.PlateID);
                Boundaries.Add(Key, Boundary);
            }
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Built boundary adjacency map with %d boundaries"), Boundaries.Num());
}

void UTectonicSimulationService::ValidateSolidAngleCoverage()
{
    // Phase 1 Task 1: Validate that plates cover the sphere (total solid angle ≈ 4π steradians)
    double TotalSolidAngle = 0.0;

    for (const FTectonicPlate& Plate : Plates)
    {
        // Approximate solid angle using spherical triangle formula (for triangular plates)
        // Ω = E (spherical excess), where E = A + B + C - π for triangle with angles A,B,C
        // For simplicity, use area approximation: Ω ≈ Area / R² (R=1 for unit sphere)

        if (Plate.VertexIndices.Num() == 3)
        {
            const FVector3d& V0 = SharedVertices[Plate.VertexIndices[0]];
            const FVector3d& V1 = SharedVertices[Plate.VertexIndices[1]];
            const FVector3d& V2 = SharedVertices[Plate.VertexIndices[2]];

            // Spherical excess formula: E = A + B + C - π
            // Using l'Huilier's theorem for numerical stability
            const double A = FMath::Acos(FVector3d::DotProduct(V1, V2));
            const double B = FMath::Acos(FVector3d::DotProduct(V2, V0));
            const double C = FMath::Acos(FVector3d::DotProduct(V0, V1));
            const double S = (A + B + C) / 2.0;

            const double TanQuarter = FMath::Sqrt(
                FMath::Tan(S / 2.0) *
                FMath::Tan((S - A) / 2.0) *
                FMath::Tan((S - B) / 2.0) *
                FMath::Tan((S - C) / 2.0)
            );

            const double SolidAngle = 4.0 * FMath::Atan(TanQuarter);
            TotalSolidAngle += SolidAngle;
        }
    }

    const double ExpectedSolidAngle = 4.0 * PI;
    const double Error = FMath::Abs(TotalSolidAngle - ExpectedSolidAngle) / ExpectedSolidAngle;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Solid angle validation: Total=%.6f, Expected=%.6f (4π), Error=%.4f%%"),
        TotalSolidAngle, ExpectedSolidAngle, Error * 100.0);

    if (Error > 0.01) // 1% tolerance
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("Solid angle coverage error exceeds 1%% tolerance"));
    }
}

void UTectonicSimulationService::MigratePlateCentroids(double DeltaTimeMy)
{
    // Phase 2 Task 4: Apply Euler pole rotation to each plate centroid
    // Rotation formula: v' = v + ω × v * Δt (for small angles)
    // For accuracy, use Rodrigues' rotation formula: v' = v*cos(θ) + (k × v)*sin(θ) + k*(k·v)*(1-cos(θ))
    // where k = normalized Euler pole axis, θ = angular velocity * Δt

    for (FTectonicPlate& Plate : Plates)
    {
        const double RotationAngle = Plate.AngularVelocity * DeltaTimeMy; // radians

        // Rodrigues' rotation formula
        const FVector3d& Axis = Plate.EulerPoleAxis; // Already normalized
        const FVector3d& V = Plate.Centroid;

        const double CosTheta = FMath::Cos(RotationAngle);
        const double SinTheta = FMath::Sin(RotationAngle);
        const double DotProduct = FVector3d::DotProduct(Axis, V);

        const FVector3d RotatedCentroid =
            V * CosTheta +
            FVector3d::CrossProduct(Axis, V) * SinTheta +
            Axis * DotProduct * (1.0 - CosTheta);

        Plate.Centroid = RotatedCentroid.GetSafeNormal(); // Keep on unit sphere

        // Log displacement for first few plates (debug)
        if (Plate.PlateID < 3)
        {
            const double DisplacementRadians = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(V, Plate.Centroid), -1.0, 1.0));
            UE_LOG(LogPlanetaryCreation, VeryVerbose, TEXT("Plate %d displaced by %.6f radians (%.2f km on Earth-scale)"),
                Plate.PlateID, DisplacementRadians, DisplacementRadians * 6370.0);
        }
    }
}

void UTectonicSimulationService::UpdateBoundaryClassifications()
{
    // Phase 2 Task 5: Classify boundaries based on relative plate velocities
    // Velocity at a point on plate = ω × r (angular velocity cross radius vector)
    // For boundary between plates A and B, compute velocities at boundary midpoint

    // Helper: Apply Rodrigues rotation to a vertex (shared with visualization)
    auto RotateVertex = [](const FVector3d& Vertex, const FVector3d& EulerPoleAxis, double TotalRotationAngle) -> FVector3d
    {
        const double CosTheta = FMath::Cos(TotalRotationAngle);
        const double SinTheta = FMath::Sin(TotalRotationAngle);
        const double DotProduct = FVector3d::DotProduct(EulerPoleAxis, Vertex);

        const FVector3d Rotated =
            Vertex * CosTheta +
            FVector3d::CrossProduct(EulerPoleAxis, Vertex) * SinTheta +
            EulerPoleAxis * DotProduct * (1.0 - CosTheta);

        return Rotated.GetSafeNormal();
    };

    int32 DivergentCount = 0;
    int32 ConvergentCount = 0;
    int32 TransformCount = 0;

    TArray<int32> DivergentSeeds;
    DivergentSeeds.Reserve(Boundaries.Num() * 2);
    TArray<int32> StateChangeSeeds;
    StateChangeSeeds.Reserve(Boundaries.Num() * 2);

    bool bChangedBoundaryTypes = false;

    for (auto& BoundaryPair : Boundaries)
    {
        const TPair<int32, int32>& PlateIDs = BoundaryPair.Key;
        FPlateBoundary& Boundary = BoundaryPair.Value;

        // Find the two plates
        const FTectonicPlate* PlateA = Plates.FindByPredicate([ID = PlateIDs.Key](const FTectonicPlate& P) { return P.PlateID == ID; });
        const FTectonicPlate* PlateB = Plates.FindByPredicate([ID = PlateIDs.Value](const FTectonicPlate& P) { return P.PlateID == ID; });

        if (!PlateA || !PlateB || Boundary.SharedEdgeVertices.Num() != 2)
        {
            continue;
        }

        // Rotate boundary vertices to current simulation time for accurate classification
        const FVector3d& V0_Original = SharedVertices[Boundary.SharedEdgeVertices[0]];
        const FVector3d& V1_Original = SharedVertices[Boundary.SharedEdgeVertices[1]];

        const double RotationAngleA = PlateA->AngularVelocity * CurrentTimeMy;
        const double RotationAngleB = PlateB->AngularVelocity * CurrentTimeMy;

        const FVector3d V0_FromA = RotateVertex(V0_Original, PlateA->EulerPoleAxis, RotationAngleA);
        const FVector3d V1_FromA = RotateVertex(V1_Original, PlateA->EulerPoleAxis, RotationAngleA);
        const FVector3d V0_FromB = RotateVertex(V0_Original, PlateB->EulerPoleAxis, RotationAngleB);
        const FVector3d V1_FromB = RotateVertex(V1_Original, PlateB->EulerPoleAxis, RotationAngleB);

        // Average both plate rotations so the midpoint stays between drifting plates.
        const FVector3d V0_Current = ((V0_FromA + V0_FromB) * 0.5).GetSafeNormal();
        const FVector3d V1_Current = ((V1_FromA + V1_FromB) * 0.5).GetSafeNormal();

        if (V0_Current.IsNearlyZero() || V1_Current.IsNearlyZero())
        {
            continue;
        }

        const FVector3d BoundaryMidpoint = ((V0_Current + V1_Current) * 0.5).GetSafeNormal();
        if (BoundaryMidpoint.IsNearlyZero())
        {
            continue;
        }

        // Compute velocity at boundary for each plate: v = ω × r
        // ω is the angular velocity vector = AngularVelocity * EulerPoleAxis
        const FVector3d OmegaA = PlateA->EulerPoleAxis * PlateA->AngularVelocity;
        const FVector3d OmegaB = PlateB->EulerPoleAxis * PlateB->AngularVelocity;

        const FVector3d VelocityA = FVector3d::CrossProduct(OmegaA, BoundaryMidpoint);
        const FVector3d VelocityB = FVector3d::CrossProduct(OmegaB, BoundaryMidpoint);

        // Relative velocity: vRel = vA - vB
        const FVector3d RelativeVelocity = VelocityA - VelocityB;
        Boundary.RelativeVelocity = RelativeVelocity.Length();

        // Build a boundary normal that is tangent to the sphere and consistently oriented.
        const FVector3d EdgeVector = (V1_Current - V0_Current).GetSafeNormal();
        if (EdgeVector.IsNearlyZero())
        {
            continue;
        }

        // Project Plate A's centroid direction onto the tangent plane so the sign check
        // is unaffected by radial components (which would otherwise flip classification).
        const FVector3d PlateATangent = (PlateA->Centroid - FVector3d::DotProduct(PlateA->Centroid, BoundaryMidpoint) * BoundaryMidpoint);

        FVector3d BoundaryNormal = FVector3d::CrossProduct(BoundaryMidpoint, EdgeVector);

        if (!BoundaryNormal.Normalize())
        {
            continue; // Degenerate geometry, skip classification this frame
        }

        FVector3d PlateTangentNormalized = PlateATangent;
        const bool bHasPlateTangent = PlateTangentNormalized.Normalize();

        // Ensure the normal points toward Plate A's side of the boundary so the
        // dot(RelativeVelocity, BoundaryNormal) sign matches physical intuition.
        if (bHasPlateTangent && FVector3d::DotProduct(BoundaryNormal, PlateTangentNormalized) < 0.0)
        {
            BoundaryNormal *= -1.0;
        }

        // Project relative velocity onto boundary normal
        const double NormalComponent = FVector3d::DotProduct(RelativeVelocity, BoundaryNormal);

        const EBoundaryType PreviousType = Boundary.BoundaryType;

        // Classify boundary
        const double ClassificationThreshold = 0.001; // Radians/My threshold
        EBoundaryType NewType = EBoundaryType::Transform;
        if (NormalComponent > ClassificationThreshold)
        {
            NewType = EBoundaryType::Divergent; // Plates separating
            DivergentCount++;
        }
        else if (NormalComponent < -ClassificationThreshold)
        {
            NewType = EBoundaryType::Convergent; // Plates colliding
            ConvergentCount++;
        }
        else
        {
            TransformCount++;
        }

        if (NewType != PreviousType)
        {
            StateChangeSeeds.Append(Boundary.SharedEdgeVertices);
            bChangedBoundaryTypes = true;
        }

        Boundary.BoundaryType = NewType;

        if (NewType == EBoundaryType::Divergent)
        {
            DivergentSeeds.Append(Boundary.SharedEdgeVertices);
        }
    }

    UE_LOG(LogPlanetaryCreation, VeryVerbose, TEXT("Boundary classification: %d divergent, %d convergent, %d transform"),
        DivergentCount, ConvergentCount, TransformCount);

    const int32 RidgeRingDepth = FMath::Max(0, Parameters.RidgeDirectionDirtyRingDepth);
    if (DivergentSeeds.Num() > 0 || StateChangeSeeds.Num() > 0)
    {
        TArray<int32> DirtySeeds;
        DirtySeeds.Reserve(DivergentSeeds.Num() + StateChangeSeeds.Num());
        DirtySeeds.Append(StateChangeSeeds);
        DirtySeeds.Append(DivergentSeeds);
        MarkRidgeRingDirty(DirtySeeds, RidgeRingDepth);
        EnqueueCrustAgeResetSeeds(DivergentSeeds);
    }

    if (bChangedBoundaryTypes)
    {
        InvalidatePlateBoundarySummaries();
    }
}

void UTectonicSimulationService::ExportMetricsToCSV()
{
    // Phase 4 Task 9: Export simulation metrics to CSV for validation and analysis

    // Create output directory if it doesn't exist
    const FString OutputDir = FPaths::ProjectSavedDir() / TEXT("TectonicMetrics");
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*OutputDir))
    {
        PlatformFile.CreateDirectory(*OutputDir);
    }

    // Generate timestamped filename
    const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    const FString Filename = FString::Printf(TEXT("TectonicMetrics_Seed%d_Step%d_%s.csv"),
        Parameters.Seed,
        static_cast<int32>(CurrentTimeMy / 2.0), // Step count
        *Timestamp);
    const FString FilePath = OutputDir / Filename;

    // Build CSV content
    TArray<FString> CSVLines;

    // Milestone 4 Task 1.3/2.1/2.2/2.3: CSV v3.0 schema version header
    CSVLines.Add(TEXT("# Planetary Creation Tectonic Simulation Metrics"));
    CSVLines.Add(TEXT("# CSV Schema Version: 3.0"));
    CSVLines.Add(TEXT("# Changes from v2.0: Added BoundaryState, StateTransitionTime, DivergentDuration, ConvergentDuration, ThermalFlux (boundary section)"));
    CSVLines.Add(TEXT("#                     Added TopologyEvents section (splits/merges)"));
    CSVLines.Add(TEXT("#                     Added Hotspots section (Task 2.1: positions, thermal output, drift)"));
    CSVLines.Add(TEXT("#                     Added RiftWidth, RiftAge columns (Task 2.2: rift progression tracking)"));
    CSVLines.Add(TEXT("#                     Added TemperatureK column (Task 2.3: analytic thermal field from hotspots + subduction)"));
    CSVLines.Add(TEXT("# Backward compatible: v2.0 readers will ignore new columns"));
    CSVLines.Add(TEXT(""));

    // Header
    CSVLines.Add(TEXT("PlateID,CentroidX,CentroidY,CentroidZ,CrustType,CrustThickness,EulerPoleAxisX,EulerPoleAxisY,EulerPoleAxisZ,AngularVelocity"));

    // Plate data
    for (const FTectonicPlate& Plate : Plates)
    {
        const FString CrustTypeName = (Plate.CrustType == ECrustType::Oceanic) ? TEXT("Oceanic") : TEXT("Continental");

        CSVLines.Add(FString::Printf(TEXT("%d,%.8f,%.8f,%.8f,%s,%.2f,%.8f,%.8f,%.8f,%.8f"),
            Plate.PlateID,
            Plate.Centroid.X, Plate.Centroid.Y, Plate.Centroid.Z,
            *CrustTypeName,
            Plate.CrustThickness,
            Plate.EulerPoleAxis.X, Plate.EulerPoleAxis.Y, Plate.EulerPoleAxis.Z,
            Plate.AngularVelocity
        ));
    }

    // Add separator and boundary data (CSV v3.0: added BoundaryState, StateTransitionTime, ThermalFlux, Task 2.2: RiftWidth, RiftAge)
    CSVLines.Add(TEXT("")); // Empty line
    CSVLines.Add(TEXT("PlateA_ID,PlateB_ID,BoundaryType,BoundaryState,StateTransitionTime_My,RelativeVelocity,AccumulatedStress_MPa,DivergentDuration_My,ConvergentDuration_My,ThermalFlux,RiftWidth_m,RiftAge_My"));

    for (const auto& BoundaryPair : Boundaries)
    {
        const TPair<int32, int32>& PlateIDs = BoundaryPair.Key;
        const FPlateBoundary& Boundary = BoundaryPair.Value;

        FString BoundaryTypeName;
        switch (Boundary.BoundaryType)
        {
            case EBoundaryType::Divergent:  BoundaryTypeName = TEXT("Divergent"); break;
            case EBoundaryType::Convergent: BoundaryTypeName = TEXT("Convergent"); break;
            case EBoundaryType::Transform:  BoundaryTypeName = TEXT("Transform"); break;
        }

        FString BoundaryStateName;
        switch (Boundary.BoundaryState)
        {
            case EBoundaryState::Nascent: BoundaryStateName = TEXT("Nascent"); break;
            case EBoundaryState::Active:  BoundaryStateName = TEXT("Active"); break;
            case EBoundaryState::Dormant: BoundaryStateName = TEXT("Dormant"); break;
            case EBoundaryState::Rifting: BoundaryStateName = TEXT("Rifting"); break; // Milestone 4 Task 2.2
        }

        // Milestone 4 Task 1.3: Thermal flux (placeholder - will be populated by Task 2.3)
        const double ThermalFlux = 0.0;

        // Milestone 4 Task 2.2: Rift width and age (only valid for rifting boundaries)
        const double RiftAgeMy = (Boundary.BoundaryState == EBoundaryState::Rifting && Boundary.RiftFormationTimeMy > 0.0)
            ? (CurrentTimeMy - Boundary.RiftFormationTimeMy)
            : 0.0;

        CSVLines.Add(FString::Printf(TEXT("%d,%d,%s,%s,%.2f,%.8f,%.2f,%.2f,%.2f,%.4f,%.0f,%.2f"),
            PlateIDs.Key, PlateIDs.Value,
            *BoundaryTypeName,
            *BoundaryStateName,
            Boundary.StateTransitionTimeMy,
            Boundary.RelativeVelocity,
            Boundary.AccumulatedStress,
            Boundary.DivergentDurationMy,
            Boundary.ConvergentDurationMy,
            ThermalFlux,
            Boundary.RiftWidthMeters,
            RiftAgeMy
        ));
    }

    // Add summary statistics
    CSVLines.Add(TEXT("")); // Empty line
    CSVLines.Add(TEXT("Metric,Value"));
    CSVLines.Add(FString::Printf(TEXT("SimulationTime_My,%.2f"), CurrentTimeMy));
    CSVLines.Add(FString::Printf(TEXT("PlateCount,%d"), Plates.Num()));
    CSVLines.Add(FString::Printf(TEXT("BoundaryCount,%d"), Boundaries.Num()));
    CSVLines.Add(FString::Printf(TEXT("Seed,%d"), Parameters.Seed));
    CSVLines.Add(FString::Printf(TEXT("TotalStepsSimulated,%lld"), static_cast<long long>(TotalStepsSimulated)));
    CSVLines.Add(FString::Printf(TEXT("RetessStepsObserved,%lld"), static_cast<long long>(RetessellationCadenceStats.StepsObserved)));
    CSVLines.Add(FString::Printf(TEXT("RetessEvaluations,%d"), RetessellationCadenceStats.EvaluationCount));
    CSVLines.Add(FString::Printf(TEXT("RetessAutoTriggers,%d"), RetessellationCadenceStats.TriggerCount));
    CSVLines.Add(FString::Printf(TEXT("RetessCooldownBlocks,%d"), RetessellationCadenceStats.CooldownBlocks));
    CSVLines.Add(FString::Printf(TEXT("RetessStepsInCooldown,%lld"), static_cast<long long>(RetessellationCadenceStats.StepsSpentInCooldown)));
    CSVLines.Add(FString::Printf(TEXT("RetessLastTriggerIntervalSteps,%d"), RetessellationCadenceStats.LastTriggerInterval));
    CSVLines.Add(FString::Printf(TEXT("RetessStepsSinceLastTrigger,%d"), RetessellationCadenceStats.StepsSinceLastTrigger));
    CSVLines.Add(FString::Printf(TEXT("RetessLastCooldownDurationSteps,%d"), RetessellationCadenceStats.LastCooldownDuration));
    CSVLines.Add(FString::Printf(TEXT("RetessLastDriftDegrees,%.2f"), RetessellationCadenceStats.LastTriggerMaxDriftDegrees));
    CSVLines.Add(FString::Printf(TEXT("RetessLastBadTriangleRatio,%.4f"), RetessellationCadenceStats.LastTriggerBadTriangleRatio));

    // Calculate total kinetic energy (for monitoring)
    double TotalKineticEnergy = 0.0;
    for (const FTectonicPlate& Plate : Plates)
    {
        // Simplified: KE ∝ ω² (ignoring moment of inertia for now)
        TotalKineticEnergy += Plate.AngularVelocity * Plate.AngularVelocity;
    }
    CSVLines.Add(FString::Printf(TEXT("TotalKineticEnergy,%.8f"), TotalKineticEnergy));

    // Count boundary types
    int32 DivergentCount = 0, ConvergentCount = 0, TransformCount = 0;
    for (const auto& BoundaryPair : Boundaries)
    {
        switch (BoundaryPair.Value.BoundaryType)
        {
            case EBoundaryType::Divergent:  DivergentCount++; break;
            case EBoundaryType::Convergent: ConvergentCount++; break;
            case EBoundaryType::Transform:  TransformCount++; break;
        }
    }
    CSVLines.Add(FString::Printf(TEXT("DivergentBoundaries,%d"), DivergentCount));
    CSVLines.Add(FString::Printf(TEXT("ConvergentBoundaries,%d"), ConvergentCount));
    CSVLines.Add(FString::Printf(TEXT("TransformBoundaries,%d"), TransformCount));

    // Milestone 4 Task 1.3: Export topology events (CSV v3.0)
    CSVLines.Add(TEXT("")); // Empty line
    CSVLines.Add(TEXT("EventType,OriginalPlateID,NewPlateID,Timestamp_My,StressAtEvent_MPa,VelocityAtEvent"));

    for (const FPlateTopologyEvent& Event : TopologyEvents)
    {
        FString EventTypeName;
        switch (Event.EventType)
        {
            case EPlateTopologyEventType::Split: EventTypeName = TEXT("Split"); break;
            case EPlateTopologyEventType::Merge: EventTypeName = TEXT("Merge"); break;
            default: EventTypeName = TEXT("None"); break;
        }

        // For split: [OriginalID, NewID], for merge: [ConsumedID, SurvivorID]
        const int32 PlateID1 = Event.PlateIDs.IsValidIndex(0) ? Event.PlateIDs[0] : INDEX_NONE;
        const int32 PlateID2 = Event.PlateIDs.IsValidIndex(1) ? Event.PlateIDs[1] : INDEX_NONE;

        CSVLines.Add(FString::Printf(TEXT("%s,%d,%d,%.2f,%.2f,%.8f"),
            *EventTypeName,
            PlateID1,
            PlateID2,
            Event.TimestampMy,
            Event.StressAtEvent,
            Event.VelocityAtEvent
        ));
    }

    if (TopologyEvents.Num() == 0)
    {
        CSVLines.Add(TEXT("# No topology events this simulation"));
    }

    // Milestone 4 Task 2.1: Export hotspot data
    CSVLines.Add(TEXT("")); // Empty line
    CSVLines.Add(TEXT("HotspotID,Type,PositionX,PositionY,PositionZ,ThermalOutput,InfluenceRadius_rad,DriftVelocityX,DriftVelocityY,DriftVelocityZ"));

    for (const FMantleHotspot& Hotspot : Hotspots)
    {
        FString TypeName;
        switch (Hotspot.Type)
        {
            case EHotspotType::Major: TypeName = TEXT("Major"); break;
            case EHotspotType::Minor: TypeName = TEXT("Minor"); break;
            default: TypeName = TEXT("Unknown"); break;
        }

        CSVLines.Add(FString::Printf(TEXT("%d,%s,%.8f,%.8f,%.8f,%.2f,%.6f,%.8f,%.8f,%.8f"),
            Hotspot.HotspotID,
            *TypeName,
            Hotspot.Position.X, Hotspot.Position.Y, Hotspot.Position.Z,
            Hotspot.ThermalOutput,
            Hotspot.InfluenceRadius,
            Hotspot.DriftVelocity.X, Hotspot.DriftVelocity.Y, Hotspot.DriftVelocity.Z
        ));
    }

    if (Hotspots.Num() == 0)
    {
        CSVLines.Add(TEXT("# No hotspots active (bEnableHotspots=false)"));
    }

    // Milestone 3: Export vertex-level data (stress, velocity, elevation, Task 2.3: temperature)
    CSVLines.Add(TEXT("")); // Empty line
    CSVLines.Add(TEXT("VertexIndex,PositionX,PositionY,PositionZ,PlateID,VelocityX,VelocityY,VelocityZ,VelocityMagnitude,StressMPa,ElevationMeters,TemperatureK"));

    /**
     * M5 Phase 3: Stress-to-elevation conversion (cosmetic visualization, not physically accurate).
     * CompressionModulus = 100.0 means "1 MPa stress → 100 m elevation" (legacy visualization scale).
     * This matches TectonicSimulationController.cpp rendering logic.
     */
    constexpr double CompressionModulus = 100.0; // 1 MPa = 100 m elevation (cosmetic)

    const int32 MaxVerticesToExport = FMath::Min(RenderVertices.Num(), 1000); // Limit to first 1000 vertices for CSV size
    for (int32 i = 0; i < MaxVerticesToExport; ++i)
    {
        const FVector3d& Position = RenderVertices[i];
        const int32 PlateID = VertexPlateAssignments.IsValidIndex(i) ? VertexPlateAssignments[i] : INDEX_NONE;
        const FVector3d& Velocity = VertexVelocities.IsValidIndex(i) ? VertexVelocities[i] : FVector3d::ZeroVector;
        const double VelocityMag = Velocity.Length();
        const double StressMPa = VertexStressValues.IsValidIndex(i) ? VertexStressValues[i] : 0.0;
        const double ElevationMeters = (StressMPa / CompressionModulus) * Parameters.ElevationScale;
        const double TemperatureK = VertexTemperatureValues.IsValidIndex(i) ? VertexTemperatureValues[i] : 0.0; // Milestone 4 Task 2.3

        CSVLines.Add(FString::Printf(TEXT("%d,%.8f,%.8f,%.8f,%d,%.8f,%.8f,%.8f,%.8f,%.2f,%.2f,%.1f"),
            i,
            Position.X, Position.Y, Position.Z,
            PlateID,
            Velocity.X, Velocity.Y, Velocity.Z,
            VelocityMag,
            StressMPa,
            ElevationMeters,
            TemperatureK
        ));
    }

    if (RenderVertices.Num() > MaxVerticesToExport)
    {
        CSVLines.Add(FString::Printf(TEXT("# Note: Vertex data truncated to %d of %d vertices for CSV size"), MaxVerticesToExport, RenderVertices.Num()));
    }

    // Write to file
    const FString CSVContent = FString::Join(CSVLines, TEXT("\n"));
    if (FFileHelper::SaveStringToFile(CSVContent, *FilePath))
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Exported metrics to: %s"), *FilePath);
    }
    else
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to export metrics to: %s"), *FilePath);
    }
}

void UTectonicSimulationService::ExportTerranesToCSV()
{
    const FString OutputDir = FPaths::ProjectSavedDir() / TEXT("TectonicMetrics");
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*OutputDir))
    {
        PlatformFile.CreateDirectory(*OutputDir);
    }

    const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    const FString Filename = FString::Printf(TEXT("Terranes_Seed%d_Step%d_%s.csv"),
        Parameters.Seed,
        static_cast<int32>(CurrentTimeMy / 2.0),
        *Timestamp);
    const FString FilePath = OutputDir / Filename;

    auto TerraneStateToString = [](ETerraneState State) -> const TCHAR*
    {
        switch (State)
        {
            case ETerraneState::Attached:     return TEXT("Attached");
            case ETerraneState::Extracted:    return TEXT("Extracted");
            case ETerraneState::Transporting: return TEXT("Transporting");
            case ETerraneState::Colliding:    return TEXT("Colliding");
            default:                          return TEXT("Unknown");
        }
    };

    auto ComputeLatLonDegrees = [](const FVector3d& Position, double& OutLatDeg, double& OutLonDeg)
    {
        const FVector3d Unit = Position.IsNearlyZero() ? FVector3d::UnitZ() : Position.GetSafeNormal();
        OutLatDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Unit.Z, -1.0, 1.0)));
        OutLonDeg = FMath::RadiansToDegrees(FMath::Atan2(Unit.Y, Unit.X));
    };

    TArray<FString> CSVLines;
    CSVLines.Add(TEXT("# Planetary Creation Terrane Export v1.0"));
    CSVLines.Add(TEXT("TerraneID,State,SourcePlateID,CarrierPlateID,TargetPlateID,CentroidLat_deg,CentroidLon_deg,Area_km2,ExtractionTime_My,ReattachmentTime_My,ActiveDuration_My,VertexCount"));

    if (Terranes.Num() == 0)
    {
        CSVLines.Add(TEXT("# No terranes recorded for current simulation state"));
    }
    else
    {
        for (const FContinentalTerrane& Terrane : Terranes)
        {
            double LatDeg = 0.0;
            double LonDeg = 0.0;
            ComputeLatLonDegrees(Terrane.Centroid, LatDeg, LonDeg);

            const double ExtractionTime = Terrane.ExtractionTimeMy;
            const bool bHasReattached = Terrane.ReattachmentTimeMy > 0.0;
            const double ActiveDuration = FMath::Max(0.0, (bHasReattached ? Terrane.ReattachmentTimeMy : CurrentTimeMy) - ExtractionTime);

            const FString ReattachColumn = bHasReattached
                ? FString::Printf(TEXT("%.2f"), Terrane.ReattachmentTimeMy)
                : TEXT("");

            CSVLines.Add(FString::Printf(TEXT("%d,%s,%d,%d,%d,%.6f,%.6f,%.2f,%.2f,%s,%.2f,%d"),
                Terrane.TerraneID,
                TerraneStateToString(Terrane.State),
                Terrane.SourcePlateID,
                Terrane.CarrierPlateID,
                Terrane.TargetPlateID,
                LatDeg,
                LonDeg,
                Terrane.AreaKm2,
                ExtractionTime,
                *ReattachColumn,
                ActiveDuration,
                Terrane.VertexPayload.Num()));
        }
    }

    const FString CSVContent = FString::Join(CSVLines, TEXT("\n"));
    if (FFileHelper::SaveStringToFile(CSVContent, *FilePath))
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Exported terrane data to: %s"), *FilePath);
    }
    else
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to export terrane data to: %s"), *FilePath);
    }
}

// Milestone 3 Task 1.1: Generate high-density render mesh
void UTectonicSimulationService::GenerateRenderMesh()
{
    RenderVertices.Reset();
    RenderTriangles.Reset();

    InvalidateRidgeDirectionCache();

    // Start with base icosahedron vertices
    const double Phi = (1.0 + FMath::Sqrt(5.0)) / 2.0;
    TArray<FVector3d> Vertices = {
        FVector3d(-1,  Phi, 0).GetSafeNormal(),
        FVector3d( 1,  Phi, 0).GetSafeNormal(),
        FVector3d(-1, -Phi, 0).GetSafeNormal(),
        FVector3d( 1, -Phi, 0).GetSafeNormal(),
        FVector3d(0, -1,  Phi).GetSafeNormal(),
        FVector3d(0,  1,  Phi).GetSafeNormal(),
        FVector3d(0, -1, -Phi).GetSafeNormal(),
        FVector3d(0,  1, -Phi).GetSafeNormal(),
        FVector3d( Phi, 0, -1).GetSafeNormal(),
        FVector3d( Phi, 0,  1).GetSafeNormal(),
        FVector3d(-Phi, 0, -1).GetSafeNormal(),
        FVector3d(-Phi, 0,  1).GetSafeNormal()
    };

    // Base icosahedron faces (20 triangles)
    TArray<TArray<int32>> Faces = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
    };

    // Subdivide based on RenderSubdivisionLevel
    const int32 SubdivLevel = FMath::Clamp(Parameters.RenderSubdivisionLevel, 0, 8);

    for (int32 Level = 0; Level < SubdivLevel; ++Level)
    {
        TArray<TArray<int32>> NewFaces;
        TMap<TPair<int32, int32>, int32> MidpointCache;

        for (const TArray<int32>& Face : Faces)
        {
            // Get or create midpoints for each edge
            const int32 V0 = Face[0];
            const int32 V1 = Face[1];
            const int32 V2 = Face[2];

            const int32 A = GetMidpointIndex(V0, V1, MidpointCache, Vertices);
            const int32 B = GetMidpointIndex(V1, V2, MidpointCache, Vertices);
            const int32 C = GetMidpointIndex(V2, V0, MidpointCache, Vertices);

            // Split triangle into 4 smaller triangles
            NewFaces.Add({V0, A, C});
            NewFaces.Add({V1, B, A});
            NewFaces.Add({V2, C, B});
            NewFaces.Add({A, B, C});
        }

        Faces = MoveTemp(NewFaces);
    }

    // Store final vertices and triangles
    RenderVertices = Vertices;
    RenderTriangles.Reserve(Faces.Num() * 3);

    for (const TArray<int32>& Face : Faces)
    {
        RenderTriangles.Add(Face[0]);
        RenderTriangles.Add(Face[1]);
        RenderTriangles.Add(Face[2]);
    }

    const int32 ExpectedFaceCount = 20 * FMath::Pow(4.0f, static_cast<float>(SubdivLevel));
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Generated render mesh: Level %d, %d vertices, %d triangles (expected %d)"),
        SubdivLevel, RenderVertices.Num(), Faces.Num(), ExpectedFaceCount);

    BuildRenderVertexAdjacency();

    // Validate Euler characteristic (V - E + F = 2) when we have plate assignments
    if (VertexPlateAssignments.Num() == RenderVertices.Num())
    {
        int32 ActiveVertexCount = 0;
        for (int32 VertexIdx = 0; VertexIdx < VertexPlateAssignments.Num(); ++VertexIdx)
        {
            if (VertexPlateAssignments[VertexIdx] != INDEX_NONE)
            {
                ActiveVertexCount++;
            }
        }

        if (ActiveVertexCount == 0)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("Render mesh generated without any active vertices; skipping Euler validation."));
        }
        else
        {
            const int32 F = Faces.Num();
            const int32 E = (F * 3) / 2; // Each edge shared by 2 faces
            const int32 EulerChar = ActiveVertexCount - E + F;

            if (EulerChar != 2)
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("Render mesh Euler characteristic validation failed: V=%d, E=%d, F=%d, χ=%d (expected 2)"),
                    ActiveVertexCount, E, F, EulerChar);
            }
            else
            {
                UE_LOG(LogPlanetaryCreation, Verbose, TEXT("Render mesh topology validated: Euler characteristic χ=2"));
            }
        }
    }

    MarkAllRidgeDirectionsDirty();
    BumpOceanicAmplificationSerial();
}

int32 UTectonicSimulationService::GetMidpointIndex(int32 V0, int32 V1, TMap<TPair<int32, int32>, int32>& MidpointCache, TArray<FVector3d>& Vertices)
{
    // Ensure consistent edge key ordering
    const TPair<int32, int32> EdgeKey = (V0 < V1) ? TPair<int32, int32>(V0, V1) : TPair<int32, int32>(V1, V0);

    // Check cache
    if (const int32* CachedIndex = MidpointCache.Find(EdgeKey))
    {
        return *CachedIndex;
    }

    // Create new midpoint vertex
    const FVector3d Midpoint = ((Vertices[V0] + Vertices[V1]) * 0.5).GetSafeNormal();
    const int32 NewIndex = Vertices.Add(Midpoint);
    MidpointCache.Add(EdgeKey, NewIndex);

    return NewIndex;
}

// Milestone 3 Task 2.1: Build Voronoi mapping
void UTectonicSimulationService::BuildVoronoiMapping()
{
    const int32 VertexCount = RenderVertices.Num();

    const bool bHadComparableAssignments = CachedVoronoiAssignments.Num() == VertexCount;
    LastVoronoiReassignedCount = 0;
    bLastVoronoiForcedFullRidgeUpdate = false;

    if (VertexCount == 0 || Plates.Num() == 0)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("Cannot build Voronoi mapping: RenderVertices=%d, Plates=%d"),
            VertexCount, Plates.Num());
        if (!bHadComparableAssignments)
        {
            MarkAllRidgeDirectionsDirty();
            bLastVoronoiForcedFullRidgeUpdate = true;
        }
        CachedVoronoiAssignments.Reset();
        return;
    }

    const double StartTime = FPlatformTime::Seconds();

    // For small plate counts (N<50), brute force is faster than KD-tree
    // due to cache locality and no tree traversal overhead
    VertexPlateAssignments.SetNumUninitialized(VertexCount);

    TArray<int32> ReassignedVertices;
    ReassignedVertices.Reserve(VertexCount);

    // Milestone 4 Task 5.0: Voronoi warping parameters
    const bool bUseWarping = Parameters.bEnableVoronoiWarping;
    const double WarpAmplitude = Parameters.VoronoiWarpingAmplitude;
    const double WarpFrequency = Parameters.VoronoiWarpingFrequency;

    constexpr double ContinentalThresholdMeters = -1000.0;
#if UE_BUILD_DEVELOPMENT
    int32 ElevationOverrideCount = 0;
#endif

    for (int32 i = 0; i < VertexCount; ++i)
    {
        int32 ClosestPlateID = INDEX_NONE;
        double MinDistSq = TNumericLimits<double>::Max();
        const FTectonicPlate* ClosestPlate = nullptr;

        const FVector3d& Vertex = RenderVertices[i];

        for (const FTectonicPlate& Plate : Plates)
        {
            double DistSq = FVector3d::DistSquared(Vertex, Plate.Centroid);

            // Milestone 4 Task 5.0: Apply noise warping to distance
            // "More irregular continent shapes can be obtained by warping the geodesic distances
            // to the centroids using a simple noise function." (Paper Section 3)
            if (bUseWarping && WarpAmplitude > SMALL_NUMBER)
            {
                // Use 3D Perlin noise seeded by vertex + plate centroid for per-plate variation
                // This ensures each plate boundary gets unique noise patterns
                const FVector NoiseInput = FVector((Vertex + Plate.Centroid) * WarpFrequency);
                const float NoiseValue = FMath::PerlinNoise3D(NoiseInput);

                // Warp distance: d' = d * (1 + amplitude * noise)
                // NoiseValue range [-1, 1], so warping range: [1 - amplitude, 1 + amplitude]
                const double WarpFactor = 1.0 + WarpAmplitude * static_cast<double>(NoiseValue);
                DistSq *= WarpFactor;
            }

            if (DistSq < MinDistSq)
            {
                MinDistSq = DistSq;
                ClosestPlateID = Plate.PlateID;
                ClosestPlate = &Plate;
            }
        }

        VertexPlateAssignments[i] = ClosestPlateID;

        if (!CachedVoronoiAssignments.IsValidIndex(i) || CachedVoronoiAssignments[i] != ClosestPlateID)
        {
            ReassignedVertices.Add(i);
        }

        if (ClosestPlate && VertexElevationValues.IsValidIndex(i))
        {
            const bool bShouldBeOceanic = (ClosestPlate->CrustType == ECrustType::Oceanic);
            const bool bElevationLooksContinental = VertexElevationValues[i] > ContinentalThresholdMeters;
            const bool bCurrentlyOceanic = !bElevationLooksContinental;

            if (bCurrentlyOceanic != bShouldBeOceanic)
            {
                const double BaselineElevation = bShouldBeOceanic
                    ? PaperElevationConstants::AbyssalPlainDepth_m
                    : PaperElevationConstants::ContinentalBaseline_m;

                VertexElevationValues[i] = BaselineElevation;

                if (VertexAmplifiedElevation.IsValidIndex(i))
                {
                    VertexAmplifiedElevation[i] = BaselineElevation;
                }

                if (VertexSedimentThickness.IsValidIndex(i))
                {
                    VertexSedimentThickness[i] = 0.0;
                }

                if (VertexErosionRates.IsValidIndex(i))
                {
                    VertexErosionRates[i] = 0.0;
                }

                if (VertexCrustAge.IsValidIndex(i))
                {
                    if (bShouldBeOceanic && !bCurrentlyOceanic)
                    {
                        // Newly formed oceanic crust emerging at a divergent boundary.
                        VertexCrustAge[i] = 0.0;
                    }
                    else if (!bShouldBeOceanic && bCurrentlyOceanic)
                    {
                        // Transitioning into continental crust; treat as mature lithosphere.
                        VertexCrustAge[i] = 200.0; // 200 My mirrors "old crust" bucket in tests
                    }
                }

#if UE_BUILD_DEVELOPMENT
                ++ElevationOverrideCount;
#endif
            }
        }
    }

    const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Built Voronoi mapping: %d vertices → %d plates in %.2f ms (avg %.3f μs per vertex)"),
        VertexCount, Plates.Num(), ElapsedMs, (ElapsedMs * 1000.0) / FMath::Max(VertexCount, 1));

#if UE_BUILD_DEVELOPMENT
    if (ElevationOverrideCount > 0)
    {
        UE_LOG(LogPlanetaryCreation, Log,
            TEXT("[Voronoi] Reset %d vertices to crust baselines after reassignment"),
            ElevationOverrideCount);
    }
#endif

    // Validate all vertices assigned
    int32 UnassignedCount = 0;
    for (int32 PlateID : VertexPlateAssignments)
    {
        if (PlateID == INDEX_NONE)
        {
            UnassignedCount++;
        }
    }

    if (UnassignedCount > 0)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("Voronoi mapping incomplete: %d vertices unassigned"), UnassignedCount);
    }

    BuildRenderVertexBoundaryCache();

    // Ensure subsequent Stage B passes recompute ridge directions against the refreshed cache.
    if (ReassignedVertices.Num() > 1)
    {
        ReassignedVertices.Sort();
        int32 WriteIndex = 1;
        for (int32 ReadIndex = 1; ReadIndex < ReassignedVertices.Num(); ++ReadIndex)
        {
            if (ReassignedVertices[ReadIndex] != ReassignedVertices[WriteIndex - 1])
            {
                ReassignedVertices[WriteIndex++] = ReassignedVertices[ReadIndex];
            }
        }
        ReassignedVertices.SetNum(WriteIndex);
    }

    if (!bHadComparableAssignments)
    {
        MarkAllRidgeDirectionsDirty();
        LastVoronoiReassignedCount = VertexCount;
        bLastVoronoiForcedFullRidgeUpdate = true;
    }
    else
    {
        int32 EffectiveRingDepth = Parameters.RidgeDirectionDirtyRingDepth;
        if (ReassignedVertices.Num() > 0 && EffectiveRingDepth > 0)
        {
            const double DirtyRatio = static_cast<double>(ReassignedVertices.Num()) / static_cast<double>(VertexCount);
            if (DirtyRatio >= 0.25)
            {
                EffectiveRingDepth = 0;
            }
            else if (DirtyRatio >= 0.1 && EffectiveRingDepth > 1)
            {
                EffectiveRingDepth -= 1;
            }
        }

        LastVoronoiReassignedCount = ReassignedVertices.Num();
        if (ReassignedVertices.Num() > 0)
        {
            MarkRidgeRingDirty(ReassignedVertices, EffectiveRingDepth);
        }
    }

    CachedVoronoiAssignments = VertexPlateAssignments;
}

void UTectonicSimulationService::BuildRenderVertexAdjacency()
{
    const int32 VertexCount = RenderVertices.Num();
    if (VertexCount == 0)
    {
        RenderVertexAdjacencyOffsets.Reset();
        RenderVertexAdjacency.Reset();
        RenderVertexAdjacencyWeights.Reset();
        return;
    }

    TArray<TSet<int32>> NeighborSets;
    NeighborSets.SetNum(VertexCount);

    for (int32 TriIdx = 0; TriIdx < RenderTriangles.Num(); TriIdx += 3)
    {
        const int32 A = RenderTriangles[TriIdx];
        const int32 B = RenderTriangles[TriIdx + 1];
        const int32 C = RenderTriangles[TriIdx + 2];

        if (!NeighborSets.IsValidIndex(A) || !NeighborSets.IsValidIndex(B) || !NeighborSets.IsValidIndex(C))
        {
            continue;
        }

        NeighborSets[A].Add(B);
        NeighborSets[A].Add(C);
        NeighborSets[B].Add(A);
        NeighborSets[B].Add(C);
        NeighborSets[C].Add(A);
        NeighborSets[C].Add(B);
    }

    RenderVertexAdjacencyOffsets.SetNum(VertexCount + 1);
    RenderVertexAdjacencyOffsets[0] = 0;

    int32 RunningTotal = 0;
    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        RenderVertexAdjacencyOffsets[VertexIdx] = RunningTotal;
        RunningTotal += NeighborSets[VertexIdx].Num();
    }
    RenderVertexAdjacencyOffsets[VertexCount] = RunningTotal;

    RenderVertexAdjacency.SetNum(RunningTotal);
    RenderVertexAdjacencyWeights.SetNum(RunningTotal);
    RenderVertexAdjacencyWeightTotals.SetNum(VertexCount);

    const double SmoothingRadius = FMath::Max(Parameters.OceanicDampeningSmoothingRadius, UE_DOUBLE_SMALL_NUMBER);
    const double InvTwoRadiusSq = 1.0 / (2.0 * SmoothingRadius * SmoothingRadius);

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const int32 Start = RenderVertexAdjacencyOffsets[VertexIdx];
        const int32 Count = NeighborSets[VertexIdx].Num();
        if (Count == 0)
        {
            continue;
        }

        TArray<int32> SortedNeighbors = NeighborSets[VertexIdx].Array();
        SortedNeighbors.Sort();

        const FVector3d& VertexPos = RenderVertices[VertexIdx];

        float WeightSum = 0.0f;

        for (int32 LocalIdx = 0; LocalIdx < Count; ++LocalIdx)
        {
            const int32 NeighborIdx = SortedNeighbors[LocalIdx];
            RenderVertexAdjacency[Start + LocalIdx] = NeighborIdx;

            const FVector3d& NeighborPos = RenderVertices.IsValidIndex(NeighborIdx)
                ? RenderVertices[NeighborIdx]
                : FVector3d::ZeroVector;

            double Weight = 0.0;
            if (RenderVertices.IsValidIndex(NeighborIdx))
            {
                const double Dot = FMath::Clamp(FVector3d::DotProduct(VertexPos.GetSafeNormal(), NeighborPos.GetSafeNormal()), -1.0, 1.0);
                const double Geodesic = FMath::Acos(Dot);
                Weight = FMath::Exp(-(Geodesic * Geodesic) * InvTwoRadiusSq);
            }

            const float WeightFloat = static_cast<float>(Weight);
            RenderVertexAdjacencyWeights[Start + LocalIdx] = WeightFloat;
            WeightSum += WeightFloat;
        }

        RenderVertexAdjacencyWeightTotals[VertexIdx] = WeightSum;
    }

    BuildRenderVertexReverseAdjacency();
    UpdateConvergentNeighborFlags();
}

void UTectonicSimulationService::BuildRenderVertexReverseAdjacency()
{
    const int32 VertexCount = RenderVertices.Num();
    if (RenderVertexAdjacencyOffsets.Num() != VertexCount + 1 || RenderVertexAdjacency.Num() == 0)
    {
        RenderVertexReverseAdjacency.Reset();
        return;
    }

    RenderVertexReverseAdjacency.SetNum(RenderVertexAdjacency.Num());

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const int32 StartOffset = RenderVertexAdjacencyOffsets[VertexIdx];
        const int32 EndOffset = RenderVertexAdjacencyOffsets[VertexIdx + 1];

        for (int32 Offset = StartOffset; Offset < EndOffset; ++Offset)
        {
            const int32 NeighborIdx = RenderVertexAdjacency[Offset];
            int32 ReverseIndex = INDEX_NONE;

            const int32 NeighborStart = RenderVertexAdjacencyOffsets[NeighborIdx];
            const int32 NeighborEnd = RenderVertexAdjacencyOffsets[NeighborIdx + 1];
            for (int32 NeighborOffset = NeighborStart; NeighborOffset < NeighborEnd; ++NeighborOffset)
            {
                if (RenderVertexAdjacency[NeighborOffset] == VertexIdx)
                {
                    ReverseIndex = NeighborOffset;
                    break;
                }
            }

            RenderVertexReverseAdjacency[Offset] = ReverseIndex;
        }
    }
}

void UTectonicSimulationService::BuildRenderVertexBoundaryCache()
{
    const int32 VertexCount = RenderVertices.Num();
    RenderVertexBoundaryCache.SetNum(VertexCount);

    if (VertexCount == 0)
    {
        return;
    }

    if (RenderVertexAdjacencyOffsets.Num() != VertexCount + 1 || RenderVertexAdjacency.Num() == 0)
    {
        BuildRenderVertexAdjacency();
    }

    TArray<FVector3d> VertexNormals;
    VertexNormals.SetNum(VertexCount);
    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        VertexNormals[VertexIdx] = RenderVertices[VertexIdx].GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
    }

    const auto GetPlateID = [&](int32 Index) -> int32
    {
        return VertexPlateAssignments.IsValidIndex(Index) ? VertexPlateAssignments[Index] : INDEX_NONE;
    };

    const auto MakeBoundaryKey = [](int32 PlateA, int32 PlateB)
    {
        return (PlateA < PlateB)
            ? TPair<int32, int32>(PlateA, PlateB)
            : TPair<int32, int32>(PlateB, PlateA);
    };

    const auto GetBoundary = [&](int32 PlateA, int32 PlateB) -> const FPlateBoundary*
    {
        if (PlateA == INDEX_NONE || PlateB == INDEX_NONE || PlateA == PlateB)
        {
            return nullptr;
        }
        return Boundaries.Find(MakeBoundaryKey(PlateA, PlateB));
    };

    auto ResetInfo = [&](int32 Index)
    {
        FRenderVertexBoundaryInfo& Info = RenderVertexBoundaryCache[Index];
        Info.DistanceRadians = TNumericLimits<float>::Max();
        Info.BoundaryTangent = FVector3d::ZeroVector;
        Info.SourcePlateID = GetPlateID(Index);
        Info.OpposingPlateID = INDEX_NONE;
        Info.bHasBoundary = false;
        Info.bIsDivergent = false;
    };

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        ResetInfo(VertexIdx);
    }

    const double QuantizeScale = 10000.0;
    auto QuantizeKey = [&](const FVector3d& Position) -> FIntVector
    {
        const FVector3d Unit = Position.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
        return FIntVector(
            FMath::RoundToInt(Unit.X * QuantizeScale),
            FMath::RoundToInt(Unit.Y * QuantizeScale),
            FMath::RoundToInt(Unit.Z * QuantizeScale));
    };

    TMap<FIntVector, TArray<int32>> PositionBuckets;
    PositionBuckets.Reserve(VertexCount / 2);
    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        PositionBuckets.FindOrAdd(QuantizeKey(RenderVertices[VertexIdx])).Add(VertexIdx);
    }

    TArray<FVector3d> SeedTangents;
    SeedTangents.SetNumZeroed(VertexCount);
    TArray<int32> SeedOpposingPlate;
    SeedOpposingPlate.Init(INDEX_NONE, VertexCount);
    TBitArray<> SeedMask(false, VertexCount);

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const int32 PlateID = GetPlateID(VertexIdx);
        if (PlateID == INDEX_NONE)
        {
            continue;
        }

        const FVector3d& VertexNormal = VertexNormals[VertexIdx];
        const int32 Start = RenderVertexAdjacencyOffsets[VertexIdx];
        const int32 End = RenderVertexAdjacencyOffsets[VertexIdx + 1];

        FVector3d TangentSum = FVector3d::ZeroVector;
        int32 DivergentNeighborCount = 0;
        int32 OpposingPlateID = INDEX_NONE;

        for (int32 Offset = Start; Offset < End; ++Offset)
        {
            const int32 NeighborIdx = RenderVertexAdjacency.IsValidIndex(Offset) ? RenderVertexAdjacency[Offset] : INDEX_NONE;
            if (!RenderVertices.IsValidIndex(NeighborIdx))
            {
                continue;
            }

            const int32 NeighborPlateID = GetPlateID(NeighborIdx);
            if (NeighborPlateID == INDEX_NONE || NeighborPlateID == PlateID)
            {
                continue;
            }

            const FPlateBoundary* Boundary = GetBoundary(PlateID, NeighborPlateID);
            if (!Boundary || Boundary->BoundaryType != EBoundaryType::Divergent)
            {
                continue;
            }

            const FVector3d& NeighborNormal = VertexNormals[NeighborIdx];
            const FVector3d PlaneNormal = FVector3d::CrossProduct(VertexNormal, NeighborNormal).GetSafeNormal();
            if (PlaneNormal.IsNearlyZero())
            {
                continue;
            }

            FVector3d Candidate = FVector3d::CrossProduct(PlaneNormal, VertexNormal).GetSafeNormal();
            if (Candidate.IsNearlyZero())
            {
                continue;
            }

            if (!TangentSum.IsNearlyZero() && (TangentSum | Candidate) < 0.0)
            {
                Candidate *= -1.0;
            }

            TangentSum += Candidate;
            ++DivergentNeighborCount;

            if (OpposingPlateID == INDEX_NONE)
            {
                OpposingPlateID = NeighborPlateID;
            }
            else if (OpposingPlateID != NeighborPlateID)
            {
                OpposingPlateID = INDEX_NONE; // Multiple opposing plates at junction
            }
        }

        if (DivergentNeighborCount > 0 && !TangentSum.IsNearlyZero())
        {
            SeedTangents[VertexIdx] = TangentSum.GetSafeNormal();
            SeedOpposingPlate[VertexIdx] = OpposingPlateID;
            SeedMask[VertexIdx] = true;
        }
    }

    TArray<FIntVector> BucketKeys;
    PositionBuckets.GenerateKeyArray(BucketKeys);
    BucketKeys.Sort([](const FIntVector& A, const FIntVector& B)
    {
        if (A.X != B.X)
        {
            return A.X < B.X;
        }
        if (A.Y != B.Y)
        {
            return A.Y < B.Y;
        }
        return A.Z < B.Z;
    });

    for (const FIntVector& BucketKey : BucketKeys)
    {
        const TArray<int32>* BucketVerticesPtr = PositionBuckets.Find(BucketKey);
        if (!BucketVerticesPtr)
        {
            continue;
        }
        const TArray<int32>& BucketVertices = *BucketVerticesPtr;
        if (BucketVertices.Num() < 2)
        {
            continue;
        }

        for (int32 IndexA = 0; IndexA < BucketVertices.Num(); ++IndexA)
        {
            const int32 VertexA = BucketVertices[IndexA];
            const int32 PlateA = GetPlateID(VertexA);
            if (PlateA == INDEX_NONE)
            {
                continue;
            }

            for (int32 IndexB = 0; IndexB < BucketVertices.Num(); ++IndexB)
            {
                if (IndexA == IndexB)
                {
                    continue;
                }

                const int32 VertexB = BucketVertices[IndexB];
                const int32 PlateB = GetPlateID(VertexB);
                if (PlateB == INDEX_NONE || PlateA == PlateB)
                {
                    continue;
                }

                const FPlateBoundary* Boundary = GetBoundary(PlateA, PlateB);
                if (!Boundary || Boundary->BoundaryType != EBoundaryType::Divergent)
                {
                    continue;
                }

                if (SeedMask[VertexA] && !SeedMask[VertexB] && !SeedTangents[VertexA].IsNearlyZero())
                {
                    SeedMask[VertexB] = true;
                    SeedTangents[VertexB] = SeedTangents[VertexA];
                    SeedOpposingPlate[VertexB] = PlateA;
                }
            }
        }
    }

    struct FPropagationNode
    {
        int32 VertexIdx;
        double Distance = 0.0;
        int32 SourcePlateID = INDEX_NONE;
        int32 OpposingPlateID = INDEX_NONE;
        FVector3d Tangent = FVector3d::ZeroVector;
        bool bIsDivergent = false;
    };

    struct FPropagationCompare
    {
        bool operator()(const FPropagationNode& A, const FPropagationNode& B) const
        {
            return A.Distance > B.Distance;
        }
    };

    std::priority_queue<FPropagationNode, std::vector<FPropagationNode>, FPropagationCompare> Frontier;

    TArray<double> Distance;
    Distance.Init(TNumericLimits<double>::Max(), VertexCount);

    auto AddBoundarySeed = [&](int32 VertexIdx, int32 PlateID, int32 OpposingPlateID, const FVector3d& Tangent)
    {
        if (!RenderVertices.IsValidIndex(VertexIdx))
        {
            return;
        }

        if (PlateID == INDEX_NONE)
        {
            return;
        }

        const FVector3d NormalizedTangent = Tangent.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZeroVector);
        if (NormalizedTangent.IsNearlyZero())
        {
            return;
        }

        FRenderVertexBoundaryInfo& Info = RenderVertexBoundaryCache[VertexIdx];
        Info.bHasBoundary = true;
        Info.bIsDivergent = true;
        Info.SourcePlateID = PlateID;
        Info.OpposingPlateID = OpposingPlateID;
        Info.BoundaryTangent = NormalizedTangent;
        Info.DistanceRadians = 0.0f;

        Distance[VertexIdx] = 0.0;

        FPropagationNode Node;
        Node.VertexIdx = VertexIdx;
        Node.Distance = 0.0;
        Node.SourcePlateID = PlateID;
        Node.OpposingPlateID = OpposingPlateID;
        Node.Tangent = NormalizedTangent;
        Node.bIsDivergent = true;
        Frontier.push(Node);
    };

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        if (!SeedMask[VertexIdx])
        {
            continue;
        }

        AddBoundarySeed(VertexIdx, GetPlateID(VertexIdx), SeedOpposingPlate[VertexIdx], SeedTangents[VertexIdx]);
    }

    const double SmallNumber = 1e-8;

    auto ParallelTransportTangent = [](const FVector3d& Tangent, const FVector3d& FromNormal, const FVector3d& ToNormal)
    {
        if (Tangent.IsNearlyZero() || FromNormal.IsNearlyZero() || ToNormal.IsNearlyZero())
        {
            return Tangent;
        }

        const double CosTheta = FMath::Clamp(FromNormal | ToNormal, -1.0, 1.0);
        const double Angle = FMath::Acos(CosTheta);

        if (!FMath::IsFinite(Angle) || Angle < UE_DOUBLE_SMALL_NUMBER)
        {
            FVector3d Projected = Tangent - (Tangent | ToNormal) * ToNormal;
            return Projected.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, Tangent);
        }

        FVector3d Axis = FVector3d::CrossProduct(FromNormal, ToNormal);
        if (!Axis.Normalize())
        {
            FVector3d Projected = Tangent - (Tangent | ToNormal) * ToNormal;
            return Projected.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, Tangent);
        }

        const double SinTheta = FMath::Sin(Angle);
        const double OneMinusCos = 1.0 - FMath::Cos(Angle);

        FVector3d Rotated = Tangent * FMath::Cos(Angle)
            + FVector3d::CrossProduct(Axis, Tangent) * SinTheta
            + Axis * ((Axis | Tangent) * OneMinusCos);

        FVector3d Projected = Rotated - (Rotated | ToNormal) * ToNormal;
        return Projected.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, Rotated);
    };

    while (!Frontier.empty())
    {
        const FPropagationNode Current = Frontier.top();
        Frontier.pop();

        if (Current.VertexIdx < 0 || Current.VertexIdx >= VertexCount)
        {
            continue;
        }

        if (Current.Distance > Distance[Current.VertexIdx] + SmallNumber)
        {
            continue;
        }

        const int32 CurrentPlateID = GetPlateID(Current.VertexIdx);
        if (CurrentPlateID != Current.SourcePlateID)
        {
            continue;
        }

        const int32 Start = RenderVertexAdjacencyOffsets[Current.VertexIdx];
        const int32 End = RenderVertexAdjacencyOffsets[Current.VertexIdx + 1];
        const FVector3d& CurrentNormal = VertexNormals[Current.VertexIdx];

        for (int32 Offset = Start; Offset < End; ++Offset)
        {
            const int32 NeighborIdx = RenderVertexAdjacency.IsValidIndex(Offset) ? RenderVertexAdjacency[Offset] : INDEX_NONE;
            if (!RenderVertices.IsValidIndex(NeighborIdx))
            {
                continue;
            }

            if (GetPlateID(NeighborIdx) != CurrentPlateID)
            {
                continue;
            }

            const FVector3d& NeighborNormal = VertexNormals[NeighborIdx];
            double EdgeCost = FMath::Acos(FMath::Clamp(CurrentNormal | NeighborNormal, -1.0, 1.0));
            if (!FMath::IsFinite(EdgeCost))
            {
                EdgeCost = 0.0;
            }

            const double NewDistance = Current.Distance + EdgeCost;
            if (NewDistance + SmallNumber >= Distance[NeighborIdx])
            {
                continue;
            }

            Distance[NeighborIdx] = NewDistance;

            FRenderVertexBoundaryInfo& NeighborInfo = RenderVertexBoundaryCache[NeighborIdx];
            NeighborInfo.bHasBoundary = true;
            NeighborInfo.bIsDivergent = Current.bIsDivergent;
            NeighborInfo.SourcePlateID = Current.SourcePlateID;
            NeighborInfo.OpposingPlateID = Current.OpposingPlateID;

            FVector3d TransportedTangent = Current.Tangent;
            if (!TransportedTangent.IsNearlyZero())
            {
                TransportedTangent = ParallelTransportTangent(Current.Tangent, CurrentNormal, NeighborNormal);
            }

            NeighborInfo.BoundaryTangent = TransportedTangent;
            NeighborInfo.DistanceRadians = static_cast<float>(NewDistance);

            FPropagationNode Next = Current;
            Next.VertexIdx = NeighborIdx;
            Next.Distance = NewDistance;
            Next.Tangent = TransportedTangent;
            Frontier.push(Next);
        }
    }

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const FRenderVertexBoundaryInfo& Info = RenderVertexBoundaryCache[VertexIdx];
        if (!Info.bHasBoundary || !Info.bIsDivergent)
        {
            ResetInfo(VertexIdx);
        }
    }

#if UE_BUILD_DEVELOPMENT
    int32 DivergentCount = 0;
    for (const FRenderVertexBoundaryInfo& Info : RenderVertexBoundaryCache)
    {
        if (Info.bIsDivergent)
        {
            ++DivergentCount;
        }
    }

    UE_LOG(LogPlanetaryCreation, Log,
        TEXT("[BoundaryCache] Divergent boundary tangents assigned to %d/%d vertices"),
        DivergentCount, VertexCount);
#endif
}

void UTectonicSimulationService::InvalidatePlateBoundarySummaries()
{
    PlateBoundarySummaries.Reset();
    PlateBoundarySummaryTopologyVersion = INDEX_NONE;
}

const FPlateBoundarySummary* UTectonicSimulationService::GetPlateBoundarySummary(int32 PlateID) const
{
    if (PlateID == INDEX_NONE)
    {
        return nullptr;
    }

    if (PlateBoundarySummaryTopologyVersion != TopologyVersion)
    {
        PlateBoundarySummaries.Reset();
        PlateBoundarySummaryTopologyVersion = TopologyVersion;
    }

    FPlateBoundarySummary& Summary = PlateBoundarySummaries.FindOrAdd(PlateID);
    if (Summary.CachedTopologyVersion != TopologyVersion)
    {
        RebuildPlateBoundarySummary(PlateID, Summary);
    }

    return &Summary;
}

void UTectonicSimulationService::RebuildPlateBoundarySummary(int32 PlateID, FPlateBoundarySummary& OutSummary) const
{
    OutSummary.Boundaries.Reset();
    OutSummary.CachedTopologyVersion = TopologyVersion;

    if (PlateID == INDEX_NONE)
    {
        return;
    }

    auto FindPlateByID = [&](int32 LookupPlateID) -> const FTectonicPlate*
    {
        return Plates.FindByPredicate([LookupPlateID](const FTectonicPlate& Plate)
        {
            return Plate.PlateID == LookupPlateID;
        });
    };

    const FTectonicPlate* SourcePlate = FindPlateByID(PlateID);

    TArray<TPair<int32, int32>> SortedBoundaryKeys;
    SortedBoundaryKeys.Reserve(Boundaries.Num());
    for (const TPair<TPair<int32, int32>, FPlateBoundary>& BoundaryPair : Boundaries)
    {
        const TPair<int32, int32>& Key = BoundaryPair.Key;
        if (Key.Key != PlateID && Key.Value != PlateID)
        {
            continue;
        }
        SortedBoundaryKeys.Add(Key);
    }
    SortedBoundaryKeys.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
    {
        if (A.Key != B.Key)
        {
            return A.Key < B.Key;
        }
        return A.Value < B.Value;
    });

    for (const TPair<int32, int32>& Key : SortedBoundaryKeys)
    {
        const FPlateBoundary* BoundaryPtr = Boundaries.Find(Key);
        if (!BoundaryPtr)
        {
            continue;
        }

        const FPlateBoundary& Boundary = *BoundaryPtr;

        FPlateBoundarySummaryEntry Entry;
        Entry.BoundaryType = Boundary.BoundaryType;
        Entry.OtherPlateID = (Key.Key == PlateID) ? Key.Value : Key.Key;

        FVector3d Accumulated = FVector3d::ZeroVector;
        int32 ValidCount = 0;

        for (int32 SharedIndex : Boundary.SharedEdgeVertices)
        {
            if (SharedVertices.IsValidIndex(SharedIndex))
            {
                Accumulated += SharedVertices[SharedIndex];
                ++ValidCount;
            }
            else if (RenderVertices.IsValidIndex(SharedIndex))
            {
                Accumulated += RenderVertices[SharedIndex];
                ++ValidCount;
            }
        }

        if (ValidCount > 0)
        {
            Accumulated /= static_cast<double>(ValidCount);
            Entry.RepresentativePosition = Accumulated;
            Entry.RepresentativeUnit = Accumulated.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
            Entry.bHasRepresentative = true;
        }
        else if (SourcePlate && !SourcePlate->Centroid.IsNearlyZero())
        {
            Entry.RepresentativePosition = SourcePlate->Centroid;
            Entry.RepresentativeUnit = SourcePlate->Centroid.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
            Entry.bHasRepresentative = true;
        }

        if (Entry.BoundaryType == EBoundaryType::Convergent)
        {
            const FTectonicPlate* OtherPlate = FindPlateByID(Entry.OtherPlateID);
            if (SourcePlate && OtherPlate && SourcePlate->CrustType != OtherPlate->CrustType)
            {
                Entry.bIsSubduction = true;
            }
        }

        OutSummary.Boundaries.Add(Entry);
    }
}

void UTectonicSimulationService::UpdateConvergentNeighborFlags()
{
    const int32 VertexCount = RenderVertices.Num();
    ConvergentNeighborFlags.SetNumZeroed(VertexCount);

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const int32 PlateA = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;
        if (PlateA == INDEX_NONE)
        {
            continue;
        }

        const int32 StartOffset = RenderVertexAdjacencyOffsets.IsValidIndex(VertexIdx) ? RenderVertexAdjacencyOffsets[VertexIdx] : INDEX_NONE;
        const int32 EndOffset = RenderVertexAdjacencyOffsets.IsValidIndex(VertexIdx + 1) ? RenderVertexAdjacencyOffsets[VertexIdx + 1] : INDEX_NONE;

        if (!RenderVertexAdjacencyOffsets.IsValidIndex(VertexIdx + 1))
        {
            continue;
        }

        for (int32 Offset = StartOffset; Offset < EndOffset; ++Offset)
        {
            const int32 NeighborIdx = RenderVertexAdjacency.IsValidIndex(Offset) ? RenderVertexAdjacency[Offset] : INDEX_NONE;
            const int32 PlateB = VertexPlateAssignments.IsValidIndex(NeighborIdx) ? VertexPlateAssignments[NeighborIdx] : INDEX_NONE;

            if (PlateB == INDEX_NONE || PlateA == PlateB)
            {
                continue;
            }

            const TPair<int32, int32> BoundaryKey = (PlateA < PlateB)
                ? TPair<int32, int32>(PlateA, PlateB)
                : TPair<int32, int32>(PlateB, PlateA);

            if (const FPlateBoundary* Boundary = Boundaries.Find(BoundaryKey))
            {
                if (Boundary->BoundaryType == EBoundaryType::Convergent)
                {
                    ConvergentNeighborFlags[VertexIdx] = 1;
                    break;
                }
            }
        }
    }
}

void UTectonicSimulationService::ComputeVelocityField()
{
    // Milestone 3 Task 2.2: Compute per-vertex velocity v = ω × r
    // where ω = plate's angular velocity vector (EulerPoleAxis * AngularVelocity)
    // and r = vertex position on unit sphere

    VertexVelocities.SetNumUninitialized(RenderVertices.Num());

    for (int32 i = 0; i < RenderVertices.Num(); ++i)
    {
        const int32 PlateID = VertexPlateAssignments[i];
        if (PlateID == INDEX_NONE)
        {
            VertexVelocities[i] = FVector3d::ZeroVector;
            continue;
        }

        // Find the plate
        const FTectonicPlate* Plate = Plates.FindByPredicate([PlateID](const FTectonicPlate& P)
        {
            return P.PlateID == PlateID;
        });

        if (!Plate)
        {
            VertexVelocities[i] = FVector3d::ZeroVector;
            continue;
        }

        // Compute angular velocity vector: ω = axis * magnitude (rad/My)
        const FVector3d Omega = Plate->EulerPoleAxis * Plate->AngularVelocity;

        // Compute velocity: v = ω × r
        // Cross product gives tangent vector in direction of motion
        const FVector3d Velocity = FVector3d::CrossProduct(Omega, RenderVertices[i]);

        VertexVelocities[i] = Velocity;
    }
}

void UTectonicSimulationService::UpdateBoundaryStress(double DeltaTimeMy)
{
    // Milestone 3 Task 2.3: COSMETIC STRESS VISUALIZATION (simplified model)
    // NOT physically accurate - for visual effect only

    constexpr double MaxStressMPa = 100.0; // Cap at 100 MPa (~10km elevation equivalent)
    constexpr double DecayTimeConstant = 10.0; // τ = 10 My for exponential decay

    for (auto& BoundaryPair : Boundaries)
    {
        FPlateBoundary& Boundary = BoundaryPair.Value;

        switch (Boundary.BoundaryType)
        {
        case EBoundaryType::Convergent:
            // Convergent: accumulate stress based on relative velocity
            // Stress += relativeVelocity × ΔT (simplified linear model)
            // relativeVelocity is in rad/My, convert to stress units
            {
                const double StressRate = Boundary.RelativeVelocity * 1000.0; // Arbitrary scaling for visualization
                Boundary.AccumulatedStress += StressRate * DeltaTimeMy;
                Boundary.AccumulatedStress = FMath::Min(Boundary.AccumulatedStress, MaxStressMPa);
            }
            break;

        case EBoundaryType::Divergent:
            // Divergent: exponential decay toward zero
            // S(t) = S₀ × exp(-Δt/τ)
            {
                const double DecayFactor = FMath::Exp(-DeltaTimeMy / DecayTimeConstant);
                Boundary.AccumulatedStress *= DecayFactor;
            }
            break;

        case EBoundaryType::Transform:
            // Transform: minimal accumulation (small fraction of convergent rate)
            {
                const double StressRate = Boundary.RelativeVelocity * 100.0; // 10x less than convergent
                Boundary.AccumulatedStress += StressRate * DeltaTimeMy;
                Boundary.AccumulatedStress = FMath::Min(Boundary.AccumulatedStress, MaxStressMPa * 0.5); // Lower cap
            }
            break;
        }
    }
}

void UTectonicSimulationService::InterpolateStressToVertices()
{
    // Milestone 3 Task 2.3: Interpolate boundary stress to render vertices
    // Using distance-based Gaussian falloff with σ = 10° angular distance

    VertexStressValues.SetNumZeroed(RenderVertices.Num());

    constexpr double SigmaDegrees = 10.0;
    const double SigmaRadians = FMath::DegreesToRadians(SigmaDegrees);
    const double TwoSigmaSquared = 2.0 * SigmaRadians * SigmaRadians;

    // For each vertex, sum weighted contributions from all boundaries
    for (int32 i = 0; i < RenderVertices.Num(); ++i)
    {
        const FVector3d& VertexPos = RenderVertices[i];
        double TotalWeight = 0.0;
        double WeightedStress = 0.0;

        for (const auto& BoundaryPair : Boundaries)
        {
            const FPlateBoundary& Boundary = BoundaryPair.Value;

            if (Boundary.SharedEdgeVertices.Num() < 2)
            {
                continue; // Need at least 2 vertices for boundary edge
            }

            // Calculate distance from vertex to boundary edge (approximate as midpoint)
            const int32 V0Index = Boundary.SharedEdgeVertices[0];
            const int32 V1Index = Boundary.SharedEdgeVertices[1];

            if (!SharedVertices.IsValidIndex(V0Index) || !SharedVertices.IsValidIndex(V1Index))
            {
                continue;
            }

            const FVector3d& EdgeV0 = SharedVertices[V0Index];
            const FVector3d& EdgeV1 = SharedVertices[V1Index];
            const FVector3d EdgeMidpoint = (EdgeV0 + EdgeV1).GetSafeNormal();

            // Angular distance from vertex to boundary (great circle distance on unit sphere)
            const double DotProduct = FVector3d::DotProduct(VertexPos, EdgeMidpoint);
            const double AngularDistance = FMath::Acos(FMath::Clamp(DotProduct, -1.0, 1.0));

            // Gaussian weight: exp(-distance² / (2σ²))
            const double Weight = FMath::Exp(-(AngularDistance * AngularDistance) / TwoSigmaSquared);

            WeightedStress += Boundary.AccumulatedStress * Weight;
            TotalWeight += Weight;
        }

        if (TotalWeight > 1e-9)
        {
            VertexStressValues[i] = WeightedStress / TotalWeight;
        }
    }
}

void UTectonicSimulationService::ApplyLloydRelaxation()
{
    // Milestone 3 Task 3.1: Lloyd's algorithm for even centroid distribution
    // Algorithm:
    // 1. For each plate, compute Voronoi cell (all render vertices closest to this plate)
    // 2. Calculate centroid of Voronoi cell (spherical average)
    // 3. Move plate centroid toward cell centroid (weighted step α = 0.5)
    // 4. Repeat until convergence (max delta < ε) or max iterations reached

    const int32 MaxIterations = Parameters.LloydIterations;
    if (MaxIterations <= 0)
    {
        UE_LOG(LogPlanetaryCreation, Verbose, TEXT("Lloyd relaxation disabled (iterations=0)"));
        return;
    }

    constexpr double ConvergenceThreshold = 0.01; // radians (~0.57°)
    constexpr double Alpha = 0.5; // Step weight for stability

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Starting Lloyd relaxation with %d iterations, ε=%.4f rad"), MaxIterations, ConvergenceThreshold);

    for (int32 Iteration = 0; Iteration < MaxIterations; ++Iteration)
    {
        // Step 1: Assign render vertices to Voronoi cells (compute nearest plate centroid)
        TArray<TArray<FVector3d>> VoronoiCells;
        VoronoiCells.SetNum(Plates.Num());

        for (int32 i = 0; i < RenderVertices.Num(); ++i)
        {
            // Find nearest plate centroid to this render vertex
            int32 NearestPlateIdx = 0;
            double MinDistanceSquared = TNumericLimits<double>::Max();

            for (int32 PlateIdx = 0; PlateIdx < Plates.Num(); ++PlateIdx)
            {
                const double DistanceSquared = FVector3d::DistSquared(RenderVertices[i], Plates[PlateIdx].Centroid);
                if (DistanceSquared < MinDistanceSquared)
                {
                    MinDistanceSquared = DistanceSquared;
                    NearestPlateIdx = PlateIdx;
                }
            }

            VoronoiCells[NearestPlateIdx].Add(RenderVertices[i]);
        }

        // Step 2: Compute cell centroids and update plate centroids
        double MaxDelta = 0.0;

        for (int32 PlateIdx = 0; PlateIdx < Plates.Num(); ++PlateIdx)
        {
            FTectonicPlate& Plate = Plates[PlateIdx];
            const TArray<FVector3d>& Cell = VoronoiCells[PlateIdx];

            if (Cell.Num() == 0)
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("Lloyd iteration %d: Plate %d has empty Voronoi cell"), Iteration, PlateIdx);
                continue;
            }

            // Compute spherical centroid (normalized sum of cell vertices)
            FVector3d CellCentroid = FVector3d::ZeroVector;
            for (const FVector3d& Vertex : Cell)
            {
                CellCentroid += Vertex;
            }
            CellCentroid.Normalize();

            // Step 3: Move plate centroid toward cell centroid (weighted)
            const FVector3d OldCentroid = Plate.Centroid;
            const FVector3d NewCentroid = ((1.0 - Alpha) * Plate.Centroid + Alpha * CellCentroid).GetSafeNormal();
            Plate.Centroid = NewCentroid;

            // Track maximum centroid movement for convergence check
            const double Delta = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(OldCentroid, NewCentroid), -1.0, 1.0));
            MaxDelta = FMath::Max(MaxDelta, Delta);
        }

        UE_LOG(LogPlanetaryCreation, Verbose, TEXT("Lloyd iteration %d: max delta = %.6f rad (%.4f°)"),
            Iteration, MaxDelta, FMath::RadiansToDegrees(MaxDelta));

        // Early termination if converged
        if (MaxDelta < ConvergenceThreshold)
        {
            UE_LOG(LogPlanetaryCreation, Log, TEXT("Lloyd relaxation converged after %d iterations (delta=%.6f rad < ε=%.4f rad)"),
                Iteration + 1, MaxDelta, ConvergenceThreshold);
            return;
        }
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Lloyd relaxation completed %d iterations (did not fully converge)"), MaxIterations);
}

void UTectonicSimulationService::CheckRetessellationNeeded()
{
    // Milestone 3 Task 3.3: Framework stub for dynamic re-tessellation detection
    // Full implementation deferred to M4

    if (InitialPlateCentroids.Num() != Plates.Num())
    {
        // Mismatch indicates simulation was reset - re-capture initial positions
        InitialPlateCentroids.SetNum(Plates.Num());
        for (int32 i = 0; i < Plates.Num(); ++i)
        {
            InitialPlateCentroids[i] = Plates[i].Centroid;
        }
        LastRetessellationMaxDriftDegrees = 0.0;
        LastRetessellationBadTriangleRatio = 0.0;
        return;
    }

    const FRetessellationAnalysis Analysis = ComputeRetessellationAnalysis();
    LastRetessellationMaxDriftDegrees = Analysis.MaxDriftDegrees;
    LastRetessellationBadTriangleRatio = Analysis.BadTriangleRatio;

    const double ThresholdRadians = FMath::DegreesToRadians(Parameters.RetessellationThresholdDegrees);
    const double TriggerRadians = FMath::DegreesToRadians(FMath::Max(Parameters.RetessellationTriggerDegrees, Parameters.RetessellationThresholdDegrees));
    const double MaxDriftRadians = FMath::DegreesToRadians(Analysis.MaxDriftDegrees);

    if (MaxDriftRadians > TriggerRadians && LastRetessellationBadTriangleRatio >= Parameters.RetessellationBadTriangleRatioThreshold)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("Re-tessellation would trigger: Plate %d drifted %.2f° (trigger %.2f°) with %.2f%% low-angle tris (threshold %.2f%%), but bEnableDynamicRetessellation=false"),
            Analysis.MaxDriftPlateID,
            Analysis.MaxDriftDegrees,
            FMath::Max(Parameters.RetessellationTriggerDegrees, Parameters.RetessellationThresholdDegrees),
            LastRetessellationBadTriangleRatio * 100.0,
            Parameters.RetessellationBadTriangleRatioThreshold * 100.0);
    }
    else if (MaxDriftRadians > ThresholdRadians)
    {
        UE_LOG(LogPlanetaryCreation, Verbose,
            TEXT("Re-tessellation would be considered: Plate %d drifted %.2f° (cooldown %.2f°), but triangle ratio %.2f%% < %.2f%%"),
            Analysis.MaxDriftPlateID,
            Analysis.MaxDriftDegrees,
            Parameters.RetessellationThresholdDegrees,
            LastRetessellationBadTriangleRatio * 100.0,
            Parameters.RetessellationBadTriangleRatioThreshold * 100.0);
    }
}

// ============================================================================
// Milestone 5 Task 1.3: History Management (Undo/Redo)
// ============================================================================

void UTectonicSimulationService::CaptureHistorySnapshot()
{
    // If we're in the middle of the history stack (after undo), truncate future history
    if (CurrentHistoryIndex < HistoryStack.Num() - 1)
    {
        HistoryStack.SetNum(CurrentHistoryIndex + 1);
    }

    // Create new snapshot
    FSimulationHistorySnapshot Snapshot;
    Snapshot.CurrentTimeMy = CurrentTimeMy;
    Snapshot.Plates = Plates;
    Snapshot.SharedVertices = SharedVertices;
    Snapshot.RenderVertices = RenderVertices;
    Snapshot.RenderTriangles = RenderTriangles;
    Snapshot.VertexPlateAssignments = VertexPlateAssignments;
    Snapshot.VertexVelocities = VertexVelocities;
    Snapshot.VertexStressValues = VertexStressValues;
    Snapshot.VertexTemperatureValues = VertexTemperatureValues;
    Snapshot.Boundaries = Boundaries;
    Snapshot.TopologyEvents = TopologyEvents;
    Snapshot.Hotspots = Hotspots;
    Snapshot.InitialPlateCentroids = InitialPlateCentroids;
    Snapshot.TopologyVersion = TopologyVersion;
    Snapshot.SurfaceDataVersion = SurfaceDataVersion;

    // Milestone 5: Capture erosion state
    Snapshot.VertexElevationValues = VertexElevationValues;
    Snapshot.VertexErosionRates = VertexErosionRates;
    Snapshot.VertexSedimentThickness = VertexSedimentThickness;
    Snapshot.VertexCrustAge = VertexCrustAge;

    // Milestone 6: Capture terrane state
    Snapshot.Terranes = Terranes;
    Snapshot.NextTerraneID = NextTerraneID;
    Snapshot.VertexRidgeDirections = VertexRidgeDirections;
    Snapshot.RenderVertexBoundaryCache = RenderVertexBoundaryCache;

    // Add to stack
    HistoryStack.Add(Snapshot);
    CurrentHistoryIndex = HistoryStack.Num() - 1;

    // Enforce max history size (sliding window)
    if (HistoryStack.Num() > MaxHistorySize)
    {
        HistoryStack.RemoveAt(0);
        CurrentHistoryIndex = HistoryStack.Num() - 1;
        UE_LOG(LogPlanetaryCreation, Verbose, TEXT("History stack full, removed oldest snapshot (max %d)"), MaxHistorySize);
    }

    UE_LOG(LogPlanetaryCreation, Verbose, TEXT("CaptureHistorySnapshot: Snapshot %d captured at %.1f My"),
        CurrentHistoryIndex, CurrentTimeMy);
}

void UTectonicSimulationService::RestoreRidgeCacheFromSnapshot(const FSimulationHistorySnapshot& Snapshot)
{
    const int32 VertexCount = Snapshot.RenderVertices.Num();

    VertexRidgeDirections = Snapshot.VertexRidgeDirections;
    RenderVertexBoundaryCache = Snapshot.RenderVertexBoundaryCache;

    EnsureRidgeDirtyMaskSize(VertexCount);

    if (VertexCount <= 0 || VertexRidgeDirections.Num() != VertexCount)
    {
        RidgeDirectionDirtyMask.Reset();
        RidgeDirectionDirtyCount = 0;
        RidgeDirectionFloatSoA.DirX.Reset();
        RidgeDirectionFloatSoA.DirY.Reset();
        RidgeDirectionFloatSoA.DirZ.Reset();
        RidgeDirectionFloatSoA.CachedTopologyVersion = INDEX_NONE;
        RidgeDirectionFloatSoA.CachedVertexCount = 0;
        CachedRidgeDirectionTopologyVersion = INDEX_NONE;
        CachedRidgeDirectionVertexCount = 0;
        LastRidgeDirectionUpdateCount = 0;
        LastRidgeDirtyVertexCount = 0;
        LastRidgeCacheHitCount = 0;
        LastRidgeMissingTangentCount = 0;
        LastRidgePoorAlignmentCount = 0;
        LastRidgeGradientFallbackCount = 0;
        return;
    }

    RidgeDirectionDirtyMask.Init(false, VertexCount);
    RidgeDirectionDirtyCount = 0;

    FRidgeDirectionFloatSoA& SoA = RidgeDirectionFloatSoA;
    SoA.DirX.SetNum(VertexCount);
    SoA.DirY.SetNum(VertexCount);
    SoA.DirZ.SetNum(VertexCount);

    for (int32 Index = 0; Index < VertexCount; ++Index)
    {
        const FVector3d SafeDir = VertexRidgeDirections[Index].GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
        VertexRidgeDirections[Index] = SafeDir;
        SoA.DirX[Index] = static_cast<float>(SafeDir.X);
        SoA.DirY[Index] = static_cast<float>(SafeDir.Y);
        SoA.DirZ[Index] = static_cast<float>(SafeDir.Z);
    }

    CachedRidgeDirectionTopologyVersion = TopologyVersion;
    CachedRidgeDirectionVertexCount = VertexCount;
    SoA.CachedTopologyVersion = TopologyVersion;
    SoA.CachedVertexCount = VertexCount;
    LastRidgeDirectionUpdateCount = 0;
    LastRidgeDirtyVertexCount = 0;
    LastRidgeCacheHitCount = 0;
    LastRidgeMissingTangentCount = 0;
    LastRidgePoorAlignmentCount = 0;
    LastRidgeGradientFallbackCount = 0;
}

bool UTectonicSimulationService::Undo()
{
    if (!CanUndo())
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("Undo: No previous state available"));
        return false;
    }

    CurrentHistoryIndex--;
    const FSimulationHistorySnapshot& Snapshot = HistoryStack[CurrentHistoryIndex];

    // Restore state from snapshot
    CurrentTimeMy = Snapshot.CurrentTimeMy;
    Plates = Snapshot.Plates;
    SharedVertices = Snapshot.SharedVertices;
    RenderVertices = Snapshot.RenderVertices;
    RenderTriangles = Snapshot.RenderTriangles;
    VertexPlateAssignments = Snapshot.VertexPlateAssignments;
    CachedVoronoiAssignments = VertexPlateAssignments;
    VertexVelocities = Snapshot.VertexVelocities;
    VertexStressValues = Snapshot.VertexStressValues;
    VertexTemperatureValues = Snapshot.VertexTemperatureValues;
    Boundaries = Snapshot.Boundaries;
    TopologyEvents = Snapshot.TopologyEvents;
    Hotspots = Snapshot.Hotspots;
    InitialPlateCentroids = Snapshot.InitialPlateCentroids;
    TopologyVersion = Snapshot.TopologyVersion;
    SurfaceDataVersion = Snapshot.SurfaceDataVersion;

    // Milestone 5: Restore erosion state
    VertexElevationValues = Snapshot.VertexElevationValues;
    VertexErosionRates = Snapshot.VertexErosionRates;
    VertexSedimentThickness = Snapshot.VertexSedimentThickness;
    VertexCrustAge = Snapshot.VertexCrustAge;

    // Milestone 6: Restore terrane state
    Terranes = Snapshot.Terranes;
    NextTerraneID = Snapshot.NextTerraneID;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Undo: Restored snapshot %d (%.1f My)"),
        CurrentHistoryIndex, CurrentTimeMy);
    RestoreRidgeCacheFromSnapshot(Snapshot);
    BumpOceanicAmplificationSerial();
    return true;
}

bool UTectonicSimulationService::Redo()
{
    if (!CanRedo())
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("Redo: No future state available"));
        return false;
    }

    CurrentHistoryIndex++;
    const FSimulationHistorySnapshot& Snapshot = HistoryStack[CurrentHistoryIndex];

    // Restore state from snapshot
    CurrentTimeMy = Snapshot.CurrentTimeMy;
    Plates = Snapshot.Plates;
    SharedVertices = Snapshot.SharedVertices;
    RenderVertices = Snapshot.RenderVertices;
    RenderTriangles = Snapshot.RenderTriangles;
    VertexPlateAssignments = Snapshot.VertexPlateAssignments;
    CachedVoronoiAssignments = VertexPlateAssignments;
    VertexVelocities = Snapshot.VertexVelocities;
    VertexStressValues = Snapshot.VertexStressValues;
    VertexTemperatureValues = Snapshot.VertexTemperatureValues;
    Boundaries = Snapshot.Boundaries;
    TopologyEvents = Snapshot.TopologyEvents;
    Hotspots = Snapshot.Hotspots;
    InitialPlateCentroids = Snapshot.InitialPlateCentroids;
    TopologyVersion = Snapshot.TopologyVersion;
    SurfaceDataVersion = Snapshot.SurfaceDataVersion;

    // Milestone 5: Restore erosion state
    VertexElevationValues = Snapshot.VertexElevationValues;
    VertexErosionRates = Snapshot.VertexErosionRates;
    VertexSedimentThickness = Snapshot.VertexSedimentThickness;
    VertexCrustAge = Snapshot.VertexCrustAge;

    // Milestone 6: Restore terrane state
    Terranes = Snapshot.Terranes;
    NextTerraneID = Snapshot.NextTerraneID;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("Redo: Restored snapshot %d (%.1f My)"),
        CurrentHistoryIndex, CurrentTimeMy);
    RestoreRidgeCacheFromSnapshot(Snapshot);
    BumpOceanicAmplificationSerial();
    return true;
}

bool UTectonicSimulationService::JumpToHistoryIndex(int32 Index)
{
    if (!HistoryStack.IsValidIndex(Index))
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("JumpToHistoryIndex: Invalid index %d (stack size %d)"),
            Index, HistoryStack.Num());
        return false;
    }

    CurrentHistoryIndex = Index;
    const FSimulationHistorySnapshot& Snapshot = HistoryStack[CurrentHistoryIndex];

    // Restore state from snapshot
    CurrentTimeMy = Snapshot.CurrentTimeMy;
    Plates = Snapshot.Plates;
    SharedVertices = Snapshot.SharedVertices;
    RenderVertices = Snapshot.RenderVertices;
    RenderTriangles = Snapshot.RenderTriangles;
    VertexPlateAssignments = Snapshot.VertexPlateAssignments;
    CachedVoronoiAssignments = VertexPlateAssignments;
    VertexVelocities = Snapshot.VertexVelocities;
    VertexStressValues = Snapshot.VertexStressValues;
    VertexTemperatureValues = Snapshot.VertexTemperatureValues;
    Boundaries = Snapshot.Boundaries;
    TopologyEvents = Snapshot.TopologyEvents;
    Hotspots = Snapshot.Hotspots;
    InitialPlateCentroids = Snapshot.InitialPlateCentroids;
    TopologyVersion = Snapshot.TopologyVersion;
    SurfaceDataVersion = Snapshot.SurfaceDataVersion;

    // Milestone 5: Restore erosion state
    VertexElevationValues = Snapshot.VertexElevationValues;
    VertexErosionRates = Snapshot.VertexErosionRates;
    VertexSedimentThickness = Snapshot.VertexSedimentThickness;
    VertexCrustAge = Snapshot.VertexCrustAge;

    // Milestone 6: Restore terrane state
    Terranes = Snapshot.Terranes;
    NextTerraneID = Snapshot.NextTerraneID;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("JumpToHistoryIndex: Jumped to snapshot %d (%.1f My)"),
        CurrentHistoryIndex, CurrentTimeMy);
    RestoreRidgeCacheFromSnapshot(Snapshot);
    BumpOceanicAmplificationSerial();
    return true;
}

// ========================================
// Milestone 6 Task 1.1: Terrane Mechanics
// ========================================

bool UTectonicSimulationService::ValidateTopology(FString& OutErrorMessage) const
{
    const int32 TotalVertices = RenderVertices.Num();
    const int32 F = RenderTriangles.Num() / 3;

    int32 ActiveVertexCount = 0;
    for (int32 VertexIdx = 0; VertexIdx < VertexPlateAssignments.Num(); ++VertexIdx)
    {
        if (VertexPlateAssignments[VertexIdx] != INDEX_NONE)
        {
            ActiveVertexCount++;
        }
    }

    if (ActiveVertexCount == 0 || F == 0)
    {
        OutErrorMessage = TEXT("Empty mesh: no vertices or faces");
        return false;
    }

    // Compute unique edges
    TSet<TPair<int32, int32>> UniqueEdges;
    TMap<TPair<int32, int32>, int32> EdgeCounts;

    for (int32 i = 0; i < RenderTriangles.Num(); i += 3)
    {
        int32 V0 = RenderTriangles[i];
        int32 V1 = RenderTriangles[i + 1];
        int32 V2 = RenderTriangles[i + 2];

        // Validate triangle indices
        if (V0 < 0 || V0 >= TotalVertices || V1 < 0 || V1 >= TotalVertices || V2 < 0 || V2 >= TotalVertices)
        {
            OutErrorMessage = FString::Printf(TEXT("Invalid triangle indices: (%d, %d, %d), vertex count: %d"),
                V0, V1, V2, TotalVertices);
            return false;
        }

        // Check for degenerate triangles
        if (V0 == V1 || V1 == V2 || V2 == V0)
        {
            OutErrorMessage = FString::Printf(TEXT("Degenerate triangle at face %d: (%d, %d, %d)"),
                i / 3, V0, V1, V2);
            return false;
        }

        // Add edges (canonical order: min vertex first)
        auto Edge01 = MakeTuple(FMath::Min(V0, V1), FMath::Max(V0, V1));
        auto Edge12 = MakeTuple(FMath::Min(V1, V2), FMath::Max(V1, V2));
        auto Edge20 = MakeTuple(FMath::Min(V2, V0), FMath::Max(V2, V0));

        UniqueEdges.Add(Edge01);
        UniqueEdges.Add(Edge12);
        UniqueEdges.Add(Edge20);

        EdgeCounts.FindOrAdd(Edge01)++;
        EdgeCounts.FindOrAdd(Edge12)++;
        EdgeCounts.FindOrAdd(Edge20)++;
    }

    const int32 E = UniqueEdges.Num();
    const int32 EulerChar = ActiveVertexCount - E + F;

    // Validation 1: Euler characteristic (must be 2 for closed sphere)
    if (EulerChar != 2)
    {
        OutErrorMessage = FString::Printf(TEXT("Invalid Euler characteristic: V=%d, E=%d, F=%d, V-E+F=%d (expected 2)"),
            ActiveVertexCount, E, F, EulerChar);
        return false;
    }

    // Validation 2: Manifold edges (each edge touches exactly 2 triangles)
    int32 NonManifoldEdges = 0;
    for (const auto& Pair : EdgeCounts)
    {
        if (Pair.Value != 2)
        {
            NonManifoldEdges++;
            if (NonManifoldEdges <= 3) // Log first 3 only
            {
                OutErrorMessage += FString::Printf(TEXT("Non-manifold edge: (%d, %d) appears %d times (expected 2); "),
                    Pair.Key.Key, Pair.Key.Value, Pair.Value);
            }
        }
    }

    if (NonManifoldEdges > 0)
    {
        OutErrorMessage = FString::Printf(TEXT("%d non-manifold edges found. "), NonManifoldEdges) + OutErrorMessage;
        return false;
    }

    // Validation 3: No orphaned vertices
    TSet<int32> ReferencedVertices;
    for (int32 Idx : RenderTriangles)
    {
        ReferencedVertices.Add(Idx);
    }

    const int32 OrphanedVertices = ActiveVertexCount - ReferencedVertices.Num();
    if (OrphanedVertices > 0)
    {
        OutErrorMessage = FString::Printf(TEXT("%d orphaned vertices found (not referenced by any triangle)"),
            OrphanedVertices);
        return false;
    }

    // All validations passed
    return true;
}

int32 UTectonicSimulationService::GenerateDeterministicTerraneID(int32 SourcePlateID, double ExtractionTimeMy, const TArray<int32>& SortedVertexIndices, int32 Salt) const
{
    uint32 Hash = 0;
    Hash = FCrc::MemCrc32(&Parameters.Seed, sizeof(int32), Hash);
    Hash = FCrc::MemCrc32(&SourcePlateID, sizeof(int32), Hash);

    const int32 TimeScaled = FMath::RoundToInt(ExtractionTimeMy * 1000.0);
    Hash = FCrc::MemCrc32(&TimeScaled, sizeof(int32), Hash);
    Hash = FCrc::MemCrc32(&Salt, sizeof(int32), Hash);

    if (SortedVertexIndices.Num() > 0)
    {
        Hash = FCrc::MemCrc32(SortedVertexIndices.GetData(), SortedVertexIndices.Num() * sizeof(int32), Hash);
    }

    if (Hash == 0)
    {
        Hash = 0xA62B9D1Du;
    }

    int32 Candidate = static_cast<int32>(Hash & 0x7fffffff);
    if (Candidate == INDEX_NONE)
    {
        Candidate = static_cast<int32>((Hash >> 1) & 0x7fffffff);
        if (Candidate == INDEX_NONE)
        {
            Candidate = 0;
        }
    }

    return Candidate;
}

double UTectonicSimulationService::ComputeTerraneArea(const TArray<int32>& VertexIndices) const
{
    if (VertexIndices.Num() < 3)
    {
        return 0.0; // Too few vertices for area
    }

    // Find triangles that are entirely within the terrane region
    TSet<int32> TerraneVertexSet(VertexIndices);
    double TotalArea = 0.0;
    const double RadiusKm = Parameters.PlanetRadius / 1000.0; // Convert meters to km

    for (int32 i = 0; i < RenderTriangles.Num(); i += 3)
    {
        int32 V0 = RenderTriangles[i];
        int32 V1 = RenderTriangles[i + 1];
        int32 V2 = RenderTriangles[i + 2];

        // Check if all 3 vertices belong to terrane
        if (TerraneVertexSet.Contains(V0) && TerraneVertexSet.Contains(V1) && TerraneVertexSet.Contains(V2))
        {
            // Spherical triangle area using L'Huilier's theorem
            const FVector3d& A = RenderVertices[V0];
            const FVector3d& B = RenderVertices[V1];
            const FVector3d& C = RenderVertices[V2];

            // Edge lengths (angles in radians on unit sphere)
            double a = FMath::Acos(FMath::Clamp(B | C, -1.0, 1.0)); // Angle BC
            double b = FMath::Acos(FMath::Clamp(C | A, -1.0, 1.0)); // Angle CA
            double c = FMath::Acos(FMath::Clamp(A | B, -1.0, 1.0)); // Angle AB

            // Semi-perimeter
            double s = (a + b + c) * 0.5;

            // L'Huilier's formula: tan(E/4) = sqrt(tan(s/2) * tan((s-a)/2) * tan((s-b)/2) * tan((s-c)/2))
            // where E is the spherical excess (area on unit sphere)
            double tan_s2 = FMath::Tan(s * 0.5);
            double tan_sa2 = FMath::Tan((s - a) * 0.5);
            double tan_sb2 = FMath::Tan((s - b) * 0.5);
            double tan_sc2 = FMath::Tan((s - c) * 0.5);

            double tan_E4 = FMath::Sqrt(FMath::Max(0.0, tan_s2 * tan_sa2 * tan_sb2 * tan_sc2));
            double E = 4.0 * FMath::Atan(tan_E4); // Spherical excess (area on unit sphere)

            // Scale by radius²
            double TriangleAreaKm2 = E * RadiusKm * RadiusKm;
            TotalArea += TriangleAreaKm2;
        }
    }

    return TotalArea;
}

void UTectonicSimulationService::InvalidateRenderVertexCaches()
{
    RenderVertexAdjacencyOffsets.Reset();
    RenderVertexAdjacency.Reset();
    RenderVertexAdjacencyWeights.Reset();
    RenderVertexAdjacencyWeightTotals.Reset();
    RenderVertexReverseAdjacency.Reset();
    ConvergentNeighborFlags.Reset();

    PendingCrustAgeResetSeeds.Reset();
    PendingCrustAgeResetMask.Init(false, RenderVertices.Num());

    RenderVertexFloatSoA = FRenderVertexFloatSoA();
    OceanicAmplificationFloatInputs = FOceanicAmplificationFloatInputs();
    RidgeDirectionFloatSoA = FRidgeDirectionFloatSoA();

    RidgeDirectionDirtyMask.Reset();
    RidgeDirectionDirtyCount = 0;
    CachedRidgeDirectionTopologyVersion = INDEX_NONE;
    CachedRidgeDirectionVertexCount = 0;
    LastRidgeDirectionUpdateCount = 0;
}

int32 UTectonicSimulationService::AppendRenderVertexFromRecord(const FTerraneVertexRecord& Record, int32 OverridePlateID)
{
    const int32 NewIndex = RenderVertices.Add(Record.Position);

    auto AppendIfSized = [NewIndex](auto& Array, const auto& Value)
    {
        if (Array.Num() == NewIndex)
        {
            Array.Add(Value);
        }
    };

    AppendIfSized(VertexVelocities, Record.Velocity);
    AppendIfSized(VertexStressValues, Record.Stress);
    AppendIfSized(VertexTemperatureValues, Record.Temperature);
    AppendIfSized(VertexElevationValues, Record.Elevation);
    AppendIfSized(VertexErosionRates, Record.ErosionRate);
    AppendIfSized(VertexSedimentThickness, Record.SedimentThickness);
    AppendIfSized(VertexCrustAge, Record.CrustAge);
    AppendIfSized(VertexAmplifiedElevation, Record.AmplifiedElevation);
    AppendIfSized(VertexRidgeDirections, Record.RidgeDirection);

    if (VertexPlateAssignments.Num() == NewIndex)
    {
        VertexPlateAssignments.Add(OverridePlateID);
    }
    else if (VertexPlateAssignments.Num() == 0)
    {
        VertexPlateAssignments.SetNum(NewIndex + 1);
        VertexPlateAssignments.Last() = OverridePlateID;
    }
    else if (VertexPlateAssignments.Num() == NewIndex + 1)
    {
        VertexPlateAssignments[NewIndex] = OverridePlateID;
    }
    else
    {
        VertexPlateAssignments.Add(OverridePlateID);
    }

    return NewIndex;
}

void UTectonicSimulationService::CompactRenderVertexData(const TArray<int32>& VerticesToRemove, TArray<int32>& OutOldToNew)
{
    const int32 OriginalCount = RenderVertices.Num();
    OutOldToNew.SetNum(OriginalCount);

    if (VerticesToRemove.Num() == 0 || OriginalCount == 0)
    {
        for (int32 Index = 0; Index < OriginalCount; ++Index)
        {
            OutOldToNew[Index] = Index;
        }
        return;
    }

    TBitArray<> RemovalMask(false, OriginalCount);
    for (int32 VertexIdx : VerticesToRemove)
    {
        if (VertexIdx >= 0 && VertexIdx < OriginalCount)
        {
            RemovalMask[VertexIdx] = true;
        }
    }

    const int32 KeptReserve = FMath::Max(OriginalCount - VerticesToRemove.Num(), 0);

    TArray<FVector3d> NewRenderVertices;
    NewRenderVertices.Reserve(KeptReserve);

    const bool bHasVelocities = VertexVelocities.Num() == OriginalCount;
    const bool bHasStress = VertexStressValues.Num() == OriginalCount;
    const bool bHasTemperature = VertexTemperatureValues.Num() == OriginalCount;
    const bool bHasElevation = VertexElevationValues.Num() == OriginalCount;
    const bool bHasErosion = VertexErosionRates.Num() == OriginalCount;
    const bool bHasSediment = VertexSedimentThickness.Num() == OriginalCount;
    const bool bHasCrustAge = VertexCrustAge.Num() == OriginalCount;
    const bool bHasAmplified = VertexAmplifiedElevation.Num() == OriginalCount;
    const bool bHasRidgeDir = VertexRidgeDirections.Num() == OriginalCount;
    const bool bHasAssignments = VertexPlateAssignments.Num() == OriginalCount;

    TArray<FVector3d> NewVelocities; if (bHasVelocities) { NewVelocities.Reserve(KeptReserve); }
    TArray<double> NewStress; if (bHasStress) { NewStress.Reserve(KeptReserve); }
    TArray<double> NewTemperature; if (bHasTemperature) { NewTemperature.Reserve(KeptReserve); }
    TArray<double> NewElevation; if (bHasElevation) { NewElevation.Reserve(KeptReserve); }
    TArray<double> NewErosion; if (bHasErosion) { NewErosion.Reserve(KeptReserve); }
    TArray<double> NewSediment; if (bHasSediment) { NewSediment.Reserve(KeptReserve); }
    TArray<double> NewCrustAge; if (bHasCrustAge) { NewCrustAge.Reserve(KeptReserve); }
    TArray<double> NewAmplified; if (bHasAmplified) { NewAmplified.Reserve(KeptReserve); }
    TArray<FVector3d> NewRidgeDir; if (bHasRidgeDir) { NewRidgeDir.Reserve(KeptReserve); }
    TArray<int32> NewAssignments; if (bHasAssignments) { NewAssignments.Reserve(KeptReserve); }

    for (int32 Index = 0; Index < OriginalCount; ++Index)
    {
        if (RemovalMask[Index])
        {
            OutOldToNew[Index] = INDEX_NONE;
            continue;
        }

        const int32 NewIndex = NewRenderVertices.Num();
        OutOldToNew[Index] = NewIndex;

        NewRenderVertices.Add(RenderVertices[Index]);

        if (bHasVelocities) { NewVelocities.Add(VertexVelocities[Index]); }
        if (bHasStress) { NewStress.Add(VertexStressValues[Index]); }
        if (bHasTemperature) { NewTemperature.Add(VertexTemperatureValues[Index]); }
        if (bHasElevation) { NewElevation.Add(VertexElevationValues[Index]); }
        if (bHasErosion) { NewErosion.Add(VertexErosionRates[Index]); }
        if (bHasSediment) { NewSediment.Add(VertexSedimentThickness[Index]); }
        if (bHasCrustAge) { NewCrustAge.Add(VertexCrustAge[Index]); }
        if (bHasAmplified) { NewAmplified.Add(VertexAmplifiedElevation[Index]); }
        if (bHasRidgeDir) { NewRidgeDir.Add(VertexRidgeDirections[Index]); }
        if (bHasAssignments) { NewAssignments.Add(VertexPlateAssignments[Index]); }
    }

    RenderVertices = MoveTemp(NewRenderVertices);

    if (bHasVelocities) { VertexVelocities = MoveTemp(NewVelocities); }
    else { VertexVelocities.Reset(); }

    if (bHasStress) { VertexStressValues = MoveTemp(NewStress); }
    else { VertexStressValues.Reset(); }

    if (bHasTemperature) { VertexTemperatureValues = MoveTemp(NewTemperature); }
    else { VertexTemperatureValues.Reset(); }

    if (bHasElevation) { VertexElevationValues = MoveTemp(NewElevation); }
    else { VertexElevationValues.Reset(); }

    if (bHasErosion) { VertexErosionRates = MoveTemp(NewErosion); }
    else { VertexErosionRates.Reset(); }

    if (bHasSediment) { VertexSedimentThickness = MoveTemp(NewSediment); }
    else { VertexSedimentThickness.Reset(); }

    if (bHasCrustAge) { VertexCrustAge = MoveTemp(NewCrustAge); }
    else { VertexCrustAge.Reset(); }

    if (bHasAmplified) { VertexAmplifiedElevation = MoveTemp(NewAmplified); }
    else { VertexAmplifiedElevation.Reset(); }

    if (bHasRidgeDir) { VertexRidgeDirections = MoveTemp(NewRidgeDir); }
    else { VertexRidgeDirections.Reset(); }

    if (bHasAssignments) { VertexPlateAssignments = MoveTemp(NewAssignments); }
    else { VertexPlateAssignments.SetNum(RenderVertices.Num()); }
    CachedVoronoiAssignments = VertexPlateAssignments;
}

const FContinentalTerrane* UTectonicSimulationService::GetTerraneByID(int32 TerraneID) const
{
    for (const FContinentalTerrane& Terrane : Terranes)
    {
        if (Terrane.TerraneID == TerraneID)
        {
            return &Terrane;
        }
    }
    return nullptr;
}

bool UTectonicSimulationService::ExtractTerrane(int32 SourcePlateID, const TArray<int32>& TerraneVertexIndices, int32& OutTerraneID)
{
    UE_LOG(LogPlanetaryCreation, Log, TEXT("ExtractTerrane: Attempting to extract %d vertices from plate %d"),
        TerraneVertexIndices.Num(), SourcePlateID);

    FTectonicPlate* SourcePlate = nullptr;
    for (FTectonicPlate& Plate : Plates)
    {
        if (Plate.PlateID == SourcePlateID)
        {
            SourcePlate = &Plate;
            break;
        }
    }

    if (!SourcePlate)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Source plate %d not found"), SourcePlateID);
        return false;
    }

    if (SourcePlate->CrustType != ECrustType::Continental)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Source plate %d is not continental"), SourcePlateID);
        return false;
    }

    if (TerraneVertexIndices.Num() == 0)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("ExtractTerrane: No vertices provided"));
        return false;
    }

    TArray<int32> SortedVertexIndices = TerraneVertexIndices;
    SortedVertexIndices.Sort();
    int32 WriteIndex = 0;
    int32 LastValue = MIN_int32;
    bool bHasLast = false;
    for (int32 Value : SortedVertexIndices)
    {
        if (!bHasLast || Value != LastValue)
        {
            SortedVertexIndices[WriteIndex++] = Value;
            LastValue = Value;
            bHasLast = true;
        }
    }
    SortedVertexIndices.SetNum(WriteIndex);

    const int32 VertexCount = RenderVertices.Num();
    for (int32 VertexIdx : SortedVertexIndices)
    {
        if (VertexIdx < 0 || VertexIdx >= VertexCount)
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Invalid vertex index %d (range: 0-%d)"), VertexIdx, VertexCount - 1);
            return false;
        }

        if (VertexPlateAssignments[VertexIdx] != SourcePlateID)
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Vertex %d does not belong to plate %d (assigned to %d)"),
                VertexIdx, SourcePlateID, VertexPlateAssignments[VertexIdx]);
            return false;
        }
    }

    const double TerraneArea = ComputeTerraneArea(SortedVertexIndices);
    if (TerraneArea < 100.0)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("ExtractTerrane: Terrane area %.2f km² below minimum 100 km², rejecting extraction"), TerraneArea);
        return false;
    }

    int32 PlateVertexCount = 0;
    for (int32 Assignment : VertexPlateAssignments)
    {
        if (Assignment == SourcePlateID)
        {
            PlateVertexCount++;
        }
    }

    if (SortedVertexIndices.Num() == PlateVertexCount)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("ExtractTerrane: Extracting all %d vertices from plate %d (treat as plate split)"),
            PlateVertexCount, SourcePlateID);
        return false;
    }

    FString ValidationError;
    if (!ValidateTopology(ValidationError))
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Pre-extraction topology invalid: %s"), *ValidationError);
        return false;
    }

    const int32 SavedNextTerraneID = NextTerraneID;

    TSet<int32> TerraneVertexSet(SortedVertexIndices);

    TArray<FIntVector> InsideTriangles;
    InsideTriangles.Reserve(RenderTriangles.Num() / 3);

    TArray<int32> RemainingTriangles;
    RemainingTriangles.Reserve(RenderTriangles.Num());

    for (int32 TriIdx = 0; TriIdx < RenderTriangles.Num(); TriIdx += 3)
    {
        const int32 A = RenderTriangles[TriIdx];
        const int32 B = RenderTriangles[TriIdx + 1];
        const int32 C = RenderTriangles[TriIdx + 2];

        const bool bAIn = TerraneVertexSet.Contains(A);
        const bool bBIn = TerraneVertexSet.Contains(B);
        const bool bCIn = TerraneVertexSet.Contains(C);

        if (bAIn && bBIn && bCIn)
        {
            InsideTriangles.Add(FIntVector(A, B, C));
        }
        else
        {
            RemainingTriangles.Add(A);
            RemainingTriangles.Add(B);
            RemainingTriangles.Add(C);
        }
    }

    if (InsideTriangles.Num() == 0)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("ExtractTerrane: No fully contained triangles found for extraction"));
        return false;
    }

    TMap<TPair<int32, int32>, int32> EdgeUseCounts;
    EdgeUseCounts.Reserve(InsideTriangles.Num() * 3);

    auto RecordEdge = [&EdgeUseCounts](int32 V0, int32 V1)
    {
        const TPair<int32, int32> Key(FMath::Min(V0, V1), FMath::Max(V0, V1));
        EdgeUseCounts.FindOrAdd(Key)++;
    };

    for (const FIntVector& Tri : InsideTriangles)
    {
        RecordEdge(Tri.X, Tri.Y);
        RecordEdge(Tri.Y, Tri.Z);
        RecordEdge(Tri.Z, Tri.X);
    }

    TMap<int32, TArray<int32>> BoundaryAdjacency;
    for (const TPair<TPair<int32, int32>, int32>& Pair : EdgeUseCounts)
    {
        if (Pair.Value == 1)
        {
            const int32 U = Pair.Key.Key;
            const int32 V = Pair.Key.Value;
            BoundaryAdjacency.FindOrAdd(U).Add(V);
            BoundaryAdjacency.FindOrAdd(V).Add(U);
        }
    }

    if (BoundaryAdjacency.Num() == 0)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Failed to identify boundary for terrane"));
        return false;
    }

    for (const TPair<int32, TArray<int32>>& Pair : BoundaryAdjacency)
    {
        if (Pair.Value.Num() < 2)
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Non-manifold boundary detected around vertex %d"), Pair.Key);
            return false;
        }
    }

    TArray<TArray<int32>> BoundaryLoops;
    BoundaryLoops.Reserve(BoundaryAdjacency.Num());
    TSet<int32> VisitedBoundaryVerts;

    for (const TPair<int32, TArray<int32>>& Entry : BoundaryAdjacency)
    {
        const int32 StartVertex = Entry.Key;
        if (VisitedBoundaryVerts.Contains(StartVertex))
        {
            continue;
        }

        TArray<int32> Loop;
        Loop.Reserve(Entry.Value.Num());
        TSet<int32> LoopVisited;

        int32 Current = StartVertex;
        int32 Previous = INDEX_NONE;
        int32 SafetyCounter = 0;
        const int32 MaxIterations = FMath::Max(FMath::Max(BoundaryAdjacency.Num() * 4, SortedVertexIndices.Num() * 4), 64);

        while (true)
        {
            if (LoopVisited.Contains(Current))
            {
                if (Current == StartVertex)
                {
                    break; // Closed the loop cleanly
                }

                UE_LOG(LogPlanetaryCreation, Error,
                    TEXT("ExtractTerrane: Detected cycle visiting boundary vertex %d without returning to start %d"),
                    Current, StartVertex);
                return false;
            }

            LoopVisited.Add(Current);
            Loop.Add(Current);
            VisitedBoundaryVerts.Add(Current);

            const TArray<int32>* NeighborsPtr = BoundaryAdjacency.Find(Current);
            if (!NeighborsPtr)
            {
                UE_LOG(LogPlanetaryCreation, Error,
                    TEXT("ExtractTerrane: Boundary adjacency missing entry for vertex %d"), Current);
                return false;
            }

            const TArray<int32>& Neighbors = *NeighborsPtr;
            int32 NextVertex = INDEX_NONE;
            for (int32 Neighbor : Neighbors)
            {
                if (Neighbor == Previous)
                {
                    continue;
                }

                if (!LoopVisited.Contains(Neighbor) || Neighbor == StartVertex)
                {
                    NextVertex = Neighbor;
                    break;
                }
            }

            if (NextVertex == INDEX_NONE)
            {
                UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Incomplete boundary loop starting at %d"), StartVertex);
                return false;
            }

            Previous = Current;
            Current = NextVertex;

            if (Current == StartVertex)
            {
                break;
            }

            if (++SafetyCounter > MaxIterations)
            {
                UE_LOG(LogPlanetaryCreation, Error,
                    TEXT("ExtractTerrane: Boundary loop traversal exceeded guard (%d iterations) starting at %d"),
                    SafetyCounter, StartVertex);
                return false;
            }
        }

        if (Loop.Num() < 3)
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Boundary loop too small (%d vertices)"), Loop.Num());
            return false;
        }

        BoundaryLoops.Add(MoveTemp(Loop));
    }

    FContinentalTerrane NewTerrane;
    int32 AssignedTerraneID = INDEX_NONE;
    const int32 SaltBase = SavedNextTerraneID;
    for (int32 Attempt = 0; Attempt < 8; ++Attempt)
    {
        const int32 Candidate = GenerateDeterministicTerraneID(SourcePlateID, CurrentTimeMy, SortedVertexIndices, SaltBase + Attempt);
        if (Candidate != INDEX_NONE && GetTerraneByID(Candidate) == nullptr)
        {
            AssignedTerraneID = Candidate;
            break;
        }
    }

    if (AssignedTerraneID == INDEX_NONE)
    {
        AssignedTerraneID = SaltBase;
    }

    NewTerrane.TerraneID = AssignedTerraneID;
    NextTerraneID++;
    NewTerrane.State = ETerraneState::Extracted;
    NewTerrane.SourcePlateID = SourcePlateID;
    NewTerrane.CarrierPlateID = INDEX_NONE;
    NewTerrane.TargetPlateID = INDEX_NONE;
    NewTerrane.AreaKm2 = TerraneArea;
    NewTerrane.ExtractionTimeMy = CurrentTimeMy;
    NewTerrane.ReattachmentTimeMy = 0.0;
    NewTerrane.OriginalVertexIndices = SortedVertexIndices;

    const bool bHasVelocities = VertexVelocities.Num() == VertexCount;
    const bool bHasStress = VertexStressValues.Num() == VertexCount;
    const bool bHasTemperature = VertexTemperatureValues.Num() == VertexCount;
    const bool bHasElevation = VertexElevationValues.Num() == VertexCount;
    const bool bHasErosion = VertexErosionRates.Num() == VertexCount;
    const bool bHasSediment = VertexSedimentThickness.Num() == VertexCount;
    const bool bHasCrustAge = VertexCrustAge.Num() == VertexCount;
    const bool bHasAmplified = VertexAmplifiedElevation.Num() == VertexCount;
    const bool bHasRidgeDir = VertexRidgeDirections.Num() == VertexCount;

    TMap<int32, int32> LocalIndexMap;
    LocalIndexMap.Reserve(SortedVertexIndices.Num());
    NewTerrane.VertexPayload.Reserve(SortedVertexIndices.Num());

    FVector3d TerraneCentroid = FVector3d::ZeroVector;

    for (int32 VertexIdx : SortedVertexIndices)
    {
        FTerraneVertexRecord Record;
        Record.Position = RenderVertices[VertexIdx];
        if (bHasVelocities) { Record.Velocity = VertexVelocities[VertexIdx]; }
        if (bHasStress) { Record.Stress = VertexStressValues[VertexIdx]; }
        if (bHasTemperature) { Record.Temperature = VertexTemperatureValues[VertexIdx]; }
        if (bHasElevation) { Record.Elevation = VertexElevationValues[VertexIdx]; }
        if (bHasErosion) { Record.ErosionRate = VertexErosionRates[VertexIdx]; }
        if (bHasSediment) { Record.SedimentThickness = VertexSedimentThickness[VertexIdx]; }
        if (bHasCrustAge) { Record.CrustAge = VertexCrustAge[VertexIdx]; }
        if (bHasAmplified) { Record.AmplifiedElevation = VertexAmplifiedElevation[VertexIdx]; }
        if (bHasRidgeDir) { Record.RidgeDirection = VertexRidgeDirections[VertexIdx]; }
        Record.PlateID = SourcePlateID;

        TerraneCentroid += Record.Position;

        const int32 LocalIndex = NewTerrane.VertexPayload.Num();
        LocalIndexMap.Add(VertexIdx, LocalIndex);
        NewTerrane.VertexPayload.Add(Record);
    }

    TerraneCentroid.Normalize();
    NewTerrane.Centroid = TerraneCentroid;

    NewTerrane.ExtractedTriangles.Reserve(InsideTriangles.Num() * 3);
    for (const FIntVector& Tri : InsideTriangles)
    {
        const int32* LocalA = LocalIndexMap.Find(Tri.X);
        const int32* LocalB = LocalIndexMap.Find(Tri.Y);
        const int32* LocalC = LocalIndexMap.Find(Tri.Z);

        if (!LocalA || !LocalB || !LocalC)
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Failed to map triangle (%d, %d, %d) to local indices"), Tri.X, Tri.Y, Tri.Z);
            NextTerraneID = SavedNextTerraneID;
            return false;
        }

        NewTerrane.ExtractedTriangles.Add(*LocalA);
        NewTerrane.ExtractedTriangles.Add(*LocalB);
        NewTerrane.ExtractedTriangles.Add(*LocalC);
    }

    TArray<TArray<int32>> BoundaryLoopsLocal;
    BoundaryLoopsLocal.Reserve(BoundaryLoops.Num());
    for (const TArray<int32>& Loop : BoundaryLoops)
    {
        TArray<int32> LocalLoop;
        LocalLoop.Reserve(Loop.Num());
        for (int32 OriginalIdx : Loop)
        {
            const int32* LocalIdxPtr = LocalIndexMap.Find(OriginalIdx);
            if (!LocalIdxPtr)
            {
                UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Boundary vertex %d not mapped to local payload"), OriginalIdx);
                NextTerraneID = SavedNextTerraneID;
                return false;
            }
            LocalLoop.Add(*LocalIdxPtr);
        }
        BoundaryLoopsLocal.Add(MoveTemp(LocalLoop));
    }

    const TArray<FVector3d> BackupRenderVertices = RenderVertices;
    const TArray<int32> BackupRenderTriangles = RenderTriangles;
    const TArray<int32> BackupVertexAssignments = VertexPlateAssignments;
    const TArray<FVector3d> BackupVertexVelocities = VertexVelocities;
    const TArray<double> BackupVertexStress = VertexStressValues;
    const TArray<double> BackupVertexTemperature = VertexTemperatureValues;
    const TArray<double> BackupVertexElevation = VertexElevationValues;
    const TArray<double> BackupVertexErosion = VertexErosionRates;
    const TArray<double> BackupVertexSediment = VertexSedimentThickness;
    const TArray<double> BackupVertexCrustAge = VertexCrustAge;
    const TArray<double> BackupVertexAmplified = VertexAmplifiedElevation;
    const TArray<FVector3d> BackupVertexRidgeDirections = VertexRidgeDirections;
    const TArray<int32> BackupAdjOffsets = RenderVertexAdjacencyOffsets;
    const TArray<int32> BackupAdjacency = RenderVertexAdjacency;
    const TArray<float> BackupAdjWeights = RenderVertexAdjacencyWeights;
    const TArray<int32> BackupReverseAdjacency = RenderVertexReverseAdjacency;
    const TArray<uint8> BackupConvergentFlags = ConvergentNeighborFlags;
    const TArray<int32> BackupPendingSeeds = PendingCrustAgeResetSeeds;
    const TBitArray<> BackupPendingMask = PendingCrustAgeResetMask;

    TArray<int32> WorkingTriangles = MoveTemp(RemainingTriangles);
    TArray<int32> PendingPatchVertexIndices;
    TArray<int32> PendingPatchTriangles;
    TMap<int32, int32> BoundaryDuplicateMap;

    PendingPatchVertexIndices.Reserve(SortedVertexIndices.Num());
    PendingPatchTriangles.Reserve(BoundaryLoopsLocal.Num() * 6);

    for (int32 LoopIdx = 0; LoopIdx < BoundaryLoopsLocal.Num(); ++LoopIdx)
    {
        const TArray<int32>& LocalLoop = BoundaryLoopsLocal[LoopIdx];
        const TArray<int32>& OriginalLoop = BoundaryLoops[LoopIdx];
        const int32 LoopCount = LocalLoop.Num();

        if (LoopCount < 3)
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Local boundary loop too small (%d vertices)"), LoopCount);

            RenderVertices = BackupRenderVertices;
            RenderTriangles = BackupRenderTriangles;
            VertexPlateAssignments = BackupVertexAssignments;
            CachedVoronoiAssignments = VertexPlateAssignments;
            VertexVelocities = BackupVertexVelocities;
            VertexStressValues = BackupVertexStress;
            VertexTemperatureValues = BackupVertexTemperature;
            VertexElevationValues = BackupVertexElevation;
            VertexErosionRates = BackupVertexErosion;
            VertexSedimentThickness = BackupVertexSediment;
            VertexCrustAge = BackupVertexCrustAge;
            VertexAmplifiedElevation = BackupVertexAmplified;
            VertexRidgeDirections = BackupVertexRidgeDirections;
            RenderVertexAdjacencyOffsets = BackupAdjOffsets;
            RenderVertexAdjacency = BackupAdjacency;
            RenderVertexAdjacencyWeights = BackupAdjWeights;
            RenderVertexReverseAdjacency = BackupReverseAdjacency;
            ConvergentNeighborFlags = BackupConvergentFlags;
            PendingCrustAgeResetSeeds = BackupPendingSeeds;
            PendingCrustAgeResetMask = BackupPendingMask;
            NextTerraneID = SavedNextTerraneID;
            return false;
        }

        TArray<int32> DuplicatedIndices;
        DuplicatedIndices.Reserve(LoopCount);

        for (int32 VertexOffset = 0; VertexOffset < LoopCount; ++VertexOffset)
        {
            const int32 LocalVertexIdx = LocalLoop[VertexOffset];
            const int32 OriginalVertexIdx = OriginalLoop[VertexOffset];

            int32 DuplicateIdx = INDEX_NONE;
            if (int32* ExistingDuplicate = BoundaryDuplicateMap.Find(OriginalVertexIdx))
            {
                DuplicateIdx = *ExistingDuplicate;
            }
            else
            {
                FTerraneVertexRecord Record = NewTerrane.VertexPayload[LocalVertexIdx];
                const double BaselineElevation = (SourcePlate && SourcePlate->CrustType == ECrustType::Continental)
                    ? PaperElevationConstants::ContinentalBaseline_m
                    : PaperElevationConstants::AbyssalPlainDepth_m;
                Record.Elevation = BaselineElevation;
                Record.AmplifiedElevation = BaselineElevation;
                DuplicateIdx = AppendRenderVertexFromRecord(Record, SourcePlateID);
                BoundaryDuplicateMap.Add(OriginalVertexIdx, DuplicateIdx);
                PendingPatchVertexIndices.Add(DuplicateIdx);
            }

            FTerraneVertexRecord& PayloadRecord = NewTerrane.VertexPayload[LocalVertexIdx];
            PayloadRecord.ReplacementVertexIndex = DuplicateIdx;

            DuplicatedIndices.Add(DuplicateIdx);

            for (int32& TriangleIndex : WorkingTriangles)
            {
                if (TriangleIndex == OriginalVertexIdx)
                {
                    TriangleIndex = DuplicateIdx;
                }
            }
        }

        FTerraneVertexRecord CenterRecord;
        CenterRecord.PlateID = SourcePlateID;

        for (int32 LocalVertexIdx : LocalLoop)
        {
            const FTerraneVertexRecord& Record = NewTerrane.VertexPayload[LocalVertexIdx];
            CenterRecord.Position += Record.Position;
            CenterRecord.Velocity += Record.Velocity;
            CenterRecord.Stress += Record.Stress;
            CenterRecord.Temperature += Record.Temperature;
            CenterRecord.Elevation += Record.Elevation;
            CenterRecord.ErosionRate += Record.ErosionRate;
            CenterRecord.SedimentThickness += Record.SedimentThickness;
            CenterRecord.CrustAge += Record.CrustAge;
            CenterRecord.AmplifiedElevation += Record.AmplifiedElevation;
            CenterRecord.RidgeDirection += Record.RidgeDirection;
        }

        const double InvCount = 1.0 / static_cast<double>(LoopCount);
        CenterRecord.Position = (CenterRecord.Position * InvCount).GetSafeNormal();
        CenterRecord.Velocity *= InvCount;
        CenterRecord.Stress *= InvCount;
        CenterRecord.Temperature *= InvCount;
        CenterRecord.Elevation *= InvCount;
        CenterRecord.ErosionRate *= InvCount;
        CenterRecord.SedimentThickness *= InvCount;
        CenterRecord.CrustAge *= InvCount;
        CenterRecord.AmplifiedElevation *= InvCount;

        const double RidgeLength = CenterRecord.RidgeDirection.Length();
        if (RidgeLength > UE_DOUBLE_SMALL_NUMBER)
        {
            CenterRecord.RidgeDirection /= RidgeLength;
        }
        else
        {
            CenterRecord.RidgeDirection = FVector3d::ZeroVector;
        }

        const double BaselineElevation = (SourcePlate && SourcePlate->CrustType == ECrustType::Continental)
            ? PaperElevationConstants::ContinentalBaseline_m
            : PaperElevationConstants::AbyssalPlainDepth_m;
        CenterRecord.Elevation = BaselineElevation;
        CenterRecord.AmplifiedElevation = BaselineElevation;
        const int32 CenterIdx = AppendRenderVertexFromRecord(CenterRecord, SourcePlateID);
        PendingPatchVertexIndices.Add(CenterIdx);

        auto QueuePatchTriangle = [&](int32 V0, int32 V1, int32 V2)
        {
            PendingPatchTriangles.Add(V0);
            PendingPatchTriangles.Add(V1);
            PendingPatchTriangles.Add(V2);
        };

        for (int32 i = 0; i < DuplicatedIndices.Num(); ++i)
        {
            const int32 V0 = DuplicatedIndices[i];
            const int32 V1 = DuplicatedIndices[(i + 1) % DuplicatedIndices.Num()];
            const FVector3d& A = RenderVertices[V0];
            const FVector3d& B = RenderVertices[V1];
            const FVector3d& C = RenderVertices[CenterIdx];

            const FVector3d Normal = FVector3d::CrossProduct(B - A, C - A);
            const double Orientation = Normal | A;
            if (Orientation >= 0.0)
            {
                QueuePatchTriangle(V0, CenterIdx, V1);
            }
            else
            {
                QueuePatchTriangle(V0, V1, CenterIdx);
            }
        }
    }

    TArray<int32> OldToNewIndex;
    CompactRenderVertexData(SortedVertexIndices, OldToNewIndex);

    for (FTerraneVertexRecord& PayloadRecord : NewTerrane.VertexPayload)
    {
        if (PayloadRecord.ReplacementVertexIndex != INDEX_NONE)
        {
            if (!OldToNewIndex.IsValidIndex(PayloadRecord.ReplacementVertexIndex) || OldToNewIndex[PayloadRecord.ReplacementVertexIndex] == INDEX_NONE)
            {
                UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Failed to remap replacement vertex %d"), PayloadRecord.ReplacementVertexIndex);

                RenderVertices = BackupRenderVertices;
                RenderTriangles = BackupRenderTriangles;
                VertexPlateAssignments = BackupVertexAssignments;
                CachedVoronoiAssignments = VertexPlateAssignments;
                VertexVelocities = BackupVertexVelocities;
                VertexStressValues = BackupVertexStress;
                VertexTemperatureValues = BackupVertexTemperature;
                VertexElevationValues = BackupVertexElevation;
                VertexErosionRates = BackupVertexErosion;
                VertexSedimentThickness = BackupVertexSediment;
                VertexCrustAge = BackupVertexCrustAge;
                VertexAmplifiedElevation = BackupVertexAmplified;
                VertexRidgeDirections = BackupVertexRidgeDirections;
                RenderVertexAdjacencyOffsets = BackupAdjOffsets;
                RenderVertexAdjacency = BackupAdjacency;
                RenderVertexAdjacencyWeights = BackupAdjWeights;
                RenderVertexReverseAdjacency = BackupReverseAdjacency;
                ConvergentNeighborFlags = BackupConvergentFlags;
                PendingCrustAgeResetSeeds = BackupPendingSeeds;
                PendingCrustAgeResetMask = BackupPendingMask;
                NextTerraneID = SavedNextTerraneID;
                return false;
            }

            PayloadRecord.ReplacementVertexIndex = OldToNewIndex[PayloadRecord.ReplacementVertexIndex];
        }
    }

    auto RemapIndex = [&](int32& Index) -> bool
    {
        if (!OldToNewIndex.IsValidIndex(Index) || OldToNewIndex[Index] == INDEX_NONE)
        {
            return false;
        }
        Index = OldToNewIndex[Index];
        return true;
    };

    for (int32& Index : WorkingTriangles)
    {
        if (!RemapIndex(Index))
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Triangle remap failed for vertex %d"), Index);

            RenderVertices = BackupRenderVertices;
            RenderTriangles = BackupRenderTriangles;
            VertexPlateAssignments = BackupVertexAssignments;
            CachedVoronoiAssignments = VertexPlateAssignments;
            VertexVelocities = BackupVertexVelocities;
            VertexStressValues = BackupVertexStress;
            VertexTemperatureValues = BackupVertexTemperature;
            VertexElevationValues = BackupVertexElevation;
            VertexErosionRates = BackupVertexErosion;
            VertexSedimentThickness = BackupVertexSediment;
            VertexCrustAge = BackupVertexCrustAge;
            VertexAmplifiedElevation = BackupVertexAmplified;
            VertexRidgeDirections = BackupVertexRidgeDirections;
            RenderVertexAdjacencyOffsets = BackupAdjOffsets;
            RenderVertexAdjacency = BackupAdjacency;
            RenderVertexAdjacencyWeights = BackupAdjWeights;
            RenderVertexReverseAdjacency = BackupReverseAdjacency;
            ConvergentNeighborFlags = BackupConvergentFlags;
            PendingCrustAgeResetSeeds = BackupPendingSeeds;
            PendingCrustAgeResetMask = BackupPendingMask;
            NextTerraneID = SavedNextTerraneID;
            return false;
        }
    }

    for (int32& Index : PendingPatchTriangles)
    {
        if (!RemapIndex(Index))
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Patch triangle remap failed for vertex %d"), Index);

            RenderVertices = BackupRenderVertices;
            RenderTriangles = BackupRenderTriangles;
            VertexPlateAssignments = BackupVertexAssignments;
            CachedVoronoiAssignments = VertexPlateAssignments;
            VertexVelocities = BackupVertexVelocities;
            VertexStressValues = BackupVertexStress;
            VertexTemperatureValues = BackupVertexTemperature;
            VertexElevationValues = BackupVertexElevation;
            VertexErosionRates = BackupVertexErosion;
            VertexSedimentThickness = BackupVertexSediment;
            VertexCrustAge = BackupVertexCrustAge;
            VertexAmplifiedElevation = BackupVertexAmplified;
            VertexRidgeDirections = BackupVertexRidgeDirections;
            RenderVertexAdjacencyOffsets = BackupAdjOffsets;
            RenderVertexAdjacency = BackupAdjacency;
            RenderVertexAdjacencyWeights = BackupAdjWeights;
            RenderVertexReverseAdjacency = BackupReverseAdjacency;
            ConvergentNeighborFlags = BackupConvergentFlags;
            PendingCrustAgeResetSeeds = BackupPendingSeeds;
            PendingCrustAgeResetMask = BackupPendingMask;
            NextTerraneID = SavedNextTerraneID;
            return false;
        }
    }

    for (int32& Index : PendingPatchVertexIndices)
    {
        if (!RemapIndex(Index))
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Patch vertex remap failed for vertex %d"), Index);

            RenderVertices = BackupRenderVertices;
            RenderTriangles = BackupRenderTriangles;
            VertexPlateAssignments = BackupVertexAssignments;
            CachedVoronoiAssignments = VertexPlateAssignments;
            VertexVelocities = BackupVertexVelocities;
            VertexStressValues = BackupVertexStress;
            VertexTemperatureValues = BackupVertexTemperature;
            VertexElevationValues = BackupVertexElevation;
            VertexErosionRates = BackupVertexErosion;
            VertexSedimentThickness = BackupVertexSediment;
            VertexCrustAge = BackupVertexCrustAge;
            VertexAmplifiedElevation = BackupVertexAmplified;
            VertexRidgeDirections = BackupVertexRidgeDirections;
            RenderVertexAdjacencyOffsets = BackupAdjOffsets;
            RenderVertexAdjacency = BackupAdjacency;
            RenderVertexAdjacencyWeights = BackupAdjWeights;
            RenderVertexReverseAdjacency = BackupReverseAdjacency;
            ConvergentNeighborFlags = BackupConvergentFlags;
            PendingCrustAgeResetSeeds = BackupPendingSeeds;
            PendingCrustAgeResetMask = BackupPendingMask;
            NextTerraneID = SavedNextTerraneID;
            return false;
        }
    }

    RenderTriangles = MoveTemp(WorkingTriangles);
    RenderTriangles.Append(PendingPatchTriangles);

    NewTerrane.PatchVertexIndices = MoveTemp(PendingPatchVertexIndices);
    NewTerrane.PatchTriangles = MoveTemp(PendingPatchTriangles);

    InvalidateRenderVertexCaches();

    SurfaceDataVersion++;
    TopologyVersion++;
    InvalidateRidgeDirectionCache();
    MarkAllRidgeDirectionsDirty();
    BumpOceanicAmplificationSerial();

    Terranes.Add(NewTerrane);
    OutTerraneID = NewTerrane.TerraneID;

    if (!ValidateTopology(ValidationError))
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("ExtractTerrane: Post-extraction topology invalid: %s"), *ValidationError);

        RenderVertices = BackupRenderVertices;
        RenderTriangles = BackupRenderTriangles;
        VertexPlateAssignments = BackupVertexAssignments;
        CachedVoronoiAssignments = VertexPlateAssignments;
        VertexVelocities = BackupVertexVelocities;
        VertexStressValues = BackupVertexStress;
        VertexTemperatureValues = BackupVertexTemperature;
        VertexElevationValues = BackupVertexElevation;
        VertexErosionRates = BackupVertexErosion;
        VertexSedimentThickness = BackupVertexSediment;
        VertexCrustAge = BackupVertexCrustAge;
        VertexAmplifiedElevation = BackupVertexAmplified;
        VertexRidgeDirections = BackupVertexRidgeDirections;
        RenderVertexAdjacencyOffsets = BackupAdjOffsets;
        RenderVertexAdjacency = BackupAdjacency;
        RenderVertexAdjacencyWeights = BackupAdjWeights;
        RenderVertexReverseAdjacency = BackupReverseAdjacency;
        ConvergentNeighborFlags = BackupConvergentFlags;
        PendingCrustAgeResetSeeds = BackupPendingSeeds;
        PendingCrustAgeResetMask = BackupPendingMask;
        Terranes.Pop();
        NextTerraneID = SavedNextTerraneID;
        SurfaceDataVersion--;
        TopologyVersion--;
        InvalidateRenderVertexCaches();
        return false;
    }

    BuildRenderVertexAdjacency();
    BuildRenderVertexReverseAdjacency();
    UpdateConvergentNeighborFlags();

    UE_LOG(LogPlanetaryCreation, Log, TEXT("ExtractTerrane: Successfully extracted terrane %d (%.2f km²) from plate %d"),
        OutTerraneID, TerraneArea, SourcePlateID);

    AssignTerraneCarrier(OutTerraneID);
    return true;
}

bool UTectonicSimulationService::ReattachTerrane(int32 TerraneID, int32 TargetPlateID)
{
    UE_LOG(LogPlanetaryCreation, Log, TEXT("ReattachTerrane: Attempting to reattach terrane %d to plate %d"),
        TerraneID, TargetPlateID);

    // Find terrane
    FContinentalTerrane* Terrane = nullptr;
    int32 TerraneIndex = INDEX_NONE;
    for (int32 i = 0; i < Terranes.Num(); ++i)
    {
        if (Terranes[i].TerraneID == TerraneID)
        {
            Terrane = &Terranes[i];
            TerraneIndex = i;
            break;
        }
    }

    if (!Terrane)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("ReattachTerrane: Terrane %d not found"), TerraneID);
        return false;
    }

    // Validation: Terrane must be detached (Extracted, Transporting, or Colliding)
    if (Terrane->State != ETerraneState::Extracted &&
        Terrane->State != ETerraneState::Transporting &&
        Terrane->State != ETerraneState::Colliding)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("ReattachTerrane: Terrane %d not in detached state (current: %d, expected: 1=Extracted, 2=Transporting, 3=Colliding)"),
            TerraneID, static_cast<int32>(Terrane->State));
        return false;
    }

    // Find target plate
    FTectonicPlate* TargetPlate = nullptr;
    for (FTectonicPlate& Plate : Plates)
    {
        if (Plate.PlateID == TargetPlateID)
        {
            TargetPlate = &Plate;
            break;
        }
    }

    if (!TargetPlate)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("ReattachTerrane: Target plate %d not found"), TargetPlateID);
        return false;
    }

    // Validation: Target must be continental
    if (TargetPlate->CrustType != ECrustType::Continental)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("ReattachTerrane: Target plate %d is not continental"), TargetPlateID);
        return false;
    }

    FString ValidationError;
    if (!ValidateTopology(ValidationError))
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("ReattachTerrane: Pre-reattachment topology invalid: %s"), *ValidationError);
        return false;
    }

    if (Terrane->VertexPayload.Num() == 0)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("ReattachTerrane: Terrane %d has no stored vertex payload"), TerraneID);
        return false;
    }

    const TArray<FVector3d> BackupRenderVertices = RenderVertices;
    const TArray<int32> BackupRenderTriangles = RenderTriangles;
    const TArray<int32> BackupVertexAssignments = VertexPlateAssignments;
    const TArray<FVector3d> BackupVertexVelocities = VertexVelocities;
    const TArray<double> BackupVertexStress = VertexStressValues;
    const TArray<double> BackupVertexTemperature = VertexTemperatureValues;
    const TArray<double> BackupVertexElevation = VertexElevationValues;
    const TArray<double> BackupVertexErosion = VertexErosionRates;
    const TArray<double> BackupVertexSediment = VertexSedimentThickness;
    const TArray<double> BackupVertexCrustAge = VertexCrustAge;
    const TArray<double> BackupVertexAmplified = VertexAmplifiedElevation;
    const TArray<FVector3d> BackupVertexRidgeDirections = VertexRidgeDirections;
    const TArray<int32> BackupAdjOffsets = RenderVertexAdjacencyOffsets;
    const TArray<int32> BackupAdjacency = RenderVertexAdjacency;
    const TArray<float> BackupAdjWeights = RenderVertexAdjacencyWeights;
    const TArray<int32> BackupReverseAdjacency = RenderVertexReverseAdjacency;
    const TArray<uint8> BackupConvergentFlags = ConvergentNeighborFlags;
    const TArray<int32> BackupPendingSeeds = PendingCrustAgeResetSeeds;
    const TBitArray<> BackupPendingMask = PendingCrustAgeResetMask;

    auto MakeSortedTriangleKey = [](int32 A, int32 B, int32 C)
    {
        int32 Values[3] = {A, B, C};
        if (Values[0] > Values[1])
        {
            const int32 Temp = Values[0];
            Values[0] = Values[1];
            Values[1] = Temp;
        }
        if (Values[1] > Values[2])
        {
            const int32 Temp = Values[1];
            Values[1] = Values[2];
            Values[2] = Temp;
        }
        if (Values[0] > Values[1])
        {
            const int32 Temp = Values[0];
            Values[0] = Values[1];
            Values[1] = Temp;
        }
        return FIntVector(Values[0], Values[1], Values[2]);
    };

    TSet<FIntVector> PatchTriangleSet;
    for (int32 TriIdx = 0; TriIdx < Terrane->PatchTriangles.Num(); TriIdx += 3)
    {
        const int32 A = Terrane->PatchTriangles[TriIdx];
        const int32 B = Terrane->PatchTriangles[TriIdx + 1];
        const int32 C = Terrane->PatchTriangles[TriIdx + 2];
        PatchTriangleSet.Add(MakeSortedTriangleKey(A, B, C));
    }

    TArray<int32> FilteredTriangles;
    FilteredTriangles.Reserve(RenderTriangles.Num());
    for (int32 TriIdx = 0; TriIdx < RenderTriangles.Num(); TriIdx += 3)
    {
        const int32 A = RenderTriangles[TriIdx];
        const int32 B = RenderTriangles[TriIdx + 1];
        const int32 C = RenderTriangles[TriIdx + 2];
        if (PatchTriangleSet.Contains(MakeSortedTriangleKey(A, B, C)))
        {
            continue;
        }
        FilteredTriangles.Add(A);
        FilteredTriangles.Add(B);
        FilteredTriangles.Add(C);
    }

    const TSet<int32> PatchVertexSet(Terrane->PatchVertexIndices);

    TArray<int32> LocalToGlobal;
    LocalToGlobal.Reserve(Terrane->VertexPayload.Num());
    for (const FTerraneVertexRecord& Payload : Terrane->VertexPayload)
    {
        FTerraneVertexRecord Record = Payload;
        Record.PlateID = TargetPlateID;
        const int32 NewIndex = AppendRenderVertexFromRecord(Record, TargetPlateID);
        LocalToGlobal.Add(NewIndex);
    }

    TMap<int32, int32> ReplacementToLocal;
    for (int32 LocalIdx = 0; LocalIdx < Terrane->VertexPayload.Num(); ++LocalIdx)
    {
        const int32 ReplacementIdx = Terrane->VertexPayload[LocalIdx].ReplacementVertexIndex;
        if (ReplacementIdx != INDEX_NONE)
        {
            ReplacementToLocal.Add(ReplacementIdx, LocalIdx);
        }
    }

    for (int32& Index : FilteredTriangles)
    {
        if (int32* LocalIdxPtr = ReplacementToLocal.Find(Index))
        {
            const int32 LocalIdx = *LocalIdxPtr;
            if (LocalToGlobal.IsValidIndex(LocalIdx))
            {
                Index = LocalToGlobal[LocalIdx];
            }
        }
    }

    TArray<int32> PatchVerticesSorted = Terrane->PatchVertexIndices;
    PatchVerticesSorted.Sort();
    int32 PatchWriteIndex = 0;
    int32 PatchLastValue = MIN_int32;
    bool bHasPatchLast = false;
    for (int32 Value : PatchVerticesSorted)
    {
        if (!bHasPatchLast || Value != PatchLastValue)
        {
            PatchVerticesSorted[PatchWriteIndex++] = Value;
            PatchLastValue = Value;
            bHasPatchLast = true;
        }
    }
    PatchVerticesSorted.SetNum(PatchWriteIndex);

    TArray<int32> OldToNewIndex;
    CompactRenderVertexData(PatchVerticesSorted, OldToNewIndex);

    auto RemapIndex = [&](int32& Index) -> bool
    {
        if (!OldToNewIndex.IsValidIndex(Index) || OldToNewIndex[Index] == INDEX_NONE)
        {
            return false;
        }
        Index = OldToNewIndex[Index];
        return true;
    };

    for (int32& Index : FilteredTriangles)
    {
        if (!RemapIndex(Index))
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("ReattachTerrane: Failed to remap retained triangle index %d"), Index);

            RenderVertices = BackupRenderVertices;
            RenderTriangles = BackupRenderTriangles;
            VertexPlateAssignments = BackupVertexAssignments;
            CachedVoronoiAssignments = VertexPlateAssignments;
            VertexVelocities = BackupVertexVelocities;
            VertexStressValues = BackupVertexStress;
            VertexTemperatureValues = BackupVertexTemperature;
            VertexElevationValues = BackupVertexElevation;
            VertexErosionRates = BackupVertexErosion;
            VertexSedimentThickness = BackupVertexSediment;
            VertexCrustAge = BackupVertexCrustAge;
            VertexAmplifiedElevation = BackupVertexAmplified;
            VertexRidgeDirections = BackupVertexRidgeDirections;
            RenderVertexAdjacencyOffsets = BackupAdjOffsets;
            RenderVertexAdjacency = BackupAdjacency;
            RenderVertexAdjacencyWeights = BackupAdjWeights;
            RenderVertexReverseAdjacency = BackupReverseAdjacency;
            ConvergentNeighborFlags = BackupConvergentFlags;
            PendingCrustAgeResetSeeds = BackupPendingSeeds;
            PendingCrustAgeResetMask = BackupPendingMask;
            return false;
        }
    }

    for (int32& Index : LocalToGlobal)
    {
        if (!RemapIndex(Index))
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("ReattachTerrane: Failed to remap terrane vertex index %d"), Index);

            RenderVertices = BackupRenderVertices;
            RenderTriangles = BackupRenderTriangles;
            VertexPlateAssignments = BackupVertexAssignments;
            CachedVoronoiAssignments = VertexPlateAssignments;
            VertexVelocities = BackupVertexVelocities;
            VertexStressValues = BackupVertexStress;
            VertexTemperatureValues = BackupVertexTemperature;
            VertexElevationValues = BackupVertexElevation;
            VertexErosionRates = BackupVertexErosion;
            VertexSedimentThickness = BackupVertexSediment;
            VertexCrustAge = BackupVertexCrustAge;
            VertexAmplifiedElevation = BackupVertexAmplified;
            VertexRidgeDirections = BackupVertexRidgeDirections;
            RenderVertexAdjacencyOffsets = BackupAdjOffsets;
            RenderVertexAdjacency = BackupAdjacency;
            RenderVertexAdjacencyWeights = BackupAdjWeights;
            RenderVertexReverseAdjacency = BackupReverseAdjacency;
            ConvergentNeighborFlags = BackupConvergentFlags;
            PendingCrustAgeResetSeeds = BackupPendingSeeds;
            PendingCrustAgeResetMask = BackupPendingMask;
            return false;
        }
    }

    RenderTriangles = MoveTemp(FilteredTriangles);

    for (int32 TriIdx = 0; TriIdx < Terrane->ExtractedTriangles.Num(); TriIdx += 3)
    {
        const int32 LocalA = Terrane->ExtractedTriangles[TriIdx];
        const int32 LocalB = Terrane->ExtractedTriangles[TriIdx + 1];
        const int32 LocalC = Terrane->ExtractedTriangles[TriIdx + 2];

        if (!LocalToGlobal.IsValidIndex(LocalA) || !LocalToGlobal.IsValidIndex(LocalB) || !LocalToGlobal.IsValidIndex(LocalC))
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("ReattachTerrane: Invalid local triangle indices (%d, %d, %d)"), LocalA, LocalB, LocalC);

            RenderVertices = BackupRenderVertices;
            RenderTriangles = BackupRenderTriangles;
            VertexPlateAssignments = BackupVertexAssignments;
            CachedVoronoiAssignments = VertexPlateAssignments;
            VertexVelocities = BackupVertexVelocities;
            VertexStressValues = BackupVertexStress;
            VertexTemperatureValues = BackupVertexTemperature;
            VertexElevationValues = BackupVertexElevation;
            VertexErosionRates = BackupVertexErosion;
            VertexSedimentThickness = BackupVertexSediment;
            VertexCrustAge = BackupVertexCrustAge;
            VertexAmplifiedElevation = BackupVertexAmplified;
            VertexRidgeDirections = BackupVertexRidgeDirections;
            RenderVertexAdjacencyOffsets = BackupAdjOffsets;
            RenderVertexAdjacency = BackupAdjacency;
            RenderVertexAdjacencyWeights = BackupAdjWeights;
            RenderVertexReverseAdjacency = BackupReverseAdjacency;
            ConvergentNeighborFlags = BackupConvergentFlags;
            PendingCrustAgeResetSeeds = BackupPendingSeeds;
            PendingCrustAgeResetMask = BackupPendingMask;
            return false;
        }

        const int32 GlobalA = LocalToGlobal[LocalA];
        const int32 GlobalB = LocalToGlobal[LocalB];
        const int32 GlobalC = LocalToGlobal[LocalC];

        RenderTriangles.Add(GlobalA);
        RenderTriangles.Add(GlobalB);
        RenderTriangles.Add(GlobalC);
    }

    InvalidateRenderVertexCaches();

    SurfaceDataVersion++;
    TopologyVersion++;
    InvalidateRidgeDirectionCache();
    MarkAllRidgeDirectionsDirty();
    BumpOceanicAmplificationSerial();

    if (!ValidateTopology(ValidationError))
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("ReattachTerrane: Post-reattachment topology invalid: %s"), *ValidationError);

        RenderVertices = BackupRenderVertices;
        RenderTriangles = BackupRenderTriangles;
        VertexPlateAssignments = BackupVertexAssignments;
        CachedVoronoiAssignments = VertexPlateAssignments;
        VertexVelocities = BackupVertexVelocities;
        VertexStressValues = BackupVertexStress;
        VertexTemperatureValues = BackupVertexTemperature;
        VertexElevationValues = BackupVertexElevation;
        VertexErosionRates = BackupVertexErosion;
        VertexSedimentThickness = BackupVertexSediment;
        VertexCrustAge = BackupVertexCrustAge;
        VertexAmplifiedElevation = BackupVertexAmplified;
        VertexRidgeDirections = BackupVertexRidgeDirections;
        RenderVertexAdjacencyOffsets = BackupAdjOffsets;
        RenderVertexAdjacency = BackupAdjacency;
        RenderVertexAdjacencyWeights = BackupAdjWeights;
        RenderVertexReverseAdjacency = BackupReverseAdjacency;
        ConvergentNeighborFlags = BackupConvergentFlags;
        PendingCrustAgeResetSeeds = BackupPendingSeeds;
        PendingCrustAgeResetMask = BackupPendingMask;
        SurfaceDataVersion--;
        TopologyVersion--;
        return false;
    }

    BuildRenderVertexAdjacency();
    BuildRenderVertexReverseAdjacency();
    UpdateConvergentNeighborFlags();

    Terrane->State = ETerraneState::Attached;
    Terrane->TargetPlateID = TargetPlateID;
    Terrane->CarrierPlateID = INDEX_NONE;
    Terrane->ReattachmentTimeMy = CurrentTimeMy;

    Terranes.RemoveAt(TerraneIndex);

    UE_LOG(LogPlanetaryCreation, Log, TEXT("ReattachTerrane: Successfully reattached terrane %d to plate %d (%.2f My transport duration)"),
        TerraneID, TargetPlateID, CurrentTimeMy - Terrane->ExtractionTimeMy);
    return true;
}

bool UTectonicSimulationService::AssignTerraneCarrier(int32 TerraneID)
{
    // Find terrane
    FContinentalTerrane* Terrane = nullptr;
    for (FContinentalTerrane& T : Terranes)
    {
        if (T.TerraneID == TerraneID)
        {
            Terrane = &T;
            break;
        }
    }

    if (!Terrane)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("AssignTerraneCarrier: Terrane %d not found"), TerraneID);
        return false;
    }

    // Validation: Terrane must be in Extracted state
    if (Terrane->State != ETerraneState::Extracted)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("AssignTerraneCarrier: Terrane %d not in Extracted state (current: %d)"),
            TerraneID, static_cast<int32>(Terrane->State));
        return false;
    }

    // Find nearest oceanic plate to terrane centroid
    double MinDistance = TNumericLimits<double>::Max();
    int32 NearestOceanicPlateID = INDEX_NONE;

    for (const FTectonicPlate& Plate : Plates)
    {
        if (Plate.CrustType == ECrustType::Oceanic)
        {
            // Geodesic distance between terrane centroid and plate centroid
            const double Distance = FMath::Acos(FMath::Clamp(Terrane->Centroid | Plate.Centroid, -1.0, 1.0));

            if (Distance < MinDistance)
            {
                MinDistance = Distance;
                NearestOceanicPlateID = Plate.PlateID;
            }
        }
    }

    if (NearestOceanicPlateID == INDEX_NONE)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("AssignTerraneCarrier: No oceanic plates found, terrane %d remains in Extracted state"), TerraneID);
        return false;
    }

    // Assign carrier and transition to Transporting state
    Terrane->CarrierPlateID = NearestOceanicPlateID;
    Terrane->State = ETerraneState::Transporting;

    const double DistanceKm = MinDistance * (Parameters.PlanetRadius / 1000.0);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("AssignTerraneCarrier: Terrane %d assigned to oceanic carrier %d (distance: %.1f km)"),
        TerraneID, NearestOceanicPlateID, DistanceKm);

    return true;
}

void UTectonicSimulationService::UpdateTerranePositions(double DeltaTimeMy)
{
    for (FContinentalTerrane& Terrane : Terranes)
    {
        // Only update terranes that are being transported by carrier plates
        if (Terrane.State != ETerraneState::Transporting)
        {
            continue;
        }

        // Find carrier plate
        const FTectonicPlate* CarrierPlate = nullptr;
        for (const FTectonicPlate& Plate : Plates)
        {
            if (Plate.PlateID == Terrane.CarrierPlateID)
            {
                CarrierPlate = &Plate;
                break;
            }
        }

        if (!CarrierPlate)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("UpdateTerranePositions: Carrier plate %d not found for terrane %d"),
                Terrane.CarrierPlateID, Terrane.TerraneID);
            continue;
        }

        // Rotate terrane vertices around carrier plate's Euler pole
        const FVector3d& RotationAxis = CarrierPlate->EulerPoleAxis;
        const double RotationAngle = CarrierPlate->AngularVelocity * DeltaTimeMy;

        // Rodrigues' rotation formula: v' = v*cos(θ) + (k×v)*sin(θ) + k*(k·v)*(1-cos(θ))
        const double CosTheta = FMath::Cos(RotationAngle);
        const double SinTheta = FMath::Sin(RotationAngle);
        const double OneMinusCosTheta = 1.0 - CosTheta;

        for (FTerraneVertexRecord& VertexRecord : Terrane.VertexPayload)
        {
            FVector3d& V = VertexRecord.Position;
            const FVector3d KCrossV = RotationAxis ^ V;
            const double KDotV = RotationAxis | V;

            V = V * CosTheta + KCrossV * SinTheta + RotationAxis * KDotV * OneMinusCosTheta;
            V.Normalize(); // Maintain unit sphere
        }

        // Update terrane centroid
        FVector3d NewCentroid = FVector3d::ZeroVector;
        for (const FTerraneVertexRecord& VertexRecord : Terrane.VertexPayload)
        {
            NewCentroid += VertexRecord.Position;
        }
        if (Terrane.VertexPayload.Num() > 0)
        {
            NewCentroid /= static_cast<double>(Terrane.VertexPayload.Num());
            Terrane.Centroid = NewCentroid.GetSafeNormal();
        }
    }

    BumpOceanicAmplificationSerial();
}

void UTectonicSimulationService::DetectTerraneCollisions()
{
    const double CollisionThreshold_km = 500.0; // Paper Section 6: proximity threshold for collision detection
    const double CollisionThreshold_rad = CollisionThreshold_km / (Parameters.PlanetRadius / 1000.0);

    for (FContinentalTerrane& Terrane : Terranes)
    {
        // Only check terranes that are currently being transported
        if (Terrane.State != ETerraneState::Transporting)
        {
            continue;
        }

        // Check distance to all continental plates
        for (const FTectonicPlate& Plate : Plates)
        {
            if (Plate.CrustType != ECrustType::Continental)
            {
                continue; // Skip oceanic plates
            }

            // Skip source plate (terrane came from here)
            if (Plate.PlateID == Terrane.SourcePlateID)
            {
                continue;
            }

            // Check if terrane is approaching this continental plate
            // Find closest boundary point between carrier and target plate
            const TPair<int32, int32> BoundaryKey = Terrane.CarrierPlateID < Plate.PlateID
                ? MakeTuple(Terrane.CarrierPlateID, Plate.PlateID)
                : MakeTuple(Plate.PlateID, Terrane.CarrierPlateID);

            const FPlateBoundary* Boundary = Boundaries.Find(BoundaryKey);
            if (!Boundary)
            {
                continue; // No boundary between carrier and this continental plate
            }

            // Check if boundary is convergent (collision zone)
            if (Boundary->BoundaryType != EBoundaryType::Convergent)
            {
                continue; // Only convergent boundaries trigger collisions
            }

            // Compute distance from terrane centroid to boundary
            double MinDistanceToBoundary = TNumericLimits<double>::Max();
            for (int32 SharedVertexIdx : Boundary->SharedEdgeVertices)
            {
                if (SharedVertexIdx < 0 || SharedVertexIdx >= SharedVertices.Num())
                {
                    continue;
                }

                const FVector3d& BoundaryPoint = SharedVertices[SharedVertexIdx];
                const double Distance = FMath::Acos(FMath::Clamp(Terrane.Centroid | BoundaryPoint, -1.0, 1.0));

                if (Distance < MinDistanceToBoundary)
                {
                    MinDistanceToBoundary = Distance;
                }
            }

            // Check if terrane is within collision threshold
            if (MinDistanceToBoundary < CollisionThreshold_rad)
            {
                // Mark terrane as colliding and set target plate
                Terrane.State = ETerraneState::Colliding;
                Terrane.TargetPlateID = Plate.PlateID;

                const double DistanceKm = MinDistanceToBoundary * (Parameters.PlanetRadius / 1000.0);
                UE_LOG(LogPlanetaryCreation, Log, TEXT("DetectTerraneCollisions: Terrane %d approaching plate %d (distance: %.1f km, threshold: %.1f km)"),
                    Terrane.TerraneID, Plate.PlateID, DistanceKm, CollisionThreshold_km);

                break; // Stop checking other plates for this terrane
            }
        }
    }
}

void UTectonicSimulationService::ProcessTerraneReattachments()
{
    // Milestone 6 Task 1.3: Automatically reattach colliding terranes
    // Note: Iterate backwards since ReattachTerrane removes terranes from array
    for (int32 i = Terranes.Num() - 1; i >= 0; --i)
    {
        FContinentalTerrane& Terrane = Terranes[i];

        if (Terrane.State == ETerraneState::Colliding)
        {
            if (Terrane.TargetPlateID == INDEX_NONE)
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("ProcessTerraneReattachments: Terrane %d in Colliding state but no target plate assigned, skipping"),
                    Terrane.TerraneID);
                continue;
            }

            const double TransportDuration = CurrentTimeMy - Terrane.ExtractionTimeMy;
            UE_LOG(LogPlanetaryCreation, Log, TEXT("ProcessTerraneReattachments: Auto-reattaching terrane %d to plate %d after %.2f My transport"),
                Terrane.TerraneID, Terrane.TargetPlateID, TransportDuration);

            // Attempt reattachment (will validate topology and rollback on failure)
            const bool bSuccess = ReattachTerrane(Terrane.TerraneID, Terrane.TargetPlateID);

            if (!bSuccess)
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("ProcessTerraneReattachments: Failed to reattach terrane %d, will retry next step"),
                    Terrane.TerraneID);
                // Keep terrane in Colliding state for retry next step
            }
            // Note: If successful, terrane was removed from Terranes array by ReattachTerrane
        }
    }
}

// ============================================================================
// Milestone 6 Task 2.1: Oceanic Amplification (Stage B)
// ============================================================================

// Forward declarations from OceanicAmplification.cpp
double ComputeGaborNoiseApproximation(const FVector3d& Position, const FVector3d& FaultDirection, double Frequency);
double ComputeOceanicAmplification(const FVector3d& Position, int32 PlateID, double CrustAge_My, double BaseElevation_m,
    const FVector3d& RidgeDirection, const TArray<FTectonicPlate>& Plates,
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries,
    const FTectonicSimulationParameters& Parameters);

// Forward declarations from ContinentalAmplification.cpp
struct FExemplarMetadata;
bool LoadExemplarLibraryJSON(const FString& ProjectContentDir);
bool IsExemplarLibraryLoaded();
FExemplarMetadata* AccessExemplarMetadata(int32 Index);
const FExemplarMetadata* AccessExemplarMetadataConst(int32 Index);
bool LoadExemplarHeightData(FExemplarMetadata& Exemplar, const FString& ProjectContentDir);
double SampleExemplarHeight(const FExemplarMetadata& Exemplar, double U, double V);
TArray<FExemplarMetadata*> GetExemplarsForTerrainType(EContinentalTerrainType TerrainType);
double BlendContinentalExemplars(const FVector3d& Position, int32 PlateID, double BaseElevation_m,
    const TArray<FExemplarMetadata*>& MatchingExemplars, const TArray<FTectonicPlate>& Plates,
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries, const FPlateBoundarySummary* BoundarySummary,
    const FString& ProjectContentDir, int32 Seed);
double ComputeContinentalAmplification(const FVector3d& Position, int32 PlateID, double BaseElevation_m,
    const TArray<FTectonicPlate>& Plates, const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries,
    const FPlateBoundarySummary* BoundarySummary, double OrogenyAge_My, EBoundaryType NearestBoundaryType,
    const FString& ProjectContentDir, int32 Seed);
FVector2d ComputeContinentalRandomOffset(const FVector3d& Position, int32 Seed);
#if UE_BUILD_DEVELOPMENT
FContinentalAmplificationDebugInfo* GetContinentalAmplificationDebugInfoPtr();
#endif

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

bool UTectonicSimulationService::RefreshRidgeDirectionsIfNeeded()
{
    const int32 VertexCount = RenderVertices.Num();
    const bool bTopologyChanged =
        (CachedRidgeDirectionTopologyVersion != TopologyVersion) ||
        (CachedRidgeDirectionVertexCount != VertexCount);

    if (!bTopologyChanged && RidgeDirectionDirtyCount == 0)
    {
        return false;
    }

    ComputeRidgeDirections();
    return true;
}


void UTectonicSimulationService::ComputeRidgeDirections()
{
    // Paper Section 5: "using the recorded parameters r_c, i.e. the local direction parallel to the ridge"
    // For each vertex, find nearest divergent boundary and compute tangent direction

    const int32 VertexCount = RenderVertices.Num();

    if (VertexCount <= 0)
    {
        VertexRidgeDirections.Reset();
        RidgeDirectionDirtyMask.Reset();
        RidgeDirectionDirtyCount = 0;
        RidgeDirectionFloatSoA.DirX.Reset();
        RidgeDirectionFloatSoA.DirY.Reset();
        RidgeDirectionFloatSoA.DirZ.Reset();
        RidgeDirectionFloatSoA.CachedTopologyVersion = TopologyVersion;
        RidgeDirectionFloatSoA.CachedVertexCount = 0;
        CachedRidgeDirectionTopologyVersion = TopologyVersion;
        CachedRidgeDirectionVertexCount = 0;
        LastRidgeDirectionUpdateCount = 0;
        return;
    }

    EnsureRidgeDirtyMaskSize(VertexCount);

    if (VertexRidgeDirections.Num() != VertexCount)
    {
        VertexRidgeDirections.SetNum(VertexCount);
        for (int32 Index = 0; Index < VertexCount; ++Index)
        {
            VertexRidgeDirections[Index] = FVector3d::ZAxisVector;
        }
    }

    FRidgeDirectionFloatSoA& RidgeSoA = RidgeDirectionFloatSoA;
    if (RidgeSoA.DirX.Num() != VertexCount)
    {
        RidgeSoA.DirX.SetNum(VertexCount);
        RidgeSoA.DirY.SetNum(VertexCount);
        RidgeSoA.DirZ.SetNum(VertexCount);
    }

    if (RidgeDirectionDirtyCount == 0)
    {
        RidgeSoA.CachedTopologyVersion = TopologyVersion;
        RidgeSoA.CachedVertexCount = VertexCount;
        CachedRidgeDirectionTopologyVersion = TopologyVersion;
        CachedRidgeDirectionVertexCount = VertexCount;
        LastRidgeDirectionUpdateCount = 0;
        return;
    }

    TArray<int32> DirtyVertices;
    DirtyVertices.Reserve(RidgeDirectionDirtyCount);
    for (TConstSetBitIterator<> It(RidgeDirectionDirtyMask); It; ++It)
    {
        DirtyVertices.Add(It.GetIndex());
    }

    if (DirtyVertices.Num() == 0)
    {
        RidgeDirectionDirtyMask.Reset();
        RidgeDirectionDirtyCount = 0;
        LastRidgeDirectionUpdateCount = 0;
        return;
    }

#if UE_BUILD_DEVELOPMENT
    UE_LOG(LogPlanetaryCreation, VeryVerbose,
        TEXT("[RidgeCompute] DirtyMask.Num=%d DirtyCount=%d DirtyVertices.Num=%d"),
        RidgeDirectionDirtyMask.Num(), RidgeDirectionDirtyCount, DirtyVertices.Num());
#endif

    if (RenderVertexBoundaryCache.Num() != VertexCount)
    {
        BuildRenderVertexBoundaryCache();
    }

    int32 UpdatedVertices = 0;
    int32 RidgeCacheHitsLocal = 0;
    int32 RidgeMissingTangentLocal = 0;
    int32 RidgePoorAlignmentLocal = 0;
    int32 RidgeGradientFallbackLocal = 0;
#if UE_BUILD_DEVELOPMENT
    int32 RidgeDiagLogged = 0;
#endif

    const TArray<FVector3d>& SharedVerts = SharedVertices;

    auto ComputeNearestBoundaryTangent = [&](const FVector3d& VertexNormal, int32 PlateID, double& OutDistance) -> FVector3d
    {
        OutDistance = TNumericLimits<double>::Max();

        auto FetchBoundaryVertex = [&](int32 SharedIndex, FVector3d& OutUnit) -> bool
        {
            if (SharedVerts.IsValidIndex(SharedIndex))
            {
                OutUnit = SharedVerts[SharedIndex].GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
                return true;
            }

            if (RenderVertices.IsValidIndex(SharedIndex))
            {
                OutUnit = RenderVertices[SharedIndex].GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
                return true;
            }

            return false;
        };

        auto AccumulateBoundary = [&](const FPlateBoundary& Boundary, double& InOutClosestDistance, FVector3d& InOutWeightedTangent, double& InOutWeightSum)
        {
            if (Boundary.BoundaryType != EBoundaryType::Divergent)
            {
                return;
            }

            const TArray<int32>& SharedEdge = Boundary.SharedEdgeVertices;
            const int32 EdgeCount = SharedEdge.Num();
            if (EdgeCount < 2)
            {
                return;
            }

            FVector3d BoundaryWeighted = FVector3d::ZeroVector;
            double BoundaryWeightSum = 0.0;
            double BoundaryClosest = InOutClosestDistance;

            for (int32 EdgeIdx = 0; EdgeIdx < EdgeCount; ++EdgeIdx)
            {
                const int32 V0Idx = SharedEdge[EdgeIdx];
                const int32 V1Idx = SharedEdge[(EdgeIdx + 1) % EdgeCount];

                FVector3d P0, P1;
                if (!FetchBoundaryVertex(V0Idx, P0) || !FetchBoundaryVertex(V1Idx, P1))
                {
                    continue;
                }

                if ((P1 - P0).IsNearlyZero())
                {
                    continue;
                }

                FVector3d EdgeVector = P1 - P0;
                EdgeVector -= (EdgeVector | VertexNormal) * VertexNormal;
                if (!EdgeVector.Normalize())
                {
                    continue;
                }

                FVector3d SegmentMid = (P0 + P1).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, VertexNormal);
                if (SegmentMid.IsNearlyZero())
                {
                    SegmentMid = P0;
                }

                double AngularDistance = FMath::Acos(FMath::Clamp(VertexNormal | SegmentMid, -1.0, 1.0));
                if (!FMath::IsFinite(AngularDistance))
                {
                    AngularDistance = PI;
                }

                BoundaryClosest = FMath::Min(BoundaryClosest, AngularDistance);

                const double Weight = 1.0 / FMath::Max(AngularDistance, 1e-3);
                BoundaryWeighted += EdgeVector * Weight;
                BoundaryWeightSum += Weight;
            }

            if (BoundaryWeightSum > 0.0)
            {
                const FVector3d BoundaryTangent = (BoundaryWeighted / BoundaryWeightSum).GetSafeNormal();
                if (!BoundaryTangent.IsNearlyZero())
                {
                    InOutClosestDistance = FMath::Min(InOutClosestDistance, BoundaryClosest);
                    InOutWeightedTangent += BoundaryTangent * BoundaryWeightSum;
                    InOutWeightSum += BoundaryWeightSum;
                }
            }
        };

        FVector3d WeightedTangent = FVector3d::ZeroVector;
        double TotalWeight = 0.0;
        double ClosestDistance = TNumericLimits<double>::Max();
        bool bFoundBoundary = false;

        const FPlateBoundarySummary* Summary = GetPlateBoundarySummary(PlateID);
        if (Summary)
        {
            for (const FPlateBoundarySummaryEntry& Entry : Summary->Boundaries)
            {
                if (Entry.BoundaryType != EBoundaryType::Divergent)
                {
                    continue;
                }

                const int32 OtherPlateID = Entry.OtherPlateID;
                const TPair<int32, int32> BoundaryKey = (PlateID < OtherPlateID)
                    ? TPair<int32, int32>(PlateID, OtherPlateID)
                    : TPair<int32, int32>(OtherPlateID, PlateID);

                if (const FPlateBoundary* Boundary = Boundaries.Find(BoundaryKey))
                {
                    bFoundBoundary = true;
                    AccumulateBoundary(*Boundary, ClosestDistance, WeightedTangent, TotalWeight);
                }
            }
        }
        else
        {
            TArray<TPair<int32, int32>> SortedKeys;
            SortedKeys.Reserve(Boundaries.Num());
            for (const auto& BoundaryPair : Boundaries)
            {
                const TPair<int32, int32>& BoundaryKey = BoundaryPair.Key;
                if (BoundaryKey.Key != PlateID && BoundaryKey.Value != PlateID)
                {
                    continue;
                }
                SortedKeys.Add(BoundaryKey);
            }

            SortedKeys.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
            {
                if (A.Key != B.Key)
                {
                    return A.Key < B.Key;
                }
                return A.Value < B.Value;
            });

            for (const TPair<int32, int32>& BoundaryKey : SortedKeys)
            {
                const FPlateBoundary* BoundaryPtr = Boundaries.Find(BoundaryKey);
                if (!BoundaryPtr)
                {
                    continue;
                }

                bFoundBoundary = true;
                AccumulateBoundary(*BoundaryPtr, ClosestDistance, WeightedTangent, TotalWeight);
            }
        }

        if (TotalWeight > 0.0 && !WeightedTangent.IsNearlyZero())
        {
            OutDistance = ClosestDistance;
            return WeightedTangent.GetSafeNormal();
        }

        OutDistance = ClosestDistance;

#if UE_BUILD_DEVELOPMENT
        if (bFoundBoundary)
        {
            UE_LOG(LogPlanetaryCreation, VeryVerbose,
                TEXT("[RidgeDiag] Plate %d boundary tangent unavailable after summary pass (weight=%.6f)"),
                PlateID,
                TotalWeight);
        }
#endif

        return FVector3d::ZeroVector;
    };

    for (int32 VertexIdx : DirtyVertices)
    {
        if (!RenderVertices.IsValidIndex(VertexIdx))
        {
            continue;
        }

        const FVector3d& VertexPosition = RenderVertices[VertexIdx];
        const int32 PlateID = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;

        FVector3d ResultDirection = FVector3d::ZAxisVector;

        if (PlateID == INDEX_NONE || !Plates.IsValidIndex(PlateID))
        {
            VertexRidgeDirections[VertexIdx] = ResultDirection;
            const FVector3d SafeDir = ResultDirection;
            RidgeSoA.DirX[VertexIdx] = static_cast<float>(SafeDir.X);
            RidgeSoA.DirY[VertexIdx] = static_cast<float>(SafeDir.Y);
            RidgeSoA.DirZ[VertexIdx] = static_cast<float>(SafeDir.Z);
            ++UpdatedVertices;
            continue;
        }

        const FTectonicPlate& Plate = Plates[PlateID];
        if (Plate.CrustType != ECrustType::Oceanic)
        {
            VertexRidgeDirections[VertexIdx] = ResultDirection;
            const FVector3d SafeDir = ResultDirection;
            RidgeSoA.DirX[VertexIdx] = static_cast<float>(SafeDir.X);
            RidgeSoA.DirY[VertexIdx] = static_cast<float>(SafeDir.Y);
            RidgeSoA.DirZ[VertexIdx] = static_cast<float>(SafeDir.Z);
            ++UpdatedVertices;
            continue;
        }

        const FVector3d VertexNormal = VertexPosition.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);

        bool bUsedBoundaryCache = false;
        bool bUsedGradient = false;

        double BoundaryDistance = TNumericLimits<double>::Max();
        FVector3d SelectedBoundaryTangent = FVector3d::ZeroVector;

        if (RenderVertexBoundaryCache.IsValidIndex(VertexIdx))
        {
            const FRenderVertexBoundaryInfo& CacheInfo = RenderVertexBoundaryCache[VertexIdx];
            const bool bCacheMatchesPlate =
                CacheInfo.bHasBoundary &&
                CacheInfo.bIsDivergent &&
                CacheInfo.SourcePlateID == Plate.PlateID &&
                !CacheInfo.BoundaryTangent.IsNearlyZero();

            if (bCacheMatchesPlate)
            {
                SelectedBoundaryTangent = CacheInfo.BoundaryTangent;
                if (CacheInfo.DistanceRadians < TNumericLimits<float>::Max())
                {
                    BoundaryDistance = static_cast<double>(CacheInfo.DistanceRadians);
                }
                bUsedBoundaryCache = true;
                ++RidgeCacheHitsLocal;
            }
        }

        if (!bUsedBoundaryCache)
        {
            SelectedBoundaryTangent = ComputeNearestBoundaryTangent(VertexNormal, Plate.PlateID, BoundaryDistance);
            if (!SelectedBoundaryTangent.IsNearlyZero())
            {
                bUsedBoundaryCache = true;
            }
        }

        const bool bBoundaryWithinInfluence = bUsedBoundaryCache && !SelectedBoundaryTangent.IsNearlyZero();

        if (bBoundaryWithinInfluence)
        {
            ResultDirection = SelectedBoundaryTangent.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
        }

        FVector3d AgeGradient = FVector3d::ZeroVector;
        double GradientLength = 0.0;

        if (!bUsedBoundaryCache)
        {
            if (VertexCrustAge.IsValidIndex(VertexIdx) && RenderVertexAdjacencyOffsets.IsValidIndex(VertexIdx + 1))
            {
                const int32 StartAdj = RenderVertexAdjacencyOffsets[VertexIdx];
                const int32 EndAdj = RenderVertexAdjacencyOffsets[VertexIdx + 1];
                for (int32 Offset = StartAdj; Offset < EndAdj; ++Offset)
                {
                    const int32 NeighborIdx = RenderVertexAdjacency.IsValidIndex(Offset) ? RenderVertexAdjacency[Offset] : INDEX_NONE;
                    if (!RenderVertices.IsValidIndex(NeighborIdx))
                    {
                        continue;
                    }

                    if (!VertexCrustAge.IsValidIndex(NeighborIdx))
                    {
                        continue;
                    }

                    const int32 NeighborPlateID = VertexPlateAssignments.IsValidIndex(NeighborIdx) ? VertexPlateAssignments[NeighborIdx] : INDEX_NONE;
                    if (NeighborPlateID != Plate.PlateID)
                    {
                        continue;
                    }

                    FVector3d Step = RenderVertices[NeighborIdx] - VertexPosition;
                    Step -= (Step | VertexNormal) * VertexNormal;
                    if (Step.IsNearlyZero())
                    {
                        continue;
                    }

                    const double AgeDiff = VertexCrustAge[NeighborIdx] - VertexCrustAge[VertexIdx];
                    AgeGradient += AgeDiff * Step;
                }
            }

            GradientLength = AgeGradient.Length();
            if (GradientLength > UE_DOUBLE_SMALL_NUMBER)
            {
                const FVector3d GradientDir = (AgeGradient / GradientLength).GetSafeNormal();
                FVector3d Candidate = FVector3d::CrossProduct(VertexNormal, GradientDir).GetSafeNormal();
                if (!Candidate.IsNearlyZero())
                {
                    ResultDirection = Candidate;
                    bUsedGradient = true;
                }
            }
        }

        if (!bUsedBoundaryCache && !bUsedGradient)
        {
            ResultDirection = FVector3d::CrossProduct(VertexNormal, FVector3d::UpVector).GetSafeNormal();
            if (ResultDirection.IsNearlyZero())
            {
                ResultDirection = FVector3d::ZAxisVector;
            }
        }

        VertexRidgeDirections[VertexIdx] = ResultDirection;

        const FVector3d SafeDir = ResultDirection.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
        RidgeSoA.DirX[VertexIdx] = static_cast<float>(SafeDir.X);
        RidgeSoA.DirY[VertexIdx] = static_cast<float>(SafeDir.Y);
        RidgeSoA.DirZ[VertexIdx] = static_cast<float>(SafeDir.Z);

        ++UpdatedVertices;

        const double DirLength = ResultDirection.Length();
        if (VertexCrustAge.IsValidIndex(VertexIdx) && VertexCrustAge[VertexIdx] < 15.0)
        {
            if (bBoundaryWithinInfluence && SelectedBoundaryTangent.IsNearlyZero())
            {
                ++RidgeMissingTangentLocal;
#if UE_BUILD_DEVELOPMENT
                if (RidgeDiagLogged < 50)
                {
                    UE_LOG(LogPlanetaryCreation, Warning,
                        TEXT("[RidgeDiag] Vertex %d Plate=%d Age=%.2f My missing cache tangent (dist=%.3f rad)"),
                        VertexIdx,
                        Plate.PlateID,
                        VertexCrustAge[VertexIdx],
                        BoundaryDistance);
                    ++RidgeDiagLogged;
                }
#endif
            }
            else if (bUsedBoundaryCache)
            {
                const double Alignment = FMath::Abs(ResultDirection | SelectedBoundaryTangent.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector));
                if (DirLength < 0.95 || Alignment < 0.95)
                {
                    ++RidgePoorAlignmentLocal;
#if UE_BUILD_DEVELOPMENT
                    if (RidgeDiagLogged < 50)
                    {
                        UE_LOG(LogPlanetaryCreation, Warning,
                            TEXT("[RidgeDiag] Vertex %d Plate=%d Age=%.2f My |Dir|=%.3f Alignment=%.1f%% (dist=%.3f rad)"),
                            VertexIdx,
                            Plate.PlateID,
                            VertexCrustAge[VertexIdx],
                            DirLength,
                            Alignment * 100.0,
                            BoundaryDistance);
                        UE_LOG(LogPlanetaryCreation, Warning,
                            TEXT("    ResultDir=(%.3f, %.3f, %.3f) CacheTan=(%.3f, %.3f, %.3f)"),
                            ResultDirection.X, ResultDirection.Y, ResultDirection.Z,
                            SelectedBoundaryTangent.X, SelectedBoundaryTangent.Y, SelectedBoundaryTangent.Z);
                        ++RidgeDiagLogged;
                    }
#endif
                }
            }
            else if (bBoundaryWithinInfluence && bUsedGradient)
            {
                ++RidgeGradientFallbackLocal;
#if UE_BUILD_DEVELOPMENT
                if (RidgeDiagLogged < 50)
                {
                    UE_LOG(LogPlanetaryCreation, Warning,
                        TEXT("[RidgeDiag] Vertex %d Plate=%d Age=%.2f My fallback to gradient (|Grad|=%.3f, dist=%.3f rad)"),
                        VertexIdx,
                        Plate.PlateID,
                        VertexCrustAge[VertexIdx],
                        GradientLength,
                        BoundaryDistance);
                    ++RidgeDiagLogged;
                }
#endif
            }
        }
    }

    for (int32 VertexIdx : DirtyVertices)
    {
        if (RidgeDirectionDirtyMask.IsValidIndex(VertexIdx))
        {
            RidgeDirectionDirtyMask[VertexIdx] = false;
        }
    }

    RidgeDirectionDirtyCount = 0;

    CachedRidgeDirectionTopologyVersion = TopologyVersion;
    CachedRidgeDirectionVertexCount = VertexCount;
    RidgeSoA.CachedTopologyVersion = TopologyVersion;
    RidgeSoA.CachedVertexCount = VertexCount;
    LastRidgeDirectionUpdateCount = UpdatedVertices;
    LastRidgeDirtyVertexCount = DirtyVertices.Num();
    LastRidgeCacheHitCount = RidgeCacheHitsLocal;
    LastRidgeMissingTangentCount = RidgeMissingTangentLocal;
    LastRidgePoorAlignmentCount = RidgePoorAlignmentLocal;
    LastRidgeGradientFallbackCount = RidgeGradientFallbackLocal;

    if (UpdatedVertices > 0)
    {
        UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[StageB][RidgeCache] Updated %d ridge directions (ring depth %d, vertices=%d)"),
            UpdatedVertices,
            Parameters.RidgeDirectionDirtyRingDepth,
            VertexCount);
        BumpOceanicAmplificationSerial();
    }

#if UE_BUILD_DEVELOPMENT
    if (RidgeCacheHitsLocal > 0 || RidgeMissingTangentLocal > 0 || RidgePoorAlignmentLocal > 0 || RidgeGradientFallbackLocal > 0)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("[RidgeDiag] Summary: CacheHits=%d Missing=%d PoorAlignment=%d GradientFallback=%d"),
            RidgeCacheHitsLocal,
            RidgeMissingTangentLocal,
            RidgePoorAlignmentLocal,
            RidgeGradientFallbackLocal);
    }
#endif
}

void UTectonicSimulationService::SetHighlightSeaLevel(bool bEnabled)
{
    if (bHighlightSeaLevel == bEnabled)
    {
        return;
    }

    bHighlightSeaLevel = bEnabled;

    // Update UI overlays without forcing a full simulation reset.
    SurfaceDataVersion++;
}

void UTectonicSimulationService::InitializeAmplifiedElevationBaseline()
{
    const int32 VertexCount = RenderVertices.Num();
    if (VertexCount == 0)
    {
        return;
    }

    if (VertexAmplifiedElevation.Num() != VertexCount)
    {
        VertexAmplifiedElevation.SetNumZeroed(VertexCount);
    }

    if (VertexElevationValues.Num() != VertexCount)
    {
        VertexElevationValues.SetNumZeroed(VertexCount);
    }

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        VertexAmplifiedElevation[VertexIdx] = VertexElevationValues[VertexIdx];
    }

    BumpOceanicAmplificationSerial();
}

void UTectonicSimulationService::RebuildStageBForCurrentLOD()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(RebuildStageBForCurrentLOD);

    if (RenderVertices.Num() == 0)
    {
        InitializeAmplifiedElevationBaseline();
        return;
    }

    InitializeAmplifiedElevationBaseline();

    const bool bMeetsAmplificationLOD = Parameters.RenderSubdivisionLevel >= Parameters.MinAmplificationLOD;
    const bool bShouldRunOceanic = bMeetsAmplificationLOD && Parameters.bEnableOceanicAmplification;
    const bool bShouldRunContinental = bMeetsAmplificationLOD && Parameters.bEnableContinentalAmplification;

    const bool bUseGPU = ShouldUseGPUAmplification() && Parameters.bSkipCPUAmplification;

    if (bShouldRunOceanic)
    {
        MarkAllRidgeDirectionsDirty();
        RefreshRidgeDirectionsIfNeeded();

        if (!Parameters.bSkipCPUAmplification)
        {
            ApplyOceanicAmplification();
        }
        else if (bUseGPU)
        {
#if WITH_EDITOR
            InitializeGPUExemplarResources();
            if (ApplyOceanicAmplificationGPU())
            {
                ProcessPendingOceanicGPUReadbacks(true, nullptr);
            }
#endif
        }
    }

    if (bShouldRunContinental)
    {
        RefreshContinentalAmplificationCache();

        if (!Parameters.bSkipCPUAmplification)
        {
            ApplyContinentalAmplification();
        }
        else if (bUseGPU)
        {
#if WITH_EDITOR
            InitializeGPUExemplarResources();
            if (ApplyContinentalAmplificationGPU())
            {
                ProcessPendingContinentalGPUReadbacks(true, nullptr);
            }
#endif
        }
    }
}

void UTectonicSimulationService::ApplyOceanicAmplification()
{
    // Paper Section 5: Apply procedural amplification to oceanic crust
    // Transform faults + high-frequency detail

    const int32 VertexCount = RenderVertices.Num();
    checkf(VertexAmplifiedElevation.Num() == VertexCount, TEXT("VertexAmplifiedElevation not initialized"));
    checkf(VertexElevationValues.Num() == VertexCount, TEXT("VertexElevationValues not initialized (must run erosion first)"));
    checkf(VertexCrustAge.Num() == VertexCount, TEXT("VertexCrustAge not initialized (must run oceanic dampening first)"));
    checkf(VertexRidgeDirections.Num() == VertexCount, TEXT("VertexRidgeDirections not initialized (must run ComputeRidgeDirections first)"));

    auto FindPlateByID = [this](int32 LookupPlateID) -> const FTectonicPlate*
    {
        if (LookupPlateID == INDEX_NONE)
        {
            return nullptr;
        }

        for (const FTectonicPlate& Plate : Plates)
        {
            if (Plate.PlateID == LookupPlateID)
            {
                return &Plate;
            }
        }

        return nullptr;
    };

    int32 DebugMismatchCount = 0;
    static int32 ContinentalAmplifiedLogCount = 0;
    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const FVector3d& VertexPosition = RenderVertices[VertexIdx];
        const int32 PlateID = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;
        const double BaseElevation_m = VertexElevationValues[VertexIdx];
        const double CrustAge_My = VertexCrustAge[VertexIdx];
        const FVector3d& RidgeDirection = VertexRidgeDirections[VertexIdx];

        // Debug Step 1: Verify vertex→plate mapping sanity
        if (const FTectonicPlate* PlatePtr = FindPlateByID(PlateID))
        {
            // Flag vertices with oceanic depths (-3500m) but continental plate assignment
            if (PlatePtr->CrustType != ECrustType::Oceanic &&
                BaseElevation_m < Parameters.SeaLevel - 10.0 &&
                DebugMismatchCount < 3)
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("StageB: vertex %d depth %.1f m but plate %d marked %s"),
                    VertexIdx, BaseElevation_m, PlateID,
                    PlatePtr->CrustType == ECrustType::Continental ? TEXT("continental") : TEXT("other"));
                DebugMismatchCount++;
            }
        }

        const FTectonicPlate* PlatePtr = FindPlateByID(PlateID);
        const bool bPlateIsOceanic = PlatePtr && PlatePtr->CrustType == ECrustType::Oceanic;

        // Call amplification function from OceanicAmplification.cpp
        const double AmplifiedElevation = ComputeOceanicAmplification(
            VertexPosition,
            PlateID,
            CrustAge_My,
            BaseElevation_m,
            RidgeDirection,
            Plates,
            Boundaries,
            Parameters
        );

        if (!bPlateIsOceanic)
        {
            if (ContinentalAmplifiedLogCount < 5)
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[StageB][Skip] Vertex %d PlateID=%d is non-oceanic; forcing base elevation"), VertexIdx, PlateID);
                ContinentalAmplifiedLogCount++;
            }
            VertexAmplifiedElevation[VertexIdx] = BaseElevation_m;
            continue;
        }

        VertexAmplifiedElevation[VertexIdx] = AmplifiedElevation;
    }

    // Milestone 4 Phase 4.2: Increment surface data version (elevation changed)
    SurfaceDataVersion++;
    BumpOceanicAmplificationSerial();
}

// ============================================================================
// Milestone 6 Task 2.2: Continental Amplification (Stage B)
// ============================================================================

void UTectonicSimulationService::ApplyContinentalAmplification()
{
    // Paper Section 5: Apply exemplar-based amplification to continental crust
    // Terrain type classification + heightfield blending

    const int32 VertexCount = RenderVertices.Num();
    checkf(VertexAmplifiedElevation.Num() == VertexCount, TEXT("VertexAmplifiedElevation not initialized"));
    checkf(VertexElevationValues.Num() == VertexCount, TEXT("VertexElevationValues not initialized (must run erosion first)"));
    checkf(VertexCrustAge.Num() == VertexCount, TEXT("VertexCrustAge not initialized"));

    LastContinentalCacheBuildSeconds = 0.0;
    LastContinentalCacheProfileMetrics = FContinentalCacheProfileMetrics();

    const FString ProjectContentDir = FPaths::ProjectContentDir();
    RefreshContinentalAmplificationCache();
    const FTectonicSimulationParameters SimParams = GetParameters();

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const FVector3d& VertexPosition = RenderVertices[VertexIdx];
        const double BaseElevation_m = VertexAmplifiedElevation[VertexIdx]; // Use oceanic-amplified elevation as base
        const FContinentalAmplificationCacheEntry* CacheEntry =
            ContinentalAmplificationCacheEntries.IsValidIndex(VertexIdx)
                ? &ContinentalAmplificationCacheEntries[VertexIdx]
                : nullptr;

        if (!CacheEntry || !CacheEntry->bHasCachedData)
        {
            continue;
        }

        const double AmplifiedElevation = ComputeContinentalAmplificationFromCache(
            VertexIdx,
            VertexPosition,
            BaseElevation_m,
            *CacheEntry,
            ProjectContentDir,
            SimParams.Seed);

        VertexAmplifiedElevation[VertexIdx] = AmplifiedElevation;
    }

    // Milestone 4 Phase 4.2: Increment surface data version (elevation changed)
    SurfaceDataVersion++;
    BumpOceanicAmplificationSerial();
}

// Milestone 6 GPU: Initialize GPU exemplar texture array for Stage B amplification
void UTectonicSimulationService::InitializeGPUExemplarResources()
{
    using namespace PlanetaryCreation::GPU;

    FExemplarTextureArray& ExemplarArray = GetExemplarTextureArray();

    if (ExemplarArray.IsInitialized())
    {
        UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[TectonicService] GPU exemplar resources already initialized"));
        return;
    }

    // Get project Content directory
    const FString ProjectContentDir = FPaths::ProjectContentDir();

    if (!ExemplarArray.Initialize(ProjectContentDir))
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("[TectonicService] Failed to initialize GPU exemplar texture array"));
        return;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[TectonicService] GPU exemplar resources initialized: %d textures (%dx%d)"),
        ExemplarArray.GetExemplarCount(), ExemplarArray.GetTextureWidth(), ExemplarArray.GetTextureHeight());
}

// Milestone 6 GPU: Shutdown GPU exemplar texture array
void UTectonicSimulationService::ShutdownGPUExemplarResources()
{
    using namespace PlanetaryCreation::GPU;

    FExemplarTextureArray& ExemplarArray = GetExemplarTextureArray();

    if (ExemplarArray.IsInitialized())
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[TectonicService] Shutting down GPU exemplar resources"));
        ExemplarArray.Shutdown();
    }
}

void UTectonicSimulationService::GetRenderVertexFloatSoA(
    const TArray<float>*& OutPositionX,
    const TArray<float>*& OutPositionY,
    const TArray<float>*& OutPositionZ,
    const TArray<float>*& OutNormalX,
    const TArray<float>*& OutNormalY,
    const TArray<float>*& OutNormalZ,
    const TArray<float>*& OutTangentX,
    const TArray<float>*& OutTangentY,
    const TArray<float>*& OutTangentZ) const
{
    RefreshRenderVertexFloatSoA();

    OutPositionX = &RenderVertexFloatSoA.PositionX;
    OutPositionY = &RenderVertexFloatSoA.PositionY;
    OutPositionZ = &RenderVertexFloatSoA.PositionZ;
    OutNormalX = &RenderVertexFloatSoA.NormalX;
    OutNormalY = &RenderVertexFloatSoA.NormalY;
    OutNormalZ = &RenderVertexFloatSoA.NormalZ;
    OutTangentX = &RenderVertexFloatSoA.TangentX;
    OutTangentY = &RenderVertexFloatSoA.TangentY;
    OutTangentZ = &RenderVertexFloatSoA.TangentZ;
}

void UTectonicSimulationService::GetOceanicAmplificationFloatInputs(
    const TArray<float>*& OutBaselineElevation,
    const TArray<FVector4f>*& OutRidgeDirections,
    const TArray<float>*& OutCrustAge,
    const TArray<FVector3f>*& OutRenderPositions,
    const TArray<uint32>*& OutOceanicMask) const
{
    RefreshOceanicAmplificationFloatInputs();

    OutBaselineElevation = &OceanicAmplificationFloatInputs.BaselineElevation;
    OutRidgeDirections = &OceanicAmplificationFloatInputs.RidgeDirections;
    OutCrustAge = &OceanicAmplificationFloatInputs.CrustAge;
    OutRenderPositions = &OceanicAmplificationFloatInputs.RenderPositions;
    OutOceanicMask = &OceanicAmplificationFloatInputs.OceanicMask;
}

bool UTectonicSimulationService::CreateContinentalAmplificationSnapshot(FContinentalAmplificationSnapshot& OutSnapshot) const
{
    OutSnapshot = FContinentalAmplificationSnapshot();

    const FContinentalAmplificationGPUInputs& Inputs = GetContinentalAmplificationGPUInputs();
    const int32 VertexCount = Inputs.BaselineElevation.Num();
    if (VertexCount <= 0)
    {
        return false;
    }

    const TArray<FContinentalAmplificationCacheEntry>& CacheEntries = GetContinentalAmplificationCacheEntries();
    if (CacheEntries.Num() != VertexCount)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("[ContinentalGPU] Snapshot cache size mismatch (Cache=%d Expected=%d)"),
            CacheEntries.Num(),
            VertexCount);
        return false;
    }

    const TArray<int32>& PlateAssignments = GetVertexPlateAssignments();
    if (PlateAssignments.Num() != VertexCount)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("[ContinentalGPU] Snapshot plate assignment mismatch (Assignments=%d Expected=%d)"),
            PlateAssignments.Num(),
            VertexCount);
        return false;
    }

    OutSnapshot.VertexCount = VertexCount;
    OutSnapshot.DataSerial = GetOceanicAmplificationDataSerial();
    OutSnapshot.TopologyVersion = GetTopologyVersion();
    OutSnapshot.SurfaceVersion = GetSurfaceDataVersion();
    OutSnapshot.Parameters = GetParameters();
    OutSnapshot.BaselineElevation = Inputs.BaselineElevation;
    OutSnapshot.RenderPositions = Inputs.RenderPositions;
    OutSnapshot.CacheEntries = CacheEntries;
    OutSnapshot.PlateAssignments = PlateAssignments;
    const TArray<double>& CurrentAmplified = GetVertexAmplifiedElevation();
    if (CurrentAmplified.Num() != VertexCount)
    {
        UE_LOG(LogPlanetaryCreation, Warning,
            TEXT("[ContinentalGPU] Snapshot amplified array mismatch (%d vs expected %d)"),
            CurrentAmplified.Num(),
            VertexCount);
        return false;
    }
    OutSnapshot.AmplifiedElevation = CurrentAmplified;
    OutSnapshot.Hash = HashContinentalSnapshot(OutSnapshot);
    if (!OutSnapshot.Hash)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[ContinentalGPU] Snapshot hash is zero; validation safeguards are limited this run."));
    }

    return true;
}

const FContinentalAmplificationGPUInputs& UTectonicSimulationService::GetContinentalAmplificationGPUInputs() const
{
    RefreshContinentalAmplificationGPUInputs();
    return ContinentalAmplificationGPUInputs;
}

const TArray<FContinentalAmplificationCacheEntry>& UTectonicSimulationService::GetContinentalAmplificationCacheEntries() const
{
    RefreshContinentalAmplificationCache();
    return ContinentalAmplificationCacheEntries;
}

#if UE_BUILD_DEVELOPMENT
void UTectonicSimulationService::ForceContinentalSnapshotSerialDrift()
{
    BumpOceanicAmplificationSerial();
}

void UTectonicSimulationService::ResetAmplifiedElevationForTests()
{
    InitializeAmplifiedElevationBaseline();
}
#endif

#if WITH_EDITOR
void UTectonicSimulationService::EnqueueOceanicGPUJob(TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> Readback, int32 VertexCount, FOceanicAmplificationSnapshot&& Snapshot)
{
    if (!Readback.IsValid() || VertexCount <= 0)
    {
        return;
    }

    FOceanicGPUAsyncJob& Job = PendingOceanicGPUJobs.AddDefaulted_GetRef();
    Job.Readback = MoveTemp(Readback);
    Job.VertexCount = VertexCount;
    Job.NumBytes = VertexCount * sizeof(float);
    Job.DispatchFence.BeginFence();
    Job.Snapshot = MoveTemp(Snapshot);
}

void UTectonicSimulationService::EnqueueContinentalGPUJob(TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> Readback, int32 VertexCount, FContinentalAmplificationSnapshot&& Snapshot)
{
    if (!Readback.IsValid() || VertexCount <= 0)
    {
        return;
    }

    FContinentalGPUAsyncJob& Job = PendingContinentalGPUJobs.AddDefaulted_GetRef();
    Job.Readback = MoveTemp(Readback);
    Job.VertexCount = VertexCount;
    Job.NumBytes = VertexCount * sizeof(float);
    Job.DispatchFence.BeginFence();
    Job.Snapshot = MoveTemp(Snapshot);
    Job.JobId = NextContinentalGPUJobId++;
}

#if WITH_EDITOR
TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> UTectonicSimulationService::AcquireOceanicGPUReadbackBuffer()
{
    const int32 DesiredPoolSize = 2;
    if (OceanicReadbackPool.Num() < DesiredPoolSize)
    {
        const int32 StartIndex = OceanicReadbackPool.Num();
        for (int32 Index = StartIndex; Index < DesiredPoolSize; ++Index)
        {
            const FString Label = FString::Printf(TEXT("PlanetaryCreation.OceanicGPU.Readback[%d]"), Index);
            OceanicReadbackPool.Add(MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(*Label));
        }
    }

    const int32 PoolCount = OceanicReadbackPool.Num();
    const int32 SafePoolCount = FMath::Max(PoolCount, 1);
    for (int32 Attempt = 0; Attempt < PoolCount; ++Attempt)
    {
        const int32 Index = (NextOceanicReadbackIndex + Attempt) % SafePoolCount;
        TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe>& Candidate = OceanicReadbackPool[Index];
        if (!Candidate.IsValid())
        {
            const FString Label = FString::Printf(TEXT("PlanetaryCreation.OceanicGPU.Readback[%d]"), Index);
            Candidate = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(*Label);
        }

        if (!IsOceanicReadbackInFlight(Candidate))
        {
            NextOceanicReadbackIndex = (Index + 1) % SafePoolCount;
            return Candidate;
        }
    }

    const FString Label = FString::Printf(TEXT("PlanetaryCreation.OceanicGPU.Readback[%d]"), OceanicReadbackPool.Num());
    TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> NewReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(*Label);
    OceanicReadbackPool.Add(NewReadback);
    NextOceanicReadbackIndex = OceanicReadbackPool.Num() > 0 ? OceanicReadbackPool.Num() - 1 : 0;
    return NewReadback;
}

TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> UTectonicSimulationService::AcquireContinentalGPUReadbackBuffer()
{
    const int32 DesiredPoolSize = 2;
    if (ContinentalReadbackPool.Num() < DesiredPoolSize)
    {
        const int32 StartIndex = ContinentalReadbackPool.Num();
        for (int32 Index = StartIndex; Index < DesiredPoolSize; ++Index)
        {
            const FString Label = FString::Printf(TEXT("PlanetaryCreation.ContinentalGPU.Readback[%d]"), Index);
            ContinentalReadbackPool.Add(MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(*Label));
        }
    }

    const int32 PoolCount = ContinentalReadbackPool.Num();
    const int32 SafePoolCount = FMath::Max(PoolCount, 1);
    for (int32 Attempt = 0; Attempt < PoolCount; ++Attempt)
    {
        const int32 Index = (NextContinentalReadbackIndex + Attempt) % SafePoolCount;
        TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe>& Candidate = ContinentalReadbackPool[Index];
        if (!Candidate.IsValid())
        {
            const FString Label = FString::Printf(TEXT("PlanetaryCreation.ContinentalGPU.Readback[%d]"), Index);
            Candidate = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(*Label);
        }

        if (!IsContinentalReadbackInFlight(Candidate))
        {
            NextContinentalReadbackIndex = (Index + 1) % SafePoolCount;
            return Candidate;
        }
    }

    const FString Label = FString::Printf(TEXT("PlanetaryCreation.ContinentalGPU.Readback[%d]"), ContinentalReadbackPool.Num());
    TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> NewReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(*Label);
    ContinentalReadbackPool.Add(NewReadback);
    NextContinentalReadbackIndex = ContinentalReadbackPool.Num() > 0 ? ContinentalReadbackPool.Num() - 1 : 0;
    return NewReadback;
}

bool UTectonicSimulationService::IsOceanicReadbackInFlight(const TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe>& Readback) const
{
    if (!Readback.IsValid())
    {
        return false;
    }

    for (const FOceanicGPUAsyncJob& Job : PendingOceanicGPUJobs)
    {
        if (Job.Readback == Readback)
        {
            return true;
        }
    }

    return false;
}

bool UTectonicSimulationService::IsContinentalReadbackInFlight(const TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe>& Readback) const
{
    if (!Readback.IsValid())
    {
        return false;
    }

    for (const FContinentalGPUAsyncJob& Job : PendingContinentalGPUJobs)
    {
        if (Job.Readback == Readback)
        {
            return true;
        }
    }

    return false;
}

bool UTectonicSimulationService::EnsureLatestOceanicSnapshotApplied()
{
    if (PendingOceanicGPUJobs.Num() == 0)
    {
        return false;
    }

    const int32 LatestIndex = PendingOceanicGPUJobs.Num() - 1;
    FOceanicGPUAsyncJob& LatestJob = PendingOceanicGPUJobs[LatestIndex];
    if (LatestJob.bCpuReplayApplied)
    {
        return false;
    }

    const FOceanicAmplificationSnapshot& Snapshot = LatestJob.Snapshot;
    if (!Snapshot.IsConsistent())
    {
        LatestJob.bCpuReplayApplied = true;
        return false;
    }

    const int32 NumVertices = Snapshot.VertexCount;
    TArray<double>& Amplified = GetMutableVertexAmplifiedElevation();
    Amplified.SetNum(NumVertices);

    const FString ProjectContentDir = FPaths::ProjectContentDir();
    for (int32 Index = 0; Index < NumVertices; ++Index)
    {
        const double CpuValue = EvaluateOceanicSnapshotVertex(Snapshot, Index, ProjectContentDir, Plates, Boundaries);
        Amplified[Index] = CpuValue;
    }

    LatestJob.bCpuReplayApplied = true;
    for (int32 JobIndex = 0; JobIndex < LatestIndex; ++JobIndex)
    {
        PendingOceanicGPUJobs[JobIndex].bCpuReplayApplied = true;
    }

    BumpOceanicAmplificationSerial();
    LatestJob.Snapshot.DataSerial = GetOceanicAmplificationDataSerial();
    uint32 LiveHash = 0;
    if (!ComputeCurrentOceanicInputHash(*this, LatestJob.Snapshot, LiveHash))
    {
        LiveHash = HashOceanicSnapshot(LatestJob.Snapshot);
    }
    LatestJob.Snapshot.Hash = LiveHash;
    return true;
}
#endif

#if WITH_AUTOMATION_TESTS
int32 UTectonicSimulationService::GetPendingOceanicGPUJobCount() const
{
#if WITH_EDITOR
    return PendingOceanicGPUJobs.Num();
#else
    return 0;
#endif
}
#endif

void UTectonicSimulationService::ProcessPendingOceanicGPUReadbacks(bool bBlockUntilComplete, double* OutReadbackSeconds)
{
    double AccumulatedSeconds = 0.0;
    static int32 ContinentalGPUCorrectionLogs = 0;
#if UE_BUILD_DEVELOPMENT
    static int32 OceanicDebugComparisonCount = 0;
#endif

    for (int32 JobIndex = PendingOceanicGPUJobs.Num() - 1; JobIndex >= 0; --JobIndex)
    {
        FOceanicGPUAsyncJob& Job = PendingOceanicGPUJobs[JobIndex];
        if (!Job.Readback.IsValid())
        {
            PendingOceanicGPUJobs.RemoveAt(JobIndex);
            BumpOceanicAmplificationSerial();
            continue;
        }

        if (!Job.DispatchFence.IsFenceComplete())
        {
            if (bBlockUntilComplete)
            {
                const double WaitStart = FPlatformTime::Seconds();
                Job.DispatchFence.Wait();
                AccumulatedSeconds += FPlatformTime::Seconds() - WaitStart;
            }
            else
            {
                continue;
            }
        }

        if (!Job.Readback->IsReady())
        {
            if (bBlockUntilComplete)
            {
                const double ReadbackWaitStart = FPlatformTime::Seconds();
                while (!Job.Readback->IsReady())
                {
                    FPlatformProcess::SleepNoStats(0.001f);
                }
                AccumulatedSeconds += FPlatformTime::Seconds() - ReadbackWaitStart;
            }
            else
            {
                continue;
            }
        }

        const int32 NumFloats = Job.VertexCount;

        TSharedRef<TArray<float>, ESPMode::ThreadSafe> TempData = MakeShared<TArray<float>, ESPMode::ThreadSafe>();
        TempData->SetNum(NumFloats);

        ENQUEUE_RENDER_COMMAND(CopyOceanicGPUReadback)(
            [Readback = Job.Readback, NumBytes = Job.NumBytes, TempData](FRHICommandListImmediate& RHICmdList)
            {
                if (!Readback.IsValid())
                {
                    return;
                }

                const float* GPUData = static_cast<const float*>(Readback->Lock(NumBytes));
                if (GPUData)
                {
                FMemory::Memcpy(TempData->GetData(), GPUData, NumBytes);
                }
                Readback->Unlock();
            });

        Job.CopyFence.BeginFence();
        if (bBlockUntilComplete)
        {
            const double CopyWaitStart = FPlatformTime::Seconds();
            Job.CopyFence.Wait();
            AccumulatedSeconds += FPlatformTime::Seconds() - CopyWaitStart;
        }
        else if (!Job.CopyFence.IsFenceComplete())
        {
            continue;
        }

        const FOceanicAmplificationSnapshot& Snapshot = Job.Snapshot;
        const bool bSnapshotConsistent = Snapshot.IsConsistent() &&
            Snapshot.VertexCount == NumFloats &&
            Snapshot.DataSerial == GetOceanicAmplificationDataSerial();

        bool bUseSnapshot = bSnapshotConsistent;
        if (bUseSnapshot)
        {
            uint32 CurrentHash = 0;
            if (!ComputeCurrentOceanicInputHash(*this, Snapshot, CurrentHash) || CurrentHash != Snapshot.Hash)
            {
                UE_LOG(LogPlanetaryCreation, Warning,
                    TEXT("[StageB][GPU] Oceanic snapshot hash mismatch for JobId %llu (expected 0x%08x, got 0x%08x). Falling back to live CPU recompute."),
                    Job.JobId,
                    Snapshot.Hash,
                    CurrentHash);
                bUseSnapshot = false;
            }
        }
        else
        {
            UE_LOG(LogPlanetaryCreation, Verbose,
                TEXT("[StageB][GPU] Oceanic snapshot inconsistent for JobId %llu. Falling back to live CPU recompute."),
                Job.JobId);
        }

        const bool bSnapshotUsable = Snapshot.IsConsistent() && Snapshot.VertexCount == NumFloats;
        const FString ProjectContentDir = FPaths::ProjectContentDir();

        if (!bUseSnapshot && bSnapshotUsable)
        {
            UE_LOG(LogPlanetaryCreation, Verbose,
                TEXT("[StageB][GPU] Oceanic snapshot mismatch for JobId %llu. Replaying snapshot on CPU to preserve parity."),
                Job.JobId);
        }

        if (!bUseSnapshot && !bSnapshotUsable)
        {
            TArray<double>& Amplified = GetMutableVertexAmplifiedElevation();
            Amplified.SetNum(NumFloats);
            for (int32 Index = 0; Index < NumFloats; ++Index)
            {
                const double GPUValue = static_cast<double>((*TempData)[Index]);
                const int32 PlateId = VertexPlateAssignments.IsValidIndex(Index) ? VertexPlateAssignments[Index] : INDEX_NONE;
                bool bOceanicPlate = false;
                const double BaseElevation = VertexElevationValues.IsValidIndex(Index) ? VertexElevationValues[Index] : GPUValue;
                if (PlateId != INDEX_NONE)
                {
                    for (const FTectonicPlate& Plate : Plates)
                    {
                        if (Plate.PlateID == PlateId)
                        {
                            bOceanicPlate = (Plate.CrustType == ECrustType::Oceanic);
                            break;
                        }
                    }
                }

                if (bOceanicPlate)
                {
                    const FVector3d& Position = RenderVertices.IsValidIndex(Index) ? RenderVertices[Index] : FVector3d::ZeroVector;
                    const FVector3d& RidgeDir = VertexRidgeDirections.IsValidIndex(Index) ? VertexRidgeDirections[Index] : FVector3d::ZAxisVector;
                    const double CrustAge = VertexCrustAge.IsValidIndex(Index) ? VertexCrustAge[Index] : 0.0;
                    const double CpuValue = ComputeOceanicAmplification(
                        Position,
                        PlateId,
                        CrustAge,
                        BaseElevation,
                        RidgeDir,
                        Plates,
                        Boundaries,
                        GetParameters());

                    if (FMath::Abs(CpuValue - GPUValue) > 1.0)
                    {
                        UE_LOG(LogPlanetaryCreation, VeryVerbose,
                            TEXT("[StageB][GPU][ParityAdjust] Vertex %d Plate=%d CPU=%.3f GPU=%.3f Base=%.3f"),
                            Index,
                            PlateId,
                            CpuValue,
                            GPUValue,
                            BaseElevation);
                    }

                    Amplified[Index] = CpuValue;
                }
                else
                {
                    Amplified[Index] = BaseElevation;
                    if (ContinentalGPUCorrectionLogs < 5)
                    {
                        const double Diff = FMath::Abs(GPUValue - BaseElevation);
                        if (Diff > 1.0)
                        {
                            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[StageB][GPU] Continental vertex %d masked out, restoring base elevation (Diff %.3f m)"), Index, Diff);
                            ContinentalGPUCorrectionLogs++;
                        }
                    }
                }
            }

            PendingOceanicGPUJobs.RemoveAt(JobIndex);
            BumpOceanicAmplificationSerial();
            continue;
        }

        TArray<double>& Amplified = GetMutableVertexAmplifiedElevation();
        Amplified.SetNum(NumFloats);

        const FOceanicAmplificationSnapshot* ActiveSnapshot = bSnapshotUsable ? &Snapshot : nullptr;

        for (int32 Index = 0; Index < NumFloats; ++Index)
        {
            const double GPUValue = static_cast<double>((*TempData)[Index]);
            const double Baseline = ActiveSnapshot && ActiveSnapshot->BaselineElevation.IsValidIndex(Index)
                ? static_cast<double>(ActiveSnapshot->BaselineElevation[Index])
                : (VertexAmplifiedElevation.IsValidIndex(Index) ? VertexAmplifiedElevation[Index] : 0.0);
            const int32 PlateId = ActiveSnapshot && ActiveSnapshot->PlateAssignments.IsValidIndex(Index)
                ? ActiveSnapshot->PlateAssignments[Index]
                : (VertexPlateAssignments.IsValidIndex(Index) ? VertexPlateAssignments[Index] : INDEX_NONE);
            const bool bOceanic = ActiveSnapshot && ActiveSnapshot->OceanicMask.IsValidIndex(Index)
                ? ActiveSnapshot->OceanicMask[Index] != 0u
                : (PlateId != INDEX_NONE && Plates.IsValidIndex(PlateId) && Plates[PlateId].CrustType == ECrustType::Oceanic);

            double CpuValue = Baseline;
            if (bOceanic && ActiveSnapshot)
            {
                CpuValue = EvaluateOceanicSnapshotVertex(*ActiveSnapshot, Index, ProjectContentDir, Plates, Boundaries);
            }
            else if (bOceanic)
            {
                const FVector3d& Position = RenderVertices.IsValidIndex(Index) ? RenderVertices[Index] : FVector3d::ZeroVector;
                const FVector3d& RidgeDir = VertexRidgeDirections.IsValidIndex(Index) ? VertexRidgeDirections[Index] : FVector3d::ZAxisVector;
                const double CrustAge = VertexCrustAge.IsValidIndex(Index) ? VertexCrustAge[Index] : 0.0;
                CpuValue = ComputeOceanicAmplification(
                    Position,
                    PlateId,
                    CrustAge,
                    Baseline,
                    RidgeDir,
                    Plates,
                    Boundaries,
                    GetParameters());
            }

            Amplified[Index] = CpuValue;

#if UE_BUILD_DEVELOPMENT
            const double Delta = FMath::Abs(CpuValue - GPUValue);
            const bool bShouldLog = (OceanicDebugComparisonCount < 5) || (Delta > 1.0);
            if (bShouldLog)
            {
                const FVector3f PositionFloat = Snapshot.RenderPositions.IsValidIndex(Index) ? Snapshot.RenderPositions[Index] : FVector3f::ZeroVector;
                const FVector4f RidgeFloat = Snapshot.RidgeDirections.IsValidIndex(Index) ? Snapshot.RidgeDirections[Index] : FVector4f(0.0f, 0.0f, 1.0f, 0.0f);

                UE_LOG(LogPlanetaryCreation, Log,
                    TEXT("[StageB][GPU][ParityAdjust] Vtx=%d Plate=%d CPU=%.3f GPU=%.3f Base=%.3f Ridge=(%.3f,%.3f,%.3f) Pos=(%.3f,%.3f,%.3f)"),
                    Index,
                    PlateId,
                    CpuValue,
                    GPUValue,
                    Baseline,
                    RidgeFloat.X, RidgeFloat.Y, RidgeFloat.Z,
                    PositionFloat.X, PositionFloat.Y, PositionFloat.Z);

                ++OceanicDebugComparisonCount;
            }
#endif

            if (!bOceanic && FMath::Abs(GPUValue - Baseline) > 1.0 && ContinentalGPUCorrectionLogs < 5)
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[StageB][GPU] Non-oceanic vertex %d received GPU override (Diff %.3f m)"), Index, FMath::Abs(GPUValue - Baseline));
                ContinentalGPUCorrectionLogs++;
            }
        }

        PendingOceanicGPUJobs.RemoveAt(JobIndex);
        BumpOceanicAmplificationSerial();
        continue;
    }

    if (OutReadbackSeconds)
    {
        *OutReadbackSeconds += AccumulatedSeconds;
    }
}

void UTectonicSimulationService::ProcessPendingContinentalGPUReadbacks(bool bBlockUntilComplete, double* OutReadbackSeconds)
{
    double AccumulatedSeconds = 0.0;

#if UE_BUILD_DEVELOPMENT
    static int32 DebugComparisonCount = 0;
#endif
    bool bAppliedAnyJob = false;

    for (int32 JobIndex = PendingContinentalGPUJobs.Num() - 1; JobIndex >= 0; --JobIndex)
    {
        FContinentalGPUAsyncJob& Job = PendingContinentalGPUJobs[JobIndex];
        if (!Job.Readback.IsValid())
        {
            PendingContinentalGPUJobs.RemoveAt(JobIndex);
            continue;
        }

        if (!Job.DispatchFence.IsFenceComplete())
        {
            if (bBlockUntilComplete)
            {
                const double WaitStart = FPlatformTime::Seconds();
                Job.DispatchFence.Wait();
                AccumulatedSeconds += FPlatformTime::Seconds() - WaitStart;
            }
            else
            {
                continue;
            }
        }

        if (!Job.Readback->IsReady())
        {
            if (bBlockUntilComplete)
            {
                const double ReadbackWaitStart = FPlatformTime::Seconds();
                while (!Job.Readback->IsReady())
                {
                    FPlatformProcess::SleepNoStats(0.001f);
                }
                AccumulatedSeconds += FPlatformTime::Seconds() - ReadbackWaitStart;
            }
            else
            {
                continue;
            }
        }

        const int32 NumFloats = Job.VertexCount;

        TSharedRef<TArray<float>, ESPMode::ThreadSafe> TempData = MakeShared<TArray<float>, ESPMode::ThreadSafe>();
        TempData->SetNum(NumFloats);

        ENQUEUE_RENDER_COMMAND(CopyContinentalGPUReadback)(
            [Readback = Job.Readback, NumBytes = Job.NumBytes, TempData](FRHICommandListImmediate& RHICmdList)
            {
                if (!Readback.IsValid())
                {
                    return;
                }

                const float* GPUData = static_cast<const float*>(Readback->Lock(NumBytes));
                if (GPUData)
                {
                    FMemory::Memcpy(TempData->GetData(), GPUData, NumBytes);
                }
                Readback->Unlock();
            });

        Job.CopyFence.BeginFence();
        if (bBlockUntilComplete)
        {
            const double CopyWaitStart = FPlatformTime::Seconds();
            Job.CopyFence.Wait();
            AccumulatedSeconds += FPlatformTime::Seconds() - CopyWaitStart;
        }
        else if (!Job.CopyFence.IsFenceComplete())
        {
            continue;
        }

        const FContinentalAmplificationSnapshot& Snapshot = Job.Snapshot;
        const bool bSnapshotUsable = Snapshot.IsConsistent() && Snapshot.VertexCount == NumFloats;
        bool bSnapshotConsistent = false;
        if (bSnapshotUsable)
        {
            bSnapshotConsistent =
                Snapshot.DataSerial == GetOceanicAmplificationDataSerial() &&
                Snapshot.TopologyVersion == GetTopologyVersion() &&
                Snapshot.SurfaceVersion == GetSurfaceDataVersion();
        }

        bool bUseSnapshot = bSnapshotConsistent;
        if (bUseSnapshot)
        {
            uint32 CurrentHash = 0;
            if (!ComputeCurrentContinentalInputHash(*this, Snapshot, CurrentHash) || CurrentHash != Snapshot.Hash)
            {
                UE_LOG(LogPlanetaryCreation, Warning,
                    TEXT("[ContinentalGPU] Snapshot hash mismatch for JobId %llu (expected 0x%08x, got 0x%08x). Falling back to CPU replay of snapshot."),
                    Job.JobId,
                    Snapshot.Hash,
                    CurrentHash);
                bUseSnapshot = false;
            }
        }
        else if (bSnapshotUsable)
        {
            UE_LOG(LogPlanetaryCreation, Verbose,
                TEXT("[ContinentalGPU] Snapshot metadata mismatch for JobId %llu (DataSerial=%llu/%llu Topology=%d/%d Surface=%d/%d). Using snapshot fallback."),
                Job.JobId,
                Snapshot.DataSerial,
                GetOceanicAmplificationDataSerial(),
                Snapshot.TopologyVersion,
                GetTopologyVersion(),
                Snapshot.SurfaceVersion,
                GetSurfaceDataVersion());
        }
        else
        {
            UE_LOG(LogPlanetaryCreation, Verbose,
                TEXT("[ContinentalGPU] No usable snapshot for JobId %llu (Consistent=%d VertexCount=%d). Using live data fallback."),
                Job.JobId,
                Snapshot.IsConsistent(),
                Snapshot.VertexCount);
        }

        const FContinentalAmplificationSnapshot* ActiveSnapshot = bSnapshotUsable ? &Snapshot : nullptr;
        const TCHAR* SummaryLabel = ActiveSnapshot ? (bUseSnapshot ? TEXT("snapshot") : TEXT("snapshot fallback")) : TEXT("live fallback");

#if UE_BUILD_DEVELOPMENT
        UE_LOG(LogPlanetaryCreation, Log,
            TEXT("[ContinentalGPUReadback] JobId=%llu VertexCount=%d SnapshotUsable=%d UseSnapshot=%d Summary=%s"),
            Job.JobId,
            NumFloats,
            bSnapshotUsable ? 1 : 0,
            bUseSnapshot ? 1 : 0,
            SummaryLabel);
#endif

        TArray<double>& Amplified = GetMutableVertexAmplifiedElevation();
        Amplified.SetNum(NumFloats);

        if (bUseSnapshot && ActiveSnapshot)
        {
            bAppliedAnyJob = true;
            LastContinentalCacheBuildSeconds = 0.0;
#if UE_BUILD_DEVELOPMENT
            double AccumulatedDelta = 0.0;
            double MaxDelta = 0.0;
            int32 SampleCount = 0;
#endif

            for (int32 Index = 0; Index < NumFloats; ++Index)
            {
                const double GPUValue = static_cast<double>((*TempData)[Index]);
                Amplified[Index] = GPUValue;

#if UE_BUILD_DEVELOPMENT
                if (ActiveSnapshot->AmplifiedElevation.IsValidIndex(Index))
                {
                    const double CpuValue = ActiveSnapshot->AmplifiedElevation[Index];
                    const double Delta = FMath::Abs(CpuValue - GPUValue);
                    AccumulatedDelta += Delta;
                    MaxDelta = FMath::Max(MaxDelta, Delta);
                    ++SampleCount;

                    if ((DebugComparisonCount < 5) || Delta > 1.0)
                    {
                        const FContinentalAmplificationCacheEntry& CacheEntry = ActiveSnapshot->CacheEntries[Index];
                        const uint32 TerrainInfo = static_cast<uint32>(CacheEntry.TerrainType) | (CacheEntry.ExemplarCount << 8u);

                        UE_LOG(LogPlanetaryCreation, Log,
                            TEXT("[ContinentalGPUReadback][Compare] Vtx=%d Base=%.2f CPU=%.2f GPU=%.2f Delta=%.2f Terrain=%u Source=Snapshot"),
                            Index,
                            ActiveSnapshot->BaselineElevation.IsValidIndex(Index) ? ActiveSnapshot->BaselineElevation[Index] : 0.0f,
                            CpuValue,
                            GPUValue,
                            Delta,
                            TerrainInfo);
                        ++DebugComparisonCount;
                    }
                }
#endif
            }

#if UE_BUILD_DEVELOPMENT
            const double MeanDelta = SampleCount > 0 ? AccumulatedDelta / static_cast<double>(SampleCount) : 0.0;
            UE_LOG(LogPlanetaryCreation, Log,
                TEXT("[ContinentalGPUReadback] GPU applied %d verts | MeanDelta=%.3f MaxDelta=%.3f (%s)"),
                NumFloats,
                MeanDelta,
                MaxDelta,
                SummaryLabel);
#endif
        }
        else
        {
            const FContinentalAmplificationGPUInputs& LiveInputs = GetContinentalAmplificationGPUInputs();
            const TArray<FContinentalAmplificationCacheEntry>& LiveCache = GetContinentalAmplificationCacheEntries();
            const FString ProjectContentDir = FPaths::ProjectContentDir();

            int32 ContinentalOverrideCount = 0;
#if UE_BUILD_DEVELOPMENT
            double AccumulatedDelta = 0.0;
            double MaxDelta = 0.0;
#endif

            for (int32 Index = 0; Index < NumFloats; ++Index)
            {
                const bool bHasSnapshotEntry = ActiveSnapshot &&
                    ActiveSnapshot->BaselineElevation.IsValidIndex(Index) &&
                    ActiveSnapshot->CacheEntries.IsValidIndex(Index) &&
                    ActiveSnapshot->RenderPositions.IsValidIndex(Index) &&
                    ActiveSnapshot->AmplifiedElevation.IsValidIndex(Index);

                const FContinentalAmplificationCacheEntry* SnapshotCacheEntry = bHasSnapshotEntry
                    ? &ActiveSnapshot->CacheEntries[Index]
                    : nullptr;

                const FContinentalAmplificationCacheEntry* LiveCacheEntry = LiveCache.IsValidIndex(Index)
                    ? &LiveCache[Index]
                    : nullptr;

                const FContinentalAmplificationCacheEntry* PreferredCache = nullptr;
                bool bUsingSnapshotData = false;

                if (bUseSnapshot && SnapshotCacheEntry && SnapshotCacheEntry->bHasCachedData && SnapshotCacheEntry->ExemplarCount > 0)
                {
                    PreferredCache = SnapshotCacheEntry;
                    bUsingSnapshotData = true;
                }
                else if (!ActiveSnapshot && LiveCacheEntry && LiveCacheEntry->bHasCachedData && LiveCacheEntry->ExemplarCount > 0)
                {
                    PreferredCache = LiveCacheEntry;
                    bUsingSnapshotData = false;
                }

                const double Baseline = bUsingSnapshotData && bHasSnapshotEntry
                    ? static_cast<double>(ActiveSnapshot->BaselineElevation[Index])
                    : (LiveInputs.BaselineElevation.IsValidIndex(Index) ? static_cast<double>(LiveInputs.BaselineElevation[Index]) : 0.0);

                double CpuValue = Baseline;
                bool bHasOverride = false;

                if (PreferredCache)
                {
                    FVector3d Position = FVector3d::ZeroVector;
                    if (bUsingSnapshotData && bHasSnapshotEntry)
                    {
                        const FVector3f& SnapshotPos = ActiveSnapshot->RenderPositions[Index];
                        Position = FVector3d(SnapshotPos.X, SnapshotPos.Y, SnapshotPos.Z);
                    }
                    else if (LiveInputs.RenderPositions.IsValidIndex(Index))
                    {
                        const FVector3f& LivePos = LiveInputs.RenderPositions[Index];
                        Position = FVector3d(LivePos.X, LivePos.Y, LivePos.Z);
                    }
                    else if (RenderVertices.IsValidIndex(Index))
                    {
                        Position = RenderVertices[Index];
                    }

                    const int32 Seed = bUsingSnapshotData && bHasSnapshotEntry
                        ? ActiveSnapshot->Parameters.Seed
                        : Parameters.Seed;

                    CpuValue = ComputeContinentalAmplificationFromCache(
                        Index,
                        Position,
                        Baseline,
                        *PreferredCache,
                        ProjectContentDir,
                        Seed);
                    bHasOverride = true;
                }
                else if (ActiveSnapshot && ActiveSnapshot->AmplifiedElevation.IsValidIndex(Index))
                {
                    CpuValue = ActiveSnapshot->AmplifiedElevation[Index];
                }

                if (bHasOverride)
                {
                    ++ContinentalOverrideCount;
                }

                Amplified[Index] = CpuValue;

#if UE_BUILD_DEVELOPMENT
                const double GPUValue = static_cast<double>((*TempData)[Index]);
                const double Delta = FMath::Abs(CpuValue - GPUValue);
                AccumulatedDelta += Delta;
                MaxDelta = FMath::Max(MaxDelta, Delta);

                if ((DebugComparisonCount < 5) || Delta > 1.0)
                {
                    const uint32 TerrainInfo = bHasSnapshotEntry
                        ? (static_cast<uint32>(ActiveSnapshot->CacheEntries[Index].TerrainType) | (ActiveSnapshot->CacheEntries[Index].ExemplarCount << 8u))
                        : (LiveInputs.PackedTerrainInfo.IsValidIndex(Index) ? LiveInputs.PackedTerrainInfo[Index] : 0u);

                    const TCHAR* SourceLabel = bHasSnapshotEntry ? (bUseSnapshot ? TEXT("Snapshot") : TEXT("SnapshotFallback")) : TEXT("Live");

                    UE_LOG(LogPlanetaryCreation, Log,
                        TEXT("[ContinentalGPUReadback][Compare] Vtx=%d Base=%.2f CPU=%.2f GPU=%.2f Delta=%.2f Terrain=%u Source=%s"),
                        Index,
                        Baseline,
                        CpuValue,
                        GPUValue,
                        Delta,
                        TerrainInfo & 0xFFu,
                        SourceLabel);
                    ++DebugComparisonCount;
                }
#endif
            }

#if UE_BUILD_DEVELOPMENT
            if (ContinentalOverrideCount > 0)
            {
                const double MeanDelta = ContinentalOverrideCount > 0 ? AccumulatedDelta / ContinentalOverrideCount : 0.0;
                UE_LOG(LogPlanetaryCreation, Log,
                    TEXT("[ContinentalGPUReadback] Overrides=%d/%d MeanDelta=%.3f MaxDelta=%.3f (%s)"),
                    ContinentalOverrideCount,
                    NumFloats,
                    MeanDelta,
                    MaxDelta,
                    SummaryLabel);
            }
            else
            {
                UE_LOG(LogPlanetaryCreation, Log,
                    TEXT("[ContinentalGPUReadback] No continental overrides applied (VertexCount=%d, Source=%s)"),
                    NumFloats,
                    SummaryLabel);
            }
#endif
            bAppliedAnyJob = true;
        }

        SurfaceDataVersion++;
        PendingContinentalGPUJobs.RemoveAt(JobIndex);
        BumpOceanicAmplificationSerial();
    }

    if (bAppliedAnyJob)
    {
        bContinentalGPUResultWasApplied = true;
    }

    if (OutReadbackSeconds)
    {
        *OutReadbackSeconds += AccumulatedSeconds;
    }
}
#else
void UTectonicSimulationService::ProcessPendingOceanicGPUReadbacks(bool bBlockUntilComplete, double* OutReadbackSeconds)
{
    (void)bBlockUntilComplete;
    (void)OutReadbackSeconds;
}

void UTectonicSimulationService::ProcessPendingContinentalGPUReadbacks(bool bBlockUntilComplete, double* OutReadbackSeconds)
{
    (void)bBlockUntilComplete;
    (void)OutReadbackSeconds;
}
#endif

void UTectonicSimulationService::RefreshRenderVertexFloatSoA() const
{
    FRenderVertexFloatSoA& Cache = RenderVertexFloatSoA;
    const int32 VertexCount = RenderVertices.Num();

    if (VertexCount <= 0)
    {
        Cache.PositionX.Reset();
        Cache.PositionY.Reset();
        Cache.PositionZ.Reset();
        Cache.NormalX.Reset();
        Cache.NormalY.Reset();
        Cache.NormalZ.Reset();
        Cache.TangentX.Reset();
        Cache.TangentY.Reset();
        Cache.TangentZ.Reset();
        return;
    }

    Cache.PositionX.SetNum(VertexCount);
    Cache.PositionY.SetNum(VertexCount);
    Cache.PositionZ.SetNum(VertexCount);
    Cache.NormalX.SetNum(VertexCount);
    Cache.NormalY.SetNum(VertexCount);
    Cache.NormalZ.SetNum(VertexCount);
    Cache.TangentX.SetNum(VertexCount);
    Cache.TangentY.SetNum(VertexCount);
    Cache.TangentZ.SetNum(VertexCount);

    for (int32 Index = 0; Index < VertexCount; ++Index)
    {
        const FVector3d& Vertex = RenderVertices[Index];
        Cache.PositionX[Index] = static_cast<float>(Vertex.X);
        Cache.PositionY[Index] = static_cast<float>(Vertex.Y);
        Cache.PositionZ[Index] = static_cast<float>(Vertex.Z);

        const FVector3d SafeNormal = Vertex.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
        Cache.NormalX[Index] = static_cast<float>(SafeNormal.X);
        Cache.NormalY[Index] = static_cast<float>(SafeNormal.Y);
        Cache.NormalZ[Index] = static_cast<float>(SafeNormal.Z);

        const FVector3d UpVector = (FMath::Abs(SafeNormal.Z) > 0.99) ? FVector3d(1.0, 0.0, 0.0) : FVector3d(0.0, 0.0, 1.0);
        FVector3d Tangent = FVector3d::CrossProduct(SafeNormal, UpVector).GetSafeNormal();
        if (Tangent.IsNearlyZero())
        {
            Tangent = FVector3d(1.0, 0.0, 0.0);
        }

        Cache.TangentX[Index] = static_cast<float>(Tangent.X);
        Cache.TangentY[Index] = static_cast<float>(Tangent.Y);
        Cache.TangentZ[Index] = static_cast<float>(Tangent.Z);
    }
}

void UTectonicSimulationService::RefreshOceanicAmplificationFloatInputs() const
{
    FOceanicAmplificationFloatInputs& Cache = OceanicAmplificationFloatInputs;

    const int32 VertexCount = VertexAmplifiedElevation.Num();
    if (Cache.CachedDataSerial == OceanicAmplificationDataSerial &&
        Cache.BaselineElevation.Num() == VertexCount)
    {
        return; // Cache is already up to date
    }

    const bool bHasValidData = VertexCount > 0 &&
        VertexCrustAge.Num() == VertexCount &&
        VertexRidgeDirections.Num() == VertexCount &&
        RenderVertices.Num() == VertexCount &&
        VertexPlateAssignments.Num() == VertexCount;

    if (!bHasValidData)
    {
        Cache.BaselineElevation.Reset();
        Cache.CrustAge.Reset();
        Cache.RidgeDirections.Reset();
        Cache.RenderPositions.Reset();
        Cache.OceanicMask.Reset();
        Cache.CachedDataSerial = OceanicAmplificationDataSerial;
        return;
    }

    Cache.BaselineElevation.SetNum(VertexCount);
    Cache.CrustAge.SetNum(VertexCount);
    Cache.RidgeDirections.SetNum(VertexCount);
    Cache.RenderPositions.SetNum(VertexCount);
    Cache.OceanicMask.SetNum(VertexCount);

    auto FindPlateByID = [this](int32 LookupPlateID) -> const FTectonicPlate*
    {
        if (LookupPlateID == INDEX_NONE)
        {
            return nullptr;
        }

        for (const FTectonicPlate& Plate : Plates)
        {
            if (Plate.PlateID == LookupPlateID)
            {
                return &Plate;
            }
        }

        return nullptr;
    };

    const bool bHasRidgeSoA =
        RidgeDirectionFloatSoA.CachedTopologyVersion == CachedRidgeDirectionTopologyVersion &&
        RidgeDirectionFloatSoA.CachedVertexCount == VertexCount &&
        RidgeDirectionFloatSoA.DirX.Num() == VertexCount &&
        RidgeDirectionFloatSoA.DirY.Num() == VertexCount &&
        RidgeDirectionFloatSoA.DirZ.Num() == VertexCount;

    for (int32 Index = 0; Index < VertexCount; ++Index)
    {
        Cache.BaselineElevation[Index] = static_cast<float>(VertexAmplifiedElevation[Index]);
        Cache.CrustAge[Index] = static_cast<float>(VertexCrustAge[Index]);

        float DirX = 0.0f;
        float DirY = 0.0f;
        float DirZ = 1.0f;

        if (bHasRidgeSoA)
        {
            DirX = RidgeDirectionFloatSoA.DirX[Index];
            DirY = RidgeDirectionFloatSoA.DirY[Index];
            DirZ = RidgeDirectionFloatSoA.DirZ[Index];
        }
        else
        {
            const FVector3d RidgeDirSafe = VertexRidgeDirections[Index].GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
            DirX = static_cast<float>(RidgeDirSafe.X);
            DirY = static_cast<float>(RidgeDirSafe.Y);
            DirZ = static_cast<float>(RidgeDirSafe.Z);
        }

        Cache.RidgeDirections[Index] = FVector4f(DirX, DirY, DirZ, 0.0f);

        const FVector3d& Position = RenderVertices[Index];
        Cache.RenderPositions[Index] = FVector3f(
            static_cast<float>(Position.X),
            static_cast<float>(Position.Y),
            static_cast<float>(Position.Z));

        uint32 bIsOceanic = 0;
        const int32 PlateId = VertexPlateAssignments.IsValidIndex(Index) ? VertexPlateAssignments[Index] : INDEX_NONE;
        if (const FTectonicPlate* PlatePtr = FindPlateByID(PlateId))
        {
            if (PlatePtr->CrustType == ECrustType::Oceanic)
            {
                bIsOceanic = 1;
            }
        }
        Cache.OceanicMask[Index] = bIsOceanic;
    }

    Cache.CachedDataSerial = OceanicAmplificationDataSerial;
}

void UTectonicSimulationService::RefreshContinentalAmplificationGPUInputs() const
{
    const int32 StageBLogModeLocal = CVarPlanetaryCreationStageBProfiling.GetValueOnAnyThread();
    const bool bCaptureMetrics = (StageBLogModeLocal > 0);
    const double FunctionStart = bCaptureMetrics ? FPlatformTime::Seconds() : 0.0;
    FContinentalCacheProfileMetrics LocalMetrics;

    if (!bCaptureMetrics)
    {
        LastContinentalCacheProfileMetrics = FContinentalCacheProfileMetrics();
        LastContinentalCacheBuildSeconds = 0.0;
    }

    FContinentalAmplificationGPUInputs& Cache = ContinentalAmplificationGPUInputs;

    const int32 VertexCount = VertexAmplifiedElevation.Num();
    if (VertexCount <= 0)
    {
        Cache.BaselineElevation.Reset();
        Cache.RenderPositions.Reset();
        Cache.PackedTerrainInfo.Reset();
        Cache.ExemplarIndices.Reset();
        Cache.ExemplarWeights.Reset();
        Cache.RandomUVOffsets.Reset();
        Cache.WrappedUVs.Reset();
        Cache.CachedDataSerial = OceanicAmplificationDataSerial;
        Cache.CachedTopologyVersion = TopologyVersion;
        Cache.CachedSurfaceVersion = SurfaceDataVersion;
        if (bCaptureMetrics)
        {
            LastContinentalCacheProfileMetrics = LocalMetrics;
            LastContinentalCacheBuildSeconds = 0.0;
        }
        return;
    }

    const bool bUpToDate = (Cache.CachedDataSerial == OceanicAmplificationDataSerial &&
        Cache.CachedTopologyVersion == TopologyVersion &&
        Cache.CachedSurfaceVersion == SurfaceDataVersion &&
        Cache.BaselineElevation.Num() == VertexCount);

    if (bUpToDate)
    {
        if (bCaptureMetrics)
        {
            LastContinentalCacheProfileMetrics = LocalMetrics;
            LastContinentalCacheBuildSeconds = 0.0;
        }
        return;
    }

    Cache.BaselineElevation.SetNum(VertexCount);
    Cache.RenderPositions.SetNum(VertexCount);
    Cache.PackedTerrainInfo.SetNum(VertexCount);
    Cache.ExemplarIndices.SetNum(VertexCount);
    Cache.ExemplarWeights.SetNum(VertexCount);
    Cache.RandomUVOffsets.SetNum(VertexCount);
    Cache.WrappedUVs.SetNum(VertexCount);

    using namespace PlanetaryCreation::GPU;

    FExemplarTextureArray& ExemplarArray = GetExemplarTextureArray();
    if (!ExemplarArray.IsInitialized())
    {
        const_cast<UTectonicSimulationService*>(this)->InitializeGPUExemplarResources();
    }

    const TArray<FExemplarTextureArray::FExemplarInfo>& ExemplarInfo = ExemplarArray.GetExemplarInfo();

    auto ResolveLibraryIndex = [&](uint32 AtlasIndex) -> uint32
    {
        if (AtlasIndex == MAX_uint32)
        {
            return MAX_uint32;
        }

        const int32 AtlasIdx = static_cast<int32>(AtlasIndex);
        if (!ExemplarInfo.IsValidIndex(AtlasIdx))
        {
            return MAX_uint32;
        }

        const int32 LibraryIndex = ExemplarInfo[AtlasIdx].LibraryIndex;
        return LibraryIndex >= 0 ? static_cast<uint32>(LibraryIndex) : MAX_uint32;
    };

    TArray<int32> AncientIndices;
    TArray<int32> AndeanIndices;
    TArray<int32> HimalayanIndices;

    AncientIndices.Reserve(ExemplarInfo.Num());
    AndeanIndices.Reserve(ExemplarInfo.Num());
    HimalayanIndices.Reserve(ExemplarInfo.Num());

    for (const FExemplarTextureArray::FExemplarInfo& Info : ExemplarInfo)
    {
        const int32 ArrayIndex = Info.ArrayIndex;
        if (ArrayIndex < 0)
        {
            continue;
        }

        if (Info.Region.Equals(TEXT("Ancient"), ESearchCase::IgnoreCase))
        {
            AncientIndices.Add(ArrayIndex);
        }
        else if (Info.Region.Equals(TEXT("Andean"), ESearchCase::IgnoreCase))
        {
            AndeanIndices.Add(ArrayIndex);
        }
        else if (Info.Region.Equals(TEXT("Himalayan"), ESearchCase::IgnoreCase))
        {
            HimalayanIndices.Add(ArrayIndex);
        }
    }

    const FTectonicSimulationParameters SimParams = GetParameters();

    TMap<int32, const FTectonicPlate*> PlateLookup;
    PlateLookup.Reserve(Plates.Num());
    for (const FTectonicPlate& Plate : Plates)
    {
        PlateLookup.Add(Plate.PlateID, &Plate);
    }

    TMap<int32, const FPlateBoundarySummary*> BoundarySummaryCache;
    BoundarySummaryCache.Reserve(Plates.Num());

    auto GetSummaryForPlate = [&](int32 PlateID) -> const FPlateBoundarySummary*
    {
        if (PlateID == INDEX_NONE)
        {
            return nullptr;
        }

        if (const FPlateBoundarySummary** Cached = BoundarySummaryCache.Find(PlateID))
        {
            return *Cached;
        }

        const FPlateBoundarySummary* Summary = GetPlateBoundarySummary(PlateID);
        BoundarySummaryCache.Add(PlateID, Summary);
        return Summary;
    };

    auto DetermineTerrainType = [&](const FTectonicPlate& SourcePlate, const FVector3d& VertexPosition,
        double BaseElevation, double OrogenyAge, const FPlateBoundarySummary* OptionalSummary) -> EContinentalTerrainType
    {
        EBoundaryType NearestBoundaryType = EBoundaryType::Transform;
        double MinDistanceToBoundary = TNumericLimits<double>::Max();
        bool bIsSubduction = false;

        const FPlateBoundarySummary* BoundarySummary = OptionalSummary;
        if (!BoundarySummary)
        {
            BoundarySummary = GetSummaryForPlate(SourcePlate.PlateID);
        }
        if (BoundarySummary)
        {
            for (const FPlateBoundarySummaryEntry& Entry : BoundarySummary->Boundaries)
            {
                if (!Entry.bHasRepresentative)
                {
                    continue;
                }

                const double Distance = FVector3d::Distance(VertexPosition, Entry.RepresentativePosition);
                if (Distance < MinDistanceToBoundary)
                {
                    MinDistanceToBoundary = Distance;
                    NearestBoundaryType = Entry.BoundaryType;
                }

                if (Entry.BoundaryType == EBoundaryType::Convergent && Entry.bIsSubduction)
                {
                    bIsSubduction = true;
                }
            }
        }
        else
        {
            for (const TPair<TPair<int32, int32>, FPlateBoundary>& BoundaryPair : Boundaries)
            {
                const TPair<int32, int32>& Key = BoundaryPair.Key;
                const FPlateBoundary& Boundary = BoundaryPair.Value;

                if (Key.Key != SourcePlate.PlateID && Key.Value != SourcePlate.PlateID)
                {
                    continue;
                }

                if (Boundary.SharedEdgeVertices.Num() > 0)
                {
                    const int32 RepresentativeVertex = Boundary.SharedEdgeVertices[0];
                    if (RenderVertices.IsValidIndex(RepresentativeVertex))
                    {
                        const double Distance = FVector3d::Distance(VertexPosition, RenderVertices[RepresentativeVertex]);
                        if (Distance < MinDistanceToBoundary)
                        {
                            MinDistanceToBoundary = Distance;
                            NearestBoundaryType = Boundary.BoundaryType;
                        }
                    }
                }

                if (Boundary.BoundaryType == EBoundaryType::Convergent)
                {
                    const FTectonicPlate* PlateA = PlateLookup.FindRef(Key.Key);
                    const FTectonicPlate* PlateB = PlateLookup.FindRef(Key.Value);
                    if (PlateA && PlateB && PlateA->CrustType != PlateB->CrustType)
                    {
                        bIsSubduction = true;
                    }
                }
            }
        }

        if (NearestBoundaryType != EBoundaryType::Convergent && BaseElevation < 500.0)
        {
            return EContinentalTerrainType::Plain;
        }

        if (OrogenyAge > 100.0)
        {
            return EContinentalTerrainType::OldMountains;
        }

        if (bIsSubduction)
        {
            return EContinentalTerrainType::AndeanMountains;
        }

        return EContinentalTerrainType::HimalayanMountains;
    };

    auto GetExemplarListForTerrain = [&](EContinentalTerrainType TerrainType) -> const TArray<int32>*
    {
        switch (TerrainType)
        {
        case EContinentalTerrainType::AndeanMountains:
            return &AndeanIndices;
        case EContinentalTerrainType::HimalayanMountains:
            return &HimalayanIndices;
        case EContinentalTerrainType::OldMountains:
        case EContinentalTerrainType::Plain:
        default:
            return &AncientIndices;
        }
    };

    const double PlanetRadius = Parameters.PlanetRadius;
    const bool bHasBoundaries = Boundaries.Num() > 0;

    constexpr uint32 MaxExemplarBlendCount = 3;
    const uint32 InvalidIndex = MAX_uint32;

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const FVector3d& VertexPosition = RenderVertices[VertexIdx];
        Cache.RenderPositions[VertexIdx] = FVector3f(VertexPosition);
        Cache.BaselineElevation[VertexIdx] = static_cast<float>(VertexAmplifiedElevation[VertexIdx]);

        uint32 PackedInfo = 0;
        FUintVector4 PackedIndices(InvalidIndex, InvalidIndex, InvalidIndex, InvalidIndex);
        FVector4f PackedWeights(0.0f, 0.0f, 0.0f, 0.0f);
        FVector2f RandomOffset(0.0f, 0.0f);
        FVector2d RandomOffsetDouble(0.0, 0.0);

        const int32 PlateID = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;
        const FTectonicPlate* PlatePtr = PlateLookup.FindRef(PlateID);

        const bool bIsContinental = PlatePtr && PlatePtr->CrustType == ECrustType::Continental;
        if (!bIsContinental)
        {
            Cache.PackedTerrainInfo[VertexIdx] = PackedInfo;
            Cache.ExemplarIndices[VertexIdx] = PackedIndices;
            Cache.ExemplarWeights[VertexIdx] = PackedWeights;
            Cache.RandomUVOffsets[VertexIdx] = RandomOffset;
            Cache.WrappedUVs[VertexIdx] = FVector2f::ZeroVector;
            continue;
        }

        ++LocalMetrics.ContinentalVertexCount;

        double ClassificationStart = 0.0;
        if (bCaptureMetrics)
        {
            ClassificationStart = FPlatformTime::Seconds();
        }

        const double BaseElevation = VertexAmplifiedElevation[VertexIdx];
        const double OrogenyAge = VertexCrustAge.IsValidIndex(VertexIdx) ? VertexCrustAge[VertexIdx] : 0.0;

        const FPlateBoundarySummary* BoundarySummary = bHasBoundaries
            ? GetSummaryForPlate(PlatePtr->PlateID)
            : nullptr;

        EContinentalTerrainType TerrainType = EContinentalTerrainType::Plain;

        if (bHasBoundaries)
        {
            TerrainType = DetermineTerrainType(*PlatePtr, VertexPosition, BaseElevation, OrogenyAge, BoundarySummary);
        }

        if (bCaptureMetrics)
        {
            LocalMetrics.ClassificationSeconds += FPlatformTime::Seconds() - ClassificationStart;
        }

        const TArray<int32>* ExemplarListPtr = GetExemplarListForTerrain(TerrainType);
        const TArray<int32>& ExemplarList = ExemplarListPtr ? *ExemplarListPtr : AncientIndices;

        const uint32 ExemplarCount = FMath::Min<uint32>(MaxExemplarBlendCount, static_cast<uint32>(ExemplarList.Num()));
        if (ExemplarCount > 0)
        {
            ++LocalMetrics.ExemplarAssignmentCount;

            double ExemplarStart = 0.0;
            if (bCaptureMetrics)
            {
                ExemplarStart = FPlatformTime::Seconds();
            }

            float TotalWeight = 0.0f;
            float Weights[MaxExemplarBlendCount] = { 0.0f, 0.0f, 0.0f };
            uint32 Indices[MaxExemplarBlendCount] = { InvalidIndex, InvalidIndex, InvalidIndex };

            for (uint32 ExemplarIdx = 0; ExemplarIdx < ExemplarCount; ++ExemplarIdx)
            {
                const int32 AtlasIndex = ExemplarList[ExemplarIdx];
                if (AtlasIndex < 0)
                {
                    continue;
                }

                const float Weight = 1.0f / static_cast<float>(ExemplarIdx + 1);
                Weights[ExemplarIdx] = Weight;
                Indices[ExemplarIdx] = static_cast<uint32>(AtlasIndex);
                TotalWeight += Weight;
            }

            PackedIndices = FUintVector4(Indices[0], Indices[1], Indices[2], InvalidIndex);
            PackedWeights = FVector4f(Weights[0], Weights[1], Weights[2], TotalWeight);

            RandomOffsetDouble = ComputeContinentalRandomOffset(VertexPosition, SimParams.Seed);
            RandomOffset = FVector2f(static_cast<float>(RandomOffsetDouble.X), static_cast<float>(RandomOffsetDouble.Y));

            PackedInfo = static_cast<uint32>(TerrainType) | (ExemplarCount << 8);

            if (bCaptureMetrics)
            {
                LocalMetrics.ExemplarSelectionSeconds += FPlatformTime::Seconds() - ExemplarStart;
            }
        }
        else
        {
            PackedInfo = static_cast<uint32>(TerrainType);
        }

        Cache.PackedTerrainInfo[VertexIdx] = PackedInfo;
        Cache.ExemplarIndices[VertexIdx] = PackedIndices;
        Cache.ExemplarWeights[VertexIdx] = PackedWeights;
        Cache.RandomUVOffsets[VertexIdx] = RandomOffset;

        const FVector3d NormalizedPos = VertexPosition.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
        FVector2d BaseUV(
            0.5 + (FMath::Atan2(NormalizedPos.Y, NormalizedPos.X) / TwoPi),
            0.5 - (FMath::Asin(NormalizedPos.Z) / PI));

        FVector2d LocalUV = BaseUV - FVector2d(0.5, 0.5);
        LocalUV += FVector2d(RandomOffsetDouble.X, RandomOffsetDouble.Y);

        FVector3d FoldDirection = FVector3d::ZeroVector;
        double FoldDistance = TNumericLimits<double>::Max();
        bool bHasFoldRotation = TryComputeFoldDirection(
            VertexPosition,
            PlatePtr->PlateID,
            Plates,
            Boundaries,
            BoundarySummary,
            FoldDirection,
            &FoldDistance);
        double FoldAngle = 0.0;

        constexpr double FoldAlignmentMaxRadians = 0.35;
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
        FinalUV.X = FMath::Frac(FinalUV.X);
        FinalUV.Y = FMath::Frac(FinalUV.Y);
        if (FinalUV.X < 0.0)
        {
            FinalUV.X += 1.0;
        }
        if (FinalUV.Y < 0.0)
        {
            FinalUV.Y += 1.0;
        }

        Cache.WrappedUVs[VertexIdx] = FVector2f(static_cast<float>(FinalUV.X), static_cast<float>(FinalUV.Y));

#if UE_BUILD_DEVELOPMENT
        static int32 DebugPackedLog = 0;
        const bool bLogVertex = (VertexIdx == 2 || VertexIdx == 9 || VertexIdx == 22 || VertexIdx == 25 || VertexIdx == 26 || VertexIdx == 154003);
        if ((DebugPackedLog < 5 || bLogVertex) && ((PackedInfo >> 8u) & 0xFFu) > 0u)
        {
            UE_LOG(LogPlanetaryCreation, Log,
                TEXT("[ContinentalGPUInputs] Vtx=%d Terrain=%u Count=%u Indices={%u,%u,%u} Weights={%.3f,%.3f,%.3f} Offset=(%.3f,%.3f) Baseline=%.2f"),
                VertexIdx,
                PackedInfo & 0xFFu,
                (PackedInfo >> 8u) & 0xFFu,
                PackedIndices.X,
                PackedIndices.Y,
                PackedIndices.Z,
                PackedWeights.X,
                PackedWeights.Y,
                PackedWeights.Z,
                RandomOffset.X,
                RandomOffset.Y,
                Cache.BaselineElevation[VertexIdx]);
            if (!bLogVertex)
            {
                ++DebugPackedLog;
            }
        }
#endif
    }

    if (bCaptureMetrics)
    {
        const double FunctionDuration = FPlatformTime::Seconds() - FunctionStart;
        LocalMetrics.TotalSeconds = FunctionDuration;
        LastContinentalCacheProfileMetrics = LocalMetrics;
        LastContinentalCacheBuildSeconds = FunctionDuration;
    }

    Cache.CachedDataSerial = OceanicAmplificationDataSerial;
    Cache.CachedTopologyVersion = TopologyVersion;
    Cache.CachedSurfaceVersion = SurfaceDataVersion;
}

void UTectonicSimulationService::BumpOceanicAmplificationSerial()
{
    ++OceanicAmplificationDataSerial;
    OceanicAmplificationFloatInputs.CachedDataSerial = 0;
    ContinentalAmplificationGPUInputs.CachedDataSerial = 0;
    ContinentalAmplificationCacheSerial = 0;
}

void UTectonicSimulationService::RefreshContinentalAmplificationCache() const
{
    const int32 VertexCount = VertexAmplifiedElevation.Num();
    if (VertexCount <= 0)
    {
        ContinentalAmplificationCacheEntries.Reset();
        ContinentalAmplificationCacheSerial = OceanicAmplificationDataSerial;
        ContinentalAmplificationCacheTopologyVersion = TopologyVersion;
        ContinentalAmplificationCacheSurfaceVersion = SurfaceDataVersion;
        return;
    }

    const bool bUpToDate =
        ContinentalAmplificationCacheSerial == OceanicAmplificationDataSerial &&
        ContinentalAmplificationCacheTopologyVersion == TopologyVersion &&
        ContinentalAmplificationCacheSurfaceVersion == SurfaceDataVersion &&
        ContinentalAmplificationCacheEntries.Num() == VertexCount;

    if (bUpToDate)
    {
        return;
    }

    const FContinentalAmplificationGPUInputs& GPUInputs = GetContinentalAmplificationGPUInputs();

    using namespace PlanetaryCreation::GPU;

    FExemplarTextureArray& ExemplarArray = GetExemplarTextureArray();
    if (!ExemplarArray.IsInitialized())
    {
        const_cast<UTectonicSimulationService*>(this)->InitializeGPUExemplarResources();
    }

    const TArray<FExemplarTextureArray::FExemplarInfo>& ExemplarInfo = ExemplarArray.GetExemplarInfo();

    auto ResolveLibraryIndex = [&](uint32 AtlasIndex) -> uint32
    {
        if (AtlasIndex == MAX_uint32)
        {
            return MAX_uint32;
        }

        const int32 AtlasIdx = static_cast<int32>(AtlasIndex);
        if (!ExemplarInfo.IsValidIndex(AtlasIdx))
        {
            return MAX_uint32;
        }

        const int32 LibraryIndex = ExemplarInfo[AtlasIdx].LibraryIndex;
        return LibraryIndex >= 0 ? static_cast<uint32>(LibraryIndex) : MAX_uint32;
    };

    ContinentalAmplificationCacheEntries.SetNum(VertexCount);

    const int32 PreviousBlendCacheCount = ContinentalAmplificationBlendCache.Num();
    ContinentalAmplificationBlendCache.SetNum(VertexCount);
    for (int32 BlendIdx = PreviousBlendCacheCount; BlendIdx < VertexCount; ++BlendIdx)
    {
        ContinentalAmplificationBlendCache[BlendIdx] = FContinentalBlendCache();
    }

    for (int32 Index = 0; Index < VertexCount; ++Index)
    {
        FContinentalAmplificationCacheEntry& Entry = ContinentalAmplificationCacheEntries[Index];
        Entry = FContinentalAmplificationCacheEntry();

        const uint32 PackedInfo = GPUInputs.PackedTerrainInfo.IsValidIndex(Index)
            ? GPUInputs.PackedTerrainInfo[Index]
            : 0u;

        Entry.TerrainType = static_cast<EContinentalTerrainType>(PackedInfo & 0xFFu);
        Entry.ExemplarCount = FMath::Min<uint32>((PackedInfo >> 8u) & 0xFFu, 3u);
        Entry.bHasCachedData = (Entry.ExemplarCount > 0);

        const FUintVector4 PackedIndices = GPUInputs.ExemplarIndices.IsValidIndex(Index)
            ? GPUInputs.ExemplarIndices[Index]
            : FUintVector4(MAX_uint32, MAX_uint32, MAX_uint32, MAX_uint32);

        Entry.ExemplarIndices[0] = ResolveLibraryIndex(PackedIndices.X);
        Entry.ExemplarIndices[1] = ResolveLibraryIndex(PackedIndices.Y);
        Entry.ExemplarIndices[2] = ResolveLibraryIndex(PackedIndices.Z);

        const FVector4f PackedWeights = GPUInputs.ExemplarWeights.IsValidIndex(Index)
            ? GPUInputs.ExemplarWeights[Index]
            : FVector4f(0.0f, 0.0f, 0.0f, 0.0f);

        Entry.Weights[0] = PackedWeights.X;
        Entry.Weights[1] = PackedWeights.Y;
        Entry.Weights[2] = PackedWeights.Z;
        Entry.TotalWeight = 0.0f;

        uint32 ValidMappings = 0;
        for (int32 SampleIdx = 0; SampleIdx < 3; ++SampleIdx)
        {
            if (Entry.ExemplarIndices[SampleIdx] == MAX_uint32)
            {
                Entry.Weights[SampleIdx] = 0.0f;
                continue;
            }

            ++ValidMappings;
            Entry.TotalWeight += Entry.Weights[SampleIdx];
        }
        Entry.ExemplarCount = FMath::Min<uint32>(Entry.ExemplarCount, ValidMappings);
        Entry.bHasCachedData = (Entry.ExemplarCount > 0);

        Entry.RandomOffset = GPUInputs.RandomUVOffsets.IsValidIndex(Index)
            ? GPUInputs.RandomUVOffsets[Index]
            : FVector2f::ZeroVector;

        Entry.WrappedUV = GPUInputs.WrappedUVs.IsValidIndex(Index)
            ? GPUInputs.WrappedUVs[Index]
            : FVector2f::ZeroVector;
    }

    ContinentalAmplificationCacheSerial = OceanicAmplificationDataSerial;
    ContinentalAmplificationCacheTopologyVersion = TopologyVersion;
    ContinentalAmplificationCacheSurfaceVersion = SurfaceDataVersion;
}

double UTectonicSimulationService::ComputeContinentalAmplificationFromCache(
    int32 VertexIdx,
    const FVector3d& Position,
    double BaseElevation_m,
    const FContinentalAmplificationCacheEntry& CacheEntry,
    const FString& ProjectContentDir,
    int32 Seed)
{
    double AmplifiedElevation = BaseElevation_m;

    if (!CacheEntry.bHasCachedData || CacheEntry.ExemplarCount == 0)
    {
        return AmplifiedElevation;
    }

    FContinentalBlendCache* BlendCacheEntry = ContinentalAmplificationBlendCache.IsValidIndex(VertexIdx)
        ? &ContinentalAmplificationBlendCache[VertexIdx]
        : nullptr;

#if UE_BUILD_DEVELOPMENT
    FContinentalAmplificationDebugInfo* DebugInfo = GetContinentalAmplificationDebugInfoPtr();
    if (DebugInfo)
    {
        DebugInfo->TerrainType = CacheEntry.TerrainType;
        DebugInfo->VertexIndex = VertexIdx;
        DebugInfo->ExemplarCount = CacheEntry.ExemplarCount;
        DebugInfo->RandomOffsetU = CacheEntry.RandomOffset.X;
        DebugInfo->RandomOffsetV = CacheEntry.RandomOffset.Y;
        DebugInfo->RandomSeed = Seed + static_cast<int32>(Position.X * 1000.0 + Position.Y * 1000.0);
        for (int32 DebugIdx = 0; DebugIdx < 3; ++DebugIdx)
        {
            DebugInfo->ExemplarIndices[DebugIdx] = MAX_uint32;
            DebugInfo->SampleHeights[DebugIdx] = 0.0;
            DebugInfo->Weights[DebugIdx] = 0.0;
        }
    }
#else
    FContinentalAmplificationDebugInfo* DebugInfo = nullptr;
#endif

    const double WrappedU = FMath::Frac(static_cast<double>(CacheEntry.WrappedUV.X));
    const double WrappedV = FMath::Frac(static_cast<double>(CacheEntry.WrappedUV.Y));

    double BlendedHeight = 0.0;
    double TotalWeight = 0.0;
    double ReferenceMean = 0.0;
    bool bHasReferenceMean = false;

    const uint64 CurrentCacheSerial = ContinentalAmplificationCacheSerial;
    const bool bDebugRequested = (DebugInfo != nullptr);
    const bool bBlendCacheValid = BlendCacheEntry && BlendCacheEntry->CachedSerial == CurrentCacheSerial && !bDebugRequested;

    if (bBlendCacheValid)
    {
        BlendedHeight = static_cast<double>(BlendCacheEntry->BlendedHeight);
        ReferenceMean = static_cast<double>(BlendCacheEntry->ReferenceMean);
        bHasReferenceMean = BlendCacheEntry->bHasReferenceMean;
        TotalWeight = static_cast<double>(CacheEntry.TotalWeight);
    }
    else
    {
        if (!IsExemplarLibraryLoaded())
        {
            if (!LoadExemplarLibraryJSON(ProjectContentDir))
            {
                UE_LOG(LogPlanetaryCreation, Error, TEXT("Failed to load exemplar library, skipping continental amplification"));
                return AmplifiedElevation;
            }
        }

        double WeightedSum = 0.0;

        for (uint32 SampleIdx = 0; SampleIdx < CacheEntry.ExemplarCount; ++SampleIdx)
        {
            const uint32 LibraryIndex = CacheEntry.ExemplarIndices[SampleIdx];
            if (LibraryIndex == MAX_uint32)
            {
                continue;
            }

            FExemplarMetadata* Exemplar = AccessExemplarMetadata(static_cast<int32>(LibraryIndex));
            if (!Exemplar)
            {
                continue;
            }

            if (!Exemplar->bDataLoaded && !LoadExemplarHeightData(*Exemplar, ProjectContentDir))
            {
                continue;
            }

            const double Weight = static_cast<double>(CacheEntry.Weights[SampleIdx]);
            if (Weight <= 0.0)
            {
                continue;
            }

            const double SampledHeight = SampleExemplarHeight(*Exemplar, WrappedU, WrappedV);
            WeightedSum += SampledHeight * Weight;
            TotalWeight += Weight;

#if UE_BUILD_DEVELOPMENT
            if (DebugInfo)
            {
                DebugInfo->ExemplarIndices[SampleIdx] = LibraryIndex;
                DebugInfo->SampleHeights[SampleIdx] = SampledHeight;
                DebugInfo->Weights[SampleIdx] = Weight;
            }
#endif
        }

        if (TotalWeight > 0.0)
        {
            BlendedHeight = WeightedSum / TotalWeight;
        }

        if (CacheEntry.ExemplarCount > 0)
        {
            const uint32 ReferenceIndex = CacheEntry.ExemplarIndices[0];
            if (ReferenceIndex != MAX_uint32)
            {
                const FExemplarMetadata* RefExemplar = AccessExemplarMetadataConst(static_cast<int32>(ReferenceIndex));
                if (RefExemplar)
                {
                    ReferenceMean = RefExemplar->ElevationMean_m;
                    bHasReferenceMean = true;
                }
            }
        }

        if (BlendCacheEntry)
        {
            BlendCacheEntry->BlendedHeight = static_cast<float>(BlendedHeight);
            BlendCacheEntry->ReferenceMean = static_cast<float>(ReferenceMean);
            BlendCacheEntry->CachedSerial = CurrentCacheSerial;
            BlendCacheEntry->bHasReferenceMean = bHasReferenceMean;
        }
    }

    if (bHasReferenceMean)
    {
        const double DetailScale = (BaseElevation_m > 1000.0)
            ? (ReferenceMean != 0.0 ? (BaseElevation_m / ReferenceMean) : 0.0)
            : 0.5;
        const double Detail = (BlendedHeight - ReferenceMean) * DetailScale;
        AmplifiedElevation += Detail;
    }

#if UE_BUILD_DEVELOPMENT
    if (DebugInfo)
    {
        DebugInfo->TotalWeight = TotalWeight;
        DebugInfo->BlendedHeight = BlendedHeight;
        DebugInfo->CpuResult = AmplifiedElevation;
        DebugInfo->UValue = static_cast<double>(CacheEntry.WrappedUV.X);
        DebugInfo->VValue = static_cast<double>(CacheEntry.WrappedUV.Y);
        DebugInfo->ReferenceMean = ReferenceMean;
    }
#endif

    return AmplifiedElevation;
}
#if UE_BUILD_DEVELOPMENT
void UTectonicSimulationService::RunTerraneMeshSurgerySpike()
{
    UE_LOG(LogPlanetaryCreation, Display, TEXT("[TerraneSpike] Running mesh surgery spike"));

    // Ensure we are at least Level 3 for meaningful test data.
    if (Parameters.RenderSubdivisionLevel < 3)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[TerraneSpike] RenderSubdivisionLevel < 3 (current: %d); regenerating Level 3 mesh"),
            Parameters.RenderSubdivisionLevel);

        FTectonicSimulationParameters TempParams = Parameters;
        TempParams.RenderSubdivisionLevel = 3;
        TempParams.bEnableDynamicRetessellation = false;
        SetParameters(TempParams);
        GenerateRenderMesh();
        BuildVoronoiMapping();
    }

    if (RenderVertices.Num() == 0 || RenderTriangles.Num() == 0)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("[TerraneSpike] Render mesh not initialized"));
        return;
    }

    const double StartTime = FPlatformTime::Seconds();

    // Step 1: Choose a seed face and gather neighbouring vertices to form a candidate terrane region.
    const int32 SeedTriangleIndex = RenderTriangles.Num() > 0 ? 0 : INDEX_NONE;
    if (SeedTriangleIndex == INDEX_NONE)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("[TerraneSpike] No triangles available"));
        return;
    }

    TSet<int32> CandidateVertices;
    TQueue<int32> Frontier;

    auto EnqueueVertex = [&](int32 VertexIdx)
    {
        if (VertexIdx >= 0 && VertexIdx < RenderVertices.Num() && !CandidateVertices.Contains(VertexIdx))
        {
            CandidateVertices.Add(VertexIdx);
            Frontier.Enqueue(VertexIdx);
        }
    };

    // Seed with first triangle (3 vertices)
    EnqueueVertex(RenderTriangles[0]);
    EnqueueVertex(RenderTriangles[1]);
    EnqueueVertex(RenderTriangles[2]);

    // Expand to ~100 vertices using adjacency from RenderTriangles
    TMap<int32, TArray<int32>> VertexAdjacency;
    VertexAdjacency.Reserve(RenderVertices.Num());

    for (int32 Tri = 0; Tri < RenderTriangles.Num(); Tri += 3)
    {
        const int32 A = RenderTriangles[Tri];
        const int32 B = RenderTriangles[Tri + 1];
        const int32 C = RenderTriangles[Tri + 2];

        VertexAdjacency.FindOrAdd(A).Add(B);
        VertexAdjacency.FindOrAdd(A).Add(C);
        VertexAdjacency.FindOrAdd(B).Add(A);
        VertexAdjacency.FindOrAdd(B).Add(C);
        VertexAdjacency.FindOrAdd(C).Add(A);
        VertexAdjacency.FindOrAdd(C).Add(B);
    }

    while (!Frontier.IsEmpty() && CandidateVertices.Num() < 100)
    {
        int32 CurrentVertex;
        Frontier.Dequeue(CurrentVertex);

        const TArray<int32>* Neighbors = VertexAdjacency.Find(CurrentVertex);
        if (!Neighbors)
        {
            continue;
        }

        for (int32 Neighbor : *Neighbors)
        {
            EnqueueVertex(Neighbor);
        }
    }

    UE_LOG(LogPlanetaryCreation, Display, TEXT("[TerraneSpike] Selected %d candidate vertices"), CandidateVertices.Num());

    if (CandidateVertices.Num() < 10)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[TerraneSpike] Not enough vertices to run surgery"));
        return;
    }

    // Convert to array for processing
    TArray<int32> CandidateVertexArray = CandidateVertices.Array();

    // Step 2: Build boundary ring using simple adjacency (find edges with exactly one triangle in the region)
    TSet<TPair<int32, int32>> BoundaryEdges;
    for (int32 Tri = 0; Tri < RenderTriangles.Num(); Tri += 3)
    {
        const int32 A = RenderTriangles[Tri];
        const int32 B = RenderTriangles[Tri + 1];
        const int32 C = RenderTriangles[Tri + 2];

        const bool bAIn = CandidateVertices.Contains(A);
        const bool bBIn = CandidateVertices.Contains(B);
        const bool bCIn = CandidateVertices.Contains(C);

        const int32 InCount = (bAIn ? 1 : 0) + (bBIn ? 1 : 0) + (bCIn ? 1 : 0);
        if (InCount == 0)
        {
            continue;
        }

        auto MarkBoundary = [&](int32 V0, int32 V1)
        {
            if ((CandidateVertices.Contains(V0) && !CandidateVertices.Contains(V1)) ||
                (CandidateVertices.Contains(V1) && !CandidateVertices.Contains(V0)))
            {
                BoundaryEdges.Add(MakeTuple(FMath::Min(V0, V1), FMath::Max(V0, V1)));
            }
        };

        MarkBoundary(A, B);
        MarkBoundary(B, C);
        MarkBoundary(C, A);
    }

    UE_LOG(LogPlanetaryCreation, Display, TEXT("[TerraneSpike] Detected %d boundary edges"), BoundaryEdges.Num());

    // Step 3: Clone vertex/triangle data for terrane region
    struct FTerraneMesh
    {
        TArray<FVector3d> Vertices;
        TArray<int32> Indices;
    };

    FTerraneMesh TerraneMesh;
    TerraneMesh.Vertices.SetNum(CandidateVertexArray.Num());

    TMap<int32, int32> GlobalToLocal;
    for (int32 LocalIdx = 0; LocalIdx < CandidateVertexArray.Num(); ++LocalIdx)
    {
        const int32 GlobalIdx = CandidateVertexArray[LocalIdx];
        GlobalToLocal.Add(GlobalIdx, LocalIdx);
        TerraneMesh.Vertices[LocalIdx] = RenderVertices[GlobalIdx];
    }

    for (int32 Tri = 0; Tri < RenderTriangles.Num(); Tri += 3)
    {
        const int32 A = RenderTriangles[Tri];
        const int32 B = RenderTriangles[Tri + 1];
        const int32 C = RenderTriangles[Tri + 2];
        if (GlobalToLocal.Contains(A) && GlobalToLocal.Contains(B) && GlobalToLocal.Contains(C))
        {
            TerraneMesh.Indices.Add(GlobalToLocal[A]);
            TerraneMesh.Indices.Add(GlobalToLocal[B]);
            TerraneMesh.Indices.Add(GlobalToLocal[C]);
        }
    }

    UE_LOG(LogPlanetaryCreation, Display, TEXT("[TerraneSpike] Terrane mesh: %d verts, %d tris"),
        TerraneMesh.Vertices.Num(), TerraneMesh.Indices.Num() / 3);

    // Step 4: Simple validation using existing topology validator (on terrane mesh)
    auto ValidateMesh = [&](const TArray<FVector3d>& MeshVerts, const TArray<int32>& MeshIndices, const TCHAR* Label)
    {
        TSet<TPair<int32, int32>> UniqueEdges;
        TMap<TPair<int32, int32>, int32> EdgeCounts;
        const int32 V = MeshVerts.Num();
        const int32 F = MeshIndices.Num() / 3;

        for (int32 Tri = 0; Tri < MeshIndices.Num(); Tri += 3)
        {
            const int32 V0 = MeshIndices[Tri];
            const int32 V1 = MeshIndices[Tri + 1];
            const int32 V2 = MeshIndices[Tri + 2];

            auto AddEdge = [&](int32 X, int32 Y)
            {
                const TPair<int32, int32> Edge(FMath::Min(X, Y), FMath::Max(X, Y));
                UniqueEdges.Add(Edge);
                EdgeCounts.FindOrAdd(Edge)++;
            };

            AddEdge(V0, V1);
            AddEdge(V1, V2);
            AddEdge(V2, V0);
        }

        const int32 E = UniqueEdges.Num();
        const int32 Euler = V - E + F;
        UE_LOG(LogPlanetaryCreation, Display, TEXT("[TerraneSpike][%s] Euler characteristic: %d (V=%d, E=%d, F=%d)"), Label, Euler, V, E, F);

        int32 NonManifold = 0;
        for (const auto& Pair : EdgeCounts)
        {
            if (Pair.Value != 2)
            {
                NonManifold++;
            }
        }

        UE_LOG(LogPlanetaryCreation, Display, TEXT("[TerraneSpike][%s] Non-manifold edges: %d"), Label, NonManifold);
    };

    ValidateMesh(TerraneMesh.Vertices, TerraneMesh.Indices, TEXT("Terrane"));

    // Step 5: Report timing
    const double EndTime = FPlatformTime::Seconds();
    UE_LOG(LogPlanetaryCreation, Display, TEXT("[TerraneSpike] Completed spike in %.2f ms"), (EndTime - StartTime) * 1000.0);
}
#endif
