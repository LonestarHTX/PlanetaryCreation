// Copyright 2025 Michael Hall. All Rights Reserved.

#include "OrbitCameraController.h"
#include "GameFramework/Actor.h"
#include "Camera/CameraComponent.h"
#include "Editor.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"

FOrbitCameraController::FOrbitCameraController()
{
}

FOrbitCameraController::~FOrbitCameraController()
{
    Shutdown();
}

void FOrbitCameraController::Initialize(AActor* InTargetActor)
{
    TargetActor = InTargetActor;

    // Initialize to default view
    CurrentYaw = DefaultYaw;
    CurrentPitch = DefaultPitch;
    CurrentDistance = DefaultDistance;
    TargetYaw = DefaultYaw;
    TargetPitch = DefaultPitch;
    TargetDistance = DefaultDistance;

    UE_LOG(LogTemp, Log, TEXT("FOrbitCameraController::Initialize() - Camera controller initialized"));
}

void FOrbitCameraController::Shutdown()
{
    TargetActor.Reset();
}

bool FOrbitCameraController::Tick(float DeltaTime)
{
    if (!TargetActor.IsValid())
    {
        return false;
    }

    // Check if camera needs to move
    const bool bNeedsUpdate =
        !FMath::IsNearlyEqual(CurrentYaw, TargetYaw, 0.1f) ||
        !FMath::IsNearlyEqual(CurrentPitch, TargetPitch, 0.1f) ||
        !FMath::IsNearlyEqual(CurrentDistance, TargetDistance, 1.0f);

    if (bNeedsUpdate)
    {
        UpdateCameraTransform(DeltaTime);
        return true;
    }

    return false;
}

void FOrbitCameraController::Rotate(float DeltaYaw, float DeltaPitch)
{
    TargetYaw += DeltaYaw;
    TargetPitch += DeltaPitch;

    // Normalize yaw to 0-360 range
    while (TargetYaw >= 360.0f) TargetYaw -= 360.0f;
    while (TargetYaw < 0.0f) TargetYaw += 360.0f;

    // Clamp pitch to prevent gimbal lock
    TargetPitch = FMath::Clamp(TargetPitch, -89.0f, 89.0f);
}

void FOrbitCameraController::Zoom(float DeltaDistance)
{
    TargetDistance += DeltaDistance;
    TargetDistance = FMath::Clamp(TargetDistance, MinDistance, MaxDistance);
}

void FOrbitCameraController::SetTargetDistance(float Distance)
{
    TargetDistance = FMath::Clamp(Distance, MinDistance, MaxDistance);
}

void FOrbitCameraController::ResetToDefault()
{
    TargetYaw = DefaultYaw;
    TargetPitch = DefaultPitch;
    TargetDistance = DefaultDistance;

    UE_LOG(LogTemp, Log, TEXT("FOrbitCameraController::ResetToDefault() - Camera reset to default view"));
}

FVector FOrbitCameraController::CalculateDesiredPosition() const
{
    if (!TargetActor.IsValid())
    {
        return FVector::ZeroVector;
    }

    // Get planet center position
    const FVector TargetLocation = TargetActor->GetActorLocation();

    // Convert spherical coordinates (yaw, pitch, distance) to Cartesian
    // Yaw rotates around Z axis, pitch rotates up/down
    const float YawRad = FMath::DegreesToRadians(CurrentYaw);
    const float PitchRad = FMath::DegreesToRadians(CurrentPitch);

    // Calculate camera offset from target
    const float CosPitch = FMath::Cos(PitchRad);
    const FVector Offset(
        CosPitch * FMath::Cos(YawRad) * CurrentDistance,
        CosPitch * FMath::Sin(YawRad) * CurrentDistance,
        FMath::Sin(PitchRad) * CurrentDistance
    );

    return TargetLocation + Offset;
}

void FOrbitCameraController::UpdateCameraTransform(float DeltaTime)
{
    if (!TargetActor.IsValid())
    {
        return;
    }

    // Smooth interpolation of orbit parameters with yaw wrap-around handling
    // Calculate shortest angular distance for yaw interpolation
    float YawDelta = TargetYaw - CurrentYaw;
    if (YawDelta > 180.0f)
    {
        YawDelta -= 360.0f; // Wrap around the other way
    }
    else if (YawDelta < -180.0f)
    {
        YawDelta += 360.0f; // Wrap around the other way
    }

    CurrentYaw += YawDelta * FMath::Clamp(DeltaTime * InterpolationSpeed * 10.0f, 0.0f, 1.0f);

    // Normalize yaw to 0-360 range
    while (CurrentYaw >= 360.0f) CurrentYaw -= 360.0f;
    while (CurrentYaw < 0.0f) CurrentYaw += 360.0f;

    CurrentPitch = FMath::FInterpTo(CurrentPitch, TargetPitch, DeltaTime, InterpolationSpeed * 10.0f);
    CurrentDistance = FMath::FInterpTo(CurrentDistance, TargetDistance, DeltaTime, InterpolationSpeed * 5.0f);

    // Calculate new camera position
    const FVector CameraPosition = CalculateDesiredPosition();
    const FVector TargetLocation = TargetActor->GetActorLocation();

    // Calculate look-at rotation (camera always looks at planet center)
    const FRotator CameraRotation = (TargetLocation - CameraPosition).Rotation();

    // Update the editor viewport camera
#if WITH_EDITOR
    if (GEditor && GUnrealEd)
    {
        // Get the active level editor viewport
        for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
        {
            if (ViewportClient && ViewportClient->IsPerspective())
            {
                // Set viewport camera location and rotation
                ViewportClient->SetViewLocation(CameraPosition);
                ViewportClient->SetViewRotation(CameraRotation);
                ViewportClient->Invalidate();
                break; // Only update the first perspective viewport
            }
        }
    }
#endif
}
