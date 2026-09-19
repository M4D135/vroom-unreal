#include "VroomRaceRules.h"
#include "VroomDriveModel.h"
#include "VroomTrafficModel.h"
#include "VroomSession.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
using namespace Vroom;

int Failures = 0;
void Check(bool Ok, const char* Text)
{
    if (!Ok)
    {
        std::cerr << "FAIL: " << Text << '\n';
        ++Failures;
    }
}

int main()
{
    Check(StartSignalFromClock(-2.5) == StartSignal::Red, "countdown starts on red");
    Check(StartSignalFromClock(-1.2) == StartSignal::Orange, "middle of countdown is orange");
    Check(StartSignalFromClock(0.0) == StartSignal::Green, "green is exactly at race start");
    Check(StartSignalFromClock(0.2) == StartSignal::Green, "green covers the first instants");
    Check(StartSignalFromClock(1.0) == StartSignal::Racing, "green does not linger");
    Check(BeepFor(StartSignal::Green).Frequency > BeepFor(StartSignal::Red).Frequency,
          "green beep is distinct from the preparation tones");
    Check(GantryColumnsLit(StartSignal::Red) == 2 && GantryColumnsLit(StartSignal::Racing) == 0,
          "gantry columns follow the three-colour sequence then go out");

    const ResourceBudget Solo = MakeResourceBudget(false, RaceDifficulty::Normal, 3813, 3);
    Check(Solo.FuelPerThrottleSecond < 0.001, "solo sessions keep mild fuel use");
    const double Lengths[] = {3813, 4018, 4359, 4778, 5911, 6405};
    const char* Names[] = {"France", "Suède", "Japon", "Royaume-Uni", "Autriche", "USA"};
    for (int Track = 0; Track < 6; ++Track)
    {
        for (int Laps : {2, 3, 6}) for (int Level = 0; Level < 3; ++Level)
        {
            const auto Diff = static_cast<RaceDifficulty>(Level);
            const ResourceBudget B = MakeResourceBudget(true, Diff, Lengths[Track], Laps);
            DriveModel Car;
            Car.FuelBurnPerThrottleSecond = B.FuelPerThrottleSecond;
            Car.TyreWearPerMetre = B.TyreWearPerMetre;
            DriveInput Hold; Hold.Throttle = .78;
            for (int Step = 0; Step < 120 * 900 && Car.State.DistanceM < Lengths[Track] * Laps; ++Step)
            {
                Car.Step(Hold, 1.0 / 120, 0.0, 4.75);
                if (Car.Fuel == 0 && Car.State.SpeedMps < .2) break;
            }
            Check(Car.State.DistanceM < Lengths[Track] * Laps, "normal race throttle cannot finish the race without fuel");
            const double FuelLaps = 60.0 / (B.FuelPerThrottleSecond * .78 * Lengths[Track]);
            const double TyreLaps = 2048.0 / (B.TyreWearPerMetre * Lengths[Track]);
            Check(FuelLaps > 1 && FuelLaps < Laps, "the next pit entry is reachable before the tank runs dry");
            Check(TyreLaps > FuelLaps && TyreLaps < Laps, "tyres need service during the race too");
            std::cout << Names[Track] << " laps=" << Laps << " difficulty=" << Level << " fuelLaps=" << FuelLaps
                      << " tyreLaps=" << TyreLaps << " noStopDistance=" << Car.State.DistanceM << "\n";
        }
    }
    const auto Two = MakeResourceBudget(true, RaceDifficulty::Normal, 4359, 2);
    const auto Six = MakeResourceBudget(true, RaceDifficulty::Normal, 4359, 6);
    Check(Two.FuelPerThrottleSecond > Six.FuelPerThrottleSecond * 2, "budget scales with race length");
    Check(MakeResourceBudget(true, RaceDifficulty::Normal, 4359, 1).FuelPerThrottleSecond < .001,
        "one-lap qualification keeps a reachable finish without a mandatory stop");
    for (StartSignal Signal : {StartSignal::Red, StartSignal::Orange, StartSignal::Green})
    {
        StartToneVoice Voice;
        Voice.Trigger(Signal);
        const int Count = Voice.SampleCount;
        double Energy = 0, Maximum = 0;
        for (int I = 0; I < Count + 100; ++I)
        {
            const double Sample = Voice.NextSample();
            Energy += Sample * Sample;
            Maximum = std::max(Maximum, std::abs(Sample));
            if (I == 0 || I >= Count - 1) Check(Sample == 0, "start tone has silent endpoints and finite duration");
        }
        Check(Energy > Count * .02 && Maximum <= BeepFor(Signal).Volume, "start tone is audible and amplitude-bounded");
        Voice.Trigger(Signal);
        Check(Voice.Sample == 0 && Voice.SampleCount == Count, "a restart resets the audio cue");
    }

    LapValidator Lap;
    const double Len = 4000;
    for (int I = 0; I <= 400; ++I)
        Lap.Observe(I * 10.0, 0, Len, 4.75, false);
    Check(Lap.ValidLaps == 1 && Lap.LastCompletedClean, "a centred lap is valid and clean");
    Check(Lap.PhysicalLaps == 2, "physical crossings still increment");

    LapValidator Dirty;
    for (int I = 0; I <= 400; ++I)
        Dirty.Observe(I * 10.0, I == 80 ? 5.2 : 0, Len, 4.75, false);
    Check(Dirty.ValidLaps == 1 && !Dirty.LastCompletedClean, "a slight overflow dirties the time but still counts");

    LapValidator Cut;
    Cut.Observe(10, 0, Len, 4.75, false);
    Cut.Observe(50, 12, Len, 4.75, false);
    Cut.Observe(4010, 0, Len, 4.75, false);
    Check(Cut.LastCompletedInvalid && Cut.ValidLaps == 0, "a cut does not count as a race lap");
    Check(std::string(Cut.LastCompletedReason).size() > 0, "the invalid lap stores a reason");

    LapValidator Reverse;
    Reverse.Observe(100, 0, Len, 4.75, false);
    Reverse.Observe(20, 0, Len, 4.75, false);
    Check(Reverse.Invalid, "running backwards invalidates the lap");

    LapValidator PitLap;
    for (int I = 0; I <= 400; ++I)
    {
        const double S = I * 10.0;
        const double Along = std::fmod(S, Len);
        const bool InPit = Along >= 55 && Along <= 190;
        PitLap.Observe(S, InPit ? 12.5 : 0, Len, 4.75, InPit);
    }
    Check(PitLap.ValidLaps == 1 && PitLap.LastCompletedClean, "the pit lane is not treated as a cut");

    LapValidator Record;
    for (int I = 0; I <= 400; ++I)
        Record.Observe(I * 10.0, I == 20 ? 12 : 0, Len, 4.75, false);
    Check(!Record.LastCompletedClean, "an invalidated lap cannot become a record");

    Check(std::string(PersonalityByLivery(0).Name) == "Le Professeur", "blue keeps the professor profile");
    Check(PersonalityByLivery(1).Aggression > PersonalityByLivery(4).Aggression, "red is more aggressive than purple");
    Check(PersonalityByLivery(3).FuelManagement < PersonalityByLivery(1).FuelManagement,
          "green manages fuel better than red");
    Check(PersonalityByLivery(2).Pace > PersonalityByLivery(4).Pace, "yellow is quicker than the technician");

    std::vector<TrafficCar> Pack(2);
    Pack[0].LiveryIndex = 1; Pack[0].Personality = PersonalityByLivery(1); Pack[0].Distance = 0; Pack[0].Speed = 40;
    Pack[1].LiveryIndex = 4; Pack[1].Personality = PersonalityByLivery(4); Pack[1].Distance = 0; Pack[1].Speed = 40;
    for (int I = 0; I < 120 * 8; ++I)
        StepTraffic(Pack, 1.0 / 120, 5000, 4.75, RaceDifficulty::Normal, nullptr, [](double){ return 0.0; });
    Check(Pack[0].Distance > Pack[1].Distance, "the red profile pulls away from the technician on a straight");

    std::vector<TrafficCar> Pitter(1);
    Pitter[0].Fuel = 0.10; Pitter[0].TyreHealth = 300; Pitter[0].Distance = 20; Pitter[0].Speed = 18;
    Pitter[0].FuelBurn = 0.004; Pitter[0].TyreWearPerMetre = 0.2;
    bool Committed = false;
    for (int I = 0; I < 120 * 8; ++I)
    {
        StepTraffic(Pitter, 1.0 / 120, 5000, 4.75, RaceDifficulty::Normal, nullptr, [](double){ return 0.0; });
        Committed |= Pitter[0].Pitting || Pitter[0].Lateral > 6.0 || Pitter[0].TargetLane > 6.0;
    }
    Check(Committed, "a low-fuel rival commits to the pit lane");

    int WishboneCount = 0;
    const Wishbone* Bars = CarWishbones(WishboneCount);
    Check(WishboneCount == 8, "each car has double wishbones at four corners");
    Check(Bars[0].Steers && !Bars[4].Steers, "only the front bars follow steering");
    Check(std::abs(Bars[0].HubY) > std::abs(Bars[0].ChassisY), "bars run from chassis out to the hubs");

    DriveModel Free, Draft;
    Free.State.SpeedMps = Draft.State.SpeedMps = 50;
    Free.State.Gear = Draft.State.Gear = 5;
    Draft.Slipstream = 1;
    DriveInput Hold; Hold.Throttle = 1;
    for (int I = 0; I < 120 * 4; ++I)
    {
        Free.Step(Hold, 1.0 / 120, 0, 4.75);
        Draft.Step(Hold, 1.0 / 120, 0, 4.75);
    }
    Check(Draft.State.DistanceM > Free.State.DistanceM + 1.5, "slipstream cuts drag instead of inventing speed");

    if (Failures)
    {
        std::cerr << Failures << " checks failed\n";
        return 1;
    }
    std::cout << "PASS: start lights, pit-required races, lap validity, colour personalities, wishbones\n";
    return 0;
}
