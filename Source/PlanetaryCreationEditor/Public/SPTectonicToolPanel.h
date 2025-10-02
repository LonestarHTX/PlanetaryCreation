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

    // Parameter accessors for spin boxes
    int32 GetSeedValue() const;
    void OnSeedValueChanged(int32 NewValue);

    TWeakPtr<FTectonicSimulationController> ControllerWeak;

    // Cached parameter values (updated on regenerate)
    int32 CachedSeed = 42;
};
