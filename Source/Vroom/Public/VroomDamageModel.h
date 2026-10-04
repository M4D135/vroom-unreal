#pragma once
#include <algorithm>
#include <cmath>
#include "VroomContactModel.h"

namespace Vroom
{
enum class DamageZone { Front, Rear, Left, Right };

struct DamageState
{
    double Front = 0, Rear = 0, Left = 0, Right = 0;
    static double Bounded(double value)
    {
        return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
    }
    double Total() const
    {
        const double f = Bounded(Front), r = Bounded(Rear), l = Bounded(Left), d = Bounded(Right);
        return .55 * std::max({f, r, l, d}) + .45 * (f + r + l + d) / 4;
    }
};

// ContactModel owns contact latches: call once for each accepted impact, never
// every frame of overlap. Persistent damage remains drivable even at its cap.
class DamageModel
{
public:
    DamageState State;
    void Reset() { State = {}; }
    void Repair() { Reset(); }
    void Sanitize()
    {
        State.Front = DamageState::Bounded(State.Front);
        State.Rear = DamageState::Bounded(State.Rear);
        State.Left = DamageState::Bounded(State.Left);
        State.Right = DamageState::Bounded(State.Right);
    }
    static DamageZone Zone(ContactFace face, int side)
    {
        return face == ContactFace::Front ? DamageZone::Front : face == ContactFace::Rear
            ? DamageZone::Rear : side < 0 ? DamageZone::Left : DamageZone::Right;
    }
    static DamageZone Opposite(DamageZone zone)
    {
        return zone == DamageZone::Front ? DamageZone::Rear : zone == DamageZone::Rear
            ? DamageZone::Front : zone == DamageZone::Left ? DamageZone::Right : DamageZone::Left;
    }
    void ApplyImpact(DamageZone zone, double relativeSpeed, bool controlledHop = false)
    {
        Sanitize();
        if (!std::isfinite(relativeSpeed) || relativeSpeed <= .75) return;
        const double severity = std::clamp((relativeSpeed - .75) / 32.0, 0.0, 1.0);
        const double amount = (.025 + .395 * severity) * (controlledHop ? .20 : 1.0);
        double* target = nullptr;
        switch (zone)
        {
        case DamageZone::Front: target = &State.Front; break;
        case DamageZone::Rear: target = &State.Rear; break;
        case DamageZone::Left: target = &State.Left; break;
        case DamageZone::Right: target = &State.Right; break;
        }
        if (target) *target = std::min(1.0, *target + amount);
    }
    // Recovery supplies emergency repairs; only a completed pit service fully
    // restores the body. This preserves the crash penalty without a soft lock.
    void Recover()
    {
        Sanitize();
        State.Front = std::min(State.Front, .45);
        State.Rear = std::min(State.Rear, .45);
        State.Left = std::min(State.Left, .45);
        State.Right = std::min(State.Right, .45);
    }
    double GripModifier() const
    {
        return 1.0 - .12 * DamageState::Bounded(State.Front)
            - .06 * std::max(DamageState::Bounded(State.Left), DamageState::Bounded(State.Right));
    }
    double DragModifier() const
    {
        return 1.0 + .14 * DamageState::Bounded(State.Front) + .10 * DamageState::Bounded(State.Rear);
    }
    double SteeringBias() const
    {
        return .035 * (DamageState::Bounded(State.Right) - DamageState::Bounded(State.Left));
    }
};
}
