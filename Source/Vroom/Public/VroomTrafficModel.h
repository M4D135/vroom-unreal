#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "VroomRaceRules.h"
#include "VroomSession.h"
#include "VroomTrackLayout.h"
#include "VroomDamageModel.h"

namespace Vroom
{
struct TrafficCar
{
    DamageModel Damage;
    double ImpactLateralVelocity = 0;
    double Distance = 0, Lateral = 0, Speed = 0;
    double TargetLane = 0, LaneVelocity = 0, DecisionDelay = 0, DamageDelay = 0;
    // -2: no pass, -1: player, otherwise the stable traffic slot. A chosen
    // side remains reserved until our rear clears the car we are passing.
    int OvertakeTarget = -2;
    double OvertakeHold = 0;
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
    return Difficulty == RaceDifficulty::Easy ? .90 : Difficulty == RaceDifficulty::Hard ? 1.05 : .98;
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
    if (Dt <= 0 || !std::isfinite(Dt) || Length < 100 || !std::isfinite(Length)
        || !std::isfinite(HalfWidth) || HalfWidth < 1.3) return;
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
        Car.ImpactLateralVelocity *= std::exp(-Dt / .35);
        Car.OvertakeHold = std::max(0.0, Car.OvertakeHold - Dt);

        const double Along = PitAlong(Old.Distance, Length);
        const double Grip = (0.65 + 0.35 * std::clamp(Old.TyreHealth / 2048.0, 0.0, 1.0)) * Old.Damage.GripModifier();
        double PackAssist = 0;
        if (Player && Player->Speed > 12 && Player->Fuel > .12 && !Player->Pitting && !Player->PitCommitted
            && !Player->PitExiting && std::abs(Player->Lateral) < HalfWidth)
            PackAssist = std::clamp(TrafficGap(Player->Distance, Old.Distance, Length) / 300.0, -.02, .04);
        const double Pace = TrafficPace(Difficulty) * std::clamp(Pers.Pace, .97, 1.03) * (1 + PackAssist);
        const double TopSpeed = (92.0 + (I % 3) * 1.0) * Pace * Grip / std::sqrt(Old.Damage.DragModifier());
        // Matches the player's launch and rolling acceleration without copying
        // its instantaneous speed or position. Torque fades near maximum speed.
        const double MotorAcceleration = 28.0 * std::max(.025, 1.0 - Old.Speed / TopSpeed);
        const double Brake = 18.0 * Grip;
        const double PlanningBrake = 11.5 * Grip / std::sqrt(Caution);
        // A distance-based braking envelope anticipates hairpins before they
        // enter a fixed 90-m window, without slowing for distant gentle bends.
        // Nine samples bound the cost even for the complete twelve-car field.
        constexpr std::array<double, 9> Lookahead = {0, 20, 40, 65, 95, 130, 170, 210, 250};
        std::array<double, 9> Curves{};
        double Desired = TopSpeed, Peak = 0, PeakSigned = 0;
        for (std::size_t Sample = 0; Sample < Lookahead.size(); ++Sample)
        {
            const double RawCurve = Curvature(Old.Distance + Lookahead[Sample]);
            Curves[Sample] = std::isfinite(RawCurve) ? RawCurve : 0.0;
            const double AbsCurve = std::abs(Curves[Sample]);
            const double CornerSpeed = std::min(TopSpeed,
                std::clamp(std::sqrt(22.0 / std::max(AbsCurve, .001)), 14.0, 100.0) * Pace * Grip);
            const double BrakeDistance = std::max(0.0, Lookahead[Sample] - 18.0 - Old.Speed * .18 / Reaction);
            Desired = std::min(Desired, std::sqrt(CornerSpeed * CornerSpeed + 2.0 * PlanningBrake * BrakeDistance));
            if (Sample <= 4 && AbsCurve > Peak) { Peak = AbsCurve; PeakSigned = Curves[Sample]; }
        }
        const double K = std::max(std::abs(Curves[0]), std::abs(Curves[1]));
        // Set up outside the corner, then approach its inside as the apex
        // reaches the car. Continuous targets avoid snapping between rails.
        const double Apex = Peak > .0025 ? std::clamp((K / Peak - .25) / .65, 0.0, 1.0) : .5;
        const double RacingLane = Peak > .0025
            ? std::copysign(1.0, PeakSigned) * Lane * (-.72 + 1.32 * Apex) : 0.0;

        // Decide before passing the entry, using the resources for another
        // complete lap plus a margin. Waiting until 18% can strand a car before
        // the next entry. Fresh cars do not stop on their initial grid lap.
        const double FuelForLap = Car.FuelBurn * Length / 48.0 * .78 * 1.12;
        const double TyresForLap = Car.TyreWearPerMetre * Length * 1.15;
        const double ToEntry = Along <= 20.0 ? 102.0 - Along : Length - Along + 102.0;
        const double RequiredLaps = 1.0 + ToEntry / Length;
        const bool NeedBodyRepair = Car.Damage.State.Total() > .48;
        const bool NeedPit = ((Car.Fuel < std::max(.18, FuelForLap * RequiredLaps)
            || Car.TyreHealth < std::max(420.0, TyresForLap * RequiredLaps))
            && (Car.Fuel < .85 || Car.TyreHealth < 2048.0 * .85)) || NeedBodyRepair;
        if (NeedPit && !Car.Pitting && !Car.PitExiting && (Along <= 20.0 || Along >= Length - 180.0))
            Car.PitCommitted = true;
        const double PitApproach = Along >= Length - 180.0 ? Along - Length : Along;
        const bool InPitWindow = Along >= 31.0 && Along <= 220.0;
        const bool InPitLane = InPitWindow && Old.Lateral > 6.5;
        const bool AtBox = Along >= 96.0 && Along <= 108.0 && Old.Lateral >= 11.0 && Old.Lateral <= 14.0;

        if (Car.Pitting)
        {
            Car.OvertakeTarget = -2; Car.OvertakeHold = 0;
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
                Car.Damage.Repair();
                Car.ImpactLateralVelocity = 0;
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

        if (Car.DamageDelay > 0) Desired *= 1 - .25 * std::clamp(Car.DamageDelay / 1.2, 0.0, 1.0);
        // Management is expressed as driving effort, never free resources.
        Desired *= 1.0 - std::clamp(1.0 - Pers.FuelManagement, 0.0, .3) * .12;
        if (K > .003) Desired *= 1.0 - std::clamp(1.0 - Pers.TyreManagement, 0.0, .3) * .16;
        if (Car.Fuel < 0.12) Desired *= 0.72;
        if (Car.TyreHealth < 360) Desired *= 0.70;
        if (Car.Fuel <= 0) Desired = 0;
        if (Consistency < 0.95)
        {
            // Small repeatable pace variations, not a one-frame brake pulse.
            Desired *= 1.0 - (0.95 - Consistency) * .09
                * (1.0 + std::sin(Old.Distance / 95.0 + static_cast<double>(I) * 2.1));
        }

        auto VisitOthers = [&](auto Function)
        {
            for (std::size_t J = 0; J < Cars.size(); ++J)
                if (J != I) Function(Before[J], J < I ? Cars[J].TargetLane : Before[J].TargetLane);
            if (Player) Function(*Player, Player->Lateral);
        };
        double Ahead = 1000, AheadSpeed = Desired;
        int AheadIndex = -2;
        VisitOthers([&](const TrafficCar& Other, double)
        {
            const double Gap = TrafficGap(Other.Distance, Old.Distance, Length);
            const double MaxLateralSpeed = &Other == Player ? 12.0 : 2.4;
            const double FutureLateral = Other.Lateral + std::clamp(Other.LaneVelocity, -MaxLateralSpeed, MaxLateralSpeed) * .6;
            const double Width = 2.55;
            const bool CrossesLane = Old.Lateral > std::min(Other.Lateral, FutureLateral) - Width
                && Old.Lateral < std::max(Other.Lateral, FutureLateral) + Width;
            if (Gap > 0 && Gap < Ahead && CrossesLane)
            {
                Ahead = Gap; AheadSpeed = Other.Speed;
                AheadIndex = &Other == Player ? -1 : static_cast<int>(&Other - Before.data());
            }
        });
        if (Car.OvertakeTarget != -2)
        {
            const auto* PassedCar = Car.OvertakeTarget == -1 ? Player
                : Car.OvertakeTarget >= 0 && Car.OvertakeTarget < static_cast<int>(Cars.size())
                    ? &Before[Car.OvertakeTarget] : nullptr;
            const double Gap = PassedCar ? TrafficGap(PassedCar->Distance, Old.Distance, Length) : -1000;
            if ((Gap < -9.0 && Car.OvertakeHold <= 0) || Gap > 110 || Gap < -70 || !PassedCar)
            { Car.OvertakeTarget = -2; Car.OvertakeHold = 0; Car.DecisionDelay = 0; }
        }
        if (Car.PitCommitted)
        {
            Car.OvertakeTarget = -2; Car.OvertakeHold = 0;
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
            Car.OvertakeTarget = -2; Car.OvertakeHold = 0;
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
        else if (Car.DecisionDelay <= 0 && Car.OvertakeTarget == -2)
        {
            double Best = 1e9, Target = Old.TargetLane;
            const bool WantsPass = Ahead < 85.0 && Desired > AheadSpeed + 1.5 / Aggression;
            for (double Candidate : {-Lane, 0.0, Lane, RacingLane, Old.TargetLane})
            {
                bool Safe = true;
                const double CrossingTime = std::min(2.6, std::abs(Candidate - Old.Lateral) / 1.8 + .35);
                double Cost = std::abs(Candidate - RacingLane) * .8 * Precision
                    + std::abs(Candidate - Old.Lateral) * .18
                    + (std::abs(Candidate - Old.TargetLane) > .3 ? .24 : 0.0);
                VisitOthers([&](const TrafficCar& Other, double Reserved)
                {
                    const double Gap = TrafficGap(Other.Distance, Old.Distance, Length);
                    const bool SweptAcross = Other.Lateral >= std::min(Old.Lateral, Candidate) - 2.5
                        && Other.Lateral <= std::max(Old.Lateral, Candidate) + 2.5;
                    const double ContactGap = 5.8;
                    if (std::abs(Gap) < ContactGap && SweptAcross) Safe = false;
                    for (double T : {.35, .8, CrossingTime})
                    {
                        const double Predicted = Gap + (Other.Speed - Old.Speed) * T;
                        const double OurLateral = Old.Lateral + std::clamp(Candidate - Old.Lateral, -1.8 * T, 1.8 * T);
                        const double TheirLateral = &Other == Player
                            ? std::clamp(Other.Lateral + std::clamp(Other.LaneVelocity, -12.0, 12.0) * T,
                                std::min(-HalfWidth + 1.2, Other.Lateral), std::max(HalfWidth - 1.2, Other.Lateral))
                            : Other.Lateral + std::clamp(Reserved - Other.Lateral, -1.8 * T, 1.8 * T);
                        if (std::abs(Predicted) < 5.8 && std::abs(TheirLateral - OurLateral) < 2.55)
                            Safe = false;
                    }
                    if (std::abs(Gap) < 15 * Caution && std::abs(Reserved - Candidate) < 2.5) Safe = false;
                    if (Gap > 0 && Gap < 85 && std::abs(Other.Lateral - Candidate) < 2.65)
                        Cost += (85 - Gap) * .12 * Aggression + std::max(0.0, Desired - Other.Speed) * .8;
                });
                if (Safe && Cost < Best) { Best = Cost; Target = Candidate; }
            }
            Car.TargetLane = Target;
            if (WantsPass && AheadIndex != -2 && std::abs(Target - Old.Lateral) > .65)
            {
                Car.OvertakeTarget = AheadIndex;
                Car.OvertakeHold = 2.0 + .35 * Caution;
            }
            Car.DecisionDelay = (.42 + (I % 3) * .07) / Reaction;
        }
        // Follow the actual occupied lane until enough room exists beside the
        // slower car. Relative-speed braking starts early at motorway speeds.
        const double BrakeLook = std::max(45.0, (Old.Speed * Old.Speed - AheadSpeed * AheadSpeed)
            / (2.0 * PlanningBrake) + Old.Speed * .7 * Caution + 9.0);
        if (Ahead < BrakeLook)
        {
            const double Headway = 6.0 + Old.Speed * .38 * Caution;
            const double Comfortable = std::sqrt(std::max(0.0, AheadSpeed * AheadSpeed
                + 2.0 * PlanningBrake * (Ahead - Headway)));
            Desired = std::min(Desired, Comfortable);
            if (Ahead < Headway)
                Desired = std::min(Desired, std::max(0.0, AheadSpeed + (Ahead - Headway) * .9));
        }
        const double MaxDelta = (Desired < Old.Speed ? Brake : MotorAcceleration) * Dt;
        Car.Speed = std::max(0.0, Old.Speed + std::clamp(Desired - Old.Speed, -MaxDelta, MaxDelta));
        if (Car.PitCommitted)
            Car.Speed = std::min(Car.Speed, std::max(0.0, 102.0 - PitApproach) / Dt);
        if (Car.PitExiting && Old.Lateral >= HalfWidth - 1.2)
            Car.Speed = std::min(Car.Speed, std::max(0.0, 220.0 - Along) / Dt);
        const double LaneRate = Car.PitCommitted || Car.PitExiting || InPitLane ? 2.4 : 1.8;
        // Acceleration-limited lateral motion also smooths the rendered yaw.
        // Safety checks below can stop a lateral move immediately if a driver
        // enters the reserved space; the pass itself keeps its chosen side.
        const double WantedLaneVelocity = std::clamp((Car.TargetLane - Old.Lateral) * 2.0, -LaneRate, LaneRate);
        const double LaneAcceleration = Car.PitCommitted || Car.PitExiting || InPitLane ? 8.0 : 3.6;
        const double LaneStep = std::clamp(Old.LaneVelocity
            + std::clamp(WantedLaneVelocity - Old.LaneVelocity, -LaneAcceleration * Dt, LaneAcceleration * Dt),
            -LaneRate, LaneRate) * Dt;
        double LatMin = -HalfWidth + 1.2;
        double LatMax = HalfWidth - 1.2;
        if (InPitWindow && (Car.PitCommitted || Car.PitExiting || Old.Lateral > 6.0 || Car.TargetLane > 6.0))
            LatMax = 14.5;
        Car.Lateral = std::clamp(Old.Lateral + LaneStep + Car.ImpactLateralVelocity * Dt, LatMin, LatMax);
        VisitOthers([&](const TrafficCar& Other, double)
        {
            const double Gap = TrafficGap(Other.Distance, Old.Distance, Length);
            const double EndGap = Gap + (Other.Speed - Car.Speed) * Dt;
            const double ClosestGap = Gap * EndGap <= 0 ? 0 : std::min(std::abs(Gap), std::abs(EndGap));
            const double MaxLateralSpeed = &Other == Player ? 12.0 : 2.4;
            const double TheirEnd = Other.Lateral + std::clamp(Other.LaneVelocity, -MaxLateralSpeed, MaxLateralSpeed) * Dt;
            if (ClosestGap < 5.8 && std::abs(Car.Lateral - TheirEnd) < 2.55
                && std::abs(Car.Lateral - TheirEnd) < std::abs(Old.Lateral - TheirEnd))
                Car.Lateral = Old.Lateral;
        });
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
