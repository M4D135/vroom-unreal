#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
class APlayerController;
namespace Vroom
{
enum class Action : uint8 { Throttle, Brake, SteerLeft, SteerRight, Drift, ShiftUp, ShiftDown,
    Start, Pause, Menu, Restart, View, Gearbox, MouseSteer, Tyres, Refuel, Options, Controls,
    NextStage, TrackNext, TrackPrevious, ModeNext, DifficultyNext, Count };
struct FPlayerSettings
{
    float SteeringSensitivity = 1.f, SteeringDeadzone = .06f, MouseSensitivity = .004f, Fov = 68.f;
    bool ChaseCamera = false, Automatic = true, GhostEnabled = true;
    float MasterVolume = 1.f, EngineVolume = 1.f, EffectsVolume = 1.f;
    TArray<FKey> Keys;
};
namespace Settings
{
void Initialize();
bool RunSelfTest();
const FPlayerSettings& Get();
FPlayerSettings& Mutable();
void Save();
bool WasPressed(APlayerController* PC, Action What);
bool IsDown(APlayerController* PC, Action What);
FString BindingLabel(Action What);
bool IsOpen();
void Toggle();
// Handles the modal only. Caller freezes/resumes the session around IsOpen().
bool HandleMenuInput(APlayerController* PC);
int RowCount();
int SelectedRow();
int FirstVisibleRow();
constexpr int VisibleRows = 12;
void SelectRow(int Row);
void Adjust(int Direction);
FString Label(int Row);
FString Value(int Row);
FString Description(int Row);
bool IsBinding();
void Apply();
void Cancel();
}
}
