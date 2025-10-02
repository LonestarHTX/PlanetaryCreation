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

    // Initialize cached seed from service
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        if (UTectonicSimulationService* Service = Controller->GetSimulationService())
        {
            CachedSeed = Service->GetParameters().Seed;
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

int32 SPTectonicToolPanel::GetSeedValue() const
{
    return CachedSeed;
}

void SPTectonicToolPanel::OnSeedValueChanged(int32 NewValue)
{
    CachedSeed = NewValue;
}
