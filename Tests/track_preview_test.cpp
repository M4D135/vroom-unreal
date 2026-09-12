#include "VroomTrackPreview.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace
{
int Failures = 0;
void Check(bool Result, const char* Explanation)
{
    if (!Result)
    {
        std::cerr << "FAIL: " << Explanation << '\n';
        ++Failures;
    }
}

std::vector<Vroom::PreviewPoint> PixelatedSquare(int PerSide, double Step)
{
    std::vector<Vroom::PreviewPoint> Points;
    const double Side = PerSide * Step;
    for (int Index = 0; Index < PerSide; ++Index)
        Points.push_back({Index * Step, 0, 0});
    for (int Index = 0; Index < PerSide; ++Index)
        Points.push_back({Side, Index * Step, 0});
    for (int Index = 0; Index < PerSide; ++Index)
        Points.push_back({Side - Index * Step, Side, 0});
    for (int Index = 0; Index < PerSide; ++Index)
        Points.push_back({0, Side - Index * Step, 0});
    return Points;
}

// UE's closed Curve spline uses centred automatic tangents. A polyline can
// pass the vertex-angle check yet its cubic interpolation has a large jump
// in curvature at every knot, felt as an abrupt change of camera yaw rate.
double MaxSplineCurvatureJump(const std::vector<Vroom::PreviewPoint>& Points)
{
    const int Count = static_cast<int>(Points.size());
    auto Tangent = [&](int Index)
    {
        const auto& Previous = Points[(Index - 1 + Count) % Count];
        const auto& Next = Points[(Index + 1) % Count];
        return Vroom::PreviewPoint{(Next.X - Previous.X) * 0.5,
            (Next.Y - Previous.Y) * 0.5, 0};
    };
    auto Curvature = [&](int Index, double T)
    {
        const auto& A = Points[Index];
        const auto& B = Points[(Index + 1) % Count];
        const auto TA = Tangent(Index);
        const auto TB = Tangent((Index + 1) % Count);
        auto First = [T](double A0, double B0, double AT, double BT)
        {
            return (6*T*T - 6*T)*A0 + (3*T*T - 4*T + 1)*AT
                + (-6*T*T + 6*T)*B0 + (3*T*T - 2*T)*BT;
        };
        auto Second = [T](double A0, double B0, double AT, double BT)
        {
            return (12*T - 6)*A0 + (6*T - 4)*AT
                + (-12*T + 6)*B0 + (6*T - 2)*BT;
        };
        const double DX = First(A.X, B.X, TA.X, TB.X);
        const double DY = First(A.Y, B.Y, TA.Y, TB.Y);
        const double DDX = Second(A.X, B.X, TA.X, TB.X);
        const double DDY = Second(A.Y, B.Y, TA.Y, TB.Y);
        return (DX * DDY - DY * DDX) / std::pow(std::hypot(DX, DY), 3);
    };
    double Maximum = 0;
    for (int Index = 0; Index < Count; ++Index)
        Maximum = std::max(Maximum, std::abs(Curvature(Index, 0)
            - Curvature((Index - 1 + Count) % Count, 1)));
    return Maximum;
}
}

int main()
{
    const auto Raw = PixelatedSquare(20, 19.25);
    Check(Vroom::MaxHeadingStepDegrees(Raw) > 80,
          "unsmoothed HUD-style square still has ~90 degree pixel corners");
    const double Target = Vroom::ClosedLength(Raw);
    const auto Driveable = Vroom::BuildDriveablePreview(Raw, Target);
    Check(Driveable.size() >= 40, "driveable preview keeps a dense vertex set");
    Check(std::abs(Vroom::ClosedLength(Driveable) - Target) < 0.02,
          "driveable preview is rescaled to the source lap length");
    Check(Vroom::MaxHeadingStepDegrees(Driveable) < 8.0,
          "resampled preview never yaws more than 8 degrees at a vertex");
    Check(Vroom::MaxAbsCurvature(Driveable) < 0.06,
          "tightest remaining corner stays inside a 17 m radius kink");
    Check(Vroom::MaxAbsCurvature(Driveable) > 0.01,
          "local smoothing preserves distinct corners instead of rounding the whole lap");
    Check(MaxSplineCurvatureJump(Driveable) < 0.004,
          "final spline avoids abrupt yaw-rate changes at 5 m resampling knots");
    // Keep the test sensitive to the bug: broad smoothing alone had low
    // vertex angles, but its subsequent linear resampling restored the ripple.
    const auto BeforeFinalFilter = Vroom::ResampleClosed(
        Vroom::ChaikinClosed(Vroom::SmoothClosed(Raw, 4), 1), 5.0);
    Check(MaxSplineCurvatureJump(BeforeFinalFilter) > 0.008,
          "pixelated-square fixture exposes cubic curvature ripple before the final filter");

    const auto TooSmall = Vroom::BuildDriveablePreview({{0, 0, 0}, {1, 0, 0}}, 100);
    Check(TooSmall.size() == 2, "fewer than four points are left unchanged");

    const auto Circleish = Vroom::BuildDriveablePreview(Raw, 4000);
    Check(std::abs(Vroom::ClosedLength(Circleish) - 4000) < 0.05,
          "an explicit target length is honoured after smoothing");

    if (Failures)
        return 1;
    std::cout << "All track-preview checks passed. heading-step "
              << Vroom::MaxHeadingStepDegrees(Driveable) << " deg, max |K| "
              << Vroom::MaxAbsCurvature(Driveable) << " /m, vertices "
              << Driveable.size() << ", spline curvature jump "
              << MaxSplineCurvatureJump(Driveable) << " /m.\n";
    return 0;
}
