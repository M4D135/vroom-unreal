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
    Check(tyreSlow.TryContact(Approach(40, 30, 1.6)).Kind == ContactKind::SlowImpact,
          "a low-speed tyre touch remains small rather than jumping");
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

    if (Failures)
        return EXIT_FAILURE;
    std::cout << "All contact-model behavioral checks passed.\n"
              << "Source threshold: " << threshold * 3.6 << " km/h relative; small-contact loss: "
              << ContactModel::SlowSpeedLossMps * 3.6 << " km/h.\n";
    return EXIT_SUCCESS;
}
