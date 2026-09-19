#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VroomRaceRules.h"
#include "VroomFeedbackComponent.generated.h"

// A bounded pool of world-space tyre strips / smoke and a stereo race mix.
// Cosmetic only: it cannot move the car or change the fixed simulation clock.
UCLASS()
class UVroomFeedbackComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVroomFeedbackComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction* Function) override;
    void ResetForRace();
private:
    UPROPERTY() TObjectPtr<class AVroomCar> Car;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> Marks;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> Smoke;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> Sparks;
    UPROPERTY() TObjectPtr<class USoundWaveProcedural> Wave;
    UPROPERTY() TObjectPtr<class UAudioComponent> Audio;
    struct FStrip { FVector A, B, C, D; double Birth = 0; };
    struct FPuff { FVector Position; double Birth = 0; float Strength = 0; };
    struct FSpark { FVector Position, Velocity; double Birth = 0; float Size = 0; int32 Tier = 0; };
    TArray<FStrip> Strips;
    TArray<FPuff> Puffs;
    TArray<FSpark> SparkPool;
    TArray<double> Phases;
    TArray<int16> AudioSamples;
    Vroom::StartToneVoice StartTone;
    double Clock = 0, VisualClock = 0, NoisePhase = 0, LastMarkClock = 0;
    double ShiftEnvelope = 0, CueEnvelope = 0, CuePhase = 0;
    int32 StripCursor = 0, PuffCursor = 0, LastGear = 1, LastChargeTier = 0, LastTrack = -1;
    int32 SparkCursor = 0, MaxSeenTier = 0;
    uint32 NoiseSeed = 0x13579;
    FVector LastWheels[2];
    bool WasMarking = false;
    bool SeenMarks = false, SeenSmoke = false, SeenBoost = false, ReportedTest = false;
    bool SeenSparks = false, SeenPerfect = false;
    void UpdateEffects(float Dt);
    void UpdateRaceSound();
};
