#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "VroomRaceRules.h"
#include "VroomSession.h"
#include "VroomTrackLayout.h"

namespace Vroom
{
struct TrafficCar
{
    double Distance = 0, Lateral = 0, Speed = 0;
    double TargetLane = 0, LaneVelocity = 0, DecisionDelay = 0, DamageDelay = 0;
    double Fuel = 1.0;
    double TyreHealth = 2048.0;
    double FuelBurn = 0.0;
    double TyreWearPerMetre = 0.0;
    bool Pitting = false;
    bool PitCommitted = false, PitExiting = false;
    double PitTimer = 0.0;
    int LiveryIndex = 0;
    DriverPersonality Personality{};
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

inline double PitAlong(double Distance, double Length)
{
    if (!std::isfinite(Distance) || !std::isfinite(Length) || Length <= 0) return -1;
    return std::fmod(std::fmod(Distance, Length) + Length, Length);
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
        const DriverPersonality& Pers = Car.Personality;
        const double Reaction = std::clamp(Pers.Reaction, 0.6, 1.4);
        const double Caution = std::clamp(Pers.Caution, 0.6, 1.5);
        const double Aggression = std::clamp(Pers.Aggression, 0.6, 1.5);
        const double Precision = std::clamp(Pers.Precision, 0.6, 1.5);
        const double Consistency = std::clamp(Pers.Consistency, 0.5, 1.4);
        Car.DecisionDelay = std::max(0.0, Car.DecisionDelay - Dt * Reaction);
        Car.DamageDelay = std::max(0.0, Car.DamageDelay - Dt);

        const double Along = PitAlong(Old.Distance, Length);
        const double K25 = std::abs(Curvature(Old.Distance + 25));
        const double K90 = std::abs(Curvature(Old.Distance + 90));
        const double K140 = std::abs(Curvature(Old.Distance + 140 * Reaction));
        const double K = std::max(K25, K90);
        const double Grip = 0.65 + 0.35 * std::clamp(Old.TyreHealth / 2048.0, 0.0, 1.0);

        // Decide before passing the entry, using the resources for another
        // complete lap plus a margin. Waiting until 18% can strand a car before
        // the next entry. Fresh cars do not stop on their initial grid lap.
        const double FuelForLap = Car.FuelBurn * Length / 48.0 * .78 * 1.12;
        const double TyresForLap = Car.TyreWearPerMetre * Length * 1.15;
        const double ToEntry = Along <= 20.0 ? 102.0 - Along : Length - Along + 102.0;
        const double RequiredLaps = 1.0 + ToEntry / Length;
        const bool NeedPit = (Car.Fuel < std::max(.18, FuelForLap * RequiredLaps)
            || Car.TyreHealth < std::max(420.0, TyresForLap * RequiredLaps))
            && (Car.Fuel < .85 || Car.TyreHealth < 2048.0 * .85);
        if (NeedPit && !Car.Pitting && !Car.PitExiting && (Along <= 20.0 || Along >= Length - 180.0))
            Car.PitCommitted = true;
        const double PitApproach = Along >= Length - 180.0 ? Along - Length : Along;
        const bool InPitWindow = Along >= 31.0 && Along <= 220.0;
        const bool InPitLane = InPitWindow && Old.Lateral > 6.5;
        const bool AtBox = Along >= 96.0 && Along <= 108.0 && Old.Lateral >= 11.0 && Old.Lateral <= 14.0;

        if (Car.Pitting)
        {
            Car.PitTimer += Dt;
            Car.Speed = 0.0;
            Car.Lateral = Old.Lateral;
            Car.TargetLane = Old.Lateral;
            Car.LaneVelocity = 0.0;
            Car.Distance = Old.Distance;
            if (Car.PitTimer >= 7.0)
            {
                Car.Pitting = false;
                Car.PitTimer = 0.0;
                Car.Fuel = 1.0;
                Car.TyreHealth = 2048.0;
                Car.PitCommitted = false;
                Car.PitExiting = true;
            }
            continue;
        }

        bool BoxOccupied = false;
        for (std::size_t J = 0; J < Cars.size(); ++J)
        {
            if (I == J || Before[J].Lateral < 9.0) continue;
            const double Gap = TrafficGap(Before[J].Distance, Old.Distance, Length);
            if (std::abs(Gap) < 9.0 && (Before[J].Pitting || (Gap > 0 && Before[J].PitCommitted)))
                BoxOccupied = true;
        }
        if (Car.PitCommitted && AtBox && Old.Speed < .3 && !BoxOccupied)
        {
            Car.Pitting = true;
            Car.PitTimer = 0.0;
            Car.Speed = 0.0;
            Car.Lateral = Old.Lateral;
            Car.TargetLane = Old.Lateral;
            Car.LaneVelocity = 0.0;
            Car.Distance = Old.Distance;
            continue;
        }

        double Desired = std::clamp(std::sqrt(16.0 / std::max(K, .001)), 14.0, 69.0 + (I % 5) * 1.5)
            * TrafficPace(Difficulty) * std::clamp(Pers.Pace, 0.85, 1.12) * Grip;
        if (K140 > std::max(K90, 0.006))
            Desired = std::min(Desired, std::sqrt(16.0 / std::max(K140, .001)) + 10.0 * Reaction);
        if (Car.DamageDelay > 0) Desired *= .50;
        // Management is expressed as driving effort, never free resources.
        Desired *= 1.0 - std::clamp(1.0 - Pers.FuelManagement, 0.0, .3) * .12;
        if (K > .003) Desired *= 1.0 - std::clamp(1.0 - Pers.TyreManagement, 0.0, .3) * .16;
        if (Car.Fuel < 0.12) Desired *= 0.72;
        if (Car.TyreHealth < 360) Desired *= 0.70;
        if (Car.Fuel <= 0) Desired = 0;
        if (Consistency < 0.95)
        {
            const int Phase = static_cast<int>(Old.Distance * 0.31 + static_cast<int>(I) * 47);
            if (Phase % 173 == 0)
                Desired *= 0.82 + 0.10 * Consistency;
        }

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
            const double Width = 2.5 * Caution;
            if (Gap > 0 && Gap < Ahead && std::abs(Other.Lateral - Old.Lateral) < Width)
            { Ahead = Gap; AheadSpeed = Other.Speed; }
        });
        if (Car.PitCommitted)
        {
            Car.TargetLane = PitApproach < TrackLayout::PitLaneStart
                ? TrackLayout::PitCenterAt(PitApproach + 2.0, HalfWidth) : TrackLayout::PitCenter;
            // Brake for the box before reaching it. Once stopped, finish the
            // lateral alignment instead of driving past while still merging.
            const double ToBox = std::max(0.0, 102.0 - PitApproach);
            Desired = std::min({Desired, 16.0, std::sqrt(2.0 * 12.0 * ToBox)});
            if (PitApproach >= 20 && PitApproach < TrackLayout::PitLaneStart) Desired = std::min(Desired, 4.0);
            // The right-hand car already owns a side-by-side entry. Wait for
            // its rear axle rather than sweeping across it into the pit lane.
            VisitOthers([&](const TrafficCar& Other, double)
            {
                const double Gap = TrafficGap(Other.Distance, Old.Distance, Length);
                if (Gap >= 0 && Gap < 7.0 && Other.Lateral > Old.Lateral + .2 && Other.Lateral < 14)
                { Desired = 0; Car.TargetLane = Old.Lateral; }
            });
        }
        else if (Car.PitExiting)
        {
            bool MergeBlocked = false;
            VisitOthers([&](const TrafficCar& Other, double)
            {
                const double Gap = TrafficGap(Other.Distance, Old.Distance, Length);
                const double Predicted = Gap + (Other.Speed - Old.Speed) * 1.5;
                if (Other.Lateral < 6 && (std::abs(Gap) < 12 || std::abs(Predicted) < 12)) MergeBlocked = true;
            });
            const double ExitT = std::clamp((Along + 2.0 - TrackLayout::PitLaneEnd)
                / (TrackLayout::PitExit - TrackLayout::PitLaneEnd), 0.0, 1.0);
            Car.TargetLane = TrackLayout::PitCenterAt(Along + 2.0, HalfWidth)
                - (HalfWidth - Lane) * ExitT * ExitT * (3.0 - 2.0 * ExitT);
            if (Along < TrackLayout::PitLaneEnd && MergeBlocked)
                Car.TargetLane = TrackLayout::PitCenter;
            Desired = std::min(Desired, Along < 175.0 ? 12.0 : 3.5);
            if (MergeBlocked && Along >= 180.0 && Along < TrackLayout::PitLaneEnd)
                Desired = 0;
            if (Along >= TrackLayout::PitExit && Old.Lateral < HalfWidth - 1.2)
                Car.PitExiting = false;
        }
        else if (InPitLane)
        {
            Car.TargetLane = Lane;
            Desired = std::min(Desired, 12.0);
        }
        else if (Car.DecisionDelay <= 0)
        {
            const double RacingLane = std::abs(K) < .003 ? 0 : (Curvature(Old.Distance + 40) > 0 ? -1.4 : 1.4);
            double Best = 1e9, Target = Old.TargetLane;
            for (double Candidate : {-Lane, 0.0, Lane})
            {
                bool Safe = true;
                double Cost = std::abs(Candidate - RacingLane) * .35 * (2.0 - Precision)
                    + std::abs(Candidate - Old.Lateral) * .45;
                VisitOthers([&](const TrafficCar& Other, double Reserved)
                {
                    const double Gap = TrafficGap(Other.Distance, Old.Distance, Length);
                    const double Predicted = Gap + (Other.Speed - Old.Speed) * Reaction;
                    const bool SweptAcross = Other.Lateral >= std::min(Old.Lateral, Candidate) - 2.5
                        && Other.Lateral <= std::max(Old.Lateral, Candidate) + 2.5;
                    const double ContactGap = 5.2 * Caution;
                    if (std::abs(Gap) < ContactGap && SweptAcross) Safe = false;
                    if (std::abs(Predicted) < 7 * Caution && std::abs(Other.Lateral - Candidate) < 2.65) Safe = false;
                    if (std::abs(Gap) < 15 * Caution && std::abs(Reserved - Candidate) < 2.5) Safe = false;
                    if (Gap > 0 && Gap < 70 && std::abs(Other.Lateral - Candidate) < 2.65)
                        Cost += (70 - Gap) * .16 / Aggression + std::max(0.0, Desired - Other.Speed) * .6;
                });
                if (Safe && Cost < Best) { Best = Cost; Target = Candidate; }
            }
            Car.TargetLane = Target;
            Car.DecisionDelay = (1.0 + (I % 3) * .12) / Reaction;
        }
        const double BrakeLook = 45.0 * Caution;
        if (Ahead < BrakeLook)
            Desired = std::min(Desired, std::max(0.0, AheadSpeed + (Ahead - 8.0 * Caution) * .65));
        const double MaxDelta = (Desired < Old.Speed ? 18.0 : 5.0) * Dt;
        Car.Speed = std::max(0.0, Old.Speed + std::clamp(Desired - Old.Speed, -MaxDelta, MaxDelta));
        if (Car.PitCommitted)
            Car.Speed = std::min(Car.Speed, std::max(0.0, 102.0 - PitApproach) / Dt);
        if (Car.PitExiting && Old.Lateral >= HalfWidth - 1.2)
            Car.Speed = std::min(Car.Speed, std::max(0.0, 220.0 - Along) / Dt);
        const double LaneRate = Car.PitCommitted || Car.PitExiting || InPitLane ? 2.4 : 1.8;
        const double LaneStep = std::clamp((Car.TargetLane - Old.Lateral) * 2.0, -LaneRate, LaneRate) * Dt;
        double LatMin = -HalfWidth + 1.2;
        double LatMax = HalfWidth - 1.2;
        if (InPitWindow && (Car.PitCommitted || Car.PitExiting || Old.Lateral > 6.0 || Car.TargetLane > 6.0))
            LatMax = 14.5;
        Car.Lateral = std::clamp(Old.Lateral + LaneStep, LatMin, LatMax);
        Car.LaneVelocity = (Car.Lateral - Old.Lateral) / Dt;
        Car.Distance = Old.Distance + Car.Speed * Dt;
        VisitOthers([&](const TrafficCar& Other, double)
        {
            const double Gap = TrafficGap(Other.Distance, Old.Distance, Length);
            if (Gap > 0 && Gap < 7 && std::abs(Other.Lateral - Car.Lateral) < 2.5)
            {
                Car.Distance = std::min(Car.Distance, Old.Distance + std::max(0.0, Gap - 5.2));
                Car.Speed = std::min(Car.Speed, Other.Speed);
            }
        });
        // Same throttle-seconds and corner-distance wear equations as the
        // player's DriveModel. The planner requests no throttle when braking
        // or stopped; low-resource personalities change pace, not these rates.
        const double Throttle = Car.Speed < Old.Speed - 1e-8 || Car.Speed < .01 ? 0.0
            : std::clamp(.18 + Car.Speed / 100.0 + std::max(0.0, (Car.Speed - Old.Speed) / Dt) * .08, 0.0, 1.0);
        Car.Fuel = std::max(0.0, Old.Fuel - Throttle * Dt * std::max(0.0, Car.FuelBurn));
        const double Travel = std::max(0.0, Car.Distance - Old.Distance);
        Car.TyreHealth = std::max(0.0, Old.TyreHealth - Car.TyreWearPerMetre * Travel
            * (1.0 + 18.0 * std::abs(Curvature(Old.Distance))));
    }
}
}
