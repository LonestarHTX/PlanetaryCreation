#pragma once

#include "CoreMinimal.h"
#include "PlanetaryCreationLogging.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "Containers/BitArray.h"
#include "TectonicSimulationService.generated.h"

UENUM(BlueprintType)
enum class ETectonicVisualizationMode : uint8
{
    PlateColors UMETA(DisplayName = "Plate Colors"),
    Elevation UMETA(DisplayName = "Elevation Heatmap"),
    Velocity UMETA(DisplayName = "Velocity Field"),
    Stress UMETA(DisplayName = "Stress Gradient")
};

struct FStageBProfile
{
    double BaselineMs = 0.0;
    double RidgeMs = 0.0;
    double OceanicCPUMs = 0.0;
    double OceanicGPUMs = 0.0;
    double ContinentalCPUMs = 0.0;
    double ContinentalGPUMs = 0.0;
    double GpuReadbackMs = 0.0;
    double CacheInvalidationMs = 0.0;

    double TotalMs() const
    {
        return BaselineMs + RidgeMs + OceanicCPUMs + OceanicGPUMs + ContinentalCPUMs + ContinentalGPUMs + GpuReadbackMs + CacheInvalidationMs;
    }
};

/**
 * Paper-compliant elevation constants (Appendix A).
 * Reference: "Procedural Tectonic Planets" paper, Table in Appendix A.
 * Sea level is 0m (reference elevation).
 */
namespace PaperElevationConstants
{
    /** Oceanic ridge elevation at divergent boundaries (zᵀ in paper). */
    constexpr double OceanicRidgeDepth_m = -1000.0;

    /** Abyssal plains elevation for mature oceanic crust (zᵇ in paper). */
    constexpr double AbyssalPlainDepth_m = -6000.0;

    /** Continental baseline elevation (implied by paper, starts at sea level). */
    constexpr double ContinentalBaseline_m = 0.0;

    /** Sea level reference (explicitly stated in Appendix A). */
    constexpr double SeaLevel_m = 0.0;
}

/** Crust type for a tectonic plate (from paper). */
UENUM()
enum class ECrustType : uint8
{
    Oceanic,
    Continental
};

/** Represents a single tectonic plate with double-precision state. */
USTRUCT()
struct FTectonicPlate
{
    GENERATED_BODY()

    /** Unique plate identifier. */
    UPROPERTY()
    int32 PlateID = INDEX_NONE;

    /** Centroid position on the unit sphere (double-precision). */
    FVector3d Centroid = FVector3d::ZeroVector;

    /** Euler pole axis (normalized) for rotation. */
    FVector3d EulerPoleAxis = FVector3d::ZAxisVector;

    /** Angular velocity around Euler pole (radians per My). */
    double AngularVelocity = 0.0;

    /** Crust type (oceanic vs continental). */
    UPROPERTY()
    ECrustType CrustType = ECrustType::Oceanic;

    /** Static crust thickness in km (deferred: dynamic updates in Milestone 3). */
    double CrustThickness = 7.0; // Default oceanic crust ~7km

    /** Indices of vertices forming this plate's polygon (into shared vertex array). */
    TArray<int32> VertexIndices;
};

/** Boundary classification based on relative velocity (from paper Section 3). */
UENUM()
enum class EBoundaryType : uint8
{
    Divergent,   // Ridge - plates separating
    Convergent,  // Subduction zone - plates colliding
    Transform    // Shear - plates sliding past
};

/** Milestone 4 Task 1.3: Boundary lifecycle states (paper Section 4.1). */
UENUM()
enum class EBoundaryState : uint8
{
    Nascent,    // Recently formed, low stress
    Active,     // Actively accumulating stress/spreading
    Dormant,    // Low velocity, stress decaying
    Rifting     // Milestone 4 Task 2.2: Active rift formation (divergent only)
};

/** Milestone 4 Task 1.2: Plate topology event types. */
UENUM()
enum class EPlateTopologyEventType : uint8
{
    Split,      // Plate split along rift
    Merge,      // Plate consumed by subduction
    None
};

/** Boundary metadata between two plates. */
USTRUCT()
struct FPlateBoundary
{
    GENERATED_BODY()

    /** Shared edge vertex indices (2 vertices for icosphere edge). */
    TArray<int32> SharedEdgeVertices;

    /** Current boundary classification (updated each step). */
    UPROPERTY()
    EBoundaryType BoundaryType = EBoundaryType::Transform;

    /** Relative velocity magnitude at boundary (for logging/debug). */
    double RelativeVelocity = 0.0;

    /**
     * Milestone 3 Task 2.3: Accumulated stress at boundary (MPa, double precision).
     * COSMETIC VISUALIZATION ONLY - simplified model, not physically accurate.
     * - Convergent boundaries: accumulates stress (capped at 100 MPa)
     * - Divergent boundaries: decays toward zero (τ = 10 My)
     * - Transform boundaries: minimal accumulation
     */
    double AccumulatedStress = 0.0;

    /**
     * Milestone 4 Task 1.3: Boundary lifecycle state.
     * Tracks boundary evolution (Nascent → Active → Dormant).
     */
    UPROPERTY()
    EBoundaryState BoundaryState = EBoundaryState::Nascent;

    /**
     * Milestone 4 Task 1.3: Simulation time when boundary entered current state (My).
     */
    double StateTransitionTimeMy = 0.0;

    /**
     * Milestone 4 Task 1.2: Time (My) that boundary has been divergent (for rift split detection).
     * Reset to 0 when boundary type changes.
     */
    double DivergentDurationMy = 0.0;

    /**
     * Milestone 4 Task 1.2: Time (My) that boundary has been convergent (for merge detection).
     * Reset to 0 when boundary type changes.
     */
    double ConvergentDurationMy = 0.0;

    /**
     * Milestone 4 Task 2.2: Rift width (meters) for rifting divergent boundaries.
     * Incremented over time based on divergence rate. Triggers split when threshold exceeded.
     */
    double RiftWidthMeters = 0.0;

    /**
     * Milestone 4 Task 2.2: Rift formation time (My) when boundary entered rifting state.
     * Used to track rift age for visualization/analytics.
     */
    double RiftFormationTimeMy = 0.0;
};

/** Milestone 4 Task 1.2: Records a plate topology change event for logging/CSV export. */
USTRUCT()
struct FPlateTopologyEvent
{
    GENERATED_BODY()

    UPROPERTY()
    EPlateTopologyEventType EventType = EPlateTopologyEventType::None;

    /** Plate IDs involved (for split: [OriginalID, NewID], for merge: [ConsumedID, SurvivorID]). */
    UPROPERTY()
    TArray<int32> PlateIDs;

    /** Simulation time when event occurred (My). */
    double TimestampMy = 0.0;

    /** Boundary stress at time of event (MPa, for validation). */
    double StressAtEvent = 0.0;

    /** Relative velocity at time of event (rad/My, for validation). */
    double VelocityAtEvent = 0.0;
};

/** Milestone 4 Task 2.1: Hotspot type classification (paper Section 4.4). */
UENUM()
enum class EHotspotType : uint8
{
    Major,      // Large, long-lived plumes (e.g., Hawaii, Iceland)
    Minor       // Smaller, shorter-lived plumes
};

/** Milestone 4 Task 2.1: Mantle hotspot/plume representation. */
USTRUCT()
struct FMantleHotspot
{
    GENERATED_BODY()

    /** Unique hotspot identifier. */
    int32 HotspotID = INDEX_NONE;

    /** Position on unit sphere in mantle reference frame (drifts independently of plates). */
    FVector3d Position = FVector3d::ZeroVector;

    /** Hotspot type (major vs minor, affects thermal output and lifetime). */
    UPROPERTY()
    EHotspotType Type = EHotspotType::Minor;

    /** Thermal output (arbitrary units, affects stress/elevation contribution). */
    double ThermalOutput = 1.0;

    /** Influence radius (radians) for thermal contribution falloff. */
    double InfluenceRadius = 0.1; // ~5.7° on unit sphere

    /** Drift velocity in mantle frame (rad/My), allows hotspots to migrate over time. */
    FVector3d DriftVelocity = FVector3d::ZeroVector;
};

/** Milestone 6 Task 1.1: Terrane lifecycle states (paper Section 6). */
UENUM()
enum class ETerraneState : uint8
{
    Attached,       // Part of continental plate (normal)
    Extracted,      // Surgically removed, awaiting carrier assignment
    Transporting,   // Riding oceanic carrier plate toward collision
    Colliding       // At convergent boundary, ready for reattachment
};

/** Per-vertex payload stored for detached terranes. */
USTRUCT()
struct FTerraneVertexRecord
{
    GENERATED_BODY()

    UPROPERTY()
    FVector3d Position = FVector3d::ZeroVector;

    UPROPERTY()
    FVector3d Velocity = FVector3d::ZeroVector;

    UPROPERTY()
    FVector3d RidgeDirection = FVector3d::ZeroVector;

    UPROPERTY()
    double Stress = 0.0;

    UPROPERTY()
    double Temperature = 0.0;

    UPROPERTY()
    double Elevation = 0.0;

    UPROPERTY()
    double ErosionRate = 0.0;

    UPROPERTY()
    double SedimentThickness = 0.0;

    UPROPERTY()
    double CrustAge = 0.0;

    UPROPERTY()
    double AmplifiedElevation = 0.0;

    UPROPERTY()
    int32 PlateID = INDEX_NONE;

    /** Duplicate vertex index injected into render mesh when terrane is extracted (INDEX_NONE for interior vertices). */
    UPROPERTY()
    int32 ReplacementVertexIndex = INDEX_NONE;
};

/** Milestone 6 Task 1.1: Continental terrane (accreted microcontinent fragment). */
USTRUCT()
struct FContinentalTerrane
{
    GENERATED_BODY()

    /** Unique terrane identifier (deterministic from seed for replay/determinism). */
    int32 TerraneID = INDEX_NONE;

    /** Current lifecycle state (Attached/Extracted/Transporting/Colliding). */
    UPROPERTY()
    ETerraneState State = ETerraneState::Attached;

    /** Original render vertex indices comprising this terrane (for diagnostics/snapshots). */
    UPROPERTY()
    TArray<int32> OriginalVertexIndices;

    /** Detached vertex payload retained while terrane is extracted. */
    UPROPERTY()
    TArray<FTerraneVertexRecord> VertexPayload;

    /** Source plate ID (where terrane was extracted from, INDEX_NONE if not yet extracted). */
    int32 SourcePlateID = INDEX_NONE;

    /** Carrier plate ID (oceanic plate transporting terrane, INDEX_NONE if attached/extracted). */
    int32 CarrierPlateID = INDEX_NONE;

    /** Target plate ID for reattachment (continental plate at collision, INDEX_NONE if not colliding). */
    int32 TargetPlateID = INDEX_NONE;

    /** Centroid position on unit sphere (for tracking/visualization). */
    FVector3d Centroid = FVector3d::ZeroVector;

    /** Area in km² (for validation, prevents single-vertex terranes). */
    double AreaKm2 = 0.0;

    /** Extraction timestamp (My) for tracking terrane age/transport duration. */
    double ExtractionTimeMy = 0.0;

    /** Reattachment timestamp (My) for suturing/collision tracking. */
    double ReattachmentTimeMy = 0.0;

    /** Triangles removed from the base mesh during extraction (triplets of local vertex indices into VertexPayload). */
    UPROPERTY()
    TArray<int32> ExtractedTriangles;

    /** Global vertex indices generated to cap the extraction hole (duplicates + optional centers). */
    UPROPERTY()
    TArray<int32> PatchVertexIndices;

    /** Triangles added to cap the extraction hole (triplets referencing PatchVertexIndices). */
    UPROPERTY()
    TArray<int32> PatchTriangles;
};

/** Simulation parameters (Phase 3 - UI integration). */
USTRUCT()
struct FTectonicSimulationParameters
{
    GENERATED_BODY()

    /** Random seed for deterministic plate generation. */
    UPROPERTY()
    int32 Seed = 42;

    /**
     * Plate subdivision level (0-3). Controls number of tectonic plates generated from icosahedron:
     * Level 0: 20 plates (baseline from paper, ~Earth's 7-15 major/minor plates)
     * Level 1: 80 plates (experimental high-resolution mode)
     * Level 2: 320 plates (experimental ultra-high resolution)
     * Level 3: 1280 plates (experimental maximum resolution)
     * Default: 0 (20 plates, aligns with Milestone 2 target of ~12-20 plates)
     */
    UPROPERTY()
    int32 SubdivisionLevel = 0;

    /** Render mesh subdivision level (0-6). Level 0=20, 1=80, 2=320, 3=1280, 4=5120, 5=20480, 6=81920 faces. */
    UPROPERTY()
    int32 RenderSubdivisionLevel = 0;

    /**
     * Milestone 3 Task 2.4: Elevation displacement scale.
     * Controls magnitude of geometric displacement from stress field.
     * 1.0 = realistic scale (100 MPa → ~10km elevation), 0.0 = flat (color only).
     */
    UPROPERTY()
    double ElevationScale = 1.0;

    /**
     * Milestone 5 Phase 3: Planet radius in meters.
     * Controls the physical size of the simulated planet for realistic geodesic calculations.
     *
     * Default 127,400 m (1/50 Earth scale):
     * - Full Earth: 6,370,000 m (too large for initial testing/profiling)
     * - 1/50 scale: 127,400 m (realistic tectonic features, manageable render distances)
     *
     * IMPORTANT: This value is embedded in history snapshots and CSV exports.
     * Changing it mid-simulation invalidates deterministic fingerprints.
     * Valid range: 10,000 m to 10,000,000 m (smaller than Jupiter, larger than asteroids).
     */
    UPROPERTY()
    double PlanetRadius = 127400.0;

    /**
     * Milestone 3 Task 3.1: Lloyd relaxation iterations.
     * Number of iterations to evenly distribute plate centroids (0-10).
     * Default 8 typically achieves convergence. 0 = disabled.
     */
    UPROPERTY()
    int32 LloydIterations = 8;

    /**
     * Milestone 3 Task 3.3: Dynamic re-tessellation threshold (degrees).
     * When plate centroid drifts >N degrees from initial position, log warning.
     * In Milestone 6+, this also serves as the cooldown threshold that must be
     * re-attained before another rebuild is permitted (hysteresis lower bound).
     */
    UPROPERTY()
    double RetessellationThresholdDegrees = 30.0;

    /** Number of steps between drift/quality evaluations (≥1). */
    UPROPERTY()
    int32 RetessellationCheckIntervalSteps = 5;

    /** Number of steps between Voronoi refreshes (≥1). */
    UPROPERTY()
    int32 VoronoiRefreshIntervalSteps = 5;

    /** High watermark (degrees) before a rebuild is allowed (≥ threshold). */
    UPROPERTY()
    double RetessellationTriggerDegrees = 45.0;

    /** Minimum acceptable interior angle (degrees) before a triangle is flagged. */
    UPROPERTY()
    double RetessellationMinTriangleAngleDegrees = 15.0;

    /** Fraction of flagged triangles that will trigger a rebuild (0–1). */
    UPROPERTY()
    double RetessellationBadTriangleRatioThreshold = 0.02;

    /**
     * Milestone 4 Task 1.1: Enable dynamic re-tessellation.
     * When true, triggers mesh rebuild when plates drift beyond RetessellationThresholdDegrees.
     * Default true for M4+ (change to false to disable and use M3 logging behavior).
     */
    UPROPERTY()
    bool bEnableDynamicRetessellation = true;

    /**
     * Milestone 4 Phase 4.1: Enable automatic LOD based on camera distance.
     * When true, render subdivision level automatically adjusts based on viewport camera distance.
     * When false, manual render subdivision level setting is respected.
     * Default true for normal usage, false to force specific LOD for debugging/screenshots.
     */
    UPROPERTY()
    bool bEnableAutomaticLOD = true;

    /** Mantle viscosity coefficient (placeholder - used in Milestone 3). */
    UPROPERTY()
    double MantleViscosity = 1.0;

    /** Thermal diffusion constant (placeholder - used in Milestone 3). */
    UPROPERTY()
    double ThermalDiffusion = 1.0;

    /** Visualization overlay applied to preview mesh. */
    UPROPERTY()
    ETectonicVisualizationMode VisualizationMode = ETectonicVisualizationMode::PlateColors;

    /**
     * Milestone 4 Task 1.2: Plate split velocity threshold (rad/My).
     * Divergent boundaries exceeding this velocity for sustained duration trigger rifting/split.
     * Default 0.05 rad/My ≈ 3-5 cm/yr on Earth scale (realistic mid-ocean ridge spreading rate).
     */
    UPROPERTY()
    double SplitVelocityThreshold = 0.05;

    /**
     * Milestone 4 Task 1.2: Sustained divergence duration required to trigger split (My).
     * Prevents transient velocity spikes from causing spurious splits.
     * Default 20 My (paper-aligned, ~1 Wilson cycle phase).
     */
    UPROPERTY()
    double SplitDurationThreshold = 20.0;

    /**
     * Milestone 4 Task 1.2: Plate merge stress threshold (MPa).
     * Convergent boundaries exceeding this stress trigger subduction/merge if plate is small enough.
     * Default 80 MPa (80% of max stress cap, indicates sustained collision).
     */
    UPROPERTY()
    double MergeStressThreshold = 80.0;

    /**
     * Milestone 4 Task 1.2: Plate area ratio threshold for merge eligibility.
     * Smaller plate must be <N% of larger plate's area to be consumed.
     * Default 0.25 (smaller plate must be <25% of larger, prevents balanced collision merges).
     */
    UPROPERTY()
    double MergeAreaRatioThreshold = 0.25;

    /**
     * Milestone 4 Task 1.2: Enable plate split/merge topology changes.
     * Default false for backward compatibility. Set true to activate split/merge detection.
     */
    UPROPERTY()
    bool bEnablePlateTopologyChanges = false;

    /**
     * Milestone 4 Task 2.1: Number of major hotspots to generate (paper Section 4.4).
     * Major hotspots have higher thermal output and longer lifetimes.
     * Default 3 (paper recommendation for Earth-like planets).
     */
    UPROPERTY()
    int32 MajorHotspotCount = 3;

    /**
     * Milestone 4 Task 2.1: Number of minor hotspots to generate.
     * Minor hotspots have lower thermal output and shorter lifetimes.
     * Default 5 (paper recommendation for Earth-like planets).
     */
    UPROPERTY()
    int32 MinorHotspotCount = 5;

    /**
     * Milestone 4 Task 2.1: Hotspot drift speed in mantle frame (rad/My).
     * Controls how fast hotspots migrate over time. 0 = stationary.
     * Default 0.01 rad/My (~0.6 cm/yr on Earth scale, realistic mantle plume drift).
     */
    UPROPERTY()
    double HotspotDriftSpeed = 0.01;

    /**
     * Milestone 4 Task 5.0: Enable Voronoi distance warping with noise.
     * "More irregular continent shapes can be obtained by warping the geodesic distances
     * to the centroids using a simple noise function." (Paper Section 3)
     * When true, applies 3D noise to distance calculations in Voronoi mapping.
     * Default true for irregular plate shapes.
     */
    UPROPERTY()
    bool bEnableVoronoiWarping = true;

    /**
     * Milestone 4 Task 5.0: Voronoi warping noise amplitude.
     * Controls how much noise distorts plate boundaries (as fraction of distance).
     * 0.0 = perfect Voronoi cells (uniform), 0.5 = moderate irregularity (realistic continents).
     * Default 0.5 (50% distance variation, paper-aligned for irregular continent shapes).
     */
    UPROPERTY()
    double VoronoiWarpingAmplitude = 0.5;

    /**
     * Milestone 4 Task 5.0: Voronoi warping noise frequency.
     * Controls noise scale/detail for boundary distortion.
     * Higher values = finer boundary details, lower values = smoother curves.
     * Default 2.0 (medium-scale continental irregularities).
     */
    UPROPERTY()
    double VoronoiWarpingFrequency = 2.0;

    /**
     * Milestone 4 Task 2.1: Thermal output multiplier for major hotspots.
     * Scales thermal contribution to stress/elevation fields.
     * Default 2.0 (major hotspots are 2x more powerful than minor).
     */
    UPROPERTY()
    double MajorHotspotThermalOutput = 2.0;

    /**
     * Milestone 4 Task 2.1: Thermal output multiplier for minor hotspots.
     * Default 1.0 (baseline thermal contribution).
     */
    UPROPERTY()
    double MinorHotspotThermalOutput = 1.0;

    /**
     * Milestone 4 Task 2.1: Enable hotspot generation and thermal coupling.
     * Default false for backward compatibility. Set true to activate hotspot system.
     */
    UPROPERTY()
    bool bEnableHotspots = false;

    /**
     * Milestone 4 Task 2.2: Rift progression rate (meters per My per rad/My velocity).
     * Controls how fast rifts widen based on divergent velocity.
     * Default 50000.0 m/My/(rad/My) ≈ realistic rift widening (~5 cm/yr at Earth scale).
     */
    UPROPERTY()
    double RiftProgressionRate = 50000.0;

    /**
     * Milestone 4 Task 2.2: Rift width threshold for triggering plate split (meters).
     * When rift width exceeds this value, boundary triggers split.
     * Default 500000.0 m (500 km, realistic for mature ocean basin rifts).
     */
    UPROPERTY()
    double RiftSplitThresholdMeters = 500000.0;

    /**
     * Milestone 4 Task 2.2: Enable rift propagation model.
     * Default false for backward compatibility. Set true to activate rift tracking.
     */
    UPROPERTY()
    bool bEnableRiftPropagation = false;

    /**
     * Milestone 5 Task 2.1: Continental erosion constant (m/My).
     * Base erosion rate for continental crust above sea level.
     * Formula: ErosionRate = k × Slope × (Elevation - SeaLevel)⁺
     * Default 0.001 m/My (paper Section 4.5, realistic geological erosion rate).
     */
    UPROPERTY()
    double ErosionConstant = 0.001;

    /**
     * Milestone 5 Task 2.1: Sea level reference elevation (meters).
     * Erosion only applies to terrain above this threshold.
     * Default 0.0 m (mean sea level).
     */
    UPROPERTY()
    double SeaLevel = 0.0;

    /**
     * Milestone 5 Task 2.1: Enable continental erosion model.
     * Default false for backward compatibility. Set true to activate erosion.
     */
    UPROPERTY()
    bool bEnableContinentalErosion = false;

    /**
     * Milestone 6 Task 2.3: Enable heightmap visualization mode.
     * When true, mesh vertex colors encode elevation (blue=low, red=high).
     * Default false (normal plate boundary visualization).
     */
    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use VisualizationMode instead."))
    bool bEnableHeightmapVisualization = false;

    /**
     * Milestone 6 GPU Preview: Skip CPU amplification path when controller handles GPU preview.
     * When true, AdvanceSteps() skips ApplyOceanicAmplification/ApplyContinentalAmplification CPU paths.
     * Controller sets this when bUseGPUPreviewMode is active to avoid redundant CPU work.
     * Default false (normal CPU/GPU-with-readback path).
     */
    UPROPERTY()
    bool bSkipCPUAmplification = false;

    /**
     * Milestone 5 Task 2.2: Sediment diffusion rate (dimensionless, 0-1).
     * Controls how quickly eroded material redistributes to neighbors.
     * Default 0.1 (10% of excess sediment diffuses per step).
     */
    UPROPERTY()
    double SedimentDiffusionRate = 0.1;

    /**
     * Milestone 5 Task 2.2: Enable sediment transport (Stage 0 diffusion).
     * Default false for backward compatibility. Set true to activate sediment redistribution.
     */
    UPROPERTY()
    bool bEnableSedimentTransport = false;

    /**
     * Milestone 5 Task 2.3: Oceanic dampening constant (m/My).
     * Smoothing rate for seafloor elevation (slower than erosion).
     * Default 0.0005 m/My (paper Section 4.5, oceanic crust subsidence).
     */
    UPROPERTY()
    double OceanicDampeningConstant = 0.0005;

    /** Gaussian smoothing radius for oceanic dampening (radians). */
    UPROPERTY()
    double OceanicDampeningSmoothingRadius = 0.1;

    /**
     * Milestone 5 Task 2.3: Oceanic age-subsidence coefficient (m/sqrt(My)).
     * Controls depth increase with crust age: depth = BaseDepth + Coeff × sqrt(age).
     * Default 350.0 m/sqrt(My) (empirical formula from paper).
     */
    UPROPERTY()
    double OceanicAgeSubsidenceCoeff = 350.0;

    /**
     * Milestone 5 Task 2.3: Enable oceanic dampening model.
     * Default false for backward compatibility. Set true to activate seafloor smoothing.
     */
    UPROPERTY()
    bool bEnableOceanicDampening = false;

    /**
     * Milestone 6 Task 2.1: Enable Stage B oceanic amplification (transform faults, fine detail).
     * Default false for backward compatibility. Set true to activate oceanic amplification.
     */
    UPROPERTY()
    bool bEnableOceanicAmplification = false;

    /**
     * Milestone 6 Task 2.1: Ridge amplitude applied to transform faults (meters).
     * Acts as a scalar multiplier for the procedural oceanic amplification noise.
     */
    UPROPERTY()
    double OceanicFaultAmplitude = 150.0;

    /**
     * Milestone 6 Task 2.1: Spatial frequency for transform fault noise (unitless).
     * Higher values yield denser fault bands, lower values produce broader ridges.
     */
    UPROPERTY()
    double OceanicFaultFrequency = 0.05;

    /**
     * Milestone 6 Task 2.1: Exponential age falloff constant for ridge noise (1/My).
     * Controls how quickly transform fault detail fades as crust ages. Default 0.02 ≈ 50 My e-folding.
     */
    UPROPERTY()
    double OceanicAgeFalloff = 0.02;

    /**
     * Milestone 6 Task 2.2: Enable Stage B continental amplification (exemplar-based terrain synthesis).
     * Default false for backward compatibility. Set true to activate continental amplification.
     */
    UPROPERTY()
    bool bEnableContinentalAmplification = false;

    /**
     * Milestone 6 Task 2.1: Minimum render subdivision level for amplification.
     * Amplification only applies at LOD levels >= this value (prevents wasted computation at low LOD).
     * Default 5 (10,242 vertices, high-detail preview per plan).
     */
    UPROPERTY()
    int32 MinAmplificationLOD = 5;

    /**
     * Milestone 6 Task 2.1: Number of adjacency rings marked dirty around ridge boundaries.
     * Controls how many neighbor layers refresh when boundary motion occurs. Higher values smooth
     * transitions but touch more vertices. Default 2 provides a 1-hop safety margin beyond
     * boundary edges.
     */
    UPROPERTY()
    int32 RidgeDirectionDirtyRingDepth = 2;
};

/**
 * Milestone 5 Phase 3: Unit conversion helper - meters to Unreal Engine centimeters.
 *
 * UE uses centimeters as base unit. All simulation logic operates in meters for geological accuracy.
 * This helper enforces the conversion at render boundaries to prevent magnitude errors.
 *
 * @param Meters Distance or dimension in meters
 * @return Distance in Unreal Engine centimeters (1 m = 100 cm)
 */
FORCEINLINE float MetersToUE(double Meters)
{
    return static_cast<float>(Meters * 100.0); // 1 meter = 100 centimeters
}

/**
 * Editor-only subsystem that holds the canonical tectonic simulation state.
 * The state uses double precision so long-running editor sessions avoid drift.
 */
UCLASS()
class UTectonicSimulationService : public UUnrealEditorSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** Resets the simulation to the initial baseline. */
    void ResetSimulation();

    /** Advance the simulation by the requested number of steps (each 2 My). */
    void AdvanceSteps(int32 StepCount);

    /** Returns the accumulated tectonic time in mega-years. */
    double GetCurrentTimeMy() const { return CurrentTimeMy; }

    /** Returns the last step time in milliseconds (Milestone 3 Task 4.5). */
    double GetLastStepTimeMs() const { return LastStepTimeMs; }
    const FStageBProfile& GetLatestStageBProfile() const { return LatestStageBProfile; }

    /** Accessor for the base sphere samples used to visualize placeholder geometry. */
    const TArray<FVector3d>& GetBaseSphereSamples() const { return BaseSphereSamples; }

    /** Accessor for plates (Milestone 2). */
    const TArray<FTectonicPlate>& GetPlates() const { return Plates; }

    /** Non-const accessor for plates (for test manipulation). */
    TArray<FTectonicPlate>& GetPlatesForModification() { return Plates; }

    /** Accessor for shared vertex pool (Milestone 2). */
    const TArray<FVector3d>& GetSharedVertices() const { return SharedVertices; }

    /** Accessor for render mesh vertices (Milestone 3 - separate from simulation vertices). */
    const TArray<FVector3d>& GetRenderVertices() const { return RenderVertices; }

    /** Accessor for render mesh triangle indices (Milestone 3). */
    const TArray<int32>& GetRenderTriangles() const { return RenderTriangles; }

    /** Skip CPU Stage B amplification passes when GPU preview handles displacement. */
    void SetSkipCPUAmplification(bool bInSkip);
    bool IsSkippingCPUAmplification() const { return Parameters.bSkipCPUAmplification; }

    /** Accessor for vertex-to-plate assignments (Milestone 3 Phase 2). */
    const TArray<int32>& GetVertexPlateAssignments() const { return VertexPlateAssignments; }

    /** Accessor for per-vertex velocity vectors (Milestone 3 Task 2.2). */
    const TArray<FVector3d>& GetVertexVelocities() const { return VertexVelocities; }

    /** Accessor for per-vertex stress values (Milestone 3 Task 2.3, cosmetic). */
    const TArray<double>& GetVertexStressValues() const { return VertexStressValues; }

    /** Milestone 4 Task 2.3: Accessor for per-vertex temperature values (K). */
    const TArray<double>& GetVertexTemperatureValues() const { return VertexTemperatureValues; }

    /** Milestone 5 Task 2.1: Accessor for per-vertex elevation values (meters). */
    const TArray<double>& GetVertexElevationValues() const { return VertexElevationValues; }

    /** Milestone 5 Task 2.1: Accessor for per-vertex erosion rates (m/My). */
    const TArray<double>& GetVertexErosionRates() const { return VertexErosionRates; }

    /** Milestone 5 Task 2.2: Accessor for per-vertex sediment thickness (meters). */
    const TArray<double>& GetVertexSedimentThickness() const { return VertexSedimentThickness; }

    /** Milestone 5 Task 2.3: Accessor for per-vertex crust age (My). */
    const TArray<double>& GetVertexCrustAge() const { return VertexCrustAge; }

    /** Milestone 6 Task 2.1: Accessor for per-vertex amplified elevation (Stage B, meters). */
    const TArray<double>& GetVertexAmplifiedElevation() const { return VertexAmplifiedElevation; }
    TArray<double>& GetMutableVertexAmplifiedElevation() { return VertexAmplifiedElevation; }

    const TArray<int32>& GetRenderVertexAdjacencyOffsets() const { return RenderVertexAdjacencyOffsets; }
    const TArray<int32>& GetRenderVertexAdjacency() const { return RenderVertexAdjacency; }

    /** Milestone 6 Task 2.1: Accessor for per-vertex ridge directions. */
    const TArray<FVector3d>& GetVertexRidgeDirections() const { return VertexRidgeDirections; }
    int32 GetLastRidgeDirectionUpdateCount() const { return LastRidgeDirectionUpdateCount; }

    /** Milestone 6 GPU: Initialize GPU exemplar texture array for Stage B amplification. */
    void InitializeGPUExemplarResources();

    /** Milestone 6 GPU: Shutdown GPU exemplar texture array (cleanup on module shutdown). */
    void ShutdownGPUExemplarResources();

    /** Accessor for boundary adjacency map (Milestone 2). */
    const TMap<TPair<int32, int32>, FPlateBoundary>& GetBoundaries() const { return Boundaries; }

    /** Accessor for simulation parameters (Milestone 2). */
    const FTectonicSimulationParameters& GetParameters() const { return Parameters; }

    /** Milestone 4 Task 1.2: Accessor for topology event log. */
    const TArray<FPlateTopologyEvent>& GetTopologyEvents() const { return TopologyEvents; }

    /** Milestone 4 Task 2.1: Accessor for active hotspots. */
    const TArray<FMantleHotspot>& GetHotspots() const { return Hotspots; }

    /** Update simulation parameters and reset (Milestone 2 - Phase 3). */
    void SetParameters(const FTectonicSimulationParameters& NewParams);

    /**
     * Milestone 6 Task 2.3: Toggle heightmap visualization without resetting simulation state.
     * Updates cached parameters, bumps surface version for LOD cache invalidation,
     * and leaves tectonic history untouched.
     */
    void SetHeightmapVisualizationEnabled(bool bEnabled);
    void SetVisualizationMode(ETectonicVisualizationMode Mode);
    ETectonicVisualizationMode GetVisualizationMode() const { return Parameters.VisualizationMode; }

    void SetHighlightSeaLevel(bool bEnabled);
    bool IsHighlightSeaLevelEnabled() const { return bHighlightSeaLevel; }

    /**
     * Milestone 4 Phase 4.1: Toggle automatic LOD selection without resetting simulation state.
     * Allows the editor UI to switch between camera-driven LOD and manual control.
     */
    void SetAutomaticLODEnabled(bool bEnabled);

    /**
     * Milestone 4 Phase 4.1: Update render subdivision level without resetting simulation state.
     * This allows LOD changes during camera movement without destroying tectonic history.
     * Only regenerates render mesh and Voronoi mapping; preserves plates, stress, rifts, etc.
     */
    void SetRenderSubdivisionLevel(int32 NewLevel);

    bool ShouldUseGPUAmplification() const;
    bool ApplyOceanicAmplificationGPU();
    bool ApplyContinentalAmplificationGPU();

    /** Export current simulation metrics to CSV (Milestone 2 - Phase 4). */
    void ExportMetricsToCSV();

    /** Milestone 4 Task 1.1: Re-tessellation public API. */

    /** Re-tessellation snapshot structure for rollback. */
    struct FRetessellationSnapshot
    {
        TArray<FVector3d> SharedVertices;
        TArray<FVector3d> RenderVertices;
        TArray<int32> RenderTriangles;
        TArray<int32> VertexPlateAssignments;
        TMap<TPair<int32, int32>, FPlateBoundary> Boundaries;
        double TimestampMy;

        /** Milestone 5: Erosion state (for rollback after failed retessellation). */
        TArray<double> VertexElevationValues;
        TArray<double> VertexErosionRates;
        TArray<double> VertexSedimentThickness;
        TArray<double> VertexCrustAge;

        FRetessellationSnapshot() : TimestampMy(0.0) {}
    };

    /** Captures current state for rollback. */
    FRetessellationSnapshot CaptureRetessellationSnapshot() const;

    /** Restores state from snapshot after failed rebuild. */
    void RestoreRetessellationSnapshot(const FRetessellationSnapshot& Snapshot);

    /** Performs incremental re-tessellation for drifted plates. Returns true if successful. */
    bool PerformRetessellation();

    /** Validates re-tessellation result against snapshot. */
    bool ValidateRetessellation(const FRetessellationSnapshot& Snapshot) const;

    /** Aggregated drift/quality metrics used to determine rebuild cadence. */
    struct FRetessellationAnalysis
    {
        double MaxDriftDegrees = 0.0;
        int32 MaxDriftPlateID = INDEX_NONE;
        double BadTriangleRatio = 0.0;
        int32 BadTriangleCount = 0;
        int32 TotalTriangleCount = 0;
    };

    /** Aggregated telemetry for re-tessellation cadence/throttling. */
    struct FRetessellationCadenceStats
    {
        int64 StepsObserved = 0;
        int64 StepsSpentInCooldown = 0;
        int32 EvaluationCount = 0;
        int32 TriggerCount = 0;
        int32 CooldownBlocks = 0;
        int32 StepsSinceLastTrigger = 0;
        int32 LastTriggerInterval = 0;
        int32 CurrentCooldownStepAccumulator = 0;
        int32 LastCooldownDuration = 0;
        double LastTriggerTimeMy = 0.0;
        double LastTriggerMaxDriftDegrees = 0.0;
        double LastTriggerBadTriangleRatio = 0.0;

        void Reset()
        {
            StepsObserved = 0;
            StepsSpentInCooldown = 0;
            EvaluationCount = 0;
            TriggerCount = 0;
            CooldownBlocks = 0;
            StepsSinceLastTrigger = 0;
            LastTriggerInterval = 0;
            CurrentCooldownStepAccumulator = 0;
            LastCooldownDuration = 0;
            LastTriggerTimeMy = 0.0;
            LastTriggerMaxDriftDegrees = 0.0;
            LastTriggerBadTriangleRatio = 0.0;
        }
    };

    /** Compute drift/quality metrics for the currently cached render mesh. */
    FRetessellationAnalysis ComputeRetessellationAnalysis() const;

    /** Apply cadence/hysteresis rules before invoking PerformRetessellation. */
    void MaybePerformRetessellation();

    /** Force ridge direction cache to rebuild on the next Stage B pass. */
    void InvalidateRidgeDirectionCache();
    void MarkAllRidgeDirectionsDirty();
    void MarkRidgeRingDirty(const TArray<int32>& SeedVertices, int32 RingDepth);
    void EnqueueCrustAgeResetSeeds(const TArray<int32>& SeedVertices);
    void ResetCrustAgeForSeeds(int32 RingDepth);

    /** Utility helpers for terrane surgery and render mesh maintenance. */
    void CompactRenderVertexData(const TArray<int32>& VerticesToRemove, TArray<int32>& OutOldToNew);
    int32 AppendRenderVertexFromRecord(const FTerraneVertexRecord& Record, int32 OverridePlateID);
    void InvalidateRenderVertexCaches();

    /** Milestone 4 Task 1.1: Re-tessellation performance tracking (public for tests). */
    double LastRetessellationTimeMs = 0.0;
    int32 RetessellationCount = 0;
    double LastRetessellationMaxDriftDegrees = 0.0;
    double LastRetessellationBadTriangleRatio = 0.0;
    int32 StepsSinceLastRetessellationCheck = 0;
    bool bRetessellationInCooldown = false;

    const FRetessellationCadenceStats& GetRetessellationCadenceStats() const { return RetessellationCadenceStats; }
    int64 GetTotalStepsSimulated() const { return TotalStepsSimulated; }

    /** Milestone 4 Phase 4.2: Version tracking for LOD cache invalidation. */
    int32 GetTopologyVersion() const { return TopologyVersion; }
    int32 GetSurfaceDataVersion() const { return SurfaceDataVersion; }

    /** Rebuild cached render adjacency after topology or LOD changes. */
    void BuildRenderVertexAdjacency();
    void BuildRenderVertexReverseAdjacency();
    void UpdateConvergentNeighborFlags();
    void BuildRenderVertexBoundaryCache();

    /** Milestone 5 Task 1.3: Full simulation history snapshot for undo/redo. */
    struct FSimulationHistorySnapshot
    {
        double CurrentTimeMy;
        TArray<FTectonicPlate> Plates;
        TArray<FVector3d> SharedVertices;
        TArray<FVector3d> RenderVertices;
        TArray<int32> RenderTriangles;
        TArray<int32> VertexPlateAssignments;
        TArray<FVector3d> VertexVelocities;
        TArray<double> VertexStressValues;
        TArray<double> VertexTemperatureValues;
        TMap<TPair<int32, int32>, FPlateBoundary> Boundaries;
        TArray<FPlateTopologyEvent> TopologyEvents;
        TArray<FMantleHotspot> Hotspots;
        TArray<FVector3d> InitialPlateCentroids;
        int32 TopologyVersion;
        int32 SurfaceDataVersion;

        /** Milestone 5: Erosion state (for undo/redo). */
        TArray<double> VertexElevationValues;
        TArray<double> VertexErosionRates;
        TArray<double> VertexSedimentThickness;
        TArray<double> VertexCrustAge;

        /** Milestone 6 Task 1.1: Terrane state (for undo/redo). */
        TArray<FContinentalTerrane> Terranes;
        int32 NextTerraneID;

        FSimulationHistorySnapshot() : CurrentTimeMy(0.0), TopologyVersion(0), SurfaceDataVersion(0), NextTerraneID(0) {}
    };

    /** Milestone 5 Task 1.3: Capture current state as history snapshot. */
    void CaptureHistorySnapshot();

    /** Milestone 5 Task 1.3: Undo to previous snapshot. Returns true if successful. */
    bool Undo();

    /** Milestone 5 Task 1.3: Redo to next snapshot. Returns true if successful. */
    bool Redo();

    /** Milestone 5 Task 1.3: Check if undo is available. */
    bool CanUndo() const { return CurrentHistoryIndex > 0; }

    /** Milestone 5 Task 1.3: Check if redo is available. */
    bool CanRedo() const { return CurrentHistoryIndex < HistoryStack.Num() - 1; }

    /** Milestone 5 Task 1.3: Get current history index (for UI display). */
    int32 GetHistoryIndex() const { return CurrentHistoryIndex; }

    /** Milestone 5 Task 1.3: Get history stack size (for UI display). */
    int32 GetHistorySize() const { return HistoryStack.Num(); }

    /** Milestone 5 Task 1.3: Get snapshot at index (for UI display). */
    const FSimulationHistorySnapshot* GetHistorySnapshotAt(int32 Index) const
    {
        return HistoryStack.IsValidIndex(Index) ? &HistoryStack[Index] : nullptr;
    }

    /** Milestone 5 Task 1.3: Jump to specific history index (for timeline scrubbing). */
    bool JumpToHistoryIndex(int32 Index);

    /**
     * Milestone 6 Task 1.1: Extract terrane from continental plate.
     * Performs mesh surgery to remove specified vertices from plate.
     *
     * @param SourcePlateID Plate to extract terrane from (must be continental)
     * @param TerraneVertexIndices Render vertex indices to extract (must be contiguous region)
     * @param OutTerraneID Unique ID assigned to newly extracted terrane
     * @return True if extraction succeeded, false if validation failed
     */
    bool ExtractTerrane(int32 SourcePlateID, const TArray<int32>& TerraneVertexIndices, int32& OutTerraneID);

    /**
     * Milestone 6 Task 1.1: Reattach terrane to target plate at collision.
     * Performs mesh surgery to merge terrane vertices into target plate.
     *
     * @param TerraneID Terrane to reattach (must be in Colliding state)
     * @param TargetPlateID Plate to attach to (must be continental at convergent boundary)
     * @return True if reattachment succeeded, false if validation failed
     */
    bool ReattachTerrane(int32 TerraneID, int32 TargetPlateID);

    /**
     * Milestone 6 Task 1.1: Validate mesh topology after terrane operation.
     * Checks Euler characteristic, manifold edges, and orphaned vertices.
     *
     * @param OutErrorMessage Detailed error description if validation fails
     * @return True if topology is valid, false otherwise
     */
    bool ValidateTopology(FString& OutErrorMessage) const;

    /**
     * Milestone 6 Task 1.1: Compute area of terrane region (km²).
     * Uses spherical triangle formula on render mesh.
     *
     * @param VertexIndices Render vertices comprising terrane
     * @return Area in km² (0 if invalid region)
     */
    double ComputeTerraneArea(const TArray<int32>& VertexIndices) const;

    /** Milestone 6 Task 1.1: Accessor for active terranes. */
    const TArray<FContinentalTerrane>& GetTerranes() const { return Terranes; }

    /** Milestone 6 Task 1.1: Get terrane by ID (nullptr if not found). */
    const FContinentalTerrane* GetTerraneByID(int32 TerraneID) const;

    /** Access cached float SoA streams for render vertices (position + normal). */
    void GetRenderVertexFloatSoA(
        const TArray<float>*& OutPositionX,
        const TArray<float>*& OutPositionY,
        const TArray<float>*& OutPositionZ,
        const TArray<float>*& OutNormalX,
        const TArray<float>*& OutNormalY,
        const TArray<float>*& OutNormalZ,
        const TArray<float>*& OutTangentX,
        const TArray<float>*& OutTangentY,
        const TArray<float>*& OutTangentZ) const;

    /** Access cached float inputs required by the oceanic amplification GPU path. */
    void GetOceanicAmplificationFloatInputs(
        const TArray<float>*& OutBaselineElevation,
        const TArray<FVector4f>*& OutRidgeDirections,
        const TArray<float>*& OutCrustAge,
        const TArray<FVector3f>*& OutRenderPositions,
        const TArray<uint32>*& OutOceanicMask) const;

    /** Pump pending GPU readbacks; optionally block until all are ready. */
    void ProcessPendingOceanicGPUReadbacks(bool bBlockUntilComplete = false, double* OutReadbackSeconds = nullptr);

#if WITH_EDITOR
    void EnqueueOceanicGPUJob(TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> Readback, int32 VertexCount);
#endif

    /**
     * Milestone 6 Task 1.2: Assign extracted terrane to nearest oceanic carrier plate.
     * Called automatically after extraction to initiate transport phase.
     *
     * @param TerraneID Terrane to assign carrier to
     * @return True if carrier assigned successfully
     */
    bool AssignTerraneCarrier(int32 TerraneID);

    /**
     * Milestone 6 Task 1.2: Update terrane positions based on carrier plate motion.
     * Called each step to migrate terranes with their carrier plates.
     */
    void UpdateTerranePositions(double DeltaTimeMy);

    /**
     * Milestone 6 Task 1.2: Detect terranes approaching continental convergent boundaries.
     * Marks terranes as Colliding when within 500 km of collision.
     */
    void DetectTerraneCollisions();

    /**
     * Milestone 6 Task 1.3: Automatically reattach colliding terranes to target continental plates.
     * Called each step after collision detection to complete terrane lifecycle.
     */
    void ProcessTerraneReattachments();

#if UE_BUILD_DEVELOPMENT
    /** Development-only spike hook: perform terrane mesh surgery on the current render mesh. */
    void RunTerraneMeshSurgerySpike();

    /** Development helper: log plate/elevation mismatches for early diagnostics. */
    void LogPlateElevationMismatches(const TCHAR* ContextLabel, int32 SampleCount = 128, int32 MaxLogged = 16) const;
#endif

    /**
     * Export heightmap visualization as color-coded PNG with elevation gradient.
     * @param ImageWidth Width of output image in pixels (default 2048)
     * @param ImageHeight Height of output image in pixels (default 1024)
     * @return Path to exported PNG file, or empty string on failure
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonic Simulation")
    FString ExportHeightmapVisualization(int32 ImageWidth = 2048, int32 ImageHeight = 1024);

private:
    void GenerateDefaultSphereSamples();

    /** Phase 1 Task 1: Generate icosphere-based plate tessellation. */
    void GenerateIcospherePlates();

    /** Phase 1 Task 2: Assign Euler poles to plates. */
    void InitializeEulerPoles();

    /** Phase 1 Task 3: Build boundary adjacency map from icosphere topology. */
    void BuildBoundaryAdjacencyMap();

    /** Phase 1 Task 1 helper: Subdivide icosphere to target plate count. */
    void SubdivideIcosphere(int32 SubdivisionLevel);

    /** Milestone 3 Task 1.1: Generate high-density render mesh from base icosphere. */
    void GenerateRenderMesh();

    /** Milestone 3 Task 1.1 helper: Subdivide a triangle by splitting edges. */
    int32 GetMidpointIndex(int32 V0, int32 V1, TMap<TPair<int32, int32>, int32>& MidpointCache, TArray<FVector3d>& Vertices);

    /** Milestone 3 Task 2.1: Build Voronoi mapping from render vertices to plates. */
    void BuildVoronoiMapping();

    /** Milestone 3 Task 2.2: Compute per-vertex velocity field (v = ω × r). */
    void ComputeVelocityField();

    /** Milestone 3 Task 2.3: Update stress at boundaries (cosmetic visualization). */
    void UpdateBoundaryStress(double DeltaTimeMy);

    /** Milestone 3 Task 2.3: Interpolate boundary stress to render vertices (Gaussian falloff). */
    void InterpolateStressToVertices();

    /** Milestone 3 Task 3.1: Apply Lloyd relaxation to evenly distribute plate centroids. */
    void ApplyLloydRelaxation();

    /** Milestone 3 Task 3.3: Check if plates have drifted beyond re-tessellation threshold. */
    void CheckRetessellationNeeded();

    /** Phase 1 Task 1 helper: Validate solid angle coverage ≈ 4π. */
    void ValidateSolidAngleCoverage();

    /** Phase 2 Task 4: Migrate plate centroids using Euler pole rotations. */
    void MigratePlateCentroids(double DeltaTimeMy);

    /** Phase 2 Task 5: Update boundary classifications based on relative velocities. */
    void UpdateBoundaryClassifications();

    /** Milestone 4 Task 1.2: Detect and execute plate splits (rift-driven). */
    void DetectAndExecutePlateSplits();

    /** Milestone 4 Task 1.2: Detect and execute plate merges (subduction-driven). */
    void DetectAndExecutePlateMerges();

    /** Milestone 4 Task 1.2: Execute plate split along divergent boundary. */
    bool SplitPlate(int32 PlateID, const TPair<int32, int32>& BoundaryKey, const FPlateBoundary& Boundary);

    /** Milestone 4 Task 1.2: Execute plate merge (consume smaller plate into larger). */
    bool MergePlates(int32 ConsumedPlateID, int32 SurvivorPlateID, const TPair<int32, int32>& BoundaryKey, const FPlateBoundary& Boundary);

    /** Milestone 4 Task 1.2: Calculate plate area (spherical triangles). */
    double ComputePlateArea(const FTectonicPlate& Plate) const;

    /** Milestone 4 Task 1.3: Update boundary lifecycle states (Nascent/Active/Dormant). */
    void UpdateBoundaryStates(double DeltaTimeMy);

    /** Milestone 4 Task 2.1: Generate hotspot seeds deterministically. */
    void GenerateHotspots();

    /** Milestone 4 Task 2.1: Update hotspot positions in mantle frame (drift over time). */
    void UpdateHotspotDrift(double DeltaTimeMy);

    /** Milestone 4 Task 2.1: Apply hotspot thermal contribution to plate stress/elevation. */
    void ApplyHotspotThermalContribution();

    /** Milestone 4 Task 2.2: Update rift progression for divergent boundaries. */
    void UpdateRiftProgression(double DeltaTimeMy);

    /** Milestone 4 Task 2.3: Compute thermal field from hotspots and subduction zones. */
    void ComputeThermalField();

    /** Milestone 5 Task 2.1: Apply continental erosion to vertices above sea level. */
    void ApplyContinentalErosion(double DeltaTimeMy);

    /** Milestone 5 Task 2.2: Redistribute sediment via diffusion (Stage 0, mass-conserving). */
    void ApplySedimentTransport(double DeltaTimeMy);

    /** Milestone 5 Task 2.3: Apply oceanic dampening and age-subsidence to seafloor. */
    void ApplyOceanicDampening(double DeltaTimeMy);

    /** Milestone 5: Helper to compute surface slope at vertex (for erosion rate). */
    double ComputeVertexSlope(int32 VertexIdx) const;

    /** Milestone 6 Task 2.1: Compute ridge directions for all oceanic vertices. */
    void ComputeRidgeDirections();

    /** Milestone 6 Task 2.1: Apply Stage B oceanic amplification (transform faults, fine detail). */
    void ApplyOceanicAmplification();

    /** Milestone 6 Task 2.2: Apply Stage B continental amplification (exemplar-based terrain synthesis). */
    void ApplyContinentalAmplification();

    /** Ensures VertexAmplifiedElevation starts from the latest base elevation before Stage B passes. */
    void InitializeAmplifiedElevationBaseline();

    double CurrentTimeMy = 0.0;
    double LastStepTimeMs = 0.0; // Milestone 3 Task 4.5: Performance tracking
    FStageBProfile LatestStageBProfile;
    int64 TotalStepsSimulated = 0;
    FRetessellationCadenceStats RetessellationCadenceStats;
    TArray<FVector3d> BaseSphereSamples;

    /** Milestone 2 state (Phase 1). */
    UPROPERTY()
    FTectonicSimulationParameters Parameters;

    TArray<FTectonicPlate> Plates;
    TArray<FVector3d> SharedVertices; // Shared vertex pool for plate polygons (simulation)
    TMap<TPair<int32, int32>, FPlateBoundary> Boundaries; // Key: (PlateA_ID, PlateB_ID), sorted

    /** Milestone 3: Separate render mesh geometry (high-density, independent from simulation). */
    TArray<FVector3d> RenderVertices;
    TArray<int32> RenderTriangles; // Triplets of indices into RenderVertices
    TArray<int32> VertexPlateAssignments; // Maps each RenderVertex index to a Plate ID (Voronoi cell)
    TArray<FVector3d> VertexVelocities; // Velocity vector (v = ω × r) for each RenderVertex (Task 2.2)
    TArray<double> VertexStressValues; // Interpolated stress (MPa) for each RenderVertex (Task 2.3, cosmetic)
    TArray<double> VertexTemperatureValues; // Milestone 4 Task 2.3: Thermal field (K) from hotspots + subduction

    /** Milestone 5 Task 2.1: Per-vertex elevation (meters) relative to sphere surface. */
    TArray<double> VertexElevationValues;

    /** Milestone 5 Task 2.1: Per-vertex erosion rate (m/My) for visualization/CSV export. */
    TArray<double> VertexErosionRates;

    /** Milestone 5 Task 2.2: Per-vertex sediment thickness (meters) from erosion redistribution. */
    TArray<double> VertexSedimentThickness;

    /** Milestone 5 Task 2.3: Per-vertex oceanic crust age (My) for age-subsidence calculations. */
    TArray<double> VertexCrustAge;

    /** Milestone 6 Task 2.1: Per-vertex ridge direction (for transform fault orientation). */
    TArray<FVector3d> VertexRidgeDirections;

    /** Milestone 6 Task 2.1: Per-vertex amplified elevation (Stage B, meters). */
    TArray<double> VertexAmplifiedElevation;

    /** Optional sealevel emphasis toggle (visual only). */
    bool bHighlightSeaLevel = false;

    /** Cached render vertex adjacency (CSR layout: Offsets.Num == RenderVertices.Num + 1). */
    TArray<int32> RenderVertexAdjacencyOffsets;
    TArray<int32> RenderVertexAdjacency;
    TArray<float> RenderVertexAdjacencyWeights;
    TArray<float> RenderVertexAdjacencyWeightTotals;
    TArray<int32> RenderVertexReverseAdjacency;
    TArray<uint8> ConvergentNeighborFlags;

    /** Pending seeds for crust age reset near divergent boundaries. */
    TArray<int32> PendingCrustAgeResetSeeds;
    TBitArray<> PendingCrustAgeResetMask;

    /** Render-vertex level boundary cache for ridge direction reconstruction. */
    struct FRenderVertexBoundaryInfo
    {
        float DistanceRadians = TNumericLimits<float>::Max();
        FVector3d BoundaryTangent = FVector3d::ZeroVector;
        int32 SourcePlateID = INDEX_NONE;
        int32 OpposingPlateID = INDEX_NONE;
        bool bHasBoundary = false;
        bool bIsDivergent = false;
    };

    TArray<FRenderVertexBoundaryInfo> RenderVertexBoundaryCache;

    /** Cadence counter for Voronoi refresh. */
    int32 StepsSinceLastVoronoiRefresh = 0;

    /** Milestone 3 Task 3.3: Initial plate centroid positions (captured after Lloyd relaxation). */
    TArray<FVector3d> InitialPlateCentroids;

    /** Milestone 4 Task 1.2: Log of plate topology change events (splits/merges). */
    TArray<FPlateTopologyEvent> TopologyEvents;

    /** Milestone 4 Task 2.1: Active mantle hotspots/plumes. */
    TArray<FMantleHotspot> Hotspots;

    /** Milestone 6 Task 1.1: Active continental terranes (extracted/transporting/colliding). */
    TArray<FContinentalTerrane> Terranes;

    /** Milestone 6 Task 1.1: Next terrane ID for deterministic generation. */
    int32 NextTerraneID = 0;

    /** Milestone 4 Phase 4.2: Topology version (increments on re-tessellation/split/merge). */
    int32 TopologyVersion = 0;

    /** Milestone 4 Phase 4.2: Surface data version (increments on stress/elevation changes). */
    int32 SurfaceDataVersion = 0;

    /** Milestone 5 Task 1.3: History stack for undo/redo (limited to 100 snapshots by default). */
    TArray<FSimulationHistorySnapshot> HistoryStack;

    /** Milestone 5 Task 1.3: Current position in history stack (for undo/redo navigation). */
    int32 CurrentHistoryIndex = -1;

    /** Milestone 5 Task 1.3: Maximum history size (prevents unbounded memory growth). */
    int32 MaxHistorySize = 100;

#if WITH_AUTOMATION_TESTS
public:
    void SetHeightmapExportTestOverrides(bool bInForceModuleFailure, bool bInForceWriteFailure = false, const FString& InOverrideOutputDirectory = FString());

    void ForceRidgeRecomputeForTest() { ComputeRidgeDirections(); }

private:
    bool bForceHeightmapModuleFailure = false;
    bool bForceHeightmapWriteFailure = false;
    FString HeightmapExportOverrideDirectory;
#endif

    /** Persistent float SoA mirrors for render vertex data. */
    struct FRenderVertexFloatSoA
    {
        TArray<float> PositionX;
        TArray<float> PositionY;
        TArray<float> PositionZ;
        TArray<float> NormalX;
        TArray<float> NormalY;
        TArray<float> NormalZ;
        TArray<float> TangentX;
        TArray<float> TangentY;
        TArray<float> TangentZ;
    };

    /** Cached float inputs shared by the oceanic amplification GPU path. */
    struct FOceanicAmplificationFloatInputs
    {
        TArray<float> BaselineElevation;
        TArray<float> CrustAge;
        TArray<FVector4f> RidgeDirections;
        TArray<FVector3f> RenderPositions;
        TArray<uint32> OceanicMask;
        uint64 CachedDataSerial = 0;
    };

    struct FRidgeDirectionFloatSoA
    {
        TArray<float> DirX;
        TArray<float> DirY;
        TArray<float> DirZ;
        int32 CachedTopologyVersion = INDEX_NONE;
        int32 CachedVertexCount = 0;
    };

    /** Mutable caches to avoid recomputing float mirrors on every call. */
    mutable FRenderVertexFloatSoA RenderVertexFloatSoA;
    mutable FOceanicAmplificationFloatInputs OceanicAmplificationFloatInputs;
    mutable FRidgeDirectionFloatSoA RidgeDirectionFloatSoA;

    /** Monotonic serial tracking modifications to amplification inputs. */
    uint64 OceanicAmplificationDataSerial = 1;

    /** Ridge direction cache bookkeeping. */
    mutable TBitArray<> RidgeDirectionDirtyMask;
    mutable int32 RidgeDirectionDirtyCount = 0;
    mutable int32 CachedRidgeDirectionTopologyVersion = INDEX_NONE;
    mutable int32 CachedRidgeDirectionVertexCount = 0;
    mutable int32 LastRidgeDirectionUpdateCount = 0;

#if WITH_EDITOR
    struct FOceanicGPUAsyncJob
    {
        TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> Readback;
        FRenderCommandFence DispatchFence;
        FRenderCommandFence CopyFence;
        int32 NumBytes = 0;
        int32 VertexCount = 0;
        uint64 JobId = 0;
        bool bCopySubmitted = false;
    };

    TArray<FOceanicGPUAsyncJob> PendingOceanicGPUJobs;
    uint64 NextOceanicGPUJobId = 1;
#endif

    void RefreshRenderVertexFloatSoA() const;
    void RefreshOceanicAmplificationFloatInputs() const;
    void BumpOceanicAmplificationSerial();
    void EnsureRidgeDirtyMaskSize(int32 VertexCount) const;
    bool MarkRidgeDirectionVertexDirty(int32 VertexIdx);
};
#if WITH_EDITOR
class FRHIGPUBufferReadback;
#endif
