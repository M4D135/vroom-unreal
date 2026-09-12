#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Closed 2D preview built from the HUD minimap polyline. The source vertices
// are ~20 m pixel stairs (90° jogs). Driving on that polyline, even after the
// old two-pass 7-tap, left a chain of short straights with heading snaps at
// each vertex — the "reposition on a straight" feel in corners.
namespace Vroom
{
struct PreviewPoint
{
    double X = 0, Y = 0, Z = 0;
};

inline double ClosedLength(const std::vector<PreviewPoint>& Points)
{
    double Length = 0;
    const int Count = static_cast<int>(Points.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        const PreviewPoint& A = Points[Index];
        const PreviewPoint& B = Points[(Index + 1) % Count];
        Length += std::hypot(B.X - A.X, B.Y - A.Y);
    }
    return Length;
}

inline void ScaleXY(std::vector<PreviewPoint>& Points, double Scale)
{
    for (PreviewPoint& Point : Points)
    {
        Point.X *= Scale;
        Point.Y *= Scale;
    }
}

inline std::vector<PreviewPoint> SmoothClosed(const std::vector<PreviewPoint>& Input, int Passes)
{
    std::vector<PreviewPoint> Points = Input;
    const int Count = static_cast<int>(Points.size());
    if (Count < 4 || Passes <= 0)
        return Points;
    for (int Pass = 0; Pass < Passes; ++Pass)
    {
        std::vector<PreviewPoint> Next(Count);
        for (int Index = 0; Index < Count; ++Index)
        {
            double SumX = 0, SumY = 0;
            for (int Offset = -3; Offset <= 3; ++Offset)
            {
                const PreviewPoint& Sample = Points[(Index + Offset + Count * 4) % Count];
                const double Weight = 4.0 - std::abs(Offset);
                SumX += Sample.X * Weight;
                SumY += Sample.Y * Weight;
            }
            Next[Index] = {SumX / 16.0, SumY / 16.0, Points[Index].Z};
        }
        Points.swap(Next);
    }
    return Points;
}

inline std::vector<PreviewPoint> ChaikinClosed(const std::vector<PreviewPoint>& Input, int Passes)
{
    std::vector<PreviewPoint> Points = Input;
    for (int Pass = 0; Pass < Passes; ++Pass)
    {
        const int Count = static_cast<int>(Points.size());
        if (Count < 3)
            break;
        std::vector<PreviewPoint> Next;
        Next.reserve(static_cast<size_t>(Count) * 2);
        for (int Index = 0; Index < Count; ++Index)
        {
            const PreviewPoint& A = Points[Index];
            const PreviewPoint& B = Points[(Index + 1) % Count];
            Next.push_back({0.75 * A.X + 0.25 * B.X, 0.75 * A.Y + 0.25 * B.Y, A.Z});
            Next.push_back({0.25 * A.X + 0.75 * B.X, 0.25 * A.Y + 0.75 * B.Y, B.Z});
        }
        Points.swap(Next);
    }
    return Points;
}

inline std::vector<PreviewPoint> ResampleClosed(const std::vector<PreviewPoint>& Input, double Spacing)
{
    const int Count = static_cast<int>(Input.size());
    if (Count < 3 || !(Spacing > 0))
        return Input;
    struct Segment
    {
        double Length, X0, Y0, X1, Y1, Z0, Z1;
    };
    std::vector<Segment> Segments;
    Segments.reserve(Count);
    double Total = 0;
    for (int Index = 0; Index < Count; ++Index)
    {
        const PreviewPoint& A = Input[Index];
        const PreviewPoint& B = Input[(Index + 1) % Count];
        const double Length = std::hypot(B.X - A.X, B.Y - A.Y);
        Segments.push_back({Length, A.X, A.Y, B.X, B.Y, A.Z, B.Z});
        Total += Length;
    }
    if (!(Total > Spacing))
        return Input;
    const int OutCount = std::max(8, static_cast<int>(std::round(Total / Spacing)));
    std::vector<PreviewPoint> Output;
    Output.reserve(OutCount);
    int SegmentIndex = 0;
    double Accumulated = 0;
    for (int Index = 0; Index < OutCount; ++Index)
    {
        const double Target = (Index * Total) / OutCount;
        while (SegmentIndex + 1 < static_cast<int>(Segments.size())
            && Accumulated + Segments[SegmentIndex].Length < Target)
        {
            Accumulated += Segments[SegmentIndex].Length;
            ++SegmentIndex;
        }
        const Segment& Current = Segments[SegmentIndex];
        const double T = Current.Length <= 1e-12
            ? 0.0
            : std::clamp((Target - Accumulated) / Current.Length, 0.0, 1.0);
        Output.push_back({Current.X0 + T * (Current.X1 - Current.X0),
            Current.Y0 + T * (Current.Y1 - Current.Y0),
            Current.Z0 + T * (Current.Z1 - Current.Z0)});
    }
    return Output;
}

inline double MaxHeadingStepDegrees(const std::vector<PreviewPoint>& Points)
{
    const int Count = static_cast<int>(Points.size());
    if (Count < 3)
        return 0;
    double Maximum = 0;
    std::vector<double> Heading(Count);
    for (int Index = 0; Index < Count; ++Index)
    {
        const PreviewPoint& A = Points[Index];
        const PreviewPoint& B = Points[(Index + 1) % Count];
        Heading[Index] = std::atan2(B.Y - A.Y, B.X - A.X);
    }
    for (int Index = 0; Index < Count; ++Index)
    {
        const double Previous = Heading[(Index - 1 + Count) % Count];
        const double Delta = std::atan2(std::sin(Heading[Index] - Previous),
            std::cos(Heading[Index] - Previous));
        Maximum = std::max(Maximum, std::abs(Delta));
    }
    return Maximum * 180.0 / std::acos(-1.0);
}

inline double MaxAbsCurvature(const std::vector<PreviewPoint>& Points)
{
    const int Count = static_cast<int>(Points.size());
    if (Count < 3)
        return 0;
    double Maximum = 0;
    for (int Index = 0; Index < Count; ++Index)
    {
        const PreviewPoint& A = Points[(Index - 1 + Count) % Count];
        const PreviewPoint& B = Points[Index];
        const PreviewPoint& C = Points[(Index + 1) % Count];
        const double Heading0 = std::atan2(B.Y - A.Y, B.X - A.X);
        const double Heading1 = std::atan2(C.Y - B.Y, C.X - B.X);
        const double Delta = std::atan2(std::sin(Heading1 - Heading0), std::cos(Heading1 - Heading0));
        const double Step = std::hypot(C.X - B.X, C.Y - B.Y);
        if (Step > 1e-6)
            Maximum = std::max(Maximum, std::abs(Delta / Step));
    }
    return Maximum;
}

// Four 7-tap passes kill the HUD 90° stairs without flattening real bends
// (eight passes + two Chaikin left Austria at R≈70 m and a kilometre of
// fake straight — no screech, "virages guidés"). Then one Chaikin and a
// 5 m resample. The resampler follows linear edges: smoothing again at that
// final spacing removes their curvature ripple before Unreal fits a cubic.
// These two local passes cover metres, not another broad pass over HUD pixels.
inline std::vector<PreviewPoint> BuildDriveablePreview(
    const std::vector<PreviewPoint>& Input, double TargetLengthM)
{
    if (Input.size() < 4 || !(TargetLengthM > 1))
        return Input;
    std::vector<PreviewPoint> Points = SmoothClosed(Input, 4);
    Points = ChaikinClosed(Points, 1);
    const double AfterSmooth = ClosedLength(Points);
    if (AfterSmooth > 1)
        ScaleXY(Points, TargetLengthM / AfterSmooth);
    Points = ResampleClosed(Points, 5.0);
    Points = SmoothClosed(Points, 2);
    const double AfterResample = ClosedLength(Points);
    if (AfterResample > 1)
        ScaleXY(Points, TargetLengthM / AfterResample);
    return Points;
}
} // namespace Vroom
