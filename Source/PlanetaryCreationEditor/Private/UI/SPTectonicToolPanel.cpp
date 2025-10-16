#include "UI/SPTectonicToolPanel.h"

#include "Utilities/PlanetaryCreationLogging.h"

#include "Simulation/TectonicSimulationController.h"
#include "Simulation/TectonicSimulationService.h"
#include "Simulation/TectonicPlaybackController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/MessageDialog.h"
#include "Misc/DateTime.h"
#include "Misc/ScopeExit.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

SPTectonicToolPanel::~SPTectonicToolPanel()
{
    if (PlaybackController)
    {
        PlaybackController->Shutdown();
    }

    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            if (StageBReadyDelegateHandle.IsValid())
            {
                Service->OnStageBAmplificationReadyChanged().Remove(StageBReadyDelegateHandle);
                StageBReadyDelegateHandle = FDelegateHandle();
            }
        }
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
            CachedPaletteMode = Service->GetHeightmapPaletteMode();
        }
    }

    RefreshStageBReadinessFromService();
    BindStageBReadyDelegate();

    InitializeVisualizationOptions();
    RefreshSelectedVisualizationOption();

    BoundaryModeOptions.Reset();
    BoundaryModeOptions.Add(MakeShared<int32>(0));
    BoundaryModeOptions.Add(MakeShared<int32>(1));

    int32 CurrentBoundaryMode = 0;
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        CurrentBoundaryMode = FMath::Clamp(Controller->GetBoundaryOverlayMode(), 0, 1);
    }
    SelectedBoundaryMode = BoundaryModeOptions.IsValidIndex(CurrentBoundaryMode)
        ? BoundaryModeOptions[CurrentBoundaryMode]
        : BoundaryModeOptions[0];

    ChildSlot
    [
        SNew(SBorder)
        .Padding(12.0f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                BuildStatsHeader()
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 8.0f, 0.0f, 0.0f)
            [
                BuildPrimaryActionRow()
            ]

            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            [
                SNew(SScrollBox)

                + SScrollBox::Slot()
                [
                    SNew(SExpandableArea)
                    .AreaTitle(NSLOCTEXT("PlanetaryCreation", "SimulationSetupHeader", "Simulation Setup"))
                    .InitiallyCollapsed(false)
                    .BodyContent()
                    [
                        BuildSimulationSection()
                    ]
                ]

                + SScrollBox::Slot()
                .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                [
                    SNew(SExpandableArea)
                    .AreaTitle(NSLOCTEXT("PlanetaryCreation", "PlaybackSectionHeader", "Playback & History"))
                    .InitiallyCollapsed(false)
                    .BodyContent()
                    [
                        BuildPlaybackSection()
                    ]
                ]

                + SScrollBox::Slot()
                .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                [
                    SNew(SExpandableArea)
                    .AreaTitle(NSLOCTEXT("PlanetaryCreation", "VisualizationSectionHeader", "Visualization & Preview"))
                    .InitiallyCollapsed(false)
                    .BodyContent()
                    [
                        BuildVisualizationSection()
                    ]
                ]

                + SScrollBox::Slot()
                .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                [
                    SNew(SExpandableArea)
                    .AreaTitle(NSLOCTEXT("PlanetaryCreation", "StageBSectionHeader", "Stage B & Detail"))
                    .InitiallyCollapsed(true)
                    .BodyContent()
                    [
                        BuildStageBSection()
                    ]
                ]

                + SScrollBox::Slot()
                .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                [
                    SNew(SExpandableArea)
                    .AreaTitle(NSLOCTEXT("PlanetaryCreation", "SurfaceSectionHeader", "Surface Processes"))
                    .InitiallyCollapsed(true)
                    .BodyContent()
                    [
                        BuildSurfaceProcessesSection()
                    ]
                ]

                + SScrollBox::Slot()
                .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                [
                    SNew(SExpandableArea)
                    .AreaTitle(NSLOCTEXT("PlanetaryCreation", "CameraSectionHeader", "Camera & View"))
                    .InitiallyCollapsed(true)
                    .BodyContent()
                    [
                        BuildCameraSection()
                    ]
                ]
            ]
        ]
    ];

    RefreshSelectedVisualizationOption();
}

TSharedRef<SWidget> SPTectonicToolPanel::BuildStatsHeader()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(this, &SPTectonicToolPanel::GetCurrentTimeLabel)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(this, &SPTectonicToolPanel::GetPlateCountLabel)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(this, &SPTectonicToolPanel::GetPerformanceStatsLabel)
            .ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(this, &SPTectonicToolPanel::GetRetessellationStatsLabel)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
        ];
}

TSharedRef<SWidget> SPTectonicToolPanel::BuildPrimaryActionRow()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SButton)
                .Text(NSLOCTEXT("PlanetaryCreation", "StepButtonLabel", "Step (2 My)"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "StepButtonTooltip", "Advance the tectonic simulation by one iteration (2 My)."))
                .OnClicked(this, &SPTectonicToolPanel::HandleStepClicked)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SButton)
                .Text(this, &SPTectonicToolPanel::GetPlaybackButtonText)
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "PlayPauseTooltip", "Start/pause continuous playback (Space)"))
                .OnClicked(this, &SPTectonicToolPanel::HandlePlayClicked)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SButton)
                .Text(NSLOCTEXT("PlanetaryCreation", "StopButtonLabel", "Stop"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "StopTooltip", "Stop playback and reset"))
                .OnClicked(this, &SPTectonicToolPanel::HandleStopClicked)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SButton)
                .Text(NSLOCTEXT("PlanetaryCreation", "RegenerateButtonLabel", "Regenerate Plates"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "RegenerateButtonTooltip", "Reset simulation with current seed and regenerate plate layout"))
                .OnClicked(this, &SPTectonicToolPanel::HandleRegenerateClicked)
            ]
        ];
}

TSharedRef<SWidget> SPTectonicToolPanel::BuildSimulationSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
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
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
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
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 8.0f, 0.0f, 0.0f)
        [
            SNew(SSeparator)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SButton)
                .Text(NSLOCTEXT("PlanetaryCreation", "ExportMetricsLabel", "Export Metrics CSV"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "ExportMetricsTooltip", "Export current simulation state to Saved/TectonicMetrics/ for analysis"))
                .OnClicked(this, &SPTectonicToolPanel::HandleExportMetricsClicked)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SButton)
                .Text(NSLOCTEXT("PlanetaryCreation", "ExportTerranesLabel", "Export Terranes CSV"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "ExportTerranesTooltip", "Export active terrane lifecycle data to Saved/TectonicMetrics/ for analysis"))
                .OnClicked(this, &SPTectonicToolPanel::HandleExportTerranesClicked)
            ]
        ];
}

TSharedRef<SWidget> SPTectonicToolPanel::BuildPlaybackSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
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
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 8.0f, 0.0f, 0.0f)
        [
            SNew(STextBlock)
            .Text(this, &SPTectonicToolPanel::GetTimelineLabel)
            .ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f)))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SSlider)
            .Value(this, &SPTectonicToolPanel::GetTimelineValue)
            .OnValueChanged(this, &SPTectonicToolPanel::OnTimelineScrubbed)
            .MinValue(0.0f)
            .MaxValue(1000.0f)
            .ToolTipText(NSLOCTEXT("PlanetaryCreation", "TimelineScrubberTooltip", "Jump to any point in simulation history (← / →)"))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 8.0f, 0.0f, 0.0f)
        [
            SNew(SSeparator)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
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
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            SNew(STextBlock)
            .Text(this, &SPTectonicToolPanel::GetHistoryStatusText)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
        ];
}

TSharedRef<SWidget> SPTectonicToolPanel::BuildVisualizationSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "VisualizationLabel", "Visualization"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "VisualizationTooltip", "Choose the active vertex color overlay (plates, elevation heatmap, velocity, or stress)."))
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SAssignNew(VisualizationCombo, SComboBox<TSharedPtr<ETectonicVisualizationMode>>)
                .OptionsSource(&VisualizationOptions)
                .OnGenerateWidget(this, &SPTectonicToolPanel::GenerateVisualizationOptionWidget)
                .OnSelectionChanged(this, &SPTectonicToolPanel::OnVisualizationModeChanged)
                .Content()
                [
                    SNew(STextBlock)
                    .Text(this, &SPTectonicToolPanel::GetCurrentVisualizationText)
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            SNew(SCheckBox)
            .IsChecked(this, &SPTectonicToolPanel::GetElevationModeState)
            .OnCheckStateChanged(this, &SPTectonicToolPanel::OnElevationModeChanged)
            .Content()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "ElevationModeLabel", "Displaced Elevation"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "ElevationModeTooltip", "Enable geometric displacement from elevation data. Elevation gradient colors are shown in both modes (blue=low → red=high). Unchecked = flat sphere with color gradient only."))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
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
        .Padding(16.0f, 2.0f, 0.0f, 0.0f)
        [
            SNew(SHorizontalBox)
            .Visibility(this, &SPTectonicToolPanel::GetBoundaryModeRowVisibility)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "BoundaryModeLabel", "Boundary Mode:"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "BoundaryModeTooltip", "Choose between detailed boundary ribbons or simplified seam polylines."))
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SAssignNew(BoundaryModeCombo, SComboBox<TSharedPtr<int32>>)
                .OptionsSource(&BoundaryModeOptions)
                .InitiallySelectedItem(SelectedBoundaryMode)
                .OnGenerateWidget(this, &SPTectonicToolPanel::GenerateBoundaryModeWidget)
                .OnSelectionChanged(this, &SPTectonicToolPanel::OnBoundaryOverlayModeChanged)
                .IsEnabled(this, &SPTectonicToolPanel::IsBoundaryModeSelectorEnabled)
                .Content()
                [
                    SNew(STextBlock)
                    .Text(this, &SPTectonicToolPanel::GetCurrentBoundaryModeText)
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            SNew(SCheckBox)
            .IsChecked(this, &SPTectonicToolPanel::GetSeaLevelHighlightState)
            .OnCheckStateChanged(this, &SPTectonicToolPanel::OnSeaLevelHighlightChanged)
            .Content()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "SeaLevelHighlightLabel", "Emphasize Sea Level"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "SeaLevelHighlightTooltip", "Render a thin white isoline near 0 m to highlight coastlines."))
            ]
        ];
}

TSharedRef<SWidget> SPTectonicToolPanel::BuildStageBSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
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
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            SNew(SCheckBox)
            .IsChecked(this, &SPTectonicToolPanel::GetGPUPreviewState)
            .OnCheckStateChanged(this, &SPTectonicToolPanel::OnGPUPreviewChanged)
            .Content()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "GPUPreviewLabel", "GPU Preview Mode"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "GPUPreviewTooltip", "Use the GPU height texture preview path (World Position Offset) to eliminate CPU readback stalls. Visualization-only; collision stays CPU-side."))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            SNew(SCheckBox)
            .IsChecked(this, &SPTectonicToolPanel::GetPBRShadingState)
            .OnCheckStateChanged(this, &SPTectonicToolPanel::OnPBRShadingChanged)
            .Content()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "PBRShadingLabel", "Enable PBR Shading"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "PBRShadingTooltip", "Blend realistic lighting (roughness/metallic) into the preview material. Keeps visualization colors intact; toggle independently of visualization mode."))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(NSLOCTEXT("PlanetaryCreation", "HeightmapLegendLabel", "Legend: deep ocean → coastal shelf → alpine"))
            .WrapTextAt(340.0f)
            .ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            SNew(SCheckBox)
            .IsChecked(this, &SPTectonicToolPanel::GetNormalizedPaletteState)
            .IsEnabled(this, &SPTectonicToolPanel::IsNormalizedPaletteToggleEnabled)
            .OnCheckStateChanged(this, &SPTectonicToolPanel::OnNormalizedPaletteChanged)
            .Content()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "HeightmapNormalizedPaletteLabel", "Use normalized palette"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "HeightmapNormalizedPaletteTooltip", "Stretch elevations between the current minimum and maximum before applying colors. Requires Stage B amplification data."))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(this, &SPTectonicToolPanel::GetPaletteStatusText)
            .ColorAndOpacity(this, &SPTectonicToolPanel::GetPaletteStatusColor)
            .Visibility(this, &SPTectonicToolPanel::GetPaletteStatusVisibility)
            .WrapTextAt(340.0f)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 8.0f, 0.0f, 0.0f)
        [
            SNew(SSeparator)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SCheckBox)
            .IsChecked(this, &SPTectonicToolPanel::GetOceanicAmplificationState)
            .OnCheckStateChanged(this, &SPTectonicToolPanel::OnOceanicAmplificationChanged)
            .Content()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "OceanicAmplificationToggleLabel", "Enable oceanic amplification"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "OceanicAmplificationToggleTooltip", "Adds Stage B oceanic detail (transform faults, fine detail). Changing this resets the simulation."))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 2.0f, 0.0f, 0.0f)
        [
            SNew(SCheckBox)
            .IsChecked(this, &SPTectonicToolPanel::GetContinentalAmplificationState)
            .OnCheckStateChanged(this, &SPTectonicToolPanel::OnContinentalAmplificationChanged)
            .Content()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "ContinentalAmplificationToggleLabel", "Enable continental amplification"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "ContinentalAmplificationToggleTooltip", "Blend exemplar heightfields for continental Stage B detail. Changing this resets the simulation."))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 6.0f, 0.0f, 2.0f)
        [
            SNew(SButton)
            .Text(NSLOCTEXT("PlanetaryCreation", "PaperReadyButtonLabel", "Paper Ready"))
            .ToolTipText(NSLOCTEXT("PlanetaryCreation", "PaperReadyButtonTooltip", "Apply the paper-authentic configuration, re-enable erosion/dampening, warm Stage B, and prep the editor for parity checks."))
            .OnClicked(this, &SPTectonicToolPanel::HandlePaperReadyClicked)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 2.0f, 0.0f, 2.0f)
        [
            SNew(SButton)
            .Text(NSLOCTEXT("PlanetaryCreation", "PrimeStageBButtonLabel", "Prime GPU Stage B"))
            .ToolTipText(NSLOCTEXT("PlanetaryCreation", "PrimeStageBButtonTooltip", "Enable both Stage B passes, keep CPU fallbacks active, and switch the GPU path on in one click (resets simulation if Stage B settings change)."))
            .OnClicked(this, &SPTectonicToolPanel::HandlePrimeGPUStageBClicked)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 2.0f, 0.0f, 2.0f)
        [
            SNew(SButton)
            .Text(NSLOCTEXT("PlanetaryCreation", "ExportHeightmapButtonLabel", "Export Heightmap..."))
            .ToolTipText(NSLOCTEXT("PlanetaryCreation", "ExportHeightmapButtonTooltip", "Run the 512×256 heightmap export commandlet (applies Paper Ready if needed)."))
            .OnClicked(this, &SPTectonicToolPanel::HandleExportHeightmapClicked)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(NSLOCTEXT("PlanetaryCreation", "BatchHint", "Batch stepping and fast-forward presets will arrive in later milestones."))
            .WrapTextAt(340.0f)
            .ColorAndOpacity(FSlateColor(FLinearColor::Gray))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            SNew(STextBlock)
            .Text(NSLOCTEXT("PlanetaryCreation", "PaperDefaultsHint", "Profiling CPU-only? Run `r.PlanetaryCreation.PaperDefaults 0` to revert to the M5 baseline."))
            .WrapTextAt(340.0f)
            .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
        ];
}

TSharedRef<SWidget> SPTectonicToolPanel::BuildSurfaceProcessesSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SCheckBox)
            .IsChecked(this, &SPTectonicToolPanel::GetContinentalErosionState)
            .OnCheckStateChanged(this, &SPTectonicToolPanel::OnContinentalErosionChanged)
            .Content()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "ErosionToggleLabel", "Enable continental erosion"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "ErosionToggleTooltip", "Apply continental erosion each step. Changing this resets the simulation."))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 2.0f, 0.0f, 0.0f)
        [
            SNew(SCheckBox)
            .IsChecked(this, &SPTectonicToolPanel::GetSedimentTransportState)
            .OnCheckStateChanged(this, &SPTectonicToolPanel::OnSedimentTransportChanged)
            .Content()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "SedimentToggleLabel", "Enable sediment transport"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "SedimentToggleTooltip", "Redistribute eroded material to neighbours. Changing this resets the simulation."))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 2.0f, 0.0f, 0.0f)
        [
            SNew(SCheckBox)
            .IsChecked(this, &SPTectonicToolPanel::GetHydraulicErosionState)
            .OnCheckStateChanged(this, &SPTectonicToolPanel::OnHydraulicErosionChanged)
            .Content()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "HydraulicToggleLabel", "Enable hydraulic erosion"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "HydraulicToggleTooltip", "Run stream-power routing on amplified terrain to carve valleys. Changing this resets the simulation."))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 2.0f, 0.0f, 0.0f)
        [
            SNew(SCheckBox)
            .IsChecked(this, &SPTectonicToolPanel::GetOceanicDampeningState)
            .OnCheckStateChanged(this, &SPTectonicToolPanel::OnOceanicDampeningChanged)
            .Content()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PlanetaryCreation", "OceanicDampeningToggleLabel", "Enable oceanic dampening"))
                .ToolTipText(NSLOCTEXT("PlanetaryCreation", "OceanicDampeningToggleTooltip", "Activate age-based subsidence and smoothing for oceanic crust. Changing this resets the simulation."))
            ]
        ];
}

TSharedRef<SWidget> SPTectonicToolPanel::BuildCameraSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(this, &SPTectonicToolPanel::GetCameraStatusText)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 8.0f, 0.0f, 0.0f)
        [
            SNew(SSeparator)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
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
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
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
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
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
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            SNew(SButton)
            .Text(NSLOCTEXT("PlanetaryCreation", "ResetCameraButton", "Reset Camera"))
            .ToolTipText(NSLOCTEXT("PlanetaryCreation", "ResetCameraTooltip", "Reset camera to default view"))
            .OnClicked(this, &SPTectonicToolPanel::HandleResetCameraClicked)
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
            Controller->RebuildPreview(); // warm preview mesh after reset

            UE_LOG(LogPlanetaryCreation, Log, TEXT("Regenerated plates with seed %d"), CachedSeed);
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

FText SPTectonicToolPanel::GetRetessellationStatsLabel() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            const UTectonicSimulationService::FRetessellationCadenceStats& Stats = Service->GetRetessellationCadenceStats();

            if (Stats.StepsObserved == 0)
            {
                return NSLOCTEXT("PlanetaryCreation", "RetessStatsPending", "Retess: waiting for cadence...");
            }

            const bool bHasTriggerSample = Stats.TriggerCount > 0;

            const FString DriftString = bHasTriggerSample
                ? FString::Printf(TEXT("%.1f"), Stats.LastTriggerMaxDriftDegrees)
                : TEXT("--");
            const FString BadTriString = bHasTriggerSample
                ? FString::Printf(TEXT("%.2f"), Stats.LastTriggerBadTriangleRatio * 100.0)
                : TEXT("--");

            const int32 SinceLast = FMath::Clamp(Stats.StepsSinceLastTrigger, 0, 999999);
            const int32 CooldownSteps = FMath::Clamp(Stats.LastCooldownDuration, 0, 999999);

            const FString LabelString = FString::Printf(
                TEXT("Retess: auto %d | eval %d | last %s° / %s%% | since %d | cool %d"),
                Stats.TriggerCount,
                Stats.EvaluationCount,
                *DriftString,
                *BadTriString,
                SinceLast,
                CooldownSteps);

            return FText::FromString(LabelString);
        }
    }

    return NSLOCTEXT("PlanetaryCreation", "RetessStatsUnavailable", "Retess: n/a");
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

FReply SPTectonicToolPanel::HandleExportTerranesClicked()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            Service->ExportTerranesToCSV();
        }
    }
    return FReply::Handled();
}

void SPTectonicToolPanel::InitializeVisualizationOptions()
{
    VisualizationOptions.Reset();
    VisualizationOptions.Add(MakeShared<ETectonicVisualizationMode>(ETectonicVisualizationMode::PlateColors));
    VisualizationOptions.Add(MakeShared<ETectonicVisualizationMode>(ETectonicVisualizationMode::Elevation));
    VisualizationOptions.Add(MakeShared<ETectonicVisualizationMode>(ETectonicVisualizationMode::Velocity));
    VisualizationOptions.Add(MakeShared<ETectonicVisualizationMode>(ETectonicVisualizationMode::Stress));
    VisualizationOptions.Add(MakeShared<ETectonicVisualizationMode>(ETectonicVisualizationMode::Amplified));
    VisualizationOptions.Add(MakeShared<ETectonicVisualizationMode>(ETectonicVisualizationMode::AmplificationBlend));
}

void SPTectonicToolPanel::RefreshSelectedVisualizationOption()
{
    if (VisualizationOptions.Num() == 0)
    {
        InitializeVisualizationOptions();
    }

    ETectonicVisualizationMode CurrentMode = ETectonicVisualizationMode::PlateColors;
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        CurrentMode = Controller->GetVisualizationMode();
    }

    SelectedVisualizationOption.Reset();
    for (const TSharedPtr<ETectonicVisualizationMode>& Option : VisualizationOptions)
    {
        if (Option.IsValid() && *Option == CurrentMode)
        {
            SelectedVisualizationOption = Option;
            break;
        }
    }

    if (!SelectedVisualizationOption.IsValid() && VisualizationOptions.Num() > 0)
    {
        SelectedVisualizationOption = VisualizationOptions[0];
    }

    if (VisualizationCombo.IsValid())
    {
        VisualizationCombo->SetSelectedItem(SelectedVisualizationOption);
    }
}

TSharedRef<SWidget> SPTectonicToolPanel::GenerateVisualizationOptionWidget(TSharedPtr<ETectonicVisualizationMode> InOption) const
{
    return SNew(STextBlock)
        .Text(GetVisualizationModeLabel(InOption));
}

void SPTectonicToolPanel::OnVisualizationModeChanged(TSharedPtr<ETectonicVisualizationMode> NewSelection, ESelectInfo::Type SelectInfo)
{
    if (!NewSelection.IsValid())
    {
        return;
    }

    SelectedVisualizationOption = NewSelection;

    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        Controller->SetVisualizationMode(*NewSelection);

        // Automatically disable PBR shading for elevation mode to show pure hypsometric colors
        // PBR lighting washes out the gradient with specular highlights
        if (*NewSelection == ETectonicVisualizationMode::Elevation)
        {
            Controller->SetPBRShadingEnabled(false);
            UE_LOG(LogPlanetaryCreation, Log, TEXT("[Visualization] Auto-disabled PBR shading for Elevation mode (prevents glossy highlights from obscuring gradient)"));
        }
    }
}

FText SPTectonicToolPanel::GetVisualizationModeLabel(TSharedPtr<ETectonicVisualizationMode> InOption) const
{
    if (!InOption.IsValid())
    {
        return NSLOCTEXT("PlanetaryCreation", "VisualizationUnknown", "Unknown");
    }

    switch (*InOption)
    {
    case ETectonicVisualizationMode::PlateColors:
        return NSLOCTEXT("PlanetaryCreation", "VisualizationPlate", "Plate Colors");
    case ETectonicVisualizationMode::Elevation:
        return NSLOCTEXT("PlanetaryCreation", "VisualizationElevation", "Elevation Heatmap");
    case ETectonicVisualizationMode::Velocity:
        return NSLOCTEXT("PlanetaryCreation", "VisualizationVelocity", "Velocity Field");
    case ETectonicVisualizationMode::Stress:
        return NSLOCTEXT("PlanetaryCreation", "VisualizationStress", "Stress Gradient");
    case ETectonicVisualizationMode::Amplified:
        return NSLOCTEXT("PlanetaryCreation", "VisualizationAmplified", "Amplified Stage B");
    case ETectonicVisualizationMode::AmplificationBlend:
        return NSLOCTEXT("PlanetaryCreation", "VisualizationAmplificationBlend", "Amplification Blend");
    default:
        return NSLOCTEXT("PlanetaryCreation", "VisualizationDefault", "Plate Colors");
    }
}

FText SPTectonicToolPanel::GetCurrentVisualizationText() const
{
    return GetVisualizationModeLabel(SelectedVisualizationOption);
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
            Controller->RebuildPreview(); // warm preview mesh after reset

            UE_LOG(LogPlanetaryCreation, Log, TEXT("Updated render subdivision level to %d"), CachedSubdivisionLevel);
        }
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
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Elevation mode: %s"), Mode == EElevationMode::Displaced ? TEXT("Displaced") : TEXT("Flat"));
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
        Controller->RefreshBoundaryOverlay();
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Boundary overlay %s"), bVisible ? TEXT("visible") : TEXT("hidden"));
    }
}

void SPTectonicToolPanel::OnBoundaryOverlayModeChanged(TSharedPtr<int32> NewMode, ESelectInfo::Type SelectInfo)
{
    if (!NewMode.IsValid())
    {
        return;
    }

    SelectedBoundaryMode = NewMode;
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        Controller->SetBoundaryOverlayMode(*NewMode);
        Controller->RefreshBoundaryOverlay();
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Boundary overlay mode set to %d"), *NewMode);
    }
}

TSharedRef<SWidget> SPTectonicToolPanel::GenerateBoundaryModeWidget(TSharedPtr<int32> InMode) const
{
    const int32 ModeValue = InMode.IsValid() ? *InMode : 0;
    FText Label;
    switch (ModeValue)
    {
        case 1:
            Label = NSLOCTEXT("PlanetaryCreation", "BoundaryModeSimplified", "Simplified seams");
            break;
        default:
            Label = NSLOCTEXT("PlanetaryCreation", "BoundaryModeDetailed", "Detailed ribbons");
            break;
    }
    return SNew(STextBlock).Text(Label);
}

FText SPTectonicToolPanel::GetCurrentBoundaryModeText() const
{
    const int32 ModeValue = SelectedBoundaryMode.IsValid() ? *SelectedBoundaryMode : 0;
    return ModeValue == 1
        ? NSLOCTEXT("PlanetaryCreation", "BoundaryModeSimplified", "Simplified seams")
        : NSLOCTEXT("PlanetaryCreation", "BoundaryModeDetailed", "Detailed ribbons");
}

bool SPTectonicToolPanel::IsBoundaryModeSelectorEnabled() const
{
    return GetBoundaryOverlayState() == ECheckBoxState::Checked;
}

EVisibility SPTectonicToolPanel::GetBoundaryModeRowVisibility() const
{
    return IsBoundaryModeSelectorEnabled() ? EVisibility::Visible : EVisibility::Collapsed;
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
            UE_LOG(LogPlanetaryCreation, Log, TEXT("Automatic LOD %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
        }
    }
}

ECheckBoxState SPTectonicToolPanel::GetGPUPreviewState() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        return Controller->IsGPUPreviewModeEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
    }
    return ECheckBoxState::Unchecked;
}

void SPTectonicToolPanel::OnGPUPreviewChanged(ECheckBoxState NewState)
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        const bool bEnabled = (NewState == ECheckBoxState::Checked);
        Controller->SetGPUPreviewMode(bEnabled);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("GPU preview mode %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
    }
}

ECheckBoxState SPTectonicToolPanel::GetNormalizedPaletteState() const
{
    return (CachedPaletteMode == EHeightmapPaletteMode::NormalizedRange)
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

void SPTectonicToolPanel::OnNormalizedPaletteChanged(ECheckBoxState NewState)
{
    const bool bEnableNormalized = (NewState == ECheckBoxState::Checked);
    CachedPaletteMode = bEnableNormalized
        ? EHeightmapPaletteMode::NormalizedRange
        : EHeightmapPaletteMode::AbsoluteHypsometric;

    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            Service->SetHeightmapPaletteMode(CachedPaletteMode);
        }
    }
}

bool SPTectonicToolPanel::IsNormalizedPaletteToggleEnabled() const
{
    return bCachedStageBReady;
}

FText SPTectonicToolPanel::GetPaletteStatusText() const
{
    return CachedPaletteStatusText;
}

FSlateColor SPTectonicToolPanel::GetPaletteStatusColor() const
{
    if (bCachedStageBReady)
    {
        return FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f));
    }

    return FSlateColor(FLinearColor(0.82f, 0.3f, 0.3f));
}

EVisibility SPTectonicToolPanel::GetPaletteStatusVisibility() const
{
    return CachedPaletteStatusText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

ECheckBoxState SPTectonicToolPanel::GetPBRShadingState() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        return Controller->IsPBRShadingEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
    }
    return ECheckBoxState::Unchecked;
}

void SPTectonicToolPanel::OnPBRShadingChanged(ECheckBoxState NewState)
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        const bool bEnabled = (NewState == ECheckBoxState::Checked);
        Controller->SetPBRShadingEnabled(bEnabled);
        UE_LOG(LogPlanetaryCreation, Log, TEXT("PBR shading %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
    }
}

ECheckBoxState SPTectonicToolPanel::GetContinentalErosionState() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (const UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            return Service->GetParameters().bEnableContinentalErosion ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
        }
    }
    return ECheckBoxState::Unchecked;
}

void SPTectonicToolPanel::OnContinentalErosionChanged(ECheckBoxState NewState)
{
    const bool bEnabled = (NewState == ECheckBoxState::Checked);
    ApplySurfaceProcessMutation(
        [bEnabled](FTectonicSimulationParameters& Params)
        {
            if (Params.bEnableContinentalErosion == bEnabled)
            {
                return false;
            }
            Params.bEnableContinentalErosion = bEnabled;
            return true;
        },
        TEXT("Continental erosion"));
}

ECheckBoxState SPTectonicToolPanel::GetSedimentTransportState() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (const UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            return Service->GetParameters().bEnableSedimentTransport ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
        }
    }
    return ECheckBoxState::Unchecked;
}

void SPTectonicToolPanel::OnSedimentTransportChanged(ECheckBoxState NewState)
{
    const bool bEnabled = (NewState == ECheckBoxState::Checked);
    ApplySurfaceProcessMutation(
        [bEnabled](FTectonicSimulationParameters& Params)
        {
            if (Params.bEnableSedimentTransport == bEnabled)
            {
                return false;
            }
            Params.bEnableSedimentTransport = bEnabled;
            return true;
        },
        TEXT("Sediment transport"));
}

ECheckBoxState SPTectonicToolPanel::GetHydraulicErosionState() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (const UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            return Service->GetParameters().bEnableHydraulicErosion ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
        }
    }
    return ECheckBoxState::Unchecked;
}

void SPTectonicToolPanel::OnHydraulicErosionChanged(ECheckBoxState NewState)
{
    const bool bEnabled = (NewState == ECheckBoxState::Checked);
    ApplySurfaceProcessMutation(
        [bEnabled](FTectonicSimulationParameters& Params)
        {
            if (Params.bEnableHydraulicErosion == bEnabled)
            {
                return false;
            }
            Params.bEnableHydraulicErosion = bEnabled;
            return true;
        },
        TEXT("Hydraulic erosion"));
}

ECheckBoxState SPTectonicToolPanel::GetOceanicDampeningState() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (const UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            return Service->GetParameters().bEnableOceanicDampening ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
        }
    }
    return ECheckBoxState::Unchecked;
}

void SPTectonicToolPanel::OnOceanicDampeningChanged(ECheckBoxState NewState)
{
    const bool bEnabled = (NewState == ECheckBoxState::Checked);
    ApplySurfaceProcessMutation(
        [bEnabled](FTectonicSimulationParameters& Params)
        {
            if (Params.bEnableOceanicDampening == bEnabled)
            {
                return false;
            }
            Params.bEnableOceanicDampening = bEnabled;
            return true;
        },
        TEXT("Oceanic dampening"));
}

ECheckBoxState SPTectonicToolPanel::GetOceanicAmplificationState() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (const UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            return Service->GetParameters().bEnableOceanicAmplification ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
        }
    }
    return ECheckBoxState::Unchecked;
}

void SPTectonicToolPanel::OnOceanicAmplificationChanged(ECheckBoxState NewState)
{
    const bool bEnabled = (NewState == ECheckBoxState::Checked);
    ApplySurfaceProcessMutation(
        [bEnabled](FTectonicSimulationParameters& Params)
        {
            if (Params.bEnableOceanicAmplification == bEnabled)
            {
                return false;
            }
            Params.bEnableOceanicAmplification = bEnabled;
            return true;
        },
        TEXT("Oceanic amplification"));
}

ECheckBoxState SPTectonicToolPanel::GetContinentalAmplificationState() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (const UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            return Service->GetParameters().bEnableContinentalAmplification ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
        }
    }
    return ECheckBoxState::Unchecked;
}

void SPTectonicToolPanel::OnContinentalAmplificationChanged(ECheckBoxState NewState)
{
    const bool bEnabled = (NewState == ECheckBoxState::Checked);
    ApplySurfaceProcessMutation(
        [bEnabled](FTectonicSimulationParameters& Params)
        {
            if (Params.bEnableContinentalAmplification == bEnabled)
            {
                return false;
            }
            Params.bEnableContinentalAmplification = bEnabled;
            return true;
        },
        TEXT("Continental amplification"));
}

void SPTectonicToolPanel::ApplySurfaceProcessMutation(TFunctionRef<bool(FTectonicSimulationParameters&)> Mutator, const TCHAR* ChangeLabel) const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            FTectonicSimulationParameters Params = Service->GetParameters();
            if (!Mutator(Params))
            {
                return;
            }

            Service->SetParameters(Params);

            Controller->RebuildPreview(); // warm preview mesh after reset

            UE_LOG(LogPlanetaryCreation, Log, TEXT("%s toggled, simulation reset."), ChangeLabel);
        }
    }
}

FReply SPTectonicToolPanel::HandlePaperReadyClicked()
{
    ApplyPaperReadyPreset();
    return FReply::Handled();
}

FReply SPTectonicToolPanel::HandleExportHeightmapClicked()
{
	UE_LOG(LogPlanetaryCreation, Log, TEXT("[PaperReady] UsingExportButton"));

	bool bReadyForExport = bPaperReadyApplied;
    if (!bReadyForExport)
    {
        bReadyForExport = ApplyPaperReadyPreset();
        if (!bReadyForExport)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[PaperReady] Stage B is still not ready after applying the preset; export will proceed with current data."));
			FMessageDialog::Open(EAppMsgType::Ok,
				NSLOCTEXT("PlanetaryCreation", "ExportHeightmapStageBNotReady", "Stage B is still warming up. The export will proceed with the current data; verify results after completion."));
		}
	}

	const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin();
	if (!Controller)
	{
		UE_LOG(LogPlanetaryCreation, Error, TEXT("[HeightmapExport] Simulation controller unavailable."));
		return FReply::Handled();
	}

	UTectonicSimulationService* Service = Controller->GetSimulationService();
	if (!Service)
	{
		UE_LOG(LogPlanetaryCreation, Error, TEXT("[HeightmapExport] Simulation service unavailable."));
		return FReply::Handled();
	}

	const FString ProjectDir = FPaths::ProjectDir();
	const FString DocsDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(ProjectDir, TEXT("Docs/Validation")));
	IFileManager::Get().MakeDirectory(*DocsDir, true);

	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    const FString OutputPath = FPaths::Combine(DocsDir, FString::Printf(TEXT("Heightmap_512x256_%s.png"), *Timestamp));

    const FString EngineExe = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/Win64/UnrealEditor-Cmd.exe")));
    const FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    const FString ScriptPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Scripts/RunHeightmapExport.py")));

    const FString EscapedOutput = OutputPath.Replace(TEXT("\""), TEXT("\\\""));
    const FString ScriptArgs = FString::Printf(TEXT("--width=512 --height=256 --output=\\\"%s\\\""), *EscapedOutput);
    const FString CommandLine = FString::Printf(
        TEXT("\"%s\" -run=pythonscript -script=\"%s\" -scriptargs=\"%s\" -SetCVar=r.PlanetaryCreation.PaperDefaults=0,r.PlanetaryCreation.UseGPUAmplification=0,r.PlanetaryCreation.SkipCPUAmplification=0 -NullRHI -unattended -nop4 -nosplash"),
        *ProjectPath,
        *ScriptPath,
        *ScriptArgs);

	FScopedSlowTask SlowTask(0.0f, NSLOCTEXT("PlanetaryCreation", "ExportHeightmapProgress", "Exporting heightmap..."));
	SlowTask.MakeDialog();

	struct FEnvOverride
	{
		FString Name;
		FString PreviousValue;
	};

	TArray<FEnvOverride> EnvOverrides;
	EnvOverrides.Reserve(4);

	auto OverrideEnvVar = [&EnvOverrides](const TCHAR* Name, const FString& Value)
	{
		FEnvOverride& Entry = EnvOverrides.AddDefaulted_GetRef();
		Entry.Name = Name;
		Entry.PreviousValue = FPlatformMisc::GetEnvironmentVariable(Name);
		FPlatformMisc::SetEnvironmentVar(Name, *Value);
	};

	const int32 RenderSubdivisionLevel = Service->GetParameters().RenderSubdivisionLevel;
	OverrideEnvVar(TEXT("PLANETARY_STAGEB_FORCE_CPU"), TEXT("1"));
	OverrideEnvVar(TEXT("PLANETARY_STAGEB_FORCE_EXEMPLAR"), TEXT("O01"));
	OverrideEnvVar(TEXT("PLANETARY_STAGEB_DISABLE_RANDOM_OFFSET"), TEXT("1"));
	OverrideEnvVar(TEXT("PLANETARY_STAGEB_RENDER_LOD"), FString::FromInt(RenderSubdivisionLevel));

	ON_SCOPE_EXIT
	{
		for (const FEnvOverride& Override : EnvOverrides)
		{
			FPlatformMisc::SetEnvironmentVar(*Override.Name, *Override.PreviousValue);
		}
	};

	FProcHandle ProcHandle = FPlatformProcess::CreateProc(*EngineExe, *CommandLine, true, true, true, nullptr, 0, *ProjectDir, nullptr);
	if (!ProcHandle.IsValid())
	{
		UE_LOG(LogPlanetaryCreation, Error, TEXT("[HeightmapExport] Failed to launch UnrealEditor-Cmd.exe"));
        FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("PlanetaryCreation", "ExportHeightmapLaunchFailed", "Failed to launch the heightmap export commandlet. See log for details."));
        return FReply::Handled();
    }

    FPlatformProcess::WaitForProc(ProcHandle);

    int32 ReturnCode = INDEX_NONE;
    FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);
    FPlatformProcess::CloseProc(ProcHandle);

    if (ReturnCode == 0)
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[HeightmapExport] Completed Path=%s"), *OutputPath);
        FMessageDialog::Open(EAppMsgType::Ok,
            FText::Format(NSLOCTEXT("PlanetaryCreation", "ExportHeightmapSuccess", "Heightmap exported to:\n{0}"), FText::FromString(OutputPath)));
    }
    else
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("[HeightmapExport] Failed ReturnCode=%d Path=%s"), ReturnCode, *OutputPath);
        FMessageDialog::Open(EAppMsgType::Ok,
            FText::Format(NSLOCTEXT("PlanetaryCreation", "ExportHeightmapFailed", "Heightmap export failed with code {0}. See log for details."), FText::AsNumber(ReturnCode)));
    }

    return FReply::Handled();
}

bool SPTectonicToolPanel::ApplyPaperReadyPreset()
{
    const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin();
    if (!Controller)
    {
        return false;
    }

    UTectonicSimulationService* Service = Controller->GetSimulationService();
    if (!Service)
    {
        return false;
    }

    // Target the published paper defaults for deterministic captures.
    FTectonicSimulationParameters Params = Service->GetParameters();

    const int32 PaperSeed = 42;
    Params.Seed = PaperSeed;
    CachedSeed = PaperSeed;

    Params.MinAmplificationLOD = FMath::Max(Params.MinAmplificationLOD, 5);
    Params.RenderSubdivisionLevel = FMath::Max(Params.MinAmplificationLOD, 5);
    Params.bEnableAutomaticLOD = false;
    Params.bEnableOceanicAmplification = true;
    Params.bEnableContinentalAmplification = true;
    Params.bEnableHydraulicErosion = true;
    Params.bEnableContinentalErosion = true;
    Params.bEnableSedimentTransport = true;
    Params.bEnableOceanicDampening = true;
    Params.bSkipCPUAmplification = true;
    Params.VisualizationMode = ETectonicVisualizationMode::Amplified;

    CachedSubdivisionLevel = Params.SubdivisionLevel;

    if (IConsoleVariable* PaperDefaultsVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.PaperDefaults")))
    {
        PaperDefaultsVar->Set(1, ECVF_SetByCode);
    }
    if (IConsoleVariable* UseGPUVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.UseGPUAmplification")))
    {
        UseGPUVar->Set(1, ECVF_SetByCode);
    }
    if (IConsoleVariable* StageBProfilingVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.StageBProfiling")))
    {
        StageBProfilingVar->Set(1, ECVF_SetByCode);
    }

    Service->SetForceHydraulicErosionDisabled(false);

#if UE_BUILD_DEVELOPMENT
    Service->SetForceStageBGPUReplayForTests(false);
#endif

    Service->SetParameters(Params);

    Controller->SetPBRShadingEnabled(true);
    Controller->SetGPUPreviewMode(true);

    Service->ForceStageBAmplificationRebuild(TEXT("PaperReadyPreset"));
    Service->ProcessPendingOceanicGPUReadbacks(true, nullptr);
    Service->ProcessPendingContinentalGPUReadbacks(true, nullptr);

    if (!Service->IsStageBAmplificationReady())
    {
        Service->AdvanceSteps(1);
        Service->ProcessPendingOceanicGPUReadbacks(true, nullptr);
        Service->ProcessPendingContinentalGPUReadbacks(true, nullptr);
    }

    Controller->RebuildPreview();
    RefreshStageBReadinessFromService();
    RefreshCachedPaletteMode();

    const bool bStageBReady = Service->IsStageBAmplificationReady();
    bPaperReadyApplied = bStageBReady;

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[PaperReady] Applied (Seed=%d RenderLOD=%d StageBReady=%s)"),
        Params.Seed,
        Params.RenderSubdivisionLevel,
        bStageBReady ? TEXT("true") : TEXT("false"));

    return bStageBReady;
}

FReply SPTectonicToolPanel::HandlePrimeGPUStageBClicked()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            FTectonicSimulationParameters Params = Service->GetParameters();
            const bool bOriginalOceanic = Params.bEnableOceanicAmplification;
            const bool bOriginalContinental = Params.bEnableContinentalAmplification;
            const bool bOriginalSkip = Params.bSkipCPUAmplification;

            Params.bEnableOceanicAmplification = true;
            Params.bEnableContinentalAmplification = true;
            Params.bSkipCPUAmplification = false;

            const bool bParamsChanged = (bOriginalOceanic != Params.bEnableOceanicAmplification) ||
                (bOriginalContinental != Params.bEnableContinentalAmplification) ||
                (bOriginalSkip != Params.bSkipCPUAmplification);

            if (bParamsChanged)
            {
                Service->SetParameters(Params);
                Controller->RebuildPreview();
            }

            if (IConsoleVariable* UseGPU = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.UseGPUAmplification")))
            {
                UseGPU->Set(1, ECVF_SetByCode);
            }
            if (IConsoleVariable* SkipCPU = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.SkipCPUAmplification")))
            {
                SkipCPU->Set(0, ECVF_SetByCode);
            }

            UE_LOG(LogPlanetaryCreation, Log, TEXT("[StageB] GPU pipeline primed: oceanic+continental amplification enabled, CPU fallback active, GPU amplification cvar set."));
            RefreshStageBReadinessFromService();
        }
    }

    return FReply::Handled();
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
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Playback paused"));
    }
    else
    {
        PlaybackController->Play();
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Playback started"));
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
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Playback stopped"));
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
                Controller->RebuildPreview(); // warm preview mesh after reset
                UE_LOG(LogPlanetaryCreation, Log, TEXT("Timeline scrubbed to step %d (%.1f My)"),
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
                Controller->RebuildPreview(); // warm preview mesh after reset
                UE_LOG(LogPlanetaryCreation, Log, TEXT("Undo successful, mesh rebuilt"));
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
                Controller->RebuildPreview(); // warm preview mesh after reset
                UE_LOG(LogPlanetaryCreation, Log, TEXT("Redo successful, mesh rebuilt"));
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

    RefreshCachedPaletteMode();

    // Update camera controller every frame
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        Controller->TickCamera(InDeltaTime);

        const ETectonicVisualizationMode CurrentMode = Controller->GetVisualizationMode();
        if (!SelectedVisualizationOption.IsValid() || *SelectedVisualizationOption != CurrentMode)
        {
            RefreshSelectedVisualizationOption();
        }
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
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Camera reset to default view"));
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

void SPTectonicToolPanel::RefreshStageBReadinessFromService()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            HandleStageBReadyChanged(Service->IsStageBAmplificationReady(), Service->GetStageBAmplificationNotReadyReason());
        }
    }
}

void SPTectonicToolPanel::HandleStageBReadyChanged(bool bReady, EStageBAmplificationReadyReason Reason)
{
    bCachedStageBReady = bReady;
    CachedStageBReason = Reason;
    if (!bReady)
    {
        bPaperReadyApplied = false;
    }

    if (bReady)
    {
        CachedPaletteStatusText = FText::GetEmpty();
    }
    else
    {
        const FText ReasonText = FText::FromString(PlanetaryCreation::StageB::GetReadyReasonDescription(Reason));
        CachedPaletteStatusText = FText::Format(
            NSLOCTEXT("PlanetaryCreation", "StageBNotReadyHeightmapPaletteStatusFmt", "Stage B pending: {0}"),
            ReasonText);
    }
}

void SPTectonicToolPanel::RefreshCachedPaletteMode()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            CachedPaletteMode = Service->GetHeightmapPaletteMode();
        }
    }
}

void SPTectonicToolPanel::BindStageBReadyDelegate()
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            if (StageBReadyDelegateHandle.IsValid())
            {
                Service->OnStageBAmplificationReadyChanged().Remove(StageBReadyDelegateHandle);
                StageBReadyDelegateHandle = FDelegateHandle();
            }

            StageBReadyDelegateHandle = Service->OnStageBAmplificationReadyChanged()
                .AddSP(this, &SPTectonicToolPanel::HandleStageBReadyChanged);

            HandleStageBReadyChanged(Service->IsStageBAmplificationReady(), Service->GetStageBAmplificationNotReadyReason());
        }
    }
}

ECheckBoxState SPTectonicToolPanel::GetSeaLevelHighlightState() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (const UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            return Service->IsHighlightSeaLevelEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
        }
    }
    return ECheckBoxState::Unchecked;
}

void SPTectonicToolPanel::OnSeaLevelHighlightChanged(ECheckBoxState NewState)
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            const bool bEnabled = (NewState == ECheckBoxState::Checked);
            Service->SetHighlightSeaLevel(bEnabled);
            UE_LOG(LogPlanetaryCreation, Log, TEXT("Sea level highlight %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));

            if (!Controller->RefreshPreviewColors())
            {
                Controller->RebuildPreview(); // warm preview mesh after reset
            }
        }
    }
}
