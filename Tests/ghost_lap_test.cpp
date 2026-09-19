#include "VroomGhostLap.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
int Failures=0;
void Check(bool Result,const char* Message)
{
    if(!Result){std::cerr<<"FAIL: "<<Message<<'\n';++Failures;}
}
}
int main()
{
    constexpr double Length=4360.0, Speed=90.0;
    // The tracker observes every frame, independently of its 30-Hz ghost
    // sampling. At 30 fps the first image is 2.8 m past the line: this used to
    // make the entire otherwise clean lap ineligible for a personal best.
    for(int Fps:{30,60,120})
    {
        const double Before=Length-.2,After=Before+Speed/Fps;
        const auto Crossing=Vroom::Progress::FindLineCrossing(Before,After,Length);
        Check(Crossing.Valid&&Crossing.Alpha>=0&&Crossing.Alpha<=1,
              "90 m/s finish crossing is observed at 30/60/120 fps");
        Check(std::abs(Before+(After-Before)*Crossing.Alpha-Length)<1e-10,
              "interpolation anchors the ghost to the actual timing line");
        Check(Vroom::Progress::CanSubmitCleanLap(true,true,2000,0,0,52),
              "an anchored clean lap remains eligible despite first rendered image overshooting");
    }
    const auto Exact=Vroom::Progress::FindLineCrossing(Length-3,Length,Length);
    Check(Exact.Valid&&Exact.Alpha==1,"an image exactly on the line is the crossing pose");
    const auto Departure=Vroom::Progress::FindLineCrossing(0,0,0);
    Check(Departure.Valid&&Departure.Alpha==0,"a stationary solo start has a real line anchor");
    Check(!Vroom::Progress::FindLineCrossing(24,27,0).Valid,
          "a race grid ahead of the line cannot create a full first-lap ghost");
    Check(!Vroom::Progress::FindLineCrossing(Length+1,Length+4,Length).Valid,
          "missing the previous image cannot fabricate a line crossing");
    Check(!Vroom::Progress::FindLineCrossing(Length+1,Length-1,Length).Valid,
          "reverse passage does not start a forward ghost lap");
    const double NaN=std::numeric_limits<double>::quiet_NaN();
    Check(!Vroom::Progress::FindLineCrossing(NaN,Length,Length).Valid&&
          !Vroom::Progress::FindLineCrossing(0,NaN,Length).Valid&&
          !Vroom::Progress::FindLineCrossing(0,Length,NaN).Valid,
          "non-finite samples are not usable crossing evidence");
    Check(!Vroom::Progress::CanSubmitCleanLap(true,true,2000,.01,2.8,52),
          "an unanchored first frame is not silently relabeled as a full lap");
    Check(!Vroom::Progress::CanSubmitCleanLap(true,true,2000,0,24,52),
          "shortened first grid lap cannot set a record");
    Check(!Vroom::Progress::CanSubmitCleanLap(false,true,2000,0,0,52)&&
          !Vroom::Progress::CanSubmitCleanLap(true,false,2000,0,0,52),
          "dirty recording or invalid completed-lap verdict both prevent record and ghost submission");
    Check(!Vroom::Progress::CanSubmitCleanLap(true,true,1,0,0,52)&&
          !Vroom::Progress::CanSubmitCleanLap(true,true,2000,0,0,NaN)&&
          !Vroom::Progress::CanSubmitCleanLap(true,true,2000,0,0,4),
          "incomplete samples and invalid times never become records");
    std::cout<<(Failures?"Ghost lap checks FAILED\n":"PASS: ghost line anchors, high-speed crossings, invalid/shortened lap rejection\n");
    return Failures?EXIT_FAILURE:EXIT_SUCCESS;
}
