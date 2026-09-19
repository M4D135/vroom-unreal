#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// Portable prototype, deliberately independent of Unreal's frame rate and physics.
// Source-derived engine parameters and approximations are detailed in docs/DRIVING.md.
namespace Vroom
{
struct DriveInput
{
    double Throttle = 0, Brake = 0, Steer = 0;
    bool ShiftUp = false, ShiftDown = false;
    // Deliberate hop-drift button; braking alone never initiates a drift.
    bool Drift = false;
};

struct DriveState
{
    double SpeedMps = 0, DistanceM = 0, LateralM = 0, HeadingRad = 0;
    double Steering = 0, Rpm = 1000, Elapsed = 0;
    int Gear = 1;
};

class DriveModel
{
public:
    DriveState State;
    bool Automatic = true;
    // TIRUSE 2048..0 (vr2.asm). Fresh tyres raise the skid threshold (LIMCRI).
    double TyreHealth = 2048;
    double Fuel = 1;
    // Session-owned rates. Reset() keeps them so a restart does not restore
    // the original mild consumption by accident.
    double FuelBurnPerThrottleSecond = 0.00055;
    double TyreWearPerMetre = 0;
    // 0..1: reduced drag when following a car. Not a rubber-band catch-up.
    double Slipstream = 0;
    bool Skidding = false;
    double SkidAmount = 0;
    bool Drifting = false;
    double DriftCharge = 0;
    // Locked slide direction and current countersteer quality for HUD/feedback.
    // Both are zero when idle; the internal slide retains its blend-out pose.
    int DriftDirection = 0;
    double DriftControl = 0;
    // Each newly earned tier offers one short release window. Staying in the
    // slide still earns its normal turbo; an accurately timed release lasts longer.
    double DriftPerfectWindowRemaining = 0;
    double BoostRemaining = 0;
    int BoostTier = 0;
    bool BoostPerfect = false;
    double HopHeightM = 0;
    // Lateral impulse from bumps / barrier bounces; decays in Integrate.
    double ExtraLatVel = 0;

    void Shove(double latVel)
    {
        ExtraLatVel = FiniteClamp(ExtraLatVel + latVel, -40, 40, ExtraLatVel);
    }
    void StartHop(double peakM = 0.42, double duration = 0.26)
    {
        if (HopDuration > 0)
            return;
        HopPeakM = FiniteClamp(peakM, 0.08, 1.2, 0.42);
        HopDuration = FiniteClamp(duration, 0.08, 0.6, 0.26);
        HopElapsed = 0;
    }

    // Original motor.asm tables. Internal speed = km/h * 1.5 = m/s * 5.4.
    inline static constexpr std::array<double, 6> GearSpeedToRpm =
        {18944, 14336, 10803, 8601, 7424, 5939};
    inline static constexpr std::array<double, 6> GearInertia =
        {14280, 18870, 25041, 31451, 36440, 45550};
    inline static constexpr std::array<double, 6> AutoDownInternal =
        {0, 105, 180, 255, 315, 360};
    inline static constexpr std::array<double, 6> AutoUpInternal =
        {120, 195, 270, 345, 405, 500};
    inline static constexpr double MaxRpm = 13312.0;
    // Arcade calibration, not ST constants. A quicker return/countersteer
    // keeps the wheel from sticking at full lock, while still reducing
    // high-frequency churn in digital + analog transitions.
    // Keyboard stand-in for the original mouse (immediate) / joystick
    // (24/270 per VBL ≈ 225 ms to lock). Keep it snappier than the stick
    // so a digital key still feels like throwing the Amiga wheel.
    inline static constexpr double SteeringResponseSeconds = 0.045;
    inline static constexpr double SteeringReturnSeconds = 0.025;
    // Low-angle exponent makes the wheel feel more progressive around center.
    inline static constexpr double SteeringInputShape = 0.92;
    // Smoothly slows large steering corrections at high speed to avoid hard snaps.
    inline static constexpr double SteeringSpeedResponseScale = 0.018;
    // Slightly stronger steering authority than pure XBCMP3 geometry to make
    // initiation and angle transitions feel plus fun at arcade pace.
    inline static constexpr double SteeringLatGain = 1.12;

    // Arcade/parallel (MGAME+2==0) keeps haldco at 0: this is leftover slip
    // for crashes and the wrecked-car coast, not the driving yaw.
    double VelocityHeading() const { return VelocityHeadingRad; }
    void SetVelocityHeading(double rad) { VelocityHeadingRad = FiniteClamp(rad, -0.8, 0.8, 0); }
    // Drawn yaw on the road tangent. Arcade clears haldco every frame so the
    // chassis stays parallel; only a small steering lean remains (cockpit).
    double VisualHeading() const
    {
        return FiniteClamp(ChassisSlip, -DriftMaxSkidAngle, DriftMaxSkidAngle, 0);
    }

    // XBCMP3 at PAL VBL (50 Hz): volant ±271, XB 256 units/m (clamp ±1536 = ±6 m).
    inline static constexpr double ArcadeVblHz = 50.0;
    inline static constexpr double ArcadeVolantMax = 271.0;
    inline static constexpr double ArcadeXbPerMetre = 256.0;
    inline static constexpr std::array<double, 8> CriVirInternal =
        {270, 300, 330, 375, 420, 490, 600, 700};

    static int ArcadeVaco(double curvature)
    {
        const double ak = std::abs(curvature);
        if (ak < 0.0008)
            return -1;
        const double v = 7.0 - std::log2(ak / 0.00125);
        return static_cast<int>(std::clamp(std::round(v), 0.0, 7.0));
    }

    static double ArcadeSteerLatMps(double steer, double speedMps)
    {
        const double internalSpeed = std::max(0.0, speedMps) * 5.4;
        const double xbPerSec = steer * ArcadeVolantMax * internalSpeed / 2048.0 * ArcadeVblHz;
        return xbPerSec / ArcadeXbPerMetre * SteeringLatGain;
    }

    // Discrete vaco events in the original are much sharper than the HUD
    // spline's K. Using (7-vaco)*v/64 on that spline barely moved XB, so the
    // car sat on the tangent ("trop guidé"). Scale wash so a typical Vroom
    // bend needs ~80 % lock — you throw the wheel or you go wide.
    inline static constexpr double ArcadeWashLengthM = 11.2;

    // Optional arcade drift layered over the original lateral driving model.
    inline static constexpr double DriftEntrySpeedMps = 12.2;
    inline static constexpr double DriftEntryBrake = 0.34;
    inline static constexpr double DriftEntrySteer = 0.22;
    inline static constexpr double DriftChargeBoost01 = 0.42;
    inline static constexpr double DriftChargeBoost02 = 1.00;
    inline static constexpr double DriftChargeBoost03 = 1.80;
    inline static constexpr double DriftMaxCharge = 2.10;
    inline static constexpr double DriftPerfectReleaseWindowSeconds = 0.22;
    inline static constexpr double DriftPerfectBonusSeconds = 0.20;
    inline static constexpr double BoostBlendInSeconds = 0.10;
    inline static constexpr double BoostBlendOutSeconds = 0.16;
    inline static constexpr double DriftLatGain = 0.060;
    inline static constexpr double DriftSlipGain = 0.28;
    inline static constexpr double DriftMaxSkidAngle = 0.52;
    inline static constexpr double DriftBlendInSeconds = 0.10;
    inline static constexpr double DriftBlendOutSeconds = 0.16;
    inline static constexpr double DriftSettleSeconds = 0.24;
    inline static constexpr double DriftControlGraceSeconds = 0.38;
    inline static constexpr double DriftFullCounterLimitSeconds = 0.90;
    inline static constexpr double DriftRearmSeconds = 0.24;
    // Chassis yaw has inertia independently of the immediate lateral controls.
    // Keep room for the road tangent's own angular acceleration in a bend.
    inline static constexpr double MaxChassisSlipAcceleration = 160.0 * 3.14159265358979323846 / 180.0;
    inline static constexpr double ChassisSlipFrequency = 18.0;

    int DriftChargeTier() const
    {
        return DriftCharge >= DriftChargeBoost03 ? 3 : DriftCharge >= DriftChargeBoost02 ? 2
            : DriftCharge >= DriftChargeBoost01 ? 1 : 0;
    }

    static double BoostDurationSeconds(int tier, bool perfect = false)
    {
        const double duration = tier >= 3 ? 1.70 : tier == 2 ? 1.15 : tier == 1 ? 0.55 : 0;
        return duration > 0 ? duration + (perfect ? DriftPerfectBonusSeconds : 0) : 0;
    }

    double BoostDurationSeconds() const { return BoostDurationSeconds(BoostTier, BoostPerfect); }

    // Curve loading and grass steering loss build with speed. Applying the
    // full arcade wash at walking pace made wash/steer almost speed-independent:
    // a stopped car could skid, stall against grass friction and never turn in.
    // Reach the existing handling exactly at 144 km/h, with a smooth endpoint.
    inline static constexpr double FullCornerLoadSpeedMps = 40.0;
    static double CornerLoad(double speedMps)
    {
        const double ratio = std::clamp(speedMps / FullCornerLoadSpeedMps, 0.0, 1.0);
        const double load = ratio * ratio;
        return load * load * (3.0 - 2.0 * load);
    }

    static double ArcadeWashLatMps(double speedMps, double curvature)
    {
        // Fade out very small spline curvature without stepping the lateral
        // velocity when a bend crosses the former 0.0008 cutoff.
        const double t = std::clamp((std::abs(curvature) - 0.0004) / 0.0004, 0.0, 1.0);
        return speedMps * curvature * ArcadeWashLengthM * t * t * (3.0 - 2.0 * t) * CornerLoad(speedMps);
    }

    void ChangeTyres() { TyreHealth = 2048; }
    void Refuel() { Fuel = 1; }
    double TyreCondition() const { return std::clamp(TyreHealth / 2048.0, 0.0, 1.0); }
    double TyreGrip() const { return .65 + .35 * TyreCondition(); }

    void CancelArcadeEffects(bool CancelHop = true)
    {
        Drifting = false;
        DriftCharge = DriftBlend = DriftTractionControl = 0;
        DriftDirection = 0;
        DriftSlideDirection = 0;
        DriftControl = DriftAge = DriftLostControl = DriftFullCounter = DriftReleaseAge = DriftArmAge = 0;
        DriftPerfectWindowRemaining = 0;
        DriftHighestTier = 0;
        DriftNeedsRelease = true;
        BoostRemaining = BoostElapsed = 0;
        BoostTier = 0;
        BoostPerfect = false;
        if (CancelHop)
            HopHeightM = HopElapsed = HopDuration = HopPeakM = 0;
        ChassisSlipRate = 0;
        Skidding = false;
        SkidAmount = 0;
    }

    void Reset()
    {
        State = DriveState{};
        ShiftDelay = 0;
        ChassisSlip = 0;
        ChassisSlipRate = 0;
        VelocityHeadingRad = 0;
        TyreHealth = 2048;
        Fuel = 1;
        Skidding = false;
        SkidAmount = 0;
        Drifting = false;
        DriftCharge = 0;
        DriftDirection = 0;
        DriftSlideDirection = 0;
        DriftControl = DriftAge = DriftLostControl = DriftFullCounter = DriftArmAge = 0;
        DriftPerfectWindowRemaining = 0;
        DriftHighestTier = 0;
        DriftReleaseAge = DriftRearmSeconds;
        DriftBlend = 0;
        DriftTractionControl = 0;
        DriftNeedsRelease = false;
        BoostRemaining = BoostElapsed = 0;
        BoostTier = 0;
        BoostPerfect = false;
        HopHeightM = 0;
        ExtraLatVel = 0;
        HopElapsed = 0;
        HopDuration = 0;
        HopPeakM = 0;
        PreviousShiftUp = PreviousShiftDown = false;
    }

    // dt is seconds; curvature is radians/metre; roadHalfWidth is metres.
    // Inputs clamp to [0,1]/[-1,1]. Simultaneous brake and throttle prioritizes brake.
    // Gear buttons are rising-edge triggered in manual mode. Reset keeps Automatic.
    // airborne: crest / tyre jump — keep air steer, drop road wash and FROTT.
    void Step(const DriveInput& input, double dt, double curvature, double roadHalfWidth, double grade = 0,
        bool airborne = false)
    {
        if (!std::isfinite(dt) || dt <= 0)
            return;
        SanitizeState();
        DriveInput clean = input;
        clean.Throttle = FiniteClamp(input.Throttle, 0, 1, 0);
        clean.Brake = FiniteClamp(input.Brake, 0, 1, 0);
        clean.Steer = FiniteClamp(input.Steer, -1, 1, 0);
        const double steerMag = std::abs(clean.Steer);
        if (steerMag > 0.0001)
            clean.Steer = std::copysign(std::pow(steerMag, SteeringInputShape), clean.Steer);
        if (clean.Brake > 0)
            clean.Throttle = 0;
        Fuel = FiniteClamp(Fuel, 0, 1, 1);
        TyreHealth = FiniteClamp(TyreHealth, 0, 2048, 2048);
        if (Fuel <= 0)
            clean.Throttle = 0;
        curvature = FiniteClamp(curvature, -0.2, 0.2, 0);
        roadHalfWidth = FiniteClamp(roadHalfWidth, 1, 100, 6);
        grade = FiniteClamp(grade, -0.5, 0.5, 0);

        if (!Automatic)
        {
            const bool up = clean.ShiftUp && !PreviousShiftUp;
            const bool down = clean.ShiftDown && !PreviousShiftDown;
            if (up != down)
                ChangeGear(State.Gear + (up ? 1 : -1));
        }
        PreviousShiftUp = clean.ShiftUp;
        PreviousShiftDown = clean.ShiftDown;

        // A pause/hitch cannot advance the model by more than a quarter second.
        // The normal owner calls at 1/120 s; larger valid steps use the same substep.
        const double elapsed = std::min(dt, 0.25);
        const int count = std::max(1, static_cast<int>(std::ceil(elapsed * 120.0)));
        const double h = elapsed / count;
        for (int i = 0; i < count; ++i)
            Integrate(clean, h, curvature, roadHalfWidth, grade, airborne);
    }

private:
    // Every 32nd sample of the original 16-rpm MOMTBL (0..13312 rpm).
    inline static constexpr std::array<double, 27> TorqueSamples =
        {0, 48, 94, 138, 180, 220, 257, 291, 324, 354, 382, 407, 430, 451,
         470, 486, 500, 511, 520, 527, 532, 534, 530, 520, 507, 492, 475};
    double ShiftDelay = 0;
    bool PreviousShiftUp = false, PreviousShiftDown = false;
    double VelocityHeadingRad = 0;
    double ChassisSlip = 0;
    double ChassisSlipRate = 0;
    double HopElapsed = 0;
    double HopDuration = 0;
    double HopPeakM = 0;
    double DriftBlend = 0;
    double DriftTractionControl = 0;
    bool DriftNeedsRelease = false;
    double DriftAge = 0, DriftLostControl = 0, DriftFullCounter = 0;
    double DriftArmAge = 0;
    int DriftSlideDirection = 0;
    int DriftHighestTier = 0;
    double DriftReleaseAge = DriftRearmSeconds;
    double BoostElapsed = 0;

    static double FiniteClamp(double value, double low, double high, double fallback)
    {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    }

    static double TorqueAt(double rpm)
    {
        const double entry = std::clamp(rpm, 0.0, MaxRpm) / 512.0;
        const int index = std::min(25, static_cast<int>(entry));
        const double fraction = entry - index;
        return TorqueSamples[index] * (1.0 - fraction) + TorqueSamples[index + 1] * fraction;
    }

    void SanitizeState()
    {
        State.SpeedMps = FiniteClamp(State.SpeedMps, 0, 120, 0);
        State.DistanceM = FiniteClamp(State.DistanceM, 0, 1e12, 0);
        State.LateralM = FiniteClamp(State.LateralM, -10000, 10000, 0);
        State.HeadingRad = FiniteClamp(State.HeadingRad, -1.2, 1.2, 0);
        State.Steering = FiniteClamp(State.Steering, -1, 1, 0);
        State.Rpm = FiniteClamp(State.Rpm, 1000, MaxRpm, 1000);
        State.Elapsed = FiniteClamp(State.Elapsed, 0, 1e12, 0);
        State.Gear = std::clamp(State.Gear, 1, 6);
        VelocityHeadingRad = FiniteClamp(VelocityHeadingRad, -0.8, 0.8, 0);
        // Use the same bound as the integrator and renderer. A tighter bound
        // here reset the yaw on every owner tick and made drift depend on fps.
        ChassisSlip = FiniteClamp(ChassisSlip, -DriftMaxSkidAngle, DriftMaxSkidAngle, 0);
        ChassisSlipRate = FiniteClamp(ChassisSlipRate, -3.0, 3.0, 0);
        ExtraLatVel = FiniteClamp(ExtraLatVel, -40, 40, 0);
        DriftCharge = FiniteClamp(DriftCharge, 0, DriftMaxCharge, 0);
        DriftControl = FiniteClamp(DriftControl, 0, 1, 0);
        DriftDirection = std::clamp(DriftDirection, -1, 1);
        DriftPerfectWindowRemaining = FiniteClamp(DriftPerfectWindowRemaining, 0, DriftPerfectReleaseWindowSeconds, 0);
        BoostRemaining = FiniteClamp(BoostRemaining, 0, 3, 0);
        BoostTier = std::clamp(BoostTier, 0, 3);
        HopHeightM = FiniteClamp(HopHeightM, 0, 1.5, 0);
    }

    void ChangeGear(int gear)
    {
        gear = std::clamp(gear, 1, 6);
        if (State.Gear != gear)
        {
            State.Gear = gear;
            ShiftDelay = 0.10;
        }
    }

    void Integrate(const DriveInput& input, double h, double curvature, double roadHalfWidth, double grade,
        bool airborne)
    {
        const double internalSpeed = State.SpeedMps * 5.4;
        if (Automatic && ShiftDelay <= 0)
        {
            const int index = State.Gear - 1;
            if (internalSpeed > AutoUpInternal[index] && State.Gear < 6)
                ChangeGear(State.Gear + 1);
            else if (internalSpeed < AutoDownInternal[index] && State.Gear > 1)
                ChangeGear(State.Gear - 1);
        }
        const int gear = State.Gear - 1;
        const double wheelRpm = internalSpeed * GearSpeedToRpm[gear] / 256.0;
        // Simple automatic clutch for a usable standing start; not an original port.
        const double launchRpm = 1000.0 + input.Throttle * 7500.0;
        const double targetRpm = std::clamp(std::max(wheelRpm, launchRpm), 1000.0, MaxRpm);
        State.Rpm += (targetRpm - State.Rpm) * (1.0 - std::exp(-h / 0.09));

        const bool HasInput = std::abs(input.Steer) > 0.001;
        const bool returning = (HasInput && input.Steer * State.Steering < 0) ||
            (!HasInput && std::abs(State.Steering) > 0.001);
        const double speedScaled = 1.0 + std::clamp(State.SpeedMps * SteeringSpeedResponseScale, 0.0, 0.60);
        const double responseTime = returning ? SteeringReturnSeconds : SteeringResponseSeconds * speedScaled;
        State.Steering += (input.Steer - State.Steering) * (1.0 - std::exp(-h / responseTime));
        const double speed = State.SpeedMps;
        const double grip = TyreGrip();

        // Lock the initial slide, then reward balancing it with countersteer.
        // A short neutral crossing is allowed; holding either full lock forever
        // cannot farm charge. Keyboard pulses have time to cross that window.
        const double edgeBlend = std::clamp((std::abs(State.LateralM) + 0.8 - roadHalfWidth) / 1.6, 0.0, 1.0);
        const bool driftCommand = input.Drift;
        const bool driftRoadValid = std::abs(State.LateralM) <= roadHalfWidth - 0.25 && !airborne;
        const bool driftSurfaceValid = speed >= DriftEntrySpeedMps && driftRoadValid && Fuel > 0 && input.Brake == 0;
        if (!driftCommand)
        {
            DriftNeedsRelease = false;
            DriftArmAge = 0;
            DriftReleaseAge = std::min(DriftRearmSeconds, DriftReleaseAge + h);
        }
        if (Drifting)
        {
            DriftAge += h;
            DriftPerfectWindowRemaining = std::max(0.0, DriftPerfectWindowRemaining - h);
            const double counter = -State.Steering * DriftDirection;
            // Moderate opposite lock gives full control. Full digital lock is
            // useful briefly, but must be relaxed before its .90 s limit.
            const double target = std::clamp(.48 - std::abs(curvature) * 10.0, .28, .48);
            DriftControl = counter <= .12 ? 0.0 : std::clamp((counter - .12) / (target - .12), 0.0, 1.0)
                * (1.0 - .32 * std::clamp((counter - .75) / .25, 0.0, 1.0));
            if (DriftAge > DriftSettleSeconds)
            {
                DriftLostControl = DriftControl >= .30 ? std::max(0.0, DriftLostControl - 2.0 * h)
                    : DriftLostControl + h;
                DriftFullCounter = counter > .90 ? DriftFullCounter + h : std::max(0.0, DriftFullCounter - 3.0 * h);
                if (DriftControl >= .30 && driftSurfaceValid)
                    DriftCharge = std::min(DriftMaxCharge, DriftCharge + h * DriftControl);
                else
                    DriftCharge = std::max(0.0, DriftCharge - .18 * h);
                if (DriftChargeTier() > DriftHighestTier)
                {
                    DriftHighestTier = DriftChargeTier();
                    DriftPerfectWindowRemaining = DriftPerfectReleaseWindowSeconds;
                }
            }
            const bool controlled = DriftLostControl < DriftControlGraceSeconds &&
                DriftFullCounter < DriftFullCounterLimitSeconds;
            if (!driftCommand || !driftSurfaceValid || !controlled)
            {
                // Centering while releasing is fine: recent control survives
                // the wheel's neutral crossing. A long uncontrolled coast is not.
                if (!driftCommand && driftSurfaceValid && controlled && DriftLostControl < .18 &&
                    DriftCharge >= DriftChargeBoost01)
                {
                    BoostTier = DriftChargeTier();
                    BoostPerfect = DriftPerfectWindowRemaining > 0 && BoostTier == DriftHighestTier;
                    BoostRemaining = BoostDurationSeconds(BoostTier, BoostPerfect);
                    BoostElapsed = 0;
                }
                else
                {
                    BoostTier = 0;
                    BoostRemaining = BoostElapsed = 0;
                    BoostPerfect = false;
                }
                Drifting = false;
                DriftDirection = 0;
                DriftCharge = DriftControl = 0;
                DriftPerfectWindowRemaining = 0;
                DriftHighestTier = 0;
                DriftNeedsRelease = driftCommand;
                DriftReleaseAge = 0;
            }
        }
        else if (driftCommand && !DriftNeedsRelease)
        {
            DriftArmAge += h;
            if (std::abs(input.Steer) >= DriftEntrySteer && driftSurfaceValid &&
                DriftReleaseAge >= DriftRearmSeconds && BoostRemaining <= 0)
            {
                StartHop(0.42, 0.26);
                Drifting = true;
                DriftDirection = DriftSlideDirection = input.Steer > 0 ? 1 : -1;
                DriftCharge = DriftControl = DriftAge = DriftLostControl = DriftFullCounter = 0;
                DriftPerfectWindowRemaining = 0;
                DriftHighestTier = 0;
                DriftReleaseAge = 0;
            }
            else if (DriftArmAge >= .16 || !driftSurfaceValid ||
                DriftReleaseAge < DriftRearmSeconds || BoostRemaining > 0)
            {
                DriftNeedsRelease = true;
                DriftReleaseAge = 0;
            }
        }
        const double blendSeconds = Drifting ? DriftBlendInSeconds : DriftBlendOutSeconds;
        DriftBlend += ((Drifting ? 1.0 : 0.0) - DriftBlend) * (1.0 - std::exp(-h / blendSeconds));
        DriftTractionControl += ((Drifting ? DriftControl : 0.0) - DriftTractionControl)
            * (1.0 - std::exp(-h / 0.09));
        if (HopDuration > 0)
        {
            HopElapsed += h;
            const double t = std::clamp(HopElapsed / HopDuration, 0.0, 1.0);
            HopHeightM = 4.0 * HopPeakM * t * (1.0 - t);
            if (HopElapsed >= HopDuration)
            {
                HopDuration = 0;
                HopHeightM = 0;
            }
        }

        Skidding = false;
        SkidAmount = 0;
        const double accelUnits = input.Brake > 0 ? -94.0 * input.Brake : 94.0 * input.Throttle;
        const bool LockedBrakes = accelUnits * internalSpeed <= -10000.0 && input.Brake >= 0.85 && !Drifting;
        if (LockedBrakes)
        {
            Skidding = true;
            SkidAmount = 0.85;
        }
        Fuel = std::max(0.0, Fuel - input.Throttle * h * std::max(0.0, FuelBurnPerThrottleSecond));

        const int vaco = ArcadeVaco(curvature);
        double wash = ArcadeWashLatMps(speed, curvature);
        if (airborne)
            wash *= 0.22;
        // Countersteer recovers enough lateral grip to hold a long slide on
        // the original narrow road. Neutral / poor control keeps the wider arc;
        // this changes tyre force, never lane position or camera orientation.
        wash *= 1.0 - (0.58 + 0.20 * DriftTractionControl) * DriftBlend;
        const double fullSteerLat = std::max(std::abs(ArcadeSteerLatMps(1.0, speed)), 0.35);
        const double demanded = std::abs(wash) / fullSteerLat;

        if (!airborne && TyreWearPerMetre > 0)
        {
            const double Corner = 1.0 + 18.0 * std::abs(curvature);
            TyreHealth = std::max(0.0, TyreHealth - TyreWearPerMetre * speed * h * Corner);
        }

        if (!airborne && ((vaco >= 0 && internalSpeed >= CriVirInternal[vaco] * grip) || demanded > 0.48 * grip))
        {
            Skidding = true;
            SkidAmount = std::max(SkidAmount, std::clamp((demanded - 0.35) / 0.55, 0.35, 1.0));
            TyreHealth = std::max(0.0, TyreHealth - 12.0 * h);
        }
        if (DriftBlend > 0.05 && !airborne)
        {
            Skidding = true;
            SkidAmount = std::max(SkidAmount, DriftBlend * std::clamp(0.42 + DriftCharge * 0.4, 0.42, 1.0));
            TyreHealth = std::max(0.0, TyreHealth - (5.0 + 5.0 * (1.0 - DriftControl)) * DriftBlend * h);
        }

        double rollingMoment = airborne ? 4.0 : 10.0 + 198.0 * edgeBlend;
        if (Skidding && !airborne)
            rollingMoment += 320.0 - 180.0 * DriftBlend;
        const double airMoment = internalSpeed * internalSpeed / (airborne ? 780.0 : 468.0)
            * (1.0 - 0.30 * std::clamp(Slipstream, 0.0, 1.0));
        const double brakePedal = input.Brake * (1.0 - 0.78 * DriftBlend);
        const double brakeMoment = std::pow(94.0 * brakePedal, 2) / 16.0 * grip;
        const double torque = (ShiftDelay > 0 || wheelRpm >= MaxRpm) ? 0.0 : TorqueAt(State.Rpm) * input.Throttle;
        const double netMoment = torque - brakeMoment - airMoment - 10.0 - rollingMoment;
        double acceleration = (256.0 * 20.0 / (GearInertia[gear] * 5.4)) * netMoment;
        if (input.Brake > 0 || !driftRoadValid || Fuel <= 0)
        {
            BoostRemaining = BoostElapsed = 0;
            BoostTier = 0;
            BoostPerfect = false;
        }
        if (input.Throttle == 0 && State.SpeedMps > 0 && BoostRemaining <= 0)
            acceleration -= 1.2;
        if (!airborne)
            acceleration -= 9.81 * grade / std::sqrt(1 + grade * grade);
        if (BoostRemaining > 0)
        {
            // Power arrives and expires progressively, so the stronger third
            // tier adds speed without an acceleration step at either endpoint.
            BoostElapsed += h;
            const auto smooth = [](double x) { x = std::clamp(x, 0.0, 1.0); return x * x * (3.0 - 2.0 * x); };
            const double envelope = std::min(smooth(BoostElapsed / BoostBlendInSeconds),
                smooth(BoostRemaining / BoostBlendOutSeconds));
            acceleration += (BoostTier >= 3 ? 26.0 : BoostTier == 2 ? 22.0 : 14.0) * envelope;
            BoostRemaining = std::max(0.0, BoostRemaining - h);
            if (BoostRemaining <= 0)
            {
                BoostTier = 0;
                BoostPerfect = false;
                BoostElapsed = 0;
            }
        }
        State.SpeedMps = std::clamp(State.SpeedMps + acceleration * h, 0.0, 120.0);

        ExtraLatVel *= std::exp(-h / 0.16);
        double steerLat = ArcadeSteerLatMps(State.Steering, State.SpeedMps) * grip;
        if (edgeBlend > 0 && !airborne)
            steerLat *= 1.0 - 0.5 * edgeBlend * CornerLoad(speed);
        if (airborne)
            steerLat *= 0.88;
        double latVel = steerLat * (1.0 - .70 * DriftBlend) - wash + ExtraLatVel;
        latVel += DriftBlend * DriftSlideDirection * std::min(State.SpeedMps, 54.0) * DriftLatGain
            * (1.0 - 0.25 * DriftTractionControl);
        const double metric = std::clamp(1.0 - curvature * State.LateralM, 0.3, 3.0);
        const double progress = State.SpeedMps / metric;
        VelocityHeadingRad = 0;
        State.LateralM = std::clamp(State.LateralM + latVel * h, -10000.0, 10000.0);
        State.DistanceM = std::min(1e12, State.DistanceM + progress * h);
        const double sideslip = std::atan2(latVel, std::max(State.SpeedMps, 1.0));
        double slipTarget = sideslip * 0.55
            + State.Steering * 0.05 * std::clamp(State.SpeedMps / (State.SpeedMps + 14.0), 0.0, 1.0);
        slipTarget = std::clamp(slipTarget + DriftBlend * DriftSlideDirection * DriftSlipGain,
            -DriftMaxSkidAngle, DriftMaxSkidAngle);
        // A first-order follower changes angular velocity instantly when a
        // brake tap starts/stops a drift. Preserve angular velocity instead.
        // Near the target this is a critically damped second-order follower;
        // for larger changes its acceleration and braking distance are bounded.
        // The 25% braking margin accommodates discrete steps and target changes
        // without clipping the final angle (which would introduce another snap).
        const double slipError = slipTarget - ChassisSlip;
        const double stoppingRate = 0.75 * std::sqrt(2.0 * MaxChassisSlipAcceleration * std::abs(slipError));
        const double desiredSlipRate = std::copysign(std::min(
            0.5 * ChassisSlipFrequency * std::abs(slipError), stoppingRate), slipError);
        const double slipAcceleration = std::clamp(
            2.0 * ChassisSlipFrequency * (desiredSlipRate - ChassisSlipRate),
            -MaxChassisSlipAcceleration, MaxChassisSlipAcceleration);
        ChassisSlip += ChassisSlipRate * h + 0.5 * slipAcceleration * h * h;
        ChassisSlipRate += slipAcceleration * h;
        State.HeadingRad = ChassisSlip;
        State.Elapsed = std::min(1e12, State.Elapsed + h);
        ShiftDelay = std::max(0.0, ShiftDelay - h);
    }
};
} // namespace Vroom
