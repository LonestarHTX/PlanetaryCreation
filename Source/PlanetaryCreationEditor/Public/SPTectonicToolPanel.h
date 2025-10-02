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
    FText GetCurrentTimeLabel() const;

    TWeakPtr<FTectonicSimulationController> ControllerWeak;
};
