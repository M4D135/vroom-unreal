#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// Portable prototype, deliberately independent of Unreal's frame rate and physics.
// Source-derived engine parameters and approximations are detailed in docs/DRIVING.md.
namespace Vroom
{
struct DriveInput
{
    double Throttle = 0, Brake = 0, Steer = 0;
    bool ShiftUp = false, ShiftDown = false;
};

struct DriveState
{
    double SpeedMps = 0, DistanceM = 0, LateralM = 0, HeadingRad = 0;
    double Steering = 0, Rpm = 1000, Elapsed = 0;
    int Gear = 1;
};

class DriveModel
{
public:
    DriveState State;
    bool Automatic = true;

    // Original motor.asm tables. Internal speed = km/h * 1.5 = m/s * 5.4.
    inline static constexpr std::array<double, 6> GearSpeedToRpm =
        {18944, 14336, 10803, 8601, 7424, 5939};
    inline static constexpr std::array<double, 6> GearInertia =
        {14280, 18870, 25041, 31451, 36440, 45550};
    inline static constexpr std::array<double, 6> AutoDownInternal =
        {0, 105, 180, 255, 315, 360};
    inline static constexpr std::array<double, 6> AutoUpInternal =
        {120, 195, 270, 345, 405, 500};
    inline static constexpr double MaxRpm = 13312.0;

    void Reset()
    {
        State = DriveState{};
        ShiftDelay = 0;
        PreviousShiftUp = PreviousShiftDown = false;
    }

    // dt is seconds; curvature is radians/metre; roadHalfWidth is metres.
    // Inputs clamp to [0,1]/[-1,1]. Simultaneous brake and throttle prioritizes brake.
    // Gear buttons are rising-edge triggered in manual mode. Reset keeps Automatic.
    void Step(const DriveInput& input, double dt, double curvature, double roadHalfWidth)
    {
        if (!std::isfinite(dt) || dt <= 0)
            return;
        SanitizeState();
        DriveInput clean = input;
        clean.Throttle = FiniteClamp(input.Throttle, 0, 1, 0);
        clean.Brake = FiniteClamp(input.Brake, 0, 1, 0);
        clean.Steer = FiniteClamp(input.Steer, -1, 1, 0);
        if (clean.Brake > 0)
            clean.Throttle = 0;
        curvature = FiniteClamp(curvature, -0.2, 0.2, 0);
        roadHalfWidth = FiniteClamp(roadHalfWidth, 1, 100, 6);

        if (!Automatic)
        {
            const bool up = clean.ShiftUp && !PreviousShiftUp;
            const bool down = clean.ShiftDown && !PreviousShiftDown;
            if (up != down)
                ChangeGear(State.Gear + (up ? 1 : -1));
        }
        PreviousShiftUp = clean.ShiftUp;
        PreviousShiftDown = clean.ShiftDown;

        // A pause/hitch cannot advance the model by more than a quarter second.
        // The normal owner calls at 1/120 s; larger valid steps use the same substep.
        const double elapsed = std::min(dt, 0.25);
        const int count = std::max(1, static_cast<int>(std::ceil(elapsed * 120.0)));
        const double h = elapsed / count;
        for (int i = 0; i < count; ++i)
            Integrate(clean, h, curvature, roadHalfWidth);
    }

private:
    // Every 32nd sample of the original 16-rpm MOMTBL (0..13312 rpm).
    inline static constexpr std::array<double, 27> TorqueSamples =
        {0, 48, 94, 138, 180, 220, 257, 291, 324, 354, 382, 407, 430, 451,
         470, 486, 500, 511, 520, 527, 532, 534, 530, 520, 507, 492, 475};
    double ShiftDelay = 0;
    bool PreviousShiftUp = false, PreviousShiftDown = false;

    static double FiniteClamp(double value, double low, double high, double fallback)
    {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    }

    static double TorqueAt(double rpm)
    {
        const double entry = std::clamp(rpm, 0.0, MaxRpm) / 512.0;
        const int index = std::min(25, static_cast<int>(entry));
        const double fraction = entry - index;
        return TorqueSamples[index] * (1.0 - fraction) + TorqueSamples[index + 1] * fraction;
    }

    void SanitizeState()
    {
        State.SpeedMps = FiniteClamp(State.SpeedMps, 0, 120, 0);
        State.DistanceM = FiniteClamp(State.DistanceM, 0, 1e12, 0);
        State.LateralM = FiniteClamp(State.LateralM, -10000, 10000, 0);
        State.HeadingRad = FiniteClamp(State.HeadingRad, -1.35, 1.35, 0);
        State.Steering = FiniteClamp(State.Steering, -1, 1, 0);
        State.Rpm = FiniteClamp(State.Rpm, 1000, MaxRpm, 1000);
        State.Elapsed = FiniteClamp(State.Elapsed, 0, 1e12, 0);
        State.Gear = std::clamp(State.Gear, 1, 6);
    }

    void ChangeGear(int gear)
    {
        gear = std::clamp(gear, 1, 6);
        if (State.Gear != gear)
        {
            State.Gear = gear;
            ShiftDelay = 0.10;
        }
    }

    void Integrate(const DriveInput& input, double h, double curvature, double roadHalfWidth)
    {
        const double internalSpeed = State.SpeedMps * 5.4;
        if (Automatic && ShiftDelay <= 0)
        {
            const int index = State.Gear - 1;
            if (internalSpeed > AutoUpInternal[index] && State.Gear < 6)
                ChangeGear(State.Gear + 1);
            else if (internalSpeed < AutoDownInternal[index] && State.Gear > 1)
                ChangeGear(State.Gear - 1);
        }
        const int gear = State.Gear - 1;
        const double wheelRpm = internalSpeed * GearSpeedToRpm[gear] / 256.0;
        // Simple automatic clutch for a usable standing start; not an original port.
        const double launchRpm = 1000.0 + input.Throttle * 7500.0;
        const double targetRpm = std::clamp(std::max(wheelRpm, launchRpm), 1000.0, MaxRpm);
        State.Rpm += (targetRpm - State.Rpm) * (1.0 - std::exp(-h / 0.09));

        // Wheel footprint is approximated by a 1.6-m track, giving gradual kerb entry.
        const double edgeBlend = std::clamp((std::abs(State.LateralM) + 0.8 - roadHalfWidth) / 1.6, 0.0, 1.0);
        const double rollingMoment = 10.0 + 198.0 * edgeBlend;
        const double airMoment = internalSpeed * internalSpeed / 468.0;
        const double brakeMoment = std::pow(94.0 * input.Brake, 2) / 16.0;
        const double torque = (ShiftDelay > 0 || wheelRpm >= MaxRpm) ? 0.0 : TorqueAt(State.Rpm) * input.Throttle;
        const double netMoment = torque - brakeMoment - airMoment - 10.0 - rollingMoment;
        // Continuous SI version of SPEDC4 DV=256*moment/GVVTBL, at adopted 20 Hz.
        double acceleration = (256.0 * 20.0 / (GearInertia[gear] * 5.4)) * netMoment;
        if (input.Throttle == 0 && State.SpeedMps > 0)
            acceleration -= 1.2; // Engine braking simplification, see DRIVING.md.
        State.SpeedMps = std::clamp(State.SpeedMps + acceleration * h, 0.0, 120.0);

        const double steerResponse = 1.0 - std::exp(-h / 0.12);
        State.Steering += (input.Steer - State.Steering) * steerResponse;
        const double speed = State.SpeedMps;
        const double maxWheelAngle = 0.38 / (1.0 + speed / 22.0);
        const double requestedYaw = speed * std::tan(State.Steering * maxWheelAngle) / 2.75;
        const double gripAcceleration = (20.0 + 0.0015 * speed * speed) * (1.0 - 0.45 * edgeBlend);
        const double maxYaw = gripAcceleration / std::max(speed, 1.0);
        const double yawRate = std::clamp(requestedYaw, -maxYaw, maxYaw);
        // Frenet coordinates: rightward lateral offset and rightward heading are positive.
        const double metric = std::clamp(1.0 - curvature * State.LateralM, 0.3, 3.0);
        const double progress = speed * std::cos(State.HeadingRad) / metric;
        State.HeadingRad = std::clamp(State.HeadingRad + (yawRate - curvature * progress) * h, -1.35, 1.35);
        State.LateralM = std::clamp(State.LateralM + speed * std::sin(State.HeadingRad) * h, -10000.0, 10000.0);
        State.DistanceM = std::min(1e12, State.DistanceM + progress * h);
        State.Elapsed = std::min(1e12, State.Elapsed + h);
        ShiftDelay = std::max(0.0, ShiftDelay - h);
    }
};
} // namespace Vroom
