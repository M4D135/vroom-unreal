#include "VroomSession.h"
#include <cassert>
#include <iostream>
#include <numeric>
using namespace Vroom;
int main()
{
    SessionModel Championship; Championship.Mode=SessionMode::GrandPrix; Championship.SelectTrack(4);
    for(int Race=0;Race<6;++Race)
    {
        const auto BeforeScores=Championship.ChampionshipScores;
        const auto BeforeWins=Championship.ChampionshipWins;
        const auto BeforeFinishes=Championship.ChampionshipFinishes;
        Championship.Begin(4000,true); Championship.Tick(300,3,0,13,1,30);
        std::array<int,13> Order{};
        for(int I=0;I<13;++I)Order[I]=(I+Race*2)%13;
        assert(Championship.ClassifyRace(Order));
        assert(!Championship.ClassifyRace(Order));
        for(int Place=0;Place<13;++Place)
        {
            const int Driver=Order[Place];
            assert(Championship.RaceRanks[Driver]==Place+1);
            assert(Championship.ChampionshipScores[Driver]-BeforeScores[Driver]==SessionModel::Points[Place]);
        }
        assert(std::accumulate(Championship.ChampionshipScores.begin(),Championship.ChampionshipScores.end(),0)==26*(Race+1));
        assert(std::accumulate(Championship.ChampionshipWins.begin(),Championship.ChampionshipWins.end(),0)==Race+1);
        const auto AwardedScores=Championship.ChampionshipScores;
        const auto AwardedWins=Championship.ChampionshipWins;
        const auto AwardedFinishes=Championship.ChampionshipFinishes;
        assert(Championship.UndoLastRace());
        assert(!Championship.UndoLastRace());
        assert(Championship.ChampionshipScores==BeforeScores && Championship.ChampionshipWins==BeforeWins
            && Championship.ChampionshipFinishes==BeforeFinishes);
        Championship.Begin(4000,true); Championship.Tick(301,3,0,13,1,30);
        assert(Championship.ClassifyRace(Order));
        assert(Championship.ChampionshipScores==AwardedScores && Championship.ChampionshipWins==AwardedWins
            && Championship.ChampionshipFinishes==AwardedFinishes);
        assert(Championship.ChampionshipPoints==Championship.ChampionshipScores[12]);
        assert(Championship.AdvanceStage()==(Race<5));
    }
    assert(Championship.ChampionshipComplete);
    const auto Standing=Championship.Standings();
    for(int Place=0;Place<13;++Place)assert(Championship.ChampionshipRank(Standing[Place].Driver)==Place+1);
    assert(Championship.ChampionshipWinner()==Standing[0].Driver);
    assert(std::accumulate(Championship.ChampionshipScores.begin(),Championship.ChampionshipScores.end(),0)==156);

    // Equal points: wins decide, then second places, then lower places.
    SessionModel Tie;
    Tie.ChampionshipScores[0]=Tie.ChampionshipScores[1]=10;
    Tie.ChampionshipFinishes[0][1]=1;Tie.ChampionshipFinishes[0][2]=1;
    Tie.ChampionshipFinishes[1][0]=1;Tie.ChampionshipWins[1]=1;
    assert(Tie.ChampionshipWinner()==1);
    Tie.ChampionshipFinishes[0][0]=1;Tie.ChampionshipWins[0]=1;
    assert(Tie.ChampionshipWinner()==0);
    Tie.ResetChampionship();assert(Tie.ChampionshipPoints==0&&Tie.ChampionshipScores[0]==0&&Tie.ChampionshipWins[1]==0);

    SessionModel Dnf;Dnf.Mode=SessionMode::GrandPrix;Dnf.Begin(4000,true);Dnf.Tick(50,0,0,4,0,0);
    std::array<int,13> Order{};std::iota(Order.begin(),Order.end(),0);
    assert(Dnf.ClassifyRace(Order)&&Dnf.ChampionshipPoints==0&&Dnf.RaceRanks[12]==13);
    assert(std::accumulate(Dnf.ChampionshipScores.begin(),Dnf.ChampionshipScores.end(),0)==26);
    Dnf.ResetChampionship();Dnf.Begin(4000,true);Dnf.Tick(300,3,0,1,1,50);
    Order[1]=Order[0];assert(!Dnf.ClassifyRace(Order));
    assert(std::accumulate(Dnf.ChampionshipScores.begin(),Dnf.ChampionshipScores.end(),0)==0);
    std::cout<<"PASS: thirteen-driver standings, six races/156 points, podium distribution, exact restart rollback, countback and DNF\n";
}
