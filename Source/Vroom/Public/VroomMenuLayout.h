#pragma once
#include "CoreMinimal.h"
#include "VroomPlayerSettings.h"
class AVroomCar;
class APlayerController;
namespace Vroom::MenuLayout
{
enum class ItemKind { Action, Track, Mode, Difficulty };
struct FItem
{
    FBox2D Bounds;
    ItemKind Kind=ItemKind::Action;
    Vroom::Action Command=Vroom::Action::Start;
    int Value=0;
    FString Label;
};
TArray<FItem> Items(const AVroomCar* Car,float Width,float Height);
bool HandleInput(AVroomCar* Car,APlayerController* PC);
int FocusedItem();
bool RunSelfTest(AVroomCar* Car);
}
