#include "VroomDriveModel.h"
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    int failures = 0;
    auto check = [&](bool ok, const char* why) { if (!ok) { ++failures; std::cerr << why << '\n'; } };
    auto step = [](Vroom::DriveModel& car, double steer, int frames)
    {
        for (int i = 0; i < frames; ++i) car.Step({.6,0,steer}, 1.0/120, 0, 4.75);
    };
    for (double sign : {-1., 1.})
    {
        Vroom::DriveModel car;
        car.State.SpeedMps = 30; car.State.Gear = 3;
        step(car, sign, 36);
        check(car.LateralVelocityMps * sign > 3, "front responds promptly to a deliberate turn");
        const double before = car.LateralVelocityMps;
        step(car, 0, 1);
        check(car.LateralVelocityMps * sign > 0 && std::abs(car.LateralVelocityMps - before) < 1,
            "rear retains short momentum through release rather than snapping sideways");
        const double position = car.State.LateralM;
        step(car, 0, 36);
        check(std::abs(car.LateralVelocityMps) < .01 && std::abs(car.State.LateralM - position) < .25,
            "tyres regain grip promptly without a long uncontrollable coast");
        step(car, -sign, 18);
        check(car.LateralVelocityMps * sign < -3, "countersteer reverses the lateral trajectory quickly");
        car.LateralVelocityMps = std::numeric_limits<double>::quiet_NaN();
        step(car, 0, 1);
        check(std::isfinite(car.State.LateralM) && std::isfinite(car.LateralVelocityMps), "invalid momentum is contained");
        car.Reset(); step(car, sign, 60);
        check(car.State.SpeedMps > 0, "normal throttle remains usable after reset");
    }
    Vroom::DriveModel parked;
    parked.LateralVelocityMps = 10;
    parked.Step({}, 1.0/120, 0, 4.75);
    check(parked.State.LateralM == 0 && parked.LateralVelocityMps == 0, "a stopped car cannot drift from stale tyre momentum");
    for (double curve : {-.008, 0., .0005, .008})
    {
        Vroom::DriveModel car;
        car.State.SpeedMps = 40; car.State.Gear = 4;
        for (int i = 0; i < 18; ++i) car.Step({.6,0,.8,false,false,true}, 1.0/120, curve, 4.75);
        check(car.Drifting == (curve == .008), "entry requires an actual bend and steering into it");
        if (car.Drifting)
        {
            for (int i = 0; i < 180; ++i) car.Step({.6,0,-.45,false,false,true}, 1.0/120, curve, 4.75);
            check(car.DriftCharge > 1, "corner control earns its real charge");
            const double charge = car.DriftCharge;
            car.Step({.6,0,-.45,false,false,true}, 1.0/120, 0, 4.75);
            check(car.DriftCharge <= charge, "straight exit cannot add drift charge");
            auto release = car;
            release.Step({.6,0,0}, 1.0/120, 0, 4.75);
            check(release.BoostTier >= 2, "timed release at corner exit keeps earned boost");
            for (int i = 0; i < 36; ++i) car.Step({.6,0,-.45,false,false,true}, 1.0/120, 0, 4.75);
            check(!car.Drifting && car.BoostRemaining == 0, "holding drift onto a straight settles without free turbo");
        }
    }
    std::cout << "Kart handling: " << (failures ? "FAIL" : "PASS") << '\n';
    return failures ? 1 : 0;
}
