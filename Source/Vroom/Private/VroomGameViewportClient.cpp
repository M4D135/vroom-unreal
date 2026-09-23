#include "VroomGameViewportClient.h"
#include "VroomGame.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

void UVroomGameViewportClient::PostRender(UCanvas* Canvas)
{
    Super::PostRender(Canvas);
    if (!Canvas || !Canvas->Canvas) return;

    if (!StartupRevealed)
    {
        const auto* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
        const auto* HUD = PC ? Cast<AVroomHUD>(PC->GetHUD()) : nullptr;
        if (HUD && HUD->HasDrawnPresentation())
        {
            StartupRevealed = true;
            UE_LOG(LogTemp, Display, TEXT("VROOM: Startup cover released after HUD presentation frame=%u"), StartupFrame);
        }
        else
        {
            // Unlike the player's HUD, this exists before possession/circuit
            // setup. Draw last, fully opaque, without suppressing world rendering.
            FCanvasTileItem Cover(FVector2D::ZeroVector, FVector2D(Canvas->ClipX, Canvas->ClipY),
                FLinearColor(.012f, .019f, .024f, 1.f));
            Cover.BlendMode = SE_BLEND_Opaque;
            Canvas->DrawItem(Cover);
        }
    }

    // Optional visible QA captures, including frames normally missed by the
    // ready-scene screenshot timer. Readbacks affect timing: never profile with
    // this option, and never replace another pending screenshot request.
    if (StartupFrame < 4 && !FScreenshotRequest::IsScreenshotRequested()
        && FParse::Param(FCommandLine::Get(), TEXT("VroomVerify")))
    {
        FString CaptureDir;
        if (FParse::Value(FCommandLine::Get(), TEXT("VroomStartupCaptureDir="), CaptureDir))
        {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(CaptureDir,
                FString::Printf(TEXT("startup-%02u.png"), StartupFrame)), true, false);
            UE_LOG(LogTemp, Display, TEXT("VROOM: Startup capture frame=%u presentation=%d"), StartupFrame, StartupRevealed);
        }
    }
    // Saturate: the diagnostic must not restart after a very long session.
    if (StartupFrame < MAX_uint32) ++StartupFrame;
}
