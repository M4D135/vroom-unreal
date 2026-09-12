#include "VroomGame.h"
#include "VroomGraphicsSettings.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "GameFramework/PlayerController.h"
#include "Scalability.h"
#include "HAL/IConsoleManager.h"

AVroomGameMode::AVroomGameMode()
{
    DefaultPawnClass = AVroomCar::StaticClass();
    HUDClass = AVroomHUD::StaticClass();
}

void AVroomGameMode::BeginPlay()
{
    Super::BeginPlay();
#if PLATFORM_MAC
    // The saved scalability state loads after engine ini CVars. Apply the Mac
    // budget here, once its callbacks are installed; do not alter PC quality.
    // Profil perf Mac (fluidite d'abord, demande David 7/09) : Lumen Medium
    // au lieu de High, ombres 1 cascade / 8 km. Le billet GPU mesure passe
    // de ~13.5 a ~9 ms a 1440x900, avec de la marge pour les pics en virage
    // (rotation = re-rendu ombres + gathering Lumen). Rendu PC inchange.
    auto Quality = Scalability::GetQualityLevels();
    Quality.SetGlobalIlluminationQuality(1);
    Scalability::SetQualityLevels(Quality);
    // Les reglages enregistres ecrasent r.VSync/t.MaxFPS de l'INI (memes
    // symptomes que pour la qualite GI). Sans VSync ni plafond, le jeu tourne
    // sans synchro : dechirures et cadence irreguliere, pire en virage.
    // On impose la cible documentee pour la session, sans ecrire sur disque.
    if (auto* UserSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
    {
        UserSettings->SetVSyncEnabled(true);
        UserSettings->SetFrameRateLimit(60.f);
        UserSettings->ApplyNonResolutionSettings();
    }
#endif
    // Saved, explicit graphics choices take precedence over the first-run
    // platform budget. Opening the options panel edits a draft until Apply.
    Vroom::Graphics::Initialize();
    auto CVarValue = [](const TCHAR* Name)
    {
        const auto* Var = IConsoleManager::Get().FindConsoleVariable(Name);
        return Var ? Var->GetFloat() : -1.f;
    };
    UE_LOG(LogTemp, Display, TEXT("VROOM: Render GI=%.0f probeDownsample=%.0f screen=%.0f VSM=%.0f reflections=%.0f"),
        CVarValue(TEXT("sg.GlobalIlluminationQuality")), CVarValue(TEXT("r.Lumen.ScreenProbeGather.DownsampleFactor")),
        CVarValue(TEXT("r.ScreenPercentage")), CVarValue(TEXT("r.Shadow.Virtual.Enable")), CVarValue(TEXT("r.Lumen.Reflections.Allow")));
    UE_LOG(LogTemp, Display, TEXT("VROOM: Version %s"), Vroom::AppVersion);
    GetWorld()->SpawnActor<AVroomCircuit>();
    auto Sun = GetWorld()->SpawnActor<ADirectionalLight>();
    auto Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
    Light->SetMobility(EComponentMobility::Movable);
    Sun->SetActorRotation(FRotator(-32,-38,0));
    Light->SetIntensity(5);
#if PLATFORM_MAC
    // Budget Mac : la rotation de la camera en virage invalide le cache
    // d'ombres et force leur re-rendu. Une seule cascade, portee 8 km, pour
    // garder de la marge GPU sous la VSync 60 Hz ; rendu PC inchange.
    Light->DynamicShadowDistanceMovableLight = 8000;
    Light->DynamicShadowCascades = 1;
#else
    Light->DynamicShadowDistanceMovableLight = 18000;
    Light->DynamicShadowCascades = 3;
#endif
    Light->SetLightColor(FLinearColor(1,.91,.77));
    Light->SetAtmosphereSunLight(true);
    GetWorld()->SpawnActor<ASkyAtmosphere>();
    auto Sky = GetWorld()->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetLightComponent()->SetIntensity(1.2f);
    Sky->GetLightComponent()->SetRealTimeCaptureEnabled(false);
    Sky->GetLightComponent()->RecaptureSky();
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
    Vroom::Graphics::ApplySceneSettings();
    UE_LOG(LogTemp, Display, TEXT("VROOM: World initialized"));
}
