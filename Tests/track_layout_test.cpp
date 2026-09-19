#include "VroomTrackLayout.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

static void Check(bool Pass, const char* Message)
{
    if (!Pass) { std::cerr << "FAIL: " << Message << '\n'; std::exit(1); }
}
int main()
{
    using namespace Vroom::TrackLayout;
    constexpr double Half = 4.75;
    Check(PitCenterAt(PitEntry, Half) == Half && PitCenterAt(PitExit, Half) == Half, "pit joins road at both ends");
    Check(PitCenterAt(55, Half) == 12.5 && PitCenterAt(190, Half) == 12.5, "pit matches service lane");
    for (double S = PitEntry; S <= PitExit; S += .1)
        Check(std::abs(PitCenterAt(S + .01, Half) - PitCenterAt(S, Half)) < .006, "pit transition stays continuous");
    Check(PitOpening(31) && PitOpening(55) && PitOpening(190) && PitOpening(214) && !PitOpening(102), "kerbs open only at access ramps");
    Check(PitKerbOpening(26.1,28.1) && PitKerbOpening(57.9,59.9)
        && PitKerbOpening(185.1,187.1) && PitKerbOpening(216.9,218.9),
        "finite kerb end triangles stay beyond both access joins");
    Check(!PitKerbOpening(25.9,27.9) && !PitKerbOpening(58.1,60.1)
        && !PitKerbOpening(184.9,186.9) && !PitKerbOpening(217.1,219.1),
        "one-metre join margin keeps the rest of the kerb intact");
    Check(!PitOpening(58) && !PitOpening(187), "visual clearance does not enlarge pit validity bounds");
    Check(VergeBlend(20) == 0 && VergeBlend(36) == 1, "verge joins road and terrain without a height step");
    Check(std::abs(GridHeight(0, 1, 2, 8, .8, .2) - 2.2) < 1e-10, "terrain height uses lower triangle");
    Check(std::abs(GridHeight(0, 1, 2, 8, .2, .8) - 2.8) < 1e-10, "terrain height uses upper triangle");
    auto Curve = [](double S) {
        double K = 0;
        for (double Apex : {600., 1100., 1650., 2200., 2800.}) K += .008 * std::exp(-std::pow((S - Apex) / 45., 2));
        return K;
    };
    const auto Markers = BrakingLandmarks(3200, Curve);
    Check(Markers.size() == 4, "four distinct bend approaches selected");
    for (std::size_t I = 0; I < Markers.size(); ++I)
    {
        Check(Markers[I].CornerDistance > Markers[I].Distance + 50, "marker is before braking bend");
        Check(Markers[I].Severity > .007, "marker follows actual curvature peak");
        if (I) Check(Markers[I].Distance - Markers[I-1].Distance >= 260, "landmarks remain separated");
    }
    Check(BrakingLandmarks(3200, [](double) { return 0.; }).empty(), "no invented corners on straight circuit");
    std::cout << "PASS: continuous pit access, terrain triangles, curvature-based landmarks\n";
}
