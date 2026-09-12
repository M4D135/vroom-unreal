#include "VroomGame.h"
#include "VroomTrackPreview.h"
#include "VroomGraphicsSettings.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
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
}

AVroomCircuit::AVroomCircuit()
{
    PrimaryActorTick.bCanEverTick = false;
    Spline = CreateDefaultSubobject<USplineComponent>(TEXT("CircuitSpline"));
    RootComponent = Spline;
    Surface = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Asphalt"));
    Surface->SetupAttachment(RootComponent);
    Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Terrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CircuitTerrain"));
    Terrain->SetupAttachment(RootComponent);
    Terrain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
    // Static terrain follows the nearest road elevation. A separate grass verge
    // covers the grid close to the track; no per-frame terrain generation occurs.
    Terrain->ClearAllMeshSections();
    FBox Bounds(ForceInit);
    for (int32 I = 0; I < Spline->GetNumberOfSplinePoints(); ++I)
        Bounds += Spline->GetLocationAtSplinePoint(I, ESplineCoordinateSpace::World);
    Bounds = Bounds.ExpandBy(FVector(40000,40000,0));
    const int32 NX = FMath::CeilToInt(Bounds.GetSize().X / 3200);
    const int32 NY = FMath::CeilToInt(Bounds.GetSize().Y / 3200);
    TArray<FVector> V, Normals;
    TArray<int32> T;
    TArray<FVector2D> UV;
    TArray<FProcMeshTangent> Tangents;
    double MinHeight = 0;
    for (const auto& P : Tracks[TrackIndex].Elevation.Samples) MinHeight = FMath::Min(MinHeight, P.HeightM);
    for (int32 Y = 0; Y <= NY; ++Y)
    {
        for (int32 X = 0; X <= NX; ++X)
        {
            FVector P(Bounds.Min.X + Bounds.GetSize().X * X / NX, Bounds.Min.Y + Bounds.GetSize().Y * Y / NY, 0);
            const float Key = Spline->FindInputKeyClosestToWorldLocation(P);
            const FVector Road = Spline->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World);
            const float S = Spline->GetDistanceAlongSplineAtSplineInputKey(Key) / 100;
            const double DistanceM = FVector::Dist2D(P, Road) / 100;
            const double Taper = FMath::SmoothStep(180.0, 350.0, DistanceM);
            P.Z = FMath::Lerp(Elevation(S).HeightM - 2.5, MinHeight - 12, Taper) * 100;
            V.Add(P); UV.Add(FVector2D(P.X / 1600, P.Y / 1600));
            if (X < NX && Y < NY)
            {
                const int32 I = Y * (NX + 1) + X;
                T.Append({I, I+NX+2, I+1, I, I+NX+1, I+NX+2});
            }
        }
    }
    UKismetProceduralMeshLibrary::CalculateTangentsForMesh(V, T, UV, Normals, Tangents);
    Terrain->CreateMeshSection_LinearColor(0, V, T, Normals, UV, TArray<FLinearColor>{}, Tangents, false);
    Terrain->SetMaterial(0, Mat(TEXT("M_Grass")));
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
    auto Trees = Group(TEXT("Cone"), TEXT("M_Foliage"));
    auto Trunks = Group(TEXT("Cylinder"), TEXT("M_Wood"));
    UInstancedStaticMeshComponent* Ads[] = {
        Group(TEXT("Plane"), TEXT("M_Ad01")), Group(TEXT("Plane"), TEXT("M_Ad02")),
        Group(TEXT("Plane"), TEXT("M_Ad03")), Group(TEXT("Plane"), TEXT("M_Ad04"))};
    StartLights = Group(TEXT("Cylinder"), TEXT("M_StartLight"));
    StartLights->ComponentTags.Add(FName(TEXT("VroomStartLights")));
    StartLights->SetNumCustomDataFloats(1);
    StartLightStage = -1;

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
    for (float S = 0; S < LengthM(); S += 12)
    {
        for (float Side : {-1.f, 1.f})
        {
            if (static_cast<int32>(S) % 36 == 0)
            {
                auto TreePoint = Position(S, Side * Random.FRandRange(28, 110));
                const float Key = Spline->FindInputKeyClosestToWorldLocation(TreePoint);
                if (FVector::Dist2D(TreePoint, Spline->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World)) > 2000)
                {
                    const float H = Random.FRandRange(7, 15);
                    TreePoint.Z = Elevation(Spline->GetDistanceAlongSplineAtSplineInputKey(Key) / 100).HeightM * 100 - 250;
                    Trees->AddInstance(FTransform(FRotator(0, Random.FRandRange(0, 360), 0), TreePoint + FVector(0, 0, H * 65), FVector(H * .55, H * .55, H)), true);
                    Trunks->AddInstance(FTransform(FRotator::ZeroRotator, TreePoint + FVector(0, 0, H * 30), FVector(.35, .35, H * .6)), true);
                }
            }
        }
    }

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

    // Collision volumes come from the same opaque instances as the scenery.
    // Exclude ground, leaves, grass and billboard artwork. The real trunks,
    // posts, pit walls, supports and trusses retain their actual height.
    constexpr double CellSize = 4000.0;
    UInstancedStaticMeshComponent* SolidGroups[] = {Posts, White, Red, Metal, RoundMetal, Windows, Trunks};
    for (auto* SolidGroup : SolidGroups)
    {
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
                FVector Expanded = Hazard.HalfExtent;
                for (int32 Axis = 0; Axis < 3; ++Axis)
                {
                    const FVector HazardAxis = Hazard.Transform.GetUnitAxis(static_cast<EAxis::Type>(Axis + 1));
                    Expanded[Axis] += FMath::Abs(FVector::DotProduct(HazardAxis, CarAxes[0])) * CarHalf.X
                        + FMath::Abs(FVector::DotProduct(HazardAxis, CarAxes[1])) * CarHalf.Y
                        + FMath::Abs(FVector::DotProduct(HazardAxis, CarAxes[2])) * CarHalf.Z;
                }
                double Fraction = 1;
                int NormalAxis = 0, NormalSide = 1;
                if (!Vroom::ContactModel::SweepObstacle({A.X,A.Y,A.Z}, {B.X,B.Y,B.Z},
                    {Expanded.X,Expanded.Y,Expanded.Z}, Fraction, NormalAxis, NormalSide) || Fraction > OutFraction)
                    continue;
                Hit = true;
                OutFraction = Fraction;
                FVector Normal = FVector::ZeroVector;
                Normal[NormalAxis] = NormalSide;
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
    OpponentMotionNeedsReset = false;
    static const TCHAR* Materials[] = { TEXT("M_F1Paint"), TEXT("M_F1Pearl"), TEXT("M_F1Carbon"), TEXT("M_F1Rubber") };
    static const TCHAR* MeshNames[] = { TEXT("SM_F1OpponentPaint"), TEXT("SM_F1OpponentTrim"),
        TEXT("SM_F1OpponentCarbon"), TEXT("SM_F1OpponentWheels") };
    for (int32 Group = 0; Group < 4; ++Group)
    {
        RenderedOpponentTransforms[Group].Reset(12);
        NextOpponentTransforms[Group].Reset(12);
        auto C = NewObject<UInstancedStaticMeshComponent>(this);
        C->SetupAttachment(RootComponent);
        C->SetMobility(EComponentMobility::Movable);
        const FString MeshPath = FString::Printf(TEXT("/Game/Meshes/%s.%s"), MeshNames[Group], MeshNames[Group]);
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
        OpponentMeshes.Add(C);
    }
    for (int32 I = 0; I < 12; ++I)
    {
        OpponentS.Add(40 + (I / 2) * 16);
        OpponentLanes.Add(I % 2 ? 2.2 : -2.2);
        OpponentSpeeds.Add(0);
        static const FLinearColor Liveries[] = { FLinearColor(.012f,.075f,.65f), FLinearColor(.72f,.012f,.018f),
            FLinearColor(.91f,.60f,.012f), FLinearColor(.02f,.35f,.13f),
            FLinearColor(.33f,.025f,.57f), FLinearColor(.88f,.19f,.012f) };
        const FLinearColor Colour = Liveries[(I / 2) % UE_ARRAY_COUNT(Liveries)];
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
    }
    PreviousOpponentS = OpponentS;
    RenderedOpponentS.Init(TNumericLimits<double>::Max(), OpponentS.Num());
    RenderOpponents(1);
}

void AVroomCircuit::AdvanceRace(float H)
{
    if (Tracks.IsEmpty() || !Running || Paused || !FMath::IsFinite(H) || H <= 0) return;
    PreviousOpponentS = OpponentS;
    RaceClock += H;
    const int32 LightStage = RaceClock < 0
        ? FMath::Clamp(FMath::CeilToInt((3.0 + RaceClock) * 5.0 / 3.0), 1, 5) : 0;
    if (StartLights && LightStage != StartLightStage)
    {
        for (int32 I = 0; I < StartLights->GetInstanceCount(); ++I)
            StartLights->SetCustomDataValue(I, 0, I / 2 < LightStage ? 1.f : 0.f, false);
        StartLights->MarkRenderStateDirty();
        StartLightStage = LightStage;
        UE_LOG(LogTemp, Display, TEXT("VROOM: Start lights columns=%d raceClock=%.3f"), LightStage, RaceClock);
    }
    if (RaceClock < 0) return;
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
    if (OpponentMeshes.Num() != 4 || PreviousOpponentS.Num() != OpponentS.Num()) return;
    Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
    bool Changed = RenderedOpponentS.Num() != OpponentS.Num();
    for (int32 I = 0; I < OpponentS.Num() && !Changed; ++I)
        Changed = !FMath::IsNearlyEqual(RenderedOpponentS[I], FMath::Lerp(PreviousOpponentS[I], OpponentS[I], Alpha), 1e-6);
    if (!Changed)
    {
        if (OpponentMotionNeedsReset)
        {
            // Clear the last frame's velocity once when stopping or pausing;
            // otherwise a skipped update leaves stale motion vectors in TSR.
            for (int32 Group = 0; Group < 4; ++Group)
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
    for (int32 I = 0; I < OpponentS.Num(); ++I)
    {
        const double Distance = FMath::Lerp(PreviousOpponentS[I], OpponentS[I], Alpha);
        RenderedOpponentS[I] = Distance;
        const FTransform Base(Rotation(Distance), Position(Distance, OpponentLanes[I]));
        for (int32 J = 0; J < 4; ++J)
            NextOpponentTransforms[OpponentPartGroup(I, J)].Add(OpponentPartTransform(J) * Base);
    }
    for (int32 Group = 0; Group < 4; ++Group)
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
