#include "TectonicSimulationService.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

void UTectonicSimulationService::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ResetSimulation();
}

void UTectonicSimulationService::Deinitialize()
{
    BaseSphereSamples.Reset();
    Plates.Reset();
    SharedVertices.Reset();
    Boundaries.Reset();
    Super::Deinitialize();
}

void UTectonicSimulationService::ResetSimulation()
{
    CurrentTimeMy = 0.0;
    GenerateDefaultSphereSamples();

    // Milestone 2: Generate plate simulation state
    GenerateIcospherePlates();
    InitializeEulerPoles();
    BuildBoundaryAdjacencyMap();
    ValidateSolidAngleCoverage();
}

void UTectonicSimulationService::AdvanceSteps(int32 StepCount)
{
    if (StepCount <= 0)
    {
        return;
    }

    constexpr double StepDurationMy = 2.0; // Paper defines delta t = 2 My per iteration

    for (int32 Step = 0; Step < StepCount; ++Step)
    {
        // Phase 2 Task 4: Migrate plate centroids via Euler pole rotation
        MigratePlateCentroids(StepDurationMy);

        // Phase 2 Task 5: Update boundary classifications based on relative velocities
        UpdateBoundaryClassifications();

        CurrentTimeMy += StepDurationMy;
    }
}

void UTectonicSimulationService::SetParameters(const FTectonicSimulationParameters& NewParams)
{
    Parameters = NewParams;
    ResetSimulation();
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
}

void UTectonicSimulationService::GenerateIcospherePlates()
{
    Plates.Reset();
    SharedVertices.Reset();

    // Phase 1 Task 1: Generate icosphere subdivision to approximate desired plate count
    // An icosahedron has 20 faces. Each subdivision level quadruples face count:
    // Level 0: 20 faces
    // Level 1: 80 faces
    // Level 2: 320 faces
    // Target ~12-20 plates → use level 0 (20 faces) for now

    const int32 SubdivisionLevel = 0; // TODO: Calculate from Parameters.PlateCount
    SubdivideIcosphere(SubdivisionLevel);

    // Assign plate IDs and initialize properties
    FRandomStream RNG(Parameters.Seed);
    const int32 NumPlates = Plates.Num();

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

        // Assign crust type: 70% oceanic, 30% continental (from paper)
        const bool bIsOceanic = RNG.FRand() < 0.7;
        Plate.CrustType = bIsOceanic ? ECrustType::Oceanic : ECrustType::Continental;
        Plate.CrustThickness = bIsOceanic ? 7.0 : 35.0; // Oceanic ~7km, Continental ~35km
    }

    UE_LOG(LogTemp, Log, TEXT("Generated %d plates from icosphere subdivision level %d"), NumPlates, SubdivisionLevel);
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

    // TODO(Milestone-3): Implement subdivision for higher detail
    // For now, using base icosahedron (20 faces = 20 plates)
    if (SubdivisionLevel > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Icosphere subdivision level %d requested but not yet implemented. Using base icosahedron."), SubdivisionLevel);
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

    UE_LOG(LogTemp, Log, TEXT("Initialized Euler poles for %d plates"), Plates.Num());
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

    UE_LOG(LogTemp, Log, TEXT("Built boundary adjacency map with %d boundaries"), Boundaries.Num());
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

    UE_LOG(LogTemp, Log, TEXT("Solid angle validation: Total=%.6f, Expected=%.6f (4π), Error=%.4f%%"),
        TotalSolidAngle, ExpectedSolidAngle, Error * 100.0);

    if (Error > 0.01) // 1% tolerance
    {
        UE_LOG(LogTemp, Warning, TEXT("Solid angle coverage error exceeds 1%% tolerance"));
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
            UE_LOG(LogTemp, VeryVerbose, TEXT("Plate %d displaced by %.6f radians (%.2f km on Earth-scale)"),
                Plate.PlateID, DisplacementRadians, DisplacementRadians * 6370.0);
        }
    }
}

void UTectonicSimulationService::UpdateBoundaryClassifications()
{
    // Phase 2 Task 5: Classify boundaries based on relative plate velocities
    // Velocity at a point on plate = ω × r (angular velocity cross radius vector)
    // For boundary between plates A and B, compute velocities at boundary midpoint

    int32 DivergentCount = 0;
    int32 ConvergentCount = 0;
    int32 TransformCount = 0;

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

        // Calculate boundary midpoint on sphere
        const FVector3d& V0 = SharedVertices[Boundary.SharedEdgeVertices[0]];
        const FVector3d& V1 = SharedVertices[Boundary.SharedEdgeVertices[1]];
        const FVector3d BoundaryMidpoint = ((V0 + V1) * 0.5).GetSafeNormal();

        // Compute velocity at boundary for each plate: v = ω × r
        // ω is the angular velocity vector = AngularVelocity * EulerPoleAxis
        const FVector3d OmegaA = PlateA->EulerPoleAxis * PlateA->AngularVelocity;
        const FVector3d OmegaB = PlateB->EulerPoleAxis * PlateB->AngularVelocity;

        const FVector3d VelocityA = FVector3d::CrossProduct(OmegaA, BoundaryMidpoint);
        const FVector3d VelocityB = FVector3d::CrossProduct(OmegaB, BoundaryMidpoint);

        // Relative velocity: vRel = vA - vB
        const FVector3d RelativeVelocity = VelocityA - VelocityB;

        // Boundary normal (outward from PlateA toward PlateB)
        const FVector3d BoundaryNormal = FVector3d::CrossProduct(V1 - V0, BoundaryMidpoint).GetSafeNormal();

        // Project relative velocity onto boundary normal
        const double NormalComponent = FVector3d::DotProduct(RelativeVelocity, BoundaryNormal);

        // Classify boundary
        const double ClassificationThreshold = 0.001; // Radians/My threshold
        if (NormalComponent > ClassificationThreshold)
        {
            Boundary.BoundaryType = EBoundaryType::Divergent; // Plates separating
            DivergentCount++;
        }
        else if (NormalComponent < -ClassificationThreshold)
        {
            Boundary.BoundaryType = EBoundaryType::Convergent; // Plates colliding
            ConvergentCount++;
        }
        else
        {
            Boundary.BoundaryType = EBoundaryType::Transform; // Shear/parallel motion
            TransformCount++;
        }

        Boundary.RelativeVelocity = RelativeVelocity.Length();
    }

    UE_LOG(LogTemp, VeryVerbose, TEXT("Boundary classification: %d divergent, %d convergent, %d transform"),
        DivergentCount, ConvergentCount, TransformCount);
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

    // Add separator and boundary data
    CSVLines.Add(TEXT("")); // Empty line
    CSVLines.Add(TEXT("PlateA_ID,PlateB_ID,BoundaryType,RelativeVelocity"));

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

        CSVLines.Add(FString::Printf(TEXT("%d,%d,%s,%.8f"),
            PlateIDs.Key, PlateIDs.Value,
            *BoundaryTypeName,
            Boundary.RelativeVelocity
        ));
    }

    // Add summary statistics
    CSVLines.Add(TEXT("")); // Empty line
    CSVLines.Add(TEXT("Metric,Value"));
    CSVLines.Add(FString::Printf(TEXT("SimulationTime_My,%.2f"), CurrentTimeMy));
    CSVLines.Add(FString::Printf(TEXT("PlateCount,%d"), Plates.Num()));
    CSVLines.Add(FString::Printf(TEXT("BoundaryCount,%d"), Boundaries.Num()));
    CSVLines.Add(FString::Printf(TEXT("Seed,%d"), Parameters.Seed));

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

    // Write to file
    const FString CSVContent = FString::Join(CSVLines, TEXT("\n"));
    if (FFileHelper::SaveStringToFile(CSVContent, *FilePath))
    {
        UE_LOG(LogTemp, Log, TEXT("Exported metrics to: %s"), *FilePath);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to export metrics to: %s"), *FilePath);
    }
}
