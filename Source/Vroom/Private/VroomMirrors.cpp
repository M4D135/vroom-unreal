#include "VroomGame.h"
#include "VroomProceduralMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

namespace
{
float MirrorInterval()
{
    // One shared rear capture serves both mirrors. Keep the 512x128 target
    // small; raise cadence rather than resolving detail smaller than a pixel.
    static const IConsoleVariable* Effects = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.EffectsQuality"));
    return 1.f / (Effects && Effects->GetInt() >= 2 ? 60.f : 30.f);
}
// Car-local coordinates, shared with the real body and front wheels. Their
// axle is at X=128 with a 35 cm tyre radius: the housings remain behind X=93.
constexpr double MirrorX = 80;
constexpr double MirrorY = 73;
constexpr double MirrorZ = 50;

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
        TArray<FVector2D> Result;
        // Real corner arcs keep the silhouette round at cockpit distance.
        for (int32 CornerIndex=0; CornerIndex<4; ++CornerIndex)
        {
            const double A0=CornerIndex*UE_PI*.5;
            const FVector2D C((CornerIndex==0 || CornerIndex==3) ? W-Corner : -W+Corner,
                CornerIndex<2 ? H-Corner : -H+Corner);
            for (int32 I=0; I<=6; ++I)
            {
                const double A=A0+UE_PI*.5*I/6;
                Result.Add(C+FVector2D(FMath::Cos(A),FMath::Sin(A))*Corner);
            }
        }
        return Result;
    }
    void Housing(FVector Center)
    {
        // A rolled front edge, broad shoulder and tapered rear cap form one
        // smooth shell. The dimensions still clear the physical front tyres.
        constexpr double X[] = {-1.7,-1.40,-.9,.9,1.55,1.7};
        constexpr double Inset[] = {.32,.08,0,.12,.55,.85};
        constexpr int32 Rings=6;
        const int32 Start=Vertices.Num();
        int32 Count=0;
        for (int32 R=0; R<Rings; ++R)
        {
            const auto Points=Outline(11.5-Inset[R],4.0-Inset[R],1.25);
            Count=Points.Num();
            const int32 Prev=FMath::Max(0,R-1), Next=FMath::Min(Rings-1,R+1);
            const double Axial=(Inset[Next]-Inset[Prev])/(X[Next]-X[Prev]);
            for (int32 I=0; I<Count; ++I)
            {
                const FVector2D D=Points[(I+1)%Count]-Points[(I+Count-1)%Count];
                const FVector2D Out=FVector2D(D.Y,-D.X).GetSafeNormal();
                Vertex(Center+FVector(X[R],Points[I].X,Points[I].Y),
                    FVector(Axial,Out.X,Out.Y),FVector2D(double(I)/Count,double(R)/(Rings-1)));
            }
        }
        for (int32 R=0; R<Rings-1; ++R) for (int32 I=0; I<Count; ++I)
        {
            const int32 A=Start+R*Count+I, B=Start+R*Count+(I+1)%Count;
            Triangle(A,B,A+Count); Triangle(B,B+Count,A+Count);
        }
        TArray<FVector> Front,Back;
        for (const FVector2D& P:Outline(11.18,3.68,1.25)) Front.Add(Center+FVector(-1.7,P.X,P.Y));
        for (const FVector2D& P:Outline(10.65,3.15,1.25)) Back.Add(Center+FVector(1.7,P.X,P.Y));
        Face(Front,-FVector::ForwardVector); Face(Back,FVector::ForwardVector);
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
        constexpr double W = 10.0, H = 3.0;
        // Each mirror uses 54% of the 4:1 shared image. Crop vertically to
        // preserve the rear cars' proportions in the new wider glass opening.
        constexpr double CaptureCropHeight = .54 * 4.0 / (W / H);
        const auto Points = Outline(W, H, .75);
        const int32 S = Vertices.Num();
        for (const FVector2D& P : Points)
        {
            const double Across = (P.X / W + 1.0) * .5;
            // A rear-facing camera reverses the car's left/right. Flip U back
            // for a mirror, then select each side with a small central overlap.
            const double U = Side < 0 ? FMath::Lerp(1.0, .46, Across)
                : FMath::Lerp(.54, 0.0, Across);
            Vertex(Center + FVector(0, P.X, P.Y), -FVector::ForwardVector,
                FVector2D(U, .5 - P.Y / H * CaptureCropHeight * .5));
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
    MirrorRoot->SetRelativeLocation(FVector::ZeroVector);
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
        auto* Mesh = NewObject<UVroomProceduralMeshComponent>(this);
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
        Red.Housing(Center);
        // Low red elbow, like the reference cockpit: a short coaming mount
        // leads into a horizontal arm joining the inner bottom of the casing.
        // Every part stays behind the front tyres; no diagonal white brace.
        const FVector Anchor(40, Side * 35, 45);
        const FVector Elbow(72, Side * 38, 45);
        const FVector Mount = Center + FVector(0, -Side * 10, -3);
        Red.Rod(Anchor, Elbow, 1.4);
        Red.Rod(Elbow, Mount, 1.4);
        Red.Commit(Mesh, 0, MirrorMaterial(TEXT("M_F1Paint")));
        FMirrorGeometry Gasket;
        Gasket.Panel(Center + FVector(-1.85, 0, 0), 10.45, 3.4, .35, 1);
        Gasket.Commit(Mesh, 1, MirrorMaterial(TEXT("M_GlovePatch")));
        FMirrorGeometry Glass;
        Glass.Glass(Center + FVector(-2.09, 0, 0), Side);
        Glass.Commit(Mesh, 2, Surface);
    }
    // Seed the longest interval so either initial camera mode captures once
    // immediately rather than showing the render target's clear colour.
    MirrorClock = .1f;
    UE_LOG(LogTemp, Display, TEXT("VROOM: Rear mirrors ready: shared 512x128 HDR capture, cockpit 30/60 Hz by applied effects quality, chase 10 Hz"));
}

void AVroomCar::UpdateMirrors(float Dt)
{
    if (!MirrorRoot || !MirrorCapture) return;
    const bool Visible = IsCockpitVisible();
    if (MirrorRoot->IsVisible() != Visible) MirrorRoot->SetVisibility(Visible, true);
    if (!FMath::IsFinite(Dt) || Dt < 0) return;
    MirrorClock += FMath::Min(Dt, .1f);
    // The same glass is visible outside the car; refresh it at reduced cost
    // in chase view instead of leaving a stale cockpit reflection on screen.
    const float Interval = ChaseCamera ? .1f : MirrorInterval();
    if (MirrorClock + 1.e-6f >= Interval)
    {
        MirrorClock = FMath::Fmod(FMath::Max(MirrorClock, Interval), Interval);
        // Deferred capture uses the final opponent positions for this frame.
        MirrorCapture->CaptureSceneDeferred();
    }
}
