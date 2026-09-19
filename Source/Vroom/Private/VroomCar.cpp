#include "VroomGame.h"
#include "VroomGraphicsSettings.h"
#include "VroomOvertake.h"
#include "VroomPlayerSettings.h"
#include "VroomMenuLayout.h"
#include "VroomProgress.h"
#include "VroomFeedbackComponent.h"
#include "VroomProceduralMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "ProceduralMeshComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"

namespace
{
UStaticMesh* Shape(const TCHAR* Name)
{
    const FString Path = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name);
    return LoadObject<UStaticMesh>(nullptr, *Path);
}
UMaterialInterface* Mat(const TCHAR* Name)
{
    const FString Path = FString::Printf(TEXT("/Game/Materials/%s.%s"), Name, Name);
    return LoadObject<UMaterialInterface>(nullptr, *Path);
}
FTransform WishboneTransform(const FVector& Chassis, const FVector& Hub)
{
    const FVector Delta = Hub - Chassis;
    const float Length = Delta.Size();
    FTransform Pose;
    Pose.SetLocation((Chassis + Hub) * 0.5f);
    Pose.SetScale3D(FVector(0.028f, 0.028f, FMath::Max(0.02f, Length * 0.01f)));
    if (Length > 1.f)
        Pose.SetRotation(FQuat::FindBetweenNormals(FVector::UpVector, Delta / Length));
    return Pose;
}
int32 WishboneWheelSlot(const Vroom::Wishbone& Bar)
{
    return Vroom::CarWishboneWheelSlot(Bar);
}
}

namespace
{
FVector CockpitEye()
{
    static FVector Eye(-65,0,82);
    static bool Init = false;
    if (!Init)
    {
        FParse::Value(FCommandLine::Get(), TEXT("VroomEyeX="), Eye.X);
        FParse::Value(FCommandLine::Get(), TEXT("VroomEyeZ="), Eye.Z);
        Init = true;
    }
    return Eye;
}
float CockpitEyePitch()
{
    static float Pitch = 0;
    static bool Init = false;
    if (!Init) { FParse::Value(FCommandLine::Get(), TEXT("VroomEyePitch="),Pitch); Init=true; }
    return Pitch;
}
const FTransform& ChaseInspectionView(bool VerificationMode)
{
    static const FTransform DefaultView(FRotator(-6,0,0), FVector(-850,0,310));
    if (!VerificationMode) return DefaultView;
    static const FTransform InspectionView = []
    {
        FVector Eye(-850,0,310);
        FRotator Rotation(-6,0,0);
        bool Changed = FParse::Value(FCommandLine::Get(), TEXT("VroomChaseEyeX="), Eye.X);
        Changed |= FParse::Value(FCommandLine::Get(), TEXT("VroomChaseEyeY="), Eye.Y);
        Changed |= FParse::Value(FCommandLine::Get(), TEXT("VroomChaseEyeZ="), Eye.Z);
        Changed |= FParse::Value(FCommandLine::Get(), TEXT("VroomChasePitch="), Rotation.Pitch);
        Changed |= FParse::Value(FCommandLine::Get(), TEXT("VroomChaseYaw="), Rotation.Yaw);
        if (Changed)
            UE_LOG(LogTemp, Display, TEXT("VROOM: QA chase view eye=(%.1f,%.1f,%.1f) pitch=%.1f yaw=%.1f"),
                Eye.X, Eye.Y, Eye.Z, Rotation.Pitch, Rotation.Yaw);
        return FTransform(Rotation, Eye);
    }();
    return InspectionView;
}

float CockpitCameraSteeringYaw()
{
    static float Value = -0.40f;
    static bool Init = false;
    if (!Init)
    {
        FParse::Value(FCommandLine::Get(), TEXT("VroomCockpitCameraYaw="), Value);
        Value = FMath::Clamp(Value, -1.0f, 1.0f);
        Init = true;
    }
    return Value;
}

float CockpitWheelSteeringScale()
{
    static float Value = 0.45f;
    static bool Init = false;
    if (!Init)
    {
        FParse::Value(FCommandLine::Get(), TEXT("VroomWheelSteerScale="), Value);
        Value = FMath::Max(Value, 0.f);
        Init = true;
    }
    return Value;
}

float CockpitWheelSteeringDamping()
{
    static float Value = 90.f;
    static bool Init = false;
    if (!Init)
    {
        FParse::Value(FCommandLine::Get(), TEXT("VroomWheelSteerDamping="), Value);
        Value = FMath::Max(Value, 1.f);
        Init = true;
    }
    return Value;
}

float VroomSteeringInputDeadzone()
{
    static float Value = 0.06f;
    static bool Init = false;
    if (!Init)
    {
        FParse::Value(FCommandLine::Get(), TEXT("VroomSteeringDeadzone="), Value);
        Value = FMath::Clamp(Value, 0.f, 0.3f);
        Init = true;
    }
    return Value;
}

float VroomSteeringInputFilterHz()
{
    static float Value = 45.f;
    static bool Init = false;
    if (!Init)
    {
        FParse::Value(FCommandLine::Get(), TEXT("VroomSteeringInputHz="), Value);
        Value = FMath::Max(Value, 12.f);
        Init = true;
    }
    return Value;
}
}

AVroomCar::AVroomCar()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CarRoot"));
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("CockpitCamera"));
    Camera->SetupAttachment(RootComponent);
    Camera->SetRelativeLocation(CockpitEye());
    Camera->SetRelativeRotation(FRotator(CockpitEyePitch(),0,0));
    Camera->SetFieldOfView(68);
    // Preserve the vertical field used by the original camera setup when the
    // 16:10 Mac viewport changes to a 16:9 4K viewport.
    Camera->SetAspectRatio(16.f / 9.f);
    Camera->bOverrideAspectRatioAxisConstraint = true;
    Camera->SetAspectRatioAxisConstraint(AspectRatio_MaintainYFOV);
    EngineAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineAudio"));
    EngineAudio->SetupAttachment(RootComponent);
    EngineAudio->bAutoActivate = false;
    AutoPossessPlayer = EAutoReceiveInput::Player0;
    CreateDefaultSubobject<UVroomFeedbackComponent>(TEXT("RaceFeedback"));
}

void AVroomCar::BeginPlay()
{
    Super::BeginPlay();
    Vroom::Settings::Initialize();
    Drive.Automatic = Vroom::Settings::Get().Automatic;
    ChaseCamera = Vroom::Settings::Get().ChaseCamera;
    TourDemo = FParse::Param(FCommandLine::Get(), TEXT("VroomTourDemo"));
    AutoDemo = FParse::Param(FCommandLine::Get(), TEXT("VroomAutoDemo"));
    AutoDemo |= TourDemo;
    AutoDemo |= FParse::Param(FCommandLine::Get(), TEXT("VroomDriftTest"));
    FParse::Value(FCommandLine::Get(), TEXT("VroomCollisionTest="), TestImpact);
    PitTest = FParse::Param(FCommandLine::Get(), TEXT("VroomPitTest"));
    TrafficTest = FParse::Param(FCommandLine::Get(), TEXT("VroomTrafficTest"));
    FParse::Value(FCommandLine::Get(), TEXT("VroomOffroadTest="), OffroadTestSide);
    OffroadTestSide = FMath::Clamp(OffroadTestSide, -1, 1);
    StartTest = FParse::Param(FCommandLine::Get(), TEXT("VroomStartTest"));
    FParse::Value(FCommandLine::Get(), TEXT("VroomStartScenario="), StartTestScenario);
    AutoDemo |= !TestImpact.IsEmpty() || PitTest || TrafficTest || OffroadTestSide != 0 || StartTest;
    TickDebug = FParse::Param(FCommandLine::Get(), TEXT("VroomTickDebug"));
    MotionTrace = FParse::Param(FCommandLine::Get(), TEXT("VroomMotionTrace"));
    VerificationMode = FParse::Param(FCommandLine::Get(), TEXT("VroomVerify"));
    ChaseCamera = FParse::Param(FCommandLine::Get(), TEXT("VroomChaseCamera")) || (!VerificationMode && ChaseCamera);
    FParse::Value(FCommandLine::Get(), TEXT("VroomCapture="), CapturePath);
    FParse::Value(FCommandLine::Get(), TEXT("VroomCaptureAfter="), CaptureAfter);
    FParse::Value(FCommandLine::Get(), TEXT("VroomExitAfter="), ExitAfter);
    float PreviewDistance = 0, PreviewSpeed = 0;
    PlaceOnStartingGrid();
    if (AutoDemo && TestImpact.IsEmpty())
    {
        // Grid cars sit at 40–120 m. V0.11 auto-demo spawned in that pack
        // and wrecked in 2 s, hiding the HUD. Default: past the field.
        Drive.State.DistanceM = 180;
        Drive.State.LateralM = 0;
        Drive.State.SpeedMps = 28;
        Drive.State.Gear = 3;
        if (FParse::Value(FCommandLine::Get(), TEXT("VroomStartDistance="), PreviewDistance)
            && FMath::IsFinite(PreviewDistance))
            Drive.State.DistanceM = FMath::Clamp(PreviewDistance, 0.f, 1000000.f);
        if (FParse::Value(FCommandLine::Get(), TEXT("VroomStartSpeed="), PreviewSpeed)
            && FMath::IsFinite(PreviewSpeed))
            Drive.State.SpeedMps = FMath::Clamp(PreviewSpeed, 0.f, 90.f);
        FParse::Value(FCommandLine::Get(), TEXT("VroomPreviewSteering="), PreviewSteering);
        if (Drive.State.SpeedMps > 40) Drive.State.Gear = 4;
        PreviousDrive = Drive.State;
        PreviousVisualHeading = Drive.VisualHeading();
    }
    auto Part = [&](const TCHAR* Mesh, const TCHAR* Material, FVector P, bool Tyre = false, bool CockpitHidden = false)
    {
        auto C = NewObject<UStaticMeshComponent>(this);
        C->SetupAttachment(RootComponent);
        C->SetMobility(EComponentMobility::Movable);
        C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Meshes/%s.%s"),Mesh,Mesh)));
        C->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/Materials/%s.%s"),Material,Material)));
        C->SetRelativeLocation(P);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCanEverAffectNavigation(false);
        if (CockpitHidden)
        {
            C->ComponentTags.Add(FName(TEXT("VroomCockpitHidden")));
            C->SetVisibility(ChaseCamera);
        }
#if PLATFORM_MAC
        C->SetAffectDistanceFieldLighting(false);
        C->SetAffectDynamicIndirectLighting(false);
#endif
        C->RegisterComponent();
        BodyBaseTransforms.Add(C->GetRelativeTransform());
        if (Tyre) TyrePartIndices.Add(BodyParts.Num());
        BodyParts.Add(C);
    };
    // Shared contoured F1 surfaces are baked in centimetres. The front wing
    // deliberately remains index 2 for the existing broken-nose animation.
    Part(TEXT("SM_F1PlayerPaint"),TEXT("M_F1Paint"),FVector::ZeroVector);
    Part(TEXT("SM_F1PlayerTrim"),TEXT("M_F1Pearl"),FVector::ZeroVector);
    Part(TEXT("SM_F1FrontWing"),TEXT("M_F1Paint"),FVector(208,0,24));
    Part(TEXT("SM_F1PlayerCarbon"),TEXT("M_F1Carbon"),FVector::ZeroVector);
    for (float Side : {-1.f,1.f})
    {
        // Each wheel includes its alloy rim; steering and crash detachment
        // therefore move the whole assembly without extra attached parts.
        Part(TEXT("SM_F1WheelFront"),TEXT("M_F1Rubber"),FVector(128,Side*91,35),true);
        Part(TEXT("SM_F1WheelRear"),TEXT("M_F1Rubber"),FVector(-115,Side*96,38),true);
    }
    // Add the chase-only driver after the wheels, preserving all existing body
    // and tyre indices (including front wing index 2 used by crash deformation).
    Part(TEXT("SM_F1PlayerDriverPaint"),TEXT("M_F1Paint"),FVector::ZeroVector,false,true);
    Part(TEXT("SM_F1PlayerDriverTrim"),TEXT("M_F1Pearl"),FVector::ZeroVector,false,true);
    Part(TEXT("SM_F1PlayerDriverCarbon"),TEXT("M_F1Carbon"),FVector::ZeroVector,false,true);
    int32 WishboneCount = 0;
    const Vroom::Wishbone* Wishbones = Vroom::CarWishbones(WishboneCount);
    for (int32 I = 0; I < WishboneCount; ++I)
    {
        const FVector Chassis(Wishbones[I].ChassisX, Wishbones[I].ChassisY, Wishbones[I].ChassisZ);
        const FVector Hub(Wishbones[I].HubX, Wishbones[I].HubY, Wishbones[I].HubZ);
        auto C = NewObject<UStaticMeshComponent>(this);
        C->SetupAttachment(RootComponent);
        C->SetMobility(EComponentMobility::Movable);
        C->SetStaticMesh(Shape(TEXT("Cylinder")));
        C->SetMaterial(0, Mat(TEXT("M_F1Carbon")));
        C->SetRelativeTransform(WishboneTransform(Chassis, Hub));
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCanEverAffectNavigation(false);
#if PLATFORM_MAC
        C->SetAffectDistanceFieldLighting(false);
        C->SetAffectDynamicIndirectLighting(false);
#endif
        C->RegisterComponent();
        SuspensionBars.Add(C);
        SuspensionChassis.Add(Chassis);
        SuspensionHubs.Add(Hub);
        SuspensionSteers.Add(Wishbones[I].Steers ? 1 : 0);
        SuspensionWheelIndex.Add(WishboneWheelSlot(Wishbones[I]));
    }
    BuildCockpit();
    BuildMirrors();
    BuildCrashDebris();
    LoadAmigaSound();
    EngineWave = NewObject<USoundWaveProcedural>(this);
    EngineWave->SetSampleRate(48000);
    EngineWave->NumChannels = 1;
    EngineWave->Duration = INDEFINITELY_LOOPING_DURATION;
    EngineWave->SoundGroup = SOUNDGROUP_Voice;
    EngineAudio->SetSound(EngineWave);
    EngineAudio->SetVolumeMultiplier(.22f);
    UpdateSound();
    EngineAudio->Play();
    if (auto* PC = Cast<APlayerController>(GetController()))
    {
        PC->bShowMouseCursor = false;
        PC->SetInputMode(FInputModeGameOnly());
    }
    if (!Circuit)
    {
        TActorIterator<AVroomCircuit> It(GetWorld());
        if (It) Circuit = *It;
    }
    PreviousDrive = Drive.State;
    PreviousVisualHeading = Drive.VisualHeading();
    if (Circuit && Circuit->CurrentTrack()) UpdatePresentation(1);
    FActorSpawnParameters ProgressSpawn;
    ProgressSpawn.Owner = this;
    GetWorld()->SpawnActor<AVroomProgressTracker>(ProgressSpawn);
    UE_LOG(LogTemp, Display, TEXT("VROOM: Player ready, demo=%d"), AutoDemo);
}

void AVroomCar::PlaceOnStartingGrid()
{
    // Fond de grille, colonne droite : un vrai slot au lieu du milieu de piste.
    // Les 12 adversaires occupent les rangs S=40..120, voies +-2,2.
    Drive.State.DistanceM = 24.0;
    Drive.State.LateralM = 2.2;
    Drive.State.HeadingRad = 0.0;
}

void AVroomCar::ResetCar()
{
    if (auto* Feedback = FindComponentByClass<UVroomFeedbackComponent>()) Feedback->ResetForRace();
    LandingFeedbackTime = LandingQuality = 0;
    Drive.Reset();
    PlayerWheelVisual.Reset();
    Contacts.Reset();
    Pit = Vroom::PitStopModel{};
    PlaceOnStartingGrid();
    ResetCrashDebris();
    MirrorClock = 1.f / 15.f;
    PreviousDrive = Drive.State;
    PreviousVisualHeading = Drive.VisualHeading();
    PreviousContact = Contacts.State;
    PreviousHopHeight = Drive.HopHeightM;
    FrameTiming = {};
    InputSteering = CockpitSteering = VisualSteering = 0;
    PendingShiftUp = PendingShiftDown = false;
    Lap = 1; LapStart = 0; LastLap = 0; LapClean = LastLapClean = true;
    LastLapInvalidated = false; LastLapInvalidReason.Empty(); LapInvalidReason.Empty();
    ValidLaps = 0; LapState.Reset();
    CarsPassed = 0; Score = 0; ScoreFrac = 0;
    OvertakeSides.Empty();
    MouseTarget = 0;
    if (Circuit) Circuit->RaceClock = 0;
}

void AVroomCar::SetSessionMode(Vroom::SessionMode Mode)
{
    const bool Changed = Session.Mode != Mode;
    Session.Mode = Mode; Session.QualificationComplete = false;
    Session.ResetChampionship();
    ReturnToMenu();
    if (Changed && Mode != Vroom::SessionMode::TimeTrial) SelectSessionTrack(Vroom::SessionModel::ChampionshipOrder[0]);
}

void AVroomCar::SetDifficulty(Vroom::RaceDifficulty Difficulty)
{
    Session.Difficulty = Difficulty;
    if (Circuit) Circuit->Difficulty = Difficulty;
}

void AVroomCar::SelectSessionTrack(int32 Index)
{
    if (!Circuit) return;
    ReturnToMenu();
    Circuit->SelectTrack(FMath::Clamp(Index, 0, 5));
    Session.SelectTrack(Circuit->TrackIndex); ResetCar(); BestLap = 0;
    BeginCircuitPreparation();
}

void AVroomCar::ReturnToMenu()
{
    if (!Circuit) return;
    Circuit->Running = false; Circuit->Paused = false;
    Session.Phase = Vroom::SessionPhase::Menu;
    Pit.Servicing = false;
    if (auto* PC = Cast<APlayerController>(GetController())) PC->bShowMouseCursor = true;
}

void AVroomCar::StartSession()
{
    if (!Circuit) return;
    if (Session.Phase == Vroom::SessionPhase::Menu) Session.ResetChampionship();
    const bool AfterQualifying = Session.Mode == Vroom::SessionMode::GrandPrix
        && Session.QualificationComplete && Session.QualifyingRun && Session.Phase == Vroom::SessionPhase::Finished;
    ResetCar(); Circuit->SelectTrack(Session.TrackIndex);
    Session.Begin(Circuit->LengthM(), AfterQualifying);
    const bool RaceResources = !Session.Solo();
    const Vroom::ResourceBudget Budget = Vroom::MakeResourceBudget(
        RaceResources, Session.Difficulty, Circuit->LengthM(), Session.LapTarget);
    Drive.FuelBurnPerThrottleSecond = Budget.FuelPerThrottleSecond;
    Drive.TyreWearPerMetre = Budget.TyreWearPerMetre;
    for (auto& Agent : Circuit->Traffic)
    {
        Agent.Personality = Vroom::PersonalityByLivery(Agent.LiveryIndex);
        Agent.Fuel = 1.0;
        Agent.TyreHealth = 2048.0;
        Agent.FuelBurn = Budget.FuelPerThrottleSecond;
        Agent.TyreWearPerMetre = Budget.TyreWearPerMetre;
        Agent.Pitting = false;
        Agent.PitCommitted = Agent.PitExiting = false;
        Agent.PitTimer = 0.0;
    }
    Circuit->StartLightStage = -1;
    Circuit->StartSignalPlayed = -2;
    Circuit->ConfigureTraffic(!Session.Solo(), Session.Mode == Vroom::SessionMode::Arcade, Session.Difficulty);
    Circuit->RaceFinishDistance = Session.Mode == Vroom::SessionMode::GrandPrix && !Session.QualifyingRun
        ? Circuit->LengthM() * Session.LapTarget : 0;
    Circuit->ReactiveTraffic = true;
    if (Session.Solo()) { Drive.State.DistanceM = 0; Drive.State.LateralM = 0; }
    else if (Session.Mode == Vroom::SessionMode::GrandPrix)
    {
        // Fixed physical grid slots, filled in qualifying order. S=0 remains
        // the lap timing line; race distance is the same line for every car.
        const int32 Slot = FMath::Clamp(Session.QualificationPosition - 1, 0, 12);
        auto GridDistance = [](int32 I) { return 120.0 - (I / 2) * 16.0; };
        Drive.State.DistanceM = GridDistance(Slot); Drive.State.LateralM = Slot % 2 ? 2.2 : -2.2;
        int32 Rival = 0;
        for (int32 I = 0; I < 13; ++I)
        {
            if (I == Slot) continue;
            Circuit->OpponentS[Rival] = Circuit->PreviousOpponentS[Rival] = GridDistance(I);
            Circuit->OpponentLanes[Rival] = Circuit->PreviousOpponentLanes[Rival] = I % 2 ? 2.2 : -2.2;
            Circuit->Traffic[Rival].Distance = GridDistance(I);
            Circuit->Traffic[Rival].Lateral = Circuit->Traffic[Rival].TargetLane = Circuit->OpponentLanes[Rival];
            ++Rival;
        }
    }
    PreviousDrive = Drive.State; PreviousVisualHeading = Drive.VisualHeading();
    Circuit->Running = true; Circuit->Paused = false; Circuit->RaceClock = -3;
    BeginCircuitPreparation();
    Circuit->RenderOpponents(1);
    if (auto* PC = Cast<APlayerController>(GetController()))
    {
        PC->bShowMouseCursor = false; PC->SetInputMode(FInputModeGameOnly());
    }
    UE_LOG(LogTemp, Display, TEXT("VROOM: Session mode=%d phase=%d track=%d difficulty=%d qualify=%d target=%d"),
        static_cast<int32>(Session.Mode), static_cast<int32>(Session.Phase), Session.TrackIndex,
        static_cast<int32>(Session.Difficulty), Session.QualifyingRun, Session.TargetPasses);
}

void AVroomCar::BeginCircuitPreparation()
{
    PreparingCircuit = true;
    PreparationReadyFrames = 0;
    // Quality changes may alter the passes used by an unchanged material.
    // Recollect against current scene settings, including ordinary static meshes.
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        TInlineComponentArray<UPrimitiveComponent*> Primitives;
        It->GetComponents(Primitives);
        for (UPrimitiveComponent* Primitive : Primitives) Primitive->PrecachePSOs();
    }
}

bool AVroomCar::UpdateCircuitPreparation()
{
    if (!PreparingCircuit || !Circuit) return false;
    if (!PreparationHoldsPause)
    {
        PreparationHoldsPause = true;
        PreparationPauseBefore = Circuit->Paused;
        PreparationStartTime = FPlatformTime::Seconds();
        UE_LOG(LogTemp, Display, TEXT("VROOM: Circuit preparation started track=%d"), Circuit->TrackIndex);
    }
    Circuit->Paused = true;
    PendingRenderJobs = Vroom::Rendering::PendingPrecache(GetWorld());
    PreparationReadyFrames = PendingRenderJobs ? 0 : PreparationReadyFrames + 1;
    // The native completion callback schedules render-state recreation. Let it
    // reach the render thread before releasing the grid or accepting menu input.
    const double Alpha = Circuit->Running ? FrameTiming.Alpha() : 1;
    UpdatePresentation(Alpha);
    Circuit->RenderOpponents(Alpha);
    UpdateMirrors(0);
    EngineAudio->SetVolumeMultiplier(0);
    if (PreparationReadyFrames < 3)
    {
        // La préparation n'est qu'un préchauffage anti-saccades : elle ne doit
        // jamais bloquer le menu ou la course si le backend de rendu cale.
        if (FPlatformTime::Seconds() - PreparationStartTime > 8.0)
        {
            UE_LOG(LogTemp, Warning, TEXT("VROOM: Circuit preparation timed out track=%d pending=%u, continuing without warmup"),
                Circuit->TrackIndex, PendingRenderJobs);
            PreparingCircuit = false;
            PreparationHoldsPause = false;
            Circuit->Paused = PreparationPauseBefore;
            return false;
        }
        return true;
    }
    PreparingCircuit = false;
    PreparationHoldsPause = false;
    Circuit->Paused = PreparationPauseBefore;
    UE_LOG(LogTemp, Display, TEXT("VROOM: Circuit preparation ready track=%d elapsed=%.3f pending=0"),
        Circuit->TrackIndex, FPlatformTime::Seconds() - PreparationStartTime);
    return false;
}

void AVroomCar::RestartSession()
{
    if (Session.Phase == Vroom::SessionPhase::Menu) { StartSession(); return; }
    // Restarting a GP race keeps its earned grid; restarting qualifying retries
    // the qualifying lap. Neither operation awards points twice.
    if (Session.Mode == Vroom::SessionMode::GrandPrix && !Session.QualifyingRun && Session.QualificationComplete)
    {
        if (Session.Phase == Vroom::SessionPhase::Finished) Session.UndoLastRace();
        Session.QualifyingRun = true; Session.Phase = Vroom::SessionPhase::Finished;
    }
    else Session.QualificationComplete = false;
    StartSession();
}

void AVroomCar::AdvanceStage()
{
    if (Session.QualifyingRun && Session.QualificationComplete) { StartSession(); return; }
    if (Session.AdvanceStage()) StartSession();
}

void AVroomCar::HandleAction(Vroom::Action What)
{
    using Vroom::Action;
    switch (What)
    {
    case Action::Start: StartSession(); break;
    case Action::NextStage: AdvanceStage(); break;
    case Action::Restart: RestartSession(); break;
    case Action::Menu: ReturnToMenu(); break;
    case Action::Pause: if (Circuit && Circuit->Running && Session.Phase != Vroom::SessionPhase::Finished) Circuit->Paused = !Circuit->Paused; break;
    case Action::View: ChaseCamera = !ChaseCamera; Vroom::Settings::Mutable().ChaseCamera = ChaseCamera; Vroom::Settings::Save(); break;
    case Action::Gearbox: Drive.Automatic = !Drive.Automatic; Vroom::Settings::Mutable().Automatic = Drive.Automatic; Vroom::Settings::Save(); break;
    case Action::MouseSteer: MouseSteer = !MouseSteer; MouseTarget = 0; break;
    case Action::TrackNext: if (Circuit) SelectSessionTrack((Circuit->TrackIndex + 1) % 6); break;
    case Action::TrackPrevious: if (Circuit) SelectSessionTrack((Circuit->TrackIndex + 5) % 6); break;
    case Action::ModeNext: SetSessionMode(static_cast<Vroom::SessionMode>((static_cast<int32>(Session.Mode) + 1) % 3)); break;
    case Action::DifficultyNext: SetDifficulty(static_cast<Vroom::RaceDifficulty>((static_cast<int32>(Session.Difficulty) + 1) % 3)); break;
    case Action::Tyres: if (Circuit && Circuit->Running && !Circuit->Paused && !Session.Solo()) Pit.Request(true, false); break;
    case Action::Refuel: if (Circuit && Circuit->Running && !Circuit->Paused && !Session.Solo()) Pit.Request(false, true); break;
    case Action::Options:
    {
        const bool WasOpen = Vroom::Graphics::IsOpen();
        if (!WasOpen) GraphicsPauseBefore = Circuit && Circuit->Paused;
        Vroom::Graphics::Toggle();
        if (Circuit) Circuit->Paused = Vroom::Graphics::IsOpen() ? true : GraphicsPauseBefore;
        if (WasOpen && !Vroom::Graphics::IsOpen()) BeginCircuitPreparation();
        if (auto* PC = Cast<APlayerController>(GetController()))
        {
            PC->bShowMouseCursor = Vroom::Graphics::IsOpen() || !Circuit || !Circuit->Running
                || Circuit->Paused || Session.Phase == Vroom::SessionPhase::Finished;
            if (PC->bShowMouseCursor)
            {
                FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false);
                Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); PC->SetInputMode(Mode);
            }
            else PC->SetInputMode(FInputModeGameOnly());
        }
        break;
    }
    case Action::Controls:
        if (!Vroom::Settings::IsOpen())
        {
            ControlsPauseBefore = Circuit && Circuit->Paused; Vroom::Settings::Toggle();
            if (Circuit) Circuit->Paused = true;
            if (auto* PC = Cast<APlayerController>(GetController()))
            {
                PC->bShowMouseCursor = true;
                FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
                PC->SetInputMode(Mode);
            }
        }
        break;
    default: break;
    }
}

double AVroomCar::QualificationBenchmark() const
{
    if (!Circuit) return 120;
    double Time = 0;
    const double Len = Circuit->LengthM();
    for (double S = 0; S < Len; S += 25)
    {
        const double K = FMath::Max(FMath::Abs(Circuit->Curvature(S + 25)), FMath::Abs(Circuit->Curvature(S + 90)));
        const double Speed = FMath::Clamp(FMath::Sqrt(16.0 / FMath::Max(K, .001)), 14.0, 72.0) * Vroom::TrafficPace(Session.Difficulty);
        Time += FMath::Min(25.0, Len - S) / Speed;
    }
    return Time + 5.0;
}

void AVroomCar::UpdateSessionRules()
{
    if ((AutoDemo && !StartTest) || !Session.Active()) return;
    const double ClassifiedDistance = ValidLaps * Circuit->LengthM()
        + Vroom::LapValidator::Along(Drive.State.DistanceM, Circuit->LengthM());
    int32 Position = 1;
    if (Circuit->OpponentsVisible)
        for (int32 I = 0; I < Circuit->OpponentS.Num(); ++I)
            if ((Circuit->OpponentFinishTimes.IsValidIndex(I) && Circuit->OpponentFinishTimes[I] > 0)
                || Circuit->OpponentS[I] > ClassifiedDistance) ++Position;
    const auto Before = Session.Phase;
    double ResultClock = Circuit->RaceClock;
    if (Session.Mode == Vroom::SessionMode::GrandPrix && !Session.QualifyingRun && Circuit->RaceFinishDistance > 0
        && ValidLaps >= Session.LapTarget && LapState.PhysicalLaps > 1)
    {
        const double Fraction = FMath::Clamp((std::floor(Drive.State.DistanceM / Circuit->LengthM()) * Circuit->LengthM() - PreviousDrive.DistanceM)
            / FMath::Max(.0001, Drive.State.DistanceM - PreviousDrive.DistanceM), 0.0, 1.0);
        ResultClock -= Vroom::FrameTiming::StepSeconds * (1.0 - Fraction);
    }
    Session.Tick(ResultClock, ValidLaps, CarsPassed, Position, Drive.Fuel, Drive.State.SpeedMps);
    if (Session.Phase == Vroom::SessionPhase::Finished && Before != Session.Phase)
    {
        if (Session.Mode == Vroom::SessionMode::GrandPrix && !Session.QualifyingRun)
        {
            std::array<int, 13> Order{};
            std::array<double, 13> FinishTimes{}, Distances{};
            for (int32 I = 0; I < 13; ++I)
            {
                Order[I] = I;
                FinishTimes[I] = I == Vroom::SessionModel::PlayerDriver ? (Session.Retired ? 0 : Session.FinishedTime)
                    : Circuit->OpponentFinishTimes[I];
                Distances[I] = I == Vroom::SessionModel::PlayerDriver ? ClassifiedDistance : Circuit->OpponentS[I];
            }
            std::sort(Order.begin(), Order.end(), [&](int A, int B)
            {
                if (A == B) return false;
                if (Session.Retired && (A == Vroom::SessionModel::PlayerDriver || B == Vroom::SessionModel::PlayerDriver))
                    return B == Vroom::SessionModel::PlayerDriver;
                const bool AFinished = FinishTimes[A] > 0, BFinished = FinishTimes[B] > 0;
                if (AFinished != BFinished) return AFinished;
                if (AFinished && FinishTimes[A] != FinishTimes[B]) return FinishTimes[A] < FinishTimes[B];
                if (!AFinished && Distances[A] != Distances[B]) return Distances[A] > Distances[B];
                return A < B;
            });
            Session.ClassifyRace(Order);
        }
        Circuit->Paused = true;
        if (auto* PC = Cast<APlayerController>(GetController())) PC->bShowMouseCursor = true;
        UE_LOG(LogTemp, Display, TEXT("VROOM: Finished mode=%d position=%d time=%.3f success=%d points=%d"),
            static_cast<int32>(Session.Mode), Session.Position, Session.FinishedTime, Session.Won, Session.ChampionshipPoints);
    }
}

bool AVroomCar::HandleGraphicsInput(APlayerController* PC)
{
    using namespace Vroom::Graphics;
    if (VerificationMode) return IsOpen();
    const bool WasOpen = IsOpen();
    bool ClickedOptions = false;
    if (!WasOpen && !Circuit->Running && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        float X = 0, Y = 0;
        int32 Width = 0, Height = 0;
        PC->GetViewportSize(Width, Height);
        const float Scale = FMath::Max(.1f, FMath::Min(Width / 1440.f, Height / 900.f));
        if (PC->GetMousePosition(X, Y))
            ClickedOptions = X / Scale >= 415 && X / Scale <= 665
                && Y / Scale >= Height / Scale - 171 && Y / Scale <= Height / Scale - 106;
    }
    if (ClickedOptions || Vroom::Settings::WasPressed(PC, Vroom::Action::Options) || PC->WasInputKeyJustPressed(EKeys::F7))
    {
        HandleAction(Vroom::Action::Options);
    }
    if (IsOpen()) Circuit->Paused = true;
    if (IsOpen() && WasOpen)
    {
        if (PC->WasInputKeyJustPressed(EKeys::Escape) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right)) Cancel();
        else if (PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)) Apply();
        else
        {
            if (PC->WasInputKeyJustPressed(EKeys::Up) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Up)) SelectRow((SelectedRow() + RowCount() - 1) % RowCount());
            if (PC->WasInputKeyJustPressed(EKeys::Down) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down)) SelectRow((SelectedRow() + 1) % RowCount());
            if (PC->WasInputKeyJustPressed(EKeys::Left) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Left)) Adjust(-1);
            if (PC->WasInputKeyJustPressed(EKeys::Right) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Right)) Adjust(1);
            float MouseX = 0, MouseY = 0;
            int32 Width = 0, Height = 0;
            PC->GetViewportSize(Width, Height);
            const float Scale = FMath::Max(.1f, FMath::Min(Width / 1440.f, Height / 900.f));
            if (PC->GetMousePosition(MouseX, MouseY) && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
            {
                const float X = MouseX / Scale - (Width / Scale / 2 - 430);
                const float Y = MouseY / Scale - (Height / Scale / 2 - 400);
                if (Y >= 733 && Y <= 777)
                {
                    if (X >= 598 && X <= 836) Apply();
                    else if (X >= 24 && X <= 196) Cancel();
                }
                else if (X >= 24 && X <= 836 && Y >= 92 && Y < 92 + RowCount() * 40)
                {
                    SelectRow(FMath::FloorToInt((Y - 92) / 40));
                    Adjust(X < 620 ? -1 : 1);
                }
            }
        }
    }
    const bool Open = IsOpen();
    if (WasOpen && !Open)
    {
        Circuit->Paused = GraphicsPauseBefore;
        if (!PreparingCircuit) BeginCircuitPreparation();
    }
    PC->bShowMouseCursor = Open || !Circuit->Running || Circuit->Paused || Session.Phase == Vroom::SessionPhase::Finished;
    if (WasOpen != Open)
    {
        if (PC->bShowMouseCursor)
        {
            FInputModeGameAndUI Mode;
            Mode.SetHideCursorDuringCapture(false);
            Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            PC->SetInputMode(Mode);
        }
        else PC->SetInputMode(FInputModeGameOnly());
        UE_LOG(LogTemp, Display, TEXT("VROOM: Graphics menu %s"), Open ? TEXT("open") : TEXT("closed"));
    }
    return WasOpen || Open;
}

void AVroomCar::Tick(float Dt)
{
    Super::Tick(Dt);
    if (!Circuit)
    {
        TActorIterator<AVroomCircuit> It(GetWorld());
        if (It) Circuit = *It;
    }
    if (!Circuit || !Circuit->CurrentTrack()) return;
    if (!TrackArgumentApplied)
    {
        TrackArgumentApplied = true;
        int32 RequestedTrack = Circuit->TrackIndex;
        const bool HasTrackArgument = FParse::Value(FCommandLine::Get(), TEXT("VroomTrack="), RequestedTrack);
        if (HasTrackArgument) Circuit->SelectTrack(FMath::Clamp(RequestedTrack, 0, 5));
        else if (!VerificationMode && !AutoDemo) Circuit->SelectTrack(Vroom::SessionModel::ChampionshipOrder[0]);
        if (FParse::Param(FCommandLine::Get(), TEXT("VroomTrackAudit")) && !Circuit->VerifyTrackGeometry())
        { FPlatformMisc::RequestExitWithStatus(false, 1); return; }
        Session.SelectTrack(Circuit->TrackIndex);
        TourStartDistance = Drive.State.DistanceM;
        if (TourDemo) Circuit->ConfigureTraffic(true, false, Session.Difficulty);
        if (TrafficTest)
        {
            ResetCar();
            Drive.State.DistanceM = 350;
            PreviousDrive = Drive.State;
            Circuit->ConfigureTraffic(true, false, Vroom::RaceDifficulty::Normal);
            Circuit->ReactiveTraffic = true;
            for (int32 I = 0; I < Circuit->OpponentS.Num(); ++I)
            {
                auto& Rival = Circuit->Traffic[I];
                Rival = Vroom::TrafficCar{};
                Rival.Distance = I == 0 ? 295 : 1050 + I * 40;
                Rival.Speed = I == 0 ? 50 : 30;
                Circuit->OpponentS[I] = Circuit->PreviousOpponentS[I] = Rival.Distance;
                Circuit->OpponentLanes[I] = Circuit->PreviousOpponentLanes[I] = 0;
                Circuit->OpponentSpeeds[I] = Rival.Speed;
            }
            Circuit->Running = true; Circuit->Paused = false; Circuit->RaceClock = 0;
        }
        if (OffroadTestSide != 0)
        {
            ResetCar();
            double BestCurve = 0;
            for (double S = 350; S < Circuit->LengthM() - 100; S += 5)
                if (FMath::Abs(Circuit->Curvature(S)) > BestCurve)
                { BestCurve = FMath::Abs(Circuit->Curvature(S)); OffroadStartDistance = S; }
            Drive.State.DistanceM = OffroadStartDistance;
            Drive.State.LateralM = OffroadTestSide * (Circuit->HalfWidthM + 2.25);
            Drive.TyreHealth = 2048 * .83; Drive.Fuel = .98;
            PreviousDrive = Drive.State;
            Circuit->ConfigureTraffic(false, false, Session.Difficulty);
            Circuit->Running = true; Circuit->Paused = false; Circuit->RaceClock = 0;
            UE_LOG(LogTemp, Display, TEXT("VROOM: Offroad restart initial side=%d speed=0 s=%.2f lateral=%.2f curvature=%.5f"),
                OffroadTestSide, OffroadStartDistance, Drive.State.LateralM, Circuit->Curvature(OffroadStartDistance));
        }
        if (StartTest)
        {
            if (StartTestScenario == TEXT("mute")) Vroom::Settings::Mutable().MasterVolume = 0;
            StartSession();
            StartTestDistance = Drive.State.DistanceM;
            for (double S : Circuit->OpponentS) StartTestOpponents.Add(S);
        }
        BeginCircuitPreparation();
        FString SessionPreview;
        if (FParse::Value(FCommandLine::Get(), TEXT("VroomSessionPreview="), SessionPreview))
        {
            // Synthetic HUD poses only; the flag does not validate race rules.
            Session.Mode = SessionPreview == TEXT("arcade") ? Vroom::SessionMode::Arcade : Vroom::SessionMode::GrandPrix;
            StartSession(); Circuit->RaceClock = 24; AutoDemo = false;
            Session.Phase = Session.QualifyingRun ? Vroom::SessionPhase::Qualifying : Vroom::SessionPhase::Racing;
            Drive.State.SpeedMps = 0; PreviousDrive = Drive.State;
            if (SessionPreview == TEXT("results"))
            {
                Session.QualifyingRun = false; Session.QualificationComplete = true;
                Session.Position = 3; Session.LapsCompleted = 3; Session.Finish(334.28, false);
                const std::array<int, 13> PreviewOrder = {0,1,12,2,3,4,5,6,7,8,9,10,11};
                Session.ClassifyRace(PreviewOrder);
                Score = 12850; Circuit->Paused = true;
            }
            else if (SessionPreview == TEXT("pit"))
            {
                Session.QualifyingRun = false; Session.Mode = Vroom::SessionMode::GrandPrix;
                Drive.State.DistanceM = 102; Drive.State.LateralM = 12.5; PreviousDrive = Drive.State;
                Pit.Locate(102,12.5,0,Circuit->LengthM()); Pit.Request(true,true);
            }
            else if (SessionPreview == TEXT("controls")) HandleAction(Vroom::Action::Controls);
        }
        // BeginPlay can run before GameMode creates the circuit. Prepare this
        // capture once, only after the same dependencies as the real menu exist.
        if (FParse::Param(FCommandLine::Get(), TEXT("VroomGraphicsPreview")))
        {
            HandleAction(Vroom::Action::Options);
            UE_LOG(LogTemp, Display, TEXT("VROOM: Graphics preview %s"),
                Vroom::Graphics::IsOpen() ? TEXT("PASS") : TEXT("FAIL"));
        }
    }
    auto* PC = Cast<APlayerController>(GetController());
    if (!PC) return;
    auto Pressed = [&](const FKey& Key) { return !VerificationMode && PC->WasInputKeyJustPressed(Key); };
    auto ActionPressed = [&](Vroom::Action Action) { return !VerificationMode && Vroom::Settings::WasPressed(PC, Action); };
    Circuit->UpdateVegetation();
    // This is also used by verification: CSV capture still starts at frame 0,
    // with the same hard frame-time gates and warmup as an ordinary launch.
    if (UpdateCircuitPreparation()) { UpdateDiagnostics(Dt); return; }
    if (StartTest && StartTestScenario == TEXT("restart") && StartTestPhase == 0 && TestSeconds >= 1.0)
    {
        RestartSession();
        StartTestPhase = 1;
        UE_LOG(LogTemp, Display, TEXT("VROOM: Start test restarted during countdown"));
        return;
    }
    if (StartTest && StartTestScenario == TEXT("pause"))
    {
        if (StartTestPhase == 0 && Circuit->RaceClock > -1.5)
        {
            Circuit->Paused = true; StartTestPauseRemaining = .75;
            StartTestPauseClock = Circuit->RaceClock; StartTestPhase = 1;
        }
        if (StartTestPhase == 1)
        {
            StartTestStationary &= FMath::Abs(Circuit->RaceClock - StartTestPauseClock) < .00001;
            StartTestPauseRemaining -= Dt;
            if (StartTestPauseRemaining <= 0)
            {
                Circuit->Paused = false; StartTestPhase = 2;
                UE_LOG(LogTemp, Display, TEXT("VROOM: Start test resumed after pause"));
            }
        }
    }
    if (!VerificationMode && !Vroom::Graphics::IsOpen())
    {
        const bool WasControlsOpen = Vroom::Settings::IsOpen();
        const bool WasPaused = Circuit->Paused;
        if (!WasControlsOpen && ActionPressed(Vroom::Action::Controls)) HandleAction(Vroom::Action::Controls);
        if (Vroom::Settings::HandleMenuInput(PC))
        {
            if (!WasControlsOpen && Vroom::Settings::IsOpen()) ControlsPauseBefore = WasPaused;
            Circuit->Paused = Vroom::Settings::IsOpen() ? true : ControlsPauseBefore;
            Drive.Automatic = Vroom::Settings::Get().Automatic;
            ChaseCamera = Vroom::Settings::Get().ChaseCamera;
            PC->bShowMouseCursor = Vroom::Settings::IsOpen() || !Circuit->Running || Circuit->Paused;
            if (PC->bShowMouseCursor)
            {
                FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
                PC->SetInputMode(Mode);
            }
            else PC->SetInputMode(FInputModeGameOnly());
            UpdatePresentation(Circuit->Running ? FrameTiming.Alpha() : 1);
            EngineAudio->SetVolumeMultiplier(0); UpdateDiagnostics(Dt); return;
        }
    }
    if (HandleGraphicsInput(PC))
    {
        UpdatePresentation(Circuit->Running ? FrameTiming.Alpha() : 1);
        EngineAudio->SetVolumeMultiplier(0);
        UpdateDiagnostics(Dt);
        return;
    }
    if (VerificationMode && Vroom::Settings::IsOpen())
    {
        UpdatePresentation(Circuit->Running ? FrameTiming.Alpha() : 1);
        UpdateDiagnostics(Dt); return;
    }
    if (!TestImpact.IsEmpty() && !TestPrepared) PrepareCollisionTest();
    if (PitTest && !TestPrepared)
    {
        ResetCar(); TestPrepared = true;
        Circuit->Running = true; Circuit->Paused = false; Circuit->RaceClock = 0;
        Circuit->ConfigureTraffic(true, false, Vroom::RaceDifficulty::Normal);
        Drive.State.DistanceM = 102; Drive.State.LateralM = 12.5;
        Drive.Fuel = .2; Drive.TyreHealth = 100;
        PreviousDrive = Drive.State;
        Pit.Locate(102,12.5,0,Circuit->LengthM()); Pit.Request(true,true);
        PitTestOpponentStart = Circuit->OpponentS.IsEmpty() ? 0 : Circuit->OpponentS[0];
        if (ExitAfter <= 0) ExitAfter = 9;
        UE_LOG(LogTemp, Display, TEXT("VROOM: Pit test started combined service=7s, throttle held throughout"));
    }
    if (!VerificationMode && Pressed(EKeys::MiddleMouseButton) && Circuit->Running)
        HandleAction(Vroom::Action::Pause);
    if (!AutoDemo && Vroom::MenuLayout::HandleInput(this, PC))
    {
        UpdatePresentation(Circuit->Running ? FrameTiming.Alpha() : 1);
        Circuit->RenderOpponents(1);
        EngineAudio->SetVolumeMultiplier(.025f * Vroom::Settings::Get().MasterVolume);
        UpdateDiagnostics(Dt); return;
    }
    const FKey TrackKeys[] = {EKeys::F1,EKeys::F2,EKeys::F3,EKeys::F4,EKeys::F5,EKeys::F6};
    for (int32 I = 0; I < 6; ++I)
    {
        if (Pressed(TrackKeys[I]))
        {
            SelectSessionTrack(I);
        }
    }
    if (AutoDemo && !Circuit->Running)
    {
        Circuit->Running = true;
        Circuit->RaceClock = FParse::Param(FCommandLine::Get(), TEXT("VroomPreviewCountdown")) ? -3 : 0;
    }
    for (auto Action : {Vroom::Action::Menu, Vroom::Action::Pause, Vroom::Action::Restart,
        Vroom::Action::Gearbox, Vroom::Action::View, Vroom::Action::MouseSteer})
        if (ActionPressed(Action)) HandleAction(Action);
    Pit.Locate(Drive.State.DistanceM, Drive.State.LateralM, Drive.State.SpeedMps, Circuit->LengthM());
    if (Pit.Available && !Session.Solo() && !Circuit->Paused
        && (ActionPressed(Vroom::Action::Start) || (MouseSteer && Pressed(EKeys::LeftMouseButton))))
        Pit.Request(true, true);
    if (ActionPressed(Vroom::Action::Tyres)) HandleAction(Vroom::Action::Tyres);
    if (ActionPressed(Vroom::Action::Refuel)) HandleAction(Vroom::Action::Refuel);
    if (Pressed(EKeys::F10)) FovIndex = (FovIndex + 1) % 3;
    Vroom::DriveInput Input;
    Input.Throttle = Vroom::Settings::IsDown(PC, Vroom::Action::Throttle) || (MouseSteer && PC->IsInputKeyDown(EKeys::LeftMouseButton)) ? 1 : PC->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis);
    Input.Brake = Vroom::Settings::IsDown(PC, Vroom::Action::Brake) || (MouseSteer && PC->IsInputKeyDown(EKeys::RightMouseButton)) ? 1 : PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis);
    const float AnalogSteer = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
    const float Deadzone = VerificationMode ? VroomSteeringInputDeadzone() : Vroom::Settings::Get().SteeringDeadzone;
    const float AnalogSteerClean = Vroom::SteeringDeadzone(AnalogSteer, Deadzone) * Vroom::Settings::Get().SteeringSensitivity;
    const float DigitalSteer = (Vroom::Settings::IsDown(PC, Vroom::Action::SteerLeft) ? -1.f : 0.f)
                           + (Vroom::Settings::IsDown(PC, Vroom::Action::SteerRight) ? 1.f : 0.f);
    float Steering = DigitalSteer != 0.f ? DigitalSteer : AnalogSteerClean;
    if (MouseSteer)
    {
        float X, Y; PC->GetInputMouseDelta(X,Y);
        MouseTarget = FMath::Clamp(MouseTarget + X * Vroom::Settings::Get().MouseSensitivity, -1.f, 1.f);
        Steering += MouseTarget;
    }
    // Filter at the simulation rate below; a render-frame clamp changed the
    // steering response whenever the GPU cadence changed in a corner.
    Input.Steer = FMath::Clamp(Steering,-1.f,1.f);
    Input.Drift = Vroom::Settings::IsDown(PC, Vroom::Action::Drift);
    PendingShiftUp |= ActionPressed(Vroom::Action::ShiftUp) || (MouseSteer && Pressed(EKeys::MouseScrollUp));
    PendingShiftDown |= ActionPressed(Vroom::Action::ShiftDown) || (MouseSteer && Pressed(EKeys::MouseScrollDown));
    Input.ShiftUp = PendingShiftUp;
    Input.ShiftDown = PendingShiftDown;
    if (AutoDemo)
    {
        const double S = Drive.State.DistanceM;
        const double K = Circuit->Curvature(S + 12);
        const double CurveAhead = TourDemo ? FMath::Max(std::abs(K), FMath::Abs(Circuit->Curvature(S + 65))) : std::abs(K);
        const double Desired = FMath::Clamp(FMath::Sqrt(26.0 / FMath::Max(CurveAhead, .004)), 15.0, TourDemo ? 60.0 : 70.0);
        Input.Throttle = Drive.State.SpeedMps < Desired ? .85 : 0;
        // Stay under the 0.34 drift-entry brake so assisted tours do not hop.
        Input.Brake = Drive.State.SpeedMps > Desired + 3 ? .28 : 0;
        // Arcade XBCMP3: hold the stick to cancel curve wash and stay near
        // a racing line. No yaw PID — the chassis stays parallel.
        const double SteerGain = std::max(Vroom::DriveModel::ArcadeSteerLatMps(1, Drive.State.SpeedMps) * Drive.TyreGrip(), 0.4);
        const double Wash = Vroom::DriveModel::ArcadeWashLatMps(Drive.State.SpeedMps, K);
        Input.Steer = FMath::Clamp(Wash / SteerGain - Drive.State.LateralM * .12, -.85, .85);
        if (FMath::IsFinite(PreviewSteering) && FMath::Abs(PreviewSteering) <= 1)
        {
            Input = Vroom::DriveInput{};
            Input.Steer = PreviewSteering;
        }
    }
    // Diagnostic override applied AFTER the control model so a probe can hold a
    // constant input with certainty. Auto-demo previously clobbered this value.
    static bool FixedInit = false, HasFixedSteer = false, HasFixedThrottle = false;
    static float FixedSteer = 0, FixedThrottle = 0;
    if (!FixedInit)
    {
        HasFixedSteer = FParse::Value(FCommandLine::Get(), TEXT("VroomFixedSteer="), FixedSteer);
        HasFixedThrottle = FParse::Value(FCommandLine::Get(), TEXT("VroomFixedThrottle="), FixedThrottle);
        FixedInit = true;
    }
    if (HasFixedSteer || HasFixedThrottle)
    {
        Input.Steer = FMath::Clamp(FixedSteer, -1.f, 1.f);
        Input.Throttle = FMath::Clamp(FixedThrottle, 0.f, 1.f);
        Input.Brake = 0;
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("VroomDriftTest")))
    {
        Input = Vroom::DriveInput{};
        FString Scenario;
        FParse::Value(FCommandLine::Get(), TEXT("VroomDriftScenario="), Scenario);
        Input.Throttle = .6;
        Input.Drift = TestSeconds >= .2 && TestSeconds < (Scenario == TEXT("expert") ? 2.55 : 1.8);
        if (Input.Drift)
            Input.Steer = TestSeconds < .5 || Scenario == TEXT("unskilled") ? .8 : -.45;
    }
    if (!TestImpact.IsEmpty())
    {
        Input = Vroom::DriveInput{};
        if (TestImpact == TEXT("rub") && Contacts.State.ControlledTyreHop)
            Input.Steer = Contacts.State.ImpactSide * .7;
        else InputSteering = 0.f;
    }
    if (PitTest) { Input = Vroom::DriveInput{}; Input.Throttle = 1; }
    if (TrafficTest) Input = Vroom::DriveInput{};
    if (OffroadTestSide != 0)
    {
        Input = Vroom::DriveInput{}; Input.Throttle = 1;
        Input.Steer = FMath::Clamp(-Drive.State.LateralM * .8, -1.0, 1.0);
        if (FMath::Abs(Drive.State.LateralM) < Circuit->HalfWidthM - .3) OffroadReturned = true;
    }
    if (StartTest) { Input = Vroom::DriveInput{}; Input.Throttle = 1; }

    if (AutoDemo && PreviewClock >= NextTrace)
    {
        NextTrace = PreviewClock + 1.0;
        UE_LOG(LogTemp, Display, TEXT("VROOM-TRACE t=%.1f s=%.1f v=%.1f lat=%.2f h=%.3f vis=%.3f yaw=%.1f steer=%.2f K=%.4f skid=%.2f"),
            PreviewClock, Drive.State.DistanceM, Drive.State.SpeedMps, Drive.State.LateralM,
            Drive.State.HeadingRad, Drive.VisualHeading(), GetActorRotation().Yaw,
            Drive.State.Steering, Circuit->Curvature(Drive.State.DistanceM), Drive.SkidAmount);
        if (TourDemo)
            UE_LOG(LogTemp, Display, TEXT("VROOM-TOUR track=%d distanceCovered=%.3f length=%.3f complete=%d"),
                Circuit->TrackIndex, Drive.State.DistanceM - TourStartDistance, Circuit->LengthM(),
                Drive.State.DistanceM - TourStartDistance >= Circuit->LengthM());
    }
    if (Circuit->Running && !Circuit->Paused)
    {
        const int32 Steps = FrameTiming.Advance(Dt);
        for (int32 Step = 0; Step < Steps; ++Step)
        {
            SimulateStep(Input, Vroom::FrameTiming::StepSeconds);
            Input.ShiftUp = false; Input.ShiftDown = false;
            PendingShiftUp = PendingShiftDown = false;
            if (Circuit->Paused || !Circuit->Running) break;
        }
    }
    const double Alpha = Circuit->Running ? FrameTiming.Alpha() : 1;
    if (TickDebug && PreviewClock < 8)
    {
        UE_LOG(LogTemp, Display, TEXT("VROOM-TICK f=%03d dt=%.4f in=%+.2f st=%+.3f vh=%+.3f vis=%+.3f hd=%+.3f lat=%+.2f s=%.1f yaw=%+.1f cock=%+.3f"),
            TickFrame++, Dt, Input.Steer, Drive.State.Steering, Drive.VelocityHeading(), Drive.VisualHeading(), Drive.State.HeadingRad,
            Drive.State.LateralM, Drive.State.DistanceM, GetActorRotation().Yaw, CockpitSteering);
    }
    UpdatePresentation(Alpha);
    if (MotionTrace)
    {
        const FVector P = GetActorLocation();
        UE_LOG(LogTemp, Display, TEXT("VROOM-MOTION dt=%.6f s=%.6f lat=%.6f x=%.4f y=%.4f z=%.4f yaw=%.6f slip=%.6f steer=%.6f hop=%.6f"),
            Dt, FMath::Lerp(PreviousDrive.DistanceM, Drive.State.DistanceM, Alpha),
            FMath::Lerp(PreviousDrive.LateralM, Drive.State.LateralM, Alpha),
            P.X, P.Y, P.Z, GetActorRotation().Yaw,
            FMath::Lerp(PreviousVisualHeading, Drive.VisualHeading(), Alpha),
            VisualSteering, FMath::Lerp(PreviousHopHeight, Drive.HopHeightM, Alpha));
    }
    UpdateCrashDebris(Circuit->Running && !Circuit->Paused ? FMath::Min(Dt, .1f) : 0);
    UpdateMirrors(Circuit->Running && !Circuit->Paused ? FMath::Min(Dt, .1f) : 0);
    if (!TestImpact.IsEmpty())
    {
        const bool Blink = Contacts.State.InvulnerabilityRemaining > 0
            && FMath::Fmod(Contacts.State.InvulnerabilityRemaining, .2) < .08;
        const bool Expected = !Blink;
        bool Correct = IsCockpitVisible() == Expected
            && CockpitRoot && CockpitRoot->IsVisible() == Expected
            && MirrorRoot && MirrorRoot->IsVisible() == Expected;
        for (const auto& Mesh : CockpitMeshes) Correct &= Mesh && Mesh->IsVisible() == Expected;
        for (const auto& Mesh : MirrorMeshes) Correct &= Mesh && Mesh->IsVisible() == Expected;
        if (!Correct) ++TestVisibilityErrors;
    }
    Circuit->RenderOpponents(Alpha);
    EngineAudio->SetVolumeMultiplier((Circuit->Paused ? 0 : (Circuit->Running ? .22f : .05f))
        * Vroom::Settings::Get().MasterVolume);
    UpdateSound();
    UpdateDiagnostics(Dt);
}

void AVroomCar::UpdateDiagnostics(float Dt)
{
    PreviewClock += Dt;
    const bool SimulationTest = !TestImpact.IsEmpty() || PitTest || TrafficTest || OffroadTestSide != 0 || StartTest
        || FParse::Param(FCommandLine::Get(), TEXT("VroomDriftTest"));
    const double CheckClock = SimulationTest ? TestSeconds : PreviewClock;
    if (!PreparingCircuit && !CapturePath.IsEmpty() && CheckClock >= CaptureAfter)
    {
        FScreenshotRequest::RequestScreenshot(CapturePath, true, false);
        UE_LOG(LogTemp, Display, TEXT("VROOM: Capture requested %s speed=%.1f km/h s=%.1f lateral=%.2f"), *CapturePath, Drive.State.SpeedMps * 3.6, Drive.State.DistanceM, Drive.State.LateralM);
        UE_LOG(LogTemp, Display, TEXT("VROOM: Capture visibility cockpit=%d mirrors=%d invulnerability=%.3f pose=%.3f startLights=%d"),
            IsCockpitVisible(), MirrorRoot && MirrorRoot->IsVisible(), Contacts.State.InvulnerabilityRemaining,
            VisualSteering, Circuit->StartLightStage);
        CapturePath.Empty();
    }
    if (ExitAfter > 0 && CheckClock >= ExitAfter)
    {
        bool Passed = TestImpact.IsEmpty() || (TestContacts == 1 && TestVisibilityErrors == 0 &&
            ((TestImpact == TEXT("slow") && Contacts.State.LastKind == Vroom::ContactKind::SlowImpact && TestRespawns == 0) ||
            (TestImpact == TEXT("rear") && Contacts.State.LastKind == Vroom::ContactKind::SlowImpact && TestRespawns == 0) ||
            (TestImpact == TEXT("rub") && Contacts.State.LastKind == Vroom::ContactKind::TyreJump
                && TestRespawns == 0 && TestControlledLandings == 1 && LandingQuality > .8
                && !Contacts.State.TyresBroken && !Contacts.State.ControlsLocked) ||
            (TestImpact == TEXT("crash") && Contacts.State.LastKind == Vroom::ContactKind::Crash && TestRespawns == 1) ||
            (TestImpact == TEXT("tyre") && Contacts.State.LastKind == Vroom::ContactKind::TyreJump && TestRespawns == 1) ||
            (TestImpact == TEXT("scenery") && Contacts.State.LastKind == Vroom::ContactKind::Crash && TestRespawns == 1
                && !Contacts.State.ControlsLocked && Drive.State.SpeedMps < .1
                && FMath::Abs(Drive.State.LateralM) < Circuit->HalfWidthM)));
        if (PitTest)
        {
            Passed = PitTestStationary && PitTestCompleted && !Pit.Servicing && Drive.TyreHealth > 2040
                && Drive.Fuel > .99 && Drive.State.SpeedMps > 1 && Drive.State.DistanceM > 102
                && !Contacts.State.ControlsLocked && TestContacts == 0 && Circuit->RaceClock >= 8
                && !Circuit->OpponentS.IsEmpty() && Circuit->OpponentS[0] > PitTestOpponentStart + 5;
            UE_LOG(LogTemp, Display, TEXT("VROOM-PIT stationary=%d completed=%d servicing=%d fuel=%.4f tyres=%.1f speed=%.2f s=%.2f clock=%.2f opponentProgress=%.2f"),
                PitTestStationary, PitTestCompleted, Pit.Servicing, Drive.Fuel, Drive.TyreHealth,
                Drive.State.SpeedMps, Drive.State.DistanceM, Circuit->RaceClock, Circuit->OpponentS[0] - PitTestOpponentStart);
        }
        if (OffroadTestSide != 0)
        {
            Passed &= OffroadReturned && Drive.State.DistanceM > OffroadStartDistance + 8
                && Drive.State.SpeedMps > 2 && TestRespawns == 0 && TestContacts == 0;
            UE_LOG(LogTemp, Display, TEXT("VROOM: Offroad restart %s returned=%d progress=%.2f speed=%.2f lateral=%.2f"),
                Passed ? TEXT("PASS") : TEXT("FAIL"), OffroadReturned, Drive.State.DistanceM - OffroadStartDistance,
                Drive.State.SpeedMps, Drive.State.LateralM);
        }
        if (StartTest)
        {
            Passed &= StartTestStationary && Circuit->RaceClock >= 2 && Drive.State.DistanceM > StartTestDistance + 3;
            Passed &= Session.Phase == Vroom::SessionPhase::Racing;
            Passed &= StartTestScenario != TEXT("restart") || StartTestPhase == 1;
            Passed &= StartTestScenario != TEXT("pause") || StartTestPhase == 2;
            Passed &= StartTestScenario != TEXT("mute") || Vroom::Settings::Get().MasterVolume == 0;
            UE_LOG(LogTemp, Display, TEXT("VROOM: Start integration %s stationary=%d raceClock=%.2f progress=%.2f"),
                Passed ? TEXT("PASS") : TEXT("FAIL"), StartTestStationary, Circuit->RaceClock,
                Drive.State.DistanceM - StartTestDistance);
        }
        if (TrafficTest)
        {
            Passed &= TrafficTestCommitted && TrafficTestBraked && TrafficTestAlongside && TrafficTestClear
                && TrafficTestCompleted && TestContacts == 0 && TestRespawns == 0;
            UE_LOG(LogTemp, Display, TEXT("VROOM: Traffic overtake %s committed=%d braked=%d alongside=%d clear=%d completed=%d"),
                Passed ? TEXT("PASS") : TEXT("FAIL"), TrafficTestCommitted, TrafficTestBraked,
                TrafficTestAlongside, TrafficTestClear, TrafficTestCompleted);
        }
        UE_LOG(LogTemp, Display, TEXT("VROOM: Verification %s scenario=%s contacts=%d respawns=%d visibilityErrors=%d"), Passed ? TEXT("PASS") : TEXT("FAIL"), *TestImpact, TestContacts, TestRespawns, TestVisibilityErrors);
        FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 1);
    }
}

void AVroomCar::SimulateStep(const Vroom::DriveInput& RawInput, double H)
{
    LandingFeedbackTime = FMath::Max(0.0, LandingFeedbackTime - H);
    PreviousDrive = Drive.State;
    PreviousVisualHeading = Contacts.State.ControlsLocked ? Drive.State.HeadingRad : Drive.VisualHeading();
    PreviousContact = Contacts.State;
    PreviousHopHeight = Drive.HopHeightM;
    Circuit->TrafficPlayer.LaneVelocity = Circuit->TrafficPlayerActive && H > 0
        ? FMath::Clamp((Drive.State.LateralM - Circuit->TrafficPlayer.Lateral) / H, -12.0, 12.0) : 0;
    Circuit->TrafficPlayer.Distance = Drive.State.DistanceM;
    Circuit->TrafficPlayer.Lateral = Drive.State.LateralM;
    Circuit->TrafficPlayer.Speed = Drive.State.SpeedMps;
    Circuit->TrafficPlayerActive = !TourDemo && Circuit->OpponentsVisible;
    if (TrafficTest && !Circuit->Traffic.empty())
    {
        const auto& Rival = Circuit->Traffic[0];
        const double Gap = Rival.Distance - Drive.State.DistanceM;
        TrafficTestCommitted |= Rival.OvertakeTarget == -1;
        TrafficTestBraked |= Gap < -5 && Rival.Speed < 48;
        if (FMath::Abs(Gap) < 4.6)
        {
            TrafficTestAlongside = true;
            TrafficTestClear &= FMath::Abs(Rival.Lateral - Drive.State.LateralM) > 2.15;
        }
        TrafficTestCompleted |= Gap > 12 && Rival.OvertakeTarget == -2;
    }
    if (StartTest && Circuit->RaceClock < 0)
    {
        StartTestStationary &= FMath::Abs(Drive.State.DistanceM - StartTestDistance) < .001;
        for (int32 I = 0; I < StartTestOpponents.Num(); ++I)
            StartTestStationary &= FMath::Abs(Circuit->OpponentS[I] - StartTestOpponents[I]) < .001;
    }
    Circuit->AdvanceRace(H);
    TestSeconds += H;
    if (Circuit->RaceClock < 0)
    {
        const double Target = 1000.0 + 8500.0 * std::clamp(RawInput.Throttle, 0.0, 1.0);
        Drive.State.Rpm = Drive.State.Rpm + (Target - Drive.State.Rpm) * (1.0 - std::exp(-H / 0.09));
        Drive.State.SpeedMps = 0;
        return;
    }
    Pit.Locate(Drive.State.DistanceM, Drive.State.LateralM, Drive.State.SpeedMps, Circuit->LengthM());
    if (Pit.Servicing)
    {
        if (PitTest) PitTestStationary &= FMath::Abs(Drive.State.DistanceM - 102) < .001
            && FMath::Abs(Drive.State.LateralM - 12.5) < .001 && Drive.State.SpeedMps < .001;
        Drive.CancelArcadeEffects(); Drive.State.SpeedMps = 0; Drive.State.Rpm = 1000;
        Drive.State.Steering = 0; InputSteering = 0;
        if (Pit.Step(H))
        {
            if (PitTest) PitTestCompleted = true;
            if (Pit.NeedTyres) { Drive.ChangeTyres(); Contacts.Reset(); ResetCrashDebris(); }
            if (Pit.NeedFuel) Drive.Refuel();
            UE_LOG(LogTemp, Display, TEXT("VROOM: Pit service complete tyres=%d fuel=%d duration=%.1f"), Pit.NeedTyres, Pit.NeedFuel, Pit.Duration);
        }
        UpdateSessionRules(); return;
    }

    Vroom::DriveInput Input = RawInput;
    if (Contacts.State.ControlsLocked) Input = Vroom::DriveInput{};
    if (Pit.InLane && !Pit.Servicing)
    {
        Input.Throttle = std::min(Input.Throttle, 0.28);
        if (Drive.State.SpeedMps > 16.0)
        {
            Input.Throttle = 0;
            Input.Brake = std::max(Input.Brake, 0.28);
        }
    }
    Drive.Slipstream = 0;
    if (Circuit->OpponentsVisible && !TourDemo)
    {
        const double Len = Circuit->LengthM();
        for (int32 I = 0; I < Circuit->OpponentS.Num(); ++I)
        {
            double Gap = std::fmod(Circuit->OpponentS[I] - Drive.State.DistanceM + Len * 100.0, Len);
            if (Gap > Len * 0.5) Gap -= Len;
            if (Gap > 0.8 && Gap < 16.0 && std::abs(Circuit->OpponentLanes[I] - Drive.State.LateralM) < 2.2)
                Drive.Slipstream = std::max(Drive.Slipstream, 1.0 - Gap / 16.0);
        }
    }
    InputSteering += (Input.Steer - InputSteering) * Vroom::ExponentialAlpha(VroomSteeringInputFilterHz(), H);
    Input.Steer = InputSteering;
    const bool Wrecked = Contacts.State.Phase == Vroom::ContactPhase::Crashed || Contacts.State.Phase == Vroom::ContactPhase::Recovering;
    const bool Recovering = Contacts.State.Phase == Vroom::ContactPhase::Recovering;
    const bool Airborne = Contacts.State.Phase == Vroom::ContactPhase::Airborne;
    const double Half = Circuit->HalfWidthM;
    const double PrevLat = Drive.State.LateralM;
    if (Recovering)
    {
        Drive.CancelArcadeEffects();
        Drive.State.SpeedMps = 0;
        Drive.State.Rpm = 1000;
        Drive.State.Steering = 0;
    }
    else if (Wrecked)
    {
        Drive.CancelArcadeEffects();
        const double Before = Drive.State.SpeedMps;
        Drive.State.SpeedMps = FMath::Max(0.0, Drive.State.SpeedMps - 45 * H);
        const double Speed = (Before + Drive.State.SpeedMps) * .5;
        const double K = Circuit->Curvature(Drive.State.DistanceM);
        const double Progress = Speed * std::cos(Drive.VelocityHeading()) / FMath::Clamp(1 - K * Drive.State.LateralM, .3, 3.0);
        Drive.State.DistanceM += Progress * H;
        Drive.State.LateralM += Speed * std::sin(Drive.VelocityHeading()) * H;
        Drive.SetVelocityHeading(Drive.VelocityHeading() - K * Progress * H);
        Drive.State.HeadingRad += (Drive.VelocityHeading() - Drive.State.HeadingRad)
            * Vroom::ExponentialAlpha(8, H);
        Drive.State.Rpm = 1000;
        Drive.State.Steering = 0;
    }
    else Drive.Step(Input, H, Circuit->Curvature(Drive.State.DistanceM), Pit.InLane ? 15.2 : Half,
        Airborne ? 0 : Circuit->Elevation(Drive.State.DistanceM).Grade, Airborne);
    Contacts.Step(H, Drive.State.Steering);
    const auto Landing = Contacts.ConsumeLanding();
    if (Landing.JustLanded)
    {
        Drive.State.SpeedMps *= Landing.SpeedRetention;
        Drive.ExtraLatVel *= Landing.LateralVelocityRetention;
        LandingQuality = Landing.LandingControl;
        LandingFeedbackTime = 1.1;
        ++TestControlledLandings;
        UE_LOG(LogTemp, Display, TEXT("VROOM: Controlled landing quality=%.3f speedRetention=%.3f"),
            Landing.LandingControl, Landing.SpeedRetention);
    }

    if (!Recovering && Contacts.State.Phase == Vroom::ContactPhase::Recovering)
    {
        Contacts.PrepareRoadRecovery(Drive.State.LateralM, Half);
        RecoveryStartHeading = Drive.State.HeadingRad;
        Drive.State.SpeedMps = 0;
        Drive.ExtraLatVel = 0;
        UE_LOG(LogTemp, Display, TEXT("VROOM: Recovery started s=%.2f lateral=%.2f duration=%.2f"),
            Drive.State.DistanceM, Drive.State.LateralM, Contacts.State.PhaseDuration);
    }
    if (Recovering || Contacts.State.Phase == Vroom::ContactPhase::Recovering)
    {
        Drive.State.LateralM = Contacts.State.RecoveryLateralM;
        Drive.State.HeadingRad = RecoveryStartHeading * (1 - Contacts.State.RecoveryBlend);
        Drive.SetVelocityHeading(Drive.State.HeadingRad);
        Drive.State.SpeedMps = 0;
    }

    // Sweep the actual rendered car volume against the opaque scenery's
    // registered instances. The tall crossbeam of a gantry stays above us;
    // its pillars, the real tree trunks and pit walls remain solid.
    if (!TourDemo && !Contacts.State.ControlsLocked && Contacts.State.InvulnerabilityRemaining <= 0)
    {
        const FVector Start = Circuit->Position(PreviousDrive.DistanceM, PreviousDrive.LateralM)
            + FVector(0,0,66 + (PreviousContact.HeightM + PreviousHopHeight) * 100);
        const FVector End = Circuit->Position(Drive.State.DistanceM, Drive.State.LateralM)
            + FVector(0,0,66 + (Contacts.State.HeightM + Drive.HopHeightM) * 100);
        FRotator CarRotation = Circuit->DrivingRotation(Drive.State.DistanceM);
        CarRotation.Yaw += FMath::RadiansToDegrees(Drive.VisualHeading());
        double Fraction = 1;
        FVector Normal;
        if (FVector::DistSquared(Start,End) > 0.01 && Circuit->SweepScenery(Start, End, CarRotation.Quaternion(), Fraction, Normal, true))
        {
            const double RelativeSpeed = FVector::Dist(Start,End) / (100 * H);
            const double SideDot = FVector::DotProduct(Normal, CarRotation.Quaternion().GetRightVector());
            const int Side = FMath::Abs(SideDot) > .1 ? (SideDot > 0 ? -1 : 1) : (Drive.State.LateralM >= 0 ? 1 : -1);
            const auto Hit = Contacts.ForceObjectGraze(RelativeSpeed, Side);
            if (Hit.Kind != Vroom::ContactKind::None)
            {
                Drive.State.DistanceM = FMath::Lerp(PreviousDrive.DistanceM, Drive.State.DistanceM, Fraction);
                Drive.State.LateralM = FMath::Lerp(PreviousDrive.LateralM, Drive.State.LateralM, Fraction);
                Drive.State.HeadingRad = Drive.VisualHeading();
                if (Hit.Kind == Vroom::ContactKind::Crash) Contacts.InheritHopHeight(Drive.HopHeightM);
                Drive.CancelArcadeEffects(Hit.Kind != Vroom::ContactKind::SlowImpact);
                Drive.ExtraLatVel = 0;
                if (Hit.Kind == Vroom::ContactKind::Crash)
                {
                    Drive.State.SpeedMps = 0;
                    Drive.State.Rpm = 1000;
                    Drive.SetVelocityHeading(Drive.State.HeadingRad);
                    Contacts.PrepareRoadRecovery(Drive.State.LateralM, Half);
                    Drive.TyreHealth = FMath::Max(0.0, Drive.TyreHealth - (Contacts.State.TyresBroken ? 180 : 40));
                    ImpactSound = Contacts.State.TyresBroken ? 1 : .4;
                }
                else
                {
                    Drive.State.SpeedMps = Hit.SpeedAfterMps;
                    Drive.SetVelocityHeading(Drive.State.HeadingRad);
                    if (std::abs(Hit.LateralShoveMps) > 0.05)
                        Drive.Shove(Hit.LateralShoveMps);
                    Drive.TyreHealth = FMath::Max(0.0, Drive.TyreHealth - 12);
                    ImpactSound = .28;
                }
                LapClean = false; LapState.MarkDirty("contact");
                ++TestContacts;
                UE_LOG(LogTemp, Display, TEXT("VROOM: Scenery impact kind=%d speed=%.1f km/h s=%.2f lateral=%.2f"),
                    static_cast<int32>(Hit.Kind), Hit.RelativeSpeedMps * 3.6, Drive.State.DistanceM, Drive.State.LateralM);
            }
        }
    }

    if (!Wrecked && Contacts.State.Phase == Vroom::ContactPhase::Driving)
    {
        // Kerb pop: MK8-style hop when a fast car climbs the strip.
        if (std::abs(PrevLat) < Half - 0.4 && std::abs(Drive.State.LateralM) >= Half - 0.4
            && Drive.State.SpeedMps > 22.0 && Drive.HopHeightM <= 0)
            Drive.StartHop(0.22, 0.20);

        // Crest jump from the elevation profile (Autriche has real bumps).
        if (Drive.State.SpeedMps > 20.0 && Drive.HopHeightM <= 0)
        {
            const double Look = std::clamp(Drive.State.SpeedMps * 0.22, 6.0, 14.0);
            const double Drop = Circuit->Elevation(Drive.State.DistanceM).Grade
                - Circuit->Elevation(Drive.State.DistanceM + Look).Grade;
            if (Drop > 0.065)
                Contacts.LaunchJump(Drive.State.SpeedMps, Drop, Drive.State.LateralM > 0 ? 1 : -1);
        }

        // Open run-off uses the drive model's continuous grass drag. An
        // invisible lateral clamp here used to teleport the view in wide turns
        // and made the actual off-track recovery threshold unreachable.
    }
    if (Contacts.ConsumeRespawn())
    {
        // The gradual path has already reached its target. Repair at rest,
        // preserving fuel, race progress and the interpolation's previous pose.
        const auto Stopped = Drive.State;
        const double Fuel = Drive.Fuel, Tyres = Drive.TyreHealth;
        Drive.Reset();
        Drive.State.DistanceM = Stopped.DistanceM;
        Drive.State.LateralM = Stopped.LateralM;
        Drive.State.Elapsed = Stopped.Elapsed;
        Drive.Fuel = Fuel;
        Drive.TyreHealth = Tyres;
        InputSteering = 0;
        MouseTarget = 0;
        ++TestRespawns;
        UE_LOG(LogTemp, Display, TEXT("VROOM: Control restored s=%.2f lateral=%.2f"), Drive.State.DistanceM, Drive.State.LateralM);
    }

    const double Len = Circuit->LengthM();

    if (Circuit->RaceClock > 0 && !Wrecked)
    {
        // vr2.asm SCORV: +1..+5 per VBL above internal 300/400/450/475/487.
        const double Internal = Drive.State.SpeedMps * 5.4;
        int32 Tick = 0;
        if (Internal >= 300) ++Tick;
        if (Internal >= 400) ++Tick;
        if (Internal >= 450) ++Tick;
        if (Internal >= 475) ++Tick;
        if (Internal >= 487) ++Tick;
        ScoreFrac += Tick * 50.0 * H;
        const int32 Whole = static_cast<int32>(ScoreFrac);
        Score = FMath::Clamp(Score + Whole, 0, 99999);
        ScoreFrac -= Whole;
    }
    if (OvertakeSides.Num() != Circuit->OpponentS.Num()) OvertakeSides.SetNumZeroed(Circuit->OpponentS.Num());
    for (int32 I = 0; I < Circuit->OpponentS.Num(); ++I)
    {
        if (!Circuit->OpponentsVisible) break;
        if (Circuit->OpponentRecycled.IsValidIndex(I) && Circuit->OpponentRecycled[I]) OvertakeSides[I] = 0;
        double Gap = std::fmod(Circuit->OpponentS[I] - Drive.State.DistanceM + Len * 100, Len);
        if (Gap > Len / 2) Gap -= Len;
        double PrevGap = Gap;
        if (I < Circuit->PreviousOpponentS.Num())
        {
            PrevGap = std::fmod(Circuit->PreviousOpponentS[I] - PreviousDrive.DistanceM + Len * 100, Len);
            if (PrevGap > Len / 2) PrevGap -= Len;
        }
        if (Circuit->RaceClock > 0 && !Wrecked)
        {
            // vr2.asm PASS3/PASS2: OVER +1 → OVPCAA, +500; OVER -1 → OVMCAA, -500.
            const int Pass = Vroom::TrackOvertake(OvertakeSides[I], PrevGap, Gap, Len);
            if (Pass > 0)
            {
                CarsPassed = FMath::Min(CarsPassed + 1, 99);
                Score = FMath::Clamp(Score + 500, 0, 99999);
            }
            else if (Pass < 0)
            {
                CarsPassed = FMath::Max(CarsPassed - 1, 0);
                Score = FMath::Max(Score - 500, 0);
            }
        }
        const double PlayerDelta = Drive.State.DistanceM - PreviousDrive.DistanceM;
        const double OpponentDelta = Circuit->OpponentS[I] - Circuit->PreviousOpponentS[I];
        Vroom::ContactQuery Q;
        Q.OpponentId = I; Q.GapM = Gap;
        Q.PreviousGapM = Gap - OpponentDelta + PlayerDelta;
        Q.LateralGapM = Circuit->OpponentLanes[I] - Drive.State.LateralM;
        Q.PreviousLateralGapM = (Circuit->PreviousOpponentLanes.IsValidIndex(I)
            ? Circuit->PreviousOpponentLanes[I] : Circuit->OpponentLanes[I]) - PreviousDrive.LateralM;
        Q.PlayerSpeedMps = Drive.State.SpeedMps; Q.OpponentSpeedMps = Circuit->OpponentSpeeds[I];
        Q.StepSeconds = H;
        if (TourDemo || (Circuit->OpponentRecycled.IsValidIndex(I) && Circuit->OpponentRecycled[I])) continue;
        const auto Hit = Contacts.TryContact(Q);
        if (Hit.Kind == Vroom::ContactKind::None) continue;
        Circuit->HitOpponent(I, Hit.RelativeSpeedMps);
        Drive.State.SpeedMps = Hit.SpeedAfterMps;
        // A contact cannot cash out a charged drift or stack its hop with the
        // separate wheel-contact flight. Preserve the interpolated chassis yaw.
        if (Hit.Kind == Vroom::ContactKind::TyreJump || Hit.Kind == Vroom::ContactKind::Crash)
            Contacts.InheritHopHeight(Drive.HopHeightM);
        Drive.CancelArcadeEffects(Hit.Kind != Vroom::ContactKind::SlowImpact);
        if (Hit.Kind == Vroom::ContactKind::Crash || Contacts.State.JumpWillBreakTyres)
            Drive.TyreHealth = FMath::Max(0.0, Drive.TyreHealth - 180);
        if (Hit.Kind == Vroom::ContactKind::Crash)
        {
            Drive.State.HeadingRad = Drive.VisualHeading();
            Drive.CancelArcadeEffects();
            Drive.SetVelocityHeading(std::atan2(Hit.LateralShoveMps, std::max(Drive.State.SpeedMps, 1.0)));
            Contacts.PrepareRoadRecovery(Drive.State.LateralM, Half);
        }
        else if (std::abs(Hit.LateralShoveMps) > 0.05)
            Drive.Shove(Hit.LateralShoveMps);
        LapClean = false; LapState.MarkDirty("contact"); ++TestContacts;
        ImpactSound = Hit.ControlledTyreHop ? .25 : (Hit.Kind == Vroom::ContactKind::SlowImpact ? .35 : 1);
        UE_LOG(LogTemp, Display, TEXT("VROOM: Contact kind=%d relative=%.2f km/h speedAfter=%.2f km/h controlledHop=%d face=%d"),
            static_cast<int32>(Hit.Kind), Hit.RelativeSpeedMps * 3.6, Hit.SpeedAfterMps * 3.6,
            Hit.ControlledTyreHop, static_cast<int32>(Hit.Face));
    }
    const int32 PreviousPhysical = LapState.PhysicalLaps;
    LapState.Observe(Drive.State.DistanceM, Drive.State.LateralM, Len, Circuit->HalfWidthM, Pit.InLane);
    LapClean = !LapState.Dirty && !LapState.Invalid;
    LapInvalidReason = LapState.Invalid ? FString(LapState.Reason)
        : (LapState.Dirty ? FString(LapState.Reason) : FString());
    if (LapState.PhysicalLaps > PreviousPhysical)
    {
        LastLapClean = LapState.LastCompletedClean;
        LastLapInvalidated = LapState.LastCompletedInvalid || !LastLapClean;
        LastLapInvalidReason = LastLapInvalidated ? FString(LapState.LastCompletedReason) : FString();
        LastLap = Circuit->RaceClock - LapStart;
        ValidLaps = LapState.ValidLaps;
        if (LastLapClean && (BestLap <= 0 || LastLap < BestLap)) BestLap = LastLap;
        if (!AutoDemo && Session.QualifyingRun && Session.Active())
        {
            Session.CompleteQualification(LastLap, LastLapClean, QualificationBenchmark());
            Circuit->Paused = true;
            if (auto* PC = Cast<APlayerController>(GetController())) PC->bShowMouseCursor = true;
            UE_LOG(LogTemp, Display, TEXT("VROOM: Qualification time=%.3f clean=%d grid=%d"), LastLap, LastLapClean, Session.QualificationPosition);
        }
        LapStart = Circuit->RaceClock;
        Lap = LapState.PhysicalLaps;
        LapClean = !Contacts.State.ControlsLocked;
        UE_LOG(LogTemp, Display, TEXT("VROOM: Lap %d time %.3f clean=%d valid=%d reason=%s"),
            Lap - 1, LastLap, LastLapClean, ValidLaps, *LastLapInvalidReason);
    }

    UpdateSessionRules();
}

void AVroomCar::UpdatePresentation(double Alpha)
{
    const double S = FMath::Lerp(PreviousDrive.DistanceM, Drive.State.DistanceM, Alpha);
    const double L = FMath::Lerp(PreviousDrive.LateralM, Drive.State.LateralM, Alpha);
    const double Heading = FMath::Lerp(PreviousVisualHeading,
        Contacts.State.ControlsLocked ? Drive.State.HeadingRad : Drive.VisualHeading(), Alpha);
    const double Height = FMath::Lerp(PreviousContact.HeightM, Contacts.State.HeightM, Alpha)
        + FMath::Lerp(PreviousHopHeight, Drive.HopHeightM, Alpha);
    FRotator R = Circuit->DrivingRotation(S);
    R.Yaw += FMath::RadiansToDegrees(Heading);
    R.Pitch += FMath::RadiansToDegrees(FMath::Lerp(PreviousContact.PitchRad, Contacts.State.PitchRad, Alpha));
    R.Roll += FMath::RadiansToDegrees(FMath::Lerp(PreviousContact.RollRad, Contacts.State.RollRad, Alpha));
    SetActorLocationAndRotation(Circuit->Position(S, L) + FVector(0,0,6 + Height * 100), R);
    // Physical driver eye uses the same chassis and wheel positions as the chase view.
    const FTransform& ChaseView = ChaseInspectionView(VerificationMode);
    Camera->SetRelativeLocation(ChaseCamera ? ChaseView.GetLocation() : CockpitEye());
    const float FovOffsets[] = {0,-12,12};
    Camera->SetFieldOfView(FMath::Clamp((VerificationMode ? 68.f : Vroom::Settings::Get().Fov) + FovOffsets[FovIndex], 55.f, 110.f));
    VisualSteering = FMath::Clamp(static_cast<float>(FMath::Lerp(PreviousDrive.Steering, Drive.State.Steering, Alpha)), -1.f, 1.f);
    const float SmoothedSteering = UpdateCockpit(VisualSteering);
    static const bool InspectCockpit = FParse::Param(FCommandLine::Get(), TEXT("VroomInspectCockpit"));
    const FRotator TargetCamRot = ChaseCamera ? ChaseView.Rotator() : FRotator(InspectCockpit ? -14 : CockpitEyePitch(),0,SmoothedSteering * CockpitCameraSteeringYaw());
    const double CamDt = Circuit->Paused ? 0.0 : FMath::Clamp(static_cast<double>(GetWorld()->GetDeltaSeconds()), 0.0, .1);
    Camera->SetRelativeRotation(FQuat::Slerp(Camera->GetRelativeRotation().Quaternion(), TargetCamRot.Quaternion(),
        Vroom::ExponentialAlpha(110, CamDt)));

    const bool Broken = Contacts.State.Phase == Vroom::ContactPhase::Crashed || Contacts.State.Phase == Vroom::ContactPhase::Recovering;
    const double WheelSpeed = FMath::Lerp(PreviousDrive.SpeedMps, Drive.State.SpeedMps, Alpha);
    PlayerWheelVisual.Observe(S, WheelSpeed, CamDt, Circuit->Running && !Circuit->Paused && !Broken);
    auto FrontYaw = [&](double Side)
    {
        return Broken ? 0.0 : Vroom::FrontWheelYaw(VisualSteering, WheelSpeed, Side,
            CockpitWheelSteeringScale(), CockpitWheelSteeringDamping());
    };
    for (int32 I : TyrePartIndices)
    {
        FTransform Pose = BodyBaseTransforms[I];
        const bool Front = Pose.GetLocation().X > 0;
        const double Yaw = Front ? FrontYaw(Pose.GetLocation().Y < 0 ? -1 : 1) : 0;
        const double Roll = Front ? PlayerWheelVisual.FrontRollRad : PlayerWheelVisual.RearRollRad;
        // Steer the axle around Z, then roll the complete tyre/rim around its
        // local Y axis. The suspension pickups receive yaw only below.
        Pose.SetRotation(FQuat(FVector::UpVector, Yaw) * FQuat(FVector::RightVector, Roll) * Pose.GetRotation());
        BodyParts[I]->SetRelativeTransform(Pose);
    }
    FTransform Nose = BodyBaseTransforms[2];
    if (Broken) Nose.SetRotation(FRotator(-18, 9, 12).Quaternion());
    BodyParts[2]->SetRelativeTransform(Nose);

    const bool Blink = Contacts.State.InvulnerabilityRemaining > 0 && FMath::Fmod(Contacts.State.InvulnerabilityRemaining, .2) < .08;
    for (int32 I = 0; I < SuspensionBars.Num(); ++I)
    {
        UStaticMeshComponent* Bar = SuspensionBars[I];
        if (!Bar || !SuspensionChassis.IsValidIndex(I) || !SuspensionHubs.IsValidIndex(I)) continue;
        const FVector Chassis = SuspensionChassis[I];
        FVector Hub = SuspensionHubs[I];
        const int32 Slot = SuspensionWheelIndex.IsValidIndex(I) ? SuspensionWheelIndex[I] : INDEX_NONE;
        if (TyrePartIndices.IsValidIndex(Slot) && BodyParts.IsValidIndex(TyrePartIndices[Slot])
            && BodyBaseTransforms.IsValidIndex(TyrePartIndices[Slot]) && BodyParts[TyrePartIndices[Slot]])
        {
            const FVector Wheel = BodyParts[TyrePartIndices[Slot]]->GetRelativeLocation();
            const FVector Inner = Hub - BodyBaseTransforms[TyrePartIndices[Slot]].GetLocation();
            Hub = Wheel + Inner;
            if (!Broken && SuspensionSteers.IsValidIndex(I) && SuspensionSteers[I])
            {
                const double SteerYaw = FrontYaw(Wheel.Y < 0 ? -1 : 1);
                const double Cos = FMath::Cos(SteerYaw), Sin = FMath::Sin(SteerYaw);
                Hub = Wheel + FVector(Inner.X * Cos - Inner.Y * Sin, Inner.X * Sin + Inner.Y * Cos, Inner.Z);
            }
        }
        Bar->SetRelativeTransform(WishboneTransform(Chassis, Hub));
        const bool Front = SuspensionSteers.IsValidIndex(I) && SuspensionSteers[I];
        Bar->SetVisibility(!Blink && (ChaseCamera || Front));
    }
    for (int32 I = 0; I < BodyParts.Num(); ++I)
    {
        const bool FrontTyre = TyrePartIndices.Contains(I) && BodyBaseTransforms[I].GetLocation().X > 0;
        const bool CockpitHidden = BodyParts[I]->ComponentHasTag(FName(TEXT("VroomCockpitHidden")));
        BodyParts[I]->SetVisibility(!Blink && (ChaseCamera
            || (!CockpitHidden && (FrontTyre || !TyrePartIndices.Contains(I)))));
    }
}

void AVroomCar::PrepareCollisionTest()
{
    ResetCar(); TestPrepared = true;
    Circuit->ReactiveTraffic = false;
    Circuit->Running = true; Circuit->Paused = false;
    Drive.State.DistanceM = 100; Drive.State.SpeedMps = TestImpact == TEXT("slow") ? 20 : (TestImpact == TEXT("crash") ? 80 : 55);
    Drive.State.LateralM = 0; Drive.State.HeadingRad = 0; Drive.State.Steering = 0;
    Drive.State.Gear = TestImpact == TEXT("slow") ? 2 : 5;
    for (int32 I = 0; I < Circuit->OpponentS.Num(); ++I) Circuit->OpponentS[I] = 1000 + I * 100;
    Circuit->OpponentS[0] = 104.4;
    Circuit->OpponentLanes[0] = TestImpact == TEXT("tyre") ? 1.55 : 0;
    Circuit->OpponentSpeeds[0] = TestImpact == TEXT("slow") ? 15 : (TestImpact == TEXT("crash") ? 40 : 10);
    if (TestImpact == TEXT("rub"))
    {
        Drive.State.SpeedMps = 35; Drive.State.Gear = 4;
        Circuit->OpponentSpeeds[0] = 31; Circuit->OpponentLanes[0] = 1.7;
    }
    if (TestImpact == TEXT("rear"))
    {
        Drive.State.SpeedMps = 20; Drive.State.Gear = 2;
        Circuit->OpponentS[0] = 95.6; Circuit->OpponentSpeeds[0] = 25;
    }
    if (TestImpact == TEXT("scenery"))
    {
        // Aim at the real start-gantry pillar at s=160 m, lateral=9 m.
        Drive.State.DistanceM = 150;
        Drive.State.LateralM = 8.5;
        Drive.State.SpeedMps = 30;
        Drive.State.Gear = 3;
        Circuit->OpponentS[0] = 1000;
    }
    Circuit->PreviousOpponentS = Circuit->OpponentS;
    PreviousDrive = Drive.State;
    PreviousVisualHeading = Drive.VisualHeading();
    PreviousContact = Contacts.State;
    UE_LOG(LogTemp, Display, TEXT("VROOM: Collision test prepared: %s"), *TestImpact);
}

void AVroomCar::LoadAmigaSound()
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *(FPaths::ProjectContentDir() / TEXT("Data/amiga_sound.bin")))
        || Bytes.Num() < 18 || FMemory::Memcmp(Bytes.GetData(), "VROMSND1", 8) != 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("VROOM: Amiga sound bank missing; synth fallback"));
        return;
    }
    const uint8* P = Bytes.GetData() + 8;
    auto U16 = [&]() { uint16 V = uint16(P[0]) | (uint16(P[1]) << 8); P += 2; return V; };
    const uint16 NPer = U16(), NEng = U16(), NScr = U16(), NCra = U16(), NNoi = U16();
    const int32 Need = 18 + NPer * 2 + NEng + NScr + NCra + NNoi;
    if (Bytes.Num() < Need || NPer == 0)
        return;
    AmigaSound = MoveTemp(Bytes);
    P = AmigaSound.GetData() + 18;
    AmigaPeriods.SetNum(NPer);
    FMemory::Memcpy(AmigaPeriods.GetData(), P, NPer * 2);
    P += NPer * 2;
    AmigaEngine = reinterpret_cast<const int8*>(P); P += NEng; AmigaEngineN = NEng;
    // mot2 is every other byte of mot1 (sub0.asm bmot2) — an octave higher.
    AmigaEngineOctave.SetNum(FMath::Max(1, NEng / 2));
    for (int32 I = 0; I < AmigaEngineOctave.Num(); ++I)
        AmigaEngineOctave[I] = AmigaEngine[FMath::Min(I * 2, NEng - 1)];
    AmigaScreech = reinterpret_cast<const int8*>(P); P += NScr; AmigaScreechN = NScr;
    AmigaCrash = reinterpret_cast<const int8*>(P); P += NCra; AmigaCrashN = NCra;
    AmigaNoise = reinterpret_cast<const int8*>(P); AmigaNoiseN = NNoi;
    UE_LOG(LogTemp, Display, TEXT("VROOM: Amiga sound bank engine=%d screech=%d crash=%d noise=%d periods=%d"),
        AmigaEngineN, AmigaScreechN, AmigaCrashN, AmigaNoiseN, AmigaPeriods.Num());
}

void AVroomCar::UpdateSound()
{
    if (!EngineWave || EngineWave->GetAvailableAudioByteCount() > 4800) return;
    const double EngineGain = Vroom::Settings::Get().EngineVolume;
    const double EffectsGain = Vroom::Settings::Get().EffectsVolume;
    TArray<int16> Samples; Samples.SetNumUninitialized(2400);
    if (AmigaEngine && AmigaEngineN > 0 && AmigaPeriods.Num() > 0)
    {
        // Original: rapport = rpm>>4, table index = rapport>>2. High revs set
        // OCT and mix mot2 (decimated loop) so the engine gets sharper, not just louder.
        const int32 Index = FMath::Clamp(int32(Drive.State.Rpm) >> 6, 0, AmigaPeriods.Num() - 1);
        const double Period = FMath::Max(80.0, double(AmigaPeriods[Index]));
        const double EngineRate = (3546895.0 / Period) / 48000.0;
        const double Bright = FMath::Clamp((Index - 72) / 90.0, 0.0, 1.0);
        const double ScreechRate = (3546895.0 / 280.0) / 48000.0;
        const double CrashRate = (3546895.0 / 220.0) / 48000.0;
        auto At = [](const int8* Loop, int32 N, double Pos)
        {
            const int32 I0 = ((int32)Pos % N + N) % N;
            const int32 I1 = (I0 + 1) % N;
            const double F = Pos - std::floor(Pos);
            return Loop[I0] * (1.0 - F) + Loop[I1] * F;
        };
        if (ImpactSound > 0.5 && AmigaCrashN > 0) AmigaCrashPos = 0;
        for (int32 I = 0; I < Samples.Num(); ++I)
        {
            AmigaEnginePos = FMath::Fmod(AmigaEnginePos + EngineRate, double(AmigaEngineN));
            double Mix = At(AmigaEngine, AmigaEngineN, AmigaEnginePos) * (52.0 / 64.0) * EngineGain;
            if (Bright > 0.01 && AmigaEngineOctave.Num() > 1)
            {
                AmigaOctavePos = FMath::Fmod(AmigaOctavePos + EngineRate, double(AmigaEngineOctave.Num()));
                Mix += At(AmigaEngineOctave.GetData(), AmigaEngineOctave.Num(), AmigaOctavePos) * Bright * 0.55 * EngineGain;
            }
            if (Drive.Skidding && AmigaScreechN > 0)
            {
                AmigaScreechPos = FMath::Fmod(AmigaScreechPos + ScreechRate, double(AmigaScreechN));
                Mix += At(AmigaScreech, AmigaScreechN, AmigaScreechPos)
                    * std::max(0.45, Drive.SkidAmount) * 1.25 * EffectsGain;
            }
            if (AmigaCrashPos >= 0 && AmigaCrashN > 0)
            {
                Mix += At(AmigaCrash, AmigaCrashN, AmigaCrashPos) * 0.9 * EffectsGain;
                AmigaCrashPos += CrashRate;
                if (AmigaCrashPos >= AmigaCrashN) AmigaCrashPos = -1;
            }
            ImpactSound *= .99965;
            Samples[I] = static_cast<int16>(FMath::Clamp(Mix * 200.0, -32000.0, 32000.0));
        }
    }
    else
    {
        const double Frequency = FMath::Clamp(Drive.State.Rpm / 60.0 * 0.5, 36.0, 190.0);
        for (int32 I = 0; I < Samples.Num(); ++I)
        {
            AudioFrequency += (Frequency - AudioFrequency) * .0012;
            AudioPhase += AudioFrequency / 48000 * UE_TWO_PI;
            AudioPhase = std::fmod(AudioPhase, UE_TWO_PI);
            const double Pulse = std::sin(AudioPhase) * .5 + std::sin(AudioPhase * 2) * .18;
            Samples[I] = static_cast<int16>(FMath::Clamp(Pulse * 11000.0 * EngineGain, -32000.0, 32000.0));
        }
    }
    EngineWave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
}
