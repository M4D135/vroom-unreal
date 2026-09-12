#include "VroomElevationProfile.h"
#include "VroomDriveModel.h"
#include <cstdlib>
#include <iostream>
#include <limits>

int main()
{
    int failures = 0;
    auto check = [&](bool good, const char* message)
    {
        if (!good) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };
    Vroom::ElevationProfile p;
    p.Samples = {{0,0,0}, {100,10,0}, {200,0,0}};
    check(p.Valid(), "closed finite profile accepted");
    check(std::abs(p.At(50).HeightM - 5) < 1e-10, "Hermite midpoint height");
    check(std::abs(p.At(50).Grade - .15) < 1e-10, "Hermite analytic slope");
    check(std::abs(p.At(250).HeightM - p.At(50).HeightM) < 1e-10, "distance wraps over lap");
    check(std::abs(p.At(-50).HeightM - p.At(150).HeightM) < 1e-10, "negative look-behind wraps");
    check(std::abs(p.At(100 - 1e-5).Grade - p.At(100 + 1e-5).Grade) < 1e-6, "crest tangent is continuous");
    check(std::abs(p.At(-1e-5).Grade - p.At(1e-5).Grade) < 1e-6, "start-line tangent is continuous");
    for (double s = 1; s < 200; s += 3.7)
        check(std::abs((p.At(s + .001).HeightM - p.At(s - .001).HeightM) / .002 - p.At(s).Grade) < 1e-8,
            "rendered height derivative matches driving slope");
    p.Samples.back().HeightM = 1;
    check(!p.Valid(), "open altitude seam rejected");
    p.Samples.back().HeightM = 0;
    p.Samples[1].Grade = std::numeric_limits<double>::quiet_NaN();
    check(!p.Valid(), "nonfinite profile rejected");
    Vroom::DriveModel up, flat, down, invalid;
    for (auto* m : {&up, &flat, &down, &invalid}) { m->State.SpeedMps = 45; m->State.Gear = 3; }
    for (int i = 0; i < 120; ++i)
    {
        up.Step({}, 1.0/120, 0, 6, .1);
        flat.Step({}, 1.0/120, 0, 6, 0);
        down.Step({}, 1.0/120, 0, 6, -.1);
        invalid.Step({}, 1.0/120, 0, 6, std::numeric_limits<double>::quiet_NaN());
    }
    check(up.State.SpeedMps < flat.State.SpeedMps && flat.State.SpeedMps < down.State.SpeedMps,
        "uphill resists and downhill assists longitudinal speed");
    check(invalid.State.SpeedMps == flat.State.SpeedMps, "nonfinite grade falls back to flat");
    if (failures) return EXIT_FAILURE;
    std::cout << "All elevation interpolation and grade checks passed.\n";
}
