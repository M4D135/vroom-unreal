#include "VroomContactModel.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace Vroom;
using Vector = std::array<double,3>;
using Axes = std::array<Vector,3>;
int main()
{
    const Vector Pole={5.5,5.5,185}, Car={220,120,60};
    const double Yaw=17.3*3.141592653589793/180;
    const Axes Facing={Vector{std::cos(Yaw),std::sin(Yaw),0},Vector{-std::sin(Yaw),std::cos(Yaw),0},Vector{0,0,1}};
    const Vector Measured={187.516,-166.710,-117.459};
    double Fraction=0;Vector Normal{};int Axis=0,Side=0;
    assert(ContactModel::SweepObstacle(Measured,Measured,{251.659,185.599,246.694},Fraction,Axis,Side));
    assert(!ContactModel::SweepOrientedObstacle(Measured,{187.6,-166.68,-117.459},Pole,Facing,Car,Fraction,Normal));
    assert(ContactModel::SweepOrientedObstacle(Measured,{0,0,-117.459},Pole,Facing,Car,Fraction,Normal));
    assert(Fraction>0&&Fraction<1);
    for(int Angle=0;Angle<360;Angle+=5)
    {
        const double R=Angle*3.141592653589793/180,C=std::cos(R),S=std::sin(R);
        const Axes Rotation={Vector{C,S,0},Vector{-S,C,0},Vector{0,0,1}};
        auto At=[&](double Along,double Lateral){return Vector{Along*C-Lateral*S,Along*S+Lateral*C,0};};
        // Every heading can pass parallel to the real pole with 22+ cm spare.
        assert(!ContactModel::SweepOrientedObstacle(At(-400,150),At(400,150),Pole,Rotation,Car,Fraction,Normal));
        // A genuine strike still registers even while crossing it in one step.
        assert(ContactModel::SweepOrientedObstacle(At(-400,100),At(400,100),Pole,Rotation,Car,Fraction,Normal));
        assert(Fraction>0&&Fraction<1);
        assert(std::abs(Normal[0]*Normal[0]+Normal[1]*Normal[1]+Normal[2]*Normal[2]-1)<1e-9);
    }
    const Axes Identity={Vector{1,0,0},Vector{0,1,0},Vector{0,0,1}};
    assert(ContactModel::SweepOrientedObstacle({-20,0,0},{20,0,0},{2,1,1},Identity,{2,1,1},Fraction,Normal));
    assert(std::abs(Fraction-.4)<1e-10&&Normal[0]==-1);
    assert(!ContactModel::SweepOrientedObstacle({-500,0,-400},{500,0,-400},{800,10,20},Facing,Car,Fraction,Normal));
    assert(!ContactModel::SweepOrientedObstacle({0,0,0},{0,0,std::numeric_limits<double>::quiet_NaN()},Pole,Facing,Car,Fraction,Normal));
    std::cout<<"PASS: measured pit-pole false collision rejected, 72 headings clear and strike correctly, tunnelling/gantry/normal checks\n";
}
