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
// Driver-eye composition: compact wheel in the opening, real nose and wheels ahead.
const FVector WheelCenter(40, 0, 50);
constexpr double WheelScale = .85;
const FQuat WheelRestTilt(FVector::RightVector, FMath::DegreesToRadians(-25.0));
constexpr double WheelRadius = 14.0 * WheelScale; // classic round rim, shared with both grip anchors
// Period instruments are fixed to the chassis, behind the three-spoke wheel.
const FVector InstrumentCenter(47, 0, 55.5);
const FQuat InstrumentTilt = WheelRestTilt;
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
    // Grip centres and hand orientations share the wheel's exact rotation.
    // The natural lower crop is preserved when a wrist turns below the view.
    const double Turn = -FMath::Clamp(double(Steering / CockpitSteeringInputMax), -1.0, 1.0)
        * CockpitSteeringVisualAngleDeg();
    static double NeutralAngle = 0.0, GripDepth = -2.8, GripReach = 19.1, Scale = .92, WristTilt = 80.0;
    static double HandDepthOffset = -.4;
    static bool Init = false;
    if (!Init)
    {
        // Retain the authored white glove anatomy and local grip anchor;
        // neutral angle and scale belong to this compact wheel's layout.
        FString Data;
        TSharedPtr<FJsonObject> Document;
        if (FFileHelper::LoadFileToString(Data, *(FPaths::ProjectContentDir() / TEXT("Data/gloves.json")))
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Data), Document) && Document.IsValid())
        {
            const TSharedPtr<FJsonObject>* Pose;
            if (Document->TryGetObjectField(TEXT("driving_grip"), Pose))
            {
                // Preserve the established 9-and-3 grip and authored wrist anatomy.
                (*Pose)->TryGetNumberField(TEXT("wrist_tilt_deg"), WristTilt);
                // The runtime rim and hand scale remain independent of the legacy bake.
                (*Pose)->TryGetNumberField(TEXT("grip_depth_cm"), GripDepth);
                (*Pose)->TryGetNumberField(TEXT("grip_reach_cm"), GripReach);
                (*Pose)->TryGetNumberField(TEXT("hand_depth_offset_cm"), HandDepthOffset);
            }
        }
        FParse::Value(FCommandLine::Get(), TEXT("VroomGripAngle="), NeutralAngle);
        FParse::Value(FCommandLine::Get(), TEXT("VroomGripDepth="), GripDepth);
        FParse::Value(FCommandLine::Get(), TEXT("VroomGripReach="), GripReach);
        FParse::Value(FCommandLine::Get(), TEXT("VroomGripScale="), Scale);
        FParse::Value(FCommandLine::Get(), TEXT("VroomWristTilt="), WristTilt);
        FParse::Value(FCommandLine::Get(), TEXT("VroomHandDepthOffset="), HandDepthOffset);
        HandDepthOffset = FMath::Clamp(HandDepthOffset, -1.0, 1.0);
        Init = true;
    }
    const double Angle = FMath::DegreesToRadians((Side < 0 ? 180.0 - NeutralAngle : NeutralAngle) + Turn);
    const FVector Contact = WheelCenter + WheelRestTilt.RotateVector(FVector(0, FMath::Cos(Angle), FMath::Sin(Angle)) * WheelRadius);
    // Canonical -X is the back of the glove, -Y points toward the knuckles,
    // and Z spans the four fingers. Align that span with the rim tangent.
    const FQuat Rotation = WheelRestTilt * FQuat(FVector::ForwardVector, Angle - UE_PI)
        * FQuat(FVector::UpVector, FMath::DegreesToRadians(WristTilt));
    const FVector Grip = FVector(GripDepth, GripReach, -5.4) - GloveCuff;
    const FVector MirroredGrip(Grip.X, Grip.Y, -Side * Grip.Z);
    // Screen depth is the wheel's X axis in Unreal (Z is vertical). Retreat
    // the whole glove toward the driver, without changing wrist/finger pose.
    // This axis is invariant under wheel rotation, including for the right hand.
    return {Contact - Rotation.RotateVector(MirroredGrip) * Scale + WheelRestTilt.RotateVector(FVector(HandDepthOffset, 0, 0)), Rotation, Scale};
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
    void ClassicRim()
    {
        constexpr int32 Segments=160, TubeSegments=16;
        TArray<FVector> Centers, U, V;
        TArray<double> Radii;
        for (int32 I=0; I<Segments; ++I)
        {
            const double A=UE_TWO_PI*I/Segments;
            // A true circular rim. Hand clearance belongs to the glove pose,
            // never to dents or local changes in the steering-wheel radius.
            Centers.Add(FVector(-.4,14.0*FMath::Cos(A),14.0*FMath::Sin(A)));
            Radii.Add(.90);
        }
        for (int32 I=0; I<Segments; ++I)
        {
            const FVector Along=(Centers[(I+1)%Segments]-Centers[(I+Segments-1)%Segments]).GetSafeNormal();
            U.Add((FVector::ForwardVector-Along*Along.X).GetSafeNormal());
            V.Add(FVector::CrossProduct(Along,U.Last()).GetSafeNormal());
        }
        auto Point=[&](int32 I,double A)
        {
            return Centers[I]+(U[I]*FMath::Sin(A)+V[I]*FMath::Cos(A))*Radii[I];
        };
        const int32 Start=Vertices.Num();
        for (int32 Row=0; Row<=Segments; ++Row)
        {
            const int32 I=Row%Segments;
            for (int32 J=0; J<=TubeSegments; ++J)
            {
                const double A=UE_TWO_PI*J/TubeSegments;
                const FVector Along=Point((I+1)%Segments,A)-Point((I+Segments-1)%Segments,A);
                const FVector Around=U[I]*FMath::Cos(A)-V[I]*FMath::Sin(A);
                Vertex(Point(I,A),FVector::CrossProduct(Along,Around),
                    FVector2D(double(Row)/Segments,double(J)/TubeSegments),Along);
            }
        }
        Grid(Start,Segments,TubeSegments);
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
        const FVector N = Normal.GetSafeNormal();
        const FVector Seed = FMath::Abs(N.Y) < .9 ? FVector::RightVector : FVector::ForwardVector;
        const FVector T = (Seed - N * FVector::DotProduct(Seed,N)).GetSafeNormal();
        const FVector B = FVector::CrossProduct(N,T);
        for (const FVector& P : Points)
            Vertex(P,N,FVector2D(FVector::DotProduct(P,T),FVector::DotProduct(P,B))*.1,T);
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
    // A machined lip catches light around the instrument case and wheel spokes.
    // Separate faces retain the hard metal edge without a paper-thin silhouette.
    void BeveledPrism(const TArray<FVector2D>& Outline, double Depth, double Bevel,
        FVector Center = FVector::ZeroVector)
    {
        double Radius = 0;
        for (const FVector2D& P : Outline) Radius = FMath::Max(Radius, P.Size());
        const double Inset = 1.0 - Bevel / FMath::Max(1.0, Radius);
        TArray<FVector> Rings[4];
        const double X[] = {-Depth*.5, -Depth*.5+Bevel, Depth*.5-Bevel, Depth*.5};
        for (int32 R=0; R<4; ++R)
            for (const FVector2D& P : Outline)
                Rings[R].Add(Center + FVector(X[R], P.X*((R==0 || R==3)?Inset:1),
                    P.Y*((R==0 || R==3)?Inset:1)));
        Face(Rings[0], -FVector::ForwardVector);
        Face(Rings[3], FVector::ForwardVector);
        for (int32 R=0; R<3; ++R)
            for (int32 I=0; I<Outline.Num(); ++I)
            {
                const int32 J=(I+1)%Outline.Num();
                const FVector N=FVector::CrossProduct(Rings[R][J]-Rings[R][I],
                    Rings[R+1][I]-Rings[R][I]).GetSafeNormal();
                Face({Rings[R][I],Rings[R+1][I],Rings[R+1][J],Rings[R][J]},N);
            }
    }
    // Curved outer shoulder of the monocoque, replacing the large flat diagonal
    // polygon. Its upper seam follows the unchanged rolled cockpit opening.
    void CockpitShoulder(double Side)
    {
        constexpr int32 Rows=36, Columns=8;
        const int32 Start=Vertices.Num();
        auto P=[&](double T,double V)
        {
            const double X=-55+117*T;
            const double YTop=34-10*T, ZTop=43+12*T;
            const double YBottom=46-13*T, ZBottom=27+13*T;
            const double Curve=V*V;
            return FVector(X,Side*FMath::Lerp(YTop,YBottom,V),
                FMath::Lerp(ZTop,ZBottom,Curve));
        };
        for (int32 I=0; I<=Rows; ++I)
            for (int32 J=0; J<=Columns; ++J)
            {
                const double T=double(I)/Rows, V=double(J)/Columns;
                const FVector Along=P(T+.001,V)-P(T-.001,V);
                const FVector Across=P(T,V+.001)-P(T,V-.001);
                Vertex(P(T,V),FVector::CrossProduct(Along,Across)*Side,
                    FVector2D(T*2,V),Along);
            }
        Grid(Start,Rows,Columns);
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
        FVector2D WristHalfSize,
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
            const double Width = FMath::Lerp(5.0, WristHalfSize.X, T) + Fold + ExtraRadius;
            const double Thickness = FMath::Lerp(3.5, WristHalfSize.Y, T) + Fold + ExtraRadius;
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
                const FVector Along(-8 * U, 23, -5 * U / FMath::Max(.05, Arch) * V);
                const FVector Up(1, 0, 5 * Arch);
                Vertex(FVector(66 - 4 * U * U + V, 23 * U, 56 + 5 * Arch * V),
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
    // Match the authored glove's cuff, rather than its convenient mesh pivot.
    // The section at canonical Y=28.5 sits inside the closed cuff end, hiding
    // the seam without moving the hand or changing its grip on the wheel.
    const auto Mirror = [Side](FVector P) { return FVector(P.X, P.Y, -Side * P.Z); };
    const FVector Cuff = Hand.Wrist + Hand.Rotation.RotateVector(
        Mirror(FVector(-4.655988, 28.5, -5.452675) - GloveCuff)) * Hand.Scale;
    const FVector Tangent = Hand.Rotation.RotateVector(
        Mirror(FVector(-.066228, -.995841, .062564))).GetSafeNormal();
    const FVector Across = Hand.Rotation.RotateVector(FVector(0,0,-Side));
    FCockpitGeometry G;
    // Slightly inset the terminal ellipse under the irregular glove rim;
    // retain the substantial forearm volume toward the elbow.
    G.Sleeve(FVector(-15, Side * 20, 20), Cuff, Tangent, Across,
        FVector2D(3.705883, 2.7314) * (Hand.Scale * .94));
    return G;
}

FCockpitGeometry InstrumentDigits(int32 Speed)
{
    FCockpitGeometry G;
    for (int32 I = 0; I < 3; ++I)
    {
        const int32 Divisor = I == 0 ? 100 : I == 1 ? 10 : 1;
        G.Digit((Speed / Divisor) % 10, FVector(-1.95, -.2 + I * 1.55, .25), 1.8);
    }
    return G;
}

FCockpitGeometry RpmSegments(int32 Lit, int32 First, int32 Last)
{
    FCockpitGeometry G;
    for (int32 I = First; I < Last; ++I)
    {
        const FVector A(-2.0, -6.0 + I * .50, 4.2);
        G.Line(A, I < Lit ? A + FVector(0, .34, 0) : A, I < Lit ? .28 : 0);
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
    CockpitRoot->SetRelativeLocation(FVector::ZeroVector);
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
    WheelMesh->SetRelativeScale3D(FVector(WheelScale));
    FCockpitGeometry Rim, Plate, Hub, Rivets, RedButtons, AmberButtons;
    // A 1990-style suede rim and three simple spokes. The same contact radius
    // drives the gloves; changing visual style never disconnects their grip.
    Rim.ClassicRim();
    Rim.Commit(WheelMesh, 0, TEXT("M_WheelNavy"));
    // Each spoke is convex, so the panel fan never fills the open apertures.
    Plate.BeveledPrism({{-12.7,-1.6},{-2.4,-2.1},{-2.4,2.1},{-12.7,1.6}},.45,.08);
    Plate.BeveledPrism({{2.4,-2.1},{12.7,-1.6},{12.7,1.6},{2.4,2.1}},.45,.08);
    Plate.BeveledPrism({{-3.0,-1.5},{-2.1,-13.0},{2.1,-13.0},{3.0,-1.5}},.45,.08);
    // Small rear paddles fit the period semi-automatic gearbox.
    for (double Side : {-1.0,1.0})
        Plate.Panel(FVector(2.1,Side*8.8,-.7),1.0,3.3,.35,.55);
    Plate.Commit(WheelMesh, 1, TEXT("M_WheelMetal"));
    Hub.Disc(FVector(1.4,0,0),2.45,2.2,48);
    Hub.Disc(FVector(-.5,0,0),2.75,.6,48);
    for (int32 I=0; I<6; ++I)
    {
        const double A=UE_TWO_PI*I/6;
        Rivets.Disc(FVector(-.86,FMath::Cos(A)*2.05,FMath::Sin(A)*2.05),.17,.13,12);
    }
    for (double Side : {-1.0,1.0})
    {
        Hub.Panel(FVector(-.7,Side*5.3,2.5),.9,1.05,.65,.18);
        RedButtons.Panel(FVector(-1.10,Side*5.3,2.5),.6,.75,.18,.12);
    }
    Hub.Commit(WheelMesh,2,TEXT("M_Dashboard"));
    Rivets.Commit(WheelMesh,3,TEXT("M_BrushedMetal"));
    RedButtons.Commit(WheelMesh,4,TEXT("M_GaugeNeedle"));
    // A plain period centre marker, without a borrowed manufacturer badge.
    AmberButtons.Disc(FVector(-.87,0,0),1.13,.08,32);
    AmberButtons.Commit(WheelMesh,5,TEXT("M_GaugeAmber"));

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
    DashMesh->SetRelativeTransform(FTransform(InstrumentTilt, InstrumentCenter, FVector::OneVector));
    FCockpitGeometry Bezel, Faces, Markings, InstrumentMetal, InstrumentTrim;
    Bezel.BeveledPrism({{-17,-4.4},{17,-4.4},{18,-2.5},{17,3.9},{13,5.2},
        {-13,5.2},{-17,3.9},{-18,-2.5}},1.4,.22);
    // Recessed LCD gasket and a small rain lip, like a separate period instrument.
    InstrumentTrim.Panel(FVector(-.76,0,0),7.55,3.18,.22,.40);
    InstrumentTrim.Panel(FVector(-.85,0,4.65),12.2,.32,.7,.22);
    // A matte hood has depth at the leading edge, with a curved outer lip.
    Bezel.Panel(FVector(-1.55,0,5.25),14.4,.34,2.4,.25);
    InstrumentTrim.Tube({{-2.6,-14.4,4.6},{-2.8,-13.6,5.35},{-2.8,13.6,5.35},{-2.6,14.4,4.6}},
        {.10,.12,.12,.10},FVector::ForwardVector,12,3);
    InstrumentTrim.Commit(DashMesh,3,TEXT("M_WheelMetal"));
    Bezel.Commit(DashMesh,0,TEXT("M_Dashboard"));
    Faces.Panel(FVector(-.92,0,0),7.2,2.85,.12,.35);
    for (double Side : {-1.0,1.0})
    {
        for (double Z : {-2.8,2.9})
        {
            InstrumentMetal.Disc(FVector(-.78,Side*15,Z),.23,.10,16);
            Faces.Line(FVector(-.85,Side*15-.13,Z),FVector(-.85,Side*15+.13,Z),.045);
        }
        // Toggle collars and levers sit in the panel instead of floating lines.
        InstrumentMetal.Disc(FVector(-.82,Side*11.8,-.5),.48,.17,20);
        Faces.Disc(FVector(-.94,Side*11.8,-.5),.33,.10,20);
        InstrumentMetal.Tube({{-1.0,Side*11.8,-.5},{-1.8,Side*11.8,.25}},
            {.12,.16},FVector::RightVector,10,1);
        Markings.Line(FVector(-.95,Side*11.8-1.0,1.1),FVector(-.95,Side*11.8+1.0,1.1),.10);
        // Recessed lenses and machined collars, kept dark instead of showing
        // permanent warning lamps for systems the game does not simulate.
        for (double Y : {8.8, 10.7})
        {
            InstrumentMetal.Disc(FVector(-.83,Side*Y,3.05),.43,.16,20);
            Faces.Disc(FVector(-.95,Side*Y,3.05),.30,.13,20);
        }
    }
    DrawText3D(Markings,TEXT("KMH"),FVector(-1.95,.1,-1.35),.60);
    Markings.Line(FVector(-1.95,-2.9,-1.9),FVector(-1.95,-2.9,1.9),.07);
    Faces.Commit(DashMesh,1,TEXT("M_GaugeFace"));
    Markings.Commit(DashMesh,2,TEXT("M_GaugeMark"));
    InstrumentMetal.Commit(DashMesh,4,TEXT("M_BrushedMetal"));

    for (int32 I = 0; I < 2; ++I)
    {
        auto* Arm = Mesh(I ? TEXT("RightDrivingSleeve") : TEXT("LeftDrivingSleeve"), CockpitRoot);
        const float Side = I ? 1.f : -1.f;
        SleeveGeometry(Side, DrivingHandPose(Side, 0.f)).Commit(Arm, 0, TEXT("M_SuitFabric"));
    }
    auto* GateMesh = Mesh(TEXT("SelectedGear"), DashMesh);
    FCockpitGeometry Gate;
    Gate.Digit(1, FVector(-1.95,-4.7,0), 3.6);
    Gate.Commit(GateMesh, 0, TEXT("M_GaugeMark"));
    auto* DigitsMesh = Mesh(TEXT("LiveInstrumentDigits"), DashMesh);
    InstrumentDigits(0).Commit(DigitsMesh, 0, TEXT("M_GaugeMark"));

    auto* SurroundMesh = Mesh(TEXT("CockpitMonocoque"), CockpitRoot);
    FCockpitGeometry Carbon, Trim, Coaming, Fasteners;
    // Thin rolled bodywork and an inset padded lip, rather than separate
    // oversized tubes. The front pad joins both sides below the windscreen.
    Carbon.Panel(FVector(63,0,51.5),24,4.5,3,2.5);
    Coaming.Panel(FVector(60.9,0,52),22,2.5,.6,1.5);
    Trim.Tube({{-55,-31,44},{-15,-29,47},{25,-26,51},{59,-22.5,54},
        {62,0,55.2},{59,22.5,54},{25,26,51},{-15,29,47},{-55,31,44}},
        {1.0,1.0,.9,.7,.65,.7,.9,1.0,1.0},FVector::UpVector,16,8);
    for (double Side : {-1.0,1.0})
    {
        TArray<FVector> Rail{{-55,Side*34,43},{-15,Side*32,46},{25,Side*29,50},{62,Side*24,55}};
        Carbon.Tube(Rail,{1.2,1.2,1.1,.9},FVector::UpVector,24,10);
        Carbon.CockpitShoulder(Side);
        Coaming.Face({{-55,Side*31,42},{59,Side*22.5,53},{55,Side*22,35},{-55,Side*31,26}}, FVector(0,-Side,0));
        Fasteners.Disc(FVector(60.5,Side*19.7,52.8),.19,.1,12);
        // Inset padded seams follow the cockpit opening; no hand or steering
        // geometry is moved. The black piping stops below the windscreen.
        Trim.Tube({{-42,Side*30.5,43.8},{-15,Side*29,47},{25,Side*26,51},{52,Side*23.7,53.6}},
            {.26,.26,.22,.16},FVector::UpVector,12,6);
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
        Lip.Add(FVector(67 - 4 * U * U, 23 * U, 56 + 5 * FMath::Sqrt(FMath::Max(0.0, 1 - U * U))));
        LipRadii.Add(.07);
    }
    Edge.Tube(Lip, LipRadii, FVector::ForwardVector, 8, 2); Edge.Commit(VisorMesh, 1, TEXT("M_BrushedMetal"));
    (void)UpdateCockpit(static_cast<float>(Drive.State.Steering));
}

bool AVroomCar::IsCockpitVisible() const
{
    const bool Blink = Contacts.State.InvulnerabilityRemaining > 0
        && FMath::Fmod(Contacts.State.InvulnerabilityRemaining, .2) < .08;
    // The cockpit is part of the car in every view: the exterior camera sees
    // the same steering wheel, white gloves, instruments and mirror housings.
    return !Blink;
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
    SteeringPivot->SetRelativeRotation(WheelRestTilt * Turn);
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
    auto* GearMesh = CockpitMeshes[GearIndicator].Get();
    const int32 DisplayGear = Disabled ? 0 : Gear;
    const auto& GearData = GearMesh->GetCustomPrimitiveData().Data;
    if (GearData.Num() == 0 || GearData[0] != DisplayGear)
    {
        FCockpitGeometry G; G.Digit(DisplayGear,FVector(-1.95,-4.7,0),3.6);
        GearMesh->UpdateMeshSection_LinearColor(0,G.Vertices,G.Normals,G.UVs,G.Colors,G.Tangents);
        GearMesh->SetCustomPrimitiveDataFloat(0,DisplayGear);
    }
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
