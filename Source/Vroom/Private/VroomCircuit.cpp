#include "VroomGame.h"
#include "VroomProceduralMeshComponent.h"
#include "VroomTrackPreview.h"
#include "VroomEnvironment.h"
#include "VroomPlayerSettings.h"
#include "VroomRaceRules.h"
#include "TimerManager.h"
#include "VroomGraphicsSettings.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundWaveProcedural.h"

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

int32 OpponentPartGroup(int32, int32 PartIndex)
{
    // Every car has one instance in each complete-surface group: painted
    // body, white livery, carbon/suspension and the four detailed wheels.
    return PartIndex;
}

FTransform OpponentPartTransform(int32)
{
    return FTransform::Identity; // Mesh positions are authored at full car scale.
}
FTransform OpponentWishboneTransform(const Vroom::Wishbone& Bar)
{
    const FVector Chassis(Bar.ChassisX, Bar.ChassisY, Bar.ChassisZ);
    const FVector Hub(Bar.HubX, Bar.HubY, Bar.HubZ);
    const FVector Delta = Hub - Chassis;
    const float Length = Delta.Size();
    FTransform Pose;
    Pose.SetLocation((Chassis + Hub) * 0.5f);
    Pose.SetScale3D(FVector(0.038f, 0.038f, FMath::Max(0.02f, Length * 0.01f)));
    if (Length > 1.f)
        Pose.SetRotation(FQuat::FindBetweenNormals(FVector::UpVector, Delta / Length));
    return Pose;
}
}

AVroomCircuit::AVroomCircuit()
{
    PrimaryActorTick.bCanEverTick = false;
    Spline = CreateDefaultSubobject<USplineComponent>(TEXT("CircuitSpline"));
    RootComponent = Spline;
    Surface = CreateDefaultSubobject<UVroomProceduralMeshComponent>(TEXT("Asphalt"));
    Surface->SetupAttachment(RootComponent);
    Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Terrain = CreateDefaultSubobject<UVroomProceduralMeshComponent>(TEXT("CircuitTerrain"));
    Terrain->SetupAttachment(RootComponent);
    Terrain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // Start light sounds
    StartLightAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("StartLightAudio"));
    StartLightAudio->SetupAttachment(RootComponent);
    StartLightAudio->bAutoActivate = false;
    StartLightAudio->SetVolumeMultiplier(1.0f);
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
        if (O->HasField(TEXT("circuit_name")))
            Track.CircuitName = O->GetStringField(TEXT("circuit_name"));
        if (Track.CircuitName.IsEmpty())
            Track.CircuitName = Track.Name;
        Track.Id = O->GetIntegerField(TEXT("id"));
        Track.SourceLengthM = O->GetNumberField(TEXT("length_m"));
        const TSharedPtr<FJsonObject>* Profile = nullptr;
        if (O->TryGetObjectField(TEXT("preview_elevation"), Profile) && Profile && Profile->IsValid())
        {
            const TArray<TSharedPtr<FJsonValue>>* Samples = nullptr;
            const TArray<TSharedPtr<FJsonValue>>* Grades = nullptr;
            if ((*Profile)->TryGetArrayField(TEXT("samples_m"), Samples)
                && (*Profile)->TryGetArrayField(TEXT("grades_m_per_m"), Grades) && Samples->Num() == Grades->Num())
            {
                for (int32 I = 0; I < Samples->Num(); ++I)
                {
                    const auto& A = (*Samples)[I]->AsArray();
                    if (A.Num() >= 2) Track.Elevation.Samples.push_back({A[0]->AsNumber(), A[1]->AsNumber(), (*Grades)[I]->AsNumber()});
                }
            }
        }
        if (!Track.Elevation.Valid() || FMath::Abs(Track.Elevation.Samples.back().DistanceM - Track.SourceLengthM) > .01)
            Track.Elevation.Samples = {{0,0,0}, {Track.SourceLengthM,0,0}};
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

float AVroomCircuit::LengthM() const
{
    if (CachedLengthM > 0) return CachedLengthM;
    if (!Spline) return 1.f;
    return FMath::Max(1.f, Spline->GetSplineLength() / 100.f);
}

void AVroomCircuit::RefreshLengthCache()
{
    CachedLengthM = Spline ? FMath::Max(1.f, Spline->GetSplineLength() / 100.f) : 1.f;
}

void AVroomCircuit::RebuildCurvatureCache()
{
    const float Len = LengthM();
    const int32 Count = FMath::Max(16, FMath::RoundToInt(Len / 2.f));
    CurvatureSampleSpacing = Len / Count;
    CurvatureSamples.SetNum(Count);
    constexpr float Window = 12.f;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const float S = Index * CurvatureSampleSpacing;
        CurvatureSamples[Index] = FMath::DegreesToRadians(
            FMath::FindDeltaAngleDegrees(Rotation(S - Window).Yaw, Rotation(S + Window).Yaw)) / (2.f * Window);
    }
    TArray<float> Filtered = CurvatureSamples;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const float Previous = CurvatureSamples[(Index - 1 + Count) % Count];
        const float Next = CurvatureSamples[(Index + 1) % Count];
        Filtered[Index] = 0.25f * Previous + 0.5f * CurvatureSamples[Index] + 0.25f * Next;
    }
    CurvatureSamples = MoveTemp(Filtered);
    float Peak = 0;
    for (float Sample : CurvatureSamples)
        Peak = FMath::Max(Peak, FMath::Abs(Sample));
    UE_LOG(LogTemp, Display, TEXT("VROOM: Curvature peak |K|=%.4f 1/m (Rmin=%.0fm) samples=%d"),
        Peak, Peak > 1e-4f ? 1.f / Peak : 0.f, Count);
}
const FVroomTrack* AVroomCircuit::CurrentTrack() const { return Tracks.IsValidIndex(TrackIndex) ? &Tracks[TrackIndex] : nullptr; }
FVector AVroomCircuit::Position(float S, float Lateral) const
{
    const float Distance = FMath::Fmod(FMath::Fmod(S, LengthM()) + LengthM(), LengthM()) * 100;
    return Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World)
        + Spline->GetRightVectorAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World) * Lateral * 100
        + FVector(0, 0, Elevation(S).HeightM * 100);
}
FRotator AVroomCircuit::Rotation(float S) const
{
    const float Distance = FMath::Fmod(FMath::Fmod(S, LengthM()) + LengthM(), LengthM()) * 100;
    FRotator R = Spline->GetRotationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
    R.Pitch = FMath::RadiansToDegrees(std::atan(Elevation(S).Grade));
    return R;
}

FRotator AVroomCircuit::DrivingRotation(float S) const
{
    // Average a short look-ahead so residual kinks do not snap the cockpit yaw
    // onto the next straight segment. Pitch stays local so crests do not nod.
    FVector Forward = FVector::ZeroVector;
    for (int32 Step = 0; Step < 4; ++Step)
        Forward += Rotation(S + Step * 4.f).Vector();
    if (Forward.IsNearlyZero())
        return Rotation(S);
    FRotator R = Forward.GetSafeNormal().Rotation();
    R.Pitch = Rotation(S).Pitch;
    R.Roll = 0;
    return R;
}
Vroom::ElevationPose AVroomCircuit::Elevation(float S) const
{
    const auto* Track = CurrentTrack();
    return Track ? Track->Elevation.At(S * Track->SourceLengthM / LengthM()) : Vroom::ElevationPose{};
}
float AVroomCircuit::Curvature(float S) const
{
    const int32 Count = CurvatureSamples.Num();
    if (Count < 2 || CurvatureSampleSpacing <= 0)
        return 0.f;
    const float Len = LengthM();
    float Along = FMath::Fmod(S, Len);
    if (Along < 0)
        Along += Len;
    const float T = Along / CurvatureSampleSpacing;
    int32 Index = FMath::FloorToInt(T);
    const float Fraction = T - Index;
    Index = ((Index % Count) + Count) % Count;
    return FMath::Lerp(CurvatureSamples[Index], CurvatureSamples[(Index + 1) % Count], Fraction);
}

void AVroomCircuit::SelectTrack(int32 Index)
{
    if (!Tracks.IsValidIndex(Index)) return;
    TrackIndex = Index;
    RaceClock = 0;
    const auto& Track = Tracks[Index];
    // HUD pixels are ~19 m stairs with 90° jogs. Two 7-tap passes left 10–15°
    // heading snaps at each vertex, so a corner read as a chain of straights.
    std::vector<Vroom::PreviewPoint> Input;
    Input.reserve(Track.Points.Num());
    for (const FVector& Point : Track.Points)
        Input.push_back({Point.X, Point.Y, Point.Z});
    const std::vector<Vroom::PreviewPoint> Smoothed =
        Vroom::BuildDriveablePreview(Input, Track.SourceLengthM);
    Spline->ClearSplinePoints(false);
    for (const auto& Point : Smoothed)
        Spline->AddSplinePoint(FVector(Point.X, Point.Y, Point.Z) * 100, ESplineCoordinateSpace::Local, false);
    Spline->SetClosedLoop(true, false);
    for (int32 I = 0; I < Spline->GetNumberOfSplinePoints(); ++I)
        Spline->SetSplinePointType(I, ESplinePointType::Curve, false);
    Spline->UpdateSpline();
    RefreshLengthCache();
    const double DistanceScale = Track.SourceLengthM / LengthM();
    for (int32 I = 0; I < Spline->GetNumberOfSplinePoints(); ++I)
        Spline->SetLocationAtSplinePoint(I, Spline->GetLocationAtSplinePoint(I, ESplineCoordinateSpace::Local) * DistanceScale,
            ESplineCoordinateSpace::Local, false);
    Spline->UpdateSpline();
    // Fige la longueur avant les constructions qui interrogent Position/Rotation.
    RefreshLengthCache();
    RebuildCurvatureCache();
    // The preview derives from the source minimap, not yet the original projection tables.
    BuildSurface();
    BuildTerrain();
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
                Normals.Add(Rotation(S).RotateVector(FVector::UpVector));
                UV.Add(FVector2D(J, S / 6));
                Colors.Add(FLinearColor(Grey, Grey, Grey, 1));
                Tangents.Add(FProcMeshTangent(Rotation(S).Vector(), false));
            }
            if (I < Count)
            {
                const int32 K = I * 2;
                T.Append({K, K + 1, K + 2, K + 1, K + 3, K + 2});
            }
        }
        Surface->CreateMeshSection_LinearColor(Section, V, T, Normals, UV, Colors, Tangents, false);
        Surface->SetMaterial(Section, Mat(Material));
    };
    Ribbon(0, -HalfWidthM - 1.5f, HalfWidthM + 1.5f, 1, TEXT("M_Gravel"), false);
    Ribbon(1, -HalfWidthM, HalfWidthM, 4, TEXT("M_Asphalt"), false);
    Ribbon(2, -HalfWidthM - .7f, -HalfWidthM, 5, TEXT("M_Curb"), false);
    Ribbon(3, HalfWidthM, HalfWidthM + .7f, 5, TEXT("M_Curb"), false);
    Ribbon(4, -HalfWidthM + .12f, -HalfWidthM + .25f, 6, TEXT("M_White"), false);
    Ribbon(5, HalfWidthM - .25f, HalfWidthM - .12f, 6, TEXT("M_White"), false);
    Ribbon(6, -24, 24, -1, TEXT("M_Grass"), false);
}

void AVroomCircuit::BuildTerrain()
{
    Terrain->ClearAllMeshSections();
    FBox Bounds(ForceInit);
    for (int32 I = 0; I < Spline->GetNumberOfSplinePoints(); ++I)
        Bounds += Spline->GetLocationAtSplinePoint(I, ESplineCoordinateSpace::World);
    const FVector CircuitCenter = Bounds.GetCenter();
    const FVector CircuitExtent = Bounds.GetExtent();
    Bounds = Bounds.ExpandBy(FVector(65000,65000,0));
    const int32 NX = FMath::CeilToInt(Bounds.GetSize().X / 2800);
    const int32 NY = FMath::CeilToInt(Bounds.GetSize().Y / 2800);
    TArray<FVector> V, Normals;
    TArray<int32> T;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    double Minimum = 0;
    for (const auto& P : Tracks[TrackIndex].Elevation.Samples) Minimum = FMath::Min(Minimum, P.HeightM);
    const auto& Profile = Vroom::Environment::Profile(TrackIndex);
    for (int32 Y = 0; Y <= NY; ++Y)
        for (int32 X = 0; X <= NX; ++X)
        {
            FVector P(Bounds.Min.X + Bounds.GetSize().X * X / NX, Bounds.Min.Y + Bounds.GetSize().Y * Y / NY, 0);
            const float Key = Spline->FindInputKeyClosestToWorldLocation(P);
            const FVector Road = Spline->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World);
            const float S = Spline->GetDistanceAlongSplineAtSplineInputKey(Key) / 100;
            double Distance = FVector::Dist2D(P, Road) / 100;
            const FVector Right = Spline->GetRightVectorAtSplineInputKey(Key, ESplineCoordinateSpace::World);
            const double LateralM = FVector::DotProduct(P - Road, Right) / 100.0;
            const double Bend = Curvature(S);
            // Hairpin islands sit a few metres from two stretches of tarmac.
            // Treat that infield as countryside so it does not rise with the road.
            if (std::abs(Bend) > 0.008 && Bend * LateralM < 0 && Distance > HalfWidthM + 1.2 && Distance < 22.0)
                Distance = 80.0;
            P.Z = Vroom::Environment::HeightM(TrackIndex, P, Distance, Elevation(S).HeightM, Minimum) * 100;
            V.Add(P); UV.Add(FVector2D(P.X / 1600, P.Y / 1600));
            const float Variation = .92f + .11f * FMath::Sin(P.X * .00047 + P.Y * .00023)
                + .045f * FMath::Cos(P.Y * .00112);
            Colors.Add(Profile.Ground * Variation);
            if (X < NX && Y < NY)
            {
                const int32 I = Y * (NX + 1) + X;
                T.Append({I,I+NX+2,I+1,I,I+NX+1,I+NX+2});
            }
        }
    UKismetProceduralMeshLibrary::CalculateTangentsForMesh(V,T,UV,Normals,Tangents);
    Terrain->CreateMeshSection_LinearColor(0,V,T,Normals,UV,Colors,Tangents,false);
    auto* Finish = Mat(TEXT("M_EnvironmentGround"));
    Terrain->SetMaterial(0, Finish ? Finish : Mat(TEXT("M_Grass")));

    // A low-cost closed skyline follows the artistic region profile. Its base
    // sits beyond the detailed terrain, never across the driveable circuit.
    V.Reset(); T.Reset(); UV.Reset(); Colors.Reset();
    constexpr int32 Segments = 96;
    for (int32 Ring = 0; Ring < 3; ++Ring)
        for (int32 I = 0; I <= Segments; ++I)
        {
            const double Angle = UE_TWO_PI * I / Segments;
            const double Wave = .42 + .20 * FMath::Sin(Angle * 5 + TrackIndex)
                + .16 * FMath::Cos(Angle * 9 - .7) + .09 * FMath::Sin(Angle * 17);
            const double Radius = Ring == 0 ? 55000 : Ring == 1 ? 115000 : 175000;
            FVector P = CircuitCenter + FVector(FMath::Cos(Angle) * (CircuitExtent.X + Radius),
                FMath::Sin(Angle) * (CircuitExtent.Y + Radius), 0);
            double Height = Minimum - 16;
            if (Ring == 1)
            {
                Height += Profile.MountainM * Wave;
                // A broad volcanic summit gives Japan a different silhouette.
                if (TrackIndex == 4)
                {
                    const double Summit = FMath::Exp(-FMath::Square(FMath::FindDeltaAngleRadians(Angle,1.1) / .34));
                    Height += Summit * 450;
                }
            }
            P.Z = Height * 100; V.Add(P); UV.Add(FVector2D(double(I)/Segments,Ring));
            FLinearColor Shade = FLinearColor::LerpUsingHSV(Profile.Ground,FLinearColor(.15,.20,.21),.45f);
            if (Ring == 1 && Height - Minimum > 340) Shade = FLinearColor(.60,.63,.61);
            Colors.Add(Shade * float(.88 + .12 * FMath::Sin(Angle * 7)));
            if (Ring < 2 && I < Segments)
            {
                const int32 A = Ring * (Segments + 1) + I;
                T.Append({A,A+1,A+Segments+2,A,A+Segments+2,A+Segments+1});
            }
        }
    UKismetProceduralMeshLibrary::CalculateTangentsForMesh(V,T,UV,Normals,Tangents);
    Terrain->CreateMeshSection_LinearColor(1,V,T,Normals,UV,Colors,Tangents,false);
    Terrain->SetMaterial(1, Finish ? Finish : Mat(TEXT("M_Grass")));
    Terrain->SetCastShadow(false);
}

void AVroomCircuit::BuildScenery()
{
    for (auto C : Scenery) if (C) C->DestroyComponent();
    Scenery.Empty();
    Hazards.Reset();
    HazardCells.Reset();
    auto Group = [&](const TCHAR* Mesh, const TCHAR* Material)
    {
        auto C = NewObject<UInstancedStaticMeshComponent>(this);
        C->SetupAttachment(RootComponent);
        C->SetStaticMesh(Shape(Mesh));
        UMaterialInterface* Finish = Mat(Material);
        C->SetMaterial(0, Finish ? Finish : Mat(TEXT("M_White")));
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCanEverAffectNavigation(false);
        C->RegisterComponent();
        Scenery.Add(C);
        return C;
    };
    auto Grass = Group(TEXT("Cube"), TEXT("M_Grass"));
    auto Posts = Group(TEXT("Cube"), TEXT("M_Dark"));
    auto White = Group(TEXT("Cube"), TEXT("M_White"));
    auto Red = Group(TEXT("Cube"), TEXT("M_Red"));
    auto Metal = Group(TEXT("Cube"), TEXT("M_Metal"));
    auto RoundMetal = Group(TEXT("Cylinder"), TEXT("M_Metal"));
    auto Windows = Group(TEXT("Cube"), TEXT("M_CockpitSlate"));
    auto Apron = Group(TEXT("Cube"), TEXT("M_Asphalt"));
    auto EnvironmentGroup = [&](const TCHAR* Name, int32 CullDistance)
    {
        const FString Path = FString::Printf(TEXT("/Game/Meshes/%s.%s"),Name,Name);
        auto* Mesh = LoadObject<UStaticMesh>(nullptr,*Path);
        if (!Mesh) { UE_LOG(LogTemp,Error,TEXT("VROOM: Missing environment asset %s"),Name); return static_cast<UHierarchicalInstancedStaticMeshComponent*>(nullptr); }
        auto* C = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
        C->SetupAttachment(RootComponent);
        C->SetMobility(EComponentMobility::Movable);
        C->SetStaticMesh(Mesh);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCanEverAffectNavigation(false);
        C->SetCastShadow(false);
        C->SetAffectDistanceFieldLighting(false);
        C->SetAffectDynamicIndirectLighting(false);
        C->SetCullDistances(CullDistance-12000,CullDistance);
        C->SetNumCustomDataFloats(3);
        C->bAutoRebuildTreeOnInstanceChanges = false;
        C->RegisterComponent(); Scenery.Add(C); return C;
    };
    UHierarchicalInstancedStaticMeshComponent* Forest[] = {
        EnvironmentGroup(TEXT("SM_EnvironmentBroadleaf"),65000),
        EnvironmentGroup(TEXT("SM_EnvironmentConifer"),65000),
        EnvironmentGroup(TEXT("SM_EnvironmentUmbrella"),65000),
        EnvironmentGroup(TEXT("SM_EnvironmentBirch"),65000)};
    auto* Rocks = EnvironmentGroup(TEXT("SM_EnvironmentRock"),26000);
    auto Trunks = Group(TEXT("Cylinder"), TEXT("M_Wood"));
    UInstancedStaticMeshComponent* Ads[] = {
        Group(TEXT("Plane"), TEXT("M_Ad01")), Group(TEXT("Plane"), TEXT("M_Ad02")),
        Group(TEXT("Plane"), TEXT("M_Ad03")), Group(TEXT("Plane"), TEXT("M_Ad04"))};
    StartLights = Group(TEXT("Cylinder"), TEXT("M_StartLight"));
    StartLights->ComponentTags.Add(FName(TEXT("VroomStartLights")));
    StartLights->SetNumCustomDataFloats(1);
    StartLightStage = -1;
    StartSignalPlayed = -2;

    // All structures use metre-sized primitives; their placements are in cm.
    // Buildings remain upright instead of banking with the road spline.
    auto Frame = [&](float S, float L)
    {
        return FTransform(FRotator(0, Rotation(S).Yaw, 0), Position(S, L));
    };
    auto LocalBox = [&](UInstancedStaticMeshComponent* G, const FTransform& F,
                        FVector OffsetM, FVector SizeM)
    {
        G->AddInstance(FTransform(FQuat::Identity, OffsetM * 100, SizeM) * F, true);
    };
    auto Beam = [&](UInstancedStaticMeshComponent* G, FVector A, FVector B, double WidthM)
    {
        const FVector Delta = B - A;
        if (Delta.SizeSquared() < 1) return;
        G->AddInstance(FTransform(FQuat::FindBetweenNormals(FVector::ForwardVector, Delta.GetSafeNormal()),
            (A + B) * .5, FVector(Delta.Size() / 100, WidthM, WidthM)), true);
    };
    auto Pole = [&](FVector A, FVector B, double DiameterM)
    {
        const FVector Delta = B - A;
        if (Delta.SizeSquared() < 1) return;
        RoundMetal->AddInstance(FTransform(FQuat::FindBetweenNormals(FVector::UpVector, Delta.GetSafeNormal()),
            (A + B) * .5, FVector(DiameterM, DiameterM, Delta.Size() / 100)), true);
    };
    // Shared advertising contract: 4:1 artwork on the Engine Plane UV0, with
    // the plane normal facing the approaching viewer. Frames and backs have
    // their own opaque materials; a texture never wraps around a box edge.
    auto Billboard = [&](FVector Center, FVector Normal, double WidthM, double HeightM,
                         int32 Artwork, bool WithSupports)
    {
        Normal.Z = 0; Normal.Normalize();
        const FVector Up = FVector::UpVector;
        const FVector Across = FVector::CrossProduct(Normal, Up).GetSafeNormal();
        const FQuat Facing = FRotationMatrix::MakeFromXZ(Normal, Up).ToQuat();
        const FQuat PlaneFacing = FRotationMatrix::MakeFromXZ(Across, Normal).ToQuat();
        Ads[Artwork % 4]->AddInstance(FTransform(PlaneFacing, Center,
            FVector(WidthM, HeightM, 1)), true);
        Posts->AddInstance(FTransform(Facing, Center - Normal * 10,
            FVector(.16, WidthM + .12, HeightM + .12)), true);
        const FVector U = Across * WidthM * 50, V = Up * HeightM * 50;
        const FVector Front = Center + Normal * 1;
        Beam(Metal, Front - U - V, Front + U - V, .065);
        Beam(Metal, Front - U + V, Front + U + V, .065);
        Beam(Metal, Front - U - V, Front - U + V, .065);
        Beam(Metal, Front + U - V, Front + U + V, .065);
        if (WithSupports)
        {
            for (double Side : {-1.0, 1.0})
            {
                const FVector Top = Center + Across * (Side * WidthM * 34) - Normal * 16;
                const FVector Foot = Top - Up * ((HeightM * .5 + 1.2) * 100);
                Pole(Foot, Top + Up * (HeightM * 28), .13);
                const FVector RearFoot = Foot - Normal * 95;
                Beam(Metal, RearFoot, Top + Up * (HeightM * 15), .075);
                White->AddInstance(FTransform(FQuat::Identity, Foot + Up * 10, FVector(.5,.5,.2)), true);
                White->AddInstance(FTransform(FQuat::Identity, RearFoot + Up * 7, FVector(.32,.32,.14)), true);
            }
        }
    };

    FBox Box(ForceInit);
    for (const auto& P : Tracks[TrackIndex].Points) Box += P * 100;
    const FVector Center = Box.GetCenter();
    const FVector Extent = Box.GetExtent() + FVector(140000, 140000, 0);
    double MinHeight = 0;
    for (const auto& P : Tracks[TrackIndex].Elevation.Samples) MinHeight = FMath::Min(MinHeight, P.HeightM);
    Grass->AddInstance(FTransform(FRotator::ZeroRotator, FVector(Center.X, Center.Y, (MinHeight - 12) * 100 - 100), FVector(Extent.X / 50, Extent.Y / 50, 2)), true);
    FRandomStream Random(1991 + TrackIndex);
    // Static crossed tufts are clustered by HISM and animated entirely in the
    // material. Three interleaved populations make runtime density changes
    // immediate; disabled tiers neither render nor cast shadows.
    UStaticMesh* TuftMesh = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Game/Meshes/SM_GrassTuft.SM_GrassTuft"));
    UMaterialInterface* TuftFinish = Mat(TEXT("M_VergeGrass"));
    if (TuftMesh && TuftFinish)
    {
        // Short road segments in a local grid exclude neighbouring branches
        // of a hairpin without searching the full spline for every tuft.
        TArray<TPair<FVector, FVector>> RoadSegments;
        TMap<FIntPoint, TArray<int32>> RoadCells;
        const float RoadClearance = (HalfWidthM + 1.25f) * 100;
        constexpr float CellSize = 1200;
        for (float S = 0; S < LengthM(); S += 5)
        {
            FVector A = Position(S), B = Position(FMath::Min(S + 5, LengthM()));
            A.Z = B.Z = 0;
            const int32 SegmentIndex = RoadSegments.Emplace(A, B);
            const int32 MinX = FMath::FloorToInt((FMath::Min(A.X, B.X) - RoadClearance) / CellSize);
            const int32 MaxX = FMath::FloorToInt((FMath::Max(A.X, B.X) + RoadClearance) / CellSize);
            const int32 MinY = FMath::FloorToInt((FMath::Min(A.Y, B.Y) - RoadClearance) / CellSize);
            const int32 MaxY = FMath::FloorToInt((FMath::Max(A.Y, B.Y) + RoadClearance) / CellSize);
            for (int32 X = MinX; X <= MaxX; ++X)
                for (int32 Y = MinY; Y <= MaxY; ++Y)
                    RoadCells.FindOrAdd(FIntPoint(X, Y)).Add(SegmentIndex);
        }
        auto EncroachesRoad = [&](FVector Root)
        {
            Root.Z = 0;
            const auto* Nearby = RoadCells.Find(FIntPoint(
                FMath::FloorToInt(Root.X / CellSize), FMath::FloorToInt(Root.Y / CellSize)));
            if (!Nearby) return false;
            for (int32 SegmentIndex : *Nearby)
            {
                const auto& Segment = RoadSegments[SegmentIndex];
                if (FMath::PointDistToSegmentSquared(Root, Segment.Key, Segment.Value)
                    < RoadClearance * RoadClearance) return true;
            }
            return false;
        };
        UMaterialInstanceDynamic* WindMaterial = UMaterialInstanceDynamic::Create(TuftFinish, this);
        for (int32 Tier = 1; Tier <= 3; ++Tier)
        {
            auto* Tufts = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
            Tufts->SetupAttachment(RootComponent);
            Tufts->SetMobility(EComponentMobility::Movable);
            Tufts->SetStaticMesh(TuftMesh);
            Tufts->SetMaterial(0, WindMaterial);
            Tufts->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Tufts->SetCanEverAffectNavigation(false);
            Tufts->SetCastShadow(false);
            Tufts->bAffectDistanceFieldLighting = false;
            Tufts->bAffectDynamicIndirectLighting = false;
            Tufts->bAutoRebuildTreeOnInstanceChanges = false;
            Tufts->ComponentTags.Add(FName(TEXT("VroomGrass")));
            Tufts->ComponentTags.Add(FName(*FString::Printf(TEXT("VroomGrassTier%d"), Tier)));
            Tufts->RegisterComponent();
            Scenery.Add(Tufts);
            FRandomStream GrassRandom(17031 + TrackIndex * 41 + Tier * 197);
            TArray<FTransform> Patches;
            Patches.Reserve(FMath::CeilToInt(LengthM() / 1.8f) * 8);
            for (float S = (Tier - 1) * .6f; S < LengthM(); S += 1.8f)
            {
                for (float Side : {-1.f, 1.f})
                {
                    // Keep the apron and pit garages clear of vegetation.
                    if (Side > 0 && S > 70 && S < 185) continue;
                    for (int32 Row = 0; Row < 4; ++Row)
                    {
                        const float Along = S + GrassRandom.FRandRange(-.65f, .65f);
                        const float Spread = Row == 0 ? .45f : 1.35f;
                        const float Lateral = Side * (HalfWidthM + 1.65f + Row * 3.35f
                            + GrassRandom.FRandRange(-Spread, Spread));
                        FVector Root = Position(Along, Lateral);
                        if (EncroachesRoad(Root)) continue;
                        const float Height = GrassRandom.FRandRange(.28f, .58f) + Row * .035f;
                        const float Width = GrassRandom.FRandRange(.85f, 1.5f);
                        Root.Z -= .5f; // Bury the roots slightly in the grass ribbon.
                        Patches.Add(FTransform(FRotator(0, GrassRandom.FRandRange(0, 360), 0),
                            Root, FVector(Width, Width, Height)));
                    }
                }
            }
            Tufts->AddInstances(Patches, false, true, false);
            Tufts->BuildTreeIfOutdated(false, true);
        }
    }
    VegetationQuality = -1;
    UpdateVegetation();
    // Trees are authored branch/crown meshes, with one instanced population
    // per silhouette. Deterministic variation breaks the old evenly spaced
    // cone rows; trunks keep matching physical collision volumes.
    const auto& Regional = Vroom::Environment::Profile(TrackIndex);
    int32 TreeCount = 0;
    for (float S = 0; S < LengthM(); S += Regional.TreeSpacingM)
        for (float Side : {-1.f,1.f})
        {
            if (S < 220 && Side > 0) continue; // Pit road stays open.
            const int32 Population = Random.FRand() < .32f ? 3 : 1;
            for (int32 Member = 0; Member < Population; ++Member)
            {
                const float Along = S + Random.FRandRange(-16,16);
                const float Lateral = Side * Random.FRandRange(29,Member ? 145 : 82);
                FVector Root = Position(Along,Lateral);
                const float Key = Spline->FindInputKeyClosestToWorldLocation(Root);
                const FVector Closest = Spline->GetLocationAtSplineInputKey(Key,ESplineCoordinateSpace::World);
                const double Distance = FVector::Dist2D(Root,Closest)/100;
                if (Distance < 24) continue;
                const float NearS = Spline->GetDistanceAlongSplineAtSplineInputKey(Key)/100;
                Root.Z = Vroom::Environment::HeightM(TrackIndex,Root,Distance,Elevation(NearS).HeightM,MinHeight)*100;
                const int32 Species = Random.FRand() < .72f ? Regional.PrimaryTree
                    : (Regional.PrimaryTree + 1 + Random.RandRange(0,1)) % 4;
                const double Scale = Random.FRandRange(.70,1.34);
                const FTransform Pose(FRotator(0,Random.FRandRange(0,360),0),Root,
                    FVector(Scale*Random.FRandRange(.88,1.13),Scale,Scale));
                if (Forest[Species])
                {
                    const int32 Instance = Forest[Species]->AddInstance(Pose,true);
                    FLinearColor Leaf = Regional.Leaf * Random.FRandRange(.75f,1.35f);
                    if (TrackIndex == 2 && Random.FRand() < .20f)
                        Leaf = FLinearColor(.23f,.115f,.032f); // Warm mixed woodland.
                    Forest[Species]->SetCustomDataValue(Instance,0,Leaf.R,false);
                    Forest[Species]->SetCustomDataValue(Instance,1,Leaf.G,false);
                    Forest[Species]->SetCustomDataValue(Instance,2,Leaf.B,false);
                    ++TreeCount;
                }
                Trunks->AddInstance(FTransform(FRotator::ZeroRotator,
                    Root+FVector(0,0,Scale*380),FVector(.34*Scale,.34*Scale,7.6*Scale)),true);
                if (Member == 0 && Rocks && Random.FRand() < .22f)
                    Rocks->AddInstance(FTransform(FRotator(0,Random.FRandRange(0,360),0),
                        Root + FVector(180,-110,-24),FVector(Scale*1.4,Scale,Scale*.75)),true);
            }
        }
    for (auto* Population : Forest)
        if (Population) { Population->BuildTreeIfOutdated(false,true); Population->MarkRenderStateDirty(); }
    if (Rocks) Rocks->BuildTreeIfOutdated(false,true);
    UE_LOG(LogTemp,Display,TEXT("VROOM: Regional scenery track=%d trees=%d primary=%d relief=%.0fm"),
        TrackIndex,TreeCount,Regional.PrimaryTree,Regional.ReliefM);

    // A driveable pit lane runs past the existing garages. Ground paint and
    // thin asphalt never enter the scenery collision grid.
    for (float S = 55; S < 190; S += 3)
    {
        const FTransform F = Frame(S+1.5f,12.5f);
        LocalBox(Apron,F,FVector(0,0,.020),FVector(3.08,4.7,.035));
        for (float Side : {-1.f,1.f})
            LocalBox(White,F,FVector(0,Side*2.12,.048),FVector(3.05,.09,.012));
        if (static_cast<int32>(S) % 9 == 1)
            LocalBox(White,F,FVector(0,0,.052),FVector(1.2,.12,.012));
    }
    // Entry/exit wedges join the main course without a wall or a kerb.
    for (int32 End = 0; End < 2; ++End)
        for (int32 Step = 0; Step < 9; ++Step)
        {
            const float Blend = Step / 8.f;
            const float S = End ? 190 + Step*3.f : 31 + Step*3.f;
            const float Lateral = FMath::Lerp(End ? 12.5f : 5.7f,End ? 5.7f : 12.5f,Blend);
            const FTransform F = Frame(S,Lateral);
            LocalBox(Apron,F,FVector(0,0,.015),FVector(3.35,4.6,.028));
        }
    const FTransform Service = Frame(102,12.5f);
    for (float Side : {-1.f,1.f})
    {
        LocalBox(White,Service,FVector(Side*5.7,0,.08),FVector(.12,3,.015));
        LocalBox(Red,Service,FVector(0,Side*1.5,.08),FVector(11.5,.13,.015));
    }
    // Painted P, legible from the apron, with no new obstacle in the service box.
    LocalBox(White,Service,FVector(-2.0,-.35,.081),FVector(2.0,.16,.016));
    LocalBox(White,Service,FVector(-1.1,.10,.081),FVector(.16,1.0,.016));
    LocalBox(White,Service,FVector(-1.55,.53,.081),FVector(.9,.16,.016));
    LocalBox(White,Service,FVector(-2.0,.12,.081),FVector(.16,.9,.016));

    // Slot du joueur en fond de grille, colonne droite (S=24, voie +2,2).
    White->AddInstance(FTransform(Rotation(24), Position(26.3f, 2.2f) + FVector(0,0,7), FVector(.10,2.4,.014)), true);
    for (float Side : {-1.f, 1.f})
        White->AddInstance(FTransform(Rotation(24), Position(25.8f, 2.2f + Side * 1.15f) + FVector(0,0,7), FVector(1.1,.10,.014)), true);
    // The chequered line stays at S=0. Grid corners match the opponent rows.
    const float GridCellM = HalfWidthM * 2.f / 16.f;
    for (int32 I = 0; I < 16; ++I)
        for (int32 J = 0; J < 2; ++J)
        {
            auto* G = (I + J) % 2 ? Posts : White;
            G->AddInstance(FTransform(Rotation(0), Position(J * .65f, -HalfWidthM + (I + .5f) * GridCellM) + FVector(0,0,7), FVector(.65f,GridCellM,.02f)), true);
        }
    for (int32 Row = 0; Row < 6; ++Row)
        for (float Lane : {-2.2f, 2.2f})
        {
            const float S = 40 + Row * 16;
            White->AddInstance(FTransform(Rotation(S), Position(S + 2.3f, Lane) + FVector(0,0,7), FVector(.10,2.4,.014)), true);
            for (float Side : {-1.f, 1.f})
                White->AddInstance(FTransform(Rotation(S), Position(S + 1.8f, Lane + Side * 1.15f) + FVector(0,0,7), FVector(1.1,.10,.014)), true);
        }

    // Twelve repeated pit bays: shutter slats, columns, awnings and upper
    // windows all share material/mesh groups. No per-bay actor or ticking light.
    for (int32 I = 0; I < 12; ++I)
    {
        const float S = 80 + I * 8;
        const FTransform F = Frame(S, 22);
        auto P = [&](FVector M) { return F.TransformPosition(M * 100); };
        LocalBox(Apron, F, FVector(0,-1,.025), FVector(7.95,17,.05));
        LocalBox(White, F, FVector(0,0,2.75), FVector(7.65,12,5.5));
        LocalBox(Posts, F, FVector(0,-6.06,1.7), FVector(5.75,.10,3.15));
        for (int32 Slat = 0; Slat < 9; ++Slat)
            LocalBox(Metal, F, FVector(0,-6.13,.3 + Slat * .34), FVector(5.68,.035,.045));
        for (double Side : {-1.0, 1.0})
        {
            LocalBox(White, F, FVector(Side * 3.05,-6.13,1.8), FVector(.23,.22,3.6));
            Pole(P(FVector(Side * 3.30,-7.35,0)), P(FVector(Side * 3.30,-7.35,3.7)), .11);
            LocalBox(White, F, FVector(Side * 3.30,-7.35,.12), FVector(.34,.34,.24));
            Beam(Metal, P(FVector(Side * 3.3,-7.35,3.6)), P(FVector(Side * 3.3,-6.1,3.2)), .065);
        }
        LocalBox(Metal, F, FVector(0,-6.75,3.77), FVector(7.15,1.65,.12));
        LocalBox(Red, F, FVector(0,-7.59,3.76), FVector(7.2,.09,.25));
        LocalBox(Windows, F, FVector(0,-6.085,4.55), FVector(5.9,.05,1.2));
        for (int32 Bar = -2; Bar <= 2; ++Bar)
            LocalBox(White, F, FVector(Bar * 1.18,-6.14,4.55), FVector(.065,.065,1.22));
        LocalBox(White, F, FVector(0,-6.13,3.93), FVector(6.0,.09,.08));
        LocalBox(White, F, FVector(0,-6.13,5.17), FVector(6.0,.09,.08));
        LocalBox(Posts, F, FVector(0,0,5.59), FVector(7.9,12.3,.18));
        LocalBox(Red, F, FVector(0,-6.20,5.70), FVector(7.95,.18,.38));
        for (double X : {-3.0, -1.5, 0.0, 1.5, 3.0})
            LocalBox(Metal, F, FVector(X,0,5.70), FVector(.045,12.25,.035));
        Billboard(P(FVector(0,-6.25,6.45)), -F.GetRotation().GetRightVector(), 4, 1, I % 4, false);
        Beam(Metal, P(FVector(-1.35,-6.15,5.65)), P(FVector(-1.35,-6.15,6.3)), .065);
        Beam(Metal, P(FVector(1.35,-6.15,5.65)), P(FVector(1.35,-6.15,6.3)), .065);
    }

    // Start-signal gantry ahead of the grid, visible from the player's S=0
    // starting position. The painted timing line and lap origin remain S=0.
    // Two open lattice towers and a triangulated box truss, without guardrails.
    const FTransform Gantry = Frame(160, 0);
    auto G = [&](double X, double Y, double Z) { return Gantry.TransformPosition(FVector(X,Y,Z) * 100); };
    for (double Side : {-1.0, 1.0})
    {
        const double Y = Side * 9.0;
        for (double X : {-.55, .55})
        {
            Pole(G(X,Y,0), G(X,Y,9.9), .23);
            LocalBox(White, Gantry, FVector(X,Y,.16), FVector(.7,.7,.32));
        }
        for (int32 Level = 0; Level < 6; ++Level)
        {
            const double Z = .35 + Level * 1.5;
            Beam(Metal, G(-.55,Y,Z), G(.55,Y,Z + 1.5), .075);
            Beam(Metal, G(.55,Y,Z), G(-.55,Y,Z + 1.5), .075);
        }
        Beam(Metal, G(-.55,Y,6.7), G(.55,Y,6.7), .12);
        Beam(Metal, G(-.55,Y,9.85), G(.55,Y,9.85), .12);
    }
    for (double X : {-.55, .55})
    {
        Beam(Metal, G(X,-9,6.75), G(X,9,6.75), .14);
        Beam(Metal, G(X,-9,9.85), G(X,9,9.85), .14);
        for (int32 Bay = 0; Bay < 9; ++Bay)
        {
            const double A = -9 + Bay * 2, B = A + 2;
            Beam(Metal, G(X,A,6.75), G(X,B,9.85), .085);
            Beam(Metal, G(X,A,9.85), G(X,B,6.75), .085);
            Beam(Metal, G(X,A,6.75), G(X,A,9.85), .085);
        }
    }
    for (int32 Bay = 0; Bay < 9; ++Bay)
        Beam(Metal, G(-.55,-9 + Bay * 2,9.85), G(.55,-7 + Bay * 2,9.85), .075);
    const FVector Upstream = -Gantry.GetRotation().GetForwardVector();
    Billboard(G(-.67,0,8.3), Upstream, 12, 3, 0, false);
    for (int32 Column = 0; Column < 5; ++Column)
    {
        const double Y = (Column - 2) * .86;
        LocalBox(Posts, Gantry, FVector(-.65,Y,5.85), FVector(.30,.67,1.48));
        Beam(Metal, G(-.5,Y,6.7), G(-.5,Y,6.45), .075);
        for (int32 Row = 0; Row < 2; ++Row)
        {
            const double Z = 5.54 + Row * .62;
            const int32 Instance = StartLights->AddInstance(FTransform(
                FQuat::FindBetweenNormals(FVector::UpVector, Upstream), G(-.825,Y,Z), FVector(.37,.37,.045)), true);
            StartLights->SetCustomDataValue(Instance, 0, 0, false);
            LocalBox(Posts, Gantry, FVector(-.95,Y,Z + .235), FVector(.37,.53,.065));
            for (double Side : {-1.0, 1.0})
                LocalBox(Posts, Gantry, FVector(-.92,Y + Side * .235,Z + .055), FVector(.30,.045,.32));
        }
    }
    StartLights->MarkRenderStateDirty();

    // Two lightweight advertising gantries. These are scenery placements,
    // not decoded source events; fractions also fit the shorter circuits.
    for (int32 I = 0; I < 2; ++I)
    {
        const float S = LengthM() * (I == 0 ? .32f : .66f);
        const FTransform F = Frame(S, 0);
        auto P = [&](double X, double Y, double Z) { return F.TransformPosition(FVector(X,Y,Z) * 100); };
        for (double Side : {-1.0, 1.0})
        {
            Pole(P(0, Side * 8.5, 0), P(0, Side * 8.5, 6.0), .16);
            LocalBox(White, F, FVector(0,Side * 8.5,.14), FVector(.52,.52,.28));
        }
        Beam(Metal, P(0,-8.5,6.0), P(0,8.5,6.0), .17);
        Billboard(P(-.17,0,7.0), -F.GetRotation().GetForwardVector(), 8, 2, I + 2, false);
    }

    auto OverheadGantry = [&](float S, double Span, double Height, int32 Artwork, bool FaceOncoming)
    {
        const FTransform F = Frame(S, 0);
        auto P = [&](double X, double Y, double Z) { return F.TransformPosition(FVector(X,Y,Z) * 100); };
        for (double Side : {-1.0, 1.0})
        {
            Pole(P(0, Side * Span, 0), P(0, Side * Span, Height), .18);
            LocalBox(White, F, FVector(0, Side * Span, .14), FVector(.52,.52,.28));
        }
        Beam(Metal, P(0,-Span,Height), P(0,Span,Height), .16);
        const FVector Face = FaceOncoming ? -F.GetRotation().GetForwardVector() : F.GetRotation().GetForwardVector();
        Billboard(P(0, 0, Height - 1.05), Face, Span * 1.45, 2.15, Artwork, false);
    };
    // Timing line: a real overhead gantry, poles off the racing surface.
    OverheadGantry(0.f, 9.6, 7.4, 0, true);
    // Pit entry / exit bridges, distinct from the advertising pair at 32 % / 66 %.
    OverheadGantry(48.f, 9.2, 6.2, 1, true);
    OverheadGantry(198.f, 9.2, 6.2, 2, true);
    if (LengthM() > 4200.f)
        OverheadGantry(LengthM() * 0.48f, 8.8, 6.4, 3, true);

    auto TracksideBoardSafe = [&](float S, float Side, int32 Artwork, float Width, float Height)
    {
        const float Lateral = Side * (HalfWidthM + 3.6f);
        const FVector MarkerPos = Position(S, Lateral);
        const float Key = Spline->FindInputKeyClosestToWorldLocation(MarkerPos);
        const FVector Closest = Spline->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World);
        if (FVector::Dist2D(MarkerPos, Closest) < (HalfWidthM + 2.4f) * 100.f) return;
        const FTransform F = Frame(S, Lateral);
        Billboard(F.TransformPosition(FVector(0, Side * 20.f, Height * 50.f)),
            -F.GetRotation().GetForwardVector(), Width, Height, Artwork, true);
    };
    TracksideBoardSafe(40.f, 1.f, 1, 3.2f, 1.6f);
    TracksideBoardSafe(70.f, 1.f, 1, 3.2f, 1.6f);
    for (int32 SampleIdx = 0; SampleIdx < 10; ++SampleIdx)
    {
        const float S = LengthM() * (SampleIdx + 1) / 11.0f;
        if (FMath::Abs(Curvature(S)) < 0.012f) continue;
        const float MarkerS = S - 70.f;
        if (MarkerS < 220.f) continue;
        const double Sign = Curvature(S) > 0 ? -1.0 : 1.0;
        TracksideBoardSafe(MarkerS, Sign, 3, 2.4f, 1.2f);
        TracksideBoardSafe(MarkerS + 22.f, Sign, 3, 2.2f, 1.1f);
    }

    // Isolated 8x2-m adverts, never a continuous wall. The first pair is visible
    // from the starting area. Later boards are separated by hundreds of metres.
    auto TracksideBoard = [&](float S, float Side, int32 Artwork)
    {
        const FVector Ground = Position(S, Side * 15.5f);
        const float Key = Spline->FindInputKeyClosestToWorldLocation(Ground);
        const FVector Closest = Spline->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World);
        if (FVector::Dist2D(Ground, Closest) < 1150) return;
        const FVector CenterPoint = Ground + FVector(0,0,220);
        const FVector Approach = Position(S - 45, 0);
        Billboard(CenterPoint, Approach - Ground, 8, 2, Artwork, true);
    };
    TracksideBoard(30, -1, 1);
    TracksideBoard(55, 1, 0);
    for (int32 I = 0; 260 + I * 360 < LengthM() - 180; ++I)
        TracksideBoard(260 + I * 360, I % 2 ? 1 : -1, I % 4);

    // Compact grandstands, with stepped seating and open roof supports. Their
    // metal/seat instances share the existing scenery draw groups.
    const int32 StandRows[] = {6,5,7,5,7,8};
    const double StandWidth[] = {30,28,34,26,34,40};
    auto LandscapeFrame = [&](float S, float Lateral)
    {
        FTransform F = Frame(S,Lateral);
        FVector P = F.GetLocation();
        const float Key = Spline->FindInputKeyClosestToWorldLocation(P);
        const FVector Near = Spline->GetLocationAtSplineInputKey(Key,ESplineCoordinateSpace::World);
        const float NearS = Spline->GetDistanceAlongSplineAtSplineInputKey(Key)/100;
        P.Z = Vroom::Environment::HeightM(TrackIndex,P,FVector::Dist2D(P,Near)/100,
            Elevation(NearS).HeightM,MinHeight)*100;
        F.SetLocation(P); return F;
    };
    for (int32 Stand = 0; Stand < 3; ++Stand)
    {
        const float S = Stand == 0 ? 115.f : LengthM() * (Stand == 1 ? .38f : .74f);
        const FTransform F = LandscapeFrame(S,-34.f-Stand*3.f);
        const double Width = StandWidth[TrackIndex], Rows = StandRows[TrackIndex];
        bool Clear = true;
        for (float X : {-float(Width)*.6f,float(Width)*.6f})
            for (float Y : {-12.f,3.f})
            {
                const FVector P = F.TransformPosition(FVector(X,Y,0)*100);
                const float Key = Spline->FindInputKeyClosestToWorldLocation(P);
                if (FVector::Dist2D(P,Spline->GetLocationAtSplineInputKey(Key,ESplineCoordinateSpace::World))<1700) Clear=false;
            }
        if (!Clear) continue;
        auto P = [&](double X,double Y,double Z){return F.TransformPosition(FVector(X,Y,Z)*100);};
        for (int32 Row = 0; Row < Rows; ++Row)
        {
            const double Y = -Row*1.12, Z = .25+Row*.49;
            LocalBox(Metal,F,FVector(0,Y,Z-.12),FVector(Width,1.12,.24));
            LocalBox(Row%3==0 ? White : Red,F,FVector(0,Y-.16,Z+.29),FVector(Width-.7,.45,.16));
            LocalBox(Row%3==0 ? White : Red,F,FVector(0,Y-.43,Z+.58),FVector(Width-.7,.10,.48));
            // Small seated silhouettes keep stands occupied without animated
            // crowds, actor ticks, or transparent billboard overdraw.
            for (int32 Seat = 0; Seat < int(Width/.72)-1; Seat += 2)
            {
                if ((Seat+Row*3+Stand)%7==0) continue;
                const double X = -Width*.5+.9+Seat*.72;
                auto* Shirt = (Seat+Row)%3==0 ? White : Posts;
                LocalBox(Shirt,F,FVector(X,Y-.13,Z+.65),FVector(.34,.28,.48));
                LocalBox(Metal,F,FVector(X,Y-.13,Z+.97),FVector(.21,.21,.22));
            }
        }
        const double RoofZ = Rows*.49+2.35;
        LocalBox(White,F,FVector(0,-Rows*.56,RoofZ),FVector(Width+1.5,Rows*1.12+2,.17));
        LocalBox(Red,F,FVector(0,.95,RoofZ-.13),FVector(Width+1.5,.15,.42));
        for (double X : {-Width*.49,0.,Width*.49})
        {
            Pole(P(X,1,0),P(X,1,RoofZ),.14);
            Pole(P(X,-Rows*1.12,0),P(X,-Rows*1.12,RoofZ),.17);
            Beam(Metal,P(X,1,RoofZ-.3),P(X,-Rows*1.12,RoofZ-.3),.12);
            Beam(Metal,P(X,-Rows*1.12,RoofZ-.3),P(X+1,-Rows*1.12,RoofZ-2),.08);
        }
        for (double Side : {-1.,1.})
            Beam(Metal,P(Side*Width*.5,.5,1.0),P(Side*Width*.5,-Rows*1.12,Rows*.49+1),.08);
    }

    // Regional paddock silhouettes: a wood clubhouse in Sweden, low hangars
    // for the former airfields, or glazed timing towers near the pit complex.
    const FTransform Landmark = LandscapeFrame(230,-72);
    auto LandmarkPoint = [&](FVector P){return Landmark.TransformPosition(P*100);};
    if (TrackIndex == 1 || TrackIndex == 5)
    {
        for (int32 Bay=0;Bay<3;++Bay)
        {
            LocalBox(White,Landmark,FVector(Bay*18,0,4.2),FVector(17,18,8.4));
            LocalBox(Posts,Landmark,FVector(Bay*18,9.05,3.2),FVector(13,.08,6.1));
            LocalBox(Metal,Landmark,FVector(Bay*18,0,8.5),FVector(17.4,18.7,.30));
            for (int32 Rib=-3;Rib<=3;++Rib)
                LocalBox(Metal,Landmark,FVector(Bay*18+Rib*2.3,9.11,3.2),FVector(.08,.04,6.1));
        }
    }
    else
    {
        const bool Clubhouse = TrackIndex == 3;
        const int32 Floors = Clubhouse ? 2 : 4;
        for (int32 Floor=0;Floor<Floors;++Floor)
        {
            LocalBox(Clubhouse ? Red : White,Landmark,FVector(0,0,1.7+Floor*3.4),FVector(13,9,3.4));
            LocalBox(Windows,Landmark,FVector(0,4.55,2.1+Floor*3.4),FVector(11.7,.06,1.6));
            for (int32 Mullion=-3;Mullion<=3;++Mullion)
                LocalBox(White,Landmark,FVector(Mullion*1.65,4.60,2.1+Floor*3.4),FVector(.10,.06,1.65));
            LocalBox(Metal,Landmark,FVector(0,0,3.4+Floor*3.4),FVector(14.2,10.2,.17));
        }
        if (TrackIndex == 4)
            for (int32 Step=0;Step<3;++Step)
                LocalBox(Posts,Landmark,FVector(0,0,Floors*3.4+.2+Step*.34),FVector(15-Step*2,11-Step*1.5,.34));
        Pole(LandmarkPoint(FVector(0,0,Floors*3.4)),LandmarkPoint(FVector(0,0,Floors*3.4+5)),.07);
    }

    // Collision volumes come from the same opaque instances as the scenery.
    // Exclude ground, leaves, grass and billboard artwork. The real trunks,
    // posts, pit walls, supports and trusses retain their actual height.
    constexpr double CellSize = 4000.0;
    const bool PitProbe = FParse::Param(FCommandLine::Get(), TEXT("VroomPitTest"));
    UInstancedStaticMeshComponent* SolidGroups[] = {Posts, White, Red, Metal, RoundMetal, Windows, Trunks, Rocks};
    for (auto* SolidGroup : SolidGroups)
    {
        if (!SolidGroup) continue;
        const UStaticMesh* Mesh = SolidGroup->GetStaticMesh();
        if (!Mesh) continue;
        const FBox MeshBounds = Mesh->GetBoundingBox();
        for (int32 Instance = 0; Instance < SolidGroup->GetInstanceCount(); ++Instance)
        {
            FTransform Pose;
            if (!SolidGroup->GetInstanceTransform(Instance, Pose, true)) continue;
            const FVector HalfExtent = MeshBounds.GetExtent() * Pose.GetScale3D().GetAbs();
            // Paint on the road uses centimetre-thin cubes, never obstacles.
            if (HalfExtent.Z < 3.0) continue;
            FVroomHazard Hazard;
            Hazard.Transform = FTransform(Pose.GetRotation(), Pose.TransformPosition(MeshBounds.GetCenter()));
            Hazard.HalfExtent = HalfExtent;
            Hazard.Bounds = FBox(-HalfExtent, HalfExtent).TransformBy(Hazard.Transform.ToMatrixWithScale());
            const int32 Index = Hazards.Add(Hazard);
            if (PitProbe && FVector::Dist2D(Hazard.Transform.GetLocation(), Position(102,12.5)) < 500)
                UE_LOG(LogTemp, Display, TEXT("VROOM-PIT-HAZARD index=%d mesh=%s material=%s instance=%d center=%s extent=%s rotation=%s"),
                    Index, *Mesh->GetName(), *GetNameSafe(SolidGroup->GetMaterial(0)), Instance,
                    *Hazard.Transform.GetLocation().ToString(), *HalfExtent.ToString(), *Hazard.Transform.Rotator().ToString());
            const int32 MinX = FMath::FloorToInt(Hazard.Bounds.Min.X / CellSize);
            const int32 MaxX = FMath::FloorToInt(Hazard.Bounds.Max.X / CellSize);
            const int32 MinY = FMath::FloorToInt(Hazard.Bounds.Min.Y / CellSize);
            const int32 MaxY = FMath::FloorToInt(Hazard.Bounds.Max.Y / CellSize);
            for (int32 X = MinX; X <= MaxX; ++X)
                for (int32 Y = MinY; Y <= MaxY; ++Y)
                    HazardCells.FindOrAdd(FIntPoint(X, Y)).Add(Index);
        }
    }
    UE_LOG(LogTemp, Display, TEXT("VROOM: Scenery collision volumes=%d cells=%d"), Hazards.Num(), HazardCells.Num());
    TWeakObjectPtr<AVroomCircuit> WeakThis(this);
    GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis]()
    {
        if (WeakThis.IsValid()) Vroom::Environment::ApplyAtmosphere(WeakThis->GetWorld(),WeakThis->TrackIndex);
    });
}

bool AVroomCircuit::SweepScenery(const FVector& PreviousCenter, const FVector& Center,
    const FQuat& CarRotation, double& OutFraction, FVector& OutNormal) const
{
    if (PreviousCenter.ContainsNaN() || Center.ContainsNaN() || CarRotation.ContainsNaN()) return false;
    FBox SweptBounds(ForceInit);
    SweptBounds += PreviousCenter;
    SweptBounds += Center;
    SweptBounds = SweptBounds.ExpandBy(FVector(260,260,260));
    constexpr double CellSize = 4000.0;
    const FVector CarAxes[] = {CarRotation.GetAxisX(), CarRotation.GetAxisY(), CarRotation.GetAxisZ()};
    const FVector CarHalf(220,120,60);
    OutFraction = 1;
    OutNormal = FVector::ZeroVector;
    bool Hit = false;
    for (int32 X = FMath::FloorToInt(SweptBounds.Min.X / CellSize); X <= FMath::FloorToInt(SweptBounds.Max.X / CellSize); ++X)
        for (int32 Y = FMath::FloorToInt(SweptBounds.Min.Y / CellSize); Y <= FMath::FloorToInt(SweptBounds.Max.Y / CellSize); ++Y)
        {
            const auto* Nearby = HazardCells.Find(FIntPoint(X,Y));
            if (!Nearby) continue;
            for (int32 Index : *Nearby)
            {
                const FVroomHazard& Hazard = Hazards[Index];
                if (!Hazard.Bounds.Intersect(SweptBounds)) continue;
                const FVector A = Hazard.Transform.InverseTransformPosition(PreviousCenter);
                const FVector B = Hazard.Transform.InverseTransformPosition(Center);
                std::array<std::array<double,3>,3> LocalCarAxes{};
                for (int32 Axis = 0; Axis < 3; ++Axis)
                {
                    const FVector Local = Hazard.Transform.InverseTransformVectorNoScale(CarAxes[Axis]);
                    LocalCarAxes[Axis] = {Local.X,Local.Y,Local.Z};
                }
                double Fraction = 1;
                std::array<double,3> LocalNormal{};
                if (!Vroom::ContactModel::SweepOrientedObstacle({A.X,A.Y,A.Z}, {B.X,B.Y,B.Z},
                    {Hazard.HalfExtent.X,Hazard.HalfExtent.Y,Hazard.HalfExtent.Z}, LocalCarAxes,
                    {CarHalf.X,CarHalf.Y,CarHalf.Z}, Fraction, LocalNormal) || Fraction > OutFraction)
                    continue;
                Hit = true;
                if (FParse::Param(FCommandLine::Get(), TEXT("VroomPitTest")))
                    UE_LOG(LogTemp, Display, TEXT("VROOM-PIT-HIT index=%d center=%s extent=%s car=%s local=%s fraction=%.6f"),
                        Index, *Hazard.Transform.GetLocation().ToString(), *Hazard.HalfExtent.ToString(),
                        *PreviousCenter.ToString(), *A.ToString(), Fraction);
                OutFraction = Fraction;
                const FVector Normal(LocalNormal[0],LocalNormal[1],LocalNormal[2]);
                OutNormal = Hazard.Transform.TransformVectorNoScale(Normal);
            }
        }
    return Hit;
}

void AVroomCircuit::UpdateVegetation()
{
    const int32 Quality = FMath::Clamp(Vroom::Graphics::FoliageQuality(), 0, 3);
    const bool Wind = Vroom::Graphics::AnimatedGrass();
    if (Quality == VegetationQuality && Wind == VegetationWind) return;
    const int32 EndDistance[] = {0, 3000, 4500, 6000};
    int32 VisibleInstances = 0;
    for (const auto& Component : Scenery)
    {
        if (!Component || !Component->ComponentHasTag(TEXT("VroomGrass"))) continue;
        int32 Tier = 1;
        if (Component->ComponentHasTag(TEXT("VroomGrassTier2"))) Tier = 2;
        if (Component->ComponentHasTag(TEXT("VroomGrassTier3"))) Tier = 3;
        Component->SetVisibility(Quality >= Tier);
        Component->SetCullDistances(FMath::Max(100, EndDistance[Quality] - 1200),
            FMath::Max(200, EndDistance[Quality]));
        if (auto* Material = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0)))
            Material->SetScalarParameterValue(TEXT("WindStrength"), Wind ? 1.f : 0.f);
        if (Quality >= Tier) VisibleInstances += Component->GetInstanceCount();
    }
    VegetationQuality = Quality;
    VegetationWind = Wind;
    UE_LOG(LogTemp, Display, TEXT("VROOM: Vegetation quality=%d wind=%d instances=%d cull=%dm"),
        Quality, Wind, VisibleInstances, EndDistance[Quality] / 100);
}

void AVroomCircuit::BuildOpponents()
{
    for (auto C : OpponentMeshes) if (C) C->DestroyComponent();
    OpponentMeshes.Empty();
    OpponentS.Empty(); OpponentLanes.Empty(); OpponentSpeeds.Empty();
    PreviousOpponentLanes.Empty(); Traffic.clear(); Traffic.reserve(12);
    OpponentRecycled.Init(false, 12);
    OpponentFinishTimes.Init(0, 12); RaceFinishDistance = 0;
    OpponentMotionNeedsReset = false;
    static const TCHAR* Materials[] = { TEXT("M_F1Paint"), TEXT("M_F1Pearl"), TEXT("M_F1Carbon"), TEXT("M_F1Rubber"), TEXT("M_F1Carbon") };
    static const TCHAR* MeshNames[] = { TEXT("SM_F1OpponentPaint"), TEXT("SM_F1OpponentTrim"),
        TEXT("SM_F1OpponentCarbon"), TEXT("SM_F1OpponentWheels"), TEXT("Cylinder") };
    for (int32 Group = 0; Group < 5; ++Group)
    {
        RenderedOpponentTransforms[Group].Reset(Group == 4 ? 96 : 12);
        NextOpponentTransforms[Group].Reset(Group == 4 ? 96 : 12);
        auto C = NewObject<UInstancedStaticMeshComponent>(this);
        C->SetupAttachment(RootComponent);
        C->SetMobility(EComponentMobility::Movable);
        const FString MeshPath = Group == 4
            ? FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), MeshNames[Group], MeshNames[Group])
            : FString::Printf(TEXT("/Game/Meshes/%s.%s"), MeshNames[Group], MeshNames[Group]);
        C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *MeshPath));
        C->SetMaterial(0, Mat(Materials[Group]));
        if (Group == 0) C->SetNumCustomDataFloats(3);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCanEverAffectNavigation(false);
        // Allocate previous instance transforms as instances are added. TSR
        // needs the motion of each car even though its ISM component is fixed.
        C->SetHasPerInstancePrevTransforms(true);
        // Keep the Mac's moving groups out of expensive Lumen card updates;
        // the Windows quality profile retains their indirect-light contribution.
#if PLATFORM_MAC
        C->SetAffectDistanceFieldLighting(false);
        C->SetAffectDynamicIndirectLighting(false);
#endif
        C->RegisterComponent();
        C->SetVisibility(OpponentsVisible);
        OpponentMeshes.Add(C);
    }
    for (int32 I = 0; I < 12; ++I)
    {
        OpponentS.Add(40 + (I / 2) * 16);
        OpponentLanes.Add(I % 2 ? 2.2 : -2.2);
        OpponentSpeeds.Add(0);
        Vroom::TrafficCar Agent;
        Agent.Distance = OpponentS.Last(); Agent.Lateral = Agent.TargetLane = OpponentLanes.Last();
        Agent.Fuel = 1.0;
        Agent.TyreHealth = 2048.0;
        Agent.Pitting = false;
        Agent.PitTimer = 0.0;
        Agent.LiveryIndex = (I / 2) % 6;
        Agent.Personality = Vroom::PersonalityByLivery(Agent.LiveryIndex);
        Traffic.push_back(Agent);
        static const FLinearColor Liveries[] = { FLinearColor(.012f,.075f,.65f), FLinearColor(.72f,.012f,.018f),
            FLinearColor(.91f,.60f,.012f), FLinearColor(.02f,.35f,.13f),
            FLinearColor(.33f,.025f,.57f), FLinearColor(.88f,.19f,.012f) };
        const FLinearColor Colour = Liveries[Agent.LiveryIndex];
        for (int32 J = 0; J < 4; ++J)
        {
            auto* Mesh = OpponentMeshes[OpponentPartGroup(I, J)].Get();
            const int32 Instance = Mesh->AddInstance(FTransform::Identity, true);
            if (J == 0)
            {
                Mesh->SetCustomDataValue(Instance, 0, Colour.R, false);
                Mesh->SetCustomDataValue(Instance, 1, Colour.G, false);
                Mesh->SetCustomDataValue(Instance, 2, Colour.B, false);
            }
        }
        int32 WishboneCount = 0;
        const Vroom::Wishbone* Wishbones = Vroom::CarWishbones(WishboneCount);
        auto* Bars = OpponentMeshes[4].Get();
        for (int32 Bar = 0; Bar < WishboneCount; ++Bar)
            Bars->AddInstance(OpponentWishboneTransform(Wishbones[Bar]), true);
    }
    PreviousOpponentS = OpponentS;
    PreviousOpponentLanes = OpponentLanes;
    RenderedOpponentS.Init(TNumericLimits<double>::Max(), OpponentS.Num());
    RenderedOpponentLanes.Init(TNumericLimits<double>::Max(), OpponentS.Num());
    OpponentVisualYaw.Init(0, OpponentS.Num());
    RenderOpponents(1);
}

void AVroomCircuit::ConfigureTraffic(bool Visible, bool Arcade, Vroom::RaceDifficulty Level)
{
    OpponentsVisible = Visible; ArcadeTraffic = Arcade; Difficulty = Level;
    for (auto Mesh : OpponentMeshes) if (Mesh) Mesh->SetVisibility(Visible);
}

void AVroomCircuit::HitOpponent(int32 Index, double RelativeSpeed)
{
    if (!ReactiveTraffic || !OpponentSpeeds.IsValidIndex(Index)) return;
    OpponentSpeeds[Index] = FMath::Max(0.0, OpponentSpeeds[Index] - FMath::Clamp(RelativeSpeed * .35, 2.0, 15.0));
    if (Index < static_cast<int32>(Traffic.size()))
    {
        Traffic[Index].Speed = OpponentSpeeds[Index];
        Traffic[Index].DamageDelay = FMath::Clamp(RelativeSpeed * .12, .5, 3.0);
    }
}

void AVroomCircuit::AdvanceRace(float H)
{
    if (Tracks.IsEmpty() || !Running || Paused || !FMath::IsFinite(H) || H <= 0) return;
    PreviousOpponentS = OpponentS;
    PreviousOpponentLanes = OpponentLanes;
    for (int32 I = 0; I < OpponentRecycled.Num(); ++I) OpponentRecycled[I] = false;
    RaceClock += H;
    const Vroom::StartSignal Signal = Vroom::StartSignalFromClock(RaceClock);
    const int32 LightStage = Vroom::GantryColumnsLit(Signal);
    if (StartLights && LightStage != StartLightStage)
    {
        for (int32 I = 0; I < StartLights->GetInstanceCount(); ++I)
            StartLights->SetCustomDataValue(I, 0, I / 2 < LightStage ? 1.f : 0.f, false);
        StartLights->MarkRenderStateDirty();
        StartLightStage = LightStage;
        UE_LOG(LogTemp, Display, TEXT("VROOM: Start lights columns=%d signal=%d raceClock=%.3f"),
            LightStage, static_cast<int32>(Signal), RaceClock);
    }
    const int32 SignalId = static_cast<int32>(Signal);
    if (SignalId != StartSignalPlayed && Signal != Vroom::StartSignal::Idle && Signal != Vroom::StartSignal::Racing)
    {
        const Vroom::StartBeep Beep = Vroom::BeepFor(Signal);
        const float Volume = static_cast<float>(Beep.Volume)
            * Vroom::Settings::Get().MasterVolume * Vroom::Settings::Get().EffectsVolume;
        if (StartLightAudio && Beep.Frequency > 0 && Volume > 0.01f)
        {
            if (!StartLightWave)
            {
                StartLightWave = NewObject<USoundWaveProcedural>(this);
                StartLightWave->SetSampleRate(48000);
                StartLightWave->NumChannels = 1;
                StartLightWave->SoundGroup = SOUNDGROUP_Default;
                StartLightAudio->SetSound(StartLightWave);
            }
            StartLightWave->Duration = Beep.Duration;
            StartLightWave->ResetAudio();
            TArray<int16> Samples;
            const int32 NumSamples = FMath::Max(1, FMath::RoundToInt(Beep.Duration * 48000));
            Samples.SetNumUninitialized(NumSamples);
            const double PhaseInc = UE_TWO_PI * Beep.Frequency / 48000.0;
            double Phase = 0.0;
            for (int32 Sample = 0; Sample < NumSamples; ++Sample)
            {
                float Envelope = 1.0f;
                if (Sample < NumSamples * 0.05f) Envelope = Sample / (NumSamples * 0.05f);
                else if (Sample > NumSamples * 0.88f) Envelope = (NumSamples - Sample) / (NumSamples * 0.12f);
                Samples[Sample] = static_cast<int16>(FMath::Sin(Phase) * 18000.0f * Envelope);
                Phase += PhaseInc;
            }
            StartLightWave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), NumSamples * sizeof(int16));
            StartLightAudio->SetVolumeMultiplier(Volume);
            StartLightAudio->Play();
        }
        StartSignalPlayed = SignalId;
        UE_LOG(LogTemp, Display, TEXT("VROOM: Start signal %d volume=%.2f"), SignalId, Volume);
    }
    if (RaceClock < 0 || !OpponentsVisible) return;
    if (ReactiveTraffic && Traffic.size() == static_cast<std::size_t>(OpponentS.Num()))
    {
        for (int32 I = 0; I < OpponentS.Num(); ++I)
        {
            Traffic[I].Distance = OpponentS[I]; Traffic[I].Lateral = OpponentLanes[I]; Traffic[I].Speed = OpponentSpeeds[I];
        }
        Vroom::StepTraffic(Traffic, H, LengthM(), HalfWidthM, Difficulty,
            TrafficPlayerActive ? &TrafficPlayer : nullptr, [&](double S) { return static_cast<double>(Curvature(S)); });
        for (int32 I = 0; I < OpponentS.Num(); ++I)
        {
            auto& Agent = Traffic[I];
            // Arcade is replenished traffic. Recycle only far behind, then
            // reset both render and pass history so spawning grants no points.
            if (ArcadeTraffic && TrafficPlayerActive && TrafficPlayer.Distance - Agent.Distance > 100)
            {
                Agent.Distance = TrafficPlayer.Distance + 230 + I * 24;
                Agent.Lateral = Agent.TargetLane = I % 2 ? 2.4 : -2.4;
                Agent.Speed = FMath::Max(20.0, TrafficPlayer.Speed * .72);
                Agent.Fuel = 1.0; Agent.TyreHealth = 2048.0; Agent.Pitting = false; Agent.PitTimer = 0.0;
                PreviousOpponentS[I] = Agent.Distance;
                PreviousOpponentLanes[I] = Agent.Lateral;
                OpponentRecycled[I] = true;
            }
            OpponentS[I] = Agent.Distance; OpponentLanes[I] = Agent.Lateral; OpponentSpeeds[I] = Agent.Speed;
            if (RaceFinishDistance > 0 && OpponentFinishTimes[I] <= 0 && Agent.Distance >= RaceFinishDistance)
            {
                const double Fraction = FMath::Clamp((RaceFinishDistance - PreviousOpponentS[I])
                    / FMath::Max(.0001, Agent.Distance - PreviousOpponentS[I]), 0.0, 1.0);
                OpponentFinishTimes[I] = RaceClock - H + H * Fraction;
            }
        }
        return;
    }
    for (int32 I = 0; I < OpponentS.Num(); ++I)
    {
        const float K = FMath::Max(FMath::Abs(Curvature(OpponentS[I] + 25)), FMath::Abs(Curvature(OpponentS[I] + 90)));
        const float Desired = FMath::Clamp(FMath::Sqrt(16.f / FMath::Max(K, .001f)), 14.f, 68.f + I);
        OpponentSpeeds[I] = FMath::FInterpConstantTo(OpponentSpeeds[I], Desired, H, Desired < OpponentSpeeds[I] ? 12 : 5);
        OpponentS[I] += OpponentSpeeds[I] * H;
    }
}

void AVroomCircuit::RenderOpponents(float Alpha)
{
    if (OpponentMeshes.Num() != 5 || PreviousOpponentS.Num() != OpponentS.Num()) return;
    Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
    bool Changed = RenderedOpponentS.Num() != OpponentS.Num() || RenderedOpponentLanes.Num() != OpponentS.Num();
    // The traffic planner can choose a lane in one simulation step. Ease only
    // the visible steering yaw; keep its safety-tested positions and speeds
    // untouched. Continue settling at rest, and freeze this state during pause.
    const double VisualDt = Running && !Paused && GetWorld() ? FMath::Clamp(static_cast<double>(GetWorld()->GetDeltaSeconds()), 0.0, .1) : 0;
    const double YawAlpha = 1 - FMath::Exp(-12.0 * VisualDt);
    for (int32 I = 0; I < OpponentS.Num(); ++I)
    {
        const double TargetYaw = ReactiveTraffic && I < static_cast<int32>(Traffic.size())
            ? FMath::Atan2(Traffic[I].LaneVelocity, FMath::Max(8.0, OpponentSpeeds[I])) : 0;
        const double BeforeYaw = OpponentVisualYaw[I];
        OpponentVisualYaw[I] += (TargetYaw - BeforeYaw) * YawAlpha;
        Changed |= !FMath::IsNearlyEqual(BeforeYaw, OpponentVisualYaw[I], 1e-7);
    }
    for (int32 I = 0; I < OpponentS.Num() && !Changed; ++I)
        Changed = !FMath::IsNearlyEqual(RenderedOpponentS[I], FMath::Lerp(PreviousOpponentS[I], OpponentS[I], Alpha), 1e-6)
            || !FMath::IsNearlyEqual(RenderedOpponentLanes[I], FMath::Lerp(PreviousOpponentLanes[I], OpponentLanes[I], Alpha), 1e-6);
    if (!Changed)
    {
        if (OpponentMotionNeedsReset)
        {
            // Clear the last frame's velocity once when stopping or pausing;
            // otherwise a skipped update leaves stale motion vectors in TSR.
            for (int32 Group = 0; Group < OpponentMeshes.Num(); ++Group)
                OpponentMeshes[Group]->BatchUpdateInstancesTransforms(0,
                    RenderedOpponentTransforms[Group], RenderedOpponentTransforms[Group], true, false, false);
            OpponentMotionNeedsReset = false;
        }
        return;
    }

    // The previous transform is the last rendered pose, not the last 120-Hz
    // physics pose. Reuse both buffers and preserve BuildOpponents ordering:
    // four bulk updates, with no per-frame transform-buffer allocations.
    for (auto& Transforms : NextOpponentTransforms) Transforms.Reset();
    RenderedOpponentS.SetNumUninitialized(OpponentS.Num());
    RenderedOpponentLanes.SetNumUninitialized(OpponentS.Num());
    for (int32 I = 0; I < OpponentS.Num(); ++I)
    {
        const double Distance = FMath::Lerp(PreviousOpponentS[I], OpponentS[I], Alpha);
        RenderedOpponentS[I] = Distance;
        const double Lane = FMath::Lerp(PreviousOpponentLanes[I], OpponentLanes[I], Alpha);
        RenderedOpponentLanes[I] = Lane;
        FRotator Facing = Rotation(Distance);
        Facing.Yaw += FMath::RadiansToDegrees(OpponentVisualYaw[I]);
        const FTransform Base(Facing, Position(Distance, Lane));
        for (int32 J = 0; J < 4; ++J)
            NextOpponentTransforms[OpponentPartGroup(I, J)].Add(OpponentPartTransform(J) * Base);
        int32 WishboneCount = 0;
        const Vroom::Wishbone* Wishbones = Vroom::CarWishbones(WishboneCount);
        for (int32 Bar = 0; Bar < WishboneCount; ++Bar)
            NextOpponentTransforms[4].Add(OpponentWishboneTransform(Wishbones[Bar]) * Base);
    }
    for (int32 Group = 0; Group < OpponentMeshes.Num(); ++Group)
    {
        auto& Current = NextOpponentTransforms[Group];
        auto& Previous = RenderedOpponentTransforms[Group];
        const bool HasPrevious = Previous.Num() == Current.Num();
        // A newly built grid has no movement from its construction at identity.
        // Batch updates already mark instance data dirty in UE 5.8. Recreating
        // the whole proxy here prevents UpdateInstances_RenderThread from
        // enabling per-instance velocity, leaving visible TSR trails.
        OpponentMeshes[Group]->BatchUpdateInstancesTransforms(0, Current,
            HasPrevious ? Previous : Current, true, false, false);
        OpponentMotionNeedsReset |= HasPrevious;
        Swap(Current, Previous);
    }
}
