#include "VroomGame.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

namespace
{
constexpr float MirrorInterval = 1.f / 15.f;
constexpr double MirrorX = 200;
constexpr double MirrorY = 70;
constexpr double MirrorZ = 12;

struct FMirrorGeometry
{
    TArray<FVector> Vertices, Normals;
    TArray<int32> Triangles;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;

    void Vertex(FVector P, FVector N, FVector2D UV)
    {
        Vertices.Add(P); Normals.Add(N.GetSafeNormal()); UVs.Add(UV);
        Colors.Add(FLinearColor::White);
        FVector Tangent = FVector::RightVector - N * FVector::DotProduct(N, FVector::RightVector);
        if (!Tangent.Normalize()) Tangent = FVector::UpVector;
        Tangents.Add(FProcMeshTangent(Tangent, false));
    }
    void Triangle(int32 A, int32 B, int32 C)
    {
        if (FVector::DotProduct(FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]), Normals[A]) > 0)
            Swap(B, C);
        Triangles.Append({A, B, C});
    }
    void Face(const TArray<FVector>& Points, FVector N)
    {
        const int32 S = Vertices.Num();
        for (const FVector& P : Points) Vertex(P, N, FVector2D(P.Y, P.Z) * .1);
        for (int32 I = 1; I + 1 < Points.Num(); ++I) Triangle(S, S + I, S + I + 1);
    }
    static TArray<FVector2D> Outline(double W, double H, double Corner)
    {
        return {{-W + Corner, -H}, {W - Corner, -H}, {W, -H + Corner}, {W, H - Corner},
            {W - Corner, H}, {-W + Corner, H}, {-W, H - Corner}, {-W, -H + Corner}};
    }
    void Panel(FVector Center, double W, double H, double Depth, double Corner)
    {
        const auto Points = Outline(W, H, Corner);
        TArray<FVector> Front, Back;
        for (const FVector2D& P : Points)
        {
            Front.Add(Center + FVector(-Depth * .5, P.X, P.Y));
            Back.Add(Center + FVector(Depth * .5, P.X, P.Y));
        }
        Face(Front, -FVector::ForwardVector); Face(Back, FVector::ForwardVector);
        for (int32 I = 0; I < Points.Num(); ++I)
        {
            const int32 J = (I + 1) % Points.Num();
            const FVector2D D = Points[J] - Points[I];
            Face({Front[I], Back[I], Back[J], Front[J]}, FVector(0, D.Y, -D.X).GetSafeNormal());
        }
    }
    void Rod(FVector A, FVector B, double Radius)
    {
        const FVector Along = (B - A).GetSafeNormal();
        const FVector U = FVector::CrossProduct(Along, FVector::UpVector).GetSafeNormal();
        const FVector V = FVector::CrossProduct(Along, U).GetSafeNormal();
        const int32 Start = Vertices.Num();
        constexpr int32 Sides = 12;
        for (int32 End = 0; End < 2; ++End)
            for (int32 I = 0; I <= Sides; ++I)
            {
                const double Angle = UE_TWO_PI * I / Sides;
                const FVector N = U * FMath::Cos(Angle) + V * FMath::Sin(Angle);
                Vertex((End ? B : A) + N * Radius, N, FVector2D(double(I) / Sides, End));
            }
        for (int32 I = 0; I < Sides; ++I)
        {
            const int32 S = Start + I;
            Triangle(S, S + 1, S + Sides + 1);
            Triangle(S + 1, S + Sides + 2, S + Sides + 1);
        }
    }
    void Glass(FVector Center, float Side)
    {
        constexpr double W = 6.65, H = 3.15;
        const auto Points = Outline(W, H, .65);
        const int32 S = Vertices.Num();
        for (const FVector2D& P : Points)
        {
            const double Across = (P.X / W + 1.0) * .5;
            // A rear-facing camera reverses the car's left/right. Flip U back
            // for a mirror, then select each side with a small central overlap.
            const double U = Side < 0 ? FMath::Lerp(1.0, .46, Across)
                : FMath::Lerp(.54, 0.0, Across);
            Vertex(Center + FVector(0, P.X, P.Y), -FVector::ForwardVector,
                FVector2D(U, (1.0 - P.Y / H) * .5));
        }
        for (int32 I = 1; I + 1 < Points.Num(); ++I) Triangle(S, S + I, S + I + 1);
    }
    void Commit(UProceduralMeshComponent* Mesh, int32 Section, UMaterialInterface* Material)
    {
        Mesh->CreateMeshSection_LinearColor(Section, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
        Mesh->SetMaterial(Section, Material);
    }
};

UMaterialInterface* MirrorMaterial(const TCHAR* Name)
{
    return LoadObject<UMaterialInterface>(nullptr,
        *FString::Printf(TEXT("/Game/Materials/%s.%s"), Name, Name));
}
}

void AVroomCar::BuildMirrors()
{
    if (MirrorRoot) return;
    MirrorRoot = NewObject<USceneComponent>(this, TEXT("MirrorRoot"));
    MirrorRoot->SetupAttachment(RootComponent);
    MirrorRoot->SetRelativeLocation(FVector(0, 0, 62));
    MirrorRoot->RegisterComponent();

    MirrorTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("RearMirrorImage"));
    MirrorTarget->RenderTargetFormat = RTF_RGBA16f;
    MirrorTarget->bAutoGenerateMips = false;
    MirrorTarget->AddressX = TA_Clamp;
    MirrorTarget->AddressY = TA_Clamp;
    MirrorTarget->ClearColor = FLinearColor(.12f, .22f, .30f, 1);
    // Linear HDR is fed to an unlit material and tone-mapped by the main view
    // only once. This avoids darkening an already tone-mapped LDR capture.
    MirrorTarget->InitCustomFormat(512, 128, PF_FloatRGBA, true);
    MirrorTarget->UpdateResourceImmediate(true);

    MirrorCapture = NewObject<USceneCaptureComponent2D>(this, TEXT("RearMirrorCapture"));
    MirrorCapture->SetupAttachment(RootComponent);
    MirrorCapture->SetRelativeLocation(FVector(-20, 0, 145));
    MirrorCapture->SetRelativeRotation(FRotator(-3, 180, 0));
    MirrorCapture->FOVAngle = 110;
    MirrorCapture->TextureTarget = MirrorTarget;
    MirrorCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDRNoAlpha;
    MirrorCapture->bCaptureEveryFrame = false;
    MirrorCapture->bCaptureOnMovement = false;
    MirrorCapture->bAlwaysPersistRenderingState = false;
    MirrorCapture->bUseRayTracingIfEnabled = false;
    MirrorCapture->LODDistanceFactor = 3;
    MirrorCapture->MaxViewDistanceOverride = 22000;
    MirrorCapture->HiddenActors.Add(this);
    MirrorCapture->ProfilingEventName = TEXT("VroomRearMirrors");
    TArray<FEngineShowFlagsSetting> Flags;
    for (const TCHAR* Name : {TEXT("DynamicShadows"), TEXT("DistanceFieldAO"), TEXT("AmbientOcclusion"),
        TEXT("LumenGlobalIllumination"), TEXT("LumenReflections"), TEXT("GlobalIllumination"),
        TEXT("ScreenSpaceReflections"), TEXT("ReflectionEnvironment"), TEXT("VolumetricFog"),
        TEXT("MotionBlur"), TEXT("TemporalAA"), TEXT("PostProcessing")})
    {
        FEngineShowFlagsSetting Flag;
        Flag.ShowFlagName = Name; Flag.Enabled = false; Flags.Add(Flag);
    }
    MirrorCapture->SetShowFlagSettings(Flags);
    MirrorCapture->PostProcessBlendWeight = 1;
    MirrorCapture->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
    MirrorCapture->PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
    MirrorCapture->PostProcessSettings.bOverride_ReflectionMethod = true;
    MirrorCapture->PostProcessSettings.ReflectionMethod = EReflectionMethod::None;
    MirrorCapture->RegisterComponent();

    auto* SurfaceMaterial = MirrorMaterial(TEXT("M_MirrorSurface"));
    auto* Surface = SurfaceMaterial ? UMaterialInstanceDynamic::Create(SurfaceMaterial, this) : nullptr;
    if (Surface) Surface->SetTextureParameterValue(TEXT("MirrorImage"), MirrorTarget);
    else UE_LOG(LogTemp, Error, TEXT("VROOM: Missing M_MirrorSurface; run bootstrap_cockpit_assets.py"));

    for (float Side : {-1.f, 1.f})
    {
        auto* Mesh = NewObject<UProceduralMeshComponent>(this);
        Mesh->SetupAttachment(MirrorRoot);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCastShadow(false);
        Mesh->SetAffectDistanceFieldLighting(false);
        Mesh->SetAffectDynamicIndirectLighting(false);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->RegisterComponent();
        MirrorMeshes.Add(Mesh);

        const FVector Center(MirrorX, Side * MirrorY, MirrorZ);
        FMirrorGeometry Red;
        Red.Panel(Center, 8, 4.5, 3, 1.5);
        Red.Rod(FVector(169, Side * 43, -4), Center + FVector(1, -Side * 6, -2), 1.7);
        Red.Commit(Mesh, 0, MirrorMaterial(TEXT("M_Red")));
        FMirrorGeometry Gasket;
        Gasket.Panel(Center + FVector(-1.65, 0, 0), 7.15, 3.65, .35, 1);
        Gasket.Commit(Mesh, 1, MirrorMaterial(TEXT("M_GlovePatch")));
        FMirrorGeometry Strut;
        Strut.Rod(FVector(206, Side * 64, 13), FVector(188, Side * 43, 12), .65);
        Strut.Rod(FVector(160, Side * 38, 4), FVector(250, Side * 74, -15), 1.0);
        Strut.Commit(Mesh, 2, MirrorMaterial(TEXT("M_White")));
        FMirrorGeometry Glass;
        Glass.Glass(Center + FVector(-1.9, 0, 0), Side);
        Glass.Commit(Mesh, 3, Surface);
    }
    MirrorClock = MirrorInterval;
    UE_LOG(LogTemp, Display, TEXT("VROOM: Rear mirrors ready: shared 512x128 HDR capture at 15 Hz"));
}

void AVroomCar::UpdateMirrors(float Dt)
{
    if (!MirrorRoot || !MirrorCapture) return;
    const bool Visible = IsCockpitVisible();
    if (MirrorRoot->IsVisible() != Visible) MirrorRoot->SetVisibility(Visible, true);
    if (ChaseCamera)
    {
        MirrorClock = MirrorInterval;
        return;
    }
    if (!FMath::IsFinite(Dt) || Dt < 0) return;
    MirrorClock += FMath::Min(Dt, .1f);
    if (MirrorClock >= MirrorInterval)
    {
        MirrorClock = FMath::Fmod(MirrorClock, MirrorInterval);
        // Deferred capture uses the final opponent positions for this frame.
        MirrorCapture->CaptureSceneDeferred();
    }
}
