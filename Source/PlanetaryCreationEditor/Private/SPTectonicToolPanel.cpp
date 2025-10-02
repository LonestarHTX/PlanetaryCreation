#include "SPTectonicToolPanel.h"

#include "TectonicSimulationController.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SPTectonicToolPanel::Construct(const FArguments& InArgs)
{
    ControllerWeak = InArgs._Controller;

    ChildSlot
    [
        SNew(SBorder)
        .Padding(12.0f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(this, &SPTectonicToolPanel::GetCurrentTimeLabel)
            ]

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

FText SPTectonicToolPanel::GetCurrentTimeLabel() const
{
    if (const TSharedPtr<FTectonicSimulationController> Controller = ControllerWeak.Pin())
    {
        const double CurrentTime = Controller->GetCurrentTimeMy();
        return FText::Format(NSLOCTEXT("PlanetaryCreation", "CurrentTimeLabel", "Current Time: {0} My"), FText::AsNumber(CurrentTime));
    }

    return NSLOCTEXT("PlanetaryCreation", "CurrentTimeUnavailable", "Current Time: n/a");
}
