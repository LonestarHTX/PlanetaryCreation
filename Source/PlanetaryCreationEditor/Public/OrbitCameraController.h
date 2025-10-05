// Copyright 2025 Michael Hall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;

/**
 * Milestone 5 Task 1.2: Orbital camera controller for smooth planet inspection.
 *
 * Controls camera orbiting around a target point (planet center) with smooth interpolation.
 * Supports mouse drag, keyboard controls, and zoom.
 */
class FOrbitCameraController
{
public:
    FOrbitCameraController();
    ~FOrbitCameraController();

    /** Initialize camera controller with target actor (planet mesh) and planet radius. */
    void Initialize(AActor* InTargetActor, double PlanetRadiusMeters);

    /** Shutdown camera controller. */
    void Shutdown();

    /** Update planet radius and recalculate distance constraints (for parameter changes). */
    void SetPlanetRadius(double PlanetRadiusMeters);

    /** Update camera position (called per frame). Returns true if camera moved. */
    bool Tick(float DeltaTime);

    /** Rotate camera around target (in degrees). */
    void Rotate(float DeltaYaw, float DeltaPitch);

    /** Zoom in/out by delta distance. */
    void Zoom(float DeltaDistance);

    /** Set target distance from planet center. */
    void SetTargetDistance(float Distance);

    /** Get current camera distance. */
    float GetCurrentDistance() const { return CurrentDistance; }

    /** Reset camera to default position. */
    void ResetToDefault();

    /** Get current orbit angles (Yaw, Pitch). */
    FVector2D GetOrbitAngles() const { return FVector2D(CurrentYaw, CurrentPitch); }

    /** Set interpolation speed (0-1, higher = faster). */
    void SetInterpolationSpeed(float Speed) { InterpolationSpeed = FMath::Clamp(Speed, 0.01f, 1.0f); }

    /** Get interpolation speed. */
    float GetInterpolationSpeed() const { return InterpolationSpeed; }

    /** Get minimum distance (for UI/validation). */
    float GetMinDistance() const { return MinDistance; }

    /** Get maximum distance (for UI/validation). */
    float GetMaxDistance() const { return MaxDistance; }

private:
    /** Recompute distance constraints from planet radius. */
    void RecomputeDistanceConstraints();
    /** Calculate desired camera position based on current orbit parameters. */
    FVector CalculateDesiredPosition() const;

    /** Update camera transform with smooth interpolation. */
    void UpdateCameraTransform(float DeltaTime);

    /** Target actor to orbit around (planet mesh). */
    TWeakObjectPtr<AActor> TargetActor;

    /** Current orbit yaw angle (degrees). */
    float CurrentYaw = 0.0f;

    /** Current orbit pitch angle (degrees, clamped -89 to 89). */
    float CurrentPitch = -30.0f;

    /** Target orbit yaw angle (for smooth interpolation). */
    float TargetYaw = 0.0f;

    /** Target orbit pitch angle (for smooth interpolation). */
    float TargetPitch = -30.0f;

    /** Planet radius in UE centimeters (cached from simulation service). */
    float PlanetRadiusUE = 0.0f;

    /** Current distance from target center (UE centimeters).
     * Computed from planet radius: DefaultDistance = PlanetRadius * 2.0 */
    float CurrentDistance = 0.0f;

    /** Target distance from target center (UE centimeters). */
    float TargetDistance = 0.0f;

    /** Interpolation speed (0-1, default 0.15 for smooth feel). */
    float InterpolationSpeed = 0.15f;

    /** Minimum zoom distance (prevent clipping into planet surface + mountains).
     * Computed: (PlanetRadius + MaxElevation) * 1.05 */
    float MinDistance = 0.0f;

    /** Maximum zoom distance (distant orbital view).
     * Computed: PlanetRadius * 6.0 */
    float MaxDistance = 0.0f;

    /** Default view settings for reset. */
    static constexpr float DefaultYaw = 0.0f;
    static constexpr float DefaultPitch = -30.0f;
    /** DefaultDistance computed dynamically: PlanetRadius * 2.0 */
};
