#include "VroomMenuLayout.h"
#include "VroomGame.h"
#include "VroomGraphicsSettings.h"
#include "GameFramework/PlayerController.h"
namespace Vroom::MenuLayout
{
namespace
{
int Focus=0;int LastScreen=-1;
void ActivateItem(AVroomCar* Car,const FItem& I)
{
    switch(I.Kind)
    {
    case ItemKind::Track:Car->SelectSessionTrack(I.Value);break;
    case ItemKind::Mode:Car->SetSessionMode(static_cast<SessionMode>(I.Value));break;
    case ItemKind::Difficulty:Car->SetDifficulty(static_cast<RaceDifficulty>(I.Value));break;
    case ItemKind::Action:Car->HandleAction(I.Command);break;
    }
}
}
int FocusedItem(){return Focus;}
TArray<FItem> Items(const AVroomCar* Car,float W,float H)
{
    TArray<FItem> Result;if(!Car||!Car->Circuit)return Result;
    auto Add=[&](float X,float Y,float Width,float Height,FString Label,ItemKind Kind,Action Command,int Value=0)
    {FItem I;I.Bounds=FBox2D(FVector2D(X,Y),FVector2D(X+Width,Y+Height));I.Label=MoveTemp(Label);I.Kind=Kind;I.Command=Command;I.Value=Value;Result.Add(MoveTemp(I));};
    auto Button=[&](float X,float Y,float Width,FString Label,Action Command){Add(X,Y,Width,52,MoveTemp(Label),ItemKind::Action,Command);};
    if(Car->Session.Phase==SessionPhase::Finished)
    {
        const bool Standings=Car->Session.Mode==SessionMode::GrandPrix&&!Car->Session.QualifyingRun;
        const float PanelWidth=Standings?1060.f:700.f;
        const float X=W*.5f-PanelWidth*.5f,Y=H*.5f-(Standings?285.f:220.f);
        const float Footer=Standings?430.f:280.f;
        const bool Qualified=Car->Session.QualifyingRun&&Car->Session.QualificationComplete;
        const bool Continue=!Car->Session.QualifyingRun&&!Car->Session.ChampionshipComplete&&Car->Session.Stage<5
            &&(Car->Session.Mode==SessionMode::GrandPrix||Car->Session.Won);
        Button(X+30,Y+Footer,PanelWidth-60,Qualified?TEXT("PRENDRE LE DÉPART"):Continue?TEXT("ÉTAPE SUIVANTE"):TEXT("REJOUER"),Qualified?Action::Start:Continue?Action::NextStage:Action::Restart);
        const float Half=(PanelWidth-84)*.5f;
        Button(X+30,Y+Footer+68,Half,TEXT("RECOMMENCER"),Action::Restart);Button(X+54+Half,Y+Footer+68,Half,TEXT("MENU"),Action::Menu);
    }
    else if(Car->Circuit->Running&&Car->Circuit->Paused)
    {
        const float X=W*.5f-260,Y=H*.5f-210;
        Button(X+35,Y+105,450,TEXT("REPRENDRE"),Action::Pause);
        Button(X+35,Y+168,450,TEXT("RECOMMENCER"),Action::Restart);
        Button(X+35,Y+231,450,TEXT("COMMANDES ET SON"),Action::Controls);
        Button(X+35,Y+294,450,TEXT("MENU"),Action::Menu);
    }
    else if(!Car->Circuit->Running)
    {
        Add(66,H-171,333,65,TEXT("ROULER  >"),ItemKind::Action,Action::Start);
        Add(415,H-171,250,65,TEXT("GRAPHISMES"),ItemKind::Action,Action::Options);
        Add(682,H-171,270,65,TEXT("COMMANDES ET SON"),ItemKind::Action,Action::Controls);
        const TCHAR*Modes[]={TEXT("ARCADE"),TEXT("GRAND PRIX"),TEXT("CHRONO")};
        for(int I=0;I<3;++I)Add(66+I*217,H-425,202,58,Modes[I],ItemKind::Mode,Action::ModeNext,I);
        const TCHAR*Levels[]={TEXT("FACILE"),TEXT("NORMAL"),TEXT("EXPERT")};
        for(int I=0;I<3;++I)Add(66+I*217,H-300,202,43,Levels[I],ItemKind::Difficulty,Action::DifficultyNext,I);
        const float CardX=W-437,CardY=90;
        for(int I=0;I<Car->Circuit->Tracks.Num()&&I<6;++I)Add(CardX+12,CardY+301+I*45,345,40,Car->Circuit->Tracks[I].Name.ToUpper(),ItemKind::Track,Action::TrackNext,I);
    }
    return Result;
}
bool HandleInput(AVroomCar* Car,APlayerController* PC)
{
    if(!Car||!PC||!Car->Circuit||Settings::IsOpen()||Graphics::IsOpen())return false;
    if(Car->Circuit->Running&&!Car->Circuit->Paused&&Car->Session.Phase!=SessionPhase::Finished)return false;
    const int Screen=Car->Session.Phase==SessionPhase::Finished?2:Car->Circuit->Running?1:0;
    if(Screen!=LastScreen){Focus=0;LastScreen=Screen;}
    int Width=0,Height=0;PC->GetViewportSize(Width,Height);const float Scale=FMath::Max(.01f,FMath::Min(Width/1440.f,Height/900.f));
    const auto Entries=Items(Car,Width/Scale,Height/Scale);if(Entries.IsEmpty())return false;Focus=FMath::Clamp(Focus,0,Entries.Num()-1);
    auto Press=[&](FKey Key){return PC->WasInputKeyJustPressed(Key);};
    auto Activate=[&](const FItem&I){ActivateItem(Car,I);};
    if(Press(EKeys::Down)||Press(EKeys::Right)||Press(EKeys::Gamepad_DPad_Down)||Press(EKeys::Gamepad_DPad_Right))Focus=(Focus+1)%Entries.Num();
    if(Press(EKeys::Up)||Press(EKeys::Left)||Press(EKeys::Gamepad_DPad_Up)||Press(EKeys::Gamepad_DPad_Left))Focus=(Focus+Entries.Num()-1)%Entries.Num();
    if(Settings::WasPressed(PC,Action::Start))Activate(Entries[Focus]);
    else if(Settings::WasPressed(PC,Action::Menu)&&Screen!=0)Car->ReturnToMenu();
    else if(Settings::WasPressed(PC,Action::Pause)&&Screen==1)Car->HandleAction(Action::Pause);
    else if(Settings::WasPressed(PC,Action::Controls))Car->HandleAction(Action::Controls);
    else if(Settings::WasPressed(PC,Action::Options))Car->HandleAction(Action::Options);
    else if(Settings::WasPressed(PC,Action::Restart)&&Screen!=0)Car->RestartSession();
    else if(Screen==0)
    {
        const FKey Tracks[]={EKeys::F1,EKeys::F2,EKeys::F3,EKeys::F4,EKeys::F5,EKeys::F6};
        for(int I=0;I<6;++I)if(Press(Tracks[I]))Car->SelectSessionTrack(I);
        if(Settings::WasPressed(PC,Action::TrackNext))Car->SelectSessionTrack((Car->Circuit->TrackIndex+1)%6);
        if(Settings::WasPressed(PC,Action::TrackPrevious))Car->SelectSessionTrack((Car->Circuit->TrackIndex+5)%6);
        if(Settings::WasPressed(PC,Action::ModeNext))Car->SetSessionMode(static_cast<SessionMode>((static_cast<int>(Car->Session.Mode)+1)%3));
        if(Settings::WasPressed(PC,Action::DifficultyNext))Car->SetDifficulty(static_cast<RaceDifficulty>((static_cast<int>(Car->Session.Difficulty)+1)%3));
    }
    if(Press(EKeys::LeftMouseButton))
    {
        float MX=0,MY=0;if(PC->GetMousePosition(MX,MY))for(int I=0;I<Entries.Num();++I)
            if(Entries[I].Bounds.IsInside(FVector2D(MX/Scale,MY/Scale))){Focus=I;Activate(Entries[I]);break;}
    }
    return true;
}
bool RunSelfTest(AVroomCar* Car)
{
    if(!Car||!Car->Circuit)return false;
    const auto Original=Car->Session;const int Track=Car->Circuit->TrackIndex;
    bool Geometry=true;int Checked=0;
    for(const FVector2D Resolution:{FVector2D(1440,900),FVector2D(1600,900),FVector2D(2560,1664)})
    {
        const double Scale=FMath::Min(Resolution.X/1440.,Resolution.Y/900.);
        const float W=Resolution.X/Scale,H=Resolution.Y/Scale;
        for(int Mode=0;Mode<3;++Mode)
        for(int Screen=0;Screen<3;++Screen)
        {
            Car->Session.Mode=static_cast<SessionMode>(Mode);Car->Session.QualifyingRun=false;
            Car->Circuit->Running=Screen!=0;Car->Circuit->Paused=Screen==1;
            Car->Session.Phase=Screen==2?SessionPhase::Finished:Screen==1?SessionPhase::Racing:SessionPhase::Menu;
            const auto Entries=Items(Car,W,H);
            for(int I=0;I<Entries.Num();++I)
            {
                const auto&B=Entries[I].Bounds;Geometry&=B.Min.X>=0&&B.Min.Y>=0&&B.Max.X<=W&&B.Max.Y<=H;++Checked;
                for(int J=I+1;J<Entries.Num();++J)
                {
                    const auto&C=Entries[J].Bounds;
                    Geometry&=!(B.Min.X<C.Max.X&&B.Max.X>C.Min.X&&B.Min.Y<C.Max.Y&&B.Max.Y>C.Min.Y);
                }
            }
        }
    }
    Car->ReturnToMenu();FItem Choice;Choice.Kind=ItemKind::Mode;Choice.Value=static_cast<int>(SessionMode::TimeTrial);ActivateItem(Car,Choice);
    Choice.Kind=ItemKind::Track;Choice.Value=0;ActivateItem(Car,Choice);
    Choice.Kind=ItemKind::Action;Choice.Command=Action::Start;ActivateItem(Car,Choice);
    const bool Started=Car->Circuit->Running&&!Car->Circuit->Paused&&Car->Session.Mode==SessionMode::TimeTrial&&Car->Session.Phase==SessionPhase::Countdown&&Car->Session.Solo();
    Choice.Command=Action::Pause;ActivateItem(Car,Choice);const bool Paused=Car->Circuit->Paused;
    ActivateItem(Car,Choice);const bool Resumed=!Car->Circuit->Paused;
    Choice.Command=Action::Menu;ActivateItem(Car,Choice);const bool Menu=!Car->Circuit->Running&&Car->Session.Phase==SessionPhase::Menu;
    Choice.Command=Action::Options;ActivateItem(Car,Choice);
    const bool GraphicsOpened=Graphics::IsOpen()&&Car->Circuit->Paused;
    ActivateItem(Car,Choice);
    const bool GraphicsClosed=!Graphics::IsOpen()&&!Car->Circuit->Paused
        &&!Car->Circuit->Running&&Car->Session.Phase==SessionPhase::Menu;
    const bool Preferences=Settings::RunSelfTest();
    Car->SelectSessionTrack(Track);Car->Session=Original;Car->ReturnToMenu();Focus=0;LastScreen=-1;
    const bool Pass=Geometry&&Started&&Paused&&Resumed&&Menu&&GraphicsOpened&&GraphicsClosed&&Preferences;
    UE_LOG(LogTemp,Display,TEXT("VROOM: Menu self-test %s bounds=%d rectangles=%d start=%d pause=%d resume=%d menu=%d graphics-open=%d graphics-close=%d preferences=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Geometry,Checked,Started,Paused,Resumed,Menu,GraphicsOpened,GraphicsClosed,Preferences);
    return Pass;
}

}
