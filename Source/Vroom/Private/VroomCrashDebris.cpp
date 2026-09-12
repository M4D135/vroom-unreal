#include "VroomGame.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

void AVroomCar::BuildCrashDebris()
{
    if (!CrashDebrisWheel)
    {
        CrashDebrisWheel = NewObject<UStaticMeshComponent>(this, TEXT("CrashDebrisWheel"));
        CrashDebrisWheel->SetupAttachment(RootComponent);
        // Keep ownership with the car, but never drag a detached tyre with it.
        CrashDebrisWheel->SetAbsolute(true, true, true);
        CrashDebrisWheel->SetMobility(EComponentMobility::Movable);
        CrashDebrisWheel->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,
            TEXT("/Game/Meshes/SM_F1WheelFront.SM_F1WheelFront")));
        CrashDebrisWheel->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Game/Materials/M_F1Rubber.M_F1Rubber")));
        CrashDebrisWheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        CrashDebrisWheel->SetGenerateOverlapEvents(false);
        CrashDebrisWheel->SetSimulatePhysics(false);
        CrashDebrisWheel->SetCastShadow(false);
        CrashDebrisWheel->SetAffectDistanceFieldLighting(false);
        CrashDebrisWheel->SetAffectDynamicIndirectLighting(false);
        CrashDebrisWheel->SetCanEverAffectNavigation(false);
        CrashDebrisWheel->SetVisibility(false);
        CrashDebrisWheel->RegisterComponent();
    }
    ResetCrashDebris();
}

void AVroomCar::ResetCrashDebris()
{
    // UpdatePresentation restores BodyParts before UpdateCrashDebris. Preserve
    // that visibility (including respawn blinking) rather than forcing a tyre on.
    if (BodyParts.IsValidIndex(DetachedTyreIndex) && BodyParts[DetachedTyreIndex])
    {
        const bool BodyVisible = Contacts.State.InvulnerabilityRemaining <= 0
            || FMath::Fmod(Contacts.State.InvulnerabilityRemaining, .2) >= .08;
        BodyParts[DetachedTyreIndex]->SetVisibility(BodyVisible);
    }
    if (CrashDebrisWheel) CrashDebrisWheel->SetVisibility(false);
    DetachedTyreIndex = INDEX_NONE;
    CrashDebrisWasBroken = false;
    CrashDebrisActive = false;
    CrashDebrisAge = 0;
    CrashDebrisGroundZ = 0;
    CrashDebrisVelocity = FVector::ZeroVector;
    CrashDebrisSpin = FVector::ZeroVector;
}

void AVroomCar::UpdateCrashDebris(float Dt)
{
    const auto Phase = Contacts.State.Phase;
    // A tyre-contact jump becomes Crashed only on a damaging landing. A small
    // intact jump and a slow bump must not detach a wheel.
    const bool Broken = Contacts.State.TyresBroken && (Phase == Vroom::ContactPhase::Crashed
        || Phase == Vroom::ContactPhase::Recovering);
    if (!Broken)
    {
        if (CrashDebrisWasBroken || CrashDebrisActive || DetachedTyreIndex != INDEX_NONE)
            ResetCrashDebris();
        return;
    }
    if (!CrashDebrisWheel) return;

    if (!CrashDebrisWasBroken)
    {
        CrashDebrisWasBroken = true;
        const double Side = Contacts.State.ImpactSide < 0 ? -1.0 : 1.0;
        int32 FallbackIndex = INDEX_NONE;
        for (int32 I : TyrePartIndices)
        {
            if (!BodyParts.IsValidIndex(I)) continue;
            const auto* Part = BodyParts[I].Get();
            if (!Part || !Part->GetStaticMesh()) continue;
            const FVector Local = Part->GetRelativeLocation();
            if (Local.X <= 0) continue;
            if (FallbackIndex == INDEX_NONE) FallbackIndex = I;
            if (Local.Y * Side > 0) { DetachedTyreIndex = I; break; }
        }
        if (DetachedTyreIndex == INDEX_NONE) DetachedTyreIndex = FallbackIndex;
        if (!BodyParts.IsValidIndex(DetachedTyreIndex)) return;

        const FTransform Start = BodyParts[DetachedTyreIndex]->GetComponentTransform();
        CrashDebrisWheel->SetWorldTransform(Start);
        FVector Forward = GetActorForwardVector();
        Forward.Z = 0;
        if (!Forward.Normalize()) Forward = FVector::ForwardVector;
        const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
        // Centimetres/second. Add a 5 m/s forward impulse to the remaining car
        // velocity, so the wheel stays ahead while the wreck brakes to a stop.
        const double ForwardSpeed = FMath::Max(0.0, Drive.State.SpeedMps) * 100.0 + 500.0;
        CrashDebrisVelocity = Forward * ForwardSpeed + Right * (Side * 200.0)
            + FVector::UpVector * 900.0;
        CrashDebrisSpin = FVector(360.0 * Side, 220.0, 900.0 * Side);
        const FVector Scale = Start.GetScale3D().GetAbs();
        const UStaticMesh* DetachedMesh = BodyParts[DetachedTyreIndex]->GetStaticMesh();
        const FVector Extent = DetachedMesh->GetBoundingBox().GetExtent();
        // The generated wheels rotate around local Y and use actual cm units.
        const double RadiusCm = FMath::Max(Scale.X * Extent.X, Scale.Z * Extent.Z);
        const double RoadZ = Circuit
            ? Circuit->Position(Drive.State.DistanceM, Drive.State.LateralM).Z
            : GetActorLocation().Z - 6.0;
        // A local ground plane is sufficient for this short cosmetic trajectory.
        // Store the minimum centre height, including the wheel's radius.
        CrashDebrisGroundZ = RoadZ + RadiusCm;
        CrashDebrisAge = 0;
        CrashDebrisActive = true;
        CrashDebrisWheel->SetVisibility(true);
        UE_LOG(LogTemp, Display, TEXT("VROOM: Detached front tyre side=%+.0f phase=%d"),
            Side, static_cast<int32>(Phase));
    }

    // This must run after the car's normal visibility/blink loop on every frame.
    if (BodyParts.IsValidIndex(DetachedTyreIndex) && BodyParts[DetachedTyreIndex])
        BodyParts[DetachedTyreIndex]->SetVisibility(false);
    if (!CrashDebrisActive || !FMath::IsFinite(Dt) || Dt <= 0) return;

    float Remaining = FMath::Min(Dt, .1f);
    while (Remaining > 0 && CrashDebrisAge < 1.9f)
    {
        const float H = FMath::Min(FMath::Min(Remaining, 1.f / 120.f), 1.9f - CrashDebrisAge);
        FVector P = CrashDebrisWheel->GetComponentLocation();
        P += CrashDebrisVelocity * H + FVector(0, 0, -490.0 * H * H);
        CrashDebrisVelocity.Z -= 980.0 * H;
        if (P.Z < CrashDebrisGroundZ)
        {
            P.Z = CrashDebrisGroundZ;
            if (CrashDebrisVelocity.Z < 0)
            {
                CrashDebrisVelocity.Z *= -.32;
                CrashDebrisVelocity.X *= .72;
                CrashDebrisVelocity.Y *= .72;
                CrashDebrisSpin *= .7;
                if (CrashDebrisVelocity.Z < 40) CrashDebrisVelocity.Z = 0;
            }
        }
        CrashDebrisWheel->SetWorldLocation(P);
        CrashDebrisWheel->AddLocalRotation(FRotator(
            CrashDebrisSpin.X * H, CrashDebrisSpin.Y * H, CrashDebrisSpin.Z * H));
        CrashDebrisAge += H;
        Remaining -= H;
    }
    if (CrashDebrisAge >= 1.9f)
    {
        CrashDebrisActive = false;
        CrashDebrisWheel->SetVisibility(false);
        // The original tyre stays absent until the recovery phase ends.
    }
}
