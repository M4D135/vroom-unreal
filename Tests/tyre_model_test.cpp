#include "VroomDriveModel.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace Vroom;
int main()
{
    DriveModel Fresh,Worn;
    Fresh.State.SpeedMps=Worn.State.SpeedMps=50;
    Fresh.State.Gear=Worn.State.Gear=4;
    Worn.TyreHealth=0;
    DriveInput Brake; Brake.Brake=.75;
    double FreshDistance=0,WornDistance=0;
    for(int I=0;I<120*15;++I)
    {
        Fresh.Step(Brake,1.0/120,0,4.75);Worn.Step(Brake,1.0/120,0,4.75);
        FreshDistance+=Fresh.State.SpeedMps/120;WornDistance+=Worn.State.SpeedMps/120;
    }
    assert(WornDistance>FreshDistance*1.12);
    Fresh.Reset();Worn.Reset();Worn.TyreHealth=0;
    Fresh.State.SpeedMps=Worn.State.SpeedMps=40;
    DriveInput Steer;Steer.Steer=.5;Steer.Throttle=.4;
    for(int I=0;I<60;++I){Fresh.Step(Steer,1.0/120,0,4.75);Worn.Step(Steer,1.0/120,0,4.75);}
    assert(Fresh.State.LateralM>Worn.State.LateralM*1.2);
    Worn.ChangeTyres();assert(Worn.TyreGrip()==1);
    assert(Worn.TyreCondition()==1 && Fresh.TyreCondition()<=1);
    std::cout<<"PASS: worn tyres extend stopping distance and reduce cornering authority; service restores grip\n";
}
