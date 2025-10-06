// Copyright 2025 Michael Hall. All Rights Reserved.

#include "PlanetaryCreationLogging.h"
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
    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Starting Milestone 5 Task 1.2: Orbit Camera Test ==="));

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

    // Create camera controller with 1/50 Earth scale (127,400 m = 12,740,000 cm)
    const double PlanetRadiusMeters = 127400.0;
    const float PlanetRadiusUE = static_cast<float>(PlanetRadiusMeters * 100.0); // 12,740,000 cm
    FOrbitCameraController CameraController;
    CameraController.Initialize(TargetActor, PlanetRadiusMeters);

    // === Test 1: Initial state ===
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 1: Initial state..."));
    const FVector2D InitialAngles = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Initial yaw should be 0"), InitialAngles.X, 0.0);
    TestEqual(TEXT("Initial pitch should be -30"), InitialAngles.Y, -30.0);

    // Default distance should be 2× planet radius
    const float ExpectedDefaultDistance = PlanetRadiusUE * 2.0f;
    TestEqual(TEXT("Initial distance should be 2× planet radius"), CameraController.GetCurrentDistance(), ExpectedDefaultDistance);

    // === Test 2: Rotation (yaw) ===
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 2: Yaw rotation..."));
    CameraController.Rotate(45.0f, 0.0f);
    CameraController.Tick(10.0f); // Tick to fully interpolate
    const FVector2D AnglesAfterYaw = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Yaw should be 45 after rotation"), AnglesAfterYaw.X, 45.0);
    TestEqual(TEXT("Pitch should remain -30"), AnglesAfterYaw.Y, -30.0);

    // === Test 3: Rotation (pitch) ===
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 3: Pitch rotation..."));
    CameraController.Rotate(0.0f, 20.0f);
    CameraController.Tick(10.0f); // Tick to fully interpolate
    const FVector2D AnglesAfterPitch = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Yaw should remain 45"), AnglesAfterPitch.X, 45.0);
    TestEqual(TEXT("Pitch should be -10 after rotation"), AnglesAfterPitch.Y, -10.0);

    // === Test 4: Pitch clamping (prevent gimbal lock) ===
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 4: Pitch clamping..."));
    CameraController.Rotate(0.0f, 100.0f); // Try to pitch beyond limit
    CameraController.Tick(10.0f); // Tick to fully interpolate
    const FVector2D AnglesAfterClamp = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Pitch should clamp to 89"), AnglesAfterClamp.Y, 89.0);

    CameraController.Rotate(0.0f, -200.0f); // Try to pitch below limit
    CameraController.Tick(10.0f); // Tick to fully interpolate
    const FVector2D AnglesAfterClampDown = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Pitch should clamp to -89"), AnglesAfterClampDown.Y, -89.0);

    // === Test 5: Yaw wrapping ===
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 5: Yaw wrapping..."));
    CameraController.Rotate(360.0f, 0.0f); // Full rotation
    CameraController.Tick(10.0f); // Tick to fully interpolate
    const FVector2D AnglesAfterWrap = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Yaw should wrap to 45 (45 + 360 = 405 → 45)"), AnglesAfterWrap.X, 45.0);

    CameraController.Rotate(-90.0f, 0.0f); // Negative rotation
    CameraController.Tick(10.0f); // Tick to fully interpolate
    const FVector2D AnglesAfterNegWrap = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Yaw should wrap to 315 (45 - 90 = -45 → 315)"), AnglesAfterNegWrap.X, 315.0);

    // === Test 6: Zoom in ===
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 6: Zoom in..."));
    const float InitialDistance = CameraController.GetCurrentDistance();
    CameraController.Zoom(-2000.0f);
    // Note: Distance is interpolated, so we check the target, not current
    CameraController.Tick(1.0f); // Tick to interpolate
    const float DistanceAfterZoomIn = CameraController.GetCurrentDistance();
    TestTrue(TEXT("Distance should decrease after zoom in"), DistanceAfterZoomIn < InitialDistance);

    // === Test 7: Zoom distance clamping (min) ===
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 7: Zoom min clamping..."));
    const float MinDistance = CameraController.GetMinDistance();
    CameraController.SetTargetDistance(MinDistance - 1000000.0f); // Try to go below min
    CameraController.Tick(10.0f); // Tick to fully interpolate
    TestEqual(TEXT("Distance should clamp to computed min"), CameraController.GetCurrentDistance(), MinDistance);

    // === Test 8: Zoom distance clamping (max) ===
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 8: Zoom max clamping..."));
    const float MaxDistance = CameraController.GetMaxDistance();
    CameraController.SetTargetDistance(MaxDistance + 1000000.0f); // Try to go above max
    CameraController.Tick(10.0f); // Tick to fully interpolate
    TestEqual(TEXT("Distance should clamp to computed max"), CameraController.GetCurrentDistance(), MaxDistance);

    // === Test 9: Distance constraints derived from radius ===
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 9: Distance constraints from planet radius..."));
    // Min = (Radius + 1M cm elevation) * 1.05
    const float ExpectedMin = (PlanetRadiusUE + 1000000.0f) * 1.05f;
    TestEqual(TEXT("Min distance should be (Radius + MaxElevation) * 1.05"), MinDistance, ExpectedMin);

    // Max = Radius * 6.0
    const float ExpectedMax = PlanetRadiusUE * 6.0f;
    TestEqual(TEXT("Max distance should be Radius * 6.0"), MaxDistance, ExpectedMax);

    // === Test 10: Reset to default ===
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 10: Reset to default..."));
    CameraController.ResetToDefault();
    CameraController.Tick(10.0f); // Tick to fully interpolate
    const FVector2D AnglesAfterReset = CameraController.GetOrbitAngles();
    TestEqual(TEXT("Yaw should reset to 0"), AnglesAfterReset.X, 0.0);
    TestEqual(TEXT("Pitch should reset to -30"), AnglesAfterReset.Y, -30.0);
    TestEqual(TEXT("Distance should reset to 2× radius"), CameraController.GetCurrentDistance(), ExpectedDefaultDistance);

    // === Test 11: Interpolation speed control ===
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 11: Interpolation speed..."));
    CameraController.SetInterpolationSpeed(0.5f);
    TestEqual(TEXT("Interpolation speed should be 0.5"), CameraController.GetInterpolationSpeed(), 0.5f);

    CameraController.SetInterpolationSpeed(1.5f); // Above max
    TestEqual(TEXT("Interpolation speed should clamp to 1.0"), CameraController.GetInterpolationSpeed(), 1.0f);

    CameraController.SetInterpolationSpeed(0.005f); // Below min
    TestEqual(TEXT("Interpolation speed should clamp to 0.01"), CameraController.GetInterpolationSpeed(), 0.01f);

    // === Test 12: Zoom delta scaling (prevents overshooting) ===
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 12: Zoom delta scaling..."));
    CameraController.ResetToDefault();
    CameraController.Tick(10.0f); // Fully reset
    const float DistanceBeforeZoom = CameraController.GetCurrentDistance();
    const float HugeZoomDelta = DistanceBeforeZoom * 5.0f; // Try to zoom 5× distance
    CameraController.Zoom(HugeZoomDelta);
    // Delta should be clamped to 10% of current distance
    const float ExpectedDelta = DistanceBeforeZoom * 0.1f;
    CameraController.Tick(10.0f); // Fully interpolate
    const float DistanceAfterZoom = CameraController.GetCurrentDistance();
    const float ActualDelta = DistanceAfterZoom - DistanceBeforeZoom;
    TestTrue(TEXT("Zoom delta should be clamped to ±10% of current distance"), FMath::Abs(ActualDelta) <= ExpectedDelta * 1.01f);

    // === Test 13: Pitch clamping every frame (prevents drift) ===
    UE_LOG(LogPlanetaryCreation, Log, TEXT("Test 13: Pitch clamping every frame..."));
    CameraController.ResetToDefault(); // Start from -30 pitch
    CameraController.Tick(10.0f); // Fully reset
    CameraController.Rotate(0.0f, 120.0f); // Pitch to +89 limit (from -30, so +120 → +90, clamped to +89)
    CameraController.Tick(10.0f); // Fully interpolate
    const float PitchAfterClamp = CameraController.GetOrbitAngles().Y;
    TestEqual(TEXT("Pitch should clamp to +89"), PitchAfterClamp, 89.0f);
    // Simulate slow interpolation that would overshoot without per-frame clamping
    CameraController.Rotate(0.0f, 5.0f); // Try to go beyond
    for (int32 i = 0; i < 100; ++i)
    {
        CameraController.Tick(0.001f); // Many small ticks
    }
    TestTrue(TEXT("Pitch should never exceed ±89 even with slow updates"), FMath::Abs(CameraController.GetOrbitAngles().Y) <= 89.0f);

    // Cleanup
    CameraController.Shutdown();
    World->DestroyActor(TargetActor);

    UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Milestone 5 Task 1.2: Orbit Camera Test PASSED ==="));
    return true;
}
