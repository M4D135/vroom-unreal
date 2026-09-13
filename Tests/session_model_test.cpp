#include "VroomSession.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace Vroom;
int main()
{
    SessionModel Arcade;
    Arcade.SelectTrack(4); Arcade.Begin(4359);
    Arcade.Tick(-1,0,99,1,1,0);
    assert(Arcade.Phase == SessionPhase::Countdown);
    Arcade.Tick(60,0,26,3,1,50);
    assert(Arcade.Phase == SessionPhase::Racing);
    Arcade.Tick(60.5,1,27,3,1,50); assert(Arcade.Active());
    Arcade.Tick(61,3,27,3,1,50);
    assert(Arcade.Phase == SessionPhase::Finished && Arcade.Won && Arcade.FinishedTime == 61);
    assert(Arcade.AdvanceStage() && Arcade.TrackIndex == 0 && Arcade.Stage == 1);
    Arcade.Begin(5911); Arcade.Tick(Arcade.TimeLimit,2,26,1,1,50);
    assert(!Arcade.Won && !Arcade.AdvanceStage());
    Arcade.Difficulty = RaceDifficulty::Hard; Arcade.Begin(5000);
    assert(Arcade.TargetPasses == 36);
    Arcade.Tick(20,0,0,13,0,0);
    assert(Arcade.Phase == SessionPhase::Finished && !Arcade.Won);
    Arcade.SelectTrack(4); Arcade.Difficulty = RaceDifficulty::Normal;
    for(int Stage=0;Stage<6;++Stage)
    {
        assert(Arcade.TrackIndex == SessionModel::ChampionshipOrder[Stage]);
        Arcade.Begin(4000); Arcade.Tick(90,3,27,1,1,50);
        assert(Arcade.AdvanceStage() == (Stage<5));
    }
    assert(Arcade.ChampionshipComplete);

    SessionModel GP; GP.Mode=SessionMode::GrandPrix; GP.Begin(5000);
    assert(GP.Solo() && GP.LapTarget == 1);
    GP.Tick(20,0,0,1,1,40); GP.CompleteQualification(100,true,100);
    assert(GP.QualificationComplete && GP.QualificationPosition > 1 && GP.QualificationPosition < 13);
    const int Grid=GP.QualificationPosition;
    GP.Begin(5000,true);
    assert(!GP.Solo() && GP.Position == Grid && GP.LapTarget == 3);
    GP.Tick(200,2,0,3,1,50); assert(GP.Active());
    GP.Tick(300,3,0,3,1,50);
    const std::array<int,13> ThirdPlace={0,1,12,2,3,4,5,6,7,8,9,10,11};
    assert(GP.ClassifyRace(ThirdPlace));
    assert(GP.Phase == SessionPhase::Finished && GP.ChampionshipPoints == 4);
    GP.Tick(301,3,0,1,1,50); assert(GP.ChampionshipPoints == 4);
    assert(GP.AdvanceStage()); GP.Begin(5000); GP.CompleteQualification(90,false,100);
    assert(GP.QualificationPosition == 13 && !GP.Won);
    GP.Begin(5000,true); GP.Tick(20,0,0,4,0,0);
    assert(GP.Retired && GP.Phase==SessionPhase::Finished && GP.Position==13 && GP.ChampionshipPoints==4);

    SessionModel Chrono; Chrono.Mode=SessionMode::TimeTrial; Chrono.Begin(5000);
    Chrono.Tick(2000,20,99,1,1,50);
    assert(Chrono.Active() && Chrono.Solo() && Chrono.LapTarget == 0);
    Chrono.Tick(std::numeric_limits<double>::quiet_NaN(),21,0,1,1,50);
    assert(Chrono.LapsCompleted == 20);

    PitStopModel Pit;
    Pit.Locate(102,0,0,5000); assert(!Pit.Available && !Pit.Request(true,true));
    Pit.Locate(102,12.5,5,5000); assert(Pit.InLane && !Pit.Available);
    Pit.Locate(5102,12.5,0,5000); assert(Pit.Available && Pit.Request(true,true));
    assert(Pit.Duration==7 && !Pit.Request(true,false));
    for(int I=0;I<839;++I) assert(!Pit.Step(1.0/120));
    assert(Pit.Servicing && Pit.Step(1.0/120));
    assert(!Pit.Servicing && Pit.Remaining==0 && Pit.NeedFuel && Pit.NeedTyres);
    assert(!Pit.Step(1));
    Pit.Locate(102,-12.5,0,5000); assert(!Pit.Available);
    std::cout << "PASS: modes, qualification/grid, results/points, six-stage progression, arcade limits, timed pit service\n";
}
