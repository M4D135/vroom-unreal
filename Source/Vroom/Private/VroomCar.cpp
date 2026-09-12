#include "VroomGame.h"
#include "VroomGraphicsSettings.h"
#include "VroomOvertake.h"
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

namespace
{
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
    static float Value = 0.34f;
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
    static float Value = 24.f;
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
    Camera->SetRelativeLocation(FVector(0,0,170));
    Camera->SetRelativeRotation(FRotator(-2,0,0));
    Camera->SetFieldOfView(80);
    // Preserve the vertical field used by the original camera setup when the
    // 16:10 Mac viewport changes to a 16:9 4K viewport.
    Camera->SetAspectRatio(16.f / 9.f);
    Camera->bOverrideAspectRatioAxisConstraint = true;
    Camera->SetAspectRatioAxisConstraint(AspectRatio_MaintainYFOV);
    EngineAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineAudio"));
    EngineAudio->SetupAttachment(RootComponent);
    EngineAudio->bAutoActivate = false;
    AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void AVroomCar::BeginPlay()
{
    Super::BeginPlay();
    AutoDemo = FParse::Param(FCommandLine::Get(), TEXT("VroomAutoDemo"));
    FParse::Value(FCommandLine::Get(), TEXT("VroomCollisionTest="), TestImpact);
    AutoDemo |= !TestImpact.IsEmpty();
    TickDebug = FParse::Param(FCommandLine::Get(), TEXT("VroomTickDebug"));
    MotionTrace = FParse::Param(FCommandLine::Get(), TEXT("VroomMotionTrace"));
    VerificationMode = FParse::Param(FCommandLine::Get(), TEXT("VroomVerify"));
    ChaseCamera = FParse::Param(FCommandLine::Get(), TEXT("VroomChaseCamera"));
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
    auto Part = [&](const TCHAR* Mesh, const TCHAR* Material, FVector P, bool Tyre = false)
    {
        auto C = NewObject<UStaticMeshComponent>(this);
        C->SetupAttachment(RootComponent);
        C->SetMobility(EComponentMobility::Movable);
        C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Meshes/%s.%s"),Mesh,Mesh)));
        C->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/Materials/%s.%s"),Material,Material)));
        C->SetRelativeLocation(P);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCanEverAffectNavigation(false);
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
    if (Circuit && FParse::Param(FCommandLine::Get(), TEXT("VroomGraphicsPreview")))
    {
        GraphicsPauseBefore = Circuit->Paused;
        Vroom::Graphics::Toggle();
        Circuit->Paused = true;
        if (auto* PC = Cast<APlayerController>(GetController()))
        {
            FInputModeGameAndUI Mode;
            Mode.SetHideCursorDuringCapture(false);
            Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            PC->SetInputMode(Mode);
            PC->bShowMouseCursor = true;
        }
    }
    if (Circuit && Circuit->CurrentTrack()) UpdatePresentation(1);
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
    Drive.Reset();
    Contacts.Reset();
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
    Lap = 1; LapStart = 0; LastLap = 0; LapClean = true;
    CarsPassed = 0; Score = 0; ScoreFrac = 0;
    OvertakeSides.Empty();
    MouseTarget = 0;
    if (Circuit) Circuit->RaceClock = 0;
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
    if (ClickedOptions || PC->WasInputKeyJustPressed(EKeys::O) || PC->WasInputKeyJustPressed(EKeys::F7))
    {
        if (!WasOpen) GraphicsPauseBefore = Circuit->Paused;
        Toggle();
    }
    if (IsOpen())
    {
        Circuit->Paused = true;
        if (PC->WasInputKeyJustPressed(EKeys::Escape)) Cancel();
        else if (PC->WasInputKeyJustPressed(EKeys::Enter)) Apply();
        else
        {
            if (PC->WasInputKeyJustPressed(EKeys::Up)) SelectRow((SelectedRow() + RowCount() - 1) % RowCount());
            if (PC->WasInputKeyJustPressed(EKeys::Down)) SelectRow((SelectedRow() + 1) % RowCount());
            if (PC->WasInputKeyJustPressed(EKeys::Left)) Adjust(-1);
            if (PC->WasInputKeyJustPressed(EKeys::Right)) Adjust(1);
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
    if (WasOpen && !Open) Circuit->Paused = GraphicsPauseBefore;
    PC->bShowMouseCursor = Open || !Circuit->Running;
    if (WasOpen != Open)
    {
        if (Open)
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
    auto* PC = Cast<APlayerController>(GetController());
    if (!PC) return;
    auto Pressed = [&](const FKey& Key) { return !VerificationMode && PC->WasInputKeyJustPressed(Key); };
    Circuit->UpdateVegetation();
    if (HandleGraphicsInput(PC))
    {
        UpdatePresentation(Circuit->Running ? FrameTiming.Alpha() : 1);
        EngineAudio->SetVolumeMultiplier(0);
        UpdateDiagnostics(Dt);
        return;
    }
    if (!TestImpact.IsEmpty() && !TestPrepared) PrepareCollisionTest();
    const FKey TrackKeys[] = {EKeys::F1,EKeys::F2,EKeys::F3,EKeys::F4,EKeys::F5,EKeys::F6};
    for (int32 I = 0; I < 6; ++I)
    {
        if (Pressed(TrackKeys[I]))
        {
            Circuit->Running = false; Circuit->Paused = false;
            Circuit->SelectTrack(I); ResetCar(); BestLap = 0;
        }
    }
    if (Pressed(EKeys::Enter))
    {
        ResetCar(); Circuit->SelectTrack(Circuit->TrackIndex);
        Circuit->Running = true; Circuit->Paused = false; Circuit->RaceClock = -3;
    }
    if (AutoDemo && !Circuit->Running)
    {
        Circuit->Running = true;
        Circuit->RaceClock = FParse::Param(FCommandLine::Get(), TEXT("VroomPreviewCountdown")) ? -3 : 0;
    }
    if (Pressed(EKeys::Escape)) { Circuit->Running = false; Circuit->Paused = false; }
    if (Pressed(EKeys::P)) Circuit->Paused = !Circuit->Paused;
    if (Pressed(EKeys::R)) { ResetCar(); Circuit->SelectTrack(Circuit->TrackIndex); }
    if (Pressed(EKeys::G)) Drive.Automatic = !Drive.Automatic;
    {
        const double Len = Circuit->LengthM();
        const double Along = std::fmod(std::fmod(Drive.State.DistanceM, Len) + Len, Len);
        const bool InPits = Circuit->Running && Drive.State.SpeedMps < 12.0
            && std::abs(Drive.State.LateralM) < 8.0
            && (Along < 90.0 || Along > Len - 40.0);
        if (InPits && Pressed(EKeys::T))
        {
            Drive.ChangeTyres();
            UE_LOG(LogTemp, Display, TEXT("VROOM: Pit stop tyres"));
        }
        if (InPits && Pressed(EKeys::F))
        {
            Drive.Refuel();
            UE_LOG(LogTemp, Display, TEXT("VROOM: Pit stop fuel"));
        }
    }
    if (Pressed(EKeys::C)) ChaseCamera = !ChaseCamera;
    if (Pressed(EKeys::M)) { MouseSteer = !MouseSteer; MouseTarget = 0; }
    if (Pressed(EKeys::F10)) FovIndex = (FovIndex + 1) % 3;
    Vroom::DriveInput Input;
    Input.Throttle = (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Z) || PC->IsInputKeyDown(EKeys::Up)) ? 1 : PC->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis);
    Input.Brake = (PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down) || PC->IsInputKeyDown(EKeys::SpaceBar)) ? 1 : PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis);
    const float AnalogSteer = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
    const float Deadzone = VroomSteeringInputDeadzone();
    const float AnalogSteerClean = Vroom::SteeringDeadzone(AnalogSteer, Deadzone);
    const float DigitalSteer = (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Q) || PC->IsInputKeyDown(EKeys::Left) ? -1.f : 0.f)
                           + (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right) ? 1.f : 0.f);
    float Steering = DigitalSteer != 0.f ? DigitalSteer : AnalogSteerClean;
    if (MouseSteer)
    {
        float X, Y; PC->GetInputMouseDelta(X,Y);
        MouseTarget = FMath::Clamp(MouseTarget + X * .004f, -1.f, 1.f);
        Steering += MouseTarget;
    }
    // Filter at the simulation rate below; a render-frame clamp changed the
    // steering response whenever the GPU cadence changed in a corner.
    Input.Steer = FMath::Clamp(Steering,-1.f,1.f);
    Input.Drift = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift)
        || PC->IsInputKeyDown(EKeys::Gamepad_LeftShoulder)
        || PC->IsInputKeyDown(EKeys::Gamepad_RightShoulder);
    PendingShiftUp |= Pressed(EKeys::E);
    PendingShiftDown |= Pressed(EKeys::X);
    Input.ShiftUp = PendingShiftUp;
    Input.ShiftDown = PendingShiftDown;
    if (AutoDemo)
    {
        const double S = Drive.State.DistanceM;
        const double K = Circuit->Curvature(S + 12);
        const double Desired = FMath::Clamp(FMath::Sqrt(26.0 / FMath::Max(std::abs(K), .004)), 15.0, 70.0);
        Input.Throttle = Drive.State.SpeedMps < Desired ? .85 : 0;
        Input.Brake = Drive.State.SpeedMps > Desired + 3 ? .6 : 0;
        // Arcade XBCMP3: hold the stick to cancel curve wash and stay near
        // a racing line. No yaw PID — the chassis stays parallel.
        const double SteerGain = std::max(Vroom::DriveModel::ArcadeSteerLatMps(1, Drive.State.SpeedMps), 0.4);
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
    if (!TestImpact.IsEmpty())
    {
        Input = Vroom::DriveInput{};
        InputSteering = 0.f;
    }
    if (AutoDemo && PreviewClock >= NextTrace)
    {
        NextTrace = PreviewClock + 1.0;
        UE_LOG(LogTemp, Display, TEXT("VROOM-TRACE t=%.1f s=%.1f v=%.1f lat=%.2f h=%.3f vis=%.3f yaw=%.1f steer=%.2f K=%.4f skid=%.2f"),
            PreviewClock, Drive.State.DistanceM, Drive.State.SpeedMps, Drive.State.LateralM,
            Drive.State.HeadingRad, Drive.VisualHeading(), GetActorRotation().Yaw,
            Drive.State.Steering, Circuit->Curvature(Drive.State.DistanceM), Drive.SkidAmount);
    }
    if (Circuit->Running && !Circuit->Paused)
    {
        const int32 Steps = FrameTiming.Advance(Dt);
        for (int32 Step = 0; Step < Steps; ++Step)
        {
            SimulateStep(Input, Vroom::FrameTiming::StepSeconds);
            Input.ShiftUp = false; Input.ShiftDown = false;
            PendingShiftUp = PendingShiftDown = false;
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
        const bool Expected = !ChaseCamera && !Blink;
        bool Correct = IsCockpitVisible() == Expected
            && CockpitRoot && CockpitRoot->IsVisible() == Expected
            && MirrorRoot && MirrorRoot->IsVisible() == Expected;
        for (const auto& Mesh : CockpitMeshes) Correct &= Mesh && Mesh->IsVisible() == Expected;
        for (const auto& Mesh : MirrorMeshes) Correct &= Mesh && Mesh->IsVisible() == Expected;
        if (!Correct) ++TestVisibilityErrors;
    }
    Circuit->RenderOpponents(Alpha);
    EngineAudio->SetVolumeMultiplier(Circuit->Paused ? 0 : (Circuit->Running ? .22f : .05f));
    UpdateSound();
    UpdateDiagnostics(Dt);
}

void AVroomCar::UpdateDiagnostics(float Dt)
{
    PreviewClock += Dt;
    const double CheckClock = TestImpact.IsEmpty() ? PreviewClock : TestSeconds;
    if (!CapturePath.IsEmpty() && CheckClock >= CaptureAfter)
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
        const bool Passed = TestImpact.IsEmpty() || (TestContacts == 1 && TestVisibilityErrors == 0 &&
            ((TestImpact == TEXT("slow") && Contacts.State.LastKind == Vroom::ContactKind::SlowImpact && TestRespawns == 0) ||
            (TestImpact == TEXT("crash") && Contacts.State.LastKind == Vroom::ContactKind::Crash && TestRespawns == 1) ||
            (TestImpact == TEXT("tyre") && Contacts.State.LastKind == Vroom::ContactKind::TyreJump && TestRespawns == 1) ||
            (TestImpact == TEXT("scenery") && Contacts.State.LastKind == Vroom::ContactKind::Crash && TestRespawns == 1
                && !Contacts.State.ControlsLocked && Drive.State.SpeedMps < .1
                && FMath::Abs(Drive.State.LateralM) < Circuit->HalfWidthM)));
        UE_LOG(LogTemp, Display, TEXT("VROOM: Verification %s scenario=%s contacts=%d respawns=%d visibilityErrors=%d"), Passed ? TEXT("PASS") : TEXT("FAIL"), *TestImpact, TestContacts, TestRespawns, TestVisibilityErrors);
        FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 1);
    }
}

void AVroomCar::SimulateStep(const Vroom::DriveInput& RawInput, double H)
{
    PreviousDrive = Drive.State;
    PreviousVisualHeading = Contacts.State.ControlsLocked ? Drive.State.HeadingRad : Drive.VisualHeading();
    PreviousContact = Contacts.State;
    PreviousHopHeight = Drive.HopHeightM;
    Circuit->AdvanceRace(H);
    TestSeconds += H;
    if (Circuit->RaceClock < 0) return;

    Vroom::DriveInput Input = RawInput;
    if (Contacts.State.ControlsLocked) Input = Vroom::DriveInput{};
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
    else Drive.Step(Input, H, Circuit->Curvature(Drive.State.DistanceM), Half,
        Airborne ? 0 : Circuit->Elevation(Drive.State.DistanceM).Grade, Airborne);
    Contacts.Step(H);

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
    if (!Contacts.State.ControlsLocked && Contacts.State.InvulnerabilityRemaining <= 0)
    {
        const FVector Start = Circuit->Position(PreviousDrive.DistanceM, PreviousDrive.LateralM)
            + FVector(0,0,66 + (PreviousContact.HeightM + PreviousHopHeight) * 100);
        const FVector End = Circuit->Position(Drive.State.DistanceM, Drive.State.LateralM)
            + FVector(0,0,66 + (Contacts.State.HeightM + Drive.HopHeightM) * 100);
        FRotator CarRotation = Circuit->DrivingRotation(Drive.State.DistanceM);
        CarRotation.Yaw += FMath::RadiansToDegrees(Drive.VisualHeading());
        double Fraction = 1;
        FVector Normal;
        if (FVector::DistSquared(Start,End) > 0.01 && Circuit->SweepScenery(Start, End, CarRotation.Quaternion(), Fraction, Normal))
        {
            const double RelativeSpeed = FVector::Dist(Start,End) / (100 * H);
            const double SideDot = FVector::DotProduct(Normal, CarRotation.Quaternion().GetRightVector());
            const int Side = FMath::Abs(SideDot) > .1 ? (SideDot > 0 ? -1 : 1) : (Drive.State.LateralM >= 0 ? 1 : -1);
            const auto Hit = Contacts.ForceObjectCrash(RelativeSpeed, Side);
            if (Hit.Kind != Vroom::ContactKind::None)
            {
                Drive.State.DistanceM = FMath::Lerp(PreviousDrive.DistanceM, Drive.State.DistanceM, Fraction);
                Drive.State.LateralM = FMath::Lerp(PreviousDrive.LateralM, Drive.State.LateralM, Fraction);
                Drive.State.SpeedMps = 0;
                Drive.State.Rpm = 1000;
                Drive.State.HeadingRad = Drive.VisualHeading();
                Drive.CancelArcadeEffects();
                Drive.ExtraLatVel = 0;
                Drive.SetVelocityHeading(Drive.State.HeadingRad);
                Contacts.PrepareRoadRecovery(Drive.State.LateralM, Half);
                Drive.TyreHealth = FMath::Max(0.0, Drive.TyreHealth - (Contacts.State.TyresBroken ? 180 : 40));
                LapClean = false;
                ImpactSound = Contacts.State.TyresBroken ? 1 : .4;
                ++TestContacts;
                UE_LOG(LogTemp, Display, TEXT("VROOM: Scenery impact speed=%.1f km/h s=%.2f lateral=%.2f broken=%d"),
                    Hit.RelativeSpeedMps * 3.6, Drive.State.DistanceM, Drive.State.LateralM, Contacts.State.TyresBroken);
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

    if (std::abs(Drive.State.LateralM) > Circuit->HalfWidthM) LapClean = false;
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
        Q.PreviousLateralGapM = Circuit->OpponentLanes[I] - PreviousDrive.LateralM;
        Q.PlayerSpeedMps = Drive.State.SpeedMps; Q.OpponentSpeedMps = Circuit->OpponentSpeeds[I];
        const auto Hit = Contacts.TryContact(Q);
        if (Hit.Kind == Vroom::ContactKind::None) continue;
        Drive.State.SpeedMps = Hit.SpeedAfterMps;
        if (Hit.Kind == Vroom::ContactKind::Crash)
        {
            Drive.State.HeadingRad = Drive.VisualHeading();
            Drive.CancelArcadeEffects();
            Drive.SetVelocityHeading(std::atan2(Hit.LateralShoveMps, std::max(Drive.State.SpeedMps, 1.0)));
            Contacts.PrepareRoadRecovery(Drive.State.LateralM, Half);
        }
        else if (std::abs(Hit.LateralShoveMps) > 0.05)
            Drive.Shove(Hit.LateralShoveMps);
        LapClean = false; ++TestContacts;
        ImpactSound = Hit.Kind == Vroom::ContactKind::SlowImpact ? .35 : 1;
        UE_LOG(LogTemp, Display, TEXT("VROOM: Contact kind=%d relative=%.2f km/h speedAfter=%.2f km/h"), static_cast<int32>(Hit.Kind), Hit.RelativeSpeedMps * 3.6, Hit.SpeedAfterMps * 3.6);
    }
    const int32 NewLap = 1 + FMath::FloorToInt(Drive.State.DistanceM / Len);
    if (NewLap > Lap)
    {
        LastLap = Circuit->RaceClock - LapStart;
        if (LapClean && (BestLap <= 0 || LastLap < BestLap)) BestLap = LastLap;
        LapStart = Circuit->RaceClock; Lap = NewLap; LapClean = !Contacts.State.ControlsLocked;
        UE_LOG(LogTemp, Display, TEXT("VROOM: Lap %d time %.3f"), Lap - 1, LastLap);
    }
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
    // Leave the road horizon beneath the Amiga information band in both views.
    Camera->SetRelativeLocation(ChaseCamera ? FVector(-850,0,310) : FVector(0,0,170));
    const float Fovs[] = {80,68,92}; Camera->SetFieldOfView(Fovs[FovIndex]);
    VisualSteering = FMath::Clamp(static_cast<float>(FMath::Lerp(PreviousDrive.Steering, Drive.State.Steering, Alpha)), -1.f, 1.f);
    const float SmoothedSteering = UpdateCockpit(VisualSteering);
    static const bool InspectCockpit = FParse::Param(FCommandLine::Get(), TEXT("VroomInspectCockpit"));
    const FRotator TargetCamRot = ChaseCamera ? FRotator(-6,0,0) : FRotator(InspectCockpit ? -20 : -2,0,SmoothedSteering * CockpitCameraSteeringYaw());
    const double CamDt = Circuit->Paused ? 0.0 : FMath::Clamp(static_cast<double>(GetWorld()->GetDeltaSeconds()), 0.0, .1);
    Camera->SetRelativeRotation(FQuat::Slerp(Camera->GetRelativeRotation().Quaternion(), TargetCamRot.Quaternion(),
        Vroom::ExponentialAlpha(110, CamDt)));

    const bool Broken = Contacts.State.Phase == Vroom::ContactPhase::Crashed || Contacts.State.Phase == Vroom::ContactPhase::Recovering;
    for (int32 I : TyrePartIndices)
    {
        FTransform Pose = BodyBaseTransforms[I];
        // The Amiga view separates the small front wheels from the low cockpit.
        // Keep the external model intact and stage this view to its composition.
        if (!ChaseCamera && Pose.GetLocation().X > 0)
        {
            FVector P = Pose.GetLocation(); P.X = 270; P.Z = 29;
            Pose.SetLocation(P);
            Pose.SetScale3D(Pose.GetScale3D() * .82);
        }
        FRotator Wheel = Pose.Rotator();
        if (!Broken && Pose.GetLocation().X > 0) Wheel.Yaw += FMath::RadiansToDegrees(SmoothedSteering * CockpitWheelSteeringScale() / (1 + Drive.State.SpeedMps / CockpitWheelSteeringDamping()));
        Pose.SetRotation(Wheel.Quaternion()); BodyParts[I]->SetRelativeTransform(Pose);
    }
    FTransform Nose = BodyBaseTransforms[2];
    if (Broken) Nose.SetRotation(FRotator(-18, 9, 12).Quaternion());
    BodyParts[2]->SetRelativeTransform(Nose);
    const bool Blink = Contacts.State.InvulnerabilityRemaining > 0 && FMath::Fmod(Contacts.State.InvulnerabilityRemaining, .2) < .08;
    for (int32 I = 0; I < BodyParts.Num(); ++I)
    {
        const bool FrontTyre = TyrePartIndices.Contains(I) && BodyBaseTransforms[I].GetLocation().X > 0;
        BodyParts[I]->SetVisibility(!Blink && (ChaseCamera || FrontTyre));
    }
}

void AVroomCar::PrepareCollisionTest()
{
    ResetCar(); TestPrepared = true;
    Circuit->Running = true; Circuit->Paused = false;
    Drive.State.DistanceM = 100; Drive.State.SpeedMps = TestImpact == TEXT("slow") ? 20 : (TestImpact == TEXT("crash") ? 80 : 55);
    Drive.State.LateralM = 0; Drive.State.HeadingRad = 0; Drive.State.Steering = 0;
    Drive.State.Gear = TestImpact == TEXT("slow") ? 2 : 5;
    for (int32 I = 0; I < Circuit->OpponentS.Num(); ++I) Circuit->OpponentS[I] = 1000 + I * 100;
    Circuit->OpponentS[0] = 104.4;
    Circuit->OpponentLanes[0] = TestImpact == TEXT("tyre") ? 1.55 : 0;
    Circuit->OpponentSpeeds[0] = TestImpact == TEXT("slow") ? 15 : (TestImpact == TEXT("crash") ? 40 : 10);
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
            double Mix = At(AmigaEngine, AmigaEngineN, AmigaEnginePos) * (52.0 / 64.0);
            if (Bright > 0.01 && AmigaEngineOctave.Num() > 1)
            {
                AmigaOctavePos = FMath::Fmod(AmigaOctavePos + EngineRate, double(AmigaEngineOctave.Num()));
                Mix += At(AmigaEngineOctave.GetData(), AmigaEngineOctave.Num(), AmigaOctavePos) * Bright * 0.55;
            }
            if (Drive.Skidding && AmigaScreechN > 0)
            {
                AmigaScreechPos = FMath::Fmod(AmigaScreechPos + ScreechRate, double(AmigaScreechN));
                Mix += At(AmigaScreech, AmigaScreechN, AmigaScreechPos)
                    * std::max(0.45, Drive.SkidAmount) * 1.25;
            }
            if (AmigaCrashPos >= 0 && AmigaCrashN > 0)
            {
                Mix += At(AmigaCrash, AmigaCrashN, AmigaCrashPos) * 0.9;
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
            Samples[I] = static_cast<int16>(FMath::Clamp(Pulse * 11000.0, -32000.0, 32000.0));
        }
    }
    EngineWave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
}
