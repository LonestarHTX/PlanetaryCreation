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

void FOrbitCameraController::Initialize(AActor* InTargetActor, double PlanetRadiusMeters)
{
    TargetActor = InTargetActor;

    // Convert planet radius from meters to UE centimeters (1 m = 100 cm)
    PlanetRadiusUE = static_cast<float>(PlanetRadiusMeters * 100.0);

    // Compute distance constraints from planet radius
    RecomputeDistanceConstraints();

    // Initialize to default view
    CurrentYaw = DefaultYaw;
    CurrentPitch = DefaultPitch;
    TargetYaw = DefaultYaw;
    TargetPitch = DefaultPitch;

    UE_LOG(LogTemp, Log, TEXT("FOrbitCameraController::Initialize() - Camera initialized (Radius=%.0f cm, Default=%.0f cm, Min=%.0f cm, Max=%.0f cm)"),
        PlanetRadiusUE, CurrentDistance, MinDistance, MaxDistance);
}

void FOrbitCameraController::SetPlanetRadius(double PlanetRadiusMeters)
{
    PlanetRadiusUE = static_cast<float>(PlanetRadiusMeters * 100.0);
    RecomputeDistanceConstraints();

    // Re-clamp current distances to new constraints
    CurrentDistance = FMath::Clamp(CurrentDistance, MinDistance, MaxDistance);
    TargetDistance = FMath::Clamp(TargetDistance, MinDistance, MaxDistance);

    UE_LOG(LogTemp, Log, TEXT("FOrbitCameraController::SetPlanetRadius() - Updated radius to %.0f cm (Min=%.0f, Max=%.0f)"),
        PlanetRadiusUE, MinDistance, MaxDistance);
}

void FOrbitCameraController::RecomputeDistanceConstraints()
{
    // Conservative max elevation estimate: 10 km = 1,000,000 cm
    // (Real max elevation from stress field ~5-8 km, but use margin for safety)
    const float MaxElevationUE = 1000000.0f;

    // Surface distance = planet radius
    const float SurfaceDistance = PlanetRadiusUE;

    // Safe surface distance = radius + max elevation
    const float SafeSurfaceDistance = PlanetRadiusUE + MaxElevationUE;

    // Default distance: comfortable orbital view at 2× planet radius
    const float DefaultDistance = PlanetRadiusUE * 2.0f;

    // Min distance: 5% above surface to prevent clipping (even with mountains)
    MinDistance = SafeSurfaceDistance * 1.05f;

    // Max distance: distant view at 6× planet radius
    MaxDistance = PlanetRadiusUE * 6.0f;

    // Set current/target to default on first compute
    if (CurrentDistance == 0.0f)
    {
        CurrentDistance = DefaultDistance;
        TargetDistance = DefaultDistance;
    }
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
    // Scale delta relative to current distance for smooth zoom feel
    // Limit delta to ±10% of current distance per input to prevent overshooting
    const float MaxDelta = TargetDistance * 0.1f;
    const float ClampedDelta = FMath::Clamp(DeltaDistance, -MaxDelta, MaxDelta);

    TargetDistance += ClampedDelta;
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

    // Default distance: 2× planet radius for comfortable orbital view
    TargetDistance = PlanetRadiusUE * 2.0f;

    UE_LOG(LogTemp, Log, TEXT("FOrbitCameraController::ResetToDefault() - Camera reset to default view (Distance=%.0f cm)"), TargetDistance);
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

    // Clamp pitch every frame to prevent drift past ±89° (slow updates can overshoot)
    CurrentPitch = FMath::Clamp(CurrentPitch, -89.0f, 89.0f);

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
