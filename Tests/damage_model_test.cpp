#include "VroomDriveModel.h"
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    int failures = 0;
    auto check = [&](bool ok, const char* why) { if (!ok) { ++failures; std::cerr << why << '\n'; } };
    using namespace Vroom;
    DamageModel damage;
    damage.ApplyImpact(DamageZone::Front, 5);
    check(damage.State.Front > 0 && damage.State.Front < .1 && damage.State.Rear == 0,
        "small frontal impact only dents its own zone");
    const double small = damage.State.Front;
    damage.Reset(); damage.ApplyImpact(DamageZone::Front, 40);
    check(damage.State.Front > small * 3 && damage.State.Front < .5, "damage scales with closing speed");
    DamageModel hop; hop.ApplyImpact(DamageZone::Front, 5, true);
    check(hop.State.Front < small * .3, "controlled wheel rub is a minor scrape");
    const double before = damage.State.Total();
    for (double invalid : {-2., 0., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        damage.ApplyImpact(DamageZone::Front, invalid);
    check(damage.State.Total() == before, "invalid/parked impact adds no damage");
    for (int i = 0; i < 100; ++i)
        for (auto zone : {DamageZone::Front, DamageZone::Rear, DamageZone::Left, DamageZone::Right})
            damage.ApplyImpact(zone, 80);
    check(damage.State.Total() == 1 && damage.GripModifier() >= .81 && damage.DragModifier() <= 1.25,
        "repeated impacts cap at a drivable handling penalty");
    damage.Recover();
    check(damage.State.Total() <= .45 + 1e-12 && damage.State.Total() > 0, "emergency recovery retains bounded damage");
    damage.Repair();
    check(damage.State.Total() == 0 && damage.GripModifier() == 1 && damage.DragModifier() == 1,
        "pit repairs restore intact handling");
    damage.ApplyImpact(DamageZone::Left, 25);
    check(damage.SteeringBias() < 0 && std::abs(damage.SteeringBias()) <= .035, "bounded pull follows damaged side");
    check(DamageModel::Opposite(DamageModel::Zone(ContactFace::Front, 0)) == DamageZone::Rear,
        "frontal player impact reaches rear of the rival");
    check(DamageModel::Opposite(DamageModel::Zone(ContactFace::Side, 1)) == DamageZone::Left,
        "right-side player contact damages rival left side");
    for (double side : {-1., 1.})
    {
        DriveModel car;
        for (int i = 0; i < 10; ++i) car.Damage.ApplyImpact(side < 0 ? DamageZone::Left : DamageZone::Right, 80);
        car.Damage.ApplyImpact(DamageZone::Front, 80);
        car.State.LateralM = side * 5.8;
        for (int i = 0; i < 720; ++i)
        {
            const double steer = std::clamp(-car.State.LateralM * .8, -1.0, 1.0);
            car.Step({1,0,steer}, 1.0/120, 0, 4.75);
        }
        check(car.State.SpeedMps > 5 && std::abs(car.State.LateralM) < 4.5,
            "damaged car can restart on grass and steer back onto road");
        car.Reset();
        check(car.Damage.State.Total() == 0 && car.LateralVelocityMps == 0, "race reset clears damage and tyre momentum");
    }
    damage.State.Left = std::numeric_limits<double>::quiet_NaN();
    check(std::isfinite(damage.State.Total()) && std::isfinite(damage.GripModifier()) && std::isfinite(damage.SteeringBias()),
        "corrupt damage does not leak NaNs into physics or HUD");
    std::cout << "Damage model: " << (failures ? "FAIL" : "PASS") << '\n';
    return failures ? 1 : 0;
}
