#include "VroomTrafficModel.h"
#include "VroomDriveModel.h"
#include <cmath>
#include <iostream>

int main()
{
    using namespace Vroom;
    int failures = 0;
    auto check = [&](bool ok, const char* why) { if (!ok) { ++failures; std::cerr << why << '\n'; } };
    constexpr double dt = 1.0 / 120;
    double minimumRatio = 10, maximumRatio = 0;
    // Test each personality in free air: stacking twelve cars at the same
    // location would test overlap avoidance, not comparative engine performance.
    for (int livery = 0; livery < 12; ++livery)
    {
        DriveModel player; player.State.DistanceM = 350;
        std::vector<TrafficCar> rivals(1);
        auto& rival = rivals[0]; rival.Distance = 350;
        rival.Personality = PersonalityByLivery(livery / 2);
        for (int frame = 1; frame <= 2400; ++frame)
        {
            const double previous = rival.Distance;
            const double previousSpeed = rival.Speed;
            player.Step({1,0,0}, dt, 0, 4.75);
            StepTraffic(rivals, dt, 5000, 4.75, RaceDifficulty::Normal, nullptr, [](double) { return 0.; });
            check(std::abs(rival.Distance - previous - rival.Speed * dt) < 1e-10,
                "rival progress follows integrated speed without teleportation");
            check(rival.Speed - previousSpeed <= 28 * dt + 1e-10, "rival acceleration remains bounded");
            if (frame == 120 || frame == 240 || frame == 720 || frame == 2400)
            {
                const double ratio = rival.Speed / player.State.SpeedMps;
                minimumRatio = std::min(minimumRatio, ratio); maximumRatio = std::max(maximumRatio, ratio);
                check(ratio > .90 && ratio < 1.12, "rival engine pace stays close to player at 1, 2, 6 and 20 seconds");
            }
        }
        check(std::abs(rival.Distance - player.State.DistanceM) < 85, "standing start no longer creates a 589 metre runaway lead");
    }
    // The pack help is deliberately absent when the player is returning to
    // the pits, running out of fuel or travelling off the racing surface.
    TrafficCar playerProxy;
    playerProxy.Distance = 450; playerProxy.Speed = 45;
    std::vector<TrafficCar> baseline(1); baseline[0].Distance = 350; baseline[0].Speed = 45;
    auto assisted = baseline;
    StepTraffic(assisted, dt, 5000, 4.75, RaceDifficulty::Normal, &playerProxy, [](double) { return 0.; });
    StepTraffic(baseline, dt, 5000, 4.75, RaceDifficulty::Normal, nullptr, [](double) { return 0.; });
    check(assisted[0].Speed > baseline[0].Speed, "a following rival receives bounded pack assistance");
    for (int state = 0; state < 3; ++state)
    {
        auto ineligible = playerProxy;
        if (state == 0) ineligible.Fuel = .05;
        if (state == 1) ineligible.Pitting = true;
        if (state == 2) ineligible.Lateral = 6;
        std::vector<TrafficCar> rival(1); rival[0].Distance = 350; rival[0].Speed = 45;
        StepTraffic(rival, dt, 5000, 4.75, RaceDifficulty::Normal, &ineligible, [](double) { return 0.; });
        check(rival[0].Speed == baseline[0].Speed, "pit, low fuel and offroad disable pack acceleration");
    }
    std::vector<TrafficCar> bend(1); bend[0].Distance = 350; bend[0].Speed = 80;
    for (int i = 0; i < 360; ++i)
        StepTraffic(bend, dt, 5000, 4.75, RaceDifficulty::Normal, nullptr, [](double) { return .025; });
    check(bend[0].Speed < 38 && bend[0].Speed > 20, "rivals still brake for a tight bend");
    auto damaged = bend;
    for (int hit = 0; hit < 3; ++hit) damaged[0].Damage.ApplyImpact(DamageZone::Front, 40);
    damaged[0].Distance = 5000 - 100;
    StepTraffic(damaged, dt, 5000, 4.75, RaceDifficulty::Normal, nullptr, [](double) { return 0.; });
    check(damaged[0].PitCommitted, "heavy body damage can request repair independently of fresh tyres and fuel");
    std::cout << "Rival pace ratio: " << minimumRatio << ".." << maximumRatio << '\n';
    return failures ? 1 : 0;
}
