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

        RealtimeMesh::FRealtimeMeshStreamSet StreamSet;
        int32 VertexCount = 0;
        int32 TriangleCount = 0;

        {
            const TArray<FVector3d>& Samples = Service->GetBaseSphereSamples();
            if (Samples.Num() >= 6)
            {
                using namespace RealtimeMesh;

                TRealtimeMeshBuilderLocal<uint16, FPackedNormal, FVector2DHalf, 1> Builder(StreamSet);
                Builder.EnableTangents();
                Builder.EnableTexCoords();
                Builder.EnableColors();
                Builder.EnablePolyGroups();

                const float RadiusUnits = 6370.0f; // 1 Unreal unit == 1 km for editor tooling

                TArray<int32, TInlineAllocator<6>> VertexIndices;
                VertexIndices.Reserve(Samples.Num());

                for (const FVector3d& Sample : Samples)
                {
                    const FVector3d Normalized = Sample.GetSafeNormal();
                    const FVector3f Position = FVector3f(Normalized * RadiusUnits);
                    const FVector3f Normal = Position.GetSafeNormal();
                    const FVector3f UpVector = (FMath::Abs(Normal.Z) > 0.99f) ? FVector3f(1.0f, 0.0f, 0.0f) : FVector3f(0.0f, 0.0f, 1.0f);
                    FVector3f TangentX = FVector3f::CrossProduct(Normal, UpVector);
                    if (!TangentX.Normalize())
                    {
                        TangentX = FVector3f::CrossProduct(Normal, FVector3f(0.0f, 1.0f, 0.0f));
                        if (!TangentX.Normalize())
                        {
                            TangentX = FVector3f::CrossProduct(Normal, FVector3f(1.0f, 0.0f, 0.0f));
                            TangentX.Normalize();
                        }
                    }

                    const FVector3f Binormal = FVector3f::CrossProduct(TangentX, Normal);
                    if (FVector3f::DotProduct(Binormal, UpVector) < 0.0f)
                    {
                        TangentX *= -1.0f;
                    }
                    const FVector2f TexCoord((Normal.X + 1.0f) * 0.5f, (Normal.Y + 1.0f) * 0.5f);

                    const int32 VertexId = Builder.AddVertex(Position)
                        .SetNormalAndTangent(Normal, TangentX)
                        .SetColor(FColor::Silver)
                        .SetTexCoord(TexCoord);
                    VertexIndices.Add(VertexId);
                }

                VertexCount = VertexIndices.Num();

                if (VertexIndices.Num() >= 6)
                {
                    const int32 PosX = VertexIndices[0];
                    const int32 NegX = VertexIndices[1];
                    const int32 PosY = VertexIndices[2];
                    const int32 NegY = VertexIndices[3];
                    const int32 PosZ = VertexIndices[4];
                    const int32 NegZ = VertexIndices[5];

                    Builder.AddTriangle(PosZ, PosY, PosX, 0);
                    Builder.AddTriangle(PosZ, NegX, PosY, 0);
                    Builder.AddTriangle(PosZ, NegY, NegX, 0);
                    Builder.AddTriangle(PosZ, PosX, NegY, 0);

                    Builder.AddTriangle(NegZ, PosX, PosY, 0);
                    Builder.AddTriangle(NegZ, PosY, NegX, 0);
                    Builder.AddTriangle(NegZ, NegX, NegY, 0);
                    Builder.AddTriangle(NegZ, NegY, PosX, 0);

                    TriangleCount = 8;
                }
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
