#include "VroomWheelVisuals.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
void Near(double A, double B, double E = 1.e-8)
{
    assert(std::abs(std::remainder(A - B, Vroom::WheelTau)) < E);
}
}

int main()
{
    // Interpolated positions yield the same phase at different frame rates,
    // also through acceleration and repeated rendering of the same instant.
    for (int Fps : {15, 30, 60, 120, 240})
    {
        Vroom::WheelVisualState Wheels;
        Wheels.Observe(0, 20, 1.0/Fps, true);
        for (int Frame = 1; Frame <= Fps*5; ++Frame)
        {
            const double T = double(Frame)/Fps;
            Wheels.Observe(20*T+2*T*T, 20+4*T, 1.0/Fps, true);
            Wheels.Observe(20*T+2*T*T, 20+4*T, 1.0/Fps, true);
        }
        Near(Wheels.FrontRollRad, 150/.35);
        Near(Wheels.RearRollRad, 150/.38);
        assert(Wheels.FrontRollRad != Wheels.RearRollRad);
    }
    Vroom::WheelVisualState Wheels;
    Wheels.Observe(1000, 30, 1.0/60, true);
    Near(Wheels.FrontRollRad, 0); // Initial placement is not driven distance.
    Wheels.Observe(1001, 30, 1.0/30, true);
    const double BeforePause = Wheels.FrontRollRad;
    Wheels.Observe(1001, 30, 10, false);
    Wheels.Observe(1002, 30, 0, true);
    Near(Wheels.FrontRollRad, BeforePause);
    Wheels.Observe(1002.5, 30, 1.0/60, true);
    Near(Wheels.FrontRollRad, BeforePause+.5/.35);
    const double BeforeTeleport = Wheels.FrontRollRad;
    Wheels.Observe(9000, 30, 1.0/60, true);
    Near(Wheels.FrontRollRad, BeforeTeleport);
    Wheels.Observe(8999.5, 30, 1.0/60, true);
    Near(Wheels.FrontRollRad, BeforeTeleport-.5/.35); // Signed reverse travel.
    Wheels.Reset();
    assert(!Wheels.Initialized);
    Near(Wheels.FrontRollRad, 0);
    Near(Wheels.RearRollRad, 0);
    Near(Vroom::WheelRollAdvance(0, .35*Vroom::WheelTau, .35), 0);
    assert(std::isfinite(Vroom::WheelRollAdvance(0, 1, 0)));

    const double RaceInner = Vroom::FrontWheelYaw(.62, 60, 1);
    const double RaceOuter = Vroom::FrontWheelYaw(.62, 60, -1);
    assert(RaceInner > RaceOuter && RaceOuter > 8*Vroom::WheelTau/360);
    assert(RaceInner < 13*Vroom::WheelTau/360);
    Near(RaceInner, -Vroom::FrontWheelYaw(-.62, 60, -1));
    Near(RaceOuter, -Vroom::FrontWheelYaw(-.62, 60, 1));
    assert(Vroom::FrontWheelYaw(-.2, 60, 1) < 0); // Countersteer keeps its sign.
    for (double Speed : {-10., 0., 30., 60., 100., 500.})
        for (double Steer : {-2., -.62, -.001, 0., .001, .62, 2.})
            for (double Side : {-1., 1.})
            {
                const double Yaw = Vroom::FrontWheelYaw(Steer, Speed, Side);
                assert(std::isfinite(Yaw) && std::abs(Yaw) <= .50);
                assert(Yaw*Steer >= 0);
            }
    Near(Vroom::FrontWheelYaw(std::numeric_limits<double>::quiet_NaN(), 60, 1), 0);
    std::cout << "Wheel animation: FPS invariance, pause, teleport, reset, roll and steering PASS\n";
}
