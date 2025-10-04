#pragma once

#include "Widgets/SCompoundWidget.h"

class FTectonicSimulationController;

/** Editor Slate panel exposing tectonic simulation controls. */
class SPTectonicToolPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPTectonicToolPanel) {}
        SLATE_ARGUMENT(TSharedPtr<FTectonicSimulationController>, Controller)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    FReply HandleStepClicked();
    FReply HandleRegenerateClicked();
    FReply HandleExportMetricsClicked();
    FText GetCurrentTimeLabel() const;
    FText GetPlateCountLabel() const;
    FText GetPerformanceStatsLabel() const; // Milestone 3 Task 4.5

    // Parameter accessors for spin boxes
    int32 GetSeedValue() const;
    void OnSeedValueChanged(int32 NewValue);

    int32 GetSubdivisionValue() const;
    void OnSubdivisionValueChanged(int32 NewValue);
    void OnSubdivisionValueCommitted(int32 NewValue, ETextCommit::Type CommitType);

    // Velocity visualization toggle (Milestone 3 Task 2.2)
    ECheckBoxState GetVelocityVisualizationState() const;
    void OnVelocityVisualizationChanged(ECheckBoxState NewState);

    // Elevation mode toggle (Milestone 3 Task 2.4)
    ECheckBoxState GetElevationModeState() const;
    void OnElevationModeChanged(ECheckBoxState NewState);

    // Boundary overlay toggle (Milestone 3 Task 3.2)
    ECheckBoxState GetBoundaryOverlayState() const;
    void OnBoundaryOverlayChanged(ECheckBoxState NewState);

    TWeakPtr<FTectonicSimulationController> ControllerWeak;

    // Cached parameter values (updated on regenerate)
    int32 CachedSeed = 42;
    int32 CachedSubdivisionLevel = 0;
};
