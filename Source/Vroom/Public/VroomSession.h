#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include "VroomTrackLayout.h"

namespace Vroom
{
enum class SessionMode { Arcade, GrandPrix, TimeTrial };
enum class SessionPhase { Menu, Countdown, Qualifying, Racing, Finished };
enum class RaceDifficulty { Easy, Normal, Hard };
struct ChampionshipEntry { int Driver = 0, Points = 0, Wins = 0; };

// Rules are independent from rendering and use the same simulation clock as
// driving. The selected format is an adaptation, not an undocumented Amiga rule.
struct SessionModel
{
    SessionMode Mode = SessionMode::Arcade;
    SessionPhase Phase = SessionPhase::Menu;
    RaceDifficulty Difficulty = RaceDifficulty::Normal;
    int Position = 13, LapTarget = 3, LapsCompleted = 0, TargetPasses = 27;
    int TrackIndex = 4, Stage = 0, ChampionshipPoints = 0;
    int QualificationPosition = 13;
    double TimeLimit = 300, FinishedTime = 0, QualificationTime = 0;
    bool Won = false, Retired = false, QualificationComplete = false, ChampionshipComplete = false;
    bool QualifyingRun = false;
    inline static constexpr int PlayerDriver = 12;
    std::array<int, 13> ChampionshipScores{}, ChampionshipWins{}, RaceRanks{};
    std::array<int, 13> RaceFinishOrder{};
    std::array<std::array<int, 13>, 13> ChampionshipFinishes{};
    bool RaceClassified = false;
    inline static constexpr std::array<int, 6> ChampionshipOrder = {4, 0, 1, 5, 3, 2};
    inline static constexpr std::array<int, 13> Points = {10,6,4,3,2,1,0,0,0,0,0,0,0};

    bool Active() const { return Phase == SessionPhase::Countdown || Phase == SessionPhase::Qualifying || Phase == SessionPhase::Racing; }
    bool Solo() const { return Mode == SessionMode::TimeTrial || QualifyingRun; }
    double RemainingSeconds(double Clock) const { return std::max(0.0, TimeLimit - std::max(0.0, Clock)); }
    void ResetChampionship()
    {
        ChampionshipPoints = 0; ChampionshipComplete = false; RaceClassified = false;
        ChampionshipScores.fill(0); ChampionshipWins.fill(0); RaceRanks.fill(0); RaceFinishOrder.fill(-1);
        for (auto& Finishes : ChampionshipFinishes) Finishes.fill(0);
    }
    std::array<ChampionshipEntry, 13> Standings() const
    {
        std::array<ChampionshipEntry, 13> Result{};
        for (int I = 0; I < 13; ++I) Result[I] = {I, ChampionshipScores[I], ChampionshipWins[I]};
        std::sort(Result.begin(), Result.end(), [&](const ChampionshipEntry& A, const ChampionshipEntry& B)
        {
            if (A.Points != B.Points) return A.Points > B.Points;
            // Count back wins, then second places, and so on. Driver identity
            // supplies a stable display order if every sporting result is tied.
            for (int Place = 0; Place < 13; ++Place)
                if (ChampionshipFinishes[A.Driver][Place] != ChampionshipFinishes[B.Driver][Place])
                    return ChampionshipFinishes[A.Driver][Place] > ChampionshipFinishes[B.Driver][Place];
            return A.Driver < B.Driver;
        });
        return Result;
    }
    int ChampionshipRank(int Driver = PlayerDriver) const
    {
        Driver = std::clamp(Driver, 0, 12);
        const auto Order = Standings();
        for (int Place = 0; Place < 13; ++Place) if (Order[Place].Driver == Driver) return Place + 1;
        return 13;
    }
    int ChampionshipWinner() const { return Standings()[0].Driver; }
    bool ClassifyRace(const std::array<int, 13>& Order)
    {
        if (Mode != SessionMode::GrandPrix || QualifyingRun || Phase != SessionPhase::Finished || RaceClassified) return false;
        if (Retired && Order.back() != PlayerDriver) return false;
        std::array<bool, 13> Seen{};
        for (int Driver : Order)
        {
            if (Driver < 0 || Driver >= 13 || Seen[Driver]) return false;
            Seen[Driver] = true;
        }
        RaceFinishOrder = Order;
        for (int Place = 0; Place < 13; ++Place)
        {
            const int Driver = Order[Place];
            RaceRanks[Driver] = Place + 1;
            ChampionshipScores[Driver] += Points[Place];
            ++ChampionshipFinishes[Driver][Place];
            if (Place == 0) ++ChampionshipWins[Driver];
        }
        Position = RaceRanks[PlayerDriver];
        ChampionshipPoints = ChampionshipScores[PlayerDriver];
        Won = !Retired && Position == 1; RaceClassified = true;
        return true;
    }
    bool UndoLastRace()
    {
        if (!RaceClassified) return false;
        for (int Driver = 0; Driver < 13; ++Driver)
        {
            const int Place = RaceRanks[Driver] - 1;
            ChampionshipScores[Driver] -= Points[Place];
            --ChampionshipFinishes[Driver][Place];
            if (Place == 0) --ChampionshipWins[Driver];
        }
        ChampionshipPoints = ChampionshipScores[PlayerDriver];
        ChampionshipComplete = false; RaceClassified = false;
        RaceRanks.fill(0); RaceFinishOrder.fill(-1);
        return true;
    }
    void Begin(double Length, bool RaceAfterQualification = false)
    {
        Length = std::isfinite(Length) ? std::clamp(Length, 500.0, 50000.0) : 5000.0;
        Phase = SessionPhase::Countdown;
        RaceClassified = false; RaceRanks.fill(0); RaceFinishOrder.fill(-1);
        Won = false; Retired = false; FinishedTime = 0; LapsCompleted = 0;
        QualifyingRun = Mode == SessionMode::GrandPrix && !RaceAfterQualification;
        if (QualifyingRun) { QualificationComplete = false; QualificationTime = 0; }
        LapTarget = QualifyingRun ? 1 : (Mode == SessionMode::TimeTrial ? 0 : 3);
        const int Level = static_cast<int>(Difficulty);
        TargetPasses = 18 + Level * 9;
        TimeLimit = Length / (Level == 0 ? 33.0 : Level == 1 ? 39.0 : 45.0) * 3.0 + 25.0;
        Position = Solo() ? 1 : (Mode == SessionMode::GrandPrix ? QualificationPosition : 13);
    }
    void Tick(double Clock, int Completed, int Passed, int RacePosition, double Fuel, double Speed)
    {
        if (!Active() || !std::isfinite(Clock)) return;
        if (Clock < 0) return;
        Phase = QualifyingRun ? SessionPhase::Qualifying : SessionPhase::Racing;
        LapsCompleted = std::max(LapsCompleted, Completed);
        Position = std::clamp(RacePosition, 1, 13);
        if (Mode == SessionMode::Arcade)
        {
            if (LapsCompleted >= LapTarget) Finish(Clock, Passed >= TargetPasses);
            else if (Clock >= TimeLimit || (Fuel <= 0 && Speed < .15)) Finish(Clock, false);
        }
        else if (Mode == SessionMode::GrandPrix && !QualifyingRun && LapsCompleted >= LapTarget)
            Finish(Clock, Position == 1);
        else if (Mode == SessionMode::GrandPrix && Fuel <= 0 && Speed < .15)
        {
            Retired = true; Position = 13;
            if (QualifyingRun) { QualificationComplete = true; QualificationPosition = 13; }
            Finish(Clock, false);
        }
    }
    void CompleteQualification(double Clock, bool Clean, double Benchmark)
    {
        if (!QualifyingRun || !Active()) return;
        QualificationTime = std::max(0.0, Clock);
        // One clean standing-start lap determines the grid against an AI pace estimate.
        QualificationPosition = Clean && Benchmark > 0
            ? std::clamp(1 + static_cast<int>(std::floor((Clock / Benchmark - .88) / .018)), 1, 13) : 13;
        QualificationComplete = true;
        LapsCompleted = 1;
        Finish(Clock, Clean);
    }
    void Finish(double Clock, bool Success)
    {
        if (Phase == SessionPhase::Finished) return;
        FinishedTime = std::max(0.0, Clock); Won = Success;
        Phase = SessionPhase::Finished;
        if (!QualifyingRun && Stage == 5 && (Mode == SessionMode::GrandPrix || Won))
            ChampionshipComplete = true;
    }
    bool AdvanceStage()
    {
        if (Phase != SessionPhase::Finished || QualifyingRun || Mode == SessionMode::TimeTrial
            || (Mode == SessionMode::Arcade && !Won) || Stage >= 5) return false;
        ++Stage; TrackIndex = ChampionshipOrder[Stage];
        QualificationComplete = false; QualificationTime = 0; QualificationPosition = 13;
        return true;
    }
    void SelectTrack(int Track)
    {
        TrackIndex = std::clamp(Track, 0, 5);
        const auto It = std::find(ChampionshipOrder.begin(), ChampionshipOrder.end(), TrackIndex);
        Stage = static_cast<int>(It - ChampionshipOrder.begin());
        ResetChampionship();
        QualificationComplete = false; QualificationPosition = 13;
        Phase = SessionPhase::Menu;
    }
};

struct PitStopModel
{
    bool InLane = false, Available = false, Servicing = false;
    bool NeedTyres = false, NeedFuel = false;
    double Remaining = 0, Duration = 0;
    static double Along(double Distance, double Length)
    {
        if (!std::isfinite(Distance) || !std::isfinite(Length) || Length <= 0) return -1;
        return std::fmod(std::fmod(Distance, Length) + Length, Length);
    }
    void Locate(double Distance, double Lateral, double Speed, double Length)
    {
        const double S = Along(Distance, Length);
        InLane = S >= TrackLayout::PitEntry && S <= TrackLayout::PitExit
            && std::isfinite(Lateral)
            && std::abs(Lateral - TrackLayout::PitCenterAt(S, 4.75)) <= TrackLayout::PitHalfWidth;
        // Forgiving service area along the straight pit lane. Entry/exit
        // connectors and the racing surface never grant a pit service.
        Available = InLane && S >= 65 && S <= 180 && Lateral >= 10.5 && Lateral <= 14.5
            && std::isfinite(Speed) && std::abs(Speed) < .8;
    }
    bool Request(bool Tyres, bool Fuel)
    {
        if (!Available || Servicing || (!Tyres && !Fuel)) return false;
        NeedTyres = Tyres; NeedFuel = Fuel;
        Duration = Remaining = (Tyres ? 4.0 : 0.0) + (Fuel ? 3.0 : 0.0);
        Servicing = true; return true;
    }
    bool Step(double Dt)
    {
        if (!Servicing || !std::isfinite(Dt) || Dt <= 0) return false;
        Remaining = std::max(0.0, Remaining - Dt);
        if (Remaining > 1e-8) return false;
        Remaining = 0; Servicing = false; return true;
    }
};
}
