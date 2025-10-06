#pragma once

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
}
