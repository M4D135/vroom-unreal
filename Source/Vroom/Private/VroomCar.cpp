#include "VroomGame.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "HAL/PlatformMisc.h"

AVroomCar::AVroomCar()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CarRoot"));
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("CockpitCamera"));
    Camera->SetupAttachment(RootComponent);
    Camera->SetRelativeLocation(FVector(0,0,108));
    Camera->SetRelativeRotation(FRotator(-2,0,0));
    Camera->SetFieldOfView(80);
    EngineAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineAudio"));
    EngineAudio->SetupAttachment(RootComponent);
    EngineAudio->bAutoActivate = false;
    AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void AVroomCar::BeginPlay()
{
    Super::BeginPlay();
    AutoDemo = FParse::Param(FCommandLine::Get(), TEXT("VroomAutoDemo"));
    ChaseCamera = FParse::Param(FCommandLine::Get(), TEXT("VroomChaseCamera"));
    FParse::Value(FCommandLine::Get(), TEXT("VroomCapture="), CapturePath);
    FParse::Value(FCommandLine::Get(), TEXT("VroomCaptureAfter="), CaptureAfter);
    FParse::Value(FCommandLine::Get(), TEXT("VroomExitAfter="), ExitAfter);
    auto Part = [&](const TCHAR* Mesh, const TCHAR* Material, FVector P, FVector Scale, FRotator R = FRotator::ZeroRotator)
    {
        auto C = NewObject<UStaticMeshComponent>(this);
        C->SetupAttachment(RootComponent);
        C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"),Mesh,Mesh)));
        C->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/Materials/%s.%s"),Material,Material)));
        C->SetRelativeTransform(FTransform(R,P,Scale));
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->RegisterComponent();
        BodyParts.Add(C);
    };
    Part(TEXT("Cube"),TEXT("M_Red"),FVector(60,0,48),FVector(2.7,.65,.3));
    Part(TEXT("Cube"),TEXT("M_White"),FVector(160,0,36),FVector(1.5,.35,.25));
    Part(TEXT("Cube"),TEXT("M_Red"),FVector(205,0,24),FVector(.32,1.98,.12));
    Part(TEXT("Cube"),TEXT("M_White"),FVector(-155,0,95),FVector(.4,1.98,.12));
    Part(TEXT("Cube"),TEXT("M_Dark"),FVector(-158,0,61),FVector(.15,.18,.7));
    for (float Side : {-1.f,1.f})
    {
        Part(TEXT("Cube"),TEXT("M_Red"),FVector(-35,Side*51,47),FVector(1.9,.5,.44));
        Part(TEXT("Cube"),TEXT("M_Black"),FVector(25,Side*55,54),FVector(.4,.38,.2));
        Part(TEXT("Cylinder"),TEXT("M_Dark"),FVector(128,Side*91,35),FVector(.7,.7,.38),FRotator(0,0,90));
        Part(TEXT("Cylinder"),TEXT("M_Dark"),FVector(-115,Side*96,38),FVector(.76,.76,.46),FRotator(0,0,90));
        Part(TEXT("Cube"),TEXT("M_Metal"),FVector(115,Side*58,33),FVector(.12,.65,.05));
    }
    Part(TEXT("Cube"),TEXT("M_Black"),FVector(39,0,74),FVector(.12,.55,.26));
    EngineWave = NewObject<USoundWaveProcedural>(this);
    EngineWave->SetSampleRate(48000);
    EngineWave->NumChannels = 1;
    EngineWave->Duration = INDEFINITELY_LOOPING_DURATION;
    EngineWave->SoundGroup = SOUNDGROUP_Voice;
    EngineAudio->SetSound(EngineWave);
    EngineAudio->SetVolumeMultiplier(.16f);
    UpdateSound();
    EngineAudio->Play();
    if (auto* PC = Cast<APlayerController>(GetController()))
    {
        PC->bShowMouseCursor = false;
        PC->SetInputMode(FInputModeGameOnly());
    }
    UE_LOG(LogTemp, Display, TEXT("VROOM: Player ready, demo=%d"), AutoDemo);
}

void AVroomCar::ResetCar()
{
    Drive.Reset();
    Accumulator = 0;
    PendingShiftUp = PendingShiftDown = false;
    Lap = 1; LapStart = 0; LastLap = 0; LapClean = true;
    MouseTarget = 0;
    if (Circuit) Circuit->RaceClock = 0;
}

void AVroomCar::Tick(float Dt)
{
    Super::Tick(Dt);
    if (!Circuit)
    {
        TActorIterator<AVroomCircuit> It(GetWorld());
        if (It) { Circuit = *It; AddTickPrerequisiteActor(Circuit); }
    }
    if (!Circuit || !Circuit->CurrentTrack()) return;
    auto* PC = Cast<APlayerController>(GetController());
    if (!PC) return;
    const FKey TrackKeys[] = {EKeys::F1,EKeys::F2,EKeys::F3,EKeys::F4,EKeys::F5,EKeys::F6};
    for (int32 I = 0; I < 6; ++I)
    {
        if (PC->WasInputKeyJustPressed(TrackKeys[I]))
        {
            Circuit->Running = false; Circuit->Paused = false;
            Circuit->SelectTrack(I); ResetCar(); BestLap = 0;
        }
    }
    if (PC->WasInputKeyJustPressed(EKeys::Enter))
    {
        ResetCar(); Circuit->SelectTrack(Circuit->TrackIndex);
        Circuit->Running = true; Circuit->Paused = false; Circuit->RaceClock = -3;
    }
    if (AutoDemo && !Circuit->Running) { Circuit->Running = true; Circuit->RaceClock = 0; }
    if (PC->WasInputKeyJustPressed(EKeys::Escape)) { Circuit->Running = false; Circuit->Paused = false; }
    if (PC->WasInputKeyJustPressed(EKeys::P)) Circuit->Paused = !Circuit->Paused;
    if (PC->WasInputKeyJustPressed(EKeys::R)) { ResetCar(); Circuit->SelectTrack(Circuit->TrackIndex); }
    if (PC->WasInputKeyJustPressed(EKeys::G)) Drive.Automatic = !Drive.Automatic;
    if (PC->WasInputKeyJustPressed(EKeys::C)) ChaseCamera = !ChaseCamera;
    if (PC->WasInputKeyJustPressed(EKeys::M)) { MouseSteer = !MouseSteer; MouseTarget = 0; }
    if (PC->WasInputKeyJustPressed(EKeys::F10)) FovIndex = (FovIndex + 1) % 3;
    Vroom::DriveInput Input;
    Input.Throttle = (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Z) || PC->IsInputKeyDown(EKeys::Up)) ? 1 : PC->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis);
    Input.Brake = (PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down) || PC->IsInputKeyDown(EKeys::SpaceBar)) ? 1 : PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis);
    float Steering = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
    if (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Q) || PC->IsInputKeyDown(EKeys::Left)) Steering -= 1;
    if (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right)) Steering += 1;
    if (MouseSteer)
    {
        float X, Y; PC->GetInputMouseDelta(X,Y);
        MouseTarget = FMath::Clamp(MouseTarget + X * .004f, -1.f, 1.f);
        Steering += MouseTarget;
    }
    Input.Steer = FMath::Clamp(Steering,-1.f,1.f);
    PendingShiftUp |= PC->WasInputKeyJustPressed(EKeys::E);
    PendingShiftDown |= PC->WasInputKeyJustPressed(EKeys::X);
    Input.ShiftUp = PendingShiftUp;
    Input.ShiftDown = PendingShiftDown;
    if (AutoDemo)
    {
        const double S = Drive.State.DistanceM;
        const double K = Circuit->Curvature(S + 12);
        const double Desired = FMath::Clamp(FMath::Sqrt(13.0 / FMath::Max(std::abs(K), .001)), 12.0, 62.0);
        Input.Throttle = Drive.State.SpeedMps < Desired ? .85 : 0;
        Input.Brake = Drive.State.SpeedMps > Desired + 3 ? .6 : 0;
        const double SteeringAngle = .38 / (1 + Drive.State.SpeedMps / 22);
        const double FeedForward = std::atan(2.75 * K) / SteeringAngle;
        Input.Steer = FMath::Clamp(FeedForward - Drive.State.HeadingRad * 2.3 - Drive.State.LateralM * .08, -.8, .8);
    }
    if (Circuit->Running && !Circuit->Paused && Circuit->RaceClock >= 0)
    {
        Accumulator += FMath::Min(Dt,.1f);
        while (Accumulator >= 1.0 / 120)
        {
            Drive.Step(Input, 1.0 / 120, Circuit->Curvature(Drive.State.DistanceM), Circuit->HalfWidthM);
            Input.ShiftUp = false; Input.ShiftDown = false;
            PendingShiftUp = PendingShiftDown = false;
            Accumulator -= 1.0 / 120;
        }
        if (std::abs(Drive.State.LateralM) > Circuit->HalfWidthM) LapClean = false;
        if (std::abs(Drive.State.LateralM) > 9)
        {
            Drive.State.LateralM = FMath::Clamp(Drive.State.LateralM,-9.0,9.0);
            const double CollisionDt = FMath::Min(Dt, .1f);
            Drive.State.HeadingRad *= std::pow(.8, CollisionDt * 60);
            Drive.State.SpeedMps *= std::pow(.4, CollisionDt);
        }
        const int32 NewLap = 1 + FMath::FloorToInt(Drive.State.DistanceM / Circuit->LengthM());
        if (NewLap > Lap)
        {
            LastLap = Circuit->RaceClock - LapStart;
            if (LapClean && (BestLap <= 0 || LastLap < BestLap)) BestLap = LastLap;
            LapStart = Circuit->RaceClock; Lap = NewLap; LapClean = true;
            UE_LOG(LogTemp, Display, TEXT("VROOM: Lap %d time %.3f"), Lap - 1, LastLap);
        }
        for (int32 I = 0; I < Circuit->OpponentS.Num(); ++I)
        {
            const double Len = Circuit->LengthM();
            double DS = std::fmod(Circuit->OpponentS[I] - Drive.State.DistanceM + Len * 100, Len);
            if (DS > Len / 2) DS -= Len;
            if (std::abs(DS) < 3.8 && std::abs(Circuit->OpponentLanes[I] - Drive.State.LateralM) < 1.7)
            {
                Drive.State.SpeedMps = FMath::Min(Drive.State.SpeedMps, Circuit->OpponentSpeeds[I] * .85);
                LapClean = false;
            }
        }
    }
    const FRotator TrackRot = Circuit->Rotation(Drive.State.DistanceM);
    FRotator CarRot = TrackRot;
    CarRot.Yaw += FMath::RadiansToDegrees(Drive.State.HeadingRad);
    SetActorLocationAndRotation(Circuit->Position(Drive.State.DistanceM, Drive.State.LateralM) + FVector(0,0,6), CarRot);
    Camera->SetRelativeLocation(ChaseCamera ? FVector(-680,0,310) : FVector(0,0,108));
    Camera->SetRelativeRotation(ChaseCamera ? FRotator(-15,0,0) : FRotator(-2,0,Drive.State.Steering * -.5));
    const float Fovs[] = {80,68,92};
    Camera->SetFieldOfView(Fovs[FovIndex]);
    EngineAudio->SetVolumeMultiplier(Circuit->Paused ? 0 : (Circuit->Running ? .16f : .04f));
    UpdateSound();
    PreviewClock += Dt;
    if (!CapturePath.IsEmpty() && PreviewClock >= CaptureAfter)
    {
        FScreenshotRequest::RequestScreenshot(CapturePath, true, false);
        UE_LOG(LogTemp, Display, TEXT("VROOM: Capture requested %s speed=%.1f km/h s=%.1f lateral=%.2f"), *CapturePath, Drive.State.SpeedMps * 3.6, Drive.State.DistanceM, Drive.State.LateralM);
        CapturePath.Empty();
    }
    if (ExitAfter > 0 && PreviewClock >= ExitAfter) FPlatformMisc::RequestExit(false);
}

void AVroomCar::UpdateSound()
{
    if (!EngineWave || EngineWave->GetAvailableAudioByteCount() > 4800) return;
    TArray<int16> Samples; Samples.SetNumUninitialized(2400);
    const double Frequency = FMath::Clamp(Drive.State.Rpm / 60 * 4, 65.0, 850.0);
    for (int32 I = 0; I < Samples.Num(); ++I)
    {
        AudioPhase += Frequency / 48000 * UE_TWO_PI;
        AudioPhase = std::fmod(AudioPhase, UE_TWO_PI);
        AudioClock += 1.0 / 48000;
        const double Pulse = std::sin(AudioPhase) * .5 + std::sin(AudioPhase * 2.01) * .22 + std::sin(AudioPhase * 3) * .15 + std::sin(AudioPhase * .5) * .13;
        Samples[I] = static_cast<int16>(Pulse * (18000 + 1000 * std::sin(AudioClock * 37)));
    }
    EngineWave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()),Samples.Num() * sizeof(int16));
}
