#pragma once
#include "CoreMinimal.h"

class UWorld;

// Artistic surroundings, not a reconstruction of the six real venues.
namespace Vroom::Environment
{
void ApplyAtmosphere(UWorld* World, int Track);

struct FProfile
{
    double ReliefM;
    double MountainM;
    FLinearColor Ground;
    FLinearColor Leaf;
    double TreeSpacingM;
    int PrimaryTree;
};
inline const FProfile& Profile(int Track)
{
    static const FProfile Profiles[] = {
        {30, 290, FLinearColor(.078,.137,.040), FLinearColor(.030,.103,.040), 25, 1},
        {12,  95, FLinearColor(.145,.154,.059), FLinearColor(.080,.122,.045), 38, 2},
        {25, 155, FLinearColor(.090,.132,.044), FLinearColor(.100,.137,.039), 27, 0},
        { 9,  65, FLinearColor(.088,.132,.067), FLinearColor(.075,.150,.053), 29, 3},
        {26, 340, FLinearColor(.075,.128,.055), FLinearColor(.030,.107,.057), 27, 1},
        { 8,  60, FLinearColor(.087,.147,.049), FLinearColor(.063,.136,.038), 35, 0},
    };
    return Profiles[FMath::Clamp(Track, 0, 5)];
}
inline double HeightM(int Track, const FVector& Point, double DistanceM, double RoadHeightM, double MinimumM)
{
    const auto& P = Profile(Track);
    const double X = Point.X / 100.0, Y = Point.Y / 100.0;
    const double Away = FMath::SmoothStep(12.0, 48.0, DistanceM);
    const double Far = FMath::SmoothStep(80.0, 280.0, DistanceM);
    const double Undulation = .42 + .25 * FMath::Sin(X * .0035 + Track * .7)
        + .18 * FMath::Cos(Y * .0041 - X * .0011) + .12 * FMath::Sin((X + Y) * .009);
    const double Base = FMath::Lerp(RoadHeightM - 0.05, MinimumM - 1.8, Far);
    return Base + P.ReliefM * Away * Undulation;
}
}
