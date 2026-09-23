#pragma once

#include "Engine/GameViewportClient.h"
#include "VroomGameViewportClient.generated.h"

// Shared by every desktop platform. The world still renders beneath the cover
// so material and HUD preparation can finish before the title is revealed.
UCLASS()
class VROOM_API UVroomGameViewportClient : public UGameViewportClient
{
    GENERATED_BODY()
public:
    virtual void PostRender(UCanvas* Canvas) override;
private:
    bool StartupRevealed = false;
    uint32 StartupFrame = 0;
};
