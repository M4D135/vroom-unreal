#include "VroomGraphicsSettings.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Components/DirectionalLightComponent.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "SceneUtils.h"

#ifndef VROOM_WITH_DLSS
#define VROOM_WITH_DLSS 0
#endif
#if VROOM_WITH_DLSS
#include "DLSSLibrary.h"
#endif

namespace Vroom::Graphics
{
namespace
{
constexpr const TCHAR* Section = TEXT("Vroom.Graphics");
constexpr int NumberOfRows = 13;
constexpr int FrameCaps[] = {30, 60, 120, 0};
constexpr int AAMethods[] = {AAM_None, AAM_FXAA, AAM_TemporalAA, AAM_TSR};

struct FSettings
{
    int RenderScale = 70;
    int AA = 2;
    int Shadows = 1;
    int GI = 0;
    int Reflections = 0;
    int Foliage = 1;
    bool Wind = true;
    int Effects = 1;
    bool MotionBlur = false;
    int FrameCap = 1;
    bool VSync = true;
    // Native AA/render scale are retained so disabling DLSS restores them.
    int DLSSMode = 0;
};

FSettings Applied, Pending;
bool bInitialized = false;
bool bOpen = false;
int Selection = 0;

#if VROOM_WITH_DLSS
bool HasDLSSRuntime()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DLSS"));
    return Plugin.IsValid() && Plugin->IsEnabled() &&
        FModuleManager::Get().IsModuleLoaded(TEXT("DLSS")) &&
        FModuleManager::Get().IsModuleLoaded(TEXT("DLSSBlueprint"));
}
#endif

bool DLSSSupported()
{
#if VROOM_WITH_DLSS
    return HasDLSSRuntime() && UDLSSLibrary::IsDLSSSupported();
#else
    return false;
#endif
}

bool DLSSScreenPercentage(int Mode, float& Percentage)
{
#if VROOM_WITH_DLSS
    static constexpr UDLSSMode Modes[] = {UDLSSMode::Off, UDLSSMode::DLAA,
        UDLSSMode::Quality, UDLSSMode::Balanced, UDLSSMode::Performance};
    if (Mode < 1 || Mode > 4 || !DLSSSupported() || !UDLSSLibrary::IsDLSSModeSupported(Modes[Mode]))
        return false;
    FVector2D Resolution(1920, 1080);
    if (GEngine && GEngine->GameViewport)
    {
        FVector2D ViewportSize;
        GEngine->GameViewport->GetViewportSize(ViewportSize);
        if (ViewportSize.X > 0 && ViewportSize.Y > 0)
            Resolution = ViewportSize;
    }
    bool bSupported = false, bFixedPercentage = false;
    float Minimum = 0, Maximum = 0, Sharpness = 0;
    UDLSSLibrary::GetDLSSModeInformation(Modes[Mode], Resolution, bSupported, Percentage,
        bFixedPercentage, Minimum, Maximum, Sharpness);
    return bSupported && FMath::IsFinite(Percentage) && Percentage > 0 && Percentage <= 100;
#else
    return false;
#endif
}

bool DLSSModeSupported(int Mode)
{
    float Percentage = 0;
    return Mode == 0 || DLSSScreenPercentage(Mode, Percentage);
}

template <typename T>
void SetUserCVar(const TCHAR* Name, T Value)
{
    if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
    {
        // UE's user-override priority is above project/device defaults and
        // below explicit command-line/debug overrides. Do not clear flags.
        CVar->Set(Value, ECVF_SetByGameOverride);
    }
}

void SetPreset(FSettings& Settings, int Preset)
{
    static constexpr int Scale[] = {70, 85, 100, 100};
    static constexpr int Shadows[] = {1, 2, 3, 3};
    static constexpr int Foliage[] = {1, 2, 3, 3};
    static constexpr int Effects[] = {1, 2, 3, 3};
    Preset = FMath::Clamp(Preset, 0, 3);
    Settings.RenderScale = Scale[Preset];
    Settings.AA = Preset < 2 ? 2 : 3;
    Settings.Shadows = Shadows[Preset];
    Settings.GI = Preset;
    Settings.Reflections = Preset;
    Settings.Foliage = Foliage[Preset];
    Settings.Effects = Effects[Preset];
    Settings.Wind = true;
    Settings.MotionBlur = false;
}

int PresetIndex(const FSettings& Settings)
{
    for (int Index = 0; Index < 4; ++Index)
    {
        FSettings Preset;
        SetPreset(Preset, Index);
        if (Settings.RenderScale == Preset.RenderScale && Settings.AA == Preset.AA &&
            Settings.Shadows == Preset.Shadows && Settings.GI == Preset.GI &&
            Settings.Reflections == Preset.Reflections && Settings.Foliage == Preset.Foliage &&
            Settings.Effects == Preset.Effects && Settings.Wind == Preset.Wind &&
            Settings.MotionBlur == Preset.MotionBlur)
            return Index;
    }
    return 4;
}

void Sanitize(FSettings& Settings)
{
    Settings.RenderScale = FMath::Clamp(FMath::RoundToInt(Settings.RenderScale / 5.f) * 5, 50, 100);
    Settings.AA = FMath::Clamp(Settings.AA, 0, 3);
    Settings.Shadows = FMath::Clamp(Settings.Shadows, 0, 3);
    Settings.GI = FMath::Clamp(Settings.GI, 0, 3);
    Settings.Reflections = FMath::Clamp(Settings.Reflections, 0, 3);
    Settings.Foliage = FMath::Clamp(Settings.Foliage, 0, 3);
    Settings.Effects = FMath::Clamp(Settings.Effects, 0, 3);
    Settings.FrameCap = FMath::Clamp(Settings.FrameCap, 0, 3);
    Settings.DLSSMode = FMath::Clamp(Settings.DLSSMode, 0, 4);
    if (!DLSSModeSupported(Settings.DLSSMode))
        Settings.DLSSMode = 0;
}

void Load(FSettings& Settings)
{
    if (!GConfig)
        return;
    GConfig->GetInt(Section, TEXT("RenderScale"), Settings.RenderScale, GGameUserSettingsIni);
    GConfig->GetInt(Section, TEXT("AntiAliasing"), Settings.AA, GGameUserSettingsIni);
    GConfig->GetInt(Section, TEXT("Shadows"), Settings.Shadows, GGameUserSettingsIni);
    GConfig->GetInt(Section, TEXT("GlobalIllumination"), Settings.GI, GGameUserSettingsIni);
    GConfig->GetInt(Section, TEXT("Reflections"), Settings.Reflections, GGameUserSettingsIni);
    GConfig->GetInt(Section, TEXT("Foliage"), Settings.Foliage, GGameUserSettingsIni);
    GConfig->GetBool(Section, TEXT("AnimatedGrass"), Settings.Wind, GGameUserSettingsIni);
    GConfig->GetInt(Section, TEXT("Effects"), Settings.Effects, GGameUserSettingsIni);
    GConfig->GetBool(Section, TEXT("MotionBlur"), Settings.MotionBlur, GGameUserSettingsIni);
    GConfig->GetInt(Section, TEXT("FrameCap"), Settings.FrameCap, GGameUserSettingsIni);
    GConfig->GetBool(Section, TEXT("VSync"), Settings.VSync, GGameUserSettingsIni);
    const bool bSavedDLSSChoice = GConfig->GetInt(Section, TEXT("DLSSMode"), Settings.DLSSMode, GGameUserSettingsIni);
    if (!bSavedDLSSChoice && DLSSModeSupported(2))
        Settings.DLSSMode = 2;
    Sanitize(Settings);
}

void Save(const FSettings& Settings)
{
    if (!GConfig)
        return;
    GConfig->SetInt(Section, TEXT("Version"), 1, GGameUserSettingsIni);
    GConfig->SetInt(Section, TEXT("RenderScale"), Settings.RenderScale, GGameUserSettingsIni);
    GConfig->SetInt(Section, TEXT("AntiAliasing"), Settings.AA, GGameUserSettingsIni);
    GConfig->SetInt(Section, TEXT("Shadows"), Settings.Shadows, GGameUserSettingsIni);
    GConfig->SetInt(Section, TEXT("GlobalIllumination"), Settings.GI, GGameUserSettingsIni);
    GConfig->SetInt(Section, TEXT("Reflections"), Settings.Reflections, GGameUserSettingsIni);
    GConfig->SetInt(Section, TEXT("Foliage"), Settings.Foliage, GGameUserSettingsIni);
    GConfig->SetBool(Section, TEXT("AnimatedGrass"), Settings.Wind, GGameUserSettingsIni);
    GConfig->SetInt(Section, TEXT("Effects"), Settings.Effects, GGameUserSettingsIni);
    GConfig->SetBool(Section, TEXT("MotionBlur"), Settings.MotionBlur, GGameUserSettingsIni);
    GConfig->SetInt(Section, TEXT("FrameCap"), Settings.FrameCap, GGameUserSettingsIni);
    GConfig->SetBool(Section, TEXT("VSync"), Settings.VSync, GGameUserSettingsIni);
    GConfig->SetInt(Section, TEXT("DLSSMode"), Settings.DLSSMode, GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}

void ApplyDirectionalShadows(int Quality)
{
    if (!GEngine)
        return;
    static constexpr int Cascades[] = {1, 1, 3, 4};
    static constexpr float Distance[] = {0, 8000, 14000, 22000};
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        UWorld* World = Context.World();
        if (!World || !World->IsGameWorld())
            continue;
        for (TActorIterator<ADirectionalLight> It(World); It; ++It)
        {
            if (auto* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
            {
                Light->SetCastShadows(Quality > 0);
                Light->SetDynamicShadowCascades(Cascades[Quality]);
                Light->SetDynamicShadowDistanceMovableLight(Distance[Quality]);
            }
        }
    }
}

void ApplyToEngine(FSettings& Settings, bool bSave)
{
    float EffectiveScale = static_cast<float>(Settings.RenderScale);
    if (Settings.DLSSMode != 0 && !DLSSScreenPercentage(Settings.DLSSMode, EffectiveScale))
    {
        Settings.DLSSMode = 0;
        EffectiveScale = static_cast<float>(Settings.RenderScale);
    }
    UGameUserSettings* User = GEngine ? GEngine->GetGameUserSettings() : nullptr;
    if (User)
    {
        User->SetResolutionScaleValueEx(static_cast<float>(Settings.RenderScale));
        User->SetAntiAliasingQuality(Settings.DLSSMode != 0 || Settings.AA != 0 ? 3 : 0);
        User->SetShadowQuality(Settings.Shadows);
        User->SetGlobalIlluminationQuality(Settings.GI);
        User->SetReflectionQuality(Settings.Reflections);
        User->SetFoliageQuality(Settings.Foliage);
        User->SetVisualEffectQuality(Settings.Effects);
        User->SetPostProcessingQuality(Settings.Effects);
        User->SetViewDistanceQuality(FMath::Max(1, Settings.Foliage));
        User->SetTextureQuality(FMath::Max(1, Settings.Effects));
        User->SetShadingQuality(Settings.Effects);
        User->SetLandscapeQuality(Settings.Foliage);
        User->SetVSyncEnabled(Settings.VSync);
        User->SetFrameRateLimit(static_cast<float>(FrameCaps[Settings.FrameCap]));
        User->SetDynamicResolutionEnabled(false);
        User->ApplyNonResolutionSettings();
    }
    SetUserCVar(TEXT("r.ScreenPercentage"), EffectiveScale);
    SetUserCVar(TEXT("r.AntiAliasingMethod"), Settings.DLSSMode ? AAM_TemporalAA : AAMethods[Settings.AA]);
#if VROOM_WITH_DLSS
    if (HasDLSSRuntime())
    {
        UDLSSLibrary::EnableDLSS(Settings.DLSSMode != 0);
        if (Settings.DLSSMode != 0 && !UDLSSLibrary::IsDLSSEnabled())
        {
            UE_LOG(LogTemp, Warning, TEXT("VROOM: DLSS activation refused by runtime; restoring native rendering"));
            Settings.DLSSMode = 0;
            EffectiveScale = static_cast<float>(Settings.RenderScale);
            SetUserCVar(TEXT("r.ScreenPercentage"), EffectiveScale);
            SetUserCVar(TEXT("r.AntiAliasingMethod"), AAMethods[Settings.AA]);
        }
    }
#endif
    SetUserCVar(TEXT("r.VSync"), Settings.VSync ? 1 : 0);
    SetUserCVar(TEXT("t.MaxFPS"), static_cast<float>(FrameCaps[Settings.FrameCap]));
    // On UE 5.8, GI Medium also enables Lumen. Keep the two light presets
    // explicitly below that cost; High and Ultra use the scalable Lumen path.
    SetUserCVar(TEXT("r.Lumen.DiffuseIndirect.Allow"), Settings.GI >= 2 ? 1 : 0);
    SetUserCVar(TEXT("r.Lumen.Reflections.Allow"), Settings.Reflections >= 2 ? 1 : 0);
    SetUserCVar(TEXT("r.MotionBlurQuality"), Settings.MotionBlur ? 3 : 0);
    SetUserCVar(TEXT("r.DefaultFeature.MotionBlur"), Settings.MotionBlur ? 1 : 0);
    SetUserCVar(TEXT("r.MotionBlur.Amount"), Settings.MotionBlur ? 0.25f : 0.f);
    ApplyDirectionalShadows(Settings.Shadows);
    IConsoleManager::Get().CallAllConsoleVariableSinks();
    if (bSave)
    {
        if (User)
            User->SaveSettings();
        Save(Settings);
    }
    UE_LOG(LogTemp, Display,
        TEXT("VROOM: Graphics applied scale=%.1f AA=%d shadows=%d GI=%d reflections=%d foliage=%d wind=%d effects=%d blur=%d fps=%d vsync=%d DLSS=%d saved=%d"),
        EffectiveScale, Settings.DLSSMode ? AAM_TemporalAA : AAMethods[Settings.AA], Settings.Shadows, Settings.GI,
        Settings.Reflections, Settings.Foliage, Settings.Wind, Settings.Effects,
        Settings.MotionBlur, FrameCaps[Settings.FrameCap], Settings.VSync, Settings.DLSSMode, bSave);
}

FString DLSSStatus()
{
#if PLATFORM_MAC
    return TEXT("Indisponible sur Mac");
#elif !PLATFORM_WINDOWS
    return TEXT("Disponible sur Windows RTX");
#else
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DLSS"));
    if (!Plugin.IsValid())
        return TEXT("Plugin non installé");
    if (!Plugin->IsEnabled())
        return TEXT("Plugin désactivé");
#if !VROOM_WITH_DLSS
    return TEXT("Recompiler avec le plugin");
#else
    if (!HasDLSSRuntime())
        return TEXT("Module non chargé");
    switch (UDLSSLibrary::QueryDLSSSupport())
    {
    case UDLSSSupport::Supported: return TEXT("Disponible");
    case UDLSSSupport::NotSupportedIncompatibleHardware: return TEXT("GPU non compatible");
    case UDLSSSupport::NotSupportedDriverOutOfDate: return TEXT("Pilote NVIDIA à mettre à jour");
    case UDLSSSupport::NotSupportedOperatingSystemOutOfDate: return TEXT("Windows à mettre à jour");
    case UDLSSSupport::NotSupportedIncompatibleAPICaptureToolActive: return TEXT("Capture GPU incompatible");
    case UDLSSSupport::NotSupportedByPlatformAtBuildTime: return TEXT("Plateforme non compatible");
    default: return TEXT("Non pris en charge");
    }
#endif
#endif
}

FString QualityName(int Quality)
{
    static const TCHAR* Names[] = {TEXT("Désactivé"), TEXT("Faible"), TEXT("Élevé"), TEXT("Ultra")};
    return Names[FMath::Clamp(Quality, 0, 3)];
}
}

void Initialize()
{
    if (bInitialized)
        return;
#if PLATFORM_MAC
    SetPreset(Applied, 0);
    Applied.FrameCap = 1;
#else
    SetPreset(Applied, 3);
    Applied.FrameCap = 2;
#endif
    Load(Applied);
    // Reproducible visible validation without modifying the player's saved profile.
    int32 PreviewPreset = -1;
    if (FParse::Value(FCommandLine::Get(), TEXT("VroomGraphicsPreset="), PreviewPreset)
        && PreviewPreset >= 0 && PreviewPreset <= 3)
    {
        SetPreset(Applied, PreviewPreset);
        Applied.FrameCap = 1;
        Applied.VSync = true;
        Applied.DLSSMode = 0;
        UE_LOG(LogTemp, Display, TEXT("VROOM: Temporary graphics preset=%d (preferences preserved)"), PreviewPreset);
    }
    Pending = Applied;
    bInitialized = true;
    ApplyToEngine(Applied, false);
}

bool IsOpen() { return bOpen; }
void ApplySceneSettings() { ApplyDirectionalShadows(Applied.Shadows); }

void Toggle()
{
    Initialize();
    if (bOpen)
    {
        Cancel();
        return;
    }
    Pending = Applied;
    bOpen = true;
}

int RowCount() { return NumberOfRows; }

FString Label(int Row)
{
    static const TCHAR* Names[] = {TEXT("Préréglage"), TEXT("Résolution interne"), TEXT("Anticrénelage"),
        TEXT("Ombres"), TEXT("Éclairage global"), TEXT("Reflets"), TEXT("Végétation"), TEXT("Vent dans l’herbe"),
        TEXT("Effets et détails"), TEXT("Flou de mouvement"), TEXT("Limite d’images/s"), TEXT("Synchronisation verticale"),
        TEXT("NVIDIA DLSS")};
    return Row >= 0 && Row < NumberOfRows ? Names[Row] : TEXT("");
}

FString Value(int Row)
{
    const FSettings& Settings = bOpen ? Pending : Applied;
    switch (Row)
    {
    case 0:
    {
        static const TCHAR* Names[] = {TEXT("Performance"), TEXT("Équilibré"), TEXT("Élevé"), TEXT("Ultra"), TEXT("Personnalisé")};
        return Names[PresetIndex(Settings)];
    }
    case 1:
    {
        float Percentage = 0;
        if (Settings.DLSSMode && DLSSScreenPercentage(Settings.DLSSMode, Percentage))
            return FString::Printf(TEXT("%.1f %% · DLSS"), Percentage);
        return FString::Printf(TEXT("%d %%"), Settings.RenderScale);
    }
    case 2:
    {
        if (Settings.DLSSMode)
            return Settings.DLSSMode == 1 ? TEXT("NVIDIA DLAA") : TEXT("NVIDIA DLSS");
        static const TCHAR* Names[] = {TEXT("Désactivé"), TEXT("FXAA"), TEXT("TAA"), TEXT("TSR")};
        return Names[Settings.AA];
    }
    case 3: return QualityName(Settings.Shadows);
    case 4:
    {
        static const TCHAR* Names[] = {TEXT("Ambiant"), TEXT("Occlusion ambiante"), TEXT("Lumen élevé"), TEXT("Lumen ultra")};
        return Names[Settings.GI];
    }
    case 5: return QualityName(Settings.Reflections);
    case 6:
    {
        static const TCHAR* Names[] = {TEXT("Désactivée"), TEXT("Clairsemée"), TEXT("Moyenne"), TEXT("Dense")};
        return Names[Settings.Foliage];
    }
    case 7: return Settings.Wind ? TEXT("Activé") : TEXT("Désactivé");
    case 8:
    {
        static const TCHAR* Names[] = {TEXT("Faible"), TEXT("Moyen"), TEXT("Élevé"), TEXT("Ultra")};
        return Names[Settings.Effects];
    }
    case 9: return Settings.MotionBlur ? TEXT("Activé") : TEXT("Désactivé");
    case 10: return FrameCaps[Settings.FrameCap] ? FString::FromInt(FrameCaps[Settings.FrameCap]) : TEXT("Sans limite");
    case 11: return Settings.VSync ? TEXT("Activée") : TEXT("Désactivée");
    case 12:
    {
        if (!DLSSSupported())
            return DLSSStatus();
        static const TCHAR* Names[] = {TEXT("Désactivé"), TEXT("DLAA · natif"), TEXT("Qualité"), TEXT("Équilibré"), TEXT("Performance")};
        return Names[Settings.DLSSMode];
    }
    default: return TEXT("");
    }
}

FString Description(int Row)
{
    const FSettings& Settings = bOpen ? Pending : Applied;
    if ((Row == 1 || Row == 2) && Settings.DLSSMode)
        return TEXT("Piloté par le mode NVIDIA DLSS. Désactiver DLSS restaure vos réglages de résolution et d’anticrénelage.");
    switch (Row)
    {
    case 0: return TEXT("Performance favorise la fluidité. Ultra privilégie les détails et l’éclairage.");
    case 1: return TEXT("Réduit les pixels calculés. Le texte reste à la résolution de la fenêtre.");
    case 2: return TEXT("TAA adoucit les contours. TSR reconstruit les détails à basse résolution, avec un coût supérieur.");
    case 3: return TEXT("Règle les détails et la portée des ombres. Plus bas libère du temps pour chaque image.");
    case 4: return TEXT("Les deux premiers niveaux sont légers. Lumen ajoute l’éclairage indirect aux niveaux supérieurs.");
    case 5: return TEXT("Ajuste les reflets. Les niveaux Élevé et Ultra utilisent Lumen selon la plateforme.");
    case 6: return TEXT("Règle la densité de l’herbe et les détails de la végétation autour de la piste.");
    case 7: return TEXT("Anime l’herbe au vent. Désactiver immobilise les brins sans supprimer la végétation.");
    case 8: return TEXT("Règle les particules, les textures et les effets après rendu.");
    case 9: return TEXT("Ajoute un flou léger aux mouvements rapides. Désactivé conserve une image plus nette.");
    case 10: return TEXT("Choisir une cadence stable laisse de la marge au GPU. Sans limite augmente sa charge.");
    case 11: return TEXT("Synchronise les images avec l’écran pour éviter les déchirures. La fréquence de l’écran reste la limite.");
    case 12: return TEXT("DLSS reconstruit une image plus fine ; DLAA travaille en résolution native. Aucune génération d’images.");
    default: return TEXT("");
    }
}

bool IsEnabled(int Row)
{
    if (Row < 0 || Row >= NumberOfRows)
        return false;
    if (Row == 12)
        return DLSSSupported();
    const FSettings& Settings = bOpen ? Pending : Applied;
    return !((Row == 1 || Row == 2) && Settings.DLSSMode != 0);
}

int SelectedRow() { return Selection; }
void SelectRow(int Row) { Selection = FMath::Clamp(Row, 0, NumberOfRows - 1); }

void Adjust(int Direction)
{
    if (!bOpen || !IsEnabled(Selection) || Direction == 0)
        return;
    Direction = Direction > 0 ? 1 : -1;
    auto Cycle = [Direction](int& Value, int Count) { Value = (Value + Direction + Count) % Count; };
    switch (Selection)
    {
    case 0:
    {
        int Preset = PresetIndex(Pending);
        Preset = Preset == 4 ? (Direction > 0 ? 0 : 3) : (Preset + Direction + 4) % 4;
        SetPreset(Pending, Preset);
        break;
    }
    case 1: Pending.RenderScale = FMath::Clamp(Pending.RenderScale + Direction * 5, 50, 100); break;
    case 2: Cycle(Pending.AA, 4); break;
    case 3: Cycle(Pending.Shadows, 4); break;
    case 4: Cycle(Pending.GI, 4); break;
    case 5: Cycle(Pending.Reflections, 4); break;
    case 6: Cycle(Pending.Foliage, 4); break;
    case 7: Pending.Wind = !Pending.Wind; break;
    case 8: Cycle(Pending.Effects, 4); break;
    case 9: Pending.MotionBlur = !Pending.MotionBlur; break;
    case 10: Cycle(Pending.FrameCap, 4); break;
    case 11: Pending.VSync = !Pending.VSync; break;
    case 12:
        for (int Attempt = 0; Attempt < 5; ++Attempt)
        {
            Cycle(Pending.DLSSMode, 5);
            if (DLSSModeSupported(Pending.DLSSMode))
                break;
        }
        break;
    default: break;
    }
}

void Apply()
{
    if (!bOpen)
        return;
    Sanitize(Pending);
    Applied = Pending;
    ApplyToEngine(Applied, true);
    bOpen = false;
}

void Cancel()
{
    Pending = Applied;
    bOpen = false;
}

bool AnimatedGrass() { return Applied.Wind; }
int FoliageQuality() { return Applied.Foliage; }
}
