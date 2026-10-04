#include "VroomDriveModel.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

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

double SteeringThreshold(double initial, double target, double threshold, bool rising)
{
    Vroom::DriveModel model;
    model.State.Steering = initial;
    for (int i = 1; i <= 240; ++i)
    {
        model.Step({0, 0, target}, 1.0 / 120, 0, 100);
        if (rising ? model.State.Steering >= threshold : model.State.Steering <= threshold)
            return i / 120.0;
    }
    return 2;
}

struct HandlingMetrics
{
    double LateralRateMps, VisualHeading, ReleaseLateralDelta;
};

HandlingMetrics MeasureHandling(double speed)
{
    Vroom::DriveModel turn;
    turn.State.SpeedMps = speed;
    turn.State.Gear = speed > 60 ? 6 : 3;
    turn.State.Rpm = speed * 5.4 * Vroom::DriveModel::GearSpeedToRpm[turn.State.Gear - 1] / 256;
    // Let the wheel catch up, then measure the XBCMP3 lateral rate on a
    // straight (arcade is parallel: no yaw circle).
    Run(turn, 0.15, {0.5, 0, 1}, 0, 100);
    const double lat1 = turn.State.LateralM;
    Run(turn, 0.25, {0.5, 0, 1}, 0, 100);
    const double rate = (turn.State.LateralM - lat1) / 0.25;
    const double visual = turn.VisualHeading();
    const double lat2 = turn.State.LateralM;
    Run(turn, 0.3, {0.5, 0, 0}, 0, 100);
    return {rate, visual, turn.State.LateralM - lat2};
}

void PrintHandlingBenchmark()
{
    std::cout << "Steering to 90%: " << SteeringThreshold(0, 1, 0.9, true) * 1000
              << " ms; release to 10%: " << SteeringThreshold(1, 0, 0.1, false) * 1000
              << " ms; countersteer crosses zero: " << SteeringThreshold(1, -1, 0, false) * 1000 << " ms\n";
    for (double speed : {40.0, 80.0})
    {
        const auto metrics = MeasureHandling(speed);
        std::cout << "Arcade slide at " << speed * 3.6 << " km/h: lat rate " << metrics.LateralRateMps
                  << " m/s; visual heading " << metrics.VisualHeading
                  << " rad; extra lateral in 300 ms after release " << metrics.ReleaseLateralDelta << " m\n";
    }
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--handling-benchmark")
    {
        PrintHandlingBenchmark();
        return EXIT_SUCCESS;
    }
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
    Check(std::abs(acceleration.State.SpeedMps * 3.6 - 297.215) < 0.01,
          "handling calibration preserves the established flat ten-second acceleration");
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

    Check(SteeringThreshold(0, 1, 0.9, true) <= 0.12,
          "digital steering reaches 90 percent in at most 120 ms");
    Check(SteeringThreshold(1, 0, 0.1, false) <= 0.08,
          "release removes 90 percent of steering in at most 80 ms");
    Check(SteeringThreshold(1, -1, 0, false) <= 0.03,
          "countersteer cancels the old steering direction within 30 ms");
    const auto mediumTurn = MeasureHandling(40), fastTurn = MeasureHandling(80);
    const double expectedMedium = Vroom::DriveModel::ArcadeSteerLatMps(1, 40);
    const double expectedFast = Vroom::DriveModel::ArcadeSteerLatMps(1, 80);
    Check(mediumTurn.LateralRateMps > expectedMedium * 0.85 && mediumTurn.LateralRateMps < expectedMedium * 1.05,
          "144-km/h full steering matches XBCMP3 volant*speed lateral rate");
    Check(fastTurn.LateralRateMps > expectedFast * 0.85 && fastTurn.LateralRateMps < expectedFast * 1.05,
          "288-km/h full steering scales with speed like XBCMP3");
    Check(std::abs(mediumTurn.VisualHeading) < 0.22 && std::abs(fastTurn.VisualHeading) < 0.22,
          "arcade slide on a straight is a lane change, not a rally yaw");
    Check(std::abs(mediumTurn.ReleaseLateralDelta) < 0.8 && std::abs(fastTurn.ReleaseLateralDelta) < 1.2,
          "releasing the wheel on a straight stops the sideways slide");
    PrintHandlingBenchmark();

    Vroom::DriveModel curve;
    curve.State.SpeedMps = 30;
    curve.State.Gear = 3;
    Run(curve, 0.5, {0.3, 0, 0}, 0.04, 100);
    Check(curve.State.LateralM < -0.4,
          "right-hand road washes an unsteered car to the left (XBCMP3 curve delta)");
    Check(std::abs(curve.VelocityHeading()) < 1e-9,
          "arcade motion stays parallel (haldco cleared, no world-straight yaw)");
    Check(std::abs(curve.VisualHeading()) > 0.04 && std::abs(curve.VisualHeading()) < 0.35,
          "the slide is visible as chassis slip, not as a guided rail");

    Vroom::DriveModel precise;
    precise.State.SpeedMps = 70;
    precise.State.Gear = 6;
    Run(precise, 0.4, {0.5, 0, 0.15}, 0, 100);
    Check(std::abs(precise.State.LateralM) > 0.4 && std::abs(precise.State.LateralM) < 8.0,
          "a small F1 steering input at 252 km/h places the car instead of washing out");
    Check(!precise.Skidding, "a light steering input does not start a rally slide");
    Vroom::DriveModel lockup;
    lockup.State.SpeedMps = 40;
    lockup.State.Gear = 4;
    lockup.State.Steering = 0.5;
    Run(lockup, 0.25, {0, 1, 0.5}, 0, 100);
    Check(lockup.Skidding && std::abs(lockup.VelocityHeading()) < 1e-9,
          "arcade LIMFRN screams but does not zero volant (FROTT skip is commented out)");
    Vroom::DriveModel worn;
    worn.TyreHealth = 200;
    worn.ChangeTyres();
    Check(worn.TyreHealth == 2048, "pit T restores TIRUSE to 2048");

    Vroom::DriveModel straighten;
    straighten.State.SpeedMps = 40;
    straighten.State.Gear = 3;
    Run(straighten, 0.4, {0.5, 0, 1}, 0, 100);
    const double slid = straighten.State.LateralM;
    Run(straighten, 0.35, {0.5, 0, 0}, 0, 100);
    Check(std::abs(straighten.VelocityHeading()) < 1e-9,
          "arcade heading stays parallel after release");
    Check(std::abs(straighten.State.LateralM - slid) < 1.5,
          "after release the car goes straight instead of drifting sideways");

    Vroom::DriveModel coastCorner, holdCorner;
    coastCorner.State.SpeedMps = holdCorner.State.SpeedMps = 40;
    coastCorner.State.Gear = holdCorner.State.Gear = 3;
    Run(coastCorner, 0.8, {0.4, 0, 0}, 0.04, 100);
    Run(holdCorner, 0.8, {0.4, 0, 0.85}, 0.04, 100);
    Check(coastCorner.State.LateralM < -6.0,
          "an unsteered car runs wide of a right-hander instead of taking it alone");
    Check(holdCorner.State.LateralM > coastCorner.State.LateralM + 2.0,
          "holding the wheel through a bend keeps the car tighter than coasting");

    Vroom::DriveModel scream;
    scream.State.SpeedMps = 80;
    scream.State.Gear = 6;
    Run(scream, 0.4, {0.5, 0, 0.9}, 0.08, 100);
    Check(scream.Skidding, "too fast for CRIVIR makes the tyres scream");

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

    Vroom::DriveModel manoeuvre60, manoeuvre120;
    manoeuvre60.State.SpeedMps = manoeuvre120.State.SpeedMps = 40;
    manoeuvre60.State.Gear = manoeuvre120.State.Gear = 3;
    for (double steer : {1.0, -1.0, 0.0})
    {
        Run(manoeuvre60, 0.25, {0.5, 0, steer}, 0.004, 100, 1.0 / 60);
        Run(manoeuvre120, 0.25, {0.5, 0, steer}, 0.004, 100, 1.0 / 120);
    }
    Check(std::abs(manoeuvre60.State.Steering - manoeuvre120.State.Steering) < 1e-12 &&
          std::abs(manoeuvre60.State.HeadingRad - manoeuvre120.State.HeadingRad) < 1e-12 &&
          std::abs(manoeuvre60.State.LateralM - manoeuvre120.State.LateralM) < 1e-10,
          "steering, countersteering and release are identical for 60/120-Hz owners");

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

    const Vroom::DriveInput driftHeld{0.6, 0, 0.8, false, false, true};
    const Vroom::DriveInput counterHeld{0.6, 0, -0.45, false, false, true};
    const Vroom::DriveInput straightDriftHeld{0.6, 0, 0, false, false, true};
    Vroom::DriveModel drift;
    drift.State.SpeedMps = 40;
    drift.State.Gear = 4;
    Run(drift, 0.12, driftHeld, .003);
    Check(drift.Drifting && drift.Skidding && drift.HopHeightM > 0.05,
          "drift button plus steer at speed starts a hop-drift");
    Run(drift, 0.18, driftHeld, .003);
    Check(std::abs(drift.VisualHeading()) > 0.04,
          "a drift shows a readable chassis angle within 0.30 seconds while retaining yaw inertia");
    Run(drift, 0.65, counterHeld, .003);
    Check(drift.DriftCharge > 0.4 && drift.DriftDirection == 1 && drift.DriftControl > .9,
          "countersteering charges the initial slide without reversing it");
    const double beforeBoost = drift.State.SpeedMps;
    Run(drift, 0.35, {0.6, 0, 0});
    Check(!drift.Drifting && drift.BoostTier >= 1, "releasing a controlled drift fires a mini-turbo");
    Check(drift.State.SpeedMps > beforeBoost + 2.0, "mini-turbo adds speed after the slide");

    Vroom::DriveModel straightDrift;
    straightDrift.State.SpeedMps = 40;
    straightDrift.State.Gear = 4;
    Run(straightDrift, 2, straightDriftHeld, 0, 100);
    Check(!straightDrift.Drifting && straightDrift.DriftCharge == 0 && straightDrift.HopHeightM == 0,
          "holding the drift button straight ahead does not hop or charge a turbo");
    Run(straightDrift, 0.1, full, 0, 100);
    Check(straightDrift.BoostTier == 0, "straight-line drift hold cannot farm a release boost");

    Vroom::DriveModel chargedDrift;
    chargedDrift.State.SpeedMps = 40;
    chargedDrift.State.Gear = 4;
    Run(chargedDrift, .3, driftHeld, .003, 100);
    Run(chargedDrift, 1.2, counterHeld, .003, 100);
    Check(chargedDrift.Drifting && chargedDrift.DriftCharge >= 1,
          "sustained moderate countersteer builds the second charge tier");
    Vroom::DriveModel strongBoost = chargedDrift;
    strongBoost.Step(full, 1.0 / 120, 0, 100);
    Check(strongBoost.BoostTier == 2 && strongBoost.BoostRemaining > 1,
          "releasing a long controlled drift rewards the stronger turbo");
    const double boostedSpeed = strongBoost.State.SpeedMps;
    Run(strongBoost, 0.1, brake, 0, 100);
    Check(strongBoost.BoostTier == 0 && strongBoost.BoostRemaining == 0 &&
          strongBoost.State.SpeedMps < boostedSpeed,
          "braking cancels the turbo and decelerates immediately");

    Vroom::DriveModel shortDrift;
    shortDrift.State.SpeedMps = 40;
    shortDrift.State.Gear = 4;
    Run(shortDrift, 0.2, driftHeld, .003, 100);
    Run(shortDrift, 0.2, counterHeld, .003, 100);
    shortDrift.Step(full, 1.0 / 120, 0, 100);
    Check(shortDrift.BoostTier == 0, "a drift below the first charge threshold cannot claim a turbo");

    for (int abortReason = 0; abortReason < 4; ++abortReason)
    {
        Vroom::DriveModel aborted = chargedDrift;
        Vroom::DriveInput held = counterHeld;
        if (abortReason == 0)
            aborted.State.LateralM = 8;
        if (abortReason == 1)
            aborted.State.SpeedMps = 5;
        if (abortReason == 3)
            held = straightDriftHeld;
        for (int frame = 0; frame < (abortReason == 3 ? 60 : 1); ++frame)
            aborted.Step(held, 1.0 / 120, 0, abortReason == 0 ? 6 : 100, 0, abortReason == 2);
        Check(!aborted.Drifting && aborted.DriftCharge == 0 && aborted.BoostTier == 0,
              "grass, insufficient speed, a jump or prolonged loss of countersteer forfeits the charge");
        aborted.State.LateralM = 0;
        aborted.State.SpeedMps = 40;
        Run(aborted, 0.1, driftHeld, .003, 100);
        Check(!aborted.Drifting && aborted.BoostTier == 0,
              "an aborted held drift cannot silently restart after returning to the road");
        Run(aborted, .3, full, .003, 100);
        aborted.Step(driftHeld, 1.0 / 120, .003, 100);
        Check(aborted.Drifting && aborted.DriftCharge < 0.02,
              "release then press after the rearm window starts a fresh uncharged drift");
    }
    Vroom::DriveModel grassRelease = chargedDrift;
    grassRelease.State.LateralM = 8;
    grassRelease.Step(full, 1.0 / 120, 0, 6);
    Check(grassRelease.BoostTier == 0,
          "releasing a charged drift on grass does not award a turbo");

    Vroom::DriveModel drift30, drift60, drift120, drift240;
    for (auto* model : {&drift30, &drift60, &drift120, &drift240})
    {
        model->State.SpeedMps = 40;
        model->State.Gear = 4;
    }
    const Vroom::DriveInput fullLockDrift{0.6, 0, 1, false, false, true};
    const std::array<Vroom::DriveInput, 3> phases{fullLockDrift, counterHeld, full};
    const std::array<double, 3> durations{.3, 1.2, 1.0};
    for (int phase = 0; phase < 3; ++phase)
    {
        const auto& input = phases[phase];
        Run(drift30, durations[phase], input, .003, 100, 1.0 / 30);
        Run(drift60, durations[phase], input, .003, 100, 1.0 / 60);
        Run(drift120, durations[phase], input, .003, 100, 1.0 / 120);
        Run(drift240, durations[phase], input, .003, 100, 1.0 / 240);
        for (const auto* model : {&drift30, &drift60})
            Check(std::abs(model->State.HeadingRad - drift120.State.HeadingRad) < 1e-12 &&
                  std::abs(model->State.LateralM - drift120.State.LateralM) < 1e-10 &&
                  std::abs(model->State.SpeedMps - drift120.State.SpeedMps) < 1e-10,
                  "drift entry, countersteer and release are identical at 30/60/120 Hz");
        Check(std::abs(drift240.State.HeadingRad - drift120.State.HeadingRad) < 0.01 &&
              std::abs(drift240.State.LateralM - drift120.State.LateralM) < 0.15 &&
              std::abs(drift240.State.SpeedMps - drift120.State.SpeedMps) < 0.25,
              "240-Hz drift converges to the same trajectory and visual yaw");
    }
    Vroom::DriveModel smoothDrift;
    smoothDrift.State.SpeedMps = 40;
    smoothDrift.State.Gear = 4;
    double largestYawStep = 0;
    for (int i = 0; i < 240; ++i)
    {
        const double before = smoothDrift.VisualHeading();
        smoothDrift.Step(i < 120 ? fullLockDrift : full, 1.0 / 120, .003, 100);
        largestYawStep = std::max(largestYawStep, std::abs(smoothDrift.VisualHeading() - before));
        Check(std::abs(smoothDrift.VisualHeading() - smoothDrift.State.HeadingRad) < 1e-12,
              "rendered chassis yaw follows the simulated yaw throughout a drift");
    }
    Check(largestYawStep < 0.035, "drift entry and exit do not snap the cockpit yaw");
    std::cout << "Drift entry/release maximum yaw step at 120 Hz: " << largestYawStep << " rad\n";
    // The France soak exposed acceleration spikes from brake-triggered drift,
    // despite small per-frame yaw steps and perfect frame cadence. Exercise the
    // actual model, including rapid releases/countersteer, at each owner rate.
    std::array<double, 4> responseAngles{}, settledAngles{};
    std::array<Vroom::DriveState, 4> mixedFinalStates{};
    int cadenceIndex = 0;
    for (int cadence : {30, 60, 120, 240})
    {
        Vroom::DriveModel probe;
        probe.Automatic = false;
        probe.State.Gear = 4;
        probe.State.SpeedMps = 40;
        const double dt = 1.0 / cadence;
        double previousYaw = 0, previousRate = 0, maxAcceleration = 0, maxAngle = 0;
        double maxSettledRebound = 0, settledStart = 0;
        for (int frame = 0; frame < cadence * 10; ++frame)
        {
            // Wide run-off lets repeated full-lock turns stay on the road;
            // speed, tyres and lateral position follow the actual drive model.
            Vroom::DriveInput held{0.6, 0, frame < cadence * 3 / 10 ? 1.0 : -.45, false, false, true};
            if (frame >= cadence * 3 && frame < cadence * 5) held.Steer = -1;
            if (frame >= cadence * 5 && frame < cadence * 7) held = {0.6, 0, 0};
            if (frame >= cadence * 7)
            {
                held.Drift = false;
                held.Steer = ((frame / (cadence / 10)) % 2) ? -0.8 : 0.8;
                held.Brake = ((frame / (cadence / 10)) % 2) ? 0 : 0.6;
            }
            probe.Step(held, dt, .003, 100);
            const double yaw = probe.VisualHeading();
            const double rate = (yaw - previousYaw) / dt;
            maxAcceleration = std::max(maxAcceleration, std::abs(rate - previousRate) / dt);
            maxAngle = std::max(maxAngle, std::abs(yaw));
            if (frame + 1 == cadence * 3 / 10) responseAngles[cadenceIndex] = yaw;
            if (frame + 1 == cadence * 2) settledStart = yaw;
            if (frame >= cadence * 2 && frame < cadence * 3)
                maxSettledRebound = std::max(maxSettledRebound, settledStart - yaw);
            if (frame + 1 == cadence * 3) settledAngles[cadenceIndex] = yaw;
            previousYaw = yaw;
            previousRate = rate;
        }
        Check(maxAcceleration <= Vroom::DriveModel::MaxChassisSlipAcceleration + 1e-7,
              "drift entry/release/countersteer/brake alternation keep bounded angular acceleration at every cadence");
        Check(maxAngle < Vroom::DriveModel::DriftMaxSkidAngle,
              "chassis brakes before its angle limits without requiring a discontinuous clamp");
        Check(responseAngles[cadenceIndex] > 0.04,
              "bounded chassis rotation is visibly established by 0.30 seconds at every cadence");
        Check(maxSettledRebound < .005 && settledAngles[cadenceIndex] > 0.18,
              "sustained drift reaches its full angle without oscillation");
        mixedFinalStates[cadenceIndex] = probe.State;
        std::cout << "Slip at " << cadence << " Hz: acceleration "
                  << maxAcceleration * 180 / 3.14159265358979323846 << " deg/s2; angle at 0.30s "
                  << responseAngles[cadenceIndex] << "; settled " << settledAngles[cadenceIndex] << '\n';
        ++cadenceIndex;
    }
    for (int i : {0, 1})
    {
        Check(std::abs(responseAngles[i] - responseAngles[2]) < 1e-12 &&
              std::abs(settledAngles[i] - settledAngles[2]) < 1e-10,
              "new chassis inertia is identical at 30/60/120 Hz");
        Check(std::abs(mixedFinalStates[i].HeadingRad - mixedFinalStates[2].HeadingRad) < 1e-10 &&
              std::abs(mixedFinalStates[i].LateralM - mixedFinalStates[2].LateralM) < 1e-9 &&
              std::abs(mixedFinalStates[i].SpeedMps - mixedFinalStates[2].SpeedMps) < 1e-9,
              "countersteer and alternating brake inputs preserve cadence-independent yaw and driving");
    }
    Check(std::abs(responseAngles[3] - responseAngles[2]) < .002 &&
          std::abs(settledAngles[3] - settledAngles[2]) < .002,
          "new chassis inertia converges at 240 Hz");
    Vroom::DriveModel cancelledInertia;
    cancelledInertia.State.SpeedMps = 40;
    Run(cancelledInertia, .3, fullLockDrift, .003, 100);
    const double cancelledAngle = cancelledInertia.VisualHeading();
    cancelledInertia.CancelArcadeEffects();
    Check(cancelledInertia.VisualHeading() == cancelledAngle,
          "cancelling arcade effects preserves the current chassis pose");
    cancelledInertia.State.Steering = 0;
    cancelledInertia.Step({}, 1.0 / 120, 0, 100);
    Check(cancelledInertia.VisualHeading() < cancelledAngle,
          "cancelling arcade effects clears inherited angular momentum before resuming");
    Check(std::abs(Vroom::DriveModel::ArcadeWashLatMps(80, 0.0008 + 1e-8) -
                   Vroom::DriveModel::ArcadeWashLatMps(80, 0.0008 - 1e-8)) < 0.001,
          "crossing the small-curvature threshold cannot step the lateral wash");
    smoothDrift.Reset();
    Run(smoothDrift, 0.1, full, 0, 100);
    Check(!smoothDrift.Drifting && smoothDrift.DriftCharge == 0 && smoothDrift.BoostTier == 0 &&
          smoothDrift.HopHeightM == 0 && smoothDrift.State.HeadingRad == 0,
          "reset clears drift, turbo, hop and residual yaw");

    Vroom::DriveModel shove;
    Vroom::DriveModel wreckDrift;
    wreckDrift.State.SpeedMps = 35;
    Run(wreckDrift, .1, fullLockDrift, .003, 100);
    Check(wreckDrift.HopHeightM > 0 && wreckDrift.Drifting, "crash regression starts during an active drift hop");
    const double WreckDistance = wreckDrift.State.DistanceM;
    wreckDrift.CancelArcadeEffects();
    Check(wreckDrift.HopHeightM == 0 && !wreckDrift.Drifting && wreckDrift.DriftCharge == 0 &&
        wreckDrift.BoostRemaining == 0 && wreckDrift.State.DistanceM == WreckDistance,
        "a crash cancels arcade effects without teleporting or resetting the race");
    Run(wreckDrift, .1, fullLockDrift, .003, 100);
    Check(!wreckDrift.Drifting && wreckDrift.HopHeightM == 0, "holding drift across a crash cannot resume its old hop or charge");
    shove.State.SpeedMps = 30;
    shove.State.Gear = 3;
    shove.Shove(12);
    Run(shove, 0.2, {0.4, 0, 0}, 0, 100);
    Check(shove.State.LateralM > 0.8, "a side bump shoves the car laterally");
    Check(std::abs(shove.ExtraLatVel) < 12, "bump impulse decays");

    if (failures)
        return EXIT_FAILURE;
    std::cout << "All drive-model behavioral checks passed.\n";
    return EXIT_SUCCESS;
}
