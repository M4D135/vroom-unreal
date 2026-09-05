#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/HUD.h"
#include "GameFramework/GameModeBase.h"
#include "VroomDriveModel.h"
#include "VroomGame.generated.h"

class USplineComponent;
class UProceduralMeshComponent;
class UCameraComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class USoundWaveProcedural;
class UAudioComponent;

struct FVroomTrack
{
    FString Name;
    int32 Id = 0;
    float SourceLengthM = 0;
    TArray<FVector> Points;
};

UCLASS()
class VROOM_API AVroomCircuit : public AActor
{
    GENERATED_BODY()
public:
    AVroomCircuit();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    void SelectTrack(int32 Index);
    FVector Position(float DistanceM, float LateralM = 0) const;
    FRotator Rotation(float DistanceM) const;
    float Curvature(float DistanceM) const;
    float LengthM() const;
    const FVroomTrack* CurrentTrack() const;
    UPROPERTY() TObjectPtr<USplineComponent> Spline;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Surface;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> CarParts;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Scenery;
    TArray<FVroomTrack> Tracks;
    int32 TrackIndex = 0;
    double RaceClock = 0;
    bool Running = false;
    bool Paused = false;
    float HalfWidthM = 6;
    TArray<double> OpponentS;
    TArray<double> OpponentLanes;
    TArray<double> OpponentSpeeds;
private:
    bool LoadTracks();
    void BuildSurface();
    void BuildScenery();
    void BuildOpponents();
};

UCLASS()
class VROOM_API AVroomCar : public APawn
{
    GENERATED_BODY()
public:
    AVroomCar();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    void ResetCar();
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<AVroomCircuit> Circuit;
    UPROPERTY() TObjectPtr<USoundWaveProcedural> EngineWave;
    UPROPERTY() TObjectPtr<UAudioComponent> EngineAudio;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BodyParts;
    Vroom::DriveModel Drive;
    bool MouseSteer = false;
    bool ChaseCamera = false;
    bool AutoDemo = false;
    float MouseTarget = 0;
    int32 FovIndex = 0;
    int32 Lap = 1;
    double LapStart = 0;
    double BestLap = 0;
    double LastLap = 0;
    bool LapClean = true;
private:
    double Accumulator = 0;
    double AudioPhase = 0;
    double AudioClock = 0;
    bool PendingShiftUp = false;
    bool PendingShiftDown = false;
    double PreviewClock = 0;
    float CaptureAfter = 6;
    float ExitAfter = 0;
    FString CapturePath;
    void UpdateSound();
};

UCLASS()
class VROOM_API AVroomHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
private:
    void Label(const FString& S, float X, float Y, float Size, FLinearColor Color);
};

UCLASS()
class VROOM_API AVroomGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AVroomGameMode();
    virtual void BeginPlay() override;
};
