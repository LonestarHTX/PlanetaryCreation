#include "TectonicSimulationController.h"

#include "Async/Future.h"
#include "Editor.h"
#include "Engine/World.h"
#include "RealtimeMeshComponent/Public/RealtimeMeshActor.h"
#include "RealtimeMeshComponent/Public/RealtimeMeshComponent.h"
#include "RealtimeMeshComponent/Public/RealtimeMeshSimple.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshBuilder.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshSectionConfig.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshStreamRange.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "TectonicSimulationService.h"

FTectonicSimulationController::FTectonicSimulationController() = default;
FTectonicSimulationController::~FTectonicSimulationController() = default;

void FTectonicSimulationController::Initialize()
{
    CachedService = GetService();
}

void FTectonicSimulationController::Shutdown()
{
    CachedService.Reset();
}

void FTectonicSimulationController::StepSimulation(int32 Steps)
{
    if (UTectonicSimulationService* Service = GetService())
    {
        Service->AdvanceSteps(Steps);

        EnsurePreviewActor();

        // Phase 5: Render actual tectonic plates with unique colors
        RealtimeMesh::FRealtimeMeshStreamSet StreamSet;
        int32 VertexCount = 0;
        int32 TriangleCount = 0;

        {
            using namespace RealtimeMesh;

            const TArray<FTectonicPlate>& Plates = Service->GetPlates();
            const TArray<FVector3d>& SharedVertices = Service->GetSharedVertices();

            if (Plates.Num() > 0 && SharedVertices.Num() > 0)
            {
                TRealtimeMeshBuilderLocal<uint16, FPackedNormal, FVector2DHalf, 1> Builder(StreamSet);
                Builder.EnableTangents();
                Builder.EnableTexCoords();
                Builder.EnableColors();
                Builder.EnablePolyGroups();

                const float RadiusUnits = 6370.0f; // 1 Unreal unit == 1 km for editor tooling

                // Helper to generate stable color from plate ID
                auto GetPlateColor = [](int32 PlateID) -> FColor
                {
                    // Use golden ratio for color distribution
                    const float GoldenRatio = 0.618033988749895f;
                    const float Hue = FMath::Fmod(PlateID * GoldenRatio, 1.0f);

                    // Convert HSV to RGB (Saturation=0.7, Value=0.9 for pastel look)
                    FLinearColor HSV(Hue * 360.0f, 0.7f, 0.9f);
                    FLinearColor RGB = HSV.HSVToLinearRGB();
                    return RGB.ToFColor(false);
                };

                // Build shared vertex pool first
                TArray<int32> VertexRemap;
                VertexRemap.SetNumUninitialized(SharedVertices.Num());

                for (int32 i = 0; i < SharedVertices.Num(); ++i)
                {
                    const FVector3d& Vertex = SharedVertices[i];
                    const FVector3d Normalized = Vertex.GetSafeNormal();
                    const FVector3f Position = FVector3f(Normalized * RadiusUnits);
                    const FVector3f Normal = Position.GetSafeNormal();

                    // Calculate tangent for proper lighting
                    const FVector3f UpVector = (FMath::Abs(Normal.Z) > 0.99f) ? FVector3f(1.0f, 0.0f, 0.0f) : FVector3f(0.0f, 0.0f, 1.0f);
                    FVector3f TangentX = FVector3f::CrossProduct(Normal, UpVector).GetSafeNormal();
                    const FVector2f TexCoord((Normal.X + 1.0f) * 0.5f, (Normal.Y + 1.0f) * 0.5f);

                    // Color will be overridden per-plate, use white as default
                    const int32 VertexId = Builder.AddVertex(Position)
                        .SetNormalAndTangent(Normal, TangentX)
                        .SetColor(FColor::White)
                        .SetTexCoord(TexCoord);

                    VertexRemap[i] = VertexId;
                }

                VertexCount = SharedVertices.Num();

                // Add triangles for each plate with unique color
                for (const FTectonicPlate& Plate : Plates)
                {
                    if (Plate.VertexIndices.Num() == 3) // Triangular plate
                    {
                        const FColor PlateColor = GetPlateColor(Plate.PlateID);

                        const int32 V0 = VertexRemap[Plate.VertexIndices[0]];
                        const int32 V1 = VertexRemap[Plate.VertexIndices[1]];
                        const int32 V2 = VertexRemap[Plate.VertexIndices[2]];

                        // Override vertex colors for this plate
                        Builder.SetColor(V0, PlateColor);
                        Builder.SetColor(V1, PlateColor);
                        Builder.SetColor(V2, PlateColor);

                        // Add triangle with correct winding order (CCW when viewed from outside)
                        Builder.AddTriangle(V0, V2, V1, Plate.PlateID);
                        TriangleCount++;
                    }
                }

                UE_LOG(LogTemp, Verbose, TEXT("Rendered %d plates with %d vertices and %d triangles"),
                    Plates.Num(), VertexCount, TriangleCount);
            }
        }

        UpdatePreviewMesh(MoveTemp(StreamSet), VertexCount, TriangleCount);
    }
}

double FTectonicSimulationController::GetCurrentTimeMy() const
{
    if (const UTectonicSimulationService* Service = GetService())
    {
        return Service->GetCurrentTimeMy();
    }
    return 0.0;
}

UTectonicSimulationService* FTectonicSimulationController::GetSimulationService() const
{
    return GetService();
}

UTectonicSimulationService* FTectonicSimulationController::GetService() const
{
    if (CachedService.IsValid())
    {
        return CachedService.Get();
    }

#if WITH_EDITOR
    if (GEditor)
    {
        UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
        if (Service)
        {
            CachedService = Service;
            return Service;
        }
    }
#endif

    return nullptr;
}

void FTectonicSimulationController::EnsurePreviewActor() const
{
    if (PreviewActor.IsValid() && PreviewMesh.IsValid())
    {
        return;
    }

#if WITH_EDITOR
    if (!GEditor)
    {
        return;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = TEXT("TectonicPreviewActor");
    SpawnParams.ObjectFlags = RF_Transient;
    SpawnParams.OverrideLevel = World->PersistentLevel;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    ARealtimeMeshActor* Actor = World->SpawnActor<ARealtimeMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (!Actor)
    {
        return;
    }

    Actor->SetActorHiddenInGame(true);
    Actor->SetIsTemporarilyHiddenInEditor(false);
    Actor->SetActorLabel(TEXT("TectonicPreviewActor"));

    PreviewActor = Actor;

    if (URealtimeMeshComponent* Component = Actor->GetRealtimeMeshComponent())
    {
        Component->SetMobility(EComponentMobility::Movable);
        if (URealtimeMeshSimple* Mesh = Component->InitializeRealtimeMesh<URealtimeMeshSimple>())
        {
            Mesh->SetupMaterialSlot(0, TEXT("TectonicPreview"));
            if (UMaterialInterface* DefaultMaterial = UMaterial::GetDefaultMaterial(EMaterialDomain::MD_Surface))
            {
                Component->SetMaterial(0, DefaultMaterial);
            }
            PreviewMesh = Mesh;
            bPreviewInitialized = false;
        }
    }
#endif
}

void FTectonicSimulationController::UpdatePreviewMesh(RealtimeMesh::FRealtimeMeshStreamSet&& StreamSet, int32 VertexCount, int32 TriangleCount)
{
    if (!PreviewMesh.IsValid())
    {
        return;
    }

    URealtimeMeshSimple* Mesh = PreviewMesh.Get();
    const FRealtimeMeshSectionGroupKey GroupKey = FRealtimeMeshSectionGroupKey::Create(0, FName(TEXT("TectonicPreview")));
    const FRealtimeMeshSectionKey SectionKey = FRealtimeMeshSectionKey::CreateForPolyGroup(GroupKey, 0);

    if (!bPreviewInitialized)
    {
        Mesh->CreateSectionGroup(GroupKey, MoveTemp(StreamSet));
        Mesh->UpdateSectionConfig(SectionKey, FRealtimeMeshSectionConfig(0));
        bPreviewInitialized = true;
    }
    else
    {
        Mesh->UpdateSectionGroup(GroupKey, MoveTemp(StreamSet));
    }

    const int32 ClampedVertices = FMath::Max(VertexCount, 0);
    const int32 ClampedTriangles = FMath::Max(TriangleCount, 0);
    const FRealtimeMeshStreamRange Range(0, ClampedVertices, 0, ClampedTriangles * 3);
    Mesh->UpdateSectionRange(SectionKey, Range);
}
