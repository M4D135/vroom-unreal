#include "VroomGame.h"
#include "VroomGraphicsSettings.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
#include "GlobalRenderResources.h"
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

struct FMapGeometryCache
{
    TWeakObjectPtr<AVroomCircuit> Circuit;
    int32 TrackIndex = INDEX_NONE;
    float LengthM = 0;
    TArray<FVector> Points;
    FVector2D Min = FVector2D::ZeroVector;
    FVector2D Max = FVector2D::ZeroVector;
};

FString LapTime(double Seconds)
{
    const int32 Milliseconds = FMath::Max(0, FMath::FloorToInt(Seconds * 1000.0));
    return FString::Printf(TEXT("%02d:%02d.%03d"), Milliseconds / 60000,
        (Milliseconds / 1000) % 60, Milliseconds % 1000);
}

}

void AVroomHUD::Label(const FString& S, float X, float Y, float Size, FLinearColor Color, bool CentreX)
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
    const FSlateFontInfo FontInfo(Font.Get(), FMath::Max(1.0f, Size * 72.0f / FontConstants::RenderDPI), FName(TEXT("Bold")));
    FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(S), FontInfo, Color);
    Item.Scale = FVector2D(1.0f, 1.0f);
    Item.FontRenderInfo.bEnableShadow = false;
    Item.bCentreX = CentreX;
    Item.bCentreY = false;
    Canvas->DrawItem(Item);
}

void AVroomHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !PlayerOwner) return;
    const AVroomCar* Car = Cast<AVroomCar>(PlayerOwner->GetPawn());
    if (!Car || !Car->Circuit) return;
    const AVroomCircuit* Circuit = Car->Circuit;
    const FVroomTrack* Track = Circuit->CurrentTrack();
    const Vroom::ContactState& Contact = Car->Contacts.State;

    // Work in a fixed minimum design space, then scale cleanly up to 4K.
    const float Scale = FMath::Max(0.01f, FMath::Min(Canvas->ClipX / 1440.0f, Canvas->ClipY / 900.0f));
    const float W = Canvas->ClipX / Scale;
    const float H = Canvas->ClipY / Scale;
    auto Text = [&](const FString& S, float X, float Y, float Size, FLinearColor Color = Paper, bool CentreX = false)
    {
        Label(S, X * Scale, Y * Scale, Size * Scale, Color, CentreX);
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
    auto Rounded = [&](float X, float Y, float Width, float Height, float Radius, FLinearColor Color)
    {
        const double R = FMath::Min(Radius, FMath::Min(Width, Height) * .5f);
        const FVector2D Centres[] = {{X + Width - R, Y + R}, {X + Width - R, Y + Height - R},
            {X + R, Y + Height - R}, {X + R, Y + R}};
        TArray<FVector2D, TInlineAllocator<28>> Outline;
        for (int32 Corner = 0; Corner < 4; ++Corner)
            for (int32 I = 0; I <= 6; ++I)
            {
                const double Angle = (Corner - 1) * UE_HALF_PI + I * UE_HALF_PI / 6;
                Outline.Add((Centres[Corner] + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * R) * Scale);
            }
        TArray<FCanvasUVTri> Triangles;
        Triangles.Reserve(Outline.Num());
        for (int32 I = 0; I < Outline.Num(); ++I)
        {
            FCanvasUVTri T;
            T.V0_Pos = FVector2D(X + Width * .5f, Y + Height * .5f) * Scale;
            T.V1_Pos = Outline[I]; T.V2_Pos = Outline[(I + 1) % Outline.Num()];
            T.V0_UV = T.V1_UV = T.V2_UV = FVector2D::ZeroVector;
            T.V0_Color = T.V1_Color = T.V2_Color = Color;
            Triangles.Add(T);
        }
        FCanvasTriangleItem Item(Triangles, GWhiteTexture);
        Item.BlendMode = SE_BLEND_Translucent;
        Canvas->DrawItem(Item);
    };
    auto Fill = [&](float X, float Y, float Width, float Height, float Ratio, FLinearColor FillColor)
    {
        const float SafeWidth = FMath::Max(0.0f, Width);
        const float SafeHeight = FMath::Max(0.0f, Height);
        const float FillWidth = SafeWidth * FMath::Clamp(Ratio, 0.0f, 1.0f);
        Rect(X, Y, SafeWidth, SafeHeight, FLinearColor(0.10f, 0.12f, 0.14f, 0.72f));
        Rect(X, Y, FillWidth, SafeHeight, FillColor);
        Rect(X, Y, SafeWidth, 1, FLinearColor(0.66f, 0.73f, 0.80f, 0.22f));
        Rect(X, Y + SafeHeight - 1, SafeWidth, 1, FLinearColor(0.66f, 0.73f, 0.80f, 0.22f));
    };
    auto Map = [&](float X, float Y, float Width, float Height, bool ShowCars)
    {
        if (!Track || Track->Points.Num() < 2) return;
        constexpr int32 SampleCount = 160;
        static FMapGeometryCache Geometry;
        const float TrackLength = Circuit->LengthM();
        if (Geometry.Circuit.Get() != Circuit || Geometry.TrackIndex != Circuit->TrackIndex
            || !FMath::IsNearlyEqual(Geometry.LengthM, TrackLength, .001f))
        {
            Geometry.Circuit = Car->Circuit.Get();
            Geometry.TrackIndex = Circuit->TrackIndex;
            Geometry.LengthM = TrackLength;
            Geometry.Points.SetNumUninitialized(SampleCount);
            Geometry.Min = FVector2D(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
            Geometry.Max = FVector2D(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
            for (int32 I = 0; I < SampleCount; ++I)
            {
                const FVector P = Circuit->Position(I * TrackLength / SampleCount) / 100.0f;
                Geometry.Points[I] = P;
                Geometry.Min.X = FMath::Min(Geometry.Min.X, P.X); Geometry.Min.Y = FMath::Min(Geometry.Min.Y, P.Y);
                Geometry.Max.X = FMath::Max(Geometry.Max.X, P.X); Geometry.Max.Y = FMath::Max(Geometry.Max.Y, P.Y);
            }
        }
        const TArray<FVector>& MapPoints = Geometry.Points;
        const FVector2D& Min = Geometry.Min;
        const FVector2D& Max = Geometry.Max;
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
            Line(A, B, FLinearColor(0.15f, 0.23f, 0.26f), ShowCars ? 4.0f : 8.0f);
        }
        for (int32 I = 0; I < SampleCount; ++I)
            Line(Project(MapPoints[I]), Project(MapPoints[(I + 1) % SampleCount]), Paper, ShowCars ? 1.6f : 2.0f);

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

    if (Vroom::Graphics::IsOpen())
    {
        const float X = W * .5f - 430, Y = H * .5f - 400;
        const FLinearColor Cyan(.31f, .86f, .88f, 1.f);
        const FLinearColor Gold(.97f, .73f, .32f, 1.f);
        const FLinearColor Inactive(.36f, .43f, .47f, 1.f);
        Rect(0, 0, W, H, FLinearColor(.004f, .009f, .014f, .78f));
        Rect(X, Y, 860, 800, FLinearColor(.017f, .036f, .048f, .98f));
        Rect(X, Y, 860, 3, Cyan);
        Text(TEXT("VROOM / GRAPHISMES"), X + 27, Y + 23, 26, Paper);
        Text(TEXT("QUALITÉ & FLUIDITÉ"), X + 29, Y + 60, 12, Gold);
        Text(TEXT("HAUT / BAS : CHOISIR   GAUCHE / DROITE : REGLER"), X + 340, Y + 60, 11, Muted);

        const int32 Selected = Vroom::Graphics::SelectedRow();
        for (int32 I = 0; I < Vroom::Graphics::RowCount(); ++I)
        {
            const float RowY = Y + 92 + I * 40;
            const bool Enabled = Vroom::Graphics::IsEnabled(I);
            const bool Focused = I == Selected;
            if (Focused)
            {
                Rect(X + 24, RowY, 812, 38, FLinearColor(.052f, .14f, .17f, 1.f));
                Rect(X + 24, RowY, 3, 38, Enabled ? Cyan : Gold);
            }
            else if (I % 2 == 0)
                Rect(X + 24, RowY, 812, 38, FLinearColor(.035f, .064f, .081f, .60f));
            Text(Vroom::Graphics::Label(I), X + 41, RowY + 10, 17, Enabled ? Paper : Inactive);
            Text(Vroom::Graphics::Value(I), X + 637, RowY + 10, 16, Enabled ? Cyan : Inactive, true);
            if (Focused && Enabled)
            {
                Text(TEXT("-"), X + 468, RowY + 5, 25, Cyan, true);
                Text(TEXT("+"), X + 801, RowY + 5, 25, Cyan, true);
            }
        }
        const float DetailsY = Y + 633;
        Rect(X + 25, DetailsY - 7, 810, 1, FLinearColor(.13f, .24f, .28f, 1.f));
        Text(Vroom::Graphics::Label(Selected).ToUpper(), X + 29, DetailsY + 7, 12, Gold);
        FString Description = Vroom::Graphics::Description(Selected);
        // Wrap longer notices without shrinking the help text.
        const int32 CharacterLimit = 102;
        int32 Cut = FMath::Min(CharacterLimit, Description.Len());
        if (Cut < Description.Len())
            while (Cut > 0 && Description[Cut] != ' ') --Cut;
        if (Cut == 0) Cut = FMath::Min(CharacterLimit, Description.Len());
        Text(Description.Left(Cut), X + 29, DetailsY + 31, 14, Muted);
        if (Cut < Description.Len())
            Text(Description.Mid(Cut).TrimStart(), X + 29, DetailsY + 51, 14, Muted);

        Rect(X + 24, Y + 733, 172, 44, FLinearColor(.05f, .082f, .10f, 1.f));
        Text(TEXT("ANNULER"), X + 110, Y + 747, 15, Paper, true);
        Text(TEXT("ÉCHAP"), X + 215, Y + 750, 11, Muted);
        Rect(X + 598, Y + 733, 238, 44, Cyan);
        Text(TEXT("APPLIQUER (ENTREE)"), X + 717, Y + 747, 14, FLinearColor(.012f, .040f, .052f, 1.f), true);
        return;
    }

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
        Rect(415, H - 171, 250, 65, FLinearColor(.038f, .095f, .119f, .96f));
        Rect(415, H - 171, 250, 2, FLinearColor(.31f, .86f, .88f, 1.f));
        Text(TEXT("O"), 437, H - 154, 25, FLinearColor(.31f, .86f, .88f, 1.f));
        Text(TEXT("GRAPHISMES"), 478, H - 150, 18);

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
        Text(TEXT("FLÈCHES / ZQSD   CONDUIRE       C   VUE       G   BOÎTE       O   GRAPHISMES"), 67, H - 73, 13, Muted);
        Text(FString::Printf(TEXT("PROTOTYPE V%s · TRACÉ PROVISOIRE"), Vroom::AppVersion), 67, H - 43, 12, Muted);
        Text(TEXT("01 / PREMIERS TOURS"), W - 258, H - 43, 12, Muted);
        return;
    }

    if (Contact.ImpactFlash > 0 && !Circuit->Paused)
    {
        const float Flash = FMath::Clamp(static_cast<float>(Contact.ImpactFlash / .4), 0.0f, 1.0f);
        Rect(0, 0, W, H, FLinearColor(.40f, .015f, .01f, .045f * Flash));
        const FLinearColor Edge(.80f, .035f, .02f, .22f * Flash);
        Rect(0, 0, W, 8, Edge);
        Rect(0, H - 8, W, 8, Edge);
        Rect(0, 8, 8, H - 16, Edge);
        Rect(W - 8, 8, 8, H - 16, Edge);
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
        return;
    }

    const FLinearColor Detail(0.55f, 0.72f, 0.78f, 1);
    Text(FString::Printf(TEXT("V%s"), Vroom::AppVersion), W - 64, H - 30, 12, Detail);
    Text(TEXT("O  GRAPHISMES"), W - 194, H - 29, 11, Muted);

    const bool NeedsRecovery = Contact.Phase == Vroom::ContactPhase::Crashed
        || Contact.Phase == Vroom::ContactPhase::Recovering;
    // A compact translucent interpretation retains the original race data
    // while leaving most of the road view open.
    const FLinearColor Ice(.37f, .89f, .90f, 1.f), Silver(.69f, .77f, .79f, 1.f);
    const FLinearColor Hairline(.58f, .81f, .83f, .16f);
    Rounded(22, 21, W - 44, 112, 16, FLinearColor(.003f, .008f, .016f, .18f));
    Rounded(22, 18, W - 44, 112, 16, Hairline);
    Rounded(23, 19, W - 46, 110, 15, FLinearColor(.007f, .024f, .036f, .74f));
    Line(FVector2D(46, 19), FVector2D(W - 46, 19), FLinearColor(.71f, .95f, .97f, .20f));
    Map(39, 32, 124, 82, true);

    const bool Starting = Circuit->RaceClock < 0;
    const FLinearColor LampColors[] = {RacingRed, FLinearColor(.96f, .67f, .23f, 1),
        FLinearColor(.39f, .91f, .59f, 1)};
    const bool LampOn[] = {Starting || NeedsRecovery,
        Starting ? Circuit->RaceClock >= -2 : Car->Drive.Skidding,
        !Starting && !NeedsRecovery};
    for (int32 I = 0; I < 3; ++I)
    {
        const FVector2D Centre(184 * Scale, (43 + I * 30) * Scale);
        FCanvasNGonItem Ring(Centre, FVector2D(7 * Scale), 24, FLinearColor(.45f, .67f, .72f, .14f));
        Canvas->DrawItem(Ring);
        FLinearColor Color = LampColors[I]; Color.A = LampOn[I] ? 1.f : .14f;
        FCanvasNGonItem Light(Centre, FVector2D(4.5f * Scale), 24, Color);
        Canvas->DrawItem(Light);
    }
    Text(FString::Printf(TEXT("GRAND PRIX / %s"), *TrackName), 214, 34, 11, Ice);
    const FString CircuitTitle = Track && !Track->CircuitName.IsEmpty() ? Track->CircuitName : TrackName;
    Text(CircuitTitle, 213, 53, 27, Paper);
    Text(Distance, 215, 96, 11, Silver);
    for (float X : {477.f, 842.f, W - 220})
        Line(FVector2D(X, 39), FVector2D(X, 109), Hairline);

    Text(TEXT("SCORE"), 501, 35, 10, Silver);
    Text(FString::Printf(TEXT("%05d"), FMath::Clamp(Car->Score, 0, 99999)), 499, 53, 31, Paper);
    Text(TEXT("STAGE"), 666, 35, 10, Silver);
    Text(FString::Printf(TEXT("%02d"), Circuit->TrackIndex + 1), 664, 53, 31, Paper);
    Text(TEXT("TOUR"), 765, 35, 10, Silver);
    Text(FString::Printf(TEXT("%02d"), FMath::Max(0, Car->Lap - 1)), 763, 53, 31, Paper);
    const FString Status = NeedsRecovery ? TEXT("RETOUR EN PISTE") : Starting ? TEXT("DÉPART")
        : Contact.Phase == Vroom::ContactPhase::Airborne ? TEXT("EN L'AIR") : TEXT("EN PISTE");
    Text(Status, 501, 98, 10, NeedsRecovery ? RacingRed : Ice);

    const float PassX = W - 562;
    Text(TEXT("DÉPASSEMENTS"), PassX, 35, 10, Silver);
    Text(FString::Printf(TEXT("%02d"), Car->CarsPassed), PassX + 234, 31, 16, Ice);
    const int32 Passed = FMath::Clamp(Car->CarsPassed, 0, 27);
    for (int32 I = 0; I < 27; ++I)
    {
        const float X = PassX + (I % 9) * 28, Y = 64 + (I / 9) * 17;
        const FLinearColor Color = I < Passed ? Ice : FLinearColor(.38f, .62f, .66f, .28f);
        Rect(X, Y, 17, 4, Color);
        Rect(X + 5, Y - 3, 7, 10, Color);
        Rect(X + 1, Y - 2, 3, 2, Color); Rect(X + 13, Y - 2, 3, 2, Color);
        Rect(X + 1, Y + 4, 3, 2, Color); Rect(X + 13, Y + 4, 3, 2, Color);
    }
    const float FuelX = W - 194;
    const float Fuel = FMath::Clamp(static_cast<float>(Car->Drive.Fuel), 0.f, 1.f);
    const FLinearColor FuelColor = Fuel < .22f ? RacingRed : Ice;
    Text(TEXT("ESSENCE"), FuelX, 35, 10, Silver);
    Text(FString::Printf(TEXT("%d"), FMath::RoundToInt(Fuel * 100)), FuelX - 2, 53, 31, Paper);
    Text(TEXT("%"), FuelX + 58, 71, 12, Silver);
    Rounded(W - 58, 41, 5, 66, 2.5, FLinearColor(.29f, .52f, .57f, .22f));
    if (Fuel > .001f) Rounded(W - 58, 107 - 66 * Fuel, 5, 66 * Fuel, 2.5, FuelColor);
    Text(Fuel < .22f ? TEXT("RAVITAILLER") : TEXT("RÉSERVE OK"), FuelX, 98, 10, FuelColor);

    if (Circuit->RaceClock < 0)
    {
        const int32 Countdown = FMath::Clamp(FMath::CeilToInt(-Circuit->RaceClock), 1, 3);
        const float Y = 146.f;
        Rect(W * .5f - 125, Y, 250, 72, Panel);
        Text(FString::Printf(TEXT("DÉPART DANS %d"), Countdown), W * .5f, Y + 10, 17, Paper, true);
        for (int32 I = 0; I < 3; ++I)
        {
            FCanvasNGonItem Lamp(FVector2D((W * .5f - 46 + I * 46) * Scale, (Y + 49) * Scale),
                FVector2D(10 * Scale), 24, I < 4 - Countdown ? RacingRed : FLinearColor(.16f, .018f, .012f, 1));
            Canvas->DrawItem(Lamp);
        }
    }
    else if (NeedsRecovery)
    {
        Text(TEXT("RETOUR EN PISTE…"), W * .5f, 154, 23, Paper, true);
    }

    // Lap timing remains useful in free practice, outside the original band.
    Text(LapTime(FMath::Max(0.0, Circuit->RaceClock - Car->LapStart)), 24, H - 65, 23);
    Text(Car->BestLap > 0 ? FString::Printf(TEXT("MEILLEUR  %s"), *LapTime(Car->BestLap))
        : TEXT("PREMIER TOUR"), 24, H - 35, 12, Muted);
    if (!Car->LapClean) Text(TEXT("TOUR INVALIDÉ"), 24, H - 90, 12, RacingRed);

    // The cockpit has its own physical speed, gear and RPM instruments.
    if (Car->ChaseCamera)
    {
        Text(FString::Printf(TEXT("%03d"), FMath::RoundToInt(Car->Drive.State.SpeedMps * 3.6)),
            W - 200, H - 139, 57);
        Text(TEXT("KM/H"), W - 194, H - 71, 13, Muted);
        Text(FString::Printf(TEXT("%d"), Car->Drive.State.Gear), W - 72, H - 123, 40);
        Text(Car->Drive.Automatic ? TEXT("AUTO") : TEXT("MANUEL"), W - 85, H - 72, 11, Muted);
        Fill(W - 193, H - 48, 152, 4,
            static_cast<float>(Car->Drive.State.Rpm / Vroom::DriveModel::MaxRpm), RacingRed);
    }

    if (!NeedsRecovery && (Car->Drive.Drifting || Car->Drive.BoostRemaining > 0))
    {
        const bool Boost = Car->Drive.BoostRemaining > 0 && !Car->Drive.Drifting;
        const FLinearColor Accent = Boost ? FLinearColor(.35f, 1.f, .88f, 1.f)
            : FLinearColor(.96f, .63f, .18f, 1.f);
        const float X = W * .5f - 130, Y = H * .57f;
        const float Charge = static_cast<float>(Car->Drive.DriftCharge);
        FString Feedback = TEXT("DÉRAPAGE");
        if (Boost) Feedback = Car->Drive.BoostTier >= 2 ? TEXT("SUPER BOOST") : TEXT("BOOST");
        else if (Charge >= 1.f) Feedback = TEXT("RELÂCHE : SUPER BOOST");
        else if (Charge >= .4f) Feedback = TEXT("RELÂCHE : BOOST");
        Text(Feedback, W * .5f, Y, 17, Accent, true);
        Fill(X, Y + 29, 260, 5, Boost ? static_cast<float>(Car->Drive.BoostRemaining / (Car->Drive.BoostTier >= 2 ? 1.15 : .55))
            : Charge / 1.35f, Accent);
        if (!Boost)
            for (float Threshold : {.4f, 1.f})
                Rect(X + 260 * Threshold / 1.35f, Y + 27, 2, 9, Paper);
    }
}
