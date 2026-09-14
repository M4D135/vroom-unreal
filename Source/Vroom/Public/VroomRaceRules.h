#pragma once

#include "VroomSession.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// Portable race rules used by the Unreal actors and by the C++ tests.
// Consumption, lap validity, start lights and driver colours live here so
// a HUD message cannot diverge from the simulation.
namespace Vroom
{
struct ResourceBudget
{
    double FuelPerThrottleSecond = 0.00055;
    double TyreWearPerMetre = 0.0;
    double TyreWearSkidPerSecond = 12.0;
    double CriticalFuel = 0.22;
    double CriticalTyres = 0.30;
};

// Race sessions are sized so a standing start with a full tank and new tyres
// cannot finish three laps without one stop. Solo sessions keep the original
// mild consumption so a clean timed lap does not require the pits.
inline ResourceBudget MakeResourceBudget(bool Race, RaceDifficulty Difficulty, double LengthM, int Laps)
{
    ResourceBudget Budget;
    if (!Race || Laps <= 0 || !std::isfinite(LengthM) || LengthM < 500.0)
        return Budget;
    const double FuelLaps = Difficulty == RaceDifficulty::Easy ? 1.85
        : Difficulty == RaceDifficulty::Hard ? 1.35 : 1.55;
    const double TyreLaps = Difficulty == RaceDifficulty::Easy ? 1.90
        : Difficulty == RaceDifficulty::Hard ? 1.40 : 1.60;
    const double PaceMps = 48.0;
    const double MeanThrottle = 0.78;
    const double FuelSeconds = FuelLaps * (LengthM / PaceMps) / MeanThrottle;
    Budget.FuelPerThrottleSecond = 1.0 / std::max(40.0, FuelSeconds);
    Budget.TyreWearPerMetre = 2048.0 / std::max(500.0, TyreLaps * LengthM);
    return Budget;
}

inline double TyrePercent(double Health)
{
    return std::clamp(Health / 2048.0, 0.0, 1.0) * 100.0;
}

struct DriverPersonality
{
    const char* Name = "Pilote";
    double Pace = 1.0;
    double Precision = 1.0;
    double Aggression = 1.0;
    double Caution = 1.0;
    double Consistency = 1.0;
    double TyreManagement = 1.0;
    double FuelManagement = 1.0;
    double Reaction = 1.0;
};

// Six liveries already used by the cars: blue, red, yellow, green, purple,
// orange-red. Original Amiga Vroom did not publish per-colour racecraft, so
// these profiles are an explicit remake table. Colour chooses character;
// race difficulty still scales overall pace.
inline DriverPersonality PersonalityByLivery(int Livery)
{
    static const DriverPersonality Table[6] = {
        {"Le Professeur", 0.98, 1.20, 0.88, 1.18, 1.22, 0.86, 0.92, 1.10},
        {"L'Attaquant",   1.03, 0.92, 1.28, 0.82, 0.84, 1.22, 1.12, 1.02},
        {"Le Sprinteur",  1.06, 0.84, 1.12, 0.90, 0.72, 1.18, 1.08, 0.88},
        {"Le Stratège",   1.00, 1.08, 1.00, 1.12, 1.10, 0.80, 0.84, 1.05},
        {"Le Technicien", 0.97, 1.24, 0.82, 1.22, 1.26, 0.88, 0.90, 1.16},
        {"Le Vétéran",    1.01, 1.05, 1.08, 1.04, 1.08, 0.86, 0.88, 1.12},
    };
    return Table[std::clamp(Livery, 0, 5)];
}

enum class StartSignal { Idle = -1, Red = 0, Orange = 1, Green = 2, Racing = 3 };

// Three-second countdown: red, then orange, then green exactly when the
// race clock reaches zero (movement, timer and AI start together).
inline StartSignal StartSignalFromClock(double Clock)
{
    if (!std::isfinite(Clock))
        return StartSignal::Idle;
    if (Clock < -2.0)
        return StartSignal::Red;
    if (Clock < 0.0)
        return StartSignal::Orange;
    if (Clock < 0.40)
        return StartSignal::Green;
    return StartSignal::Racing;
}

inline int GantryColumnsLit(StartSignal Signal)
{
    switch (Signal)
    {
    case StartSignal::Red: return 2;
    case StartSignal::Orange: return 4;
    case StartSignal::Green: return 5;
    default: return 0;
    }
}

struct StartBeep
{
    double Frequency = 0;
    double Duration = 0;
    double Volume = 0;
};

inline StartBeep BeepFor(StartSignal Signal)
{
    switch (Signal)
    {
    case StartSignal::Red: return {880.0, 0.16, 0.55};
    case StartSignal::Orange: return {1175.0, 0.20, 0.65};
    case StartSignal::Green: return {1760.0, 0.38, 0.92};
    default: return {};
    }
}

struct Wishbone
{
    double ChassisX = 0, ChassisY = 0, ChassisZ = 0;
    double HubX = 0, HubY = 0, HubZ = 0;
    bool Steers = false;
};

// Thin double wishbones in centimetres, matching the existing wheel hubs.
inline const Wishbone* CarWishbones(int& Count)
{
    static const Wishbone Bars[] = {
        {102, -40, 16, 128, -91, 22, true},
        {96, -44, 34, 128, -91, 48, true},
        {102, 40, 16, 128, 91, 22, true},
        {96, 44, 34, 128, 91, 48, true},
        {-78, -44, 18, -115, -96, 24, false},
        {-70, -48, 36, -115, -96, 52, false},
        {-78, 44, 18, -115, 96, 24, false},
        {-70, 48, 36, -115, 96, 52, false},
    };
    Count = 8;
    return Bars;
}

struct LapValidator
{
    static constexpr int CheckpointCount = 8;
    bool Hit[CheckpointCount] = {};
    int LastCheckpoint = -1;
    double LastDistance = -1;
    bool Dirty = false;
    bool Invalid = false;
    const char* Reason = "";
    int ValidLaps = 0;
    int PhysicalLaps = 1;
    bool LastCompletedInvalid = false;
    bool LastCompletedClean = true;
    const char* LastCompletedReason = "";

    void Reset()
    {
        *this = LapValidator{};
        PhysicalLaps = 1;
    }

    void MarkDirty(const char* Why)
    {
        Dirty = true;
        if (Reason[0] == '\0')
            Reason = Why;
    }

    void MarkInvalid(const char* Why)
    {
        Invalid = true;
        Reason = Why;
    }

    static double Along(double Distance, double Length)
    {
        if (!std::isfinite(Distance) || !std::isfinite(Length) || Length <= 0)
            return 0;
        return std::fmod(std::fmod(Distance, Length) + Length, Length);
    }

    void Observe(double Distance, double Lateral, double Length, double HalfWidth, bool InPit)
    {
        if (!std::isfinite(Distance) || !std::isfinite(Length) || Length < 100)
            return;
        if (LastDistance >= 0 && Distance + 2.0 < LastDistance)
            MarkInvalid("sens inverse");

        const bool Off = !InPit && std::abs(Lateral) > HalfWidth;
        const bool Cut = !InPit && std::abs(Lateral) > HalfWidth + 6.0;
        if (Cut)
            MarkInvalid("coupe de circuit");
        else if (Off)
            MarkDirty("sortie de piste");

        const double S = Along(Distance, Length);
        const double Spacing = Length / CheckpointCount;
        const int Index = std::clamp(static_cast<int>(S / Spacing), 0, CheckpointCount - 1);
        const double Center = Index * Spacing;
        double Delta = std::abs(S - Center);
        Delta = std::min(Delta, Length - Delta);
        const bool Near = Delta < std::max(20.0, Spacing * 0.40);
        const bool Admissible = InPit || std::abs(Lateral) <= HalfWidth + 8.0;
        if (Near && Admissible)
        {
            if (LastCheckpoint >= 0)
            {
                const int Step = (Index - LastCheckpoint + CheckpointCount) % CheckpointCount;
                if (Step > 1 && Step < CheckpointCount - 1)
                    MarkInvalid("passage manqué");
            }
            Hit[Index] = true;
            LastCheckpoint = Index;
        }

        const int NewPhysical = 1 + static_cast<int>(std::floor(std::max(0.0, Distance) / Length));
        if (NewPhysical > PhysicalLaps)
        {
            bool Complete = true;
            for (int I = 1; I < CheckpointCount; ++I)
                if (!Hit[I])
                    Complete = false;
            LastCompletedInvalid = Invalid || !Complete;
            LastCompletedClean = !Dirty && !LastCompletedInvalid;
            if (!Complete && !Invalid)
                LastCompletedReason = "tour incomplet";
            else if (LastCompletedInvalid)
                LastCompletedReason = Reason[0] ? Reason : "tour invalidé";
            else if (Dirty)
                LastCompletedReason = Reason[0] ? Reason : "sortie de piste";
            else
                LastCompletedReason = "";
            if (!LastCompletedInvalid)
                ++ValidLaps;
            PhysicalLaps = NewPhysical;
            for (int I = 0; I < CheckpointCount; ++I)
                Hit[I] = false;
            Hit[0] = true;
            LastCheckpoint = 0;
            Dirty = false;
            Invalid = false;
            Reason = "";
        }
        LastDistance = Distance;
    }
};
} // namespace Vroom
