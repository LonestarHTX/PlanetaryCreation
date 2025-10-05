#include "SPTectonicToolPanel.h"

#include "TectonicSimulationController.h"
#include "TectonicSimulationService.h"
#include "TectonicPlaybackController.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

SPTectonicToolPanel::~SPTectonicToolPanel()
{
    if (PlaybackController)
    {
        PlaybackController->Shutdown();
    }
}

void SPTectonicToolPanel::Construct(const FArguments& InArgs)
{
    ControllerWeak = InArgs._Controller;

    // Milestone 5 Task 1.1: Initialize playback controller
    PlaybackController = MakeUnique<FTectonicPlaybackController>();
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        PlaybackController->Initialize(Controller);
    }

    // Initialize cached parameters from service
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            CachedSeed = Service->GetParameters().Seed;
            CachedSubdivisionLevel = Service->GetParameters().RenderSubdivisionLevel;
        }
    }

    ChildSlot
    [
        SNew(SBorder)
        .Padding(12.0f)
        [
            SNew(SVerticalBox)

            // Time display
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(this, &SPTectonicToolPanel::GetCurrentTimeLabel)
            ]

            // Plate count display
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(this, &SPTectonicToolPanel::GetPlateCountLabel)
            ]

            // Performance stats (Milestone 3 Task 4.5)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(this, &SPTectonicToolPanel::GetPerformanceStatsLabel)
                .ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
            ]

            // Seed input
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 12.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("PlanetaryCreation", "SeedLabel", "Seed:"))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(SSpinBox<int32>)
                    .Value(this, &SPTectonicToolPanel::GetSeedValue)
                    .OnValueChanged(this, &SPTectonicToolPanel::OnSeedValueChanged)
                    .MinValue(0)
                    .MaxValue(999999)
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "SeedTooltip", "Random seed for deterministic plate generation (Paper Section 2.1)"))
                ]
            ]

            // Render subdivision level
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("PlanetaryCreation", "SubdivisionLabel", "Render Detail:"))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(SSpinBox<int32>)
                    .Value(this, &SPTectonicToolPanel::GetSubdivisionValue)
                    .OnValueChanged(this, &SPTectonicToolPanel::OnSubdivisionValueChanged)
                    .OnValueCommitted(this, &SPTectonicToolPanel::OnSubdivisionValueCommitted)
                    .MinValue(0)
                    .MaxValue(8)
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "SubdivisionTooltip", "Render mesh density (0=20, 1=80, 2=320, 3=1280, 4=5120, 5=20480, 6=81920, 7=327680, 8=1.3M faces)"))
                ]
            ]

            // Regenerate button
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SButton)
                .Text(NSLOCTEXT("PlanetaryCreation", "RegenerateButtonLabel", "Regenerate Plates"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "RegenerateButtonTooltip", "Reset simulation with current seed and regenerate plate layout"))
                .OnClicked(this, &SPTectonicToolPanel::HandleRegenerateClicked)
            ]

            // Step button
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 12.0f)
            [
                SNew(SButton)
                .Text(NSLOCTEXT("PlanetaryCreation", "StepButtonLabel", "Step (2 My)"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "StepButtonTooltip", "Advance the tectonic simulation by one iteration (2 My)."))
                .OnClicked(this, &SPTectonicToolPanel::HandleStepClicked)
            ]

            // Milestone 5 Task 1.3: Undo/Redo buttons
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 8.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(0.5f)
                .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(NSLOCTEXT("PlanetaryCreation", "UndoButtonLabel", "Undo (Ctrl+Z)"))
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "UndoButtonTooltip", "Undo the last simulation step"))
                    .IsEnabled(this, &SPTectonicToolPanel::IsUndoEnabled)
                    .OnClicked(this, &SPTectonicToolPanel::HandleUndoClicked)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(0.5f)
                .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(NSLOCTEXT("PlanetaryCreation", "RedoButtonLabel", "Redo (Ctrl+Y)"))
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "RedoButtonTooltip", "Redo the next simulation step"))
                    .IsEnabled(this, &SPTectonicToolPanel::IsRedoEnabled)
                    .OnClicked(this, &SPTectonicToolPanel::HandleRedoClicked)
                ]
            ]

            // History status text
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(STextBlock)
                .Text(this, &SPTectonicToolPanel::GetHistoryStatusText)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
            ]

            // Milestone 5 Task 1.2: Camera controls separator
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 12.0f)
            [
                SNew(SSeparator)
            ]

            // Camera controls label
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "CameraControlsLabel", "Camera Controls"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
            ]

            // Camera status text
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(STextBlock)
                .Text(this, &SPTectonicToolPanel::GetCameraStatusText)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
            ]

            // Rotation controls (Left/Right)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(0.5f)
                .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(NSLOCTEXT("PlanetaryCreation", "RotateLeftButton", "← Rotate Left"))
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "RotateLeftTooltip", "Rotate camera 15° left"))
                    .OnClicked(this, &SPTectonicToolPanel::HandleRotateLeftClicked)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(0.5f)
                .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(NSLOCTEXT("PlanetaryCreation", "RotateRightButton", "Rotate Right →"))
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "RotateRightTooltip", "Rotate camera 15° right"))
                    .OnClicked(this, &SPTectonicToolPanel::HandleRotateRightClicked)
                ]
            ]

            // Tilt controls (Up/Down)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(0.5f)
                .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(NSLOCTEXT("PlanetaryCreation", "TiltUpButton", "↑ Tilt Up"))
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "TiltUpTooltip", "Tilt camera 10° up"))
                    .OnClicked(this, &SPTectonicToolPanel::HandleTiltUpClicked)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(0.5f)
                .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(NSLOCTEXT("PlanetaryCreation", "TiltDownButton", "↓ Tilt Down"))
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "TiltDownTooltip", "Tilt camera 10° down"))
                    .OnClicked(this, &SPTectonicToolPanel::HandleTiltDownClicked)
                ]
            ]

            // Zoom controls (In/Out)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(0.5f)
                .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(NSLOCTEXT("PlanetaryCreation", "ZoomInButton", "+ Zoom In"))
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "ZoomInTooltip", "Zoom in 1.5M km"))
                    .OnClicked(this, &SPTectonicToolPanel::HandleZoomInClicked)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(0.5f)
                .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(NSLOCTEXT("PlanetaryCreation", "ZoomOutButton", "- Zoom Out"))
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "ZoomOutTooltip", "Zoom out 1.5M km"))
                    .OnClicked(this, &SPTectonicToolPanel::HandleZoomOutClicked)
                ]
            ]

            // Reset camera button
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SButton)
                .Text(NSLOCTEXT("PlanetaryCreation", "ResetCameraButton", "Reset Camera"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "ResetCameraTooltip", "Reset camera to default view"))
                .OnClicked(this, &SPTectonicToolPanel::HandleResetCameraClicked)
            ]

            // Milestone 5 Task 1.1: Playback controls separator
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 12.0f)
            [
                SNew(SSeparator)
            ]

            // Playback controls label
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "PlaybackControlsLabel", "Continuous Playback"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
            ]

            // Play/Pause/Stop buttons
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .Padding(0.0f, 0.0f, 2.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(this, &SPTectonicToolPanel::GetPlaybackButtonText)
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "PlayPauseTooltip", "Start/pause continuous playback (Space)"))
                    .OnClicked(this, &SPTectonicToolPanel::HandlePlayClicked)
                    // Play button is always enabled (can start from Stopped or resume from Paused)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .Padding(2.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(NSLOCTEXT("PlanetaryCreation", "StopButtonLabel", "Stop"))
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "StopTooltip", "Stop playback and reset"))
                    .OnClicked(this, &SPTectonicToolPanel::HandleStopClicked)
                ]
            ]

            // Playback speed control
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(this, &SPTectonicToolPanel::GetPlaybackSpeedLabel)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(SSlider)
                    .Value(this, &SPTectonicToolPanel::GetPlaybackSpeed)
                    .OnValueChanged(this, &SPTectonicToolPanel::OnPlaybackSpeedChanged)
                    .MinValue(0.5f)
                    .MaxValue(10.0f)
                    .StepSize(0.5f)
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "PlaybackSpeedTooltip", "Adjust playback speed (0.5× to 10×)"))
                ]
            ]

            // Timeline scrubber
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 8.0f)
            [
                SNew(STextBlock)
                .Text(this, &SPTectonicToolPanel::GetTimelineLabel)
                .ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f)))
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 2.0f)
            [
                SNew(SSlider)
                .Value(this, &SPTectonicToolPanel::GetTimelineValue)
                .OnValueChanged(this, &SPTectonicToolPanel::OnTimelineScrubbed)
                .MinValue(0.0f)
                .MaxValue(1000.0f)
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "TimelineScrubberTooltip", "Jump to any point in simulation history (← / →)"))
            ]

            // Export metrics button
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SButton)
                .Text(NSLOCTEXT("PlanetaryCreation", "ExportMetricsLabel", "Export Metrics CSV"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "ExportMetricsTooltip", "Export current simulation state to Saved/TectonicMetrics/ for analysis"))
                .OnClicked(this, &SPTectonicToolPanel::HandleExportMetricsClicked)
            ]

            // Velocity visualization toggle (Milestone 3 Task 2.2)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 12.0f)
            [
                SNew(SCheckBox)
                .IsChecked(this, &SPTectonicToolPanel::GetVelocityVisualizationState)
                .OnCheckStateChanged(this, &SPTectonicToolPanel::OnVelocityVisualizationChanged)
                .Content()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("PlanetaryCreation", "VelocityOverlayLabel", "Show Velocity Field"))
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "VelocityOverlayTooltip", "Visualize plate velocity magnitude as vertex colors (blue=slow, red=fast)"))
                ]
            ]

            // Elevation mode toggle (Milestone 3 Task 2.4)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SCheckBox)
                .IsChecked(this, &SPTectonicToolPanel::GetElevationModeState)
                .OnCheckStateChanged(this, &SPTectonicToolPanel::OnElevationModeChanged)
                .Content()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("PlanetaryCreation", "ElevationModeLabel", "Displaced Elevation"))
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "ElevationModeTooltip", "Enable geometric displacement from stress field (green=0 MPa → red=100 MPa). Unchecked = flat color-only mode."))
                ]
            ]

            // Boundary overlay toggle (Milestone 3 Task 3.2)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SCheckBox)
                .IsChecked(this, &SPTectonicToolPanel::GetBoundaryOverlayState)
                .OnCheckStateChanged(this, &SPTectonicToolPanel::OnBoundaryOverlayChanged)
                .Content()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("PlanetaryCreation", "BoundaryOverlayLabel", "Show Plate Boundaries"))
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "BoundaryOverlayTooltip", "Visualize plate boundaries as colored lines (red=convergent, green=divergent, yellow=transform)"))
                ]
            ]

            // Automatic LOD toggle (Milestone 4 Phase 4.1)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SCheckBox)
                .IsChecked(this, &SPTectonicToolPanel::GetAutomaticLODState)
                .OnCheckStateChanged(this, &SPTectonicToolPanel::OnAutomaticLODChanged)
                .Content()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("PlanetaryCreation", "AutomaticLODLabel", "Automatic LOD"))
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "AutomaticLODTooltip", "Automatically adjust render detail based on camera distance. Disable to manually control LOD."))
                ]
            ]

            // Heightmap visualization toggle (Milestone 6 Task 2.3)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SCheckBox)
                .IsChecked(this, &SPTectonicToolPanel::GetHeightmapVisualizationState)
                .OnCheckStateChanged(this, &SPTectonicToolPanel::OnHeightmapVisualizationChanged)
                .Content()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("PlanetaryCreation", "HeightmapVisualizationLabel", "Heightmap Visualization"))
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "HeightmapVisualizationTooltip", "Color vertices by elevation: blue (deep ocean -6km) → cyan → green (sea level) → yellow → red (mountains +2km)"))
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "BatchHint", "Batch stepping and fast-forward presets will arrive in later milestones."))
                .WrapTextAt(340.0f)
                .ColorAndOpacity(FSlateColor(FLinearColor::Gray))
            ]
        ]
    ];
}

FReply SPTectonicToolPanel::HandleStepClicked()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        Controller->StepSimulation(1);
    }
    return FReply::Handled();
}

FReply SPTectonicToolPanel::HandleRegenerateClicked()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            FTectonicSimulationParameters NewParams = Service->GetParameters();
            NewParams.Seed = CachedSeed;
            Service->SetParameters(NewParams);

            // Refresh preview mesh without advancing time
            Controller->RebuildPreview();

            UE_LOG(LogTemp, Log, TEXT("Regenerated plates with seed %d"), CachedSeed);
        }
    }
    return FReply::Handled();
}

FText SPTectonicToolPanel::GetCurrentTimeLabel() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        const double CurrentTime = Controller->GetCurrentTimeMy();
        return FText::Format(NSLOCTEXT("PlanetaryCreation", "CurrentTimeLabel", "Current Time: {0} My"), FText::AsNumber(CurrentTime));
    }

    return NSLOCTEXT("PlanetaryCreation", "CurrentTimeUnavailable", "Current Time: n/a");
}

FText SPTectonicToolPanel::GetPlateCountLabel() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            const int32 PlateCount = Service->GetPlates().Num();
            return FText::Format(NSLOCTEXT("PlanetaryCreation", "PlateCountLabel", "Plates: {0}"), FText::AsNumber(PlateCount));
        }
    }

    return NSLOCTEXT("PlanetaryCreation", "PlateCountUnavailable", "Plates: n/a");
}

FText SPTectonicToolPanel::GetPerformanceStatsLabel() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            const double StepTimeMs = Service->GetLastStepTimeMs();
            const int32 VertexCount = Service->GetRenderVertices().Num();
            const int32 TriangleCount = Service->GetRenderTriangles().Num() / 3;

            return FText::Format(
                NSLOCTEXT("PlanetaryCreation", "PerfStatsLabel", "Step: {0}ms | Verts: {1} | Tris: {2}"),
                FText::AsNumber(FMath::RoundToInt(StepTimeMs)),
                FText::AsNumber(VertexCount),
                FText::AsNumber(TriangleCount)
            );
        }
    }

    return NSLOCTEXT("PlanetaryCreation", "PerfStatsUnavailable", "Performance: n/a");
}

int32 SPTectonicToolPanel::GetSeedValue() const
{
    return CachedSeed;
}

void SPTectonicToolPanel::OnSeedValueChanged(int32 NewValue)
{
    CachedSeed = NewValue;
}

FReply SPTectonicToolPanel::HandleExportMetricsClicked()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            Service->ExportMetricsToCSV();
        }
    }
    return FReply::Handled();
}

int32 SPTectonicToolPanel::GetSubdivisionValue() const
{
    return CachedSubdivisionLevel;
}

void SPTectonicToolPanel::OnSubdivisionValueChanged(int32 NewValue)
{
    CachedSubdivisionLevel = NewValue;
}

void SPTectonicToolPanel::OnSubdivisionValueCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
    CachedSubdivisionLevel = NewValue;

    // Apply the new subdivision level
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            FTectonicSimulationParameters NewParams = Service->GetParameters();
            NewParams.RenderSubdivisionLevel = CachedSubdivisionLevel;
            Service->SetParameters(NewParams);

            // Refresh preview mesh
            Controller->RebuildPreview();

            UE_LOG(LogTemp, Log, TEXT("Updated render subdivision level to %d"), CachedSubdivisionLevel);
        }
    }
}

ECheckBoxState SPTectonicToolPanel::GetVelocityVisualizationState() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        return Controller->IsVelocityVisualizationEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
    }
    return ECheckBoxState::Unchecked;
}

void SPTectonicToolPanel::OnVelocityVisualizationChanged(ECheckBoxState NewState)
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        const bool bEnabled = (NewState == ECheckBoxState::Checked);
        Controller->SetVelocityVisualizationEnabled(bEnabled);
        UE_LOG(LogTemp, Log, TEXT("Velocity visualization %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
    }
}

ECheckBoxState SPTectonicToolPanel::GetElevationModeState() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        return Controller->GetElevationMode() == EElevationMode::Displaced ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
    }
    return ECheckBoxState::Unchecked;
}

void SPTectonicToolPanel::OnElevationModeChanged(ECheckBoxState NewState)
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        const EElevationMode Mode = (NewState == ECheckBoxState::Checked) ? EElevationMode::Displaced : EElevationMode::Flat;
        Controller->SetElevationMode(Mode);
        UE_LOG(LogTemp, Log, TEXT("Elevation mode: %s"), Mode == EElevationMode::Displaced ? TEXT("Displaced") : TEXT("Flat"));
    }
}

ECheckBoxState SPTectonicToolPanel::GetBoundaryOverlayState() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        return Controller->AreBoundariesVisible() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
    }
    return ECheckBoxState::Unchecked;
}

void SPTectonicToolPanel::OnBoundaryOverlayChanged(ECheckBoxState NewState)
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        const bool bVisible = (NewState == ECheckBoxState::Checked);
        Controller->SetBoundariesVisible(bVisible);
        UE_LOG(LogTemp, Log, TEXT("Boundary overlay %s"), bVisible ? TEXT("visible") : TEXT("hidden"));
    }
}

ECheckBoxState SPTectonicToolPanel::GetAutomaticLODState() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            return Service->GetParameters().bEnableAutomaticLOD ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
        }
    }
    return ECheckBoxState::Checked; // Default to checked
}

void SPTectonicToolPanel::OnAutomaticLODChanged(ECheckBoxState NewState)
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            const bool bEnabled = (NewState == ECheckBoxState::Checked);
            Service->SetAutomaticLODEnabled(bEnabled);
            UE_LOG(LogTemp, Log, TEXT("Automatic LOD %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
        }
    }
}

ECheckBoxState SPTectonicToolPanel::GetHeightmapVisualizationState() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            return Service->GetParameters().bEnableHeightmapVisualization ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
        }
    }
    return ECheckBoxState::Unchecked; // Default to unchecked
}

void SPTectonicToolPanel::OnHeightmapVisualizationChanged(ECheckBoxState NewState)
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            const bool bEnabled = (NewState == ECheckBoxState::Checked);
            Service->SetHeightmapVisualizationEnabled(bEnabled);
            UE_LOG(LogTemp, Log, TEXT("Heightmap visualization %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));

            // Trigger mesh refresh to apply new coloring immediately
            Controller->RebuildPreview();
        }
    }
}

// Milestone 5 Task 1.1: Playback control handlers

FReply SPTectonicToolPanel::HandlePlayClicked()
{
    if (!PlaybackController)
    {
        return FReply::Handled();
    }

    if (PlaybackController->IsPlaying())
    {
        PlaybackController->Pause();
        UE_LOG(LogTemp, Log, TEXT("Playback paused"));
    }
    else
    {
        PlaybackController->Play();
        UE_LOG(LogTemp, Log, TEXT("Playback started"));
    }

    return FReply::Handled();
}

FReply SPTectonicToolPanel::HandlePauseClicked()
{
    if (PlaybackController)
    {
        PlaybackController->Pause();
    }
    return FReply::Handled();
}

FReply SPTectonicToolPanel::HandleStopClicked()
{
    if (PlaybackController)
    {
        PlaybackController->Stop();
        UE_LOG(LogTemp, Log, TEXT("Playback stopped"));
    }
    return FReply::Handled();
}

FText SPTectonicToolPanel::GetPlaybackButtonText() const
{
    if (PlaybackController && PlaybackController->IsPlaying())
    {
        return NSLOCTEXT("PlanetaryCreation", "PauseButtonLabel", "Pause");
    }
    return NSLOCTEXT("PlanetaryCreation", "PlayButtonLabel", "Play");
}

bool SPTectonicToolPanel::IsPlaybackPlaying() const
{
    return PlaybackController && PlaybackController->IsPlaying();
}

bool SPTectonicToolPanel::IsPlaybackStopped() const
{
    return !PlaybackController || PlaybackController->GetPlaybackState() == EPlaybackState::Stopped;
}

void SPTectonicToolPanel::OnPlaybackSpeedChanged(float NewValue)
{
    if (PlaybackController)
    {
        PlaybackController->SetPlaybackSpeed(NewValue);
    }
}

float SPTectonicToolPanel::GetPlaybackSpeed() const
{
    if (PlaybackController)
    {
        return PlaybackController->GetPlaybackSpeed();
    }
    return 1.0f;
}

FText SPTectonicToolPanel::GetPlaybackSpeedLabel() const
{
    if (PlaybackController)
    {
        const float Speed = PlaybackController->GetPlaybackSpeed();
        return FText::Format(NSLOCTEXT("PlanetaryCreation", "PlaybackSpeedLabel", "Speed: {0}×"), FText::AsNumber(Speed));
    }
    return NSLOCTEXT("PlanetaryCreation", "PlaybackSpeedDefault", "Speed: 1.0×");
}

void SPTectonicToolPanel::OnTimelineScrubbed(float NewValue)
{
    // Milestone 5 Task 1.3: Timeline scrubbing via history system
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            const int32 TargetIndex = FMath::RoundToInt(NewValue);
            if (Service->JumpToHistoryIndex(TargetIndex))
            {
                // Rebuild mesh to reflect the jumped-to state
                Controller->RebuildPreview();
                UE_LOG(LogTemp, Log, TEXT("Timeline scrubbed to step %d (%.1f My)"),
                    TargetIndex, Service->GetCurrentTimeMy());
            }
        }
    }
}

float SPTectonicToolPanel::GetTimelineValue() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            // Each step is 2 My, so step count = time / 2
            return static_cast<float>(Service->GetCurrentTimeMy() / 2.0);
        }
    }
    return 0.0f;
}

float SPTectonicToolPanel::GetTimelineMaxValue() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            // Return current step as max for now; will be history size once rollback is implemented
            // Each step is 2 My, so step count = time / 2
            return FMath::Max(1.0f, static_cast<float>(Service->GetCurrentTimeMy() / 2.0));
        }
    }
    return 1.0f;
}

FText SPTectonicToolPanel::GetTimelineLabel() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            const double CurrentTime = Service->GetCurrentTimeMy();
            // Each step is 2 My, so step count = time / 2
            const int32 CurrentStep = FMath::FloorToInt(CurrentTime / 2.0);
            return FText::Format(
                NSLOCTEXT("PlanetaryCreation", "TimelineLabel", "Timeline: Step {0} ({1} My)"),
                FText::AsNumber(CurrentStep),
                FText::AsNumber(FMath::RoundToInt(CurrentTime))
            );
        }
    }
    return NSLOCTEXT("PlanetaryCreation", "TimelineUnavailable", "Timeline: n/a");
}

// ============================================================================
// Milestone 5 Task 1.3: Undo/Redo Handlers
// ============================================================================

FReply SPTectonicToolPanel::HandleUndoClicked()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            if (Service->Undo())
            {
                // Rebuild mesh to reflect restored state
                Controller->RebuildPreview();
                UE_LOG(LogTemp, Log, TEXT("Undo successful, mesh rebuilt"));
            }
        }
    }
    return FReply::Handled();
}

FReply SPTectonicToolPanel::HandleRedoClicked()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            if (Service->Redo())
            {
                // Rebuild mesh to reflect restored state
                Controller->RebuildPreview();
                UE_LOG(LogTemp, Log, TEXT("Redo successful, mesh rebuilt"));
            }
        }
    }
    return FReply::Handled();
}

bool SPTectonicToolPanel::IsUndoEnabled() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            return Service->CanUndo();
        }
    }
    return false;
}

bool SPTectonicToolPanel::IsRedoEnabled() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            return Service->CanRedo();
        }
    }
    return false;
}

FText SPTectonicToolPanel::GetHistoryStatusText() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            const int32 CurrentIndex = Service->GetHistoryIndex();
            const int32 HistorySize = Service->GetHistorySize();
            return FText::Format(
                NSLOCTEXT("PlanetaryCreation", "HistoryStatus", "History: {0}/{1}"),
                FText::AsNumber(CurrentIndex + 1),
                FText::AsNumber(HistorySize)
            );
        }
    }
    return NSLOCTEXT("PlanetaryCreation", "HistoryUnavailable", "History: n/a");
}

// ============================================================================
// Milestone 5 Task 1.2: Camera Control Implementation
// ============================================================================

void SPTectonicToolPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

    // Update camera controller every frame
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        Controller->TickCamera(InDeltaTime);
    }
}

FReply SPTectonicToolPanel::HandleRotateLeftClicked()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        Controller->RotateCamera(15.0f, 0.0f); // Rotate 15 degrees left (positive = counter-clockwise)
    }
    return FReply::Handled();
}

FReply SPTectonicToolPanel::HandleRotateRightClicked()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        Controller->RotateCamera(-15.0f, 0.0f); // Rotate 15 degrees right (negative = clockwise)
    }
    return FReply::Handled();
}

FReply SPTectonicToolPanel::HandleTiltUpClicked()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        Controller->RotateCamera(0.0f, 10.0f); // Tilt 10 degrees up
    }
    return FReply::Handled();
}

FReply SPTectonicToolPanel::HandleTiltDownClicked()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        Controller->RotateCamera(0.0f, -10.0f); // Tilt 10 degrees down
    }
    return FReply::Handled();
}

FReply SPTectonicToolPanel::HandleZoomInClicked()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        // M5 Phase 3.7: Scale zoom for meter-based coordinates (1.5M km step)
        Controller->ZoomCamera(-150000000.0f); // Zoom in by 1.5M km
    }
    return FReply::Handled();
}

FReply SPTectonicToolPanel::HandleZoomOutClicked()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        // M5 Phase 3.7: Scale zoom for meter-based coordinates (1.5M km step)
        Controller->ZoomCamera(150000000.0f); // Zoom out by 1.5M km
    }
    return FReply::Handled();
}

FReply SPTectonicToolPanel::HandleResetCameraClicked()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        Controller->ResetCamera();
        UE_LOG(LogTemp, Log, TEXT("Camera reset to default view"));
    }
    return FReply::Handled();
}

FText SPTectonicToolPanel::GetCameraStatusText() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        const FVector2D Angles = Controller->GetCameraAngles();
        const float Distance = Controller->GetCameraDistance();
        return FText::Format(
            NSLOCTEXT("PlanetaryCreation", "CameraStatus", "Camera: Yaw {0}° Pitch {1}° Dist {2}"),
            FText::AsNumber(FMath::RoundToInt(Angles.X)),
            FText::AsNumber(FMath::RoundToInt(Angles.Y)),
            FText::AsNumber(FMath::RoundToInt(Distance))
        );
    }
    return NSLOCTEXT("PlanetaryCreation", "CameraUnavailable", "Camera: n/a");
}
