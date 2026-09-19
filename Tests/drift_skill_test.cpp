#include "VroomDriveModel.h"
#include "VroomFrameTiming.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
int Failures = 0;
void Check(bool ok, const char* text) { if (!ok) { ++Failures; std::cerr << "FAIL: " << text << '\n'; } }
struct Driver {
    Vroom::DriveModel Model;
    double Filtered = 0, MaxLateral = 0, MaxYawAcceleration = 0, PreviousYaw = 0, PreviousYawRate = 0;
    explicit Driver(double speed = 40) { Model.State.SpeedMps = speed; Model.State.Gear = 4; }
    void Run(double seconds, double steer, bool drift, double curvature = 0, double filterRate = 0,
             double cadence = 120, double halfWidth = 4.75, double throttle = .6, double brake = 0) {
        const double dt = 1.0 / cadence;
        for (int frame = 0; frame < std::lround(seconds * cadence); ++frame) {
            if (filterRate > 0) Filtered += (steer - Filtered) * Vroom::ExponentialAlpha(filterRate, dt);
            else Filtered = steer;
            Model.Step({throttle, brake, Filtered, false, false, drift}, dt, curvature, halfWidth);
            const double yaw = Model.VisualHeading(), rate = (yaw - PreviousYaw) / dt;
            MaxYawAcceleration = std::max(MaxYawAcceleration, std::abs(rate - PreviousYawRate) / dt);
            PreviousYaw = yaw; PreviousYawRate = rate;
            MaxLateral = std::max(MaxLateral, std::abs(Model.State.LateralM));
        }
    }
};
}

int main() {
    // Actual physical road width plus both conservative and current Car filters.
    for (double filter : {20.0, 45.0}) for (double sign : {-1.0, 1.0}) {
        Driver d;
        d.Run(.3, .8 * sign, true, 0, filter);
        Check(d.Model.Drifting && d.Model.HopHeightM >= 0 && d.Model.DriftDirection == sign,
              "button and filtered steering arm the chosen slide on the physical road");
        d.Run(1.3, -.45 * sign, true, 0, filter);
        Check(d.Model.DriftCharge >= 1 && d.Model.DriftControl > .9 && d.Model.DriftDirection == sign,
              "filtered countersteering charges tier two while preserving entry direction");
        Check(d.Model.VisualHeading() * sign > .18, "countersteering preserves the original visible slide angle");
        d.Run(.10, 0, false, 0, filter);
        Check(d.Model.BoostTier == 2 && d.Model.BoostRemaining > .9 && d.Model.DriftDirection == 0,
              "clean release grants super boost and clears public drift direction");
        Check(d.MaxLateral < 4.5, "runtime drift sequence stays on the 4.75m half-width road");
        Check(d.MaxYawAcceleration <= Vroom::DriveModel::MaxChassisSlipAcceleration + 1e-8,
              "countersteer and release preserve bounded cockpit angular acceleration");
        std::cout << "Filtered drift " << filter << " Hz sign=" << sign << " max lateral=" << d.MaxLateral
                  << " charge/release tier=" << d.Model.BoostTier << '\n';
    }
    for (double cadence : {30.0, 60.0, 120.0}) {
        Driver basic;
        basic.Run(.3, 1, true, 0, 0, cadence);
        basic.Run(.7, -1, true, 0, 0, cadence);
        basic.Run(1.0 / cadence, 0, false, 0, 0, cadence);
        Check(basic.Model.BoostTier == 1, "timed full digital countersteer earns the basic tier without an analog stick");

        Driver keyboard;
        keyboard.Run(.3, 1, true, 0, 20, cadence);
        for (int pulse = 0; pulse < 8; ++pulse) {
            keyboard.Run(.20, -1, true, 0, 20, cadence);
            keyboard.Run(.10, 0, true, 0, 20, cadence);
        }
        keyboard.Run(1.0 / cadence, 0, false, 0, 20, cadence);
        Check(keyboard.Model.BoostTier == 2, "modulated keyboard countersteer can reach the second tier at every cadence");
        Check(keyboard.MaxLateral < 4.5, "modulated keyboard drift stays on the physical road");

        Driver uncontrolled;
        uncontrolled.Run(1.5, .8, true, 0, 0, cadence);
        uncontrolled.Run(.1, 0, false, 0, 0, cadence);
        Check(!uncontrolled.Model.Drifting && uncontrolled.Model.DriftCharge == 0 && uncontrolled.Model.BoostTier == 0,
              "holding the entry direction cannot farm a release boost");
        Driver excess;
        excess.Run(.3, 1, true, 0, 0, cadence);
        excess.Run(1.5, -1, true, 0, 0, cadence);
        excess.Run(.1, 0, false, 0, 0, cadence);
        Check(excess.Model.BoostTier == 0, "unmodulated opposite full lock eventually loses the slide and its charge");
        Driver early;
        early.Run(.2, .8, true, 0, 0, cadence);
        early.Run(.2, -.45, true, 0, 0, cadence);
        early.Run(.1, 0, false, 0, 0, cadence);
        Check(early.Model.BoostTier == 0, "an early release never produces an unearned boost");
    }
    Driver grace;
    grace.Run(.3, .8, true);
    grace.Run(.5, -.45, true);
    const double earned = grace.Model.DriftCharge;
    grace.Run(.15, 0, true);
    Check(grace.Model.Drifting && grace.Model.DriftCharge > earned - .05,
          "a brief neutral crossing tolerates a correction instead of cancelling instantly");
    grace.Run(.8, -.45, true);
    grace.Run(.05, 0, false);
    Check(grace.Model.BoostTier == 2, "control can recover after a short neutral correction");

    for (int cause = 0; cause < 5; ++cause) {
        Driver d;
        d.Run(.3, .8, true); d.Run(1.2, -.45, true);
        if (cause == 0) d.Model.State.LateralM = 4.8;
        if (cause == 1) d.Model.Fuel = 0;
        if (cause == 2) d.Model.CancelArcadeEffects();
        d.Model.Step({.6, cause == 3 ? .3 : 0, -.45, false, false, true}, 1.0/120, 0, 4.75, 0, cause == 4);
        Check(!d.Model.Drifting && d.Model.DriftCharge == 0 && d.Model.BoostRemaining == 0,
              "grass, empty fuel, contact cancellation, brake or external jump destroys the earned charge");
        d.Model.State.LateralM = 0; d.Model.Fuel = 1;
        d.Run(.3, .8, true);
        Check(!d.Model.Drifting, "a cancelled slide does not restart while its button remains held");
        d.Run(.3, 0, false); d.Run(.1, -.8, true);
        Check(d.Model.Drifting && d.Model.DriftDirection == -1 && d.Model.DriftCharge == 0,
              "releasing and rearming starts a fresh opposite slide without old charge");
    }
    Driver spam;
    int starts = 0;
    for (int pulse = 0; pulse < 30; ++pulse) {
        spam.Run(.10, .8, true); starts += spam.Model.Drifting;
        spam.Run(.10, 0, false);
        Check(spam.Model.BoostTier == 0, "rapid button spam never earns a boost");
    }
    Check(starts == 1, "rapid represses cannot repeatedly hop before the rearm delay");
    Driver braking;
    braking.Run(.5, .8, false, 0, 0, 120, 4.75, .6, .7);
    Check(!braking.Model.Drifting && braking.Model.HopHeightM == 0,
          "ordinary braking and cornering never engage the drift feature");
    for (bool boost : {false, true}) {
        Driver contact;
        contact.Run(.3, .8, true); contact.Run(1.3, -.45, true);
        if (boost) contact.Run(.025, 0, false);
        contact.Model.StartHop(.42, .26);
        contact.Run(.1, boost ? 0 : -.45, !boost);
        Check(boost ? contact.Model.BoostRemaining > 0 : contact.Model.DriftCharge > 1,
              "soft contact regression begins with real earned charge or an active boost");
        const double height = contact.Model.HopHeightM;
        auto expectedArc = contact.Model;
        auto defaultCancel = contact.Model;
        contact.Model.CancelArcadeEffects(false);
        Check(!contact.Model.Drifting && contact.Model.DriftCharge == 0 && contact.Model.BoostRemaining == 0 &&
              contact.Model.BoostTier == 0 && contact.Model.HopHeightM == height && height > .3,
              "soft contact cancels charge and boost without discontinuity in hop height");
        defaultCancel.CancelArcadeEffects();
        Check(defaultCancel.HopHeightM == 0 && !defaultCancel.Drifting && defaultCancel.BoostRemaining == 0,
              "default cancellation still removes the physical hop immediately");
        for (int step = 0; step < 24; ++step) {
            contact.Model.Step({}, 1.0 / 120, 0, 4.75);
            expectedArc.Step({}, 1.0 / 120, 0, 4.75);
            Check(std::abs(contact.Model.HopHeightM - expectedArc.HopHeightM) < 1e-12,
                  "preserved hop follows its original arc and elapsed time after soft contact");
        }
        Check(contact.Model.HopHeightM == 0 && contact.Model.BoostRemaining == 0,
              "preserved hop ends naturally without restoring a cancelled boost");
    }
    for (double speed : {30.0, 40.0, 50.0}) for (double curvature : {-.018, -.010, -.003, .003, .010, .018}) {
        const double sign = curvature > 0 ? 1 : -1;
        Driver d(speed);
        d.Model.TyreWearPerMetre = .08;
        d.Run(.3, .8 * sign, true, curvature, 20);
        d.Run(1.2, -.45 * sign, true, curvature, 20);
        Check(d.Model.Drifting && d.Model.DriftCharge >= 1,
              "a controlled curved-road slide charges across normal arcade speeds");
        Check(d.MaxLateral < 4.5 && std::isfinite(d.Model.State.HeadingRad),
              "curved-road slides stay finite and retain the playable road corridor");
        Check(d.Model.TyreHealth < 2048 - .08 * d.Model.State.DistanceM,
              "cornering and drift consume extra tyre wear beyond straight-line distance");
    }
    if (Failures) return EXIT_FAILURE;
    std::cout << "Skill drift inputs, keyboard modulation, failures, curves and wear PASS.\n";
    return EXIT_SUCCESS;
}
