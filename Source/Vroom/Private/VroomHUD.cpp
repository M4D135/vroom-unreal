#include "VroomGame.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
const FLinearColor Paper(0.94f, 0.925f, 0.86f, 1.0f);
const FLinearColor Muted(0.57f, 0.60f, 0.59f, 1.0f);
const FLinearColor RacingRed(0.92f, 0.055f, 0.035f, 1.0f);
const FLinearColor Panel(0.016f, 0.022f, 0.025f, 0.88f);

FString LapTime(double Seconds)
{
    const int32 Milliseconds = FMath::Max(0, FMath::FloorToInt(Seconds * 1000.0));
    return FString::Printf(TEXT("%02d:%02d.%03d"), Milliseconds / 60000,
        (Milliseconds / 1000) % 60, Milliseconds % 1000);
}
}

void AVroomHUD::Label(const FString& S, float X, float Y, float Size, FLinearColor Color)
{
    if (!Canvas) return;
    // Canvas requires a UFont even when supplied a Slate font description.
    // Retain it across frames so GC cannot invalidate the runtime font provider.
    static const TStrongObjectPtr<UFont> Font = []()
    {
        TStrongObjectPtr<UFont> RuntimeFont(NewObject<UFont>(GetTransientPackage()));
        RuntimeFont->FontCacheType = EFontCacheType::Runtime;
        RuntimeFont->GetMutableInternalCompositeFont() = FCompositeFont(FName(TEXT("Bold")),
            FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf"),
            EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
        return RuntimeFont;
    }();
    // Slate uses points at 96 DPI. Rasterize at the final pixel size, without
    // magnifying the low-resolution offline engine font atlas.
    const FSlateFontInfo FontInfo(Font.Get(), FMath::Max(1.0f, Size * 72.0f / FontConstants::RenderDPI), FName(TEXT("Bold")));
    FCanvasTextItem Text(FVector2D(X, Y), FText::FromString(S), FontInfo, Color);
    Text.Scale = FVector2D(1.0f, 1.0f);
    Text.FontRenderInfo.bEnableShadow = false;
    Text.bCentreX = false;
    Text.bCentreY = false;
    Canvas->DrawItem(Text);
}

void AVroomHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !PlayerOwner) return;
    const AVroomCar* Car = Cast<AVroomCar>(PlayerOwner->GetPawn());
    if (!Car || !Car->Circuit) return;
    const AVroomCircuit* Circuit = Car->Circuit;
    const FVroomTrack* Track = Circuit->CurrentTrack();

    // Work in a fixed minimum design space, then scale cleanly up to 4K.
    const float Scale = FMath::Max(0.01f, FMath::Min(Canvas->ClipX / 1440.0f, Canvas->ClipY / 900.0f));
    const float W = Canvas->ClipX / Scale;
    const float H = Canvas->ClipY / Scale;
    auto Text = [&](const FString& S, float X, float Y, float Size, FLinearColor Color = Paper)
    {
        Label(S, X * Scale, Y * Scale, Size * Scale, Color);
    };
    auto Rect = [&](float X, float Y, float Width, float Height, FLinearColor Color)
    {
        DrawRect(Color, X * Scale, Y * Scale, Width * Scale, Height * Scale);
    };
    auto Line = [&](FVector2D A, FVector2D B, FLinearColor Color, float Thickness = 1.0f)
    {
        FCanvasLineItem Item(A * Scale, B * Scale);
        Item.SetColor(Color);
        Item.LineThickness = Thickness * Scale;
        Canvas->DrawItem(Item);
    };
    auto Map = [&](float X, float Y, float Width, float Height, bool ShowCars)
    {
        if (!Track || Track->Points.Num() < 2) return;
        constexpr int32 SampleCount = 300;
        FVector MapPoints[SampleCount];
        const float TrackLength = Circuit->LengthM();
        FVector2D Min(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
        FVector2D Max(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
        for (int32 I = 0; I < SampleCount; ++I)
        {
            const FVector P = Circuit->Position(I * TrackLength / SampleCount) / 100.0f;
            MapPoints[I] = P;
            Min.X = FMath::Min(Min.X, P.X); Min.Y = FMath::Min(Min.Y, P.Y);
            Max.X = FMath::Max(Max.X, P.X); Max.Y = FMath::Max(Max.Y, P.Y);
        }
        const float MapScale = FMath::Min((Width - 26.0f) / FMath::Max(1.0, Max.X - Min.X),
            (Height - 26.0f) / FMath::Max(1.0, Max.Y - Min.Y));
        const FVector2D Mid = (Min + Max) * 0.5;
        auto Project = [&](const FVector& P)
        {
            return FVector2D(X + Width * 0.5f + (P.X - Mid.X) * MapScale,
                Y + Height * 0.5f - (P.Y - Mid.Y) * MapScale);
        };
        for (int32 I = 0; I < SampleCount; ++I)
        {
            const FVector2D A = Project(MapPoints[I]);
            const FVector2D B = Project(MapPoints[(I + 1) % SampleCount]);
            Line(A, B, FLinearColor(0.15f, 0.18f, 0.18f), 8.0f);
        }
        for (int32 I = 0; I < SampleCount; ++I)
            Line(Project(MapPoints[I]), Project(MapPoints[(I + 1) % SampleCount]), Paper, 2.0f);

        const FVector2D Start = Project(MapPoints[0]);
        Rect(Start.X - 4, Start.Y - 4, 8, 8, Paper);
        if (ShowCars)
        {
            for (double Distance : Circuit->OpponentS)
            {
                const FVector2D P = Project(Circuit->Position(Distance) / 100.0f);
                Rect(P.X - 2, P.Y - 2, 4, 4, Muted);
            }
            const FVector2D P = Project(Circuit->Position(Car->Drive.State.DistanceM) / 100.0f);
            Rect(P.X - 6, P.Y - 6, 12, 12, Panel);
            Rect(P.X - 4, P.Y - 4, 8, 8, RacingRed);
        }
    };

    const FString TrackName = Track ? Track->Name.ToUpper() : TEXT("CIRCUIT");
    const FString Distance = FString::Printf(TEXT("%.2f KM"), Circuit->LengthM() / 1000.0f);

    if (!Circuit->Running)
    {
        Rect(0, 0, W, H, FLinearColor(0.008f, 0.012f, 0.015f, 0.37f));
        // The left shade keeps the opening screen legible over the live circuit.
        for (int32 I = 0; I < 32; ++I)
        {
            const float Alpha = 0.92f * (1.0f - static_cast<float>(I) / 32.0f);
            Rect(I * W * 0.027f, 0, W * 0.027f + 1, H,
                FLinearColor(0.008f, 0.012f, 0.015f, Alpha));
        }
        Rect(62, 68, 46, 5, RacingRed);
        Text(TEXT("1991 / RETOUR EN PISTE"), 124, 61, 17);
        Text(TEXT("VROOM"), 57, 116, 155);
        Text(TEXT("PREMIERS TOURS"), 66, 294, 32);
        Text(TEXT("La vitesse. Le rythme. La trajectoire."), 67, 347, 19, Muted);

        Text(TEXT("SÉANCE LIBRE"), 67, H - 316, 14, Muted);
        Text(TrackName, 64, H - 286, 43);
        Text(FString::Printf(TEXT("CIRCUIT %02d / 06    ·    %s"), Circuit->TrackIndex + 1, *Distance),
            67, H - 228, 16, Muted);
        Rect(66, H - 171, 333, 65, RacingRed);
        Text(TEXT("ENTRÉE"), 87, H - 154, 25);
        Text(TEXT("ROULER  >"), 231, H - 150, 18);

        const float CardX = W - 437;
        const float CardY = 90;
        Rect(CardX, CardY, 369, 614, Panel);
        Rect(CardX, CardY, 369, 3, RacingRed);
        Text(TEXT("LES SIX CIRCUITS"), CardX + 24, CardY + 24, 18);
        Text(TEXT("F1 — F6 POUR CHOISIR"), CardX + 24, CardY + 54, 13, Muted);
        Map(CardX + 33, CardY + 91, 303, 192, false);
        for (int32 I = 0; I < Circuit->Tracks.Num() && I < 6; ++I)
        {
            const float Y = CardY + 306 + I * 45;
            const bool Selected = I == Circuit->TrackIndex;
            if (Selected)
            {
                Rect(CardX + 12, Y - 5, 345, 40, FLinearColor(0.18f, 0.035f, 0.026f, 0.94f));
                Rect(CardX + 12, Y - 5, 3, 40, RacingRed);
            }
            Text(FString::Printf(TEXT("F%d"), I + 1), CardX + 26, Y + 1, 16, Selected ? RacingRed : Muted);
            Text(Circuit->Tracks[I].Name.ToUpper(), CardX + 74, Y, 18, Selected ? Paper : Muted);
            if (Selected) Text(TEXT("<"), CardX + 324, Y, 18, RacingRed);
        }
        Text(TEXT("FLÈCHES / ZQSD   CONDUIRE       C   VUE       G   BOÎTE"), 67, H - 73, 13, Muted);
        Text(TEXT("PROTOTYPE · TRACÉ PROVISOIRE"), 67, H - 43, 12, Muted);
        Text(TEXT("01 / PREMIERS TOURS"), W - 258, H - 43, 12, Muted);
        return;
    }

    Rect(28, 24, W - 56, 88, Panel);
    Rect(28, 24, 5, 88, RacingRed);
    Text(TEXT("VROOM / 1991"), 49, 37, 13, Muted);
    Text(TrackName, 47, 63, 26);
    Rect(345, 42, 1, 52, FLinearColor(0.22f, 0.24f, 0.24f, 1));
    Text(TEXT("TOUR"), 371, 39, 12, Muted);
    Text(FString::Printf(TEXT("%02d"), Car->Lap), 369, 59, 32);
    Text(TEXT("EN COURS"), 476, 39, 12, Muted);
    Text(LapTime(Circuit->RaceClock - Car->LapStart), 474, 63, 26, Car->LapClean ? Paper : RacingRed);
    Text(TEXT("MEILLEUR"), 706, 39, 12, Muted);
    Text(Car->BestLap > 0 ? LapTime(Car->BestLap) : TEXT("--:--.---"), 704, 63, 26);
    Text(TEXT("PRÉCÉDENT"), 936, 39, 12, Muted);
    Text(Car->LastLap > 0 ? LapTime(Car->LastLap) : TEXT("--:--.---"), 934, 63, 26);
    Text(TEXT("PROTOTYPE"), W - 219, 41, 13, Muted);
    Text(TEXT("TRACÉ PROVISOIRE"), W - 219, 68, 12, Muted);

    Rect(W - 266, 131, 238, 227, Panel);
    Map(W - 251, 139, 208, 176, true);
    Rect(W - 245, 332, 6, 6, RacingRed);
    Text(TEXT("VOUS"), W - 229, 326, 12, Muted);
    Text(Distance, W - 123, 326, 12, Muted);

    const float DashboardY = H - 253;
    Rect(28, DashboardY, 404, 178, Panel);
    Rect(28, DashboardY, 4, 178, RacingRed);
    Text(FString::Printf(TEXT("%03d"), FMath::RoundToInt(FMath::Abs(Car->Drive.State.SpeedMps) * 3.6)),
        44, DashboardY + 9, 94);
    Text(TEXT("KM/H"), 53, DashboardY + 105, 12, Muted);
    Rect(309, DashboardY + 18, 100, 92, FLinearColor(0.075f, 0.085f, 0.085f, 1));
    const FString Gear = Car->Drive.State.Gear > 0 ? FString::FromInt(Car->Drive.State.Gear) : TEXT("N");
    Text(Gear, 333, DashboardY + 17, 69);
    Text(Car->Drive.Automatic ? TEXT("AUTO") : TEXT("MANUEL"), 322, DashboardY + 91, 11, Muted);

    constexpr int32 RpmSegments = 28;
    const float RpmFraction = FMath::Clamp(static_cast<float>(Car->Drive.State.Rpm / Vroom::DriveModel::MaxRpm), 0.0f, 1.0f);
    for (int32 I = 0; I < RpmSegments; ++I)
    {
        const bool Lit = RpmFraction >= static_cast<float>(I + 1) / RpmSegments;
        const FLinearColor Segment = Lit ? (I >= 23 ? RacingRed : Paper) : FLinearColor(0.13f, 0.16f, 0.16f, 1);
        Rect(51 + I * 12.7f, DashboardY + 137, 9.7f, 8, Segment);
    }
    Text(FString::Printf(TEXT("%05d TR/MIN"), FMath::RoundToInt(Car->Drive.State.Rpm)), 51, DashboardY + 155, 11, Muted);
    Text(TEXT("13 312"), 365, DashboardY + 155, 11, Muted);

    Rect(W - 366, H - 126, 338, 51, Panel);
    Text(Car->ChaseCamera ? TEXT("POURSUITE") : TEXT("COCKPIT"), W - 346, H - 109, 13);
    Text(Car->MouseSteer ? TEXT("PILOTAGE SOURIS") : TEXT("PILOTAGE CLAVIER"), W - 213, H - 109, 12, Muted);
    Rect(28, H - 57, W - 56, 31, Panel);
    Text(TEXT("ZQSD / FLÈCHES · CONDUIRE     E / X · RAPPORTS     G · BOÎTE     C · VUE     M · SOURIS     F10 · FOCALE     P · PAUSE     R · RESET     ÉCHAP · MENU"),
        42, H - 49, 11, Muted);

    if (!Car->LapClean)
    {
        const float X = W * 0.5f - 156;
        Rect(X, 131, 312, 39, Panel);
        Rect(X, 131, 3, 39, RacingRed);
        Text(TEXT("TOUR INVALIDÉ"), X + 23, 141, 15, RacingRed);
    }

    if (Circuit->RaceClock < 0)
    {
        const int32 Countdown = FMath::Clamp(FMath::CeilToInt(-Circuit->RaceClock), 1, 3);
        const int32 LitCount = 4 - Countdown;
        const float X = W * 0.5f - 170;
        const float Y = H * 0.37f;
        Rect(X, Y, 340, 147, Panel);
        Text(FString::Printf(TEXT("DÉPART DANS %d"), Countdown), X + 68, Y + 19, 23);
        for (int32 I = 0; I < 3; ++I)
        {
            const FVector2D Center((X + 84 + I * 86) * Scale, (Y + 96) * Scale);
            FCanvasNGonItem Housing(Center, FVector2D(28 * Scale), 32,
                FLinearColor(0.08f, 0.095f, 0.095f, 1));
            Canvas->DrawItem(Housing);
            FCanvasNGonItem Lamp(Center, FVector2D(21 * Scale), 32,
                I < LitCount ? RacingRed : FLinearColor(0.16f, 0.018f, 0.012f, 1));
            Canvas->DrawItem(Lamp);
        }
    }

    if (Circuit->Paused)
    {
        Rect(0, 0, W, H, FLinearColor(0.008f, 0.012f, 0.015f, 0.57f));
        const float X = W * 0.5f - 225;
        const float Y = H * 0.5f - 111;
        Rect(X, Y, 450, 222, Panel);
        Rect(X, Y, 450, 4, RacingRed);
        Text(TEXT("AU STAND"), X + 33, Y + 28, 48);
        Text(TEXT("La séance est en pause."), X + 37, Y + 98, 19, Muted);
        Text(TEXT("P  REPRENDRE"), X + 37, Y + 158, 19);
        Text(TEXT("ÉCHAP  MENU"), X + 245, Y + 161, 15, Muted);
    }
}
