#pragma once

namespace aiverse {
// Accumulate wall time of completed simulation steps on the calling thread.
// The sink belongs to the caller and is restored when the scope ends.
class SimulationStepTiming {
public:
    explicit SimulationStepTiming(double& milliseconds);
    ~SimulationStepTiming();
    SimulationStepTiming(const SimulationStepTiming&) = delete;
    SimulationStepTiming& operator=(const SimulationStepTiming&) = delete;
private:
    double* previous_;
};
}
