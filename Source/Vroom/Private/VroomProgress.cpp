#include "VroomProgress.h"
#include "VroomGhostLap.h"
#include "VroomGame.h"
#include "VroomPlayerSettings.h"
#include "VroomMenuLayout.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Async/Async.h"
#include "Misc/CoreDelegates.h"

namespace Vroom::Progress
{
namespace
{
TMap<int,FRecord> Records;
bool Initialized=false;
TSharedFuture<void> PendingWrite;
FDelegateHandle ExitHandle;
bool ShuttingDown=false;
void FlushWrites()
{
    if(PendingWrite.IsValid())PendingWrite.Wait();
    // A completed UE future still owns a pooled event. Release it while the
    // engine event pool is alive, not from a C++ global destructor after exit.
    PendingWrite=TSharedFuture<void>();
}
void Shutdown()
{
    ShuttingDown=true;FlushWrites();
    FCoreDelegates::OnPreExit.Remove(ExitHandle);ExitHandle.Reset();
}
constexpr int FormatVersion=1;
constexpr int MaxSamples=36000;
FGhostSample LineSample(const FGhostSample& Previous,const FGhostSample& Current,double Alpha)
{
    FGhostSample Origin;
    Origin.Location=FMath::Lerp(Previous.Location,Current.Location,Alpha);
    Origin.Rotation=FQuat::Slerp(Previous.Rotation,Current.Rotation,Alpha).GetNormalized();
    return Origin;
}
int Key(int Track,int Mode){return FMath::Clamp(Track,0,5)*3+FMath::Clamp(Mode,0,2);}
FString File()
{
    return FPaths::ProjectSavedDir() / (FParse::Param(FCommandLine::Get(),TEXT("VroomProgressTest"))
        ? TEXT("Validation/player-data-self-test/records-v1.json") : TEXT("Player/records-v1.json"));
}
TSharedPtr<FJsonObject> Read(const FString& Path)
{
    FString Text;TSharedPtr<FJsonObject> Root;
    if(IFileManager::Get().FileSize(*Path)>32*1024*1024||!FFileHelper::LoadFileToString(Text,*Path))return nullptr;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root))return nullptr;
    return Root;
}
bool Parse(const TSharedPtr<FJsonObject>& Root)
{
    double Version=0;const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;
    if(!Root||!Root->TryGetNumberField(TEXT("version"),Version)||Version!=FormatVersion||!Root->TryGetArrayField(TEXT("records"),Entries)||Entries->Num()>18)return false;
    TMap<int,FRecord> Loaded;
    for(const auto&Value:*Entries)
    {
        const TSharedPtr<FJsonObject>* Object=nullptr;
        if(!Value->TryGetObject(Object)||!Object||!Object->IsValid())return false;
        double Id=0,Best=0,Laps=0;
        if(!(*Object)->TryGetNumberField(TEXT("id"),Id)||!FMath::IsFinite(Id)||Id<0||Id>17||Id!=FMath::FloorToInt(Id))return false;
        if(!(*Object)->TryGetNumberField(TEXT("best"),Best)||!FMath::IsFinite(Best)||Best<5||Best>3600)return false;
        FRecord R;R.Best=Best;
        if((*Object)->TryGetNumberField(TEXT("laps"),Laps)&&FMath::IsFinite(Laps)&&Laps>=1&&Laps<=1000000)R.Laps=static_cast<int>(Laps);
        const TArray<TSharedPtr<FJsonValue>>* History=nullptr;
        if((*Object)->TryGetArrayField(TEXT("history"),History))
            for(const auto&Time:*History){double T=0;if(Time->TryGetNumber(T)&&FMath::IsFinite(T)&&T>=5&&T<=3600)R.History.Add(T);if(R.History.Num()>=10)break;}
        const TArray<TSharedPtr<FJsonValue>>* Ghost=nullptr;
        if((*Object)->TryGetArrayField(TEXT("ghost"),Ghost)&&Ghost->Num()<=MaxSamples)
        {
            bool Valid=true;double PreviousTime=-1,PreviousDistance=-1;
            for(const auto&Point:*Ghost)
            {
                const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!Point->TryGetArray(A)||A->Num()!=9){Valid=false;break;}
                double N[9];for(int I=0;I<9;++I)if(!(*A)[I]->TryGetNumber(N[I])||!FMath::IsFinite(N[I])||FMath::Abs(N[I])>1.e8){Valid=false;break;}
                if(!Valid||N[0]<0||N[0]>Best+.1||N[0]<=PreviousTime||N[1]<PreviousDistance||N[1]<0||N[1]>20000){Valid=false;break;}
                FGhostSample S;S.Time=N[0];S.Distance=N[1];S.Location=FVector(N[2],N[3],N[4]);S.Rotation=FQuat(N[5],N[6],N[7],N[8]);
                if(S.Rotation.SizeSquared()<.8||S.Rotation.SizeSquared()>1.2){Valid=false;break;}
                S.Rotation.Normalize();R.Ghost.Add(S);PreviousTime=S.Time;PreviousDistance=S.Distance;
            }
            if(!Valid||R.Ghost.Num()<2)R.Ghost.Reset();
        }
        Loaded.Add(static_cast<int>(Id),MoveTemp(R));
    }
    Records=MoveTemp(Loaded);return true;
}
void Persist(TMap<int,FRecord> Snapshot,const FString Path)
{
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("version"),FormatVersion);
    TArray<TSharedPtr<FJsonValue>> Entries;
    for(const auto&Pair:Snapshot)
    {
        auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("id"),Pair.Key);R->SetNumberField(TEXT("best"),Pair.Value.Best);R->SetNumberField(TEXT("laps"),Pair.Value.Laps);
        TArray<TSharedPtr<FJsonValue>> History;for(double T:Pair.Value.History)History.Add(MakeShared<FJsonValueNumber>(T));R->SetArrayField(TEXT("history"),History);
        TArray<TSharedPtr<FJsonValue>> Ghost;
        for(const auto&S:Pair.Value.Ghost)
        {
            TArray<TSharedPtr<FJsonValue>> A;for(double N:{S.Time,S.Distance,S.Location.X,S.Location.Y,S.Location.Z,S.Rotation.X,S.Rotation.Y,S.Rotation.Z,S.Rotation.W})A.Add(MakeShared<FJsonValueNumber>(N));
            Ghost.Add(MakeShared<FJsonValueArray>(A));
        }
        R->SetArrayField(TEXT("ghost"),Ghost);Entries.Add(MakeShared<FJsonValueObject>(R));
    }
    Root->SetArrayField(TEXT("records"),Entries);FString Text;FJsonSerializer::Serialize(Root,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    const FString Temp=Path+TEXT(".tmp");IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    if(FFileHelper::SaveStringToFile(Text,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        if(IFileManager::Get().FileExists(*Path))IFileManager::Get().Copy(*(Path+TEXT(".bak")),*Path,true,true);
        if(!IFileManager::Get().Move(*Path,*Temp,true))UE_LOG(LogTemp,Warning,TEXT("VROOM: Could not replace player records"));
    }
    else UE_LOG(LogTemp,Warning,TEXT("VROOM: Could not save player records"));
}
void Save()
{
    if(ShuttingDown)return;
    // Copy immutable samples on the game thread; JSON construction and filesystem
    // I/O run in one ordered chain so a lap crossing cannot stall rendering.
    TMap<int,FRecord> Snapshot=Records;
    const auto Previous=PendingWrite;const FString Path=File();
    PendingWrite=Async(EAsyncExecution::ThreadPool,[Previous,Snapshot=MoveTemp(Snapshot),Path]() mutable
    {
        if(Previous.IsValid())Previous.Wait();
        Persist(MoveTemp(Snapshot),Path);
    }).Share();
}
void Submit(int Track,int Mode,double Time,const TArray<FGhostSample>& Samples)
{
    Initialize();if(!FMath::IsFinite(Time)||Time<5||Time>3600)return;
    FRecord&R=Records.FindOrAdd(Key(Track,Mode));R.Laps++;R.History.Insert(Time,0);if(R.History.Num()>10)R.History.SetNum(10);
    if(R.Best<=0||Time<R.Best)
    {
        R.Best=Time;R.Ghost=Samples;
        UE_LOG(LogTemp,Display,TEXT("VROOM: Personal best track=%d mode=%d time=%.3f ghost=%d"),Track,Mode,Time,Samples.Num());
    }
    Save();
}
}
void Initialize()
{
    if(Initialized)return;Initialized=true;
    ExitHandle=FCoreDelegates::OnPreExit.AddStatic(&Shutdown);
    if(!Parse(Read(File())))Parse(Read(File()+TEXT(".bak")));
}
const FRecord& Record(int Track,int Mode){Initialize();static const FRecord Empty;const FRecord*R=Records.Find(Key(Track,Mode));return R?*R:Empty;}
TOptional<double> LapDelta(int Track,int Mode,double Distance,double Time)
{
    const auto&G=Record(Track,Mode).Ghost;if(G.Num()<2||Distance<G[0].Distance||Distance>G.Last().Distance)return {};
    int Lo=0,Hi=G.Num()-1;while(Hi-Lo>1){const int Mid=(Lo+Hi)/2;if(G[Mid].Distance<Distance)Lo=Mid;else Hi=Mid;}
    const double Span=G[Hi].Distance-G[Lo].Distance;
    if(Span<.001)return {};
    return Time-FMath::Lerp(G[Lo].Time,G[Hi].Time,FMath::Clamp((Distance-G[Lo].Distance)/Span,0.,1.));
}
bool RunSelfTest()
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("VroomProgressTest")))return false;
    Initialize();FlushWrites();Records.Reset();
    TArray<FGhostSample> Samples;
    for(int I=0;I<3;++I){FGhostSample S;S.Time=I*5.;S.Distance=I*100.;S.Location=FVector(I*100.,2.,3.);Samples.Add(S);}
    Submit(0,2,10.,Samples);Submit(0,2,11.,Samples);FlushWrites();
    Records.Reset();const bool Loaded=Parse(Read(File()));const FRecord&R=Record(0,2);
    const auto Delta=LapDelta(0,2,50.,3.);
    const bool RoundTrip=Loaded&&R.Best==10.&&R.Laps==2&&R.History.Num()==2&&R.History[0]==11.&&R.Ghost.Num()==3&&R.Ghost[1].Location.Equals(FVector(100,2,3));
    const bool Interpolation=Delta.IsSet()&&FMath::IsNearlyEqual(Delta.GetValue(),.5)&&!LapDelta(0,2,201.,3.).IsSet();
    auto Invalid=MakeShared<FJsonObject>();Invalid->SetNumberField(TEXT("version"),-1);
    const bool RejectInvalid=!Parse(Invalid);
    const bool Backup=Read(File()+TEXT(".bak")).IsValid();
    FGhostSample Before,After;
    Before.Distance=4359.8;After.Distance=4362.8;
    Before.Location=FVector(980,40,7);After.Location=FVector(1010,70,10);
    Before.Rotation=FQuat(FVector::UpVector,-.1);After.Rotation=FQuat(FVector::UpVector,.2);
    const auto Crossing=FindLineCrossing(Before.Distance,After.Distance,4360);
    const auto Origin=LineSample(Before,After,Crossing.Alpha);
    const bool Anchor=Crossing.Valid&&Origin.Time==0&&Origin.Distance==0
        &&Origin.Location.Equals(FVector(982,42,7.2),.00001)
        &&Origin.Rotation.Equals(FQuat(FVector::UpVector,-.08),.00001);
    const bool CleanGate=CanSubmitCleanLap(true,true,3,Origin.Time,Origin.Distance,10)
        &&!CanSubmitCleanLap(false,true,3,0,0,10)&&!CanSubmitCleanLap(true,false,3,0,0,10)
        &&!CanSubmitCleanLap(true,true,3,0,24,10);
    const bool Pass=RoundTrip&&Interpolation&&RejectInvalid&&Backup&&Anchor&&CleanGate;
    UE_LOG(LogTemp,Display,TEXT("VROOM: Progress self-test %s roundtrip=%d delta=%d malformed=%d backup=%d anchor=%d clean_gate=%d isolated=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),RoundTrip,Interpolation,RejectInvalid,Backup,Anchor,CleanGate,*File());
    return Pass;
}

}

AVroomProgressTracker::AVroomProgressTracker()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("GhostRoot")));
    SetActorEnableCollision(false);
}
void AVroomProgressTracker::BuildGhost(AVroomCar* Player)
{
    auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_F1Ghost.M_F1Ghost"));
    if(!Material)return;
    for(const auto&Original:Player->BodyParts)
    {
        if(!Original||!Original->GetStaticMesh())continue;
        auto*C=NewObject<UStaticMeshComponent>(this);C->SetupAttachment(GetRootComponent());C->SetMobility(EComponentMobility::Movable);
        C->SetStaticMesh(Original->GetStaticMesh());C->SetRelativeTransform(Original->GetRelativeTransform());
        for(int I=0;I<FMath::Max(1,Original->GetNumMaterials());++I)C->SetMaterial(I,Material);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCanEverAffectNavigation(false);C->SetCastShadow(false);C->SetVisibility(false);C->RegisterComponent();GhostMeshes.Add(C);
    }
}
void AVroomProgressTracker::Tick(float Dt)
{
    Super::Tick(Dt);
    if(!Car.IsValid())
    {
        Car=Cast<AVroomCar>(GetOwner());
        if(Car.IsValid())
        {
            BuildGhost(Car.Get());
            if(FParse::Param(FCommandLine::Get(),TEXT("VroomProgressTest")))Vroom::Progress::RunSelfTest();
            if(FParse::Param(FCommandLine::Get(),TEXT("VroomMenuTest")))Vroom::MenuLayout::RunSelfTest(Car.Get());
        }
    }
    if(!Car.IsValid()||!Car->Circuit)return;
    auto*Player=Car.Get();auto*Circuit=Player->Circuit.Get();
    // Verification runs must not overwrite a person's records with assisted laps.
    const bool Diagnostic=FParse::Param(FCommandLine::Get(),TEXT("VroomVerify"))||Player->AutoDemo;
    const int Track=Circuit->TrackIndex,Mode=static_cast<int>(Player->Session.Mode);
    const double Clock=Circuit->RaceClock,Length=Circuit->LengthM();
    const bool Changed=Track!=TrackIndex||Mode!=ModeIndex||Clock<LastClock-.01||!Circuit->Running;
    if(Changed)
    {
        Samples.Reset();Recording=false;ObservedLap=Player->Lap;SampleClock=-1;Clean=true;
        HasPreviousFrame=false;TrackIndex=Track;ModeIndex=Mode;
    }
    Vroom::Progress::FGhostSample Frame;
    Frame.Time=Clock;Frame.Distance=Player->Drive.State.DistanceM;
    Frame.Location=Player->GetActorLocation();Frame.Rotation=Player->GetActorQuat();
    const bool CrossedLap=Player->Lap==ObservedLap+1;
    const double Line=(Player->Lap-1)*Length;
    const auto Crossing=HasPreviousFrame&&!Changed
        ? Vroom::Progress::FindLineCrossing(PreviousFrame.Distance,Frame.Distance,Line)
        : Vroom::Progress::LineCrossing{};
    Vroom::Progress::FGhostSample Origin;
    if(Crossing.Valid)Origin=Vroom::Progress::LineSample(PreviousFrame,Frame,Crossing.Alpha);
    if(!Diagnostic&&Circuit->Running&&Player->Lap>ObservedLap)
    {
        if(Recording&&CrossedLap&&Crossing.Valid&&Vroom::Progress::CanSubmitCleanLap(
            Clean,Player->LastLapClean,Samples.Num(),Samples.IsEmpty()?-1:Samples[0].Time,
            Samples.IsEmpty()?-1:Samples[0].Distance,Player->LastLap))
        {
            auto End=Origin;End.Time=Player->LastLap;End.Distance=Length;
            if(End.Time>Samples.Last().Time)Samples.Add(End);
            Vroom::Progress::Submit(Track,Mode,Player->LastLap,Samples);
        }
        Samples.Reset();Recording=false;SampleClock=-1;Clean=true;
        // Seed the next full lap at the observed timing line, not at the first
        // 30-Hz sample, which can already be several metres beyond it.
        if(CrossedLap&&Crossing.Valid)
        {
            Samples.Add(Origin);Recording=true;SampleClock=0;
        }
    }
    ObservedLap=Player->Lap;
    const bool Active=Circuit->Running&&Clock>=0&&!Circuit->Paused&&Player->Session.Phase!=Vroom::SessionPhase::Finished;
    const double LapTime=FMath::Max(0.,Clock-Player->LapStart);
    const double LapDistance=FMath::Clamp(Frame.Distance-Line,0.,Length);
    if(Active&&!Diagnostic)
    {
        if(!Recording)
        {
            // Solo sessions can start exactly on the line. A standing race grid
            // starts ahead of it: that shortened first lap never gets an anchor.
            if(Crossing.Valid)
                Samples.Add(Origin);
            else if(Frame.Distance==Line&&LapTime==0)
            {
                Origin.Location=Frame.Location;Origin.Rotation=Frame.Rotation;Samples.Add(Origin);
            }
            else Clean=false;
            Recording=true;
            if(!Samples.IsEmpty())SampleClock=0;
        }
        // LapValidator owns off-road/contact/checkpoint validity, including the
        // allowed pit lane. A second raw lateral-width test rejected legal pits.
        Clean=Clean&&Player->LapClean;
        if(LapTime-SampleClock>=1./30.)
        {
            auto S=Frame;S.Time=LapTime;S.Distance=LapDistance;
            if(!Samples.IsEmpty()&&(S.Time<=Samples.Last().Time||S.Distance<Samples.Last().Distance-.01))Clean=false;
            if(Samples.Num()<36000)Samples.Add(S);else Clean=false;
            SampleClock=LapTime;
        }
    }
    PreviousFrame=Frame;HasPreviousFrame=true;LastClock=Clock;
    const auto&G=Vroom::Progress::Record(Track,Mode).Ghost;
    bool Visible=Active&&Player->Session.Mode==Vroom::SessionMode::TimeTrial&&Vroom::Settings::Get().GhostEnabled&&G.Num()>1&&LapTime>=G[0].Time&&LapTime<=G.Last().Time;
    if(Visible)
    {
        int Lo=0,Hi=G.Num()-1;while(Hi-Lo>1){int Mid=(Lo+Hi)/2;if(G[Mid].Time<LapTime)Lo=Mid;else Hi=Mid;}
        const double Alpha=FMath::Clamp((LapTime-G[Lo].Time)/FMath::Max(.001,G[Hi].Time-G[Lo].Time),0.,1.);
        const FVector P=FMath::Lerp(G[Lo].Location,G[Hi].Location,Alpha);
        // Suppress the translucent shell when it coincides with the player's cockpit.
        Visible=FVector::DistSquared(P,Player->GetActorLocation())>FMath::Square(220.);
        SetActorLocationAndRotation(P,FQuat::Slerp(G[Lo].Rotation,G[Hi].Rotation,Alpha).GetNormalized());
    }
    for(const auto&Mesh:GhostMeshes)if(Mesh)Mesh->SetVisibility(Visible);
}
