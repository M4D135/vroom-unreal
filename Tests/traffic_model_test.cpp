#include "VroomTrafficModel.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace Vroom;
int main()
{
    const auto Straight=[](double){return 0.0;};
    const double H=1.0/120;
    TrafficCar Player; Player.Distance=35; Player.Lateral=0; Player.Speed=0;
    std::vector<TrafficCar> Cars(1); Cars[0].Speed=35;
    bool ChangedLane=false, Passed=false;
    for(int I=0;I<120*12;++I)
    {
        const auto Old=Cars[0];
        StepTraffic(Cars,H,5000,4.75,RaceDifficulty::Normal,&Player,Straight);
        assert(std::isfinite(Cars[0].Speed) && Cars[0].Speed>=0 && std::abs(Cars[0].Lateral)<=3.55);
        assert(std::abs(Cars[0].Lateral-Old.Lateral)<=1.8*H+1e-10);
        assert(Cars[0].Distance>=Old.Distance);
        ChangedLane|=std::abs(Cars[0].Lateral)>2.5;
        Passed|=Cars[0].Distance>Player.Distance+10;
        assert(!(std::abs(Cars[0].Distance-Player.Distance)<4.9 && std::abs(Cars[0].Lateral)<2.4));
    }
    assert(ChangedLane && Passed);
    std::vector<TrafficCar> Easy(1),Hard(1);
    for(int I=0;I<120*20;++I)
    {
        StepTraffic(Easy,H,5000,4.75,RaceDifficulty::Easy,nullptr,Straight);
        StepTraffic(Hard,H,5000,4.75,RaceDifficulty::Hard,nullptr,Straight);
    }
    assert(Hard[0].Speed>Easy[0].Speed+10 && Hard[0].Distance>Easy[0].Distance);
    Hard[0].DamageDelay=2;
    const double BeforeDamage=Hard[0].Speed;
    for(int I=0;I<120;++I)StepTraffic(Hard,H,5000,4.75,RaceDifficulty::Hard,nullptr,Straight);
    assert(Hard[0].Speed<BeforeDamage-10);
    std::vector<TrafficCar> Pack(12);
    for(int I=0;I<12;++I){Pack[I].Distance=40+(I/2)*16;Pack[I].TargetLane=Pack[I].Lateral=I%2?2.2:-2.2;}
    for(int Frame=0;Frame<120*60;++Frame)
    {
        StepTraffic(Pack,H,5000,4.75,RaceDifficulty::Normal,nullptr,[](double S){return std::sin(S/300)*.008;});
        for(std::size_t I=0;I<Pack.size();++I)
        {
            assert(std::isfinite(Pack[I].Distance) && std::abs(Pack[I].Lateral)<=3.55);
            for(std::size_t J=I+1;J<Pack.size();++J)
                assert(!(std::abs(TrafficGap(Pack[I].Distance,Pack[J].Distance,5000))<4.9 && std::abs(Pack[I].Lateral-Pack[J].Lateral)<2.4));
        }
    }
    for (int Hz : {30,60,120})
    {
        std::vector<TrafficCar> Pitter(1);
        auto& P = Pitter[0]; P.Distance=20; P.Speed=18; P.Fuel=.10; P.TyreHealth=300;
        P.FuelBurn=.004; P.TyreWearPerMetre=.2;
        bool Serviced=false, Entered=false, Exited=false;
        double ServiceSeconds=0;
        for (int Frame=0;Frame<Hz*65;++Frame)
        {
            const auto Old=P;
            StepTraffic(Pitter,1.0/Hz,5000,4.75,RaceDifficulty::Normal,nullptr,Straight);
            assert(std::abs(P.Lateral-Old.Lateral)<=2.4/Hz+1e-8);
            if (P.Pitting && !Old.Pitting) {Entered=true; ServiceSeconds=0;}
            if (Old.Pitting)
            {
                ServiceSeconds+=1.0/Hz;
                assert(P.Distance==Old.Distance && P.Speed==0 && P.Lateral==Old.Lateral);
            }
            if (P.Fuel>Old.Fuel+.1)
            {
                assert(Old.Pitting && ServiceSeconds>=7-1e-8);
                assert(PitAlong(P.Distance,5000)>=96 && PitAlong(P.Distance,5000)<=108);
                Serviced=true;
            }
            Exited|=Serviced && !P.PitExiting && P.Distance>220 && P.Lateral<3.55;
        }
        assert(Entered && Serviced && Exited && P.Speed>20);
    }
    std::vector<TrafficCar> Empty(1); Empty[0].Fuel=0; Empty[0].Distance=300; Empty[0].Speed=30;
    for(int I=0;I<120*3;++I)StepTraffic(Empty,H,5000,4.75,RaceDifficulty::Normal,nullptr,Straight);
    assert(Empty[0].Speed==0 && Empty[0].Fuel==0); // No free 8 m/s limp mode.

    for (int Level=0;Level<3;++Level)
    {
        const auto Difficulty=static_cast<RaceDifficulty>(Level);
        const auto Budget=MakeResourceBudget(true,Difficulty,4359,3);
        std::vector<TrafficCar> Race(12);
        std::array<int,12> Stops{};
        std::array<bool,12> Finished{};
        for(int I=0;I<12;++I)
        {
            Race[I].Distance=40+(I/2)*16;
            Race[I].Lateral=Race[I].TargetLane=I%2?2.2:-2.2;
            Race[I].LiveryIndex=I/2; Race[I].Personality=PersonalityByLivery(I/2);
            Race[I].FuelBurn=Budget.FuelPerThrottleSecond; Race[I].TyreWearPerMetre=Budget.TyreWearPerMetre;
        }
        bool Complete=false;
        for(int Frame=0;Frame<120*1000;++Frame)
        {
            const auto Before=Race;
            StepTraffic(Race,H,4359,4.75,Difficulty,nullptr,[](double S){return std::sin(S/300)*.008;});
            Complete=true;
            for(int I=0;I<12;++I)
            {
                if(Finished[I])continue;
                assert(Race[I].Fuel>0 && Race[I].Distance>=Before[I].Distance);
                assert(std::abs(Race[I].Lateral-Before[I].Lateral)<=2.4*H+1e-8);
                if(Race[I].Fuel>Before[I].Fuel+.1){assert(Before[I].Pitting);++Stops[I];}
                Finished[I]=Race[I].Distance>=4359*3;
                if(Finished[I])assert(Stops[I]>=1 && Stops[I]<=3);
                Complete&=Finished[I];
            }
            if(Complete)break;
        }
        assert(Complete); // Includes a busy single-box queue and pit exit merges.
    }
    std::cout<<"PASS: overtaking, difficulty, damage, 30/60/120 Hz pit service, fuel depletion, three complete twelve-car races\n";
}
