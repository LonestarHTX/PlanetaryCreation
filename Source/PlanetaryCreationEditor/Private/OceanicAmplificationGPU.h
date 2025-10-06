#pragma once

#include "RHIResources.h"

class UTectonicSimulationService;

namespace PlanetaryCreation::GPU
{
    /**
     * Attempts to execute Stage B oceanic amplification on the GPU.
     * @return true if the GPU path ran and produced output, false to fall back to CPU.
     */
    bool ApplyOceanicAmplificationGPU(UTectonicSimulationService& Service);

    /**
     * Attempts to execute Stage B continental amplification on the GPU.
     * @return true if the GPU path ran and produced output, false to fall back to CPU.
     */
    bool ApplyContinentalAmplificationGPU(UTectonicSimulationService& Service);

    /**
     * GPU preview path: Writes Stage B oceanic amplification directly to PF_R16F texture.
     * Eliminates CPU readback by keeping displacement data on-device for WPO material consumption.
     * @param Service Simulation service with current state
     * @param OutHeightTexture RHI texture reference that will receive the height data
     * @param TextureSize Equirectangular texture dimensions (e.g., 2048x1024)
     * @return true if preview texture was written successfully, false on error
     */
    bool ApplyOceanicAmplificationGPUPreview(UTectonicSimulationService& Service, FTextureRHIRef& OutHeightTexture, FIntPoint TextureSize);
}
