#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "VroomSession.h"

namespace Vroom
{
struct TrafficCar
{
    double Distance = 0, Lateral = 0, Speed = 0;
    double TargetLane = 0, LaneVelocity = 0, DecisionDelay = 0, DamageDelay = 0;
};
inline double TrafficGap(double Ahead, double Behind, double Length)
{
    double Gap = std::fmod(Ahead - Behind, Length);
    if (Gap > Length * .5) Gap -= Length;
    if (Gap < -Length * .5) Gap += Length;
    return Gap;
}
inline double TrafficPace(RaceDifficulty Difficulty)
{
    return Difficulty == RaceDifficulty::Easy ? .83 : Difficulty == RaceDifficulty::Hard ? 1.07 : .96;
}

// A bounded twelve-car planner. Decisions use an immutable frame snapshot;
// previously selected target lanes reserve room for later decisions, avoiding
// two neighbours simultaneously claiming the same overtaking gap.
template<class CurvatureAt>
void StepTraffic(std::vector<TrafficCar>& Cars, double Dt, double Length, double HalfWidth,
    RaceDifficulty Difficulty, const TrafficCar* Player, CurvatureAt Curvature)
{
    if (Dt <= 0 || !std::isfinite(Dt) || Length < 100 || !std::isfinite(Length)) return;
    if (Cars.size() > 32) return;
    std::array<TrafficCar, 32> Before{};
    std::copy(Cars.begin(), Cars.end(), Before.begin());
    const double Lane = std::min(2.65, std::max(0.0, HalfWidth - 1.3));
    for (std::size_t I = 0; I < Cars.size(); ++I)
    {
        auto& Car = Cars[I]; const auto& Old = Before[I];
        Car.DecisionDelay = std::max(0.0, Car.DecisionDelay - Dt);
        Car.DamageDelay = std::max(0.0, Car.DamageDelay - Dt);
        const double K = std::max(std::abs(Curvature(Old.Distance + 25)), std::abs(Curvature(Old.Distance + 90)));
        double Desired = std::clamp(std::sqrt(16.0 / std::max(K, .001)), 14.0, 69.0 + (I % 5) * 1.5) * TrafficPace(Difficulty);
        if (Car.DamageDelay > 0) Desired *= .50;
        auto VisitOthers = [&](auto Function)
        {
            for (std::size_t J = 0; J < Cars.size(); ++J)
                if (J != I) Function(Before[J], J < I ? Cars[J].TargetLane : Before[J].TargetLane);
            if (Player) Function(*Player, Player->Lateral);
        };
        double Ahead = 1000, AheadSpeed = Desired;
        VisitOthers([&](const TrafficCar& Other, double)
        {
            const double Gap = TrafficGap(Other.Distance, Old.Distance, Length);
            if (Gap > 0 && Gap < Ahead && std::abs(Other.Lateral - Old.Lateral) < 2.5)
            { Ahead = Gap; AheadSpeed = Other.Speed; }
        });
        if (Car.DecisionDelay <= 0)
        {
            const double RacingLane = std::abs(K) < .003 ? 0 : (Curvature(Old.Distance + 40) > 0 ? -1.4 : 1.4);
            double Best = 1e9, Target = Old.TargetLane;
            for (double Candidate : {-Lane, 0.0, Lane})
            {
                bool Safe = true; double Cost = std::abs(Candidate - RacingLane) * .35 + std::abs(Candidate - Old.Lateral) * .45;
                VisitOthers([&](const TrafficCar& Other, double Reserved)
                {
                    const double Gap = TrafficGap(Other.Distance, Old.Distance, Length);
                    const double Predicted = Gap + (Other.Speed - Old.Speed) * 1.0;
                    const bool SweptAcross = Other.Lateral >= std::min(Old.Lateral, Candidate) - 2.5
                        && Other.Lateral <= std::max(Old.Lateral, Candidate) + 2.5;
                    if (std::abs(Gap) < 5.2 && SweptAcross) Safe = false;
                    if (std::abs(Predicted) < 7 && std::abs(Other.Lateral - Candidate) < 2.65) Safe = false;
                    if (std::abs(Gap) < 15 && std::abs(Reserved - Candidate) < 2.5) Safe = false;
                    if (Gap > 0 && Gap < 70 && std::abs(Other.Lateral - Candidate) < 2.65)
                        Cost += (70 - Gap) * .16 + std::max(0.0, Desired - Other.Speed) * .6;
                });
                if (Safe && Cost < Best) { Best = Cost; Target = Candidate; }
            }
            Car.TargetLane = Target; Car.DecisionDelay = 1.0 + (I % 3) * .12;
        }
        // Brake until an overtaking lane is actually reached, not merely selected.
        if (Ahead < 45) Desired = std::min(Desired, std::max(0.0, AheadSpeed + (Ahead - 8.0) * .65));
        const double MaxDelta = (Desired < Old.Speed ? 18.0 : 5.0) * Dt;
        Car.Speed = std::max(0.0, Old.Speed + std::clamp(Desired - Old.Speed, -MaxDelta, MaxDelta));
        const double LaneStep = std::clamp((Car.TargetLane - Old.Lateral) * 2.0, -1.8, 1.8) * Dt;
        Car.Lateral = std::clamp(Old.Lateral + LaneStep, -HalfWidth + 1.2, HalfWidth - 1.2);
        Car.LaneVelocity = (Car.Lateral - Old.Lateral) / Dt;
        Car.Distance = Old.Distance + Car.Speed * Dt;
        // Resolve accidental overlap after emergency braking at a standing car.
        VisitOthers([&](const TrafficCar& Other, double)
        {
            const double Gap = TrafficGap(Other.Distance, Old.Distance, Length);
            if (Gap > 0 && Gap < 7 && std::abs(Other.Lateral - Car.Lateral) < 2.5)
            {
                Car.Distance = std::min(Car.Distance, Old.Distance + std::max(0.0, Gap - 5.2));
                Car.Speed = std::min(Car.Speed, Other.Speed);
            }
        });
    }
}
}
