#include "VroomGame.h"
#include "VroomProceduralMeshComponent.h"
#include "Math/RotationMatrix.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Misc/Char.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "UnrealClient.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"

namespace
{
// The original view crops the lower wheel beneath a narrow instrument band.
const FVector WheelCenter(106, 0, 46);
constexpr double WheelRadius = 18.8;
constexpr double WheelTube = 1.20;
const FVector InstrumentCenter(121, 0, 51);
const FQuat InstrumentTilt(FVector::RightVector, FMath::DegreesToRadians(12.0));
enum ECockpitPart { Wheel, LeftGlove, RightGlove, Dashboard, LeftForearm, RightForearm, GearIndicator, Readout, Surround, RpmStrip, Windshield, CockpitPartCount };

const FVector GloveCuff(-5.2, 29.2, -5.0);
constexpr float CockpitHandDeadzone = 0.07f;
constexpr float CockpitSteeringInputMax = 0.62f;
constexpr float CockpitSteeringSmoothing = 55.f;
float CockpitSteeringVisualAngleDeg()
{
    static float Value = 95.f;
    static bool Init = false;
    if (!Init)
    {
        FParse::Value(FCommandLine::Get(), TEXT("VroomCockpitVisualAngle="), Value);
        Value = FMath::Clamp(Value, 60.f, 160.f);
        Init = true;
    }
    return Value;
}

struct FDrivingHandPose { FVector Wrist; FQuat Rotation; double Scale; };

FDrivingHandPose DrivingHandPose(float Side, float Steering)
{
    // The rim passes through the closed grip. Both the grip position and its
    // orientation share the wheel's exact rotation, so a hand cannot slide
    // over the rim while steering. The opposite hand naturally leaves the
    // original low camera crop; camera height is not changed to reveal it.
    const double Turn = -FMath::Clamp(double(Steering / CockpitSteeringInputMax), -1.0, 1.0)
        * CockpitSteeringVisualAngleDeg();
    static double NeutralAngle = 30.0, GripDepth = -2.5, GripReach = 19.1, Scale = 1.05, WristTilt = 80.0;
    static bool Init = false;
    if (!Init)
    {
        // The baked grip and rendered pose use the same fit parameters. The
        // Amiga neutral reference puts the glove backs on the wheel's flanks.
        FString Data;
        TSharedPtr<FJsonObject> Document;
        if (FFileHelper::LoadFileToString(Data, *(FPaths::ProjectContentDir() / TEXT("Data/gloves.json")))
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Data), Document) && Document.IsValid())
        {
            const TSharedPtr<FJsonObject>* Pose;
            if (Document->TryGetObjectField(TEXT("driving_grip"), Pose))
            {
                (*Pose)->TryGetNumberField(TEXT("neutral_angle_deg"), NeutralAngle);
                (*Pose)->TryGetNumberField(TEXT("wrist_tilt_deg"), WristTilt);
                (*Pose)->TryGetNumberField(TEXT("scale"), Scale);
            }
        }
        FParse::Value(FCommandLine::Get(), TEXT("VroomGripAngle="), NeutralAngle);
        FParse::Value(FCommandLine::Get(), TEXT("VroomGripDepth="), GripDepth);
        FParse::Value(FCommandLine::Get(), TEXT("VroomGripReach="), GripReach);
        FParse::Value(FCommandLine::Get(), TEXT("VroomGripScale="), Scale);
        FParse::Value(FCommandLine::Get(), TEXT("VroomWristTilt="), WristTilt);
        Init = true;
    }
    const double Angle = FMath::DegreesToRadians((Side < 0 ? 180.0 - NeutralAngle : NeutralAngle) + Turn);
    const FVector Contact = WheelCenter + FVector(0, FMath::Cos(Angle), FMath::Sin(Angle)) * WheelRadius;
    // Canonical -X is the back of the glove, -Y points toward the knuckles,
    // and Z spans the four fingers. Align that span with the rim tangent.
    const FQuat Rotation = FQuat(FVector::ForwardVector, Angle - UE_PI)
        * FQuat(FVector::UpVector, FMath::DegreesToRadians(WristTilt));
    const FVector Grip = FVector(GripDepth, GripReach, -5.4) - GloveCuff;
    const FVector MirroredGrip(Grip.X, Grip.Y, -Side * Grip.Z);
    return {Contact - Rotation.RotateVector(MirroredGrip) * Scale, Rotation, Scale};
}

struct FCockpitGeometry
{
    TArray<FVector> Vertices, Normals;
    TArray<int32> Triangles;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;

    int32 Vertex(FVector P, FVector N, FVector2D UV, FVector Tangent)
    {
        const int32 Index = Vertices.Add(P);
        Normals.Add(N.GetSafeNormal()); UVs.Add(UV); Colors.Add(FLinearColor::White);
        Tangents.Add(FProcMeshTangent(Tangent.GetSafeNormal(), false));
        return Index;
    }
    void Triangle(int32 A, int32 B, int32 C)
    {
        const FVector N = FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]);
        if (FVector::DotProduct(N, Normals[A] + Normals[B] + Normals[C]) > 0) Swap(B, C);
        Triangles.Append({A, B, C});
    }
    void Grid(int32 Start, int32 Rows, int32 Columns)
    {
        for (int32 I = 0; I < Rows; ++I)
            for (int32 J = 0; J < Columns; ++J)
            {
                const int32 A = Start + I * (Columns + 1) + J;
                Triangle(A, A + 1, A + Columns + 1);
                Triangle(A + 1, A + Columns + 2, A + Columns + 1);
            }
    }
    void Ellipsoid(FVector Center, FVector Radii, FQuat Rotation = FQuat::Identity, int32 Rings = 10, int32 Sides = 16)
    {
        const int32 Start = Vertices.Num();
        for (int32 I = 0; I <= Rings; ++I)
        {
            const double Latitude = UE_PI * I / Rings;
            for (int32 J = 0; J <= Sides; ++J)
            {
                const double Longitude = UE_TWO_PI * J / Sides;
                const FVector Unit(FMath::Sin(Latitude) * FMath::Cos(Longitude),
                    FMath::Sin(Latitude) * FMath::Sin(Longitude), FMath::Cos(Latitude));
                Vertex(Center + Rotation.RotateVector(Unit * Radii),
                    Rotation.RotateVector(Unit / Radii), FVector2D(double(J) / Sides, double(I) / Rings),
                    Rotation.RotateVector(FVector(-FMath::Sin(Longitude), FMath::Cos(Longitude), 0)));
            }
        }
        Grid(Start, Rings, Sides);
    }
    void Torus(double Radius, double Tube, FVector Center = FVector::ZeroVector,
        double StartAngle = 0, double Sweep = UE_TWO_PI, int32 Segments = 72)
    {
        constexpr int32 TubeSegments = 12;
        const int32 Start = Vertices.Num();
        for (int32 I = 0; I <= Segments; ++I)
        {
            const double Angle = StartAngle + Sweep * I / Segments;
            const FVector Radial(0, FMath::Cos(Angle), FMath::Sin(Angle));
            for (int32 J = 0; J <= TubeSegments; ++J)
            {
                const double Round = UE_TWO_PI * J / TubeSegments;
                const FVector N(FMath::Sin(Round), Radial.Y * FMath::Cos(Round), Radial.Z * FMath::Cos(Round));
                Vertex(Center + Radial * Radius + N * Tube, N,
                    FVector2D(double(I) / Segments, double(J) / TubeSegments),
                    FVector(0, -FMath::Sin(Angle), FMath::Cos(Angle)));
            }
        }
        Grid(Start, Segments, TubeSegments);
    }
    // A swept tube keeps the finger shafts cylindrical between curved joints.
    void Tube(const TArray<FVector>& Points, const TArray<double>& Radii,
        FVector Across = FVector::UpVector, int32 Sides = 12, int32 Steps = 4)
    {
        const int32 Start = Vertices.Num();
        const int32 Rows = (Points.Num() - 1) * Steps;
        for (int32 I = 0; I <= Rows; ++I)
        {
            const int32 Span = FMath::Min(I / Steps, Points.Num() - 2);
            const double T = double(I - Span * Steps) / Steps;
            const FVector& A = Points[FMath::Max(Span - 1, 0)];
            const FVector& B = Points[Span];
            const FVector& C = Points[Span + 1];
            const FVector& D = Points[FMath::Min(Span + 2, Points.Num() - 1)];
            const FVector C1 = (C - A) * .5, C2 = A - B * 2.5 + C * 2 - D * .5;
            const FVector C3 = (D - A) * .5 + (B - C) * 1.5;
            const FVector P = B + C1 * T + C2 * T * T + C3 * T * T * T;
            const FVector Along = (C1 + C2 * (2 * T) + C3 * (3 * T * T)).GetSafeNormal();
            FVector U = (Across - Along * FVector::DotProduct(Across, Along)).GetSafeNormal();
            if (U.IsNearlyZero()) U = FVector::CrossProduct(Along, FVector::RightVector).GetSafeNormal();
            const FVector V = FVector::CrossProduct(Along, U).GetSafeNormal();
            const double Radius = FMath::Lerp(Radii[Span], Radii[Span + 1], T);
            for (int32 J = 0; J <= Sides; ++J)
            {
                const double Angle = UE_TWO_PI * J / Sides;
                const FVector N = U * FMath::Cos(Angle) + V * FMath::Sin(Angle);
                Vertex(P + N * Radius, N, FVector2D(double(I) / Rows, double(J) / Sides), Along);
            }
        }
        Grid(Start, Rows, Sides);
    }
    void Line(FVector A, FVector B, double Width)
    {
        const FVector Along = (B - A).GetSafeNormal();
        const FVector Edge = FVector::CrossProduct(FVector::ForwardVector, Along) * (Width * .5);
        const int32 S = Vertices.Num();
        Vertex(A - Edge, -FVector::ForwardVector, FVector2D(0, 0), Along);
        Vertex(A + Edge, -FVector::ForwardVector, FVector2D(0, 1), Along);
        Vertex(B + Edge, -FVector::ForwardVector, FVector2D(1, 1), Along);
        Vertex(B - Edge, -FVector::ForwardVector, FVector2D(1, 0), Along);
        Triangle(S, S + 1, S + 2); Triangle(S, S + 2, S + 3);
    }
    void Face(const TArray<FVector>& Points, FVector Normal)
    {
        const int32 S = Vertices.Num();
        for (const FVector& P : Points) Vertex(P, Normal, FVector2D(P.Y, P.Z) * .1, FVector::RightVector);
        for (int32 I = 1; I + 1 < Points.Num(); ++I) Triangle(S, S + I, S + I + 1);
    }
    void Prism(const TArray<FVector2D>& Outline, double Depth, FVector Center = FVector::ZeroVector,
        FQuat Rotation = FQuat::Identity)
    {
        TArray<FVector> Front, Back;
        for (const FVector2D& P : Outline)
        {
            Front.Add(Center + Rotation.RotateVector(FVector(-Depth * .5, P.X, P.Y)));
            Back.Add(Center + Rotation.RotateVector(FVector(Depth * .5, P.X, P.Y)));
        }
        Face(Front, Rotation.RotateVector(-FVector::ForwardVector));
        Face(Back, Rotation.RotateVector(FVector::ForwardVector));
        for (int32 I = 0; I < Outline.Num(); ++I)
        {
            const int32 J = (I + 1) % Outline.Num();
            const FVector2D Delta = Outline[J] - Outline[I];
            Face({Front[I], Back[I], Back[J], Front[J]}, Rotation.RotateVector(FVector(0, Delta.Y, -Delta.X)));
        }
    }
    void Disc(FVector Center, double Radius, double Depth, int32 Sides = 32)
    {
        TArray<FVector2D> Outline;
        for (int32 I = 0; I < Sides; ++I)
        {
            const double A = UE_TWO_PI * I / Sides;
            Outline.Add(FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
        }
        Prism(Outline, Depth, Center);
    }
    void Panel(FVector Center, double HalfWidth, double HalfHeight, double Depth, double Corner = 1)
    {
        Prism({{-HalfWidth + Corner, -HalfHeight}, {HalfWidth - Corner, -HalfHeight},
            {HalfWidth, -HalfHeight + Corner}, {HalfWidth, HalfHeight - Corner},
            {HalfWidth - Corner, HalfHeight}, {-HalfWidth + Corner, HalfHeight},
            {-HalfWidth, HalfHeight - Corner}, {-HalfWidth, -HalfHeight + Corner}}, Depth, Center);
    }
    void Sleeve(FVector Elbow, FVector Wrist, FVector WristTangent, FVector WristAcross,
        double From = 0, double To = 1, double ExtraRadius = 0,
        double StartAngle = 0, double Sweep = UE_TWO_PI)
    {
        const int32 Rings = FMath::Max(8, FMath::CeilToInt((To - From) * 28));
        constexpr int32 Sides = 24;
        const FVector P1 = Elbow + (Wrist - Elbow) * .48 + FVector(0, 0, -2);
        const FVector P2 = Wrist - WristTangent * 8;
        const int32 Start = Vertices.Num();
        for (int32 I = 0; I <= Rings; ++I)
        {
            const double T = FMath::Lerp(From, To, double(I) / Rings), U = 1 - T;
            const FVector Center = Elbow * (U * U * U) + P1 * (3 * U * U * T) + P2 * (3 * U * T * T) + Wrist * (T * T * T);
            const FVector Along = ((P1 - Elbow) * (3 * U * U) + (P2 - P1) * (6 * U * T) + (Wrist - P2) * (3 * T * T)).GetSafeNormal();
            const FVector Across = (WristAcross - Along * FVector::DotProduct(WristAcross, Along)).GetSafeNormal();
            const FVector Depth = FVector::CrossProduct(Along, Across).GetSafeNormal();
            const double Fold = .12 * FMath::Exp(-FMath::Square((T - .78) / .035)) - .08 * FMath::Exp(-FMath::Square((T - .85) / .025));
            const double Width = FMath::Lerp(4.25, 3.1, T) + Fold + ExtraRadius;
            const double Thickness = FMath::Lerp(2.75, 2.0, T) + Fold + ExtraRadius;
            for (int32 J = 0; J <= Sides; ++J)
            {
                const double A = StartAngle + Sweep * J / Sides;
                const double C = FMath::Cos(A), S = FMath::Sin(A);
                Vertex(Center + Across * (Width * S) + Depth * (Thickness * C),
                    Across * (S / Width) + Depth * (C / Thickness), FVector2D(T, double(J) / Sides), Along);
            }
        }
        Grid(Start, Rings, Sides);
    }
    void Visor()
    {
        constexpr int32 Rows = 8, Columns = 48;
        const int32 Start = Vertices.Num();
        for (int32 I = 0; I <= Rows; ++I)
            for (int32 J = 0; J <= Columns; ++J)
            {
                const double U = -1.0 + 2.0 * J / Columns, V = double(I) / Rows;
                const double Arch = FMath::Sqrt(FMath::Max(0.0, 1 - U * U));
                const FVector Along(-16 * U, 37, -33 * U / FMath::Max(.05, Arch) * V);
                const FVector Up(2, 0, 33 * Arch);
                Vertex(FVector(135 - 8 * U * U + 2 * V, 37 * U, 23 + 33 * Arch * V),
                    FVector::CrossProduct(Up, Along), FVector2D((U + 1) * .5, V), Along);
            }
        Grid(Start, Rows, Columns);
    }
    // Seven-segment geometry has no font asset dependency. Disabled bars collapse.
    void Peg(FVector Center, double HalfY, double HalfZ, double Depth)
    {
        Prism({{-HalfY, -HalfZ}, {HalfY, -HalfZ}, {HalfY, HalfZ}, {-HalfY, HalfZ}}, Depth, Center);
    }
    void Quad(FVector Center, double HalfW, double HalfH, double Depth)
    {
        const FVector N(-1, 0, 0);
        const FVector T(0, 1, 0);
        const int32 S = Vertices.Num();
        Vertex(Center + FVector(-Depth, -HalfW, -HalfH), N, FVector2D(0.0f, 1.0f), T);
        Vertex(Center + FVector(-Depth,  HalfW, -HalfH), N, FVector2D(1.0f, 1.0f), T);
        Vertex(Center + FVector(-Depth,  HalfW,  HalfH), N, FVector2D(1.0f, 0.0f), T);
        Vertex(Center + FVector(-Depth, -HalfW,  HalfH), N, FVector2D(0.0f, 0.0f), T);
        Triangle(S, S + 1, S + 2);
        Triangle(S, S + 2, S + 3);
    }
    void Digit(int32 Value, FVector Center, double Height)
    {
        static const uint8 Masks[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f, 0x54};
        static const FVector2D A[] = {{-.42, .5}, {.5, .43}, {.5, -.07}, {-.42, -.5},
            {-.5, -.07}, {-.5, .43}, {-.42, 0}};
        static const FVector2D B[] = {{.42, .5}, {.5, .07}, {.5, -.43}, {.42, -.5},
            {-.5, -.43}, {-.5, .07}, {.42, 0}};
        const uint8 Mask = Masks[FMath::Clamp(Value, 0, 10)];
        for (int32 I = 0; I < 7; ++I)
        {
            const bool Lit = (Mask & (1 << I)) != 0;
            const FVector From = Center + FVector(0, A[I].X * Height * .53, A[I].Y * Height);
            const FVector To = Lit ? Center + FVector(0, B[I].X * Height * .53, B[I].Y * Height) : From;
            Line(From, To, Lit ? Height * .075 : 0);
        }
    }
    void Commit(UProceduralMeshComponent* Mesh, int32 Section, const TCHAR* Material)
    {
        Mesh->CreateMeshSection_LinearColor(Section, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
        const FString Path = FString::Printf(TEXT("/Game/Materials/%s.%s"), Material, Material);
        Mesh->SetMaterial(Section, LoadObject<UMaterialInterface>(nullptr, *Path));
    }
};

FCockpitGeometry SleeveGeometry(float Side, const FDrivingHandPose& Hand)
{
    const FVector Tangent = Hand.Rotation.RotateVector(FVector(0,-1,0));
    const FVector Across = Hand.Rotation.RotateVector(FVector(0,0,-Side));
    FCockpitGeometry G;
    G.Sleeve(FVector(68, Side * 18, 26), Hand.Wrist, Tangent, Across);
    return G;
}

FCockpitGeometry InstrumentDigits(int32 Speed)
{
    FCockpitGeometry G;
    for (int32 I = 0; I < 3; ++I)
    {
        const int32 Divisor = I == 0 ? 100 : I == 1 ? 10 : 1;
        G.Digit((Speed / Divisor) % 10, FVector(-1.95, 7.0 + I * 5.0, .75), 5.1);
    }
    return G;
}

FCockpitGeometry RpmSegments(int32 Lit, int32 First, int32 Last)
{
    FCockpitGeometry G;
    for (int32 I = First; I < Last; ++I)
    {
        const FVector A(-2.0, -24.0 + I * 2.0, -5.15);
        G.Line(A, I < Lit ? A + FVector(0, 1.52, 0) : A, I < Lit ? .62 : 0);
    }
    return G;
}

enum EBannerPart { BannerHousing, BannerMap, BannerLamps, BannerTitle, BannerDigits, BannerCars, BannerFuel, BannerBlip, BannerPartCount };

FVector HudPx(double X, double Y, double Depth = 0)
{
    return FVector(-Depth, (X - 160.0) * 0.1, (34.0 - Y) * 0.1);
}

const uint8* GlyphRows(TCHAR C)
{
    switch (FChar::ToUpper(C))
    {
    case '0': { static const uint8 R[] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}; return R; }
    case '1': { static const uint8 R[] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}; return R; }
    case '2': { static const uint8 R[] = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}; return R; }
    case '3': { static const uint8 R[] = {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}; return R; }
    case '4': { static const uint8 R[] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}; return R; }
    case '5': { static const uint8 R[] = {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}; return R; }
    case '6': { static const uint8 R[] = {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}; return R; }
    case '7': { static const uint8 R[] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}; return R; }
    case '8': { static const uint8 R[] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}; return R; }
    case '9': { static const uint8 R[] = {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}; return R; }
    case 'A': { static const uint8 R[] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; return R; }
    case 'B': { static const uint8 R[] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}; return R; }
    case 'C': { static const uint8 R[] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; return R; }
    case 'D': { static const uint8 R[] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}; return R; }
    case 'E': { static const uint8 R[] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; return R; }
    case 'F': { static const uint8 R[] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}; return R; }
    case 'G': { static const uint8 R[] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}; return R; }
    case 'H': { static const uint8 R[] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; return R; }
    case 'I': { static const uint8 R[] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}; return R; }
    case 'J': { static const uint8 R[] = {0x07,0x01,0x01,0x01,0x11,0x11,0x0E}; return R; }
    case 'K': { static const uint8 R[] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11}; return R; }
    case 'L': { static const uint8 R[] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; return R; }
    case 'M': { static const uint8 R[] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; return R; }
    case 'N': { static const uint8 R[] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11}; return R; }
    case 'O': { static const uint8 R[] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; return R; }
    case 'P': { static const uint8 R[] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; return R; }
    case 'Q': { static const uint8 R[] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}; return R; }
    case 'R': { static const uint8 R[] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}; return R; }
    case 'S': { static const uint8 R[] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; return R; }
    case 'T': { static const uint8 R[] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; return R; }
    case 'U': { static const uint8 R[] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; return R; }
    case 'V': { static const uint8 R[] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; return R; }
    case 'W': { static const uint8 R[] = {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}; return R; }
    case 'X': { static const uint8 R[] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}; return R; }
    case 'Y': { static const uint8 R[] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; return R; }
    case 'Z': { static const uint8 R[] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}; return R; }
    case '-': { static const uint8 R[] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}; return R; }
    case '\'': { static const uint8 R[] = {0x04,0x04,0x08,0x00,0x00,0x00,0x00}; return R; }
    default: return nullptr;
    }
}

void DrawText3D(FCockpitGeometry& G, const FString& S, FVector Origin, double Height)
{
    const double Pix = Height / 7.0;
    FVector Cursor = Origin;
    for (int32 I = 0; I < S.Len(); ++I)
    {
        const TCHAR C = S[I];
        if (C == ' ') { Cursor.Y += Pix * 4.0; continue; }
        if (const uint8* Rows = GlyphRows(C))
        {
            for (int32 R = 0; R < 7; ++R)
                for (int32 Col = 0; Col < 5; ++Col)
                    if (Rows[R] & (0x10 >> Col))
                        G.Peg(Cursor + FVector(0, (Col - 2.0) * Pix, (3.0 - R) * Pix), Pix * 0.40, Pix * 0.40, Pix * 0.55);
        }
        Cursor.Y += Pix * 6.2;
    }
}

double TextWidth3D(const FString& S, double Height)
{
    const double Pix = Height / 7.0;
    double W = 0;
    for (int32 I = 0; I < S.Len(); ++I)
        W += S[I] == ' ' ? Pix * 4.0 : Pix * 6.2;
    return W;
}

UTexture2D* LoadBannerPng(const FString& Name)
{
    const FString Path = FPaths::ProjectContentDir() / TEXT("Data/hud") / Name;
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() < 8)
        return nullptr;
    IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
    if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num()))
        return nullptr;
    TArray<uint8> Raw;
    if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
        return nullptr;
    UTexture2D* Texture = UTexture2D::CreateTransient(Wrapper->GetWidth(), Wrapper->GetHeight(), PF_B8G8R8A8);
    if (!Texture) return nullptr;
    Texture->Filter = TF_Nearest;
    Texture->SRGB = true;
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;
    Texture->NeverStream = true;
    FTexturePlatformData* Platform = Texture->GetPlatformData();
    if (!Platform || Platform->Mips.Num() == 0) return nullptr;
    void* Mip = Platform->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(Mip, Raw.GetData(), Raw.Num());
    Platform->Mips[0].BulkData.Unlock();
    Texture->UpdateResource();
    return Texture;
}

FString BannerKey(const FString& TrackName)
{
    if (TrackName.Contains(TEXT("AUTRICHE"))) return TEXT("autriche");
    if (TrackName.Contains(TEXT("FRANCE"))) return TEXT("france");
    if (TrackName.Contains(TEXT("ROYAUME")) || TrackName.Contains(TEXT("ANGLETERRE"))) return TEXT("royaume-uni");
    if (TrackName.Contains(TEXT("SU"))) return TEXT("suede");
    if (TrackName.Contains(TEXT("USA"))) return TEXT("usa");
    return TEXT("japon");
}

void MiniCar(FCockpitGeometry& G, FVector C)
{
    G.Peg(C, 0.62, 0.13, 0.22);
    G.Peg(C + FVector(-0.04, 0, 0.10), 0.22, 0.08, 0.12);
    for (double Y : {-0.42, 0.42})
        for (double Z : {-0.16, 0.16})
            G.Disc(C + FVector(0.02, Y, Z), 0.11, 0.08, 8);
}
}

void AVroomCar::BuildCockpit()
{
    if (CockpitRoot) return;
    CockpitRoot = NewObject<USceneComponent>(this, TEXT("CockpitRoot"));
    CockpitRoot->SetupAttachment(RootComponent);
    CockpitRoot->SetRelativeLocation(FVector(0, 0, 62));
    AddInstanceComponent(CockpitRoot); CockpitRoot->RegisterComponent();
    SteeringPivot = NewObject<USceneComponent>(this, TEXT("SteeringPivot"));
    SteeringPivot->SetupAttachment(CockpitRoot);
    SteeringPivot->SetRelativeLocation(WheelCenter);
    AddInstanceComponent(SteeringPivot); SteeringPivot->RegisterComponent();
    auto Mesh = [&](const TCHAR* Name, USceneComponent* Parent)
    {
        auto* C = NewObject<UVroomProceduralMeshComponent>(this, Name);
        C->SetupAttachment(Parent);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCanEverAffectNavigation(false);
        C->SetCastShadow(false); C->SetVisibleInRayTracing(false);
        AddInstanceComponent(C); C->RegisterComponent(); CockpitMeshes.Add(C);
        return C;
    };

    auto* WheelMesh = Mesh(TEXT("SteeringWheel"), SteeringPivot);
    FCockpitGeometry Rim; Rim.Torus(WheelRadius, WheelTube); Rim.Commit(WheelMesh, 0, TEXT("M_WheelNavy"));
    FCockpitGeometry Metal, Hub, Rivets;
    for (double Angle : {0.0, double(UE_PI), UE_PI * 1.5})
    {
        const FQuat Q(FVector::ForwardVector, Angle);
        Metal.Prism({{2.5, -1.0}, {17.75, -.72}, {17.75, .72}, {2.5, 1.0}}, .75, FVector(.2, 0, 0), Q);
        for (double Y : {8.2, 12.0})
            Hub.Ellipsoid(Q.RotateVector(FVector(-.22, Y, 0)), FVector(.045, 1.35, .32), Q, 6, 12);
    }
    Metal.Disc(FVector(-.15, 0, 0), 3.55, 1.0);
    Metal.Commit(WheelMesh, 1, TEXT("M_F1Carbon"));
    Hub.Disc(FVector(-.87, 0, 0), 2.56, .55);
    Hub.Torus(WheelRadius - .30, .035, FVector(-WheelTube + .06, 0, 0));
    Hub.Commit(WheelMesh, 2, TEXT("M_Dashboard"));
    for (int32 I = 0; I < 6; ++I)
    {
        const double A = UE_TWO_PI * I / 6;
        Rivets.Disc(FVector(-.83, FMath::Cos(A) * 2.95, FMath::Sin(A) * 2.95), .22, .18, 8);
    }
    Rivets.Disc(FVector(-1.22, 0, 0), .48, .2, 6); Rivets.Commit(WheelMesh, 3, TEXT("M_BrushedMetal"));
    FCockpitGeometry Marker;
    Marker.Torus(WheelRadius, WheelTube + .035, FVector::ZeroVector, UE_PI * .484, UE_PI * .032, 5);
    Marker.Commit(WheelMesh, 4, TEXT("M_Red"));

    auto* LeftHandMesh = Mesh(TEXT("LeftDrivingGlove"), CockpitRoot);
    auto* RightHandMesh = Mesh(TEXT("RightDrivingGlove"), CockpitRoot);
    FString HandData;
    TSharedPtr<FJsonObject> Glove;
    if (FFileHelper::LoadFileToString(HandData, *(FPaths::ProjectContentDir() / TEXT("Data/gloves.json")))
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(HandData), Glove) && Glove.IsValid())
    {
        const auto& Positions = Glove->GetArrayField(TEXT("vertices"));
        const auto& Normals = Glove->GetArrayField(TEXT("normals"));
        const auto& UVs = Glove->GetArrayField(TEXT("uvs"));
        const auto& Indices = Glove->GetArrayField(TEXT("indices"));
        for (float Side : {-1.f, 1.f})
        {
            FCockpitGeometry Hands;
            const int32 Start = 0;
            for (int32 I = 0; I < Positions.Num(); ++I)
            {
                const auto& P = Positions[I]->AsArray();
                const auto& N = Normals[I]->AsArray();
                const auto& UV = UVs[I]->AsArray();
                const FVector Local = FVector(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber()) - GloveCuff;
                Hands.Vertex(FVector(Local.X, Local.Y, -Side * Local.Z),
                    FVector(N[0]->AsNumber(), N[1]->AsNumber(), -Side * N[2]->AsNumber()),
                    FVector2D(UV[0]->AsNumber(), UV[1]->AsNumber()), FVector::RightVector);
            }
            for (int32 I = 0; I + 2 < Indices.Num(); I += 3)
                Hands.Triangle(Start + int32(Indices[I]->AsNumber()), Start + int32(Indices[I + 1]->AsNumber()),
                    Start + int32(Indices[I + 2]->AsNumber()));
        // Preserve the authored smooth normals, deriving tangent frames from the
        // original UVs after mirroring, so leather normals light both hands correctly.
        TArray<FVector> U, V;
        U.Init(FVector::ZeroVector, Hands.Vertices.Num()); V.Init(FVector::ZeroVector, Hands.Vertices.Num());
        for (int32 I = 0; I < Hands.Triangles.Num(); I += 3)
        {
            const int32 A = Hands.Triangles[I], B = Hands.Triangles[I + 1], C = Hands.Triangles[I + 2];
            const FVector E1 = Hands.Vertices[B] - Hands.Vertices[A], E2 = Hands.Vertices[C] - Hands.Vertices[A];
            const FVector2D T1 = Hands.UVs[B] - Hands.UVs[A], T2 = Hands.UVs[C] - Hands.UVs[A];
            const double D = T1.X * T2.Y - T1.Y * T2.X;
            if (FMath::Abs(D) < 1.e-10) continue;
            const FVector TU = (E1 * T2.Y - E2 * T1.Y) / D, TV = (E2 * T1.X - E1 * T2.X) / D;
            for (int32 J : {A, B, C}) { U[J] += TU; V[J] += TV; }
        }
        for (int32 I = 0; I < Hands.Vertices.Num(); ++I)
        {
            const FVector N = Hands.Normals[I];
            const FVector T = (U[I] - N * FVector::DotProduct(N, U[I])).GetSafeNormal();
            Hands.Tangents[I] = FProcMeshTangent(T, FVector::DotProduct(FVector::CrossProduct(N, T), V[I]) < 0);
        }
            Hands.Commit(Side < 0 ? LeftHandMesh : RightHandMesh, 0, TEXT("M_RacingGlove"));
            (Side < 0 ? LeftHandMesh : RightHandMesh)->SetTextureForceResidentFlag(true);
        }
        UE_LOG(LogTemp, Display, TEXT("Gloves loaded: %d vertices, %d triangles"), Positions.Num() * 2, Indices.Num() * 2 / 3);
    }
    else UE_LOG(LogTemp, Error, TEXT("Unable to load Data/gloves.json"));

    auto* DashMesh = Mesh(TEXT("InstrumentBar"), CockpitRoot);
    DashMesh->SetRelativeTransform(FTransform(InstrumentTilt, InstrumentCenter, FVector(.52)));
    FCockpitGeometry Bezel, Faces, Markings, InstrumentTrim;
    InstrumentTrim.Panel(FVector(.25, 0, 0), 31.05, 7.55, 2.7, 2.2);
    for (double Side : {-1.0, 1.0})
        for (double Height : {-4.8, 4.8})
            InstrumentTrim.Disc(FVector(-1.55, Side * 27.5, Height), .36, .10, 10);
    InstrumentTrim.Commit(DashMesh, 3, TEXT("M_BrushedMetal"));
    Bezel.Panel(FVector(0, 0, 0), 30.5, 7.0, 2.8, 2.0);
    Bezel.Commit(DashMesh, 0, TEXT("M_CockpitSlate"));
    Faces.Panel(FVector(-1.49, 0, 0), 29.3, 5.85, .15, 1.4);
    Faces.Panel(FVector(-1.68, 12, .7), 9.0, 3.7, .16, .3);
    Faces.Commit(DashMesh, 1, TEXT("M_GaugeFace"));
    // The original groups the H gate, speed and horizontal RPM strip around the rim.
    Markings.Line(FVector(-1.86, -22, 0), FVector(-1.86, -10, 0), .18);
    for (int32 I = 0; I < 3; ++I)
    {
        const double Y = -22.0 + I * 6.0;
        Markings.Line(FVector(-1.86, Y, -2.75), FVector(-1.86, Y, 2.75), .18);
        Markings.Digit(I * 2 + 1, FVector(-1.86, Y, 3.8), 1.15);
        Markings.Digit(I * 2 + 2, FVector(-1.86, Y, -3.8), 1.15);
    }
    // Small engraved KM/H strokes; unit labels stay fixed, all values are live.
    auto UnitLine = [&](double Y1, double Z1, double Y2, double Z2)
    { Markings.Line(FVector(-1.85, Y1, Z1), FVector(-1.85, Y2, Z2), .075); };
    UnitLine(9.7, -3.9, 9.7, -2.75); UnitLine(9.7, -3.33, 10.4, -2.75); UnitLine(9.7, -3.33, 10.4, -3.9);
    UnitLine(11.0, -3.9, 11.0, -2.75); UnitLine(11.0, -2.75, 11.45, -3.3); UnitLine(11.45, -3.3, 11.9, -2.75); UnitLine(11.9, -2.75, 11.9, -3.9);
    UnitLine(12.45, -3.9, 12.85, -2.75); UnitLine(13.4, -3.9, 13.4, -2.75); UnitLine(14.2, -3.9, 14.2, -2.75); UnitLine(13.4, -3.33, 14.2, -3.33);
    Markings.Commit(DashMesh, 2, TEXT("M_GaugeMark"));

    for (int32 I = 0; I < 2; ++I)
    {
        auto* Arm = Mesh(I ? TEXT("RightDrivingSleeve") : TEXT("LeftDrivingSleeve"), CockpitRoot);
        const float Side = I ? 1.f : -1.f;
        SleeveGeometry(Side, DrivingHandPose(Side, 0.f)).Commit(Arm, 0, TEXT("M_SuitFabric"));
    }
    auto* GateMesh = Mesh(TEXT("SelectedGear"), DashMesh);
    FCockpitGeometry Gate;
    Gate.Panel(FVector::ZeroVector, .56, .48, .08, .15);
    Gate.Commit(GateMesh, 0, TEXT("M_GaugeNeedle"));
    auto* DigitsMesh = Mesh(TEXT("LiveInstrumentDigits"), DashMesh);
    InstrumentDigits(0).Commit(DigitsMesh, 0, TEXT("M_GaugeMark"));

    auto* SurroundMesh = Mesh(TEXT("CockpitMonocoque"), CockpitRoot);
    FCockpitGeometry Carbon, Trim, Coaming, Fasteners;
    // Low side rails and a shallow coaming, never a tall modern dashboard.
    Carbon.Panel(FVector(123, 0, 43), 37, 3.5, 6, 2.5);
    // A low carbon top and rounded padding give the original red surround
    // a finished edge without raising it into the road view.
    Coaming.Panel(FVector(119.7, 0, 43.8), 35.8, 2.55, .5, 1.8);
    Trim.Tube({FVector(118.7, -35, 46.4), FVector(118.7, 0, 46.4), FVector(118.7, 35, 46.4)}, {.45, .45, .45});
    for (float Side : {-1.f, 1.f})
    {
        const FVector N(0, Side, .35);
        Carbon.Face({FVector(25, Side * 36, 20), FVector(130, Side * 36, 44),
            FVector(143, Side * 43, 35), FVector(20, Side * 43, 12)}, N);
        Carbon.Face({FVector(25, Side * 36, 20), FVector(130, Side * 36, 44),
            FVector(128, Side * 33, 38), FVector(25, Side * 33, 15)}, -N);
        Coaming.Face({FVector(25, Side * 36, 20.2), FVector(130, Side * 36, 44.2),
            FVector(130, Side * 39, 43.2), FVector(25, Side * 39, 18.2)}, FVector::UpVector);
        Trim.Tube({FVector(25, Side * 35.7, 21), FVector(80, Side * 35.7, 34), FVector(129, Side * 35.7, 44.4)}, {.7, .7, .6});
        for (double Y : {28.5, 33.5})
            Fasteners.Disc(FVector(119.32, Side * Y, 43.9), .28, .11, 10);
    }
    Carbon.Commit(SurroundMesh, 0, TEXT("M_F1Paint")); Trim.Commit(SurroundMesh, 1, TEXT("M_Dashboard"));
    Coaming.Commit(SurroundMesh, 2, TEXT("M_F1Carbon")); Fasteners.Commit(SurroundMesh, 3, TEXT("M_BrushedMetal"));
    auto* RpmMesh = Mesh(TEXT("EngineRpmStrip"), DashMesh);
    RpmSegments(24, 0, 16).Commit(RpmMesh, 0, TEXT("M_GaugeGreen"));
    RpmSegments(24, 16, 20).Commit(RpmMesh, 1, TEXT("M_GaugeAmber"));
    RpmSegments(24, 20, 24).Commit(RpmMesh, 2, TEXT("M_GaugeNeedle"));
    auto* VisorMesh = Mesh(TEXT("CockpitWindshield"), CockpitRoot);
    FCockpitGeometry Glass, Edge;
    Glass.Visor(); Glass.Commit(VisorMesh, 0, TEXT("M_VisorGlass"));
    TArray<FVector> Lip; TArray<double> LipRadii;
    for (int32 I = 0; I <= 32; ++I)
    {
        const double U = -1.0 + 2.0 * I / 32;
        Lip.Add(FVector(137 - 8 * U * U, 37 * U, 23 + 33 * FMath::Sqrt(FMath::Max(0.0, 1 - U * U))));
        LipRadii.Add(.16);
    }
    Edge.Tube(Lip, LipRadii, FVector::ForwardVector, 8, 2); Edge.Commit(VisorMesh, 1, TEXT("M_GloveBlue"));
    (void)UpdateCockpit(static_cast<float>(Drive.State.Steering));
}

bool AVroomCar::IsCockpitVisible() const
{
    const bool Blink = Contacts.State.InvulnerabilityRemaining > 0
        && FMath::Fmod(Contacts.State.InvulnerabilityRemaining, .2) < .08;
    return !ChaseCamera && !Blink;
}

float AVroomCar::UpdateCockpit(float Steering)
{
    if (!CockpitRoot || !SteeringPivot || CockpitMeshes.Num() != CockpitPartCount) return 0.f;
    // Blend out neutral continuously: crossing the old hard cutoff jumped the
    // wheel by 11 degrees and also moved the camera roll and both wrists.
    const float TargetSteering = Steering * FMath::SmoothStep(0.f, CockpitHandDeadzone, FMath::Abs(Steering));
    const float SmoothSteeringDt = Circuit && Circuit->Paused ? 0.f
        : FMath::Max(0.f, GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f);
    const float SteeringAlpha = 1.f - FMath::Exp(-CockpitSteeringSmoothing * SmoothSteeringDt);
    CockpitSteering = FMath::Lerp(CockpitSteering, TargetSteering, SteeringAlpha);
    const bool Visible = IsCockpitVisible();
    if (CockpitRoot->IsVisible() != Visible) CockpitRoot->SetVisibility(Visible, true);
    if (!Visible) return FMath::Clamp(CockpitSteering, -1.f, 1.f);
    const float DisplaySteering = FMath::Clamp(CockpitSteering / CockpitSteeringInputMax, -1.f, 1.f);
    const FQuat Turn(FVector::ForwardVector, FMath::DegreesToRadians(-DisplaySteering * CockpitSteeringVisualAngleDeg()));
    SteeringPivot->SetRelativeRotation(Turn);
    const float HandSteering = DisplaySteering * CockpitSteeringInputMax;
    for (int32 I = 0; I < 2; ++I)
    {
        auto* Arm = CockpitMeshes[LeftForearm + I].Get();
        const float Side = I ? 1.f : -1.f;
        const FDrivingHandPose Hand = DrivingHandPose(Side, HandSteering);
        // Keep the grip attached even when it moves below the camera crop.
        const bool HandVisible = Visible;
        CockpitMeshes[LeftGlove + I]->SetVisibility(HandVisible);
        Arm->SetVisibility(HandVisible);
        CockpitMeshes[LeftGlove + I]->SetRelativeTransform(FTransform(Hand.Rotation, Hand.Wrist, FVector(Hand.Scale)));
        const auto& Previous = Arm->GetCustomPrimitiveData().Data;
        if (Previous.Num() < 1 || FMath::Abs(Previous[0] - HandSteering) > 0.00001f)
        {
            const FCockpitGeometry G = SleeveGeometry(Side, Hand);
            Arm->UpdateMeshSection_LinearColor(0, G.Vertices, G.Normals, G.UVs, G.Colors, G.Tangents);
            // The cuff must follow the wrist every visible frame. Quantizing
            // this to 0.10 made the sleeve detach and then snap back in turns.
            Arm->SetCustomPrimitiveDataFloat(0, HandSteering);
        }
    }
    const bool Disabled = Contacts.State.Phase == Vroom::ContactPhase::Crashed || Contacts.State.Phase == Vroom::ContactPhase::Recovering;
    const int32 Gear = FMath::Clamp(Drive.State.Gear, 1, 6);
    CockpitMeshes[GearIndicator]->SetRelativeLocation(FVector(-2.02,
        Disabled ? -16.0 : -22.0 + ((Gear - 1) / 2) * 6.0, Disabled ? 0.0 : (Gear % 2 ? 2.6 : -2.6)));
    const int32 Speed = FMath::Clamp(FMath::RoundToInt(Drive.State.SpeedMps * 3.6), 0, 999);
    auto* DigitsMesh = CockpitMeshes[Readout].Get();
    // Component-local caches avoid rebuilding small display buffers when unchanged.
    const auto& DisplayData = DigitsMesh->GetCustomPrimitiveData().Data;
    if (DisplayData.Num() == 0 || DisplayData[0] != Speed)
    {
        const FCockpitGeometry Digits = InstrumentDigits(Speed);
        DigitsMesh->UpdateMeshSection_LinearColor(0, Digits.Vertices, Digits.Normals, Digits.UVs, Digits.Colors, Digits.Tangents);
        DigitsMesh->SetCustomPrimitiveDataFloat(0, static_cast<float>(Speed));
    }
    const int32 Lit = Disabled ? 0 : FMath::Clamp(FMath::RoundToInt(Drive.State.Rpm / Vroom::DriveModel::MaxRpm * 24), 0, 24);
    auto* RpmMesh = CockpitMeshes[RpmStrip].Get();
    const auto& RpmData = RpmMesh->GetCustomPrimitiveData().Data;
    if (RpmData.Num() == 0 || RpmData[0] != Lit)
    {
        const int32 First[] = {0, 16, 20}, Last[] = {16, 20, 24};
        for (int32 I = 0; I < 3; ++I)
        {
            const FCockpitGeometry G = RpmSegments(Lit, First[I], Last[I]);
            RpmMesh->UpdateMeshSection_LinearColor(I, G.Vertices, G.Normals, G.UVs, G.Colors, G.Tangents);
        }
        RpmMesh->SetCustomPrimitiveDataFloat(0, static_cast<float>(Lit));
    }
    return FMath::Clamp(CockpitSteering, -1.f, 1.f);
}

void AVroomCar::BuildBanner()
{
    if (BannerRoot || !Camera) return;
    BannerRoot = NewObject<USceneComponent>(this, TEXT("BannerRoot"));
    BannerRoot->SetupAttachment(Camera);
    AddInstanceComponent(BannerRoot);
    BannerRoot->RegisterComponent();
    auto Mesh = [&](const TCHAR* Name)
    {
        auto* C = NewObject<UVroomProceduralMeshComponent>(this, Name);
        C->SetupAttachment(BannerRoot);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCanEverAffectNavigation(false);
        C->SetCastShadow(false);
        C->SetVisibleInRayTracing(false);
        C->SetBoundsScale(8.f);
        AddInstanceComponent(C);
        C->RegisterComponent();
        BannerMeshes.Add(C);
        return C;
    };

    auto* Housing = Mesh(TEXT("BannerHousing"));
    FCockpitGeometry Body, Wells;
    // Positive depth points toward the camera. Keep the housing behind its
    // displays; previously its front face occluded the map and title.
    Body.Panel(HudPx(160, 34, -0.65), 16.05, 3.42, 1.35, 0.42);
    Body.Panel(HudPx(110, 34, 0.40), 0.92, 2.65, 0.85, 0.85);
    Body.Commit(Housing, 0, TEXT("M_HudTeal"));
    Wells.Panel(HudPx(54, 35, 0.08), 4.85, 2.85, 0.42, 0.28);
    Wells.Panel(HudPx(208, 48, 0.10), 8.8, 1.58, 0.32, 0.18);
    Wells.Panel(HudPx(310, 40, 0.10), 0.48, 2.45, 0.32, 0.10);
    Wells.Commit(Housing, 1, TEXT("M_Dark"));

    Mesh(TEXT("BannerMap"));
    auto* Lamps = Mesh(TEXT("BannerLamps"));
    FCockpitGeometry Green, Amber, Red;
    Green.Disc(HudPx(110, 52, 1.15), 0.52, 0.28, 16);
    Green.Commit(Lamps, 0, TEXT("M_GaugeGreen"));
    Red.Disc(HudPx(110, 16, 1.15), 0.52, 0.28, 16);
    Red.Commit(Lamps, 1, TEXT("M_GaugeNeedle"));
    Amber.Disc(HudPx(110, 34, 1.15), 0.52, 0.28, 16);
    Amber.Commit(Lamps, 2, TEXT("M_GaugeAmber"));

    FCockpitGeometry Title;
    DrawText3D(Title, TEXT("GRAND PRIX"), HudPx(168, 8, 1.05), 0.75);
    Title.Commit(Mesh(TEXT("BannerTitle")), 0, TEXT("M_HudGold"));
    FCockpitGeometry Digits;
    DrawText3D(Digits, TEXT("SCORE"), HudPx(128, 36, 1.0), 0.72);
    Digits.Commit(Mesh(TEXT("BannerDigits")), 0, TEXT("M_HudCyan"));
    Mesh(TEXT("BannerCars"));
    Mesh(TEXT("BannerFuel"));
    FCockpitGeometry Blip;
    Blip.Disc(FVector::ZeroVector, 0.18, 0.10, 8);
    Blip.Commit(Mesh(TEXT("BannerBlip")), 0, TEXT("M_White"));
    BannerTrackKey.Empty();
    BannerScoreDrawn = BannerCarsDrawn = BannerStageDrawn = BannerLapDrawn = -1;
    BannerFuelDrawn = -1;
}

void AVroomCar::UpdateBanner()
{
    if (!BannerRoot || !Camera || BannerMeshes.Num() != BannerPartCount) return;
    const bool Visible = Circuit && Circuit->Running;
    if (BannerRoot->IsVisible() != Visible) BannerRoot->SetVisibility(Visible, true);
    if (!Visible) return;

    // Camera::FieldOfView is not the vertical FOV. Read the actual projection
    // after Unreal's viewport/aspect-axis constraints, including F10 changes.
    const double HorizontalScale = 1.0 / FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView * .5));
    FVector2D Projection(HorizontalScale, HorizontalScale * 1.6);
    if (const auto* PC = Cast<APlayerController>(GetController()))
        if (const auto* Local = PC->GetLocalPlayer(); Local && Local->ViewportClient && Local->ViewportClient->Viewport)
        {
            FSceneViewProjectionData Data;
            if (Local->GetProjectionData(Local->ViewportClient->Viewport, Data))
                Projection = FVector2D(Data.ProjectionMatrix.M[0][0], Data.ProjectionMatrix.M[1][1]);
        }
    if (Projection.X <= 0 || Projection.Y <= 0) return;
    // Fit the whole 320x68 layout without stretching its map, text or lamps.
    // These bounds include extrusion and the bottom row of passed-car icons.
    // Shallow physical depth keeps the fascia readable without the large
    // perspective/parallax of a thick box only 34 cm from the camera.
    constexpr double D = 60.0, HalfWidth = 16.10, HalfHeight = 3.48;
    constexpr double DepthRatio = .08, FrontDepth = 1.40 * DepthRatio;
    constexpr double Edge = .985, HeightFraction = .34;
    const double WidthScale = D * Edge / (Projection.X * HalfWidth + Edge * FrontDepth);
    const double HeightScale = D * (2 * HeightFraction)
        / (Projection.Y * (2 * HalfHeight) + 2 * HeightFraction * FrontDepth);
    const double Scale = FMath::Min(WidthScale, HeightScale);
    const double NearestDepth = D - FrontDepth * Scale;
    const double Top = Edge * NearestDepth / Projection.Y;
    BannerRoot->SetRelativeLocation(FVector(D, 0, Top - HalfHeight * Scale));
    BannerRoot->SetRelativeRotation(FRotator::ZeroRotator);
    BannerRoot->SetRelativeScale3D(FVector(Scale * DepthRatio, Scale, Scale));

    const FVroomTrack* Track = Circuit->CurrentTrack();
    const FString TrackName = Track ? Track->Name.ToUpper() : TEXT("CIRCUIT");
    const FString Key = BannerKey(TrackName);
    if (Key != BannerTrackKey)
    {
        BannerTrackKey = Key;
        BannerMapTexture = LoadBannerPng(FString::Printf(TEXT("map_%s.png"), *Key));
        auto* MapMesh = BannerMeshes[BannerMap].Get();
        FCockpitGeometry Map;
        // Keep the full 96x58 texture inside the raised lamp-column edge.
        Map.Quad(HudPx(53, 35, 0.48), 4.55, 4.55 * 58.0 / 96.0, 0.02);
        Map.Commit(MapMesh, 0, TEXT("M_HudUnlit"));
        FVector2D MapMin(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
        FVector2D MapMax(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
        for (int32 I = 0; I < 300; ++I)
        {
            const FVector P = Circuit->Position(I * Circuit->LengthM() / 300) / 100.0f;
            MapMin.X = FMath::Min(MapMin.X, P.X); MapMin.Y = FMath::Min(MapMin.Y, P.Y);
            MapMax.X = FMath::Max(MapMax.X, P.X); MapMax.Y = FMath::Max(MapMax.Y, P.Y);
        }
        MapMesh->SetCustomPrimitiveDataFloat(0, MapMin.X);
        MapMesh->SetCustomPrimitiveDataFloat(1, MapMin.Y);
        MapMesh->SetCustomPrimitiveDataFloat(2, MapMax.X);
        MapMesh->SetCustomPrimitiveDataFloat(3, MapMax.Y);
        if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_HudUnlit.M_HudUnlit")))
        {
            BannerMapMaterial = UMaterialInstanceDynamic::Create(Base, this);
            if (BannerMapMaterial && BannerMapTexture)
                BannerMapMaterial->SetTextureParameterValue(TEXT("Texture"), BannerMapTexture);
            if (BannerMapMaterial)
                MapMesh->SetMaterial(0, BannerMapMaterial);
        }
        // English names and circuit labels visible in the six Amiga banners.
        static const TCHAR* Countries[] = {TEXT("AUSTRIA"), TEXT("FRANCE"), TEXT("THE USA"),
            TEXT("SWEDEN"), TEXT("JAPAN"), TEXT("ENGLAND")};
        static const TCHAR* Circuits[] = {TEXT("ZELTWEG"), TEXT("CASTELLET"), TEXT("WATKINS GLEN"),
            TEXT("ANDERSTORP"), TEXT("FUJI YAMA"), TEXT("SILVERSTONE")};
        const int32 ReferenceIndex = Track ? FMath::Clamp(Track->Id - 1, 0, 5) : 0;
        const FString CircuitLine = Circuits[ReferenceIndex];
        FCockpitGeometry Title;
        const FString Line1 = FString::Printf(TEXT("GRAND PRIX OF %s"), Countries[ReferenceIndex]);
        const double H1 = 0.75, H2 = 0.70;
        DrawText3D(Title, Line1, HudPx(216.5, 8, 1.05) - FVector(0, TextWidth3D(Line1, H1) * 0.5, 0), H1);
        DrawText3D(Title, CircuitLine, HudPx(216.5, 20, 1.05) - FVector(0, TextWidth3D(CircuitLine, H2) * 0.5, 0), H2);
        Title.Commit(BannerMeshes[BannerTitle].Get(), 0, TEXT("M_HudGold"));
        BannerScoreDrawn = -1;
    }

    const int32 ScoreNow = FMath::Clamp(this->Score, 0, 99999);
    const int32 Stage = Circuit->TrackIndex + 1;
    const int32 LapN = FMath::Max(0, Lap - 1);
    const bool Disabled = Contacts.State.Phase == Vroom::ContactPhase::Crashed
        || Contacts.State.Phase == Vroom::ContactPhase::Recovering;
    const auto& DigitsData = BannerMeshes[BannerDigits]->GetCustomPrimitiveData().Data;
    const bool DisabledChanged = DigitsData.Num() == 0 || DigitsData[0] != (Disabled ? 1.f : 0.f);
    if (ScoreNow != BannerScoreDrawn || Stage != BannerStageDrawn || LapN != BannerLapDrawn || DisabledChanged)
    {
        BannerScoreDrawn = ScoreNow;
        BannerStageDrawn = Stage;
        BannerLapDrawn = LapN;
        FCockpitGeometry Digits;
        auto Number = [&](int32 Value, int32 Width, FVector Origin, double Height)
        {
            FString S;
            for (int32 I = Width - 1; I >= 0; --I)
            {
                int32 Div = 1;
                for (int32 K = 0; K < I; ++K) Div *= 10;
                S.AppendChar(TCHAR('0' + (Value / Div) % 10));
            }
            DrawText3D(Digits, S, Origin, Height);
        };
        DrawText3D(Digits, TEXT("SCORE"), HudPx(126, 36, 1.02), 0.70);
        Number(ScoreNow, 5, HudPx(160, 36, 1.02), 0.78);
        DrawText3D(Digits, TEXT("STAGE"), HudPx(204, 36, 1.02), 0.70);
        Number(Stage, 1, HudPx(238, 36, 1.02), 0.78);
        DrawText3D(Digits, TEXT("LAP"), HudPx(252, 36, 1.02), 0.70);
        Number(LapN, 1, HudPx(274, 36, 1.02), 0.78);
        if (Disabled)
            DrawText3D(Digits, TEXT("SPIN"), HudPx(200, 54, 1.05), 0.80);
        Digits.Commit(BannerMeshes[BannerDigits].Get(), 0, TEXT("M_HudCyan"));
        BannerMeshes[BannerDigits]->SetCustomPrimitiveDataFloat(0, Disabled ? 1.f : 0.f);
    }

    const int32 Shown = FMath::Clamp(CarsPassed, 0, 27);
    if (Shown != BannerCarsDrawn)
    {
        BannerCarsDrawn = Shown;
        FCockpitGeometry Cars, EmptySlots;
        const bool Qualify = Shown >= 9;
        for (int32 I = 0; I < 27; ++I)
            MiniCar(I < Shown ? Cars : EmptySlots, HudPx(136 + (I % 9) * 16, 46 + (I / 9) * 7, 1.08));
        Cars.Commit(BannerMeshes[BannerCars].Get(), 0, Qualify ? TEXT("M_GaugeNeedle") : TEXT("M_GaugeGreen"));
        EmptySlots.Commit(BannerMeshes[BannerCars].Get(), 1, TEXT("M_HudTeal"));
    }
    BannerMeshes[BannerCars]->SetVisibility(!Disabled);

    const float FuelNow = static_cast<float>(Drive.Fuel);
    if (FMath::Abs(FuelNow - BannerFuelDrawn) > 0.02f)
    {
        BannerFuelDrawn = FuelNow;
        FCockpitGeometry FuelG;
        const double Fill = 24.0 * FMath::Clamp(Drive.Fuel, 0.0, 1.0);
        FuelG.Peg(HudPx(310, 52.0 - Fill * 0.5, 0.85), 0.22, Fill * 0.05, 0.18);
        FuelG.Commit(BannerMeshes[BannerFuel].Get(), 0,
            Drive.Fuel < 0.22 ? TEXT("M_GaugeNeedle") : TEXT("M_GaugeGreen"));
    }

    if (Track && Track->Points.Num() >= 2)
    {
        const auto& MapBounds = BannerMeshes[BannerMap]->GetCustomPrimitiveData().Data;
        if (MapBounds.Num() < 4) return;
        const FVector2D Min(MapBounds[0], MapBounds[1]), Max(MapBounds[2], MapBounds[3]);
        const FVector P = Circuit->Position(Drive.State.DistanceM) / 100.0f;
        const float MapScale = FMath::Min(90.f / FMath::Max(1.0, Max.X - Min.X),
            52.f / FMath::Max(1.0, Max.Y - Min.Y));
        const FVector2D Mid = (Min + Max) * 0.5;
        const double Px = 54.0 + (P.X - Mid.X) * MapScale;
        const double Py = 35.0 - (P.Y - Mid.Y) * MapScale;
        // Moving one rigid dot needs a transform, not a new render proxy and
        // material lookup on every frame.
        BannerMeshes[BannerBlip]->SetRelativeLocation(
            HudPx(FMath::Clamp(Px, 12.0, 96.0), FMath::Clamp(Py, 10.0, 60.0), 1.12));
    }
}
