#pragma once
#include <cmath>
#include <cstdint>

namespace Vroom
{
// Confirm a pass only after leaving a 3 m overlap zone. Remember which
// side the opponent occupied instead of demanding a 3 m jump in one tick.
inline int TrackOvertake(std::int8_t& Side, double PreviousGap, double Gap, double LapLength)
{
    if (Side == 0 || std::abs(Gap - PreviousGap) > LapLength * .5)
    {
        Side = Gap >= 0 ? 1 : -1;
        return 0;
    }
    if (Side > 0 && Gap < -1.5) { Side = -1; return 1; }
    if (Side < 0 && Gap > 1.5) { Side = 1; return -1; }
    return 0;
}
}
