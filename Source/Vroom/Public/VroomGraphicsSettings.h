#pragma once

#include "CoreMinimal.h"

namespace Vroom::Graphics
{
// Call after the platform's initial rendering profile, before spawning scenery.
void Initialize();
void ApplySceneSettings();
bool IsOpen();
void Toggle();
int RowCount();
FString Label(int Row);
FString Value(int Row);
FString Description(int Row);
bool IsEnabled(int Row);
int SelectedRow();
void SelectRow(int Row);
void Adjust(int Direction);
// Edits remain pending until Apply. Both operations close the menu.
void Apply();
void Cancel();
// Scenery reads the applied values, never the unsaved menu preview.
bool AnimatedGrass();
// Enables the applied photo lighting/scenery budget, including when its
// individual quality values have subsequently been customised in the menu.
bool PhotorealismEnabled();
// 0: none, 1: sparse, 2: medium, 3: dense.
int FoliageQuality();
}
