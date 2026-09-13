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
    std::cout<<"PASS: player avoidance/overtaking, continuous lane changes, difficulty, damage, sixty-second traffic pack\n";
}
