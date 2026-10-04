#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

// Version affichee dans le HUD et le journal : le seul temoin de build fiable.
namespace Vroom
{
inline constexpr const TCHAR* AppVersion = TEXT("0.35");
}
#include "GameFramework/Pawn.h"
#include "GameFramework/HUD.h"
#include "GameFramework/GameModeBase.h"
#include "VroomDriveModel.h"
#include "VroomContactModel.h"
#include "VroomElevationProfile.h"
#include "VroomFrameTiming.h"
#include "VroomRaceRules.h"
#include "VroomSession.h"
#include "VroomTrafficModel.h"
#include "VroomWheelVisuals.h"
#include "VroomGame.generated.h"

namespace Vroom { enum class Action : uint8; }

class USplineComponent;
class UProceduralMeshComponent;
class UCameraComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class USceneComponent;
class USoundWaveProcedural;
class UAudioComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UTexture2D;
class UMaterialInstanceDynamic;
class FRenderCommandFence;

struct FVroomTrack
{
    FString Name;
    FString CircuitName;
    int32 Id = 0;
    float SourceLengthM = 0;
    TArray<FVector> Points;
    Vroom::ElevationProfile Elevation;
};

struct FVroomHazard
{
    FTransform Transform;
    FVector HalfExtent = FVector::ZeroVector;
    FBox Bounds;
};

UCLASS()
class VROOM_API AVroomCircuit : public AActor
{
    GENERATED_BODY()
public:
    AVroomCircuit();
    virtual void BeginPlay() override;
    void AdvanceRace(float StepSeconds);
    void RenderOpponents(float Alpha);
    void UpdateVegetation();
    void SelectTrack(int32 Index);
    FVector Position(float DistanceM, float LateralM = 0) const;
    FRotator Rotation(float DistanceM) const;
    FRotator DrivingRotation(float DistanceM) const;
    float Curvature(float DistanceM) const;
    Vroom::ElevationPose Elevation(float DistanceM) const;
    float LengthM() const;
    float TerrainHeightAt(const FVector& Point) const;
    bool VerifyTrackGeometry() const;
    const FVroomTrack* CurrentTrack() const;
    bool SweepScenery(const FVector& PreviousCenter, const FVector& Center, const FQuat& CarRotation,
        double& OutFraction, FVector& OutNormal, bool IgnoreSeparatingInitialContact = false) const;
    UPROPERTY() TObjectPtr<USplineComponent> Spline;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Surface;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Terrain;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> OpponentMeshes;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Scenery;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> StartLights;
    UPROPERTY() TObjectPtr<UAudioComponent> StartLightAudio;
    UPROPERTY() TObjectPtr<USoundWaveProcedural> StartLightWave;
    int32 StartLightStage = -1;
    int32 StartSignalPlayed = -2;
    TArray<FVroomTrack> Tracks;
    int32 TrackIndex = 0;
    double RaceClock = 0;
    bool Running = false;
    bool Paused = false;
    float HalfWidthM = 4.75f;
    TArray<double> OpponentS;
    TArray<double> PreviousOpponentS;
    TArray<double> OpponentLanes;
    TArray<double> PreviousOpponentLanes;
    TArray<double> OpponentSpeeds;
    TArray<double> OpponentFinishTimes;
    double RaceFinishDistance = 0;
    TArray<bool> OpponentRecycled;
    std::vector<Vroom::TrafficCar> Traffic;
    Vroom::TrafficCar TrafficPlayer;
    Vroom::RaceDifficulty Difficulty = Vroom::RaceDifficulty::Normal;
    bool ReactiveTraffic = true, OpponentsVisible = true, ArcadeTraffic = false, TrafficPlayerActive = false;
    void ConfigureTraffic(bool Visible, bool Arcade, Vroom::RaceDifficulty Level);
    void HitOpponent(int32 Index, const Vroom::ContactResult& Hit);
    TArray<FVroomHazard> Hazards;
private:
    TMap<FIntPoint, TArray<int32>> HazardCells;
    int32 VegetationQuality = -1;
    bool VegetationWind = false;
    bool VegetationTreeShadows = false;
    TArray<double> RenderedOpponentS;
    TArray<double> RenderedOpponentLanes;
    TArray<double> OpponentVisualYaw;
    TArray<double> OpponentWheelSteering;
    TArray<Vroom::WheelVisualState> OpponentWheelVisuals;
    TArray<bool> OpponentVisualDiscontinuity;
    static constexpr int32 OpponentMeshGroupCount = 5;
    TArray<FTransform> RenderedOpponentTransforms[OpponentMeshGroupCount];
    // Longueur du spline mise en cache : LengthM() est appelee des dizaines de
    // fois par image (courbure, positions, adversaires, HUD). Evite de
    // retraverser le spline a chaque appel, surtout sensible en virage ou les
    // sous-pas physiques s'accumulent.
    mutable float CachedLengthM = 0;
    TArray<float> CurvatureSamples;
    float CurvatureSampleSpacing = 2.f;
    void RefreshLengthCache();
    void RebuildCurvatureCache();
    TArray<FTransform> NextOpponentTransforms[OpponentMeshGroupCount];
    bool OpponentMotionNeedsReset = false;
    bool LoadTracks();
    void BuildSurface();
    void BuildTerrain();
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
    void BuildCockpit();
    float UpdateCockpit(float Steering);
    bool IsCockpitVisible() const;
    void BuildBanner();
    void UpdateBanner();
    void BuildMirrors();
    void UpdateMirrors(float Dt);
    void StartSession();
    void RestartSession();
    void AdvanceStage();
    void ReturnToMenu();
    void SetSessionMode(Vroom::SessionMode Mode);
    void SetDifficulty(Vroom::RaceDifficulty Difficulty);
    void SelectSessionTrack(int32 Index);
    void HandleAction(Vroom::Action Action);
    double GetLandingFeedbackTime() const { return LandingFeedbackTime; }
    double GetLandingQuality() const { return LandingQuality; }
    // The first rendered frame can precede the first tick that finds the circuit.
    bool PreparingCircuit = true;
    uint32 PendingRenderJobs = 0;
    UPROPERTY() TObjectPtr<USceneComponent> MirrorRoot;
    UPROPERTY() TObjectPtr<USceneCaptureComponent2D> MirrorCapture;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> MirrorTarget;
    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> MirrorMeshes;
    UPROPERTY() TObjectPtr<USceneComponent> CockpitRoot;
    UPROPERTY() TObjectPtr<USceneComponent> SteeringPivot;
    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> CockpitMeshes;
    UPROPERTY() TObjectPtr<USceneComponent> BannerRoot;
    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> BannerMeshes;
    UPROPERTY() TObjectPtr<UTexture2D> BannerMapTexture;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BannerMapMaterial;
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<AVroomCircuit> Circuit;
    UPROPERTY() TObjectPtr<USoundWaveProcedural> EngineWave;
    UPROPERTY() TObjectPtr<UAudioComponent> EngineAudio;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BodyParts;
    Vroom::DriveModel Drive;
    Vroom::ContactModel Contacts;
    Vroom::SessionModel Session;
    Vroom::PitStopModel Pit;
    bool TourDemo = false;
    bool MouseSteer = false;
    bool ChaseCamera = false;
	float VisualSteering = 0;
    bool AutoDemo = false;
    float MouseTarget = 0;
    int32 FovIndex = 0;
    int32 Lap = 1;
    double LapStart = 0;
    double BestLap = 0;
    double LastLap = 0;
    bool LapClean = true;
    bool LastLapClean = true;
    bool LastLapInvalidated = false;
    FString LastLapInvalidReason;
    FString LapInvalidReason;
    int32 ValidLaps = 0;
    Vroom::LapValidator LapState;
    // Arcade banner (vr2.asm CAAPAS / ASCORE): 27 slots, +500 per pass.
    int32 CarsPassed = 0;
    int32 Score = 0;
    double ScoreFrac = 0;
    private:
    Vroom::DriveState PreviousDrive;
    double PreviousVisualHeading = 0;
    double PreviousHopHeight = 0;
    double RecoveryStartHeading = 0;
    float MirrorClock = 0;
    float CockpitSteering = 0.f;
    float InputSteering = 0.f;
    FString BannerTrackKey;
    int32 BannerScoreDrawn = -1;
    int32 BannerCarsDrawn = -1;
    int32 BannerStageDrawn = -1;
    int32 BannerLapDrawn = -1;
    float BannerFuelDrawn = -1;
    Vroom::ContactState PreviousContact;
    TArray<FTransform> BodyBaseTransforms;
    TArray<int8> OvertakeSides;
    TArray<int32> TyrePartIndices;
    Vroom::WheelVisualState PlayerWheelVisual;
    TArray<TObjectPtr<UStaticMeshComponent>> SuspensionBars;
    TArray<FVector> SuspensionChassis;
    TArray<FVector> SuspensionHubs;
    TArray<int32> SuspensionWheelIndex;
    TArray<int8> SuspensionSteers;
    Vroom::FrameTiming FrameTiming;
    double AudioPhase = 0;
    double AudioClock = 0;
    double AudioFrequency = 65;
    double ImpactSound = 0;
    TArray<uint8> AmigaSound;
    TArray<uint16> AmigaPeriods;
    const int8* AmigaEngine = nullptr;
    TArray<int8> AmigaEngineOctave;
    const int8* AmigaScreech = nullptr;
    const int8* AmigaCrash = nullptr;
    const int8* AmigaNoise = nullptr;
    int32 AmigaEngineN = 0, AmigaScreechN = 0, AmigaCrashN = 0, AmigaNoiseN = 0;
    double AmigaEnginePos = 0, AmigaOctavePos = 0, AmigaScreechPos = 0, AmigaCrashPos = -1, AmigaNoisePos = 0;
    void LoadAmigaSound();
    bool PendingShiftUp = false;
    bool PendingShiftDown = false;
    double PreviewClock = 0;
    bool TickDebug = false;
    bool MotionTrace = false;
    bool VerificationMode = false;
    bool GraphicsPauseBefore = false;
    bool ControlsPauseBefore = false;
    bool TrackArgumentApplied = false;
    bool PreparationHoldsPause = false;
    bool PreparationPauseBefore = false;
    int32 PreparationReadyFrames = 0;
    double PreparationStartTime = 0;
    void BeginCircuitPreparation();
    bool UpdateCircuitPreparation();
    double TourStartDistance = 0;
    int32 TickFrame = 0;
    double NextTrace = 0;
    float CaptureAfter = 6;
    float PreviewSteering = 2;
    float ExitAfter = 0;
    FString CapturePath;
    FString TestImpact;
    bool TestPrepared = false;
    int32 OffroadTestSide = 0;
    double OffroadStartDistance = 0;
    bool OffroadReturned = false;
    bool StartTest = false, StartTestStationary = true;
    FString StartTestScenario;
    int32 StartTestPhase = 0;
    double StartTestPauseRemaining = 0, StartTestPauseClock = 0;
    double StartTestDistance = 0;
    TArray<double> StartTestOpponents;
    bool PitTest = false, PitTestStationary = true, PitTestCompleted = false;
    bool TrafficTest = false, TrafficTestCommitted = false, TrafficTestBraked = false;
    bool TrafficTestAlongside = false, TrafficTestClear = true, TrafficTestCompleted = false;
    double PitTestOpponentStart = 0;
    int32 TestContacts = 0;
    int32 TestDriftPeakTier = 0, TestDriftReleasedTier = 0;
    double DriftTestDirection = 1;
    int32 TestRespawns = 0;
    int32 TestVisibilityErrors = 0;
    int32 TestControlledLandings = 0;
    double LandingFeedbackTime = 0, LandingQuality = 0;
    double TestSeconds = 0;
    void SimulateStep(const Vroom::DriveInput& Input, double H);
    void PlaceOnStartingGrid();
    void UpdatePresentation(double Alpha);
    bool HandleGraphicsInput(APlayerController* PC);
    void UpdateDiagnostics(float Dt);
    void PrepareCollisionTest();
    void UpdateSessionRules();
    double QualificationBenchmark() const;
    void UpdateSound();
    void BuildCrashDebris();
    void ResetCrashDebris();
    void UpdateCrashDebris(float Dt);
    UPROPERTY() TObjectPtr<UStaticMeshComponent> CrashDebrisWheel;
    int32 DetachedTyreIndex = INDEX_NONE;
    bool CrashDebrisWasBroken = false;
    bool CrashDebrisActive = false;
    float CrashDebrisAge = 0;
    float CrashDebrisGroundZ = 0;
    FVector CrashDebrisVelocity = FVector::ZeroVector;
    FVector CrashDebrisSpin = FVector::ZeroVector;
};

UCLASS()
class VROOM_API AVroomHUD : public AHUD
{
    GENERATED_BODY()
public:
    AVroomHUD();
    virtual ~AVroomHUD() override;
    virtual void DrawHUD() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    bool IsCanvasPreparationComplete() const;
    bool HasDrawnPresentation() const { return PresentationDrawn; }
private:
    bool PresentationDrawn = false;
    bool CanvasWarmupSubmitted = false;
    TUniquePtr<FRenderCommandFence> CanvasWarmupFence;
    void Label(const FString& S, float X, float Y, float Size, FLinearColor Color, bool CentreX = false);
};

UCLASS()
class VROOM_API AVroomGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AVroomGameMode();
    virtual void BeginPlay() override;
};
