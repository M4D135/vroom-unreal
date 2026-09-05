#include "VroomGame.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "GameFramework/PlayerController.h"

AVroomGameMode::AVroomGameMode()
{
    DefaultPawnClass = AVroomCar::StaticClass();
    HUDClass = AVroomHUD::StaticClass();
}

void AVroomGameMode::BeginPlay()
{
    Super::BeginPlay();
    GetWorld()->SpawnActor<AVroomCircuit>();
    auto Sun = GetWorld()->SpawnActor<ADirectionalLight>();
    Sun->SetActorRotation(FRotator(-32,-38,0));
    auto Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
    Light->SetMobility(EComponentMobility::Movable);
    Light->SetIntensity(5);
    Light->SetLightColor(FLinearColor(1,.91,.77));
    Light->SetAtmosphereSunLight(true);
    GetWorld()->SpawnActor<ASkyAtmosphere>();
    auto Sky = GetWorld()->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetLightComponent()->SetIntensity(1.2f);
    Sky->GetLightComponent()->SetRealTimeCaptureEnabled(true);
    auto Fog = GetWorld()->SpawnActor<AExponentialHeightFog>();
    Fog->GetComponent()->SetFogDensity(.006f);
    Fog->GetComponent()->SetFogHeightFalloff(.2f);
    Fog->GetComponent()->SetStartDistance(5000);
    auto PP = GetWorld()->SpawnActor<APostProcessVolume>();
    PP->bUnbound = true;
    PP->Settings.bOverride_AutoExposureMethod = true;
    PP->Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    PP->Settings.bOverride_AutoExposureBias = true;
    PP->Settings.AutoExposureBias = 0;
    PP->Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    PP->Settings.AutoExposureApplyPhysicalCameraExposure = false;
    PP->Settings.bOverride_VignetteIntensity = true;
    PP->Settings.VignetteIntensity = .2f;
    UE_LOG(LogTemp, Display, TEXT("VROOM: World initialized"));
}
