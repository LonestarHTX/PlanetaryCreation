#pragma once

#include "RHIResources.h"

class UTectonicSimulationService;

namespace PlanetaryCreation::GPU
{
    struct FStageBUnifiedDispatchResult
    {
        bool bOceanicRequested = false;
        bool bContinentalRequested = false;
        bool bExecutedOceanic = false;
        bool bExecutedContinental = false;
        double OceanicDispatchSeconds = 0.0;
        double ContinentalDispatchSeconds = 0.0;
    };

    /**
     * Dispatches the unified Stage B GPU pipeline for the requested crust types.
     * @param Service Simulation service providing cached buffers and dispatch bookkeeping.
     * @param bDispatchOceanic When true, runs the oceanic kernel.
     * @param bDispatchContinental When true, runs the continental kernel.
     * @param OutResult Filled with execution flags and CPU-side dispatch durations.
     * @return true if at least one requested kernel executed successfully.
     */
    bool ApplyStageBUnifiedGPU(UTectonicSimulationService& Service, bool bDispatchOceanic, bool bDispatchContinental, FStageBUnifiedDispatchResult& OutResult);

    /**
     * GPU preview path: Writes Stage B oceanic amplification directly to PF_R16F texture.
     * Eliminates CPU readback by keeping displacement data on-device for WPO material consumption.
     * @param Service Simulation service with current state
     * @param OutHeightTexture RHI texture reference that will receive the height data
     * @param TextureSize Equirectangular texture dimensions (e.g., 2048x1024)
     * @return true if preview texture was written successfully, false on error
     */
    bool ApplyOceanicAmplificationGPUPreview(
        UTectonicSimulationService& Service,
        FTextureRHIRef& OutHeightTexture,
        FIntPoint TextureSize,
        int32* OutLeftCoverage = nullptr,
        int32* OutRightCoverage = nullptr,
        int32* OutMirroredCoverage = nullptr);
}
