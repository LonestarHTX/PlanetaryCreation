#include "TectonicSimulationController.h"

#include "Async/Future.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "RealtimeMeshComponent/Public/RealtimeMeshActor.h"
#include "RealtimeMeshComponent/Public/RealtimeMeshComponent.h"
#include "RealtimeMeshComponent/Public/RealtimeMeshSimple.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshBuilder.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshSectionConfig.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshStreamRange.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "TectonicSimulationService.h"
#include "UObject/ConstructorHelpers.h"

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
        BuildAndUpdateMesh();
    }
}

void FTectonicSimulationController::RebuildPreview()
{
    BuildAndUpdateMesh();
}

void FTectonicSimulationController::BuildAndUpdateMesh()
{
    if (UTectonicSimulationService* Service = GetService())
    {
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
                // Note: Not enabling PolyGroups to avoid raytracing complexity

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

                // Helper: Apply Rodrigues rotation to a vertex given Euler pole and total rotation
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

                // Build vertices and triangles per-plate (avoid shared vertices for color correctness)
                for (const FTectonicPlate& Plate : Plates)
                {
                    if (Plate.VertexIndices.Num() == 3) // Triangular plate
                    {
                        const FColor PlateColor = GetPlateColor(Plate.PlateID);

                        // Calculate total rotation: AngularVelocity * CurrentTime
                        const double CurrentTimeMy = Service->GetCurrentTimeMy();
                        const double TotalRotation = Plate.AngularVelocity * CurrentTimeMy;

                        // Add 3 vertices for this plate with the plate's color
                        TArray<int32, TInlineAllocator<3>> PlateVerts;
                        for (int32 i = 0; i < 3; ++i)
                        {
                            // Rotate the vertex using the same Euler pole that rotated the centroid
                            const FVector3d& OriginalVertex = SharedVertices[Plate.VertexIndices[i]];
                            const FVector3d RotatedVertex = RotateVertex(OriginalVertex, Plate.EulerPoleAxis, TotalRotation);
                            const FVector3f Position = FVector3f(RotatedVertex * RadiusUnits);
                            const FVector3f Normal = Position.GetSafeNormal();

                            // Calculate tangent for proper lighting
                            const FVector3f UpVector = (FMath::Abs(Normal.Z) > 0.99f) ? FVector3f(1.0f, 0.0f, 0.0f) : FVector3f(0.0f, 0.0f, 1.0f);
                            FVector3f TangentX = FVector3f::CrossProduct(Normal, UpVector).GetSafeNormal();
                            const FVector2f TexCoord((Normal.X + 1.0f) * 0.5f, (Normal.Y + 1.0f) * 0.5f);

                            const int32 VertexId = Builder.AddVertex(Position)
                                .SetNormalAndTangent(Normal, TangentX)
                                .SetColor(PlateColor)
                                .SetTexCoord(TexCoord);

                            PlateVerts.Add(VertexId);
                            VertexCount++;
                        }

                        // Add triangle with CCW winding when viewed from outside (reverse order fixes inside-out)
                        Builder.AddTriangle(PlateVerts[0], PlateVerts[2], PlateVerts[1]);
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

    // Clean up stale actor if it exists but weak pointer is invalid
    if (!PreviewActor.IsValid())
    {
        for (TActorIterator<ARealtimeMeshActor> It(World); It; ++It)
        {
            if (It->GetActorLabel() == TEXT("TectonicPreviewActor"))
            {
                World->DestroyActor(*It);
                break;
            }
        }
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

        // Disable raytracing and expensive rendering features for editor preview
        Component->SetCastShadow(false);
        Component->SetVisibleInRayTracing(false);
        Component->SetAffectDistanceFieldLighting(false);
        Component->SetAffectDynamicIndirectLighting(false);

        if (URealtimeMeshSimple* Mesh = Component->InitializeRealtimeMesh<URealtimeMeshSimple>())
        {
            Mesh->SetupMaterialSlot(0, TEXT("TectonicPreview"));

            // Create simple unlit material that displays vertex colors
            UMaterial* VertexColorMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
            VertexColorMaterial->MaterialDomain = EMaterialDomain::MD_Surface;
            VertexColorMaterial->SetShadingModel(EMaterialShadingModel::MSM_Unlit);

            UMaterialExpressionVertexColor* VertexColorNode = NewObject<UMaterialExpressionVertexColor>(VertexColorMaterial);
            VertexColorMaterial->GetExpressionCollection().AddExpression(VertexColorNode);
            VertexColorMaterial->GetEditorOnlyData()->EmissiveColor.Expression = VertexColorNode;

            VertexColorMaterial->PostEditChange();

            Component->SetMaterial(0, VertexColorMaterial);

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
