#include "VroomOvertake.h"
#include <iostream>

int main()
{
    std::int8_t Side = 0;
    int Passes = 0;
    double Previous = 5;
    for (double Gap = 5; Gap >= -5; Gap -= .1)
    {
        Passes += Vroom::TrackOvertake(Side, Previous, Gap, 5900);
        Previous = Gap;
    }
    if (Passes != 1) return 1;
    for (double Gap : {-.5, .5, -.2, .8, -.1})
    {
        if (Vroom::TrackOvertake(Side, Previous, Gap, 5900) != 0) return 2;
        Previous = Gap;
    }
    if (Vroom::TrackOvertake(Side, Previous, 2, 5900) != -1) return 3;
    if (Vroom::TrackOvertake(Side, 2949, -2949, 5900) != 0) return 4;
    if (Vroom::TrackOvertake(Side, -2949, -2948, 5900) != 0) return 5;
    std::cout << "All overtake count and lap-wrap checks passed.\n";
}
