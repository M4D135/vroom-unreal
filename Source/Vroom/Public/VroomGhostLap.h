#pragma once

#include <algorithm>
#include <cmath>

namespace Vroom::Progress
{
struct LineCrossing
{
    bool Valid = false;
    double Alpha = 0;
};

// A real observed crossing, including departure from the timing line at rest.
// Distances are unwrapped; a lap counter change alone cannot invent an origin.
inline LineCrossing FindLineCrossing(double PreviousDistance, double Distance, double Line)
{
    if (!std::isfinite(PreviousDistance) || !std::isfinite(Distance) || !std::isfinite(Line)
        || PreviousDistance > Line || Distance < Line || Distance < PreviousDistance)
        return {};
    const double Span = Distance - PreviousDistance;
    return {true, Span > 0 ? std::clamp((Line - PreviousDistance) / Span, 0.0, 1.0) : 0.0};
}

inline bool CanSubmitCleanLap(bool RecordingClean, bool CompletedClean, int SampleCount,
                              double FirstTime, double FirstDistance, double LapTime)
{
    return RecordingClean && CompletedClean && SampleCount >= 2
        && FirstTime == 0 && FirstDistance == 0
        && std::isfinite(LapTime) && LapTime >= 5 && LapTime <= 3600;
}
}
