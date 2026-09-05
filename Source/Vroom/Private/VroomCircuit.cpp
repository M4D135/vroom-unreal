#include "VroomGame.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
UMaterialInterface* Mat(const TCHAR* Name)
{
    const FString Path = FString::Printf(TEXT("/Game/Materials/%s.%s"), Name, Name);
    return LoadObject<UMaterialInterface>(nullptr, *Path);
}
UStaticMesh* Shape(const TCHAR* Name)
{
    const FString Path = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name);
    return LoadObject<UStaticMesh>(nullptr, *Path);
}
}

AVroomCircuit::AVroomCircuit()
{
    PrimaryActorTick.bCanEverTick = true;
    Spline = CreateDefaultSubobject<USplineComponent>(TEXT("CircuitSpline"));
    RootComponent = Spline;
    Surface = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Asphalt"));
    Surface->SetupAttachment(RootComponent);
    Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

bool AVroomCircuit::LoadTracks()
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/tracks.json"))))
    {
        UE_LOG(LogTemp, Error, TEXT("VROOM: Missing Content/Data/tracks.json"));
        return false;
    }
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return false;
    const TArray<TSharedPtr<FJsonValue>>* Rows;
    if (!Root->TryGetArrayField(TEXT("tracks"), Rows)) return false;
    for (const auto& Row : *Rows)
    {
        const auto O = Row->AsObject();
        if (!O.IsValid()) continue;
        FVroomTrack Track;
        Track.Name = O->GetStringField(TEXT("name"));
        Track.Id = O->GetIntegerField(TEXT("id"));
        Track.SourceLengthM = O->GetNumberField(TEXT("length_m"));
        for (const auto& V : O->GetArrayField(TEXT("preview_points_m")))
        {
            const auto& A = V->AsArray();
            if (A.Num() >= 3) Track.Points.Add(FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()));
        }
        if (Track.Points.Num() >= 4) Tracks.Add(MoveTemp(Track));
    }
    return !Tracks.IsEmpty();
}

void AVroomCircuit::BeginPlay()
{
    Super::BeginPlay();
    if (LoadTracks()) SelectTrack(0);
}

float AVroomCircuit::LengthM() const { return FMath::Max(1.f, Spline->GetSplineLength() / 100.f); }
const FVroomTrack* AVroomCircuit::CurrentTrack() const { return Tracks.IsValidIndex(TrackIndex) ? &Tracks[TrackIndex] : nullptr; }
FVector AVroomCircuit::Position(float S, float Lateral) const
{
    const float Distance = FMath::Fmod(FMath::Fmod(S, LengthM()) + LengthM(), LengthM()) * 100;
    return Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World)
        + Spline->GetRightVectorAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World) * Lateral * 100;
}
FRotator AVroomCircuit::Rotation(float S) const
{
    const float Distance = FMath::Fmod(FMath::Fmod(S, LengthM()) + LengthM(), LengthM()) * 100;
    return Spline->GetRotationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
}
float AVroomCircuit::Curvature(float S) const
{
    return FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(Rotation(S - 4).Yaw, Rotation(S + 4).Yaw)) / 8.f;
}

void AVroomCircuit::SelectTrack(int32 Index)
{
    if (!Tracks.IsValidIndex(Index)) return;
    TrackIndex = Index;
    RaceClock = 0;
    const auto& Track = Tracks[Index];
    // Remove staircase corners from the tiny HUD bitmap. This is a preview
    // interpretation; it does not recover the original road projection.
    TArray<FVector> Smoothed = Track.Points;
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        TArray<FVector> Next; Next.SetNum(Smoothed.Num());
        for (int32 I = 0; I < Smoothed.Num(); ++I)
        {
            FVector Sum = FVector::ZeroVector;
            for (int32 J = -3; J <= 3; ++J)
                Sum += Smoothed[(I + J + Smoothed.Num()) % Smoothed.Num()] * (4 - FMath::Abs(J));
            Next[I] = Sum / 16;
        }
        Smoothed = MoveTemp(Next);
    }
    Spline->ClearSplinePoints(false);
    for (const auto& P : Smoothed) Spline->AddSplinePoint(P * 100, ESplineCoordinateSpace::Local, false);
    Spline->SetClosedLoop(true, false);
    for (int32 I = 0; I < Track.Points.Num(); ++I) Spline->SetSplinePointType(I, ESplinePointType::CurveClamped, false);
    Spline->UpdateSpline();
    const double DistanceScale = Track.SourceLengthM / LengthM();
    for (int32 I = 0; I < Smoothed.Num(); ++I)
        Spline->SetLocationAtSplinePoint(I, Smoothed[I] * (100 * DistanceScale), ESplineCoordinateSpace::Local, false);
    Spline->UpdateSpline();
    // The preview derives from the source minimap, not yet the original projection tables.
    BuildSurface();
    BuildScenery();
    BuildOpponents();
    UE_LOG(LogTemp, Display, TEXT("VROOM: Circuit %s ready, preview %.1fm, source %.1fm"), *Track.Name, LengthM(), Track.SourceLengthM);
}

void AVroomCircuit::BuildSurface()
{
    Surface->ClearAllMeshSections();
    const int32 Count = FMath::CeilToInt(LengthM() / 5.f);
    auto Ribbon = [&](int32 Section, float Left, float Right, float Z, const TCHAR* Material, bool Alternating)
    {
        TArray<FVector> V, Normals;
        TArray<int32> T;
        TArray<FVector2D> UV;
        TArray<FLinearColor> Colors;
        TArray<FProcMeshTangent> Tangents;
        for (int32 I = 0; I <= Count; ++I)
        {
            const float S = I * LengthM() / Count;
            const float Grey = Alternating && (I / 2) % 2 ? .13f : 1.f;
            V.Add(Position(S, Left) + FVector(0, 0, Z));
            V.Add(Position(S, Right) + FVector(0, 0, Z));
            for (int32 J = 0; J < 2; ++J)
            {
                Normals.Add(FVector::UpVector);
                UV.Add(FVector2D(J, S / 6));
                Colors.Add(FLinearColor(Grey, Grey, Grey, 1));
                Tangents.Add(FProcMeshTangent(Rotation(S).Vector(), false));
            }
            if (I < Count)
            {
                const int32 K = I * 2;
                T.Append({K, K + 2, K + 1, K + 1, K + 2, K + 3});
            }
        }
        Surface->CreateMeshSection_LinearColor(Section, V, T, Normals, UV, Colors, Tangents, false);
        Surface->SetMaterial(Section, Mat(Material));
    };
    Ribbon(0, -7.5f, 7.5f, 1, TEXT("M_Gravel"), false);
    Ribbon(1, -HalfWidthM, HalfWidthM, 4, TEXT("M_Asphalt"), false);
    Ribbon(2, -HalfWidthM - .7f, -HalfWidthM, 5, TEXT("M_Curb"), false);
    Ribbon(3, HalfWidthM, HalfWidthM + .7f, 5, TEXT("M_Curb"), false);
    Ribbon(4, -HalfWidthM + .12f, -HalfWidthM + .25f, 6, TEXT("M_White"), false);
    Ribbon(5, HalfWidthM - .25f, HalfWidthM - .12f, 6, TEXT("M_White"), false);
}

void AVroomCircuit::BuildScenery()
{
    for (auto C : Scenery) if (C) C->DestroyComponent();
    Scenery.Empty();
    auto Group = [&](const TCHAR* Mesh, const TCHAR* Material)
    {
        auto C = NewObject<UInstancedStaticMeshComponent>(this);
        C->SetupAttachment(RootComponent);
        C->SetStaticMesh(Shape(Mesh));
        C->SetMaterial(0, Mat(Material));
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->RegisterComponent();
        Scenery.Add(C);
        return C;
    };
    auto Grass = Group(TEXT("Cube"), TEXT("M_Grass"));
    auto Rail = Group(TEXT("Cube"), TEXT("M_Metal"));
    auto Posts = Group(TEXT("Cube"), TEXT("M_Dark"));
    auto White = Group(TEXT("Cube"), TEXT("M_White"));
    auto Red = Group(TEXT("Cube"), TEXT("M_Red"));
    auto Trees = Group(TEXT("Cone"), TEXT("M_Foliage"));
    auto Trunks = Group(TEXT("Cylinder"), TEXT("M_Wood"));
    FBox Box(ForceInit);
    for (const auto& P : Tracks[TrackIndex].Points) Box += P * 100;
    const FVector Center = Box.GetCenter();
    const FVector Extent = Box.GetExtent() + FVector(140000, 140000, 0);
    Grass->AddInstance(FTransform(FRotator::ZeroRotator, FVector(Center.X, Center.Y, -110), FVector(Extent.X / 50, Extent.Y / 50, 2)), true);
    FRandomStream Random(1991 + TrackIndex);
    for (float S = 0; S < LengthM(); S += 12)
    {
        for (float Side : {-1.f, 1.f})
        {
            FVector A = Position(S, Side * 10);
            FVector B = Position(S + 12, Side * 10);
            auto Rot = (B - A).Rotation();
            const FVector Mid = (A + B) * .5f;
            Rail->AddInstance(FTransform(Rot, Mid + FVector(0, 0, 65), FVector((B - A).Size() / 100 + .03, .1, .25)), true);
            Posts->AddInstance(FTransform(Rot, A + FVector(0, 0, 40), FVector(.12, .14, .8)), true);
            if (static_cast<int32>(S) % 36 == 0)
            {
                auto TreePoint = Position(S, Side * Random.FRandRange(28, 110));
                const float Key = Spline->FindInputKeyClosestToWorldLocation(TreePoint);
                if ((TreePoint - Spline->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World)).Size() > 2000)
                {
                    const float H = Random.FRandRange(7, 15);
                    Trees->AddInstance(FTransform(FRotator(0, Random.FRandRange(0, 360), 0), TreePoint + FVector(0, 0, H * 65), FVector(H * .55, H * .55, H)), true);
                    Trunks->AddInstance(FTransform(FRotator::ZeroRotator, TreePoint + FVector(0, 0, H * 30), FVector(.35, .35, H * .6)), true);
                }
            }
        }
    }
    // Start line, grid boxes, pit structures and trackside distance markers.
    for (int32 I = 0; I < 16; ++I)
    {
        for (int32 J = 0; J < 2; ++J)
        {
            auto* G = (I + J) % 2 ? Posts : White;
            G->AddInstance(FTransform(Rotation(0), Position(J * .65f, -5.625f + I * .75f) + FVector(0,0,7), FVector(.65f,.75f,.02f)), true);
        }
    }
    for (int32 I = 0; I < 12; ++I)
    {
        const float S = 80 + I * 8;
        const FRotator Rot = Rotation(S);
        White->AddInstance(FTransform(Rot, Position(S, 22) + FVector(0,0,250), FVector(7.5,12,5)), true);
        Red->AddInstance(FTransform(Rot, Position(S, 22) + FVector(0,0,510), FVector(7.7,12.2,.25)), true);
        Posts->AddInstance(FTransform(Rot, Position(S, 15.95) + FVector(0,0,150), FVector(5,.08,2.8)), true);
    }
    for (int32 I = 0; I < 2; ++I)
    {
        Posts->AddInstance(FTransform(Rotation(0), Position(0, I ? 9 : -9) + FVector(0,0,350), FVector(.5,.5,7)), true);
    }
    Red->AddInstance(FTransform(Rotation(0), Position(0) + FVector(0,0,710), FVector(.55,18.5,1.2)), true);
}

void AVroomCircuit::BuildOpponents()
{
    for (auto C : CarParts) if (C) C->DestroyComponent();
    CarParts.Empty();
    OpponentS.Empty(); OpponentLanes.Empty(); OpponentSpeeds.Empty();
    for (int32 I = 0; I < 12; ++I)
    {
        OpponentS.Add(40 + (I / 2) * 16);
        OpponentLanes.Add(I % 2 ? 2.2 : -2.2);
        OpponentSpeeds.Add(0);
        for (int32 J = 0; J < 10; ++J)
        {
            auto C = NewObject<UStaticMeshComponent>(this);
            C->SetupAttachment(RootComponent);
            C->SetStaticMesh(Shape(J >= 6 ? TEXT("Cylinder") : TEXT("Cube")));
            C->SetMaterial(0, Mat(J >= 6 ? TEXT("M_Dark") : (J == 4 ? TEXT("M_Black") : (I % 3 ? TEXT("M_White") : TEXT("M_Red")))));
            C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            C->RegisterComponent();
            CarParts.Add(C);
        }
    }
}

void AVroomCircuit::Tick(float Dt)
{
    Super::Tick(Dt);
    Dt = FMath::Min(Dt, .1f);
    if (Tracks.IsEmpty()) return;
    if (Running && !Paused) RaceClock += Dt;
    static const FVector Offsets[] = { {0,0,46}, {155,0,35}, {190,0,20}, {-170,0,90}, {-38,0,85}, {-35,0,50}, {120,-90,35}, {120,90,35}, {-115,-95,38}, {-115,95,38} };
    static const FVector Scales[] = { {3.6,.8,.42}, {1.45,.35,.3}, {.3,1.95,.1}, {.45,1.9,.1}, {.65,.58,.5}, {1.8,1.5,.45}, {.7,.7,.38}, {.7,.7,.38}, {.76,.76,.45}, {.76,.76,.45} };
    for (int32 I = 0; I < OpponentS.Num(); ++I)
    {
        if (Running && !Paused && RaceClock >= 0)
        {
            const float K = FMath::Max(FMath::Abs(Curvature(OpponentS[I] + 25)), FMath::Abs(Curvature(OpponentS[I] + 90)));
            const float Desired = FMath::Clamp(FMath::Sqrt(16.f / FMath::Max(K, .001f)), 14.f, 68.f + I);
            OpponentSpeeds[I] = FMath::FInterpConstantTo(OpponentSpeeds[I], Desired, Dt, Desired < OpponentSpeeds[I] ? 12 : 5);
            OpponentS[I] += OpponentSpeeds[I] * Dt;
        }
        const FTransform Base(Rotation(OpponentS[I]), Position(OpponentS[I], OpponentLanes[I]));
        for (int32 J = 0; J < 10; ++J)
        {
            auto C = CarParts[I * 10 + J];
            C->SetWorldTransform(FTransform(J >= 6 ? FRotator(0,0,90) : FRotator::ZeroRotator, Offsets[J], Scales[J]) * Base);
        }
    }
}
