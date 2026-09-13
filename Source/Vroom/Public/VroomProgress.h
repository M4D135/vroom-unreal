#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VroomProgress.generated.h"
class UStaticMeshComponent;
class AVroomCar;
namespace Vroom::Progress
{
struct FGhostSample { double Time=0, Distance=0; FVector Location=FVector::ZeroVector; FQuat Rotation=FQuat::Identity; };
struct FRecord { double Best=0; int Laps=0; TArray<double> History; TArray<FGhostSample> Ghost; };
void Initialize();
bool RunSelfTest();
const FRecord& Record(int Track,int Mode);
// Positive = behind personal best, negative = ahead. Unset before a valid reference exists.
TOptional<double> LapDelta(int Track,int Mode,double LapDistance,double LapTime);
}
UCLASS()
class VROOM_API AVroomProgressTracker : public AActor
{
    GENERATED_BODY()
public:
    AVroomProgressTracker();
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> GhostMeshes;
    TWeakObjectPtr<AVroomCar> Car;
    int TrackIndex=-1, ModeIndex=-1, ObservedLap=0;
    double LastClock=-1, SampleClock=-1;
    bool Clean=true, Recording=false;
    TArray<Vroom::Progress::FGhostSample> Samples;
    void BuildGhost(AVroomCar* Player);
};
