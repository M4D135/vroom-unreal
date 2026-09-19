#include "VroomContactModel.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
int Failures = 0;
void Check(bool condition, const char* description)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << description << '\n';
        ++Failures;
    }
}
Vroom::ContactQuery Approach(double player, double other, double lateral = 0, int id = 0)
{
    return {id, 4.5, 4.0, lateral, lateral, player, other};
}
void Run(Vroom::ContactModel& model, double seconds, double step = 1.0 / 120)
{
    const int count = static_cast<int>(std::round(seconds / step));
    for (int i = 0; i < count; ++i)
        model.Step(step);
}
bool Near(double a, double b) { return std::abs(a - b) < 1e-8; }
}

int main()
{
    using namespace Vroom;
    ContactModel slow;
    auto query = Approach(60, 50);
    auto impact = slow.TryContact(query);
    Check(impact.Kind == ContactKind::SlowImpact, "a 36 km/h closing impact is small even at 216 km/h absolute");
    Check(Near(impact.SpeedAfterMps, 60 - 50.0 / 3.6), "small collision subtracts the original 50 km/h once");
    Check(!slow.State.ControlsLocked, "small contact leaves driving controls available");
    query.PreviousGapM = query.GapM;
    for (int i = 0; i < 500; ++i)
        Check(slow.TryContact(query).Kind == ContactKind::None, "persistent overlap does not repeat an impact");
    query.PreviousGapM = query.GapM = 9;
    Check(slow.TryContact(query).Kind == ContactKind::None, "separation is not an impact");
    Run(slow, ContactModel::PairContactCooldownSeconds + .05);
    Check(slow.TryContact(Approach(60, 50)).Kind == ContactKind::SlowImpact, "the pair rearms after separation");

    ContactModel low;
    Check(low.TryContact(Approach(8, 1)).SpeedAfterMps == 0, "small impact cannot produce negative speed");
    ContactModel fast;
    const double threshold = ContactModel::FastClosingSpeedMps;
    impact = fast.TryContact(Approach(30 + threshold, 30));
    Check(impact.Kind == ContactKind::Crash, "central contact is a crash at exactly 70 km/h relative speed");
    Check(impact.SpeedAfterMps > 0 && impact.SpeedAfterMps < 30, "big collision keeps some speed instead of a full stop");
    Check(std::abs(impact.LateralShoveMps) > 1.0, "a car hit shoves the player sideways");
    Check(fast.State.ControlsLocked && fast.State.Phase == ContactPhase::Crashed, "big crash locks driving");
    fast.PrepareRoadRecovery(6, 4.75);
    Run(fast, ContactModel::CrashDurationSeconds);
    Check(fast.State.Phase == ContactPhase::Recovering, "crash progresses to recovery");
    Check(fast.State.ControlsLocked && fast.State.TyresBroken, "serious car crash stays damaged during the return");
    Run(fast, fast.State.PhaseDuration + 0.02);
    Check(fast.State.Phase == ContactPhase::Driving && !fast.State.ControlsLocked, "recovery restores input");
    Check(fast.ConsumeRespawn() && !fast.ConsumeRespawn(), "serious car crash finishes one gradual return");
    Check(fast.State.InvulnerabilityRemaining > 1.4, "recovery grants brief collision immunity");
    Check(fast.TryContact(Approach(100, 20, 0, 8)).Kind == ContactKind::None,
          "another overlapping car cannot crash the protected recovery");

    ContactModel tyreSlow;
    const auto wheelBrush = tyreSlow.TryContact(Approach(40, 30, 1.6));
    Check(wheelBrush.Kind == ContactKind::TyreJump && wheelBrush.ControlledTyreHop,
          "a moving low-relative-speed tyre brush produces a controlled small hop");
    Check(wheelBrush.SpeedAfterMps > 38.8 && !tyreSlow.State.ControlsLocked && !tyreSlow.State.JumpWillBreakTyres,
          "small wheel hop preserves momentum and air control without damaging tyres");
    tyreSlow.Step(tyreSlow.State.PhaseDuration * .5);
    Check(tyreSlow.State.HeightM >= .10 && tyreSlow.State.HeightM <= .32
          && std::abs(tyreSlow.State.PitchRad) < .04 && std::abs(tyreSlow.State.RollRad) < .023,
          "wheel brush has a small stable cockpit arc instead of the large crash jump");
    ContactModel jump;
    impact = jump.TryContact(Approach(30 + threshold, 30, 1.6));
    Check(impact.Kind == ContactKind::TyreJump, "fast tyre contact takes the jump branch");
    Check(Near(impact.SpeedAfterMps, 30 + threshold), "tyre launch preserves translation speed initially");
    Check(!jump.State.ControlsLocked && !jump.State.JumpWillBreakTyres, "minimum fast jump keeps air steer and allows a clean landing");
    const double jumpDuration = jump.State.PhaseDuration;
    Run(jump, 0.5);
    Check(jump.State.HeightM > 0.8 && jump.State.HeightM < 3, "jump has a visible bounded metre-space arc");
    Check(jump.TryContact(Approach(100, 10, 0, 5)).Kind == ContactKind::None, "airborne car cannot repeatedly strike cars");
    Run(jump, jumpDuration + 0.1);
    Check(jump.State.Phase == ContactPhase::Driving && jump.State.HeightM == 0 && !jump.ConsumeRespawn(),
          "soft landing returns to driving in place without a forced respawn");

    ContactModel damagingJump;
    impact = damagingJump.TryContact(Approach(60, 20, -1.5));
    Check(impact.Kind == ContactKind::TyreJump && damagingJump.State.JumpWillBreakTyres,
          "large source jump impulse marks tyre damage for landing");
    Run(damagingJump, 1.6);
    Check(damagingJump.State.Phase == ContactPhase::Crashed && damagingJump.State.TyresBroken,
          "hard landing enters the broken-tyre phase");
    damagingJump.PrepareRoadRecovery(-8, 4.75);
    Run(damagingJump, 5);
    Check(damagingJump.ConsumeRespawn() && damagingJump.State.Phase == ContactPhase::Driving
          && !damagingJump.State.TyresBroken,
          "hard landing repairs and completes a gradual road return");

    ContactModel swept;
    query = {0, 12, -12, 0, 0, 100, 10};
    Check(swept.TryContact(query).Kind == ContactKind::Crash,
          "swept contact detects an entire car crossed between samples");
    ContactModel diagonalMiss;
    query = {0, 12, 0, 0, 5, 100, 10};
    Check(diagonalMiss.TryContact(query).Kind == ContactKind::None,
          "non-simultaneous longitudinal and lateral overlap is not a false hit");
    ContactModel outside;
    Check(outside.TryContact(Approach(100, 20, 2.3)).Kind == ContactKind::None, "a clear adjacent lane does not contact");
    Check(outside.TryContact(Approach(20, 30)).Kind == ContactKind::None, "a receding faster car ahead does not impact");
    query = {3, -9, -8, 0, 0, 100, 20};
    Check(outside.TryContact(query).Kind == ContactKind::None, "cars behind are not treated as front impacts");

    ContactModel sixty, oneTwenty;
    sixty.TryContact(Approach(60, 20, 1.5));
    oneTwenty.TryContact(Approach(60, 20, 1.5));
    Run(sixty, 0.6, 1.0 / 60);
    Run(oneTwenty, 0.6);
    Check(Near(sixty.State.HeightM, oneTwenty.State.HeightM) &&
          Near(sixty.State.PitchRad, oneTwenty.State.PitchRad), "jump pose depends on elapsed time, not display frequency");
    Run(sixty, 4, 1.0 / 60);
    Run(oneTwenty, 4);
    Check(sixty.State.Phase == oneTwenty.State.Phase &&
          Near(sixty.State.InvulnerabilityRemaining, oneTwenty.State.InvulnerabilityRemaining),
          "landing, repair, recovery and protection duration agree at 60 and 120 Hz");

    ContactModel invalid;
    query = Approach(50, 20);
    query.GapM = std::numeric_limits<double>::quiet_NaN();
    Check(invalid.TryContact(query).Kind == ContactKind::None, "invalid geometry is ignored");
    invalid.Step(std::numeric_limits<double>::infinity());
    invalid.Step(-1);
    Check(invalid.State.PhaseElapsed == 0, "invalid time does not change phase state");
    invalid.TryContact(Approach(80, 20));
    invalid.Step(100);
    Check(Near(invalid.State.PhaseElapsed, 0.25), "a long hitch advances at most a quarter-second");
    invalid.Reset();
    Check(!invalid.ConsumeRespawn() && invalid.State.LastKind == ContactKind::None &&
          invalid.TryContact(Approach(80, 20)).Kind == ContactKind::Crash, "reset clears animations, pending respawn and pair history");

    ContactModel scenery;
    auto wall = scenery.ForceObjectCrash(40, 1);
    Check(wall.Kind == ContactKind::Crash && scenery.State.ControlsLocked && scenery.State.TyresBroken,
          "a fast scenery strike locks the controls and damages tyres");
    scenery.PrepareRoadRecovery(14, 4.75);
    Check(scenery.ForceObjectCrash(100, -1).Kind == ContactKind::None,
          "a wreck cannot restart its crash timer on the same obstacle");
    Run(scenery, ContactModel::ObjectCrashDurationSeconds);
    Check(scenery.State.Phase == ContactPhase::Recovering && Near(scenery.State.RecoveryLateralM, 14),
          "return starts at the crash position without a lateral teleport");
    Check(scenery.State.PhaseDuration >= ContactModel::RecoveryDurationSeconds,
          "the gradual road return imposes a real time penalty");
    double previousLateral = scenery.State.RecoveryLateralM;
    scenery.Step(1.0 / 120);
    Check(std::abs(scenery.State.RecoveryLateralM - previousLateral) < 0.0001,
          "return starts with effectively zero velocity");
    for (int i = 0; i < 1000 && scenery.State.Phase == ContactPhase::Recovering; ++i)
    {
        previousLateral = scenery.State.RecoveryLateralM;
        scenery.Step(1.0 / 120);
        Check(scenery.State.RecoveryLateralM <= previousLateral + 1e-9 && scenery.State.RecoveryLateralM >= 2.1375 - 1e-9,
              "return moves monotonically inside the lane without overshoot");
        if (scenery.State.Phase == ContactPhase::Recovering)
            Check(scenery.State.ControlsLocked, "driver cannot interrupt the recovery penalty");
    }
    Check(Near(scenery.State.RecoveryLateralM, 2.1375) && Near(scenery.State.RecoveryBlend, 1),
          "return reaches a safe inside-road target");
    Check(std::abs(scenery.State.RecoveryLateralM - previousLateral) < 0.0001,
          "return reaches the lane with effectively zero velocity");
    Check(scenery.ConsumeRespawn() && !scenery.ConsumeRespawn(), "scenery return emits exactly one completion event");

    ContactModel lowObject, thresholdObject, aboveThreshold;
    lowObject.ForceObjectCrash(3, 42);
    thresholdObject.ForceObjectCrash(ContactModel::ObjectBigCrashMps, -1);
    aboveThreshold.ForceObjectCrash(ContactModel::ObjectBigCrashMps + .001, -1);
    Check(!lowObject.State.TyresBroken && lowObject.State.ImpactSide == 1
          && !thresholdObject.State.TyresBroken && aboveThreshold.State.TyresBroken,
          "object tyre damage begins strictly above the original 60 km/h threshold");

    ContactModel airObject;
    airObject.LaunchJump(50, .1);
    airObject.Step(.25);
    const double airborneHeight = airObject.State.HeightM;
    Check(airObject.ForceObjectCrash(50, -1).Kind == ContactKind::Crash,
          "airborne cars still strike solid scenery");
    Check(Near(airObject.State.HeightM, airborneHeight), "airborne impact preserves the contact height");
    airObject.Step(1.0 / 120);
    Check(airObject.State.HeightM > airborneHeight * .9,
          "airborne wreck falls gradually instead of snapping to the ground");

    ContactModel thirtyRecovery, sixtyRecovery, oneTwentyRecovery;
    for (ContactModel* model : {&thirtyRecovery, &sixtyRecovery, &oneTwentyRecovery})
    {
        model->ForceObjectCrash(40, -1);
        model->PrepareRoadRecovery(-20, 4.75);
    }
    Run(thirtyRecovery, 3, 1.0 / 30);
    Run(sixtyRecovery, 3, 1.0 / 60);
    Run(oneTwentyRecovery, 3);
    Check(Near(thirtyRecovery.State.RecoveryLateralM, oneTwentyRecovery.State.RecoveryLateralM)
          && Near(sixtyRecovery.State.RecoveryLateralM, oneTwentyRecovery.State.RecoveryLateralM),
          "road-return position agrees at 30, 60 and 120 Hz");
    Run(thirtyRecovery, 3, 1.0 / 30);
    Run(sixtyRecovery, 3, 1.0 / 60);
    Run(oneTwentyRecovery, 3);
    Check(thirtyRecovery.State.Phase == ContactPhase::Driving && sixtyRecovery.State.Phase == ContactPhase::Driving
          && oneTwentyRecovery.State.Phase == ContactPhase::Driving
          && Near(thirtyRecovery.State.InvulnerabilityRemaining, oneTwentyRecovery.State.InvulnerabilityRemaining)
          && Near(sixtyRecovery.State.InvulnerabilityRemaining, oneTwentyRecovery.State.InvulnerabilityRemaining),
          "return completion and immunity agree at 30, 60 and 120 Hz");

    double hitFraction = -1;
    int hitAxis = -1, hitSide = 0;
    Check(ContactModel::SweepObstacle({-20,0,0}, {20,0,0}, {2,1,1}, hitFraction, hitAxis, hitSide)
          && Near(hitFraction, .45) && hitAxis == 0 && hitSide == -1,
          "a fast sweep cannot tunnel through a narrow scenery obstacle");
    Check(!ContactModel::SweepObstacle({-20,0,-6}, {20,0,-6}, {2,10,1}, hitFraction, hitAxis, hitSide),
          "a car passes below a tall gantry beam without a false collision");
    Check(!ContactModel::SweepObstacle({-10,0,0}, {0,10,0}, {2,2,2}, hitFraction, hitAxis, hitSide),
          "a diagonal sweep requires simultaneous overlap on every axis");
    Check(ContactModel::SweepObstacle({0,-10,0}, {0,0,0}, {2,1,1}, hitFraction, hitAxis, hitSide)
          && hitAxis == 1 && hitSide == -1 && Near(hitFraction, .9),
          "side strikes report the actual obstacle-face normal");
    Check(!ContactModel::SweepObstacle({0,0,0}, {0,0,std::numeric_limits<double>::quiet_NaN()},
          {2,1,1}, hitFraction, hitAxis, hitSide), "nonfinite obstacle sweeps are ignored");

    ContactModel Graze;
    const auto Slow = Graze.ForceObjectGraze(10, 1);
    Check(Slow.Kind == ContactKind::SlowImpact && Graze.State.Phase == ContactPhase::Driving,
          "a slow scenery graze does not lock the car into recovery");
    Check(Slow.SpeedAfterMps > 3 && Slow.SpeedAfterMps < 10, "a graze keeps residual speed");
    for (int side : {-1, 1})
    {
        ContactModel sideGraze;
        const auto hit = sideGraze.ForceObjectGraze(5, side);
        Check(hit.Kind == ContactKind::SlowImpact && hit.ImpactSide == side
              && hit.LateralShoveMps * side < 0 && !sideGraze.State.ControlsLocked,
              "a slow obstacle graze pushes away from either wall without locking controls");
        const std::array<std::array<double, 3>, 3> identity = {{{1,0,0},{0,1,0},{0,0,1}}};
        std::array<double, 3> normal{};
        double fraction = -1;
        // Car is exactly touching the outside of an axis-aligned obstacle;
        // moving out still returns an initial SAT overlap, with an outward
        // normal. The caller uses positive motion dot normal to allow escape.
        const std::array<double, 3> from = {0, -2.0 * side, 0};
        const std::array<double, 3> away = {0, -2.1 * side, 0};
        Check(ContactModel::SweepOrientedObstacle(from, away, {3,1,1}, identity, {1,1,1}, fraction, normal)
              && Near(fraction, 0) && Near(normal[1], -side)
              && (away[1] - from[1]) * normal[1] > 0,
              "SAT reports a finite outward normal for initial separating contact on either wall");
        Check(ContactModel::SeparatingAtStart(fraction, from, away, normal),
              "moving away from a wall can clear its initial overlap");
        const std::array<double, 3> inward = {0, -1.9 * side, 0};
        Check(!ContactModel::SeparatingAtStart(fraction, from, inward, normal),
              "moving farther into an initially touching wall remains a real contact");
        Check(ContactModel::SeparatingAtStart(0, from, {1, from[1], 0}, normal)
              && !ContactModel::SeparatingAtStart(.5, from, away, normal),
              "sliding along an initial wall is allowed but a future swept hit is never discarded");
        Check(!ContactModel::SeparatingAtStart(0, from, away, {0,0,0})
              && !ContactModel::SeparatingAtStart(0, from, away, {0,std::numeric_limits<double>::quiet_NaN(),0}),
              "an invalid normal cannot make a contact disappear");
    }
    const auto Hard = Graze.ForceObjectGraze(30, -1);
    Check(Hard.Kind == ContactKind::Crash && Graze.State.Phase == ContactPhase::Crashed,
          "a fast scenery hit still becomes a crash");

    ContactModel rear, hardRear, rearWheel;
    query = {21, -4.5, -4.0, 0, 0, 30, 40};
    impact = rear.TryContact(query);
    Check(impact.Kind == ContactKind::SlowImpact && impact.Face == ContactFace::Rear
          && impact.SpeedAfterMps < 30 && impact.SpeedAfterMps > 27,
          "a pursuing car hits the rear without adding speed or inventing a frontal crash");
    query.PlayerSpeedMps = 30; query.OpponentSpeedMps = 65;
    impact = hardRear.TryContact(query);
    Check(impact.Kind == ContactKind::Crash && hardRear.State.ControlsLocked
          && impact.SpeedAfterMps <= 30 && hardRear.State.TyresBroken,
          "a severe rear impact is penalized and cannot catapult the player forward");
    query.PlayerSpeedMps = 35; query.OpponentSpeedMps = 45; query.PreviousLateralGapM = query.LateralGapM = -1.6;
    impact = rearWheel.TryContact(query);
    Check(impact.ControlledTyreHop && impact.Face == ContactFace::Rear && impact.LateralShoveMps > 0,
          "a light rear wheel contact hops and shoves away from the other car");

    auto SideApproach = [](double velocity, double step, int side = 1, int id = 22) {
        return ContactQuery{id, 0, 0, side * 1.96, side * (1.96 - velocity * step), 40, 40, step};
    };
    for (double step : {1.0/30, 1.0/60, 1.0/120})
        for (int side : {-1, 1})
        {
            ContactModel lateral;
            impact = lateral.TryContact(SideApproach(4, step, side));
            Check(impact.ControlledTyreHop && impact.Face == ContactFace::Side
                  && Near(impact.RelativeSpeedMps, 4) && impact.LateralShoveMps * side < 0,
                  "equal-speed cars still detect a lateral wheel brush at every fixed rate and side");
            Check(impact.HitFraction > 0 && impact.HitFraction < 1, "side telemetry reports the swept contact fraction");
            ContactModel violentSide;
            impact = violentSide.TryContact(SideApproach(ContactModel::SideCrashSpeedMps, step, side));
            Check(impact.Kind == ContactKind::Crash && violentSide.State.TyresBroken,
                  "a violent side swipe always crashes instead of granting a small hop");
        }
    ContactModel fastGlance;
    query = SideApproach(2, 1.0/120); query.PlayerSpeedMps = 110; query.OpponentSpeedMps = 20;
    impact = fastGlance.TryContact(query);
    Check(impact.Kind == ContactKind::TyreJump && !impact.ControlledTyreHop && fastGlance.State.JumpWillBreakTyres,
          "a huge tangential speed difference is not disguised as harmless lateral rubbing");
    ContactModel parked;
    query = {23, 0, 0, 1.6, 1.6, 0, 0};
    Check(parked.TryContact(query).Kind == ContactKind::None, "stationary overlap does not launch or lock the car");
    query = Approach(1, 0, 1.6, 24);
    Check(parked.TryContact(query).Kind == ContactKind::SlowImpact && !parked.State.ControlsLocked,
          "parking-speed wheel rub does not create pogo jumps or recovery locks");
    ContactModel escaping;
    query = {25, 0, 0, 1.7, 2.0, 40, 40};
    Check(escaping.TryContact(query).Kind == ContactKind::None, "separating lateral overlap does not hit again");

    ContactModel cooldown;
    Check(cooldown.TryContact(Approach(50, 40)).Kind == ContactKind::SlowImpact, "cooldown test starts with a contact");
    query = Approach(50, 40); query.PreviousGapM = query.GapM = 10;
    cooldown.TryContact(query);
    Check(cooldown.TryContact(Approach(50, 40)).Kind == ContactKind::None, "instant separation/recontact cannot farm a pair");
    Run(cooldown, 1);
    Check(cooldown.TryContact(Approach(50, 40)).Kind == ContactKind::None,
          "a recontact suppressed during cooldown stays latched after the timer expires");
    cooldown.TryContact(query);
    Check(cooldown.TryContact(Approach(50, 40)).Kind == ContactKind::SlowImpact,
          "real separation after cooldown permits a later independent impact");

    ContactModel hopFarm;
    hopFarm.TryContact(Approach(50, 40, 1.6, 1));
    Run(hopFarm, .6);
    impact = hopFarm.TryContact(Approach(50, 40, 1.6, 2));
    Check(impact.Kind == ContactKind::SlowImpact && !impact.ControlledTyreHop,
          "alternating opponent IDs cannot chain small hops after landing");
    Run(hopFarm, 1);
    Check(hopFarm.TryContact(Approach(50, 40, 1.6, 3)).ControlledTyreHop,
          "a fresh wheel contact after the global cooldown can hop again");
    ContactModel interruptedHop;
    interruptedHop.TryContact(Approach(50, 40, 1.6, 1));
    interruptedHop.Step(.1, 1);
    const double hopHeight = interruptedHop.State.HeightM;
    impact = interruptedHop.TryContact(Approach(80, 20, 0, 2));
    Check(impact.Kind == ContactKind::Crash && interruptedHop.State.ControlsLocked
          && Near(interruptedHop.State.HeightM, hopHeight),
          "a small hop is not invulnerability to a different severe contact and retains impact height");

    for (double relative : {15.0/3.6, 40.0/3.6, 60.0/3.6})
        for (int side : {-1, 1})
            for (double steering : {-1.0, 0.0, 1.0})
            {
                ContactLanding previousLanding;
                for (double step : {1.0/30, 1.0/60, 1.0/120})
                {
                    ContactModel skill;
                    skill.TryContact(Approach(40 + relative, 40, side * 1.6));
                    for (int frame = 0; frame < int(std::round(.6 / step)); ++frame) skill.Step(step, steering * side);
                    const auto landing = skill.ConsumeLanding();
                    Check(landing.JustLanded && !skill.ConsumeLanding().JustLanded && !skill.ConsumeRespawn(),
                          "a controlled landing emits exactly one event and no forced repositioning");
                    const double expected = steering > 0 ? 1.0 : (steering < 0 ? .94 : .97);
                    Check(Near(landing.SpeedRetention, expected) && landing.SpeedRetention <= 1,
                          "countersteer preserves momentum; wrong/no correction costs speed without any boost");
                    Check(landing.LateralVelocityRetention >= .30 - 1.e-9 && landing.LateralVelocityRetention <= .75,
                          "landing damps residual shove instead of reversing or amplifying it");
                    if (previousLanding.JustLanded)
                        Check(Near(landing.LandingControl, previousLanding.LandingControl),
                              "landing skill score agrees at 30, 60 and 120 Hz");
                    previousLanding = landing;
                }
            }
    ContactModel keyboardCorrection;
    keyboardCorrection.TryContact(Approach(50, 40, 1.6));
    for (int frame = 0; frame < 72; ++frame)
        keyboardCorrection.Step(1.0/120, frame >= 12 && frame < 26 ? 1 : 0);
    const auto corrected = keyboardCorrection.ConsumeLanding();
    Check(corrected.JustLanded && corrected.LandingControl > .7 && corrected.SpeedRetention > .98,
          "a short countersteer tap after a 100ms keyboard reaction still earns a clean recovery");
    ContactModel pausedHop;
    pausedHop.TryContact(Approach(50, 40, 1.6));
    pausedHop.Step(0, 1);
    Check(pausedHop.State.PhaseElapsed == 0 && !pausedHop.ConsumeLanding().JustLanded,
          "paused time does not advance the airborne skill window");
    pausedHop.Step(.1, std::numeric_limits<double>::quiet_NaN());
    Check(std::isfinite(pausedHop.State.LandingControl), "invalid control input cannot poison the landing result");
    pausedHop.Reset();
    Check(!pausedHop.ConsumeLanding().JustLanded, "reset clears a pending landing event");
    query = SideApproach(4, 1.0/120); query.StepSeconds = 0;
    Check(pausedHop.TryContact(query).Kind == ContactKind::None, "invalid contact duration is rejected");

    double inheritedAtPoint = -1;
    for (double step : {1.0/30, 1.0/60, 1.0/120})
    {
        ContactModel inherited, baseline;
        inherited.TryContact(Approach(50, 40, 1.6));
        baseline.TryContact(Approach(50, 40, 1.6));
        const double duration = inherited.State.PhaseDuration;
        inherited.InheritHopHeight(.3);
        Check(Near(inherited.State.HeightM, .3) && inherited.State.PhaseElapsed == 0
              && Near(inherited.State.PhaseDuration, duration) && !inherited.State.ControlsLocked,
              "a wheel hit inherits a 30 cm drift hop continuously without restarting or locking controls");
        for (int frame = 0; frame < int(std::round(.2 / step)); ++frame)
        {
            inherited.Step(step, 1); baseline.Step(step, 1);
            Check(inherited.State.HeightM >= baseline.State.HeightM
                  && inherited.State.HeightM <= .3 + .32,
                  "transferred hop height decays above the original arc without an unbounded extra launch");
        }
        const double remaining = 1 - inherited.State.PhaseElapsed / duration;
        Check(Near(inherited.State.HeightM - baseline.State.HeightM, .3 * remaining * remaining),
              "the old hop contribution fades smoothly toward the same landing");
        if (inheritedAtPoint >= 0)
            Check(Near(inherited.State.HeightM, inheritedAtPoint), "inherited hop height agrees at 30, 60 and 120 Hz");
        inheritedAtPoint = inherited.State.HeightM;
        Run(inherited, 1, step);
        Check(inherited.State.Phase == ContactPhase::Driving && Near(inherited.State.HeightM, 0)
              && inherited.ConsumeLanding().JustLanded && !inherited.ConsumeRespawn(),
              "transferred small hop lands once at zero height without a forced repositioning");

        ContactModel inheritedCrash;
        inheritedCrash.ForceObjectCrash(30, 1);
        inheritedCrash.InheritHopHeight(.3);
        Check(Near(inheritedCrash.State.HeightM, .3) && inheritedCrash.State.ControlsLocked
              && inheritedCrash.State.TyresBroken,
              "a crash inherits the visible hop height and preserves its severe penalty");
        inheritedCrash.Step(step);
        Check(inheritedCrash.State.HeightM > .25,
              "an inherited airborne wreck falls gradually instead of dropping to ground next step");
        Run(inheritedCrash, 5, step);
        Check(Near(inheritedCrash.State.HeightM, 0) && inheritedCrash.ConsumeRespawn(),
              "inherited crash height finishes on the ground through the ordinary recovery");
    }
    ContactModel interruptedTransfer;
    interruptedTransfer.TryContact(Approach(50, 40, 1.6));
    interruptedTransfer.InheritHopHeight(.3);
    interruptedTransfer.Step(.1);
    const double beforeSecondHit = interruptedTransfer.State.HeightM;
    interruptedTransfer.TryContact(Approach(80, 20, 0, 2));
    interruptedTransfer.InheritHopHeight(0);
    Check(Near(interruptedTransfer.State.HeightM, beforeSecondHit),
          "a severe second hit preserves the previously transferred hop without double-counting it");
    interruptedTransfer.InheritHopHeight(std::numeric_limits<double>::quiet_NaN());
    interruptedTransfer.InheritHopHeight(-1);
    Check(Near(interruptedTransfer.State.HeightM, beforeSecondHit), "invalid inherited heights are ignored");
    interruptedTransfer.Reset();
    interruptedTransfer.TryContact(Approach(50, 40, 1.6));
    Check(interruptedTransfer.State.HeightM == 0, "reset clears the inherited hop origin");

    if (Failures)
        return EXIT_FAILURE;
    std::cout << "All contact-model behavioral checks passed.\n"
              << "Source threshold: " << threshold * 3.6 << " km/h relative; small-contact loss: "
              << ContactModel::SlowSpeedLossMps * 3.6 << " km/h.\n";
    return EXIT_SUCCESS;
}
