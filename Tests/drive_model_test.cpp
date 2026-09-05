#include "VroomDriveModel.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
int failures = 0;
void Check(bool result, const char* explanation)
{
    if (!result)
    {
        std::cerr << "FAIL: " << explanation << '\n';
        ++failures;
    }
}

void Run(Vroom::DriveModel& model, double seconds, const Vroom::DriveInput& input,
         double curvature = 0, double halfWidth = 6, double step = 1.0 / 120.0)
{
    const int steps = static_cast<int>(std::round(seconds / step));
    for (int i = 0; i < steps; ++i)
        model.Step(input, step, curvature, halfWidth);
}

bool Finite(const Vroom::DriveState& s)
{
    return std::isfinite(s.SpeedMps) && std::isfinite(s.DistanceM) &&
        std::isfinite(s.LateralM) && std::isfinite(s.HeadingRad) &&
        std::isfinite(s.Steering) && std::isfinite(s.Rpm) && std::isfinite(s.Elapsed);
}
}

int main()
{
    const Vroom::DriveInput full{1, 0, 0, false, false};
    const Vroom::DriveInput brake{0, 1, 0, false, false};
    const Vroom::DriveInput coast{};
    Vroom::DriveModel acceleration;
    Run(acceleration, 10, full);
    Check(acceleration.State.SpeedMps > 40, "full throttle reaches 144 km/h within ten seconds");
    Check(acceleration.State.DistanceM > 200, "acceleration advances track position");
    Check(acceleration.State.Gear >= 3, "automatic shifts through multiple original thresholds");
    Check(acceleration.State.LateralM == 0 && acceleration.State.HeadingRad == 0,
          "straight unsteered road does not drift");
    std::cout << "Prototype 10 s: " << acceleration.State.SpeedMps * 3.6
              << " km/h, gear " << acceleration.State.Gear << '\n';

    Vroom::DriveModel stopping = acceleration;
    Vroom::DriveModel coasting = acceleration;
    Run(stopping, 3, brake);
    Run(coasting, 3, coast);
    Check(stopping.State.SpeedMps < coasting.State.SpeedMps * 0.6,
          "service brake decelerates materially faster than coasting");
    Run(stopping, 12, brake);
    Check(stopping.State.SpeedMps == 0, "sustained braking stops without reverse motion");
    const double stoppedAt = stopping.State.DistanceM;
    Run(stopping, 3, brake);
    Check(stopping.State.DistanceM == stoppedAt, "brakes hold the stopped car");

    Vroom::DriveModel left, right;
    left.State.SpeedMps = right.State.SpeedMps = 25;
    left.State.Gear = right.State.Gear = 2;
    Run(right, 0.5, {0.3, 0, 0.5}, 0, 100);
    Run(left, 0.5, {0.3, 0, -0.5}, 0, 100);
    Check(right.State.HeadingRad > 0 && right.State.LateralM > 0,
          "positive steer turns and moves right");
    Check(std::abs(right.State.LateralM + left.State.LateralM) < 1e-10,
          "left and right handling are symmetric");

    Vroom::DriveModel curve;
    curve.State.SpeedMps = 30;
    curve.State.Gear = 3;
    Run(curve, 0.5, {0.3, 0, 0}, 0.01, 100);
    Check(curve.State.LateralM < 0 && curve.State.HeadingRad < 0,
          "right-hand road bends away from an unsteered car to the left");

    Vroom::DriveModel road = acceleration, grass = acceleration;
    grass.State.LateralM = 9;
    Run(road, 4, full);
    Run(grass, 4, full);
    Check(grass.State.SpeedMps < road.State.SpeedMps - 4,
          "off-road rolling resistance makes a meaningful speed difference");

    Vroom::DriveModel longRun;
    Run(longRun, 90, full);
    Check(longRun.State.Gear == 6, "sustained full throttle reaches sixth gear");
    Check(longRun.State.SpeedMps * 3.6 > 300 && longRun.State.SpeedMps * 3.6 < 370,
          "source torque and drag give a bounded prototype top speed");
    Check(longRun.State.Rpm >= 1000 && longRun.State.Rpm <= Vroom::DriveModel::MaxRpm,
          "rpm stays inside table limits");
    std::cout << "Prototype 90 s: " << longRun.State.SpeedMps * 3.6 << " km/h\n";

    Vroom::DriveModel sixtyHz, oneTwentyHz;
    Run(sixtyHz, 12, full, 0, 6, 1.0 / 60.0);
    Run(oneTwentyHz, 12, full);
    Check(std::abs(sixtyHz.State.SpeedMps - oneTwentyHz.State.SpeedMps) < 1e-10 &&
          std::abs(sixtyHz.State.DistanceM - oneTwentyHz.State.DistanceM) < 1e-8,
          "60-Hz owner and 120-Hz owner produce the same substep trajectory");

    Vroom::DriveModel manual;
    manual.Automatic = false;
    Run(manual, 1, {1, 0, 0, true, false});
    Check(manual.State.Gear == 2, "holding shift causes one gear change");
    manual.Step(coast, 1.0 / 120, 0, 6);
    manual.Step({0, 0, 0, true, false}, 1.0 / 120, 0, 6);
    Check(manual.State.Gear == 3, "releasing and pressing shift changes again");
    manual.Reset();
    Check(manual.State.Gear == 1 && manual.State.DistanceM == 0 && !manual.Automatic,
          "reset restores the car while preserving gearbox preference");

    Vroom::DriveModel invalid;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    invalid.Step(full, -1, 0, 6);
    invalid.Step(full, nan, 0, 6);
    Check(invalid.State.Elapsed == 0, "non-positive or non-finite dt is ignored");
    invalid.State.SpeedMps = nan;
    invalid.State.Gear = 999;
    invalid.State.LateralM = inf;
    invalid.Step({nan, inf, nan}, 100, nan, inf);
    Check(Finite(invalid.State) && invalid.State.Gear <= 6 && invalid.State.Gear >= 1,
          "invalid telemetry and controls cannot propagate NaNs");
    Check(invalid.State.Elapsed <= 0.25000001, "large hitch is bounded to a quarter second");
    for (int i = 0; i < 24000; ++i)
    {
        const double t = i / 120.0;
        invalid.Step({0.5 + 0.5 * std::sin(t), 0.2 + 0.2 * std::cos(t * 2),
                      std::sin(t * 3)}, 1.0 / 120, 0.02 * std::sin(t * 0.7), 6);
        if (!Finite(invalid.State))
            break;
    }
    Check(Finite(invalid.State), "long mixed driving stays numerically finite");

    if (failures)
        return EXIT_FAILURE;
    std::cout << "All drive-model behavioral checks passed.\n";
    return EXIT_SUCCESS;
}
