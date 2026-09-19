#include "VroomPlayerSettings.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"

namespace Vroom::Settings
{
namespace
{
FPlayerSettings Applied, Pending;
bool Ready = false, Open = false, Capturing = false, IgnoreOpeningInput = false;
int Selected = 0;
constexpr int ParameterRows = 10;
constexpr TCHAR Section[] = TEXT("Vroom.Player");
const TCHAR* ActionNames[] = {TEXT("Accélérer"),TEXT("Freiner"),TEXT("Tourner à gauche"),TEXT("Tourner à droite"),
    TEXT("Déraper"),TEXT("Rapport supérieur"),TEXT("Rapport inférieur"),TEXT("Démarrer / valider"),TEXT("Pause"),TEXT("Retour au menu"),
    TEXT("Recommencer"),TEXT("Changer de vue"),TEXT("Boîte automatique"),TEXT("Conduite souris"),TEXT("Changer les pneus"),TEXT("Ravitaillement"),
    TEXT("Graphismes"),TEXT("Commandes et son"),TEXT("Étape suivante"),TEXT("Circuit suivant"),TEXT("Circuit précédent"),TEXT("Mode suivant"),TEXT("Difficulté suivante")};
const TArray<FKey>& Defaults()
{
    static const TArray<FKey> Keys = {EKeys::W,EKeys::S,EKeys::A,EKeys::D,EKeys::LeftShift,EKeys::E,EKeys::X,EKeys::Enter,EKeys::P,EKeys::Escape,
        EKeys::R,EKeys::C,EKeys::G,EKeys::M,EKeys::T,EKeys::F,EKeys::O,EKeys::F8,EKeys::N,EKeys::PageDown,EKeys::PageUp,EKeys::Tab,EKeys::H};
    return Keys;
}
bool Bindable(const FKey& Key)
{
    return Key.IsValid()&&!Key.IsAnalog()&&!Key.IsGamepadKey()&&!Key.IsMouseButton();
}
bool Assign(FPlayerSettings& S,int Index,const FKey& Key)
{
    if(!S.Keys.IsValidIndex(Index)||!Bindable(Key))return false;
    const FKey Old=S.Keys[Index];
    for(int I=0;I<S.Keys.Num();++I)if(I!=Index&&S.Keys[I]==Key)S.Keys[I]=Old;
    S.Keys[Index]=Key;return true;
}
void Sanitize(FPlayerSettings& S)
{
    auto Safe = [](float V, float Low, float High, float Default) { return FMath::IsFinite(V) ? FMath::Clamp(V,Low,High) : Default; };
    S.SteeringSensitivity=Safe(S.SteeringSensitivity,.4f,2.f,1.f); S.SteeringDeadzone=Safe(S.SteeringDeadzone,0.f,.3f,.06f);
    S.MouseSensitivity=Safe(S.MouseSensitivity,.001f,.012f,.004f); S.Fov=Safe(S.Fov,60.f,105.f,68.f);
    S.MasterVolume=Safe(S.MasterVolume,0.f,1.f,1.f); S.EngineVolume=Safe(S.EngineVolume,0.f,1.f,1.f); S.EffectsVolume=Safe(S.EffectsVolume,0.f,1.f,1.f);
}
bool Extra(APlayerController* PC, Action What, bool Press)
{
    auto Test=[&](FKey Key)
    {
        if(!Key.IsGamepadKey())
            for(int I=0;I<Applied.Keys.Num();++I)
                if(I!=static_cast<int>(What)&&Applied.Keys[I]==Key)return false;
        return Press?PC->WasInputKeyJustPressed(Key):PC->IsInputKeyDown(Key);
    };
    // Fixed controller shortcuts remain available while keyboard bindings are customized.
    switch(What)
    {
    case Action::Drift: if(Test(EKeys::Gamepad_LeftShoulder)||Test(EKeys::Gamepad_RightShoulder)) return true; break;
    case Action::Start: case Action::NextStage: return Test(EKeys::Gamepad_FaceButton_Bottom);
    case Action::Menu: return Test(EKeys::Gamepad_FaceButton_Right);
    case Action::Pause: return Test(EKeys::Gamepad_Special_Right);
    case Action::View: return Test(EKeys::Gamepad_Special_Left);
    case Action::ShiftUp: return Test(EKeys::Gamepad_FaceButton_Top);
    case Action::ShiftDown: return Test(EKeys::Gamepad_FaceButton_Left);
    default: break;
    }
    if(Applied.Keys[static_cast<int>(What)]!=Defaults()[static_cast<int>(What)]) return false;
    switch(What)
    {
    case Action::Throttle: return Test(EKeys::Z)||Test(EKeys::Up);
    case Action::Brake: return Test(EKeys::Down)||Test(EKeys::SpaceBar);
    case Action::SteerLeft: return Test(EKeys::Q)||Test(EKeys::Left);
    case Action::SteerRight: return Test(EKeys::Right);
    case Action::Drift: return Test(EKeys::RightShift);
    case Action::Options: return Test(EKeys::F7);
    default: return false;
    }
}
void BeginBinding() { Capturing=true; }
}
void Initialize()
{
    if(Ready) return;
    Ready=true; Applied.Keys=Defaults();
    if(GConfig)
    {
        GConfig->GetFloat(Section,TEXT("SteeringSensitivity"),Applied.SteeringSensitivity,GGameUserSettingsIni);
        GConfig->GetFloat(Section,TEXT("SteeringDeadzone"),Applied.SteeringDeadzone,GGameUserSettingsIni);
        GConfig->GetFloat(Section,TEXT("MouseSensitivity"),Applied.MouseSensitivity,GGameUserSettingsIni);
        GConfig->GetFloat(Section,TEXT("Fov"),Applied.Fov,GGameUserSettingsIni);
        int32 Version = 1;
        GConfig->GetInt(Section,TEXT("Version"),Version,GGameUserSettingsIni);
        // Migrate only the former default; preserve an explicitly customised FOV.
        if (Version < 2 && FMath::IsNearlyEqual(Applied.Fov,80.f)) Applied.Fov=68.f;
        GConfig->GetBool(Section,TEXT("ChaseCamera"),Applied.ChaseCamera,GGameUserSettingsIni);
        GConfig->GetBool(Section,TEXT("Automatic"),Applied.Automatic,GGameUserSettingsIni);
        GConfig->GetBool(Section,TEXT("GhostEnabled"),Applied.GhostEnabled,GGameUserSettingsIni);
        GConfig->GetFloat(Section,TEXT("MasterVolume"),Applied.MasterVolume,GGameUserSettingsIni);
        GConfig->GetFloat(Section,TEXT("EngineVolume"),Applied.EngineVolume,GGameUserSettingsIni);
        GConfig->GetFloat(Section,TEXT("EffectsVolume"),Applied.EffectsVolume,GGameUserSettingsIni);
        for(int I=0;I<Applied.Keys.Num();++I)
        {
            FString Name;
            if(GConfig->GetString(Section,*FString::Printf(TEXT("Key%d"),I),Name,GGameUserSettingsIni))
            {
                const FKey Key{FName(*Name)};
                if(Bindable(Key)) Assign(Applied,I,Key);
            }
        }
    }
    Sanitize(Applied); Pending=Applied;
}
const FPlayerSettings& Get(){Initialize();return Applied;}
FPlayerSettings& Mutable(){Initialize();return Applied;}
void Save()
{
    Initialize(); Sanitize(Applied); if(!GConfig)return;
    GConfig->SetInt(Section,TEXT("Version"),2,GGameUserSettingsIni);
    GConfig->SetFloat(Section,TEXT("SteeringSensitivity"),Applied.SteeringSensitivity,GGameUserSettingsIni);
    GConfig->SetFloat(Section,TEXT("SteeringDeadzone"),Applied.SteeringDeadzone,GGameUserSettingsIni);
    GConfig->SetFloat(Section,TEXT("MouseSensitivity"),Applied.MouseSensitivity,GGameUserSettingsIni);
    GConfig->SetFloat(Section,TEXT("Fov"),Applied.Fov,GGameUserSettingsIni);
    GConfig->SetBool(Section,TEXT("ChaseCamera"),Applied.ChaseCamera,GGameUserSettingsIni);
    GConfig->SetBool(Section,TEXT("Automatic"),Applied.Automatic,GGameUserSettingsIni);
    GConfig->SetBool(Section,TEXT("GhostEnabled"),Applied.GhostEnabled,GGameUserSettingsIni);
    GConfig->SetFloat(Section,TEXT("MasterVolume"),Applied.MasterVolume,GGameUserSettingsIni);
    GConfig->SetFloat(Section,TEXT("EngineVolume"),Applied.EngineVolume,GGameUserSettingsIni);
    GConfig->SetFloat(Section,TEXT("EffectsVolume"),Applied.EffectsVolume,GGameUserSettingsIni);
    for(int I=0;I<Applied.Keys.Num();++I)GConfig->SetString(Section,*FString::Printf(TEXT("Key%d"),I),*Applied.Keys[I].GetFName().ToString(),GGameUserSettingsIni);
    GConfig->Flush(false,GGameUserSettingsIni);
    UE_LOG(LogTemp,Display,TEXT("VROOM: Player preferences saved FOV=%.0f sensitivity=%.2f deadzone=%.2f volumes=%.2f/%.2f/%.2f"),Applied.Fov,Applied.SteeringSensitivity,Applied.SteeringDeadzone,Applied.MasterVolume,Applied.EngineVolume,Applied.EffectsVolume);
}
bool WasPressed(APlayerController* PC,Action What){Initialize();return PC&&(PC->WasInputKeyJustPressed(Applied.Keys[static_cast<int>(What)])||Extra(PC,What,true));}
bool IsDown(APlayerController* PC,Action What){Initialize();return PC&&(PC->IsInputKeyDown(Applied.Keys[static_cast<int>(What)])||Extra(PC,What,false));}
FString BindingLabel(Action What){Initialize();return Applied.Keys[static_cast<int>(What)].GetDisplayName().ToString();}
bool IsOpen(){return Open;}
void Toggle(){Initialize();if(Open){Cancel();return;}Pending=Applied;Open=true;Capturing=false;IgnoreOpeningInput=true;}
int RowCount(){return ParameterRows+static_cast<int>(Action::Count);}
int SelectedRow(){return Selected;}
int FirstVisibleRow(){return Selected/VisibleRows*VisibleRows;}
void SelectRow(int Row){Selected=FMath::Clamp(Row,0,RowCount()-1);Capturing=false;}
bool IsBinding(){return Capturing;}
void Adjust(int Direction)
{
    if(Selected>=ParameterRows){BeginBinding();return;}
    switch(Selected)
    {
    case 0:Pending.SteeringSensitivity+=Direction*.1f;break;case 1:Pending.SteeringDeadzone+=Direction*.01f;break;
    case 2:Pending.MouseSensitivity+=Direction*.001f;break;case 3:Pending.Fov+=Direction*2.f;break;
    case 4:Pending.ChaseCamera=!Pending.ChaseCamera;break;case 5:Pending.Automatic=!Pending.Automatic;break;
    case 6:Pending.MasterVolume+=Direction*.1f;break;case 7:Pending.EngineVolume+=Direction*.1f;break;
    case 8:Pending.EffectsVolume+=Direction*.1f;break;case 9:Pending.GhostEnabled=!Pending.GhostEnabled;break;
    }Sanitize(Pending);
}
FString Label(int Row)
{
    const TCHAR* Names[]={TEXT("Sensibilité direction"),TEXT("Zone morte du stick"),TEXT("Sensibilité souris"),TEXT("Champ de vision"),TEXT("Vue au démarrage"),TEXT("Boîte de vitesses"),TEXT("Volume général"),TEXT("Volume moteur"),TEXT("Volume effets"),TEXT("Fantôme en chrono")};
    return Row<ParameterRows?Names[Row]:ActionNames[Row-ParameterRows];
}
FString Value(int Row)
{
    const FPlayerSettings&S=Open?Pending:Applied;
    if(Row>=ParameterRows)return Capturing&&Row==Selected?TEXT("APPUYER SUR UNE TOUCHE"):S.Keys[Row-ParameterRows].GetDisplayName().ToString();
    switch(Row)
    {
    case 0:return FString::Printf(TEXT("%.1f x"),S.SteeringSensitivity);case 1:return FString::Printf(TEXT("%.0f %%"),S.SteeringDeadzone*100);
    case 2:return FString::Printf(TEXT("%.0f / 12"),S.MouseSensitivity*1000);case 3:return FString::Printf(TEXT("%.0f°"),S.Fov);
    case 4:return S.ChaseCamera?TEXT("Poursuite"):TEXT("Cockpit");case 5:return S.Automatic?TEXT("Automatique"):TEXT("Manuelle");
    case 6:return FString::Printf(TEXT("%.0f %%"),S.MasterVolume*100);case 7:return FString::Printf(TEXT("%.0f %%"),S.EngineVolume*100);
    case 8:return FString::Printf(TEXT("%.0f %%"),S.EffectsVolume*100);case 9:return S.GhostEnabled?TEXT("Activé"):TEXT("Masqué");default:return TEXT("");
    }
}
FString Description(int Row)
{
    if(Row>=ParameterRows)return TEXT("Entrée pour attribuer une touche clavier. Échap annule la saisie. Une touche déjà utilisée est échangée.");
    const TCHAR* Help[]={TEXT("Ajuste l’amplitude du stick. Le clavier garde toute la plage de braquage."),TEXT("Ignore le mouvement résiduel du stick. Monter seulement si la voiture tourne seule."),TEXT("Vitesse de braquage lorsque la conduite souris est activée."),TEXT("Angle horizontal de caméra. La position du cockpit reste inchangée."),TEXT("Vue préférée, conservée à chaque lancement."),TEXT("En manuel, attribuer les rapports supérieur et inférieur dans les commandes."),TEXT("Niveau de tous les sons du jeu."),TEXT("Niveau du moteur du joueur."),TEXT("Niveau des pneus, impacts et effets de course."),TEXT("Rejoue votre meilleur tour propre dans le mode Chrono. Aucun contact avec la voiture fantôme.")};
    return Help[Row];
}
void Apply(){Applied=Pending;Save();Open=Capturing=IgnoreOpeningInput=false;}
void Cancel(){Pending=Applied;Open=Capturing=IgnoreOpeningInput=false;}
bool HandleMenuInput(APlayerController* PC)
{
    if(!Open||!PC)return false;
    if(IgnoreOpeningInput){IgnoreOpeningInput=false;return true;}
    if(Capturing)
    {
        if(PC->WasInputKeyJustPressed(EKeys::Escape)||PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right)){Capturing=false;return true;}
        TArray<FKey> Keys;EKeys::GetAllKeys(Keys);
        for(const FKey&Key:Keys)
            if(Bindable(Key)&&PC->WasInputKeyJustPressed(Key))
            { Assign(Pending,Selected-ParameterRows,Key);Capturing=false;break; }
        return true;
    }
    auto Press=[&](FKey K){return PC->WasInputKeyJustPressed(K);};
    if(Press(EKeys::Escape)||Press(EKeys::Gamepad_FaceButton_Right)){Cancel();return true;}
    if(Press(EKeys::Up)||Press(EKeys::Gamepad_DPad_Up))SelectRow((Selected+RowCount()-1)%RowCount());
    if(Press(EKeys::Down)||Press(EKeys::Gamepad_DPad_Down))SelectRow((Selected+1)%RowCount());
    if(Press(EKeys::Left)||Press(EKeys::Gamepad_DPad_Left))Adjust(-1);
    if(Press(EKeys::Right)||Press(EKeys::Gamepad_DPad_Right))Adjust(1);
    if(Press(EKeys::PageDown))SelectRow(FMath::Min(Selected+VisibleRows,RowCount()-1));
    if(Press(EKeys::PageUp))SelectRow(FMath::Max(Selected-VisibleRows,0));
    if(Press(EKeys::Enter)||Press(EKeys::Gamepad_FaceButton_Bottom)){if(Selected>=ParameterRows)BeginBinding();else Apply();}
    if(Press(EKeys::Gamepad_Special_Right))Apply();
    if(Press(EKeys::LeftMouseButton))
    {
        int W=0,H=0;float MX=0,MY=0;PC->GetViewportSize(W,H);
        const float Scale=FMath::Max(.01f,FMath::Min(W/1440.f,H/900.f));
        if(PC->GetMousePosition(MX,MY))
        {
            const float X=MX/Scale-(W/Scale*.5f-430),Y=MY/Scale-(H/Scale*.5f-400);
            if(Y>=733&&Y<=777){if(X>=598&&X<=836)Apply();else if(X>=24&&X<=196)Cancel();}
            else if(Y>=92&&Y<572&&X>=24&&X<=836)
            {
                const int Row=FirstVisibleRow()+FMath::FloorToInt((Y-92)/40);if(Row<RowCount()){SelectRow(Row);Adjust(X<620?-1:1);}
            }
            else if(Y>=581&&Y<=620){if(X>=24&&X<=196)SelectRow(FMath::Max(0,FirstVisibleRow()-VisibleRows));else if(X>=598&&X<=836)SelectRow(FMath::Min(RowCount()-1,FirstVisibleRow()+VisibleRows));}
        }
    }
    return true;
}
bool RunSelfTest()
{
    Initialize();FPlayerSettings Probe=Applied;Probe.Keys=Defaults();
    const bool Swap=Assign(Probe,static_cast<int>(Action::Throttle),EKeys::D)
        &&Probe.Keys[static_cast<int>(Action::Throttle)]==EKeys::D&&Probe.Keys[static_cast<int>(Action::SteerRight)]==EKeys::W;
    const bool Rejected=!Assign(Probe,0,FKey())&&!Assign(Probe,0,EKeys::MouseX)&&!Assign(Probe,0,EKeys::Gamepad_LeftX)&&!Assign(Probe,0,EKeys::LeftMouseButton);
    Probe.SteeringSensitivity=999;Probe.SteeringDeadzone=-5;Probe.Fov=999;Probe.EngineVolume=-1;Sanitize(Probe);
    const bool Bounds=Probe.SteeringSensitivity==2.f&&Probe.SteeringDeadzone==0&&Probe.Fov==105.f&&Probe.EngineVolume==0;
    const auto SavedPending=Pending;const bool SavedOpen=Open,SavedCapturing=Capturing,SavedIgnore=IgnoreOpeningInput;const int SavedRow=Selected;
    const float Original=Applied.SteeringSensitivity;Open=false;Toggle();SelectRow(0);Adjust(1);Cancel();
    const bool Cancelled=Applied.SteeringSensitivity==Original&&!Open;
    Pending=SavedPending;Open=SavedOpen;Capturing=SavedCapturing;IgnoreOpeningInput=SavedIgnore;Selected=SavedRow;
    const bool Pass=Swap&&Rejected&&Bounds&&Cancelled;
    UE_LOG(LogTemp,Display,TEXT("VROOM: Settings self-test %s swap=%d invalid-key=%d bounds=%d cancel=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Swap,Rejected,Bounds,Cancelled);
    return Pass;
}

}
