// Copyright 2025 Michael Hall. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "OrbitCameraController.h"
#include "GameFramework/Actor.h"
#include "Editor.h"

/**
 * Milestone 5 Task 1.2: Orbital Camera Test
 *
 * Validates:
 * - Camera orbit rotation (yaw/pitch)
 * - Zoom in/out with distance clamping
 * - Pitch clamping to prevent gimbal lock
 * - Reset to default view
 * - Smooth interpolation
 * - Angle wrapping
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOrbitCameraTest,
    "PlanetaryCreation.Milestone5.OrbitCamera",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOrbitCameraTest::RunTest(const FString& Parameters)
{
    UE_LOG(LogTemp, Log, TEXT("=== Starting Milestone 5 Task 1.2: Orbit Camera Test ==="));

    // Create a dummy target actor
    UWorld* World = GEditor->GetEditorWorldContext().World();
    TestNotNull(TEXT("World should exist"), World);
    if (!World)
    {
        return false;
    }

    AActor* TargetActor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
    TestNotNull(TEXT("Target actor should be created"), TargetActor);
    if (!TargetActor)
    {
        return false;
    }

    // Create camera controller
    FOrbitCameraController CameraController;
    CameraController.Initialize(TargetActor);

    // === Test 1: Initial state ===
    UE_LOG(LogTemp, Log, TEXT("Test 1: Initial state..."));
    const FVector2D InitialAngles = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Initial yaw should be 0"), InitialAngles.X, 0.0);
    TestEqual(TEXT("Initial pitch should be -30"), InitialAngles.Y, -30.0);
    TestEqual(TEXT("Initial distance should be 15000"), CameraController.GetCurrentDistance(), 15000.0f);

    // === Test 2: Rotation (yaw) ===
    UE_LOG(LogTemp, Log, TEXT("Test 2: Yaw rotation..."));
    CameraController.Rotate(45.0f, 0.0f);
    CameraController.Tick(10.0f); // Tick to fully interpolate
    const FVector2D AnglesAfterYaw = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Yaw should be 45 after rotation"), AnglesAfterYaw.X, 45.0);
    TestEqual(TEXT("Pitch should remain -30"), AnglesAfterYaw.Y, -30.0);

    // === Test 3: Rotation (pitch) ===
    UE_LOG(LogTemp, Log, TEXT("Test 3: Pitch rotation..."));
    CameraController.Rotate(0.0f, 20.0f);
    CameraController.Tick(10.0f); // Tick to fully interpolate
    const FVector2D AnglesAfterPitch = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Yaw should remain 45"), AnglesAfterPitch.X, 45.0);
    TestEqual(TEXT("Pitch should be -10 after rotation"), AnglesAfterPitch.Y, -10.0);

    // === Test 4: Pitch clamping (prevent gimbal lock) ===
    UE_LOG(LogTemp, Log, TEXT("Test 4: Pitch clamping..."));
    CameraController.Rotate(0.0f, 100.0f); // Try to pitch beyond limit
    CameraController.Tick(10.0f); // Tick to fully interpolate
    const FVector2D AnglesAfterClamp = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Pitch should clamp to 89"), AnglesAfterClamp.Y, 89.0);

    CameraController.Rotate(0.0f, -200.0f); // Try to pitch below limit
    CameraController.Tick(10.0f); // Tick to fully interpolate
    const FVector2D AnglesAfterClampDown = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Pitch should clamp to -89"), AnglesAfterClampDown.Y, -89.0);

    // === Test 5: Yaw wrapping ===
    UE_LOG(LogTemp, Log, TEXT("Test 5: Yaw wrapping..."));
    CameraController.Rotate(360.0f, 0.0f); // Full rotation
    CameraController.Tick(10.0f); // Tick to fully interpolate
    const FVector2D AnglesAfterWrap = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Yaw should wrap to 45 (45 + 360 = 405 → 45)"), AnglesAfterWrap.X, 45.0);

    CameraController.Rotate(-90.0f, 0.0f); // Negative rotation
    CameraController.Tick(10.0f); // Tick to fully interpolate
    const FVector2D AnglesAfterNegWrap = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Yaw should wrap to 315 (45 - 90 = -45 → 315)"), AnglesAfterNegWrap.X, 315.0);

    // === Test 6: Zoom in ===
    UE_LOG(LogTemp, Log, TEXT("Test 6: Zoom in..."));
    const float InitialDistance = CameraController.GetCurrentDistance();
    CameraController.Zoom(-2000.0f);
    // Note: Distance is interpolated, so we check the target, not current
    CameraController.Tick(1.0f); // Tick to interpolate
    const float DistanceAfterZoomIn = CameraController.GetCurrentDistance();
    TestTrue(TEXT("Distance should decrease after zoom in"), DistanceAfterZoomIn < InitialDistance);

    // === Test 7: Zoom distance clamping (min) ===
    UE_LOG(LogTemp, Log, TEXT("Test 7: Zoom min clamping..."));
    CameraController.SetTargetDistance(5000.0f); // Below min (7000)
    CameraController.Tick(10.0f); // Tick to fully interpolate
    TestEqual(TEXT("Distance should clamp to min 7000"), CameraController.GetCurrentDistance(), 7000.0f);

    // === Test 8: Zoom distance clamping (max) ===
    UE_LOG(LogTemp, Log, TEXT("Test 8: Zoom max clamping..."));
    CameraController.SetTargetDistance(60000.0f); // Above max (50000)
    CameraController.Tick(10.0f); // Tick to fully interpolate
    TestEqual(TEXT("Distance should clamp to max 50000"), CameraController.GetCurrentDistance(), 50000.0f);

    // === Test 9: Reset to default ===
    UE_LOG(LogTemp, Log, TEXT("Test 9: Reset to default..."));
    CameraController.ResetToDefault();
    CameraController.Tick(10.0f); // Tick to fully interpolate
    const FVector2D AnglesAfterReset = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Yaw should reset to 0"), AnglesAfterReset.X, 0.0);
    TestEqual(TEXT("Pitch should reset to -30"), AnglesAfterReset.Y, -30.0);
    TestEqual(TEXT("Distance should reset to 15000"), CameraController.GetCurrentDistance(), 15000.0f);

    // === Test 10: Interpolation speed control ===
    UE_LOG(LogTemp, Log, TEXT("Test 10: Interpolation speed..."));
    CameraController.SetInterpolationSpeed(0.5f);
    TestEqual(TEXT("Interpolation speed should be 0.5"), CameraController.GetInterpolationSpeed(), 0.5f);

    CameraController.SetInterpolationSpeed(1.5f); // Above max
    TestEqual(TEXT("Interpolation speed should clamp to 1.0"), CameraController.GetInterpolationSpeed(), 1.0f);

    CameraController.SetInterpolationSpeed(0.005f); // Below min
    TestEqual(TEXT("Interpolation speed should clamp to 0.01"), CameraController.GetInterpolationSpeed(), 0.01f);

    // === Test 11: Tick returns false when no movement needed ===
    UE_LOG(LogTemp, Log, TEXT("Test 11: Tick return value..."));
    CameraController.ResetToDefault();
    CameraController.Tick(10.0f); // Fully interpolate
    const bool bNeedsUpdate = CameraController.Tick(0.016f); // Check if still needs update
    TestFalse(TEXT("Tick should return false when camera is at target"), bNeedsUpdate);

    // Cleanup
    CameraController.Shutdown();
    World->DestroyActor(TargetActor);

    UE_LOG(LogTemp, Log, TEXT("=== Milestone 5 Task 1.2: Orbit Camera Test PASSED ==="));
    return true;
}
