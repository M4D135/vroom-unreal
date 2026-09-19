#include "VroomRaceRules.h"
#include "VroomSession.h"
#include "VroomTrackLayout.h"
#include "VroomGhostLap.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
int Failures=0;
constexpr double Length=4000,HalfWidth=4.75;
void Check(bool Passed,const char* Message)
{
    if(!Passed){std::cerr<<"FAIL: "<<Message<<'\n';++Failures;}
}
void RoadTo(Vroom::LapValidator& Lap,double From,double To)
{
    for(double S=From;S<To;S+=10) Lap.Observe(S,0,Length,HalfWidth,false);
    Lap.Observe(To,0,Length,HalfWidth,false);
}
bool RecordEligible(const Vroom::LapValidator& Lap)
{
    return Vroom::Progress::CanSubmitCleanLap(true,Lap.LastCompletedClean,2000,0,0,60);
}
}

int main()
{
    Vroom::LapValidator Overflow;
    for(int S=0;S<=4000;S+=10)
        Overflow.Observe(S,S==800?HalfWidth+.1:0,Length,HalfWidth,false);
    Check(Overflow.ValidLaps==1&&!Overflow.LastCompletedInvalid,
          "brief slight overflow still counts toward the race distance");
    Check(!Overflow.LastCompletedClean&&!RecordEligible(Overflow),
          "slight overflow cannot set a record or save a ghost");
    Check(Overflow.LastCompletedReason[0]!='\0',"a dirty counted lap retains its explanation");

    Vroom::LapValidator Cut;
    for(int S=0;S<=4000;S+=10)
        Cut.Observe(S,S==800?HalfWidth+6.1:0,Length,HalfWidth,false);
    Check(Cut.ValidLaps==0&&Cut.LastCompletedInvalid&&!RecordEligible(Cut),
          "a large cut cannot count as a race lap or personal best despite visiting every checkpoint");
    RoadTo(Cut,4010,8000);
    Check(Cut.ValidLaps==1&&Cut.LastCompletedClean&&!Cut.LastCompletedInvalid&&RecordEligible(Cut),
          "the next genuinely complete clean lap becomes valid after an invalid one");
    for(int S=8010;S<=12000;S+=10)
        Cut.Observe(S,S==8800?HalfWidth+6.1:0,Length,HalfWidth,false);
    Check(Cut.ValidLaps==1&&Cut.LastCompletedInvalid,
          "a later invalid lap does not erase earlier valid laps");

    Vroom::LapValidator Jump;
    RoadTo(Jump,0,100);
    // Lands near the next checkpoint, so checkpoint order alone cannot detect
    // this impossible movement. The remaining checkpoint sequence is complete.
    Jump.Observe(620,0,Length,HalfWidth,false);
    Check(Jump.Invalid,"a forward jump greater than one eighth of the circuit invalidates the lap");
    RoadTo(Jump,630,4000);
    Check(Jump.ValidLaps==0&&Jump.LastCompletedInvalid&&!RecordEligible(Jump),
          "a teleported lap is rejected even after all checkpoint bits have been seen");

    Vroom::LapValidator SlowReverse;
    RoadTo(SlowReverse,0,1000);
    for(int I=1;I<=10;++I) SlowReverse.Observe(1000-I*.1,0,Length,HalfWidth,false);
    Check(SlowReverse.Invalid,"slow reverse movement of 10 cm per observation is detected");
    RoadTo(SlowReverse,1000,4000);
    Check(SlowReverse.ValidLaps==0&&SlowReverse.LastCompletedInvalid&&!RecordEligible(SlowReverse),
          "driving forward again cannot cleanse a lap containing reverse motion");

    Vroom::LapValidator Oscillated;
    RoadTo(Oscillated,0,4000);
    Check(Oscillated.ValidLaps==1&&Oscillated.PhysicalLaps==2,"initial clean crossing is counted once");
    for(int I=0;I<20;++I)
    {
        Oscillated.Observe(3999.9,0,Length,HalfWidth,false);
        Oscillated.Observe(4000.1,0,Length,HalfWidth,false);
    }
    Check(Oscillated.ValidLaps==1&&Oscillated.PhysicalLaps==2,
          "oscillating across the timing line never adds completed race laps");
    Check(Oscillated.Invalid,"oscillation dirties the current lap through reverse motion");
    RoadTo(Oscillated,4010,8000);
    Check(Oscillated.ValidLaps==1&&Oscillated.LastCompletedInvalid,
          "a full later crossing does not erase the oscillation offence");
    RoadTo(Oscillated,8010,12000);
    Check(Oscillated.ValidLaps==2&&Oscillated.LastCompletedClean,
          "a fresh valid lap counts after the invalid oscillation lap without losing previous progress");

    Vroom::LapValidator PitLap;
    Vroom::PitStopModel Pit;
    for(int S=0;S<=4000;++S)
    {
        const bool OnPitSurface=S>=Vroom::TrackLayout::PitEntry&&S<=Vroom::TrackLayout::PitExit;
        const double Lateral=OnPitSurface?Vroom::TrackLayout::PitCenterAt(S,HalfWidth):0;
        Pit.Locate(S,Lateral,12,Length);
        if(OnPitSurface) Check(Pit.InLane,"painted pit entry, lane and exit are all recognized as legal pit surface");
        PitLap.Observe(S,Lateral,Length,HalfWidth,Pit.InLane);
    }
    Check(PitLap.ValidLaps==1&&PitLap.LastCompletedClean&&RecordEligible(PitLap),
          "a full legal pit passage including both ramps preserves race and record validity");
    for(double S:{30.0,215.0})
    {
        Pit.Locate(S,12.5,12,Length);
        Check(!Pit.InLane,"pit permission does not extend beyond the visible entry and exit");
    }
    for(double S:{43.0,100.0,202.0})
    {
        Pit.Locate(S,-12.5,12,Length);
        Check(!Pit.InLane,"the opposite grass verge is not excused as a pit lane");
        Pit.Locate(S,Vroom::TrackLayout::PitCenterAt(S,HalfWidth)+Vroom::TrackLayout::PitHalfWidth+1,12,Length);
        Check(!Pit.InLane,"driving beyond the pit surface remains off-road");
    }

    const double NaN=std::numeric_limits<double>::quiet_NaN();
    for(bool CorruptDistance:{false,true})
    {
        Vroom::LapValidator Corrupt;
        RoadTo(Corrupt,0,100);
        Corrupt.Observe(CorruptDistance?NaN:110,CorruptDistance?0:NaN,Length,HalfWidth,false);
        RoadTo(Corrupt,120,4000);
        Check(Corrupt.LastCompletedInvalid&&Corrupt.ValidLaps==0&&!RecordEligible(Corrupt),
              "non-finite driving samples cannot be skipped to manufacture a clean lap");
    }
    std::cout<<(Failures?"Lap validity checks FAILED\n":"PASS: counted/clean laps, cuts, jumps, slow reverse, line oscillation, pit entry and exit\n");
    return Failures?EXIT_FAILURE:EXIT_SUCCESS;
}
