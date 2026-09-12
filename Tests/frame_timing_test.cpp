#include "VroomFrameTiming.h"
#include "VroomDriveModel.h"
#include <iostream>
#include <limits>
#include <vector>

namespace
{
int Failures = 0;
void Check(bool Pass, const char* Message)
{
    if (!Pass) { ++Failures; std::cerr << "FAIL: " << Message << '\n'; }
}

Vroom::DriveState Run(int Fps)
{
    Vroom::FrameTiming Clock;
    Vroom::DriveModel Car;
    Car.State.SpeedMps = 30;
    double Filtered = 0;
    for (int Frame = 0; Frame < 2 * Fps; ++Frame)
    {
        Vroom::DriveInput Input;
        const double Target = Frame < Fps ? .4 : 0;
        for (int Steps = Clock.Advance(1.0 / Fps); Steps > 0; --Steps)
        {
            Filtered += (Target - Filtered) * Vroom::ExponentialAlpha(45, Clock.StepSeconds);
            Input.Steer = Filtered;
            Car.Step(Input, Clock.StepSeconds, .002, 100);
        }
    }
    return Car.State;
}
}

int main()
{
    const auto Reference = Run(120);
    for (int Fps : {30, 60, 90, 120, 144, 240})
    {
        const auto State = Run(Fps);
        Check(std::abs(State.DistanceM - Reference.DistanceM) < 1e-8,
            "identical held controls cover the same distance at every render rate");
        Check(std::abs(State.LateralM - Reference.LateralM) < 1e-8,
            "steer and release follow the same line at 30, 60, 90, 120, 144 and 240 Hz");
    }

    Vroom::FrameTiming Clock;
    double Current = 0, Previous = 0, LastRendered = 0;
    for (double Dt : {0.006, 0.014, 0.004, 0.017, 0.16, 0.002, 0.023, 0.09})
    {
        const int Steps = Clock.Advance(Dt);
        Check(Steps <= Clock.MaxSteps, "long frames bound catch-up work");
        for (int I = 0; I < Steps; ++I)
        {
            Previous = Current;
            Current += 50 * Clock.StepSeconds;
        }
        const double Rendered = Previous + (Current - Previous) * Clock.Alpha();
        Check(Rendered >= LastRendered, "irregular frames and hitches never rewind the rendered car");
        LastRendered = Rendered;
    }
    Clock = {};
    Clock.Advance(.004);
    const double Phase = Clock.Alpha();
    Check(Clock.Advance(.8) == Clock.MaxSteps, "an 800 ms hitch cannot spiral into unbounded physics work");
    Check(std::abs(Clock.Alpha() - Phase) < 1e-10, "capping a hitch preserves the interpolation phase");
    Check(Clock.Advance(std::numeric_limits<double>::quiet_NaN()) == 0, "invalid dt does not poison frame timing");
    const double PausedPhase = Clock.Alpha();
    Check(Clock.Advance(0) == 0 && Clock.Alpha() == PausedPhase, "a paused frame retains its presentation pose");

    Check(Vroom::SteeringDeadzone(.06, .06) == 0, "stick noise remains centred");
    Check(std::abs(Vroom::SteeringDeadzone(.060001, .06)) < .000002,
        "crossing the stick deadzone does not produce a steering step");
    Check(Vroom::SteeringDeadzone(1, .06) == 1 && Vroom::SteeringDeadzone(-1, .06) == -1,
        "continuous deadzone retains both steering locks");
    if (Failures) return 1;
    std::cout << "All frame-timing and render-rate handling checks passed.\n";
}
