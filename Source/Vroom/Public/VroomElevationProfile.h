#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace Vroom
{
struct ElevationSample { double DistanceM = 0, HeightM = 0, Grade = 0; };
struct ElevationPose { double HeightM = 0, Grade = 0; };

// A closed distance-parametrized profile. Source projection units are converted
// by the extractor; this class only interpolates the explicitly calibrated data.
class ElevationProfile
{
public:
    std::vector<ElevationSample> Samples;
    bool Valid() const
    {
        if (Samples.size() < 2 || Samples.front().DistanceM != 0) return false;
        for (std::size_t i = 0; i < Samples.size(); ++i)
        {
            const auto& p = Samples[i];
            if (!std::isfinite(p.DistanceM) || !std::isfinite(p.HeightM) || !std::isfinite(p.Grade)
                || (i && p.DistanceM <= Samples[i - 1].DistanceM)) return false;
        }
        return std::abs(Samples.front().HeightM - Samples.back().HeightM) < 1e-5
            && std::abs(Samples.front().Grade - Samples.back().Grade) < 1e-5;
    }
    ElevationPose At(double distance) const
    {
        if (Samples.size() < 2 || !std::isfinite(distance)) return {};
        const double length = Samples.back().DistanceM;
        if (length <= 0) return {};
        distance = std::fmod(distance, length);
        if (distance < 0) distance += length;
        const auto next = std::upper_bound(Samples.begin(), Samples.end(), distance,
            [](double d, const ElevationSample& p) { return d < p.DistanceM; });
        if (next == Samples.begin() || next == Samples.end()) return {};
        const auto& a = *(next - 1);
        const auto& b = *next;
        const double span = b.DistanceM - a.DistanceM;
        const double t = (distance - a.DistanceM) / span;
        const double t2 = t * t, t3 = t2 * t;
        const double height = (2*t3-3*t2+1)*a.HeightM + (t3-2*t2+t)*span*a.Grade
            + (-2*t3+3*t2)*b.HeightM + (t3-t2)*span*b.Grade;
        const double grade = ((6*t2-6*t)*a.HeightM + (3*t2-4*t+1)*span*a.Grade
            + (-6*t2+6*t)*b.HeightM + (3*t2-2*t)*span*b.Grade) / span;
        return {height, grade};
    }
};
}
