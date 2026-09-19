#pragma once
#include <algorithm>
#include <cmath>

namespace Vroom
{
constexpr double WheelTau = 6.28318530717958647692;

// Wheel meshes use X forward and their axle along Y. Positive Y rotation
// moves the bottom of the tyre backwards, matching forward travel.
inline double WheelRollAdvance(double PhaseRad, double SignedDistanceM, double RadiusM)
{
    if (!std::isfinite(PhaseRad) || !std::isfinite(SignedDistanceM)
        || !std::isfinite(RadiusM) || RadiusM <= 0) return 0;
    return std::remainder(PhaseRad + SignedDistanceM / RadiusM, WheelTau);
}

struct WheelVisualState
{
    double FrontRollRad = 0;
    double RearRollRad = 0;
    double PreviousDistanceM = 0;
    bool Initialized = false;

    void Reset() { *this = {}; }

    // Observe interpolated progress, rather than accumulating render delta
    // time: duplicate presentation calls, pauses and frame rates cannot add
    // revolutions. Repositioning rebases progress without spinning the tyres.
    void Observe(double DistanceM, double SpeedMps, double Dt, bool Active)
    {
        if (!std::isfinite(DistanceM)) { Initialized = false; return; }
        const double DeltaM = DistanceM - PreviousDistanceM;
        const bool HadSample = Initialized;
        PreviousDistanceM = DistanceM;
        Initialized = true;
        if (!HadSample || !Active || !std::isfinite(SpeedMps)
            || !std::isfinite(Dt) || Dt <= 0) return;
        const double LimitM = std::max(3.0, 2 * std::abs(SpeedMps) * std::clamp(Dt, 0.0, .25) + .5);
        if (std::abs(DeltaM) > LimitM) return;
        FrontRollRad = WheelRollAdvance(FrontRollRad, DeltaM, .35);
        RearRollRad = WheelRollAdvance(RearRollRad, DeltaM, .38);
    }
};

// The visual steering stays legible at racing speed without altering the
// drive model. Follow the actual signed input, including countersteering;
// do not turn wheels automatically toward the chassis slip direction.
inline double FrontWheelYaw(double Steering, double SpeedMps, double Side,
    double Scale = .45, double DampingMps = 90)
{
    if (!std::isfinite(Steering) || !std::isfinite(SpeedMps)
        || !std::isfinite(Side) || !std::isfinite(Scale) || !std::isfinite(DampingMps)) return 0;
    const double Center = std::clamp(Steering, -1.0, 1.0) * std::clamp(Scale, 0.0, .65)
        / (1 + std::abs(SpeedMps) / std::max(DampingMps, 1.0));
    const double Angle = std::abs(Center);
    if (Angle < 1.e-8) return 0;
    constexpr double WheelbaseM = 2.43, HalfFrontTrackM = .91;
    const double TurnRadiusM = WheelbaseM / std::tan(Angle);
    const bool Inner = Center * Side > 0;
    const double Ackermann = std::atan(WheelbaseM
        / std::max(.5, TurnRadiusM + (Inner ? -HalfFrontTrackM : HalfFrontTrackM)));
    return std::copysign(std::min(.50, Angle + .45 * (Ackermann - Angle)), Center);
}
}
