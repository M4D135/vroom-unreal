#pragma once

#include <algorithm>
#include <cmath>

namespace Vroom
{
inline double ExponentialAlpha(double Rate, double Dt)
{
    return 1.0 - std::exp(-std::max(0.0, Rate) * std::max(0.0, Dt));
}

inline double SteeringDeadzone(double Input, double Deadzone)
{
    const double Zone = std::clamp(Deadzone, 0.0, 0.99);
    const double Magnitude = std::clamp((std::abs(Input) - Zone) / (1.0 - Zone), 0.0, 1.0);
    return std::copysign(Magnitude, Input);
}

// Retain the fractional simulation phase even when a long frame is capped.
// Dropping it restarts interpolation at the previous pose after each hitch.
struct FrameTiming
{
    static constexpr double StepSeconds = 1.0 / 120.0;
    static constexpr int MaxSteps = 12;
    double Remainder = 0;

    int Advance(double Dt)
    {
        if (!std::isfinite(Dt) || Dt <= 0) return 0;
        Remainder += std::min(Dt, MaxSteps * StepSeconds);
        const int Steps = std::min(MaxSteps, static_cast<int>(std::floor((Remainder + 1e-12) / StepSeconds)));
        Remainder = std::max(0.0, Remainder - Steps * StepSeconds);
        return Steps;
    }

    double Alpha() const { return std::clamp(Remainder / StepSeconds, 0.0, 1.0); }
};
}
