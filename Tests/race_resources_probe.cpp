#include "VroomTrafficModel.h"
#include "VroomTrackPreview.h"
#include <array>
#include <iostream>
#include <limits>

// Read source preview points supplied by the Python test. Curvature is a
// portable approximation of the smoothed centreline, not an Unreal renderer.
int main()
{
    int TrackCount = 0;
    std::cin >> TrackCount;
    int Failed = 0;
    for (int Track = 0; Track < TrackCount; ++Track)
    {
        double Length; int Count;
        std::cin >> Length >> Count;
        std::vector<Vroom::PreviewPoint> Raw(Count);
        for (auto& Point : Raw) std::cin >> Point.X >> Point.Y;
        const auto Points = Vroom::BuildDriveablePreview(Raw, Length);
        std::vector<double> Arcs(Points.size() + 1, 0), Curvature(Points.size());
        for (std::size_t I = 0; I < Points.size(); ++I)
        {
            const auto& P = Points[(I + Points.size() - 1) % Points.size()];
            const auto& A = Points[I]; const auto& B = Points[(I + 1) % Points.size()];
            const double DX = A.X - P.X, DY = A.Y - P.Y;
            const double EX = B.X - A.X, EY = B.Y - A.Y;
            const double Angle = std::atan2(DX * EY - DY * EX, DX * EX + DY * EY);
            Curvature[I] = Angle / std::max(.01, (std::hypot(DX, DY) + std::hypot(EX, EY)) * .5);
            Arcs[I + 1] = Arcs[I] + std::hypot(EX, EY);
        }
        std::array<double, 1024> Samples{};
        for (std::size_t I = 0; I < Samples.size(); ++I)
        {
            const double S = I * Arcs.back() / Samples.size();
            const auto Index = std::min(Points.size() - 1,
                std::size_t(std::upper_bound(Arcs.begin(), Arcs.end(), S) - Arcs.begin() - 1));
            const double T = (S - Arcs[Index]) / std::max(.01, Arcs[Index + 1] - Arcs[Index]);
            Samples[I] = Curvature[Index] * (1 - T) + Curvature[(Index + 1) % Points.size()] * T;
        }
        const auto Curve = [&](double S)
        {
            const double T = Vroom::PitAlong(S, Length) * Samples.size() / Length;
            const auto I = static_cast<std::size_t>(T);
            return Samples[I % Samples.size()] * (1 - (T - I)) + Samples[(I + 1) % Samples.size()] * (T - I);
        };
        for (int Laps : {2, 3, 6}) for (int Level = 0; Level < 3; ++Level)
        {
            const auto Difficulty = static_cast<Vroom::RaceDifficulty>(Level);
            const auto Budget = Vroom::MakeResourceBudget(true, Difficulty, Length, Laps);
            int MinStops = 1000, MaxStops = 0;
            double MinFuel = 1, MaxSeconds = 0;
            for (int Livery = 0; Livery < 6; ++Livery)
            {
                std::vector<Vroom::TrafficCar> Cars(1);
                auto& Car = Cars[0];
                Car.Distance = 0; Car.Personality = Vroom::PersonalityByLivery(Livery);
                Car.LiveryIndex = Livery; Car.FuelBurn = Budget.FuelPerThrottleSecond;
                Car.TyreWearPerMetre = Budget.TyreWearPerMetre;
                int Stops = 0, ServiceFrames = 0;
                double ServiceDistance = 0, LowestFuel = 1;
                int Frame = 0;
                constexpr double H = 1.0 / 120.0;
                for (; Frame < 120 * 1800 && Car.Distance < Length * Laps; ++Frame)
                {
                    const auto Old = Car;
                    Vroom::StepTraffic(Cars, H, Length, 4.75, Difficulty, nullptr, Curve);
                    LowestFuel = std::min(LowestFuel, Car.Fuel);
                    if (!Old.Pitting && Car.Pitting) { ServiceFrames = 0; ServiceDistance = Car.Distance; }
                    if (Old.Pitting)
                    {
                        ++ServiceFrames;
                        if (Car.Speed != 0 || Car.Distance != ServiceDistance) ++Failed;
                    }
                    if (Car.Fuel > Old.Fuel + .1)
                    {
                        ++Stops;
                        const double Along = Vroom::PitAlong(Car.Distance, Length);
                        if (!Old.Pitting || ServiceFrames < 840 || Along < 96 || Along > 108 || Car.Lateral < 11 || Car.Lateral > 14)
                        { std::cerr << "Invalid pit service\n"; ++Failed; }
                    }
                    const double Along = Vroom::PitAlong(Car.Distance, Length);
                    const bool OnRoad = std::abs(Car.Lateral) <= 4.75;
                    const bool OnPit = Along >= Vroom::TrackLayout::PitEntry && Along <= Vroom::TrackLayout::PitExit
                        && std::abs(Car.Lateral - Vroom::TrackLayout::PitCenterAt(Along, 4.75)) <= Vroom::TrackLayout::PitHalfWidth;
                    if (!OnRoad && !OnPit) { std::cerr << "AI missed the visible pit surface: S=" << Along << " lat=" << Car.Lateral << "\n"; ++Failed; break; }
                    if (!std::isfinite(Car.Distance) || Car.Fuel < 0 || Car.TyreHealth < 0
                        || Car.Distance < Old.Distance || std::abs(Car.Lateral - Old.Lateral) > 2.4 * H + 1e-8)
                    { std::cerr << "Nonphysical resource/pit state\n"; ++Failed; break; }
                }
                if (Car.Distance < Length * Laps || Stops < 1 || Stops > Laps || LowestFuel < .01)
                {
                    std::cerr << "Race failed: track=" << Track << " laps=" << Laps << " difficulty=" << Level
                        << " livery=" << Livery << " stops=" << Stops << " distance=" << Car.Distance
                        << " fuel=" << LowestFuel << " lat=" << Car.Lateral << '\n';
                    ++Failed;
                }
                MinStops = std::min(MinStops, Stops); MaxStops = std::max(MaxStops, Stops);
                MinFuel = std::min(MinFuel, LowestFuel); MaxSeconds = std::max(MaxSeconds, Frame * H);
            }
            std::cout << "track=" << Track << " metres=" << Length << " laps=" << Laps << " difficulty=" << Level
                << " fuelPerSecond=" << Budget.FuelPerThrottleSecond << " tyrePerMetre=" << Budget.TyreWearPerMetre
                << " stops=" << MinStops << '-' << MaxStops << " minFuel=" << MinFuel << " maxSeconds=" << MaxSeconds << '\n';
        }
    }
    if (Failed) { std::cerr << Failed << " failures\n"; return 1; }
    std::cout << "PASS: 324 complete races, six track centrelines, three lengths/difficulties, six personalities; full seven-second pit service\n";
}
