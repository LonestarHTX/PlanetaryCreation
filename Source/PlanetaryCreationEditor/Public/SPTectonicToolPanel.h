#pragma once

#include "Widgets/SCompoundWidget.h"
#include "TectonicPlaybackController.h"
#include "Templates/Function.h"

class FTectonicSimulationController;
struct FTectonicSimulationParameters;

/** Editor Slate panel exposing tectonic simulation controls. */
class SPTectonicToolPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPTectonicToolPanel) {}
        SLATE_ARGUMENT(TSharedPtr<FTectonicSimulationController>, Controller)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SPTectonicToolPanel();

    // Slate Tick override to update camera
    virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
    FReply HandleStepClicked();
    FReply HandleRegenerateClicked();
    FReply HandleExportMetricsClicked();
    FText GetCurrentTimeLabel() const;
    FText GetPlateCountLabel() const;
    FText GetPerformanceStatsLabel() const; // Milestone 3 Task 4.5

    // Milestone 5 Task 1.1: Playback controls
    FReply HandlePlayClicked();
    FReply HandlePauseClicked();
    FReply HandleStopClicked();
    FText GetPlaybackButtonText() const;
    bool IsPlaybackPlaying() const;
    bool IsPlaybackStopped() const;

    // Playback speed controls
    void OnPlaybackSpeedChanged(float NewValue);
    float GetPlaybackSpeed() const;
    FText GetPlaybackSpeedLabel() const;

    // Timeline scrubber
    void OnTimelineScrubbed(float NewValue);
    float GetTimelineValue() const;
    float GetTimelineMaxValue() const;
    FText GetTimelineLabel() const;

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

    // Automatic LOD toggle (Milestone 4 Phase 4.1)
    ECheckBoxState GetAutomaticLODState() const;
    void OnAutomaticLODChanged(ECheckBoxState NewState);

    // Heightmap visualization toggle (Milestone 6 Task 2.3)
    ECheckBoxState GetHeightmapVisualizationState() const;
    void OnHeightmapVisualizationChanged(ECheckBoxState NewState);

    // Sea level emphasis toggle
    ECheckBoxState GetSeaLevelHighlightState() const;
    void OnSeaLevelHighlightChanged(ECheckBoxState NewState);

    // Surface process toggles (Milestone 6 Stage B)
    ECheckBoxState GetContinentalErosionState() const;
    void OnContinentalErosionChanged(ECheckBoxState NewState);

    ECheckBoxState GetSedimentTransportState() const;
    void OnSedimentTransportChanged(ECheckBoxState NewState);

    ECheckBoxState GetOceanicDampeningState() const;
    void OnOceanicDampeningChanged(ECheckBoxState NewState);

    ECheckBoxState GetOceanicAmplificationState() const;
    void OnOceanicAmplificationChanged(ECheckBoxState NewState);

    ECheckBoxState GetContinentalAmplificationState() const;
    void OnContinentalAmplificationChanged(ECheckBoxState NewState);

    void ApplySurfaceProcessMutation(TFunctionRef<bool(FTectonicSimulationParameters&)> Mutator, const TCHAR* ChangeLabel) const;

    // Milestone 5 Task 1.3: Undo/Redo controls
    FReply HandleUndoClicked();
    FReply HandleRedoClicked();
    bool IsUndoEnabled() const;
    bool IsRedoEnabled() const;
    FText GetHistoryStatusText() const;

    // Milestone 5 Task 1.2: Camera controls
    FReply HandleRotateLeftClicked();
    FReply HandleRotateRightClicked();
    FReply HandleTiltUpClicked();
    FReply HandleTiltDownClicked();
    FReply HandleZoomInClicked();
    FReply HandleZoomOutClicked();
    FReply HandleResetCameraClicked();
    FText GetCameraStatusText() const;

    TWeakPtr<FTectonicSimulationController> ControllerWeak;

    // Milestone 5 Task 1.1: Playback controller
    TUniquePtr<FTectonicPlaybackController> PlaybackController;

    // Cached parameter values (updated on regenerate)
    int32 CachedSeed = 42;
    int32 CachedSubdivisionLevel = 0;
};
