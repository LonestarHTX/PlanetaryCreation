#include "SPTectonicToolPanel.h"

#include "TectonicSimulationController.h"
#include "TectonicSimulationService.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SPTectonicToolPanel::Construct(const FArguments& InArgs)
{
    ControllerWeak = InArgs._Controller;

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
                    .MaxValue(6)
                    .ToolTipText(NSLOCTEXT("PlanetaryCreation", "SubdivisionTooltip", "Render mesh density (0=20, 1=80, 2=320, 3=1280, 4=5120, 5=20480, 6=81920 faces)"))
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
