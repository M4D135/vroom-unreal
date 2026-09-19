#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// Metres along the driving spline. Shared by the visible surface and its audit.
namespace Vroom::TrackLayout
{
constexpr double PitEntry = 31.0, PitLaneStart = 55.0;
constexpr double PitLaneEnd = 190.0, PitExit = 214.0;
constexpr double PitCenter = 12.5, PitHalfWidth = 2.35;
constexpr double FlatVerge = 20.0, VergeEnd = 36.0;

inline double PitCenterAt(double S, double HalfWidth)
{
    if (S < PitEntry || S > PitExit) return HalfWidth;
    const double T = S < PitLaneStart ? (S - PitEntry) / (PitLaneStart - PitEntry)
        : S > PitLaneEnd ? (PitExit - S) / (PitExit - PitLaneEnd) : 1.0;
    const double Blend = T * T * (3.0 - 2.0 * T);
    return HalfWidth + (PitCenter - HalfWidth) * Blend;
}

inline bool PitOpening(double S)
{
    return (S >= PitEntry - 2 && S <= PitLaneStart + 2)
        || (S >= PitLaneEnd - 2 && S <= PitExit + 2);
}

inline bool PitKerbOpening(double SegmentStart, double SegmentEnd)
{
    // The rendered kerb is a finite-width chord, not a point on the spline.
    // Keep one metre beyond each access end clear so its end triangles cannot
    // intrude into the join on a bend. Driving/pit-validity bounds stay intact.
    constexpr double EndClearance = 1.0;
    return (SegmentEnd >= PitEntry - 2 - EndClearance && SegmentStart <= PitLaneStart + 2 + EndClearance)
        || (SegmentEnd >= PitLaneEnd - 2 - EndClearance && SegmentStart <= PitExit + 2 + EndClearance);
}

inline double VergeBlend(double Distance)
{
    return std::clamp((Distance - FlatVerge) / (VergeEnd - FlatVerge), 0.0, 1.0);
}

inline double GridHeight(double A, double B, double C, double D, double U, double V)
{
    // Same A--D diagonal as the terrain mesh, not bilinear interpolation.
    return U >= V ? A + U * (B - A) + V * (D - B)
                  : A + U * (D - C) + V * (C - A);
}

struct Landmark { double Distance, CornerDistance, Severity; };

template<class Curvature>
std::vector<Landmark> BrakingLandmarks(double Length, Curvature Curve)
{
    std::vector<Landmark> Candidates, Result;
    // Select separated bend approaches. No arbitrary fractions of the circuit.
    for (double S = 350; S < Length - 140; S += 10)
    {
        const double K = std::abs(Curve(S));
        if (K < .0015 || K < std::abs(Curve(S - 20)) || K < std::abs(Curve(S + 20))) continue;
        const double Approach = std::clamp(45.0 + K * 9000.0, 55.0, 115.0);
        Candidates.push_back({S - Approach, S, K});
    }
    std::sort(Candidates.begin(), Candidates.end(), [](const auto& A, const auto& B) {
        return A.Severity != B.Severity ? A.Severity > B.Severity : A.Distance < B.Distance;
    });
    for (const auto& Candidate : Candidates)
    {
        bool Clear = true;
        for (const auto& Existing : Result)
            if (std::abs(Existing.Distance - Candidate.Distance) < 260) Clear = false;
        if (Clear) Result.push_back(Candidate);
        if (Result.size() == 4) break;
    }
    std::sort(Result.begin(), Result.end(), [](const auto& A, const auto& B) { return A.Distance < B.Distance; });
    return Result;
}
}
