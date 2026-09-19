#include "VroomDriveModel.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
int Failures = 0;
void Check(bool passed, const char* message)
{
    if (!passed) { std::cerr << "FAIL: " << message << '\n'; ++Failures; }
}
constexpr double HalfWidth = 4.75;
constexpr double RoadInterior = HalfWidth - .8;

struct Recovery
{
    bool Returned = false;
    double Seconds = 0;
    Vroom::DriveState State;
};

Recovery Recover(double side, double curve, double condition, double grade, int fps, bool meterThrottle = true)
{
    Vroom::DriveModel model;
    model.State.LateralM = 7 * side;
    model.TyreHealth = 2048 * condition;
    // Standstill after the player leaves the road: no teleport, boost, hop or
    // recovery timer. Only accelerator + steering back toward the asphalt.
    for (int tick = 0; tick < 8 * fps; ++tick)
    {
        // As with an ordinary return from the verge, moderate speed while
        // turning across it. On destroyed tyres, full throttle on a steep
        // downhill hairpin can legitimately send a moving car wide again.
        const double throttle = meterThrottle ? std::clamp((15.0 - model.State.SpeedMps) / 3.0, 0.0, 1.0) : 1.0;
        model.Step({throttle, 0, -side}, 1.0 / fps, curve, HalfWidth, grade);
        if (std::abs(model.State.LateralM) <= RoadInterior)
            return {true, (tick + 1.0) / fps, model.State};
    }
    return {false, 8, model.State};
}

Vroom::DriveModel Run(double side, double curve, double condition, double grade,
                      int fps, const Vroom::DriveInput& input, double seconds)
{
    Vroom::DriveModel model;
    model.State.LateralM = 7 * side;
    model.TyreHealth = 2048 * condition;
    for (int tick = 0; tick < static_cast<int>(seconds * fps); ++tick)
        model.Step(input, 1.0 / fps, curve, HalfWidth, grade);
    return model;
}
}

int main()
{
    double worstSeconds = 0;
    int recoveries = 0;
    for (double curve : {-.024, -.01, 0.0, .01, .024})
        for (double side : {-1.0, 1.0})
            for (double condition : {0.0, .2, .83, 1.0})
                for (double grade : {-.18, 0.0, .18})
                    for (int fps : {30, 60, 120})
                    {
                        const Recovery r = Recover(side, curve, condition, grade, fps);
                        if (!r.Returned)
                            std::cerr << "curve=" << curve << " side=" << side << " tyres=" << condition
                                      << " grade=" << grade << " fps=" << fps << " speed=" << r.State.SpeedMps
                                      << " lateral=" << r.State.LateralM << '\n';
                        Check(r.Returned, "standing car can steer off grass and fully back onto road within eight seconds");
                        Check(r.State.DistanceM > 3 && r.State.SpeedMps > 5,
                              "recovery comes from driving forward, without repositioning a stationary car");
                        worstSeconds = std::max(worstSeconds, r.Seconds);
                        ++recoveries;
                    }

    // Exact reported state (83% tyres), including both sides of the hairpin:
    // holding full throttle must suffice, without needing to nurse a stuck car.
    for (double side : {-1.0, 1.0})
        for (double curve : {-.024, -.01, .01, .024})
            for (double grade : {-.18, 0.0, .18})
            {
                const auto fullThrottle = Recover(side, curve, .83, grade, 120, false);
                Check(fullThrottle.Returned && fullThrottle.Seconds < 5,
                      "reported 83%-tyre state restarts and returns even holding full throttle");
            }

    // Check the same elapsed instant, rather than the first outer-frame sample
    // crossing the road edge (which is necessarily quantized by capture fps).
    for (double condition : {0.0, .83, 1.0})
    {
        const auto reference = Run(-1, .024, condition, .18, 120, {1, 0, 1}, 3);
        for (int fps : {30, 60})
        {
            const auto candidate = Run(-1, .024, condition, .18, fps, {1, 0, 1}, 3);
            Check(std::abs(candidate.State.SpeedMps - reference.State.SpeedMps) < 1e-10 &&
                  std::abs(candidate.State.DistanceM - reference.State.DistanceM) < 1e-10 &&
                  std::abs(candidate.State.LateralM - reference.State.LateralM) < 1e-10 &&
                  std::abs(candidate.VisualHeading() - reference.VisualHeading()) < 1e-10,
                  "off-road restart and cockpit yaw are identical at 30/60/120 Hz");
        }
        const auto mirror = Run(1, -.024, condition, .18, 120, {1, 0, -1}, 3);
        Check(std::abs(mirror.State.LateralM + reference.State.LateralM) < 1e-10 &&
              std::abs(mirror.State.DistanceM - reference.State.DistanceM) < 1e-10 &&
              std::abs(mirror.VisualHeading() + reference.VisualHeading()) < 1e-10,
              "left and right restart trajectories and visual yaw remain symmetric");
    }

    for (double side : {-1.0, 1.0})
    {
        const auto held = Run(side, .024, .83, -.18, 120, {1, 1, -side}, 3);
        Check(held.State.SpeedMps == 0 && held.State.DistanceM == 0 && held.State.LateralM == 7 * side,
              "brake takes priority over throttle and holds an off-road car even downhill");
        Check(!held.Skidding, "an immobile car does not falsely skid in a bend");
        Vroom::DriveModel empty;
        empty.State.LateralM = 7 * side;
        empty.Fuel = 0;
        for (int tick = 0; tick < 360; ++tick)
            empty.Step({1, 0, -side}, 1.0 / 120, .024, HalfWidth);
        Check(empty.State.SpeedMps == 0 && empty.State.DistanceM == 0 && empty.State.LateralM == 7 * side,
              "restart assistance does not move a car with an empty tank");
    }

    // Grass must still penalize speed. Recovery changes loading, not its rolling
    // resistance; a straight standing start on grass stays slower than on tarmac.
    const auto grass = Run(-1, 0, .83, 0, 120, {1, 0, 0}, 3);
    const auto tarmac = Run(0, 0, .83, 0, 120, {1, 0, 0}, 3);
    Check(grass.State.SpeedMps < tarmac.State.SpeedMps - 5 &&
          grass.State.DistanceM < tarmac.State.DistanceM - 5,
          "off-road rolling resistance remains a substantial time and speed penalty");

    // Original high-speed wash remains unchanged above 144 km/h. This also
    // catches accidental changes to the public helper used by the auto-driver.
    for (double speed : {40.0, 60.0, 80.0})
        for (double curve : {-.024, -.01, .01, .024})
            Check(std::abs(Vroom::DriveModel::ArcadeWashLatMps(speed, curve) -
                           speed * curve * Vroom::DriveModel::ArcadeWashLengthM) < 1e-12,
                  "full-speed corner wash preserves its previous calibration");

    std::cout << "Off-road restart matrix: " << recoveries << " cases; longest return " << worstSeconds << " s\n";
    std::cout << (Failures ? "Off-road restart checks FAILED\n" : "All off-road restart checks passed.\n");
    return Failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
