#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace Vroom
{
enum class ContactKind { None, SlowImpact, Crash, TyreJump };
enum class ContactPhase { Driving, Airborne, Crashed, Recovering };

// Gaps are OTHER minus PLAYER, in the same continuous circuit-local frame.
// Unwrap longitudinal gaps across the start line before supplying both endpoints.
struct ContactQuery
{
    int OpponentId = -1;
    double PreviousGapM = 0, GapM = 0;
    double PreviousLateralGapM = 0, LateralGapM = 0;
    double PlayerSpeedMps = 0, OpponentSpeedMps = 0;
};

struct ContactResult
{
    ContactKind Kind = ContactKind::None;
    double SpeedAfterMps = 0;
    double RelativeSpeedMps = 0;
    int ImpactSide = 0; // -1 opponent left, +1 opponent right, 0 central.
    double LateralShoveMps = 0;
};

struct ContactState
{
    ContactPhase Phase = ContactPhase::Driving;
    ContactKind LastKind = ContactKind::None;
    double PhaseElapsed = 0, PhaseDuration = 0, TimeRemaining = 0;
    double HeightM = 0, PitchRad = 0, RollRad = 0;
    double ImpactFlash = 0, InvulnerabilityRemaining = 0;
    double RelativeSpeedMps = 0;
    double RecoveryLateralM = 0, RecoveryBlend = 0;
    int ImpactSide = 0;
    bool ControlsLocked = false;
    bool JumpWillBreakTyres = false;
    bool TyresBroken = false;
};

// Source facts (preserved July 1991 Atari ST snapshot, final-release parity unknown):
// vr2.asm TSCRHC6, lines 1758-1787: closing speed = player - car ahead.
// Below 105 internal units => CRASHCI, reducing PLAYER by 75 units (=50 km/h).
// At/above 105 (=70 km/h): central contact => CRASHCJ; outer D4>=40 or D4<-40
// => JUMPJ. CRASHCJ sets speed to OTHER minus 75, rpm1000 and neutral gear.
// vr2.asm 3166-3182 doubles the jump speed gap; sub6.asm 1042-1051 clamps
// this impulse to [120,400]. sub6.asm 1093-1098 marks broken tyres at >=250.
// vr2.asm JUMPA waits for a stop, then 40 cycles before clearing broken tyres.
// Car crash animation lasts >=16 cycles (CRASHCA); object crash/recentring is
// a distinct CRASHA/SHIFTA path. The car-contact routines do not establish the
// automatic centre-of-road respawn used here.
//
// Adaptations: metre-space swept AABB contact and tyre bands replace projected
// sprite coordinates; the 3D jump arc, crash/recovery timings, input lock, forced
// gradual return and brief immunity are prototype behavior. We do NOT reproduce
// CRASHCJ's twice-doubled speed gap, annotated "*** BUG ***" in the source.
// This model emits contact response; its owner controls car position and speed.
class ContactModel
{
public:
    ContactState State;

    inline static constexpr double FastClosingSpeedMps = 105.0 / 5.4;
    // vr2.asm TSCRHV15: SP1D+2 > 90 internal units = 60 km/h → CRASHJ.
    inline static constexpr double ObjectBigCrashMps = 90.0 / 5.4;
    inline static constexpr double SlowSpeedLossMps = 75.0 / 5.4;
    inline static constexpr double BrokenTyreImpulse = 250.0;
    inline static constexpr double LongitudinalReachM = 4.1;
    inline static constexpr double LateralReachM = 1.95;
    // D4 is a sprite overlap midpoint, not world-space lateral distance.
    // Mapping its ~40/66 outer portion to our rendered cars gives this tuned band.
    inline static constexpr double TyreBandInnerM = 1.15;
    inline static constexpr double CrashDurationSeconds = 0.80;
    inline static constexpr double ObjectCrashDurationSeconds = 1.05;
    inline static constexpr double BrokenTyreRepairSeconds = 1.20;
    inline static constexpr double RecoveryDurationSeconds = 2.80;
    inline static constexpr double RespawnInvulnerabilitySeconds = 1.6;
    inline static constexpr double ClearContactMarginM = 0.25;

    void Reset()
    {
        State = ContactState{};
        Latches.fill(PairLatch{});
        NextLatch = 0;
        RespawnPending = false;
        JumpPeakHeightM = 0;
        RecoverOntoRoad = false;
        AirSteer = false;
        RecoveryStartLateralM = RecoveryTargetLateralM = 0;
        RecoverySeconds = RecoveryDurationSeconds;
        CrashStartHeightM = 0;
    }

    // Call once per opponent per fixed step, including when no longer touching.
    // A pair rearms only after separation, so persistent overlaps do not subtract
    // 50 km/h every image. Airborne/crash/recovery/immunity contacts are ignored.
    ContactResult TryContact(const ContactQuery& q)
    {
        ContactResult result;
        result.SpeedAfterMps = SafeSpeed(q.PlayerSpeedMps);
        if (q.OpponentId < 0 || !FiniteQuery(q))
            return result;
        PairLatch& latch = FindLatch(q.OpponentId);
        const bool separated = std::abs(q.GapM) > LongitudinalReachM + ClearContactMarginM ||
            std::abs(q.LateralGapM) > LateralReachM + ClearContactMarginM;
        const bool wasLatched = latch.Touching;
        if (separated)
            latch.Touching = false;
        if (wasLatched)
            return result;

        double hitFraction = 0;
        if (!SweptOverlap(q, hitFraction))
            return result;
        // The original path treats the player's front overtaking a car ahead.
        // Contacts from a pursuing car behind and equal-speed parked overlaps
        // require a separate behavior, rather than reversing this source rule.
        const double closing = SafeSpeed(q.PlayerSpeedMps) - SafeSpeed(q.OpponentSpeedMps);
        if (closing <= 0.05 || q.PreviousGapM < -LongitudinalReachM)
            return result;
        latch.Touching = true;
        if (State.Phase != ContactPhase::Driving || State.InvulnerabilityRemaining > 0 || RespawnPending)
            return result;

        const double lateralAtHit = q.PreviousLateralGapM +
            (q.LateralGapM - q.PreviousLateralGapM) * hitFraction;
        result.RelativeSpeedMps = closing;
        result.ImpactSide = lateralAtHit > 0.05 ? 1 : (lateralAtHit < -0.05 ? -1 : 0);

        // Order matters: low relative speed always produces a small impact,
        // including tyre contact. The wheel branch is considered only at >=70 km/h.
        const double shoveAway = (result.ImpactSide != 0 ? -result.ImpactSide : (lateralAtHit >= 0 ? -1.0 : 1.0));
        if (closing < FastClosingSpeedMps)
        {
            result.Kind = ContactKind::SlowImpact;
            result.SpeedAfterMps = std::max(0.0, result.SpeedAfterMps - SlowSpeedLossMps);
            result.LateralShoveMps = shoveAway * (3.6 + 0.07 * closing);
        }
        else if (std::abs(lateralAtHit) >= TyreBandInnerM)
        {
            result.Kind = ContactKind::TyreJump;
            const double impulse = std::clamp(closing * 5.4 * 2.0, 120.0, 400.0);
            State.JumpWillBreakTyres = impulse >= BrokenTyreImpulse;
            State.TyresBroken = false;
            const double strength = (impulse - 120.0) / 280.0;
            JumpPeakHeightM = 0.55 + 2.25 * strength;
            result.LateralShoveMps = shoveAway * (2.0 + 4.0 * strength);
            RecoverOntoRoad = false;
            AirSteer = true;
            EnterPhase(ContactPhase::Airborne, 0.75 + 0.55 * strength);
        }
        else
        {
            result.Kind = ContactKind::Crash;
            result.SpeedAfterMps = std::max(0.0, SafeSpeed(q.OpponentSpeedMps) - SlowSpeedLossMps * 0.65);
            result.LateralShoveMps = shoveAway * (7.0 + 0.12 * closing);
            State.JumpWillBreakTyres = false;
            State.TyresBroken = true;
            RecoverOntoRoad = true;
            AirSteer = false;
            EnterPhase(ContactPhase::Crashed, CrashDurationSeconds);
        }
        State.LastKind = result.Kind;
        State.RelativeSpeedMps = result.RelativeSpeedMps;
        State.ImpactSide = result.ImpactSide;
        State.ImpactFlash = result.Kind == ContactKind::SlowImpact ? 0.22 : 0.4;
        return result;
    }

    // Seconds, normally 1/120. Phase integration uses elapsed time, not frame count.
    // The owner pauses calls when gameplay is paused. A hitch is bounded at 0.25 s.
    void Step(double dt)
    {
        if (!std::isfinite(dt) || dt <= 0)
            return;
        double remaining = std::min(dt, 0.25);
        State.ImpactFlash = std::max(0.0, State.ImpactFlash - remaining);
        // At most airborne -> crashed -> recovering -> driving in one call.
        for (int transitions = 0; remaining > 0 && transitions < 4; ++transitions)
        {
            if (State.Phase == ContactPhase::Driving)
            {
                State.InvulnerabilityRemaining = std::max(0.0, State.InvulnerabilityRemaining - remaining);
                break;
            }
            const double slice = std::min(remaining, std::max(0.0, State.PhaseDuration - State.PhaseElapsed));
            State.PhaseElapsed += slice;
            State.TimeRemaining = std::max(0.0, State.PhaseDuration - State.PhaseElapsed);
            remaining -= slice;
            State.InvulnerabilityRemaining = std::max(0.0, State.InvulnerabilityRemaining - slice);
            UpdatePose();
            if (State.TimeRemaining > 1e-10)
                break;
            if (State.Phase == ContactPhase::Airborne)
            {
                State.HeightM = State.PitchRad = State.RollRad = 0;
                if (State.JumpWillBreakTyres)
                {
                    State.TyresBroken = true;
                    RecoverOntoRoad = true;
                    EnterPhase(ContactPhase::Crashed, BrokenTyreRepairSeconds);
                }
                else
                {
                    EnterPhase(ContactPhase::Driving, 0);
                    State.JumpWillBreakTyres = false;
                    AirSteer = false;
                }
            }
            else if (State.Phase == ContactPhase::Crashed)
                EnterPhase(ContactPhase::Recovering, RecoverySeconds);
            else if (State.Phase == ContactPhase::Recovering)
            {
                EnterPhase(ContactPhase::Driving, 0);
                State.TyresBroken = State.JumpWillBreakTyres = false;
                State.HeightM = State.PitchRad = State.RollRad = 0;
                AirSteer = false;
                if (RecoverOntoRoad)
                    RespawnPending = true;
                RecoverOntoRoad = false;
                State.InvulnerabilityRemaining = RespawnInvulnerabilitySeconds;
            }
        }
    }

    // Exactly once after the gradual recovery. The owner has already moved the
    // car along RecoveryLateralM; this event restores controls without a teleport.
    bool ConsumeRespawn()
    {
        const bool result = RespawnPending;
        RespawnPending = false;
        return result;
    }

    // Prepare after an impact, then refresh when the wreck has finished sliding.
    // The displayed path is a quintic ease with zero endpoint velocity and
    // acceleration, while elapsed race time and other cars keep advancing.
    void PrepareRoadRecovery(double lateralM, double roadHalfWidthM)
    {
        if (State.Phase != ContactPhase::Crashed && State.Phase != ContactPhase::Recovering)
            return;
        RecoveryStartLateralM = std::isfinite(lateralM) ? std::clamp(lateralM, -10000.0, 10000.0) : 0;
        const double half = std::isfinite(roadHalfWidthM) ? std::clamp(roadHalfWidthM, 1.0, 100.0) : 4.75;
        RecoveryTargetLateralM = std::clamp(RecoveryStartLateralM, -half * 0.45, half * 0.45);
        RecoverySeconds = std::clamp(RecoveryDurationSeconds +
            std::abs(RecoveryStartLateralM - RecoveryTargetLateralM) * 0.09,
            RecoveryDurationSeconds, 7.0);
        State.RecoveryLateralM = RecoveryStartLateralM;
        State.RecoveryBlend = 0;
        if (State.Phase == ContactPhase::Recovering)
        {
            State.PhaseDuration = RecoverySeconds;
            State.TimeRemaining = std::max(0.0, RecoverySeconds - State.PhaseElapsed);
            UpdatePose();
        }
    }

    // Object / scenery crash (vr2.asm CRASHI/CRASHJ): lock, zero-speed owner,
    // then SHIFTA recentres onto the road. Big crash above 60 km/h.
    ContactResult ForceObjectCrash(double playerSpeedMps, int impactSide)
    {
        ContactResult result;
        result.SpeedAfterMps = 0;
        if ((State.Phase != ContactPhase::Driving && State.Phase != ContactPhase::Airborne)
            || State.InvulnerabilityRemaining > 0 || RespawnPending)
            return result;
        result.Kind = ContactKind::Crash;
        result.RelativeSpeedMps = SafeSpeed(playerSpeedMps);
        result.ImpactSide = std::clamp(impactSide, -1, 1);
        State.LastKind = ContactKind::Crash;
        State.RelativeSpeedMps = result.RelativeSpeedMps;
        State.ImpactSide = result.ImpactSide;
        State.ImpactFlash = 0.4;
        State.JumpWillBreakTyres = false;
        State.TyresBroken = SafeSpeed(playerSpeedMps) > ObjectBigCrashMps;
        RecoverOntoRoad = true;
        AirSteer = false;
        EnterPhase(ContactPhase::Crashed,
            State.TyresBroken ? ObjectCrashDurationSeconds : 0.18);
        return result;
    }

    // Sweep a point through a box already expanded by the car's projected
    // half-extents. Call in the obstacle's local frame; testing all axes over
    // the same interval prevents tunnelling and diagonal near-miss false hits.
    static bool SweepObstacle(const std::array<double, 3>& previous,
        const std::array<double, 3>& current, const std::array<double, 3>& extent,
        double& fraction, int& normalAxis, int& normalSide)
    {
        double entry = 0, exit = 1;
        normalAxis = 0;
        normalSide = previous[0] >= 0 ? 1 : -1;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(previous[axis]) || !std::isfinite(current[axis]) ||
                !std::isfinite(extent[axis]) || extent[axis] < 0)
                return false;
            const double before = entry;
            if (!Slab(previous[axis], current[axis], extent[axis], entry, exit))
                return false;
            if (entry > before)
            {
                normalAxis = axis;
                normalSide = previous[axis] + (current[axis] - previous[axis]) * entry >= 0 ? 1 : -1;
            }
        }
        fraction = entry;
        return entry <= 1 && exit >= 0;
    }

    // World-space stand-in for tstcra/tcrcoo: nose 0.64 m (FRTCRH=64 cm) plus
    // car half-width 0.95 m against a roadside sprite/pole.
    static bool SceneryOverlap(double distanceM, double lateralM, double hazardS,
        double hazardLat, double hazardRadius, double lengthM)
    {
        if (!std::isfinite(distanceM) || !std::isfinite(lateralM) || !std::isfinite(hazardS)
            || !std::isfinite(hazardLat) || !std::isfinite(hazardRadius) || lengthM <= 0)
            return false;
        double ds = std::fmod(distanceM - hazardS, lengthM);
        if (ds < 0) ds += lengthM;
        if (ds > lengthM * 0.5) ds -= lengthM;
        constexpr double NoseM = 64.0 / 100.0;
        constexpr double CarHalfM = 0.95;
        return std::abs(ds) <= NoseM + 1.1 && std::abs(lateralM - hazardLat) <= hazardRadius + CarHalfM;
    }

    // Crest / kerb launch. Air steer stays on; landing uses the jump arc.
    void LaunchJump(double speedMps, double crestDrop, int side = 0)
    {
        if (State.Phase != ContactPhase::Driving || State.InvulnerabilityRemaining > 0 || RespawnPending)
            return;
        const double v = SafeSpeed(speedMps);
        const double drop = std::isfinite(crestDrop) ? std::clamp(crestDrop, 0.0, 0.4) : 0;
        const double strength = std::clamp((v - 16.0) / 42.0 * drop / 0.10, 0.18, 1.0);
        JumpPeakHeightM = 0.38 + 2.05 * strength;
        State.JumpWillBreakTyres = strength > 0.92 && v > 55.0;
        State.TyresBroken = false;
        State.LastKind = ContactKind::TyreJump;
        State.ImpactSide = side;
        State.ImpactFlash = 0.12;
        RecoverOntoRoad = false;
        AirSteer = true;
        EnterPhase(ContactPhase::Airborne, 0.50 + 0.70 * strength);
    }

private:
    struct PairLatch { int Id = -1; bool Touching = false; };
    std::array<PairLatch, 32> Latches{};
    unsigned NextLatch = 0;
    double JumpPeakHeightM = 0;
    bool RespawnPending = false;
    bool RecoverOntoRoad = false;
    bool AirSteer = false;
    double RecoveryStartLateralM = 0, RecoveryTargetLateralM = 0;
    double RecoverySeconds = RecoveryDurationSeconds;
    double CrashStartHeightM = 0;

    static double SafeSpeed(double speed)
    {
        return std::isfinite(speed) ? std::clamp(speed, 0.0, 150.0) : 0;
    }

    static bool FiniteQuery(const ContactQuery& q)
    {
        return std::isfinite(q.PreviousGapM) && std::isfinite(q.GapM) &&
            std::isfinite(q.PreviousLateralGapM) && std::isfinite(q.LateralGapM) &&
            std::isfinite(q.PlayerSpeedMps) && std::isfinite(q.OpponentSpeedMps) &&
            std::abs(q.PreviousGapM) < 1e9 && std::abs(q.GapM) < 1e9 &&
            std::abs(q.PreviousLateralGapM) < 1e6 && std::abs(q.LateralGapM) < 1e6;
    }

    PairLatch& FindLatch(int id)
    {
        for (auto& pair : Latches)
            if (pair.Id == id)
                return pair;
        for (auto& pair : Latches)
            if (pair.Id < 0)
            {
                pair.Id = id;
                return pair;
            }
        PairLatch& replacement = Latches[NextLatch++ % Latches.size()];
        replacement = {id, false};
        return replacement;
    }

    static bool Slab(double previous, double current, double extent, double& entry, double& exit)
    {
        const double delta = current - previous;
        if (std::abs(delta) < 1e-12)
            return std::abs(previous) <= extent;
        double first = (-extent - previous) / delta;
        double last = (extent - previous) / delta;
        if (first > last)
            std::swap(first, last);
        entry = std::max(entry, first);
        exit = std::min(exit, last);
        return entry <= exit;
    }

    static bool SweptOverlap(const ContactQuery& q, double& fraction)
    {
        double entry = 0, exit = 1;
        if (!Slab(q.PreviousGapM, q.GapM, LongitudinalReachM, entry, exit) ||
            !Slab(q.PreviousLateralGapM, q.LateralGapM, LateralReachM, entry, exit))
            return false;
        fraction = entry;
        return entry <= 1 && exit >= 0;
    }

    void EnterPhase(ContactPhase phase, double duration)
    {
        if (phase == ContactPhase::Crashed)
            CrashStartHeightM = std::max(0.0, State.HeightM);
        State.Phase = phase;
        State.PhaseElapsed = 0;
        State.PhaseDuration = duration;
        State.TimeRemaining = duration;
        State.ControlsLocked = phase == ContactPhase::Crashed || phase == ContactPhase::Recovering;
    }

    void UpdatePose()
    {
        constexpr double Pi = 3.14159265358979323846;
        const double progress = State.PhaseDuration > 0 ?
            std::clamp(State.PhaseElapsed / State.PhaseDuration, 0.0, 1.0) : 1;
        if (State.Phase == ContactPhase::Airborne)
        {
            State.HeightM = 4 * JumpPeakHeightM * progress * (1 - progress);
            State.PitchRad = 0.18 * std::sin(2 * Pi * progress);
            State.RollRad = -State.ImpactSide * 0.11 * std::sin(Pi * progress);
        }
        else if (State.Phase == ContactPhase::Crashed)
        {
            const double decay = (1 - progress) * (1 - progress);
            State.HeightM = CrashStartHeightM * decay + 0.1 * std::abs(std::sin(4 * Pi * progress)) * decay;
            State.PitchRad = -0.24 * std::sin(3 * Pi * progress) * decay;
            State.RollRad = State.ImpactSide * 0.2 * std::sin(2 * Pi * progress) * decay;
        }
        else if (State.Phase == ContactPhase::Recovering)
        {
            State.RecoveryBlend = progress * progress * progress * (progress * (progress * 6 - 15) + 10);
            State.RecoveryLateralM = RecoveryStartLateralM +
                (RecoveryTargetLateralM - RecoveryStartLateralM) * State.RecoveryBlend;
            State.HeightM = State.PitchRad = State.RollRad = 0;
        }
        else
            State.HeightM = State.PitchRad = State.RollRad = 0;
    }
};
} // namespace Vroom
