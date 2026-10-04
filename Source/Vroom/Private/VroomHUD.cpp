#include "VroomGame.h"
#include "VroomGraphicsSettings.h"
#include "VroomPlayerSettings.h"
#include "VroomProgress.h"
#include "VroomMenuLayout.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
#include "GlobalRenderResources.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/PlatformTime.h"
#include "CanvasTypes.h"
#include "RenderCommandFence.h"
#include "Misc/ScopeExit.h"

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

AVroomHUD::AVroomHUD() = default;
AVroomHUD::~AVroomHUD() = default;

bool AVroomHUD::IsCanvasPreparationComplete() const
{
    return CanvasWarmupSubmitted && CanvasWarmupFence && CanvasWarmupFence->IsFenceComplete();
}

void AVroomHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    // Release the task while rendering and its event pools are still alive.
    if (CanvasWarmupFence) CanvasWarmupFence->Wait();
    CanvasWarmupFence.Reset();
    Super::EndPlay(Reason);
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
    if (!Track) return;
    // The viewport keeps its initial opaque cover until a complete HUD draw.
    // Its PostRender runs after the canvas below has been submitted.
    ON_SCOPE_EXIT { PresentationDrawn = true; };
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
    // Canvas thick lines create a fresh rasterizer state for every batch. On
    // Metal that changes the pointer-based RHI PSO key every frame, defeating
    // warmup even when the underlying shader is already cached. Reuse the same
    // static triangle state as the other HUD shapes instead.
    FCanvasTriangleItem LineItem(FVector2D::ZeroVector, FVector2D::ZeroVector,
        FVector2D::ZeroVector, GWhiteTexture);
    LineItem.TriangleList.SetNum(2);
    LineItem.BlendMode = SE_BLEND_Translucent;
    auto Line = [&](FVector2D A, FVector2D B, FLinearColor Color, float Thickness = 1.0f)
    {
        const FVector2D Direction = (B - A).GetSafeNormal();
        if (Direction.IsNearlyZero()) return;
        const double Half = FMath::Max(0.0f, Thickness) * Scale * .5;
        const FVector2D Side(-Direction.Y * Half, Direction.X * Half);
        // Square ends keep adjacent minimap segments joined at their corners.
        A = A * Scale - Direction * Half;
        B = B * Scale + Direction * Half;
        auto& First = LineItem.TriangleList[0];
        auto& Second = LineItem.TriangleList[1];
        First.V0_Pos = A - Side; First.V1_Pos = B - Side; First.V2_Pos = B + Side;
        Second.V0_Pos = A - Side; Second.V1_Pos = B + Side; Second.V2_Pos = A + Side;
        // FCanvasLineItem also forced line opacity, regardless of input alpha.
        Color.A = 1;
        for (auto& Triangle : LineItem.TriangleList)
        {
            Triangle.V0_UV = Triangle.V1_UV = Triangle.V2_UV = FVector2D::ZeroVector;
            Triangle.V0_Color = Triangle.V1_Color = Triangle.V2_Color = Color;
        }
        Canvas->DrawItem(LineItem);
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
            if (Circuit->OpponentsVisible)
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

    if (Car->PreparingCircuit)
    {
        const FLinearColor PrepBg(.012f, .019f, .024f, 1);
        Rect(0, 0, W, H, PrepBg);
        if (!CanvasWarmupSubmitted && Canvas->Canvas)
        {
            // Warm the HUD paths in the same dark colour so the first frame
            // never flashes alphabet soup or white boxes before the title.
            Rect(4, 4, 28, 20, PrepBg);
            Rect(36, 4, 28, 20, PrepBg);
            Rounded(68, 4, 36, 24, 7, PrepBg);
            Rounded(108, 4, 36, 24, 7, PrepBg);
            FCanvasNGonItem Lamp(FVector2D(160 * Scale, 16 * Scale),
                FVector2D(8 * Scale), 24, PrepBg);
            Lamp.BlendMode = SE_BLEND_Opaque;
            Canvas->DrawItem(Lamp);
            for (float Thickness : {1.f, 1.6f, 4.f, 8.f})
            {
                const float Y = 34 + Thickness * 7;
                Line(FVector2D(4, Y), FVector2D(100, Y + 2), PrepBg, Thickness);
                Line(FVector2D(110, Y), FVector2D(206, Y + 2), PrepBg, Thickness);
            }
            const FString Glyphs = TEXT("0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz ÀÂÄÇÉÈÊËÎÏÔÖÙÛÜŸ àâäçéèêëîïôöùûüÿ °%+−=:.,/()·—…←→↑↓");
            for (float Size : {10.f, 12.f, 16.f, 24.f, 32.f, 52.f})
                Text(Glyphs, 4, 72 + Size * 2, Size, PrepBg);
            Text(Glyphs, 4, 205, 18, PrepBg);
            CanvasWarmupFence = MakeUnique<FRenderCommandFence>();
            CanvasWarmupFence->BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);
            CanvasWarmupSubmitted = true;
            UE_LOG(LogTemp, Display, TEXT("VROOM: Canvas PSO preparation submitted output=%dx%d"),
                FMath::RoundToInt(Canvas->ClipX), FMath::RoundToInt(Canvas->ClipY));
        }
        if (Car->Session.Phase != Vroom::SessionPhase::Menu || Car->AutoDemo)
        {
        Rect(0, 0, W, H, PrepBg);
        const float X = W * .5f, Y = H * .5f;
        Text(TEXT("VROOM"), X, Y - 96, 19, RacingRed, true);
        Text(TEXT("PRÉPARATION DU CIRCUIT"), X, Y - 44, 32, Paper, true);
        Text(Track ? Track->Name : TEXT(""), X, Y + 8, 18, Muted, true);
        Text(TEXT("Un instant, la piste se prépare…"), X, Y + 57, 16, Muted, true);
        const float Phase = static_cast<float>(FMath::Fmod(FPlatformTime::Seconds(), 1.6) / 1.6);
        Rect(X - 120, Y + 105, 240, 3, FLinearColor(.1f, .15f, .18f, 1));
        Rect(X - 120 + Phase * 180, Y + 105, 60, 3, RacingRed);
        return;
        }
    }
    if (CanvasWarmupSubmitted && !Car->PreparingCircuit)
    {
        // This preparation is complete. Arm a fresh pass for the next circuit
        // or graphics change, releasing the completed fence immediately.
        CanvasWarmupSubmitted = false;
        CanvasWarmupFence.Reset();
        UE_LOG(LogTemp, Display, TEXT("VROOM: Canvas PSO preparation ready"));
    }
    if (Vroom::Graphics::IsOpen() || Vroom::Settings::IsOpen())
    {
        const bool Controls = Vroom::Settings::IsOpen();
        auto RowLabel = [&](int Row) { return Controls ? Vroom::Settings::Label(Row) : Vroom::Graphics::Label(Row); };
        auto RowValue = [&](int Row) { return Controls ? Vroom::Settings::Value(Row) : Vroom::Graphics::Value(Row); };
        const float X = W * .5f - 430, Y = H * .5f - 400;
        const FLinearColor Cyan(.31f, .86f, .88f, 1.f);
        const FLinearColor Gold(.97f, .73f, .32f, 1.f);
        const FLinearColor Inactive(.36f, .43f, .47f, 1.f);
        Rect(0, 0, W, H, FLinearColor(.004f, .009f, .014f, .78f));
        Rect(X, Y, 860, 800, FLinearColor(.017f, .036f, .048f, .98f));
        Rect(X, Y, 860, 3, Cyan);
        Text(Controls ? TEXT("VROOM / COMMANDES ET SON") : TEXT("VROOM / GRAPHISMES"), X + 27, Y + 23, 26, Paper);
        Text(Controls ? TEXT("CONFORT DE PILOTAGE") : TEXT("QUALITÉ & FLUIDITÉ"), X + 29, Y + 60, 12, Gold);
        Text(TEXT("HAUT / BAS : CHOISIR   GAUCHE / DROITE : REGLER"), X + 340, Y + 60, 11, Muted);

        const int32 Selected = Controls ? Vroom::Settings::SelectedRow() : Vroom::Graphics::SelectedRow();
        const int32 First = Controls ? Vroom::Settings::FirstVisibleRow() : 0;
        const int32 Count = Controls ? Vroom::Settings::RowCount() : Vroom::Graphics::RowCount();
        const int32 Last = Controls ? FMath::Min(Count, First + Vroom::Settings::VisibleRows) : Count;
        for (int32 I = First; I < Last; ++I)
        {
            const float RowY = Y + 92 + (I - First) * 40;
            const bool Enabled = Controls || Vroom::Graphics::IsEnabled(I);
            const bool Focused = I == Selected;
            if (Focused)
            {
                Rect(X + 24, RowY, 812, 38, FLinearColor(.052f, .14f, .17f, 1.f));
                Rect(X + 24, RowY, 3, 38, Enabled ? Cyan : Gold);
            }
            else if (I % 2 == 0)
                Rect(X + 24, RowY, 812, 38, FLinearColor(.035f, .064f, .081f, .60f));
            Text(RowLabel(I), X + 41, RowY + 10, 17, Enabled ? Paper : Inactive);
            Text(RowValue(I), X + 637, RowY + 10, 16, Enabled ? Cyan : Inactive, true);
            if (Focused && Enabled)
            {
                Text(TEXT("-"), X + 468, RowY + 5, 25, Cyan, true);
                Text(TEXT("+"), X + 801, RowY + 5, 25, Cyan, true);
            }
        }
        if (Controls)
        {
            Text(TEXT("< PAGE PRÉCÉDENTE"), X + 28, Y + 590, 12, Cyan);
            Text(FString::Printf(TEXT("%d / %d"), First / Vroom::Settings::VisibleRows + 1,
                FMath::DivideAndRoundUp(Count, Vroom::Settings::VisibleRows)), X + 430, Y + 590, 12, Muted, true);
            Text(TEXT("PAGE SUIVANTE >"), X + 645, Y + 590, 12, Cyan);
        }
        const float DetailsY = Y + 633;
        Rect(X + 25, DetailsY - 7, 810, 1, FLinearColor(.13f, .24f, .28f, 1.f));
        Text(RowLabel(Selected).ToUpper(), X + 29, DetailsY + 7, 12, Gold);
        FString Description = Controls ? Vroom::Settings::Description(Selected) : Vroom::Graphics::Description(Selected);
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
        Text(Controls ? TEXT("APPLIQUER / START") : TEXT("APPLIQUER (ENTREE)"), X + 717, Y + 747, 14, FLinearColor(.012f, .040f, .052f, 1.f), true);
        return;
    }

    auto MenuButtons = [&]()
    {
        const auto Items = Vroom::MenuLayout::Items(Car, W, H);
        for (int32 I = 0; I < Items.Num(); ++I)
        {
            const auto& Item = Items[I];
            const bool Selected = (Item.Kind == Vroom::MenuLayout::ItemKind::Mode && Item.Value == static_cast<int>(Car->Session.Mode))
                || (Item.Kind == Vroom::MenuLayout::ItemKind::Difficulty && Item.Value == static_cast<int>(Car->Session.Difficulty))
                || (Item.Kind == Vroom::MenuLayout::ItemKind::Track && Item.Value == Circuit->TrackIndex);
            const bool Focus = I == Vroom::MenuLayout::FocusedItem();
            const bool Primary = Item.Kind == Vroom::MenuLayout::ItemKind::Action && I == 0;
            const FVector2D P = Item.Bounds.Min, Size = Item.Bounds.GetSize();
            Rounded(P.X, P.Y, Size.X, Size.Y, 6, Primary ? RacingRed : Selected ? FLinearColor(.065f,.20f,.23f,.98f) : FLinearColor(.028f,.058f,.071f,.96f));
            if (Focus || Selected)
                Rect(P.X + 3, P.Y + Size.Y - 3, Size.X - 6, 2, Focus ? Paper : FLinearColor(.31f,.86f,.88f,1));
            Text(Item.Label, P.X + Size.X * .5f, P.Y + Size.Y * .5f - 10, Item.Kind == Vroom::MenuLayout::ItemKind::Track ? 16 : 15, Paper, true);
        }
    };
    const auto& Record = Vroom::Progress::Record(Circuit->TrackIndex, static_cast<int>(Car->Session.Mode));
    if (Car->Session.Phase == Vroom::SessionPhase::Finished)
    {
        Rect(0,0,W,H,FLinearColor(.004f,.01f,.016f,.70f));
        const bool Qualifying=Car->Session.QualifyingRun;
        const bool Standings=Car->Session.Mode==Vroom::SessionMode::GrandPrix&&!Qualifying;
        const float PanelWidth=Standings?1060.f:700.f,PanelHeight=Standings?570.f:430.f;
        const float X=W*.5f-PanelWidth*.5f,Y=H*.5f-(Standings?285.f:220.f);
        Rounded(X,Y,PanelWidth,PanelHeight,14,Panel);Rect(X+30,Y+28,48,4,RacingRed);
        const FString Title=Car->Session.ChampionshipComplete?TEXT("CHAMPIONNAT TERMINÉ")
            :Car->Session.Retired?TEXT("ABANDON · PANNE SÈCHE"):Qualifying?TEXT("QUALIFICATIONS")
            :Car->Session.Mode==Vroom::SessionMode::Arcade?(Car->Session.Won?TEXT("OBJECTIF ATTEINT"):TEXT("FIN DE COURSE")):TEXT("ARRIVÉE");
        Text(Title,X+30,Y+53,29);Text(TrackName,X+32,Y+97,15,Muted);
        if(Qualifying)
        {
            Text(FString::Printf(TEXT("P%d SUR LA GRILLE"),Car->Session.QualificationPosition),X+30,Y+141,35);
            Text(Car->Session.Won?LapTime(Car->Session.QualificationTime):TEXT("TOUR INVALIDÉ · DÉPART EN FOND DE GRILLE"),X+32,Y+194,17,Car->Session.Won?Paper:RacingRed);
        }
        else if(Standings)
        {
            const FLinearColor Cyan(.31f,.86f,.88f,1.f);
            Text(Car->Session.Retired?TEXT("COURSE · ABANDON"):FString::Printf(TEXT("COURSE · P%d / 13"),Car->Session.Position),X+30,Y+141,29);
            Text(FString::Printf(TEXT("TEMPS  %s"),*LapTime(Car->Session.FinishedTime)),X+32,Y+191,16,Muted);
            Text(FString::Printf(TEXT("GÉNÉRAL  P%d · %d POINTS"),Car->Session.ChampionshipRank(),Car->Session.ChampionshipPoints),X+32,Y+227,21,Cyan);
            if(Car->Session.ChampionshipComplete)
            {
                const int Winner=Car->Session.ChampionshipWinner();
                Text(Winner==12?TEXT("VOUS REMPORTEZ LE CHAMPIONNAT")
                    :FString::Printf(TEXT("CHAMPION · F1-%02d"),Winner+1),X+32,Y+277,19,Winner==12?Cyan:Paper);
            }
            else Text(FString::Printf(TEXT("ÉTAPE %d / 6"),Car->Session.Stage+1),X+32,Y+279,16,Muted);
            Text(TEXT("Les rivaux encore en piste sont classés"),X+32,Y+343,13,Muted);
            Text(TEXT("selon leur avancement à votre arrivée."),X+32,Y+362,13,Muted);
            const float TX=X+520;
            Text(TEXT("CLASSEMENT GÉNÉRAL"),TX,Y+28,17,Cyan);
            Text(TEXT("RG"),TX+12,Y+64,10,Muted);Text(TEXT("PILOTE"),TX+55,Y+64,10,Muted);
            Text(TEXT("COURSE"),TX+230,Y+64,10,Muted);Text(TEXT("PTS"),TX+342,Y+64,10,Muted);Text(TEXT("VIC."),TX+430,Y+64,10,Muted);
            const auto Table=Car->Session.Standings();
            for(int I=0;I<13;++I)
            {
                const auto& Entry=Table[I];const bool Player=Entry.Driver==12;
                const float RowY=Y+87+I*23;
                Rect(TX,RowY,508,21,Player?FLinearColor(.04f,.21f,.23f,.98f):FLinearColor(.035f,.065f,.080f,I%2?.65f:.25f));
                const FLinearColor C=Player?Cyan:Paper;
                Text(FString::FromInt(I+1),TX+12,RowY+3,13,C);
                Text(Player?TEXT("VOUS"):FString::Printf(TEXT("F1-%02d"),Entry.Driver+1),TX+55,RowY+3,13,C);
                const int RaceRank=Car->Session.RaceRanks[Entry.Driver];
                Text(RaceRank>0?FString::Printf(TEXT("P%d"),RaceRank):TEXT("—"),TX+230,RowY+3,13,C);
                Text(FString::FromInt(Entry.Points),TX+342,RowY+3,13,C);
                Text(FString::FromInt(Entry.Wins),TX+430,RowY+3,13,C);
            }
        }
        else
        {
            Text(FString::Printf(TEXT("%d / %d DÉPASSEMENTS"),Car->CarsPassed,Car->Session.TargetPasses),X+30,Y+143,29);
            Text(FString::Printf(TEXT("TEMPS  %s     SCORE  %05d"),*LapTime(Car->Session.FinishedTime),Car->Score),X+32,Y+193,16,Muted);
        }
        MenuButtons();return;
    }
    if (!Circuit->Running)
    {
        Rect(0,0,W,H,FLinearColor(.008f,.012f,.015f,.37f));
        for(int32 I=0;I<32;++I)Rect(I*W*.027f,0,W*.027f+1,H,FLinearColor(.008f,.012f,.015f,.92f*(1.f-I/32.f)));
        Rect(66,77,46,4,RacingRed);Text(TEXT("1991 / RETOUR EN PISTE"),130,69,15);
        Text(TEXT("VROOM"),57,114,126);Text(TEXT("La vitesse. Le rythme. La trajectoire."),67,278,20,Muted);
        Text(TEXT("CHOISIR SA COURSE"),67,H-456,13,Muted);
        const TCHAR* Descriptions[] = {TEXT("Dépasse les adversaires avant la fin du chrono. Six étapes à enchaîner."),
            TEXT("Un tour de qualification, une grille de 13 voitures, puis trois tours de course."),
            TEXT("Seul en piste : améliore ton record et poursuis le fantôme de ton meilleur tour.")};
        Text(Descriptions[static_cast<int>(Car->Session.Mode)],67,H-347,14,Muted);
        Text(TEXT("DIFFICULTÉ"),67,H-324,12,Muted);
        Text(Record.Best>0?FString::Printf(TEXT("RECORD PERSONNEL  %s"),*LapTime(Record.Best)):TEXT("RECORD PERSONNEL  —  À ÉTABLIR"),67,H-235,17,Paper);
        if(!Record.History.IsEmpty())
        {
            FString History=TEXT("DERNIERS TOURS  ");
            for(int I=0;I<FMath::Min(3,Record.History.Num());++I)History+=LapTime(Record.History[I])+(I<2?TEXT("   "):TEXT(""));
            Text(History,67,H-207,12,Muted);
        }
        const float CardX=W-437,CardY=90;
        Rounded(CardX,CardY,369,614,10,Panel);Rect(CardX+15,CardY,339,3,RacingRed);
        Text(TEXT("LES SIX CIRCUITS"),CardX+24,CardY+24,18);Text(TEXT("CLIC / F1–F6 / MANETTE"),CardX+24,CardY+54,12,Muted);
        Map(CardX+33,CardY+91,303,192,false);
        MenuButtons();
        Text(FString::Printf(TEXT("FLÈCHES / CROIX : CHOISIR    %s / A : VALIDER    %s / B : RETOUR"),*Vroom::Settings::BindingLabel(Vroom::Action::Start),*Vroom::Settings::BindingLabel(Vroom::Action::Menu)),67,H-72,12,Muted);
        Text(FString::Printf(TEXT("V%s · %s · %s"),Vroom::AppVersion,*TrackName,*Distance),67,H-43,12,Muted);
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
        Rect(0,0,W,H,FLinearColor(.008f,.012f,.015f,.64f));
        const float X=W*.5f-260,Y=H*.5f-210;
        Rounded(X,Y,520,395,12,Panel);Rect(X+35,Y+28,46,4,RacingRed);
        Text(TEXT("PAUSE"),X+35,Y+49,35);
        MenuButtons();return;
    }

    const FLinearColor Detail(0.55f, 0.72f, 0.78f, 1);
    Text(FString::Printf(TEXT("V%s"), Vroom::AppVersion), W - 64, H - 30, 12, Detail);
    static double LastFpsTime = 0; static float ShownFps = 60.f;
    {
        const double Now = FPlatformTime::Seconds();
        if (LastFpsTime > 0)
        {
            const float Dt = static_cast<float>(Now - LastFpsTime);
            if (Dt > 0) ShownFps = ShownFps * 0.9f + (1.f / Dt) * 0.1f;
        }
        LastFpsTime = Now;
    }
    Text(FString::Printf(TEXT("%d FPS"), FMath::RoundToInt(ShownFps)), W - 194, H - 29, 11, Muted);

    const bool NeedsRecovery = Contact.Phase == Vroom::ContactPhase::Crashed
        || Contact.Phase == Vroom::ContactPhase::Recovering;
    // 84 px in the 1440 x 900 design space; all original race indicators remain.
    const FLinearColor Ice(.37f,.89f,.90f,1.f),Silver(.69f,.77f,.79f,1.f),Hairline(.58f,.81f,.83f,.16f);
    Rounded(22,18,W-44,84,12,Hairline);Rounded(23,19,W-46,82,11,FLinearColor(.007f,.024f,.036f,.77f));
    Map(34,23,104,71,true);
    const bool Starting=Circuit->RaceClock<0;
    const FLinearColor LampColors[]={RacingRed,FLinearColor(.96f,.67f,.23f,1),FLinearColor(.39f,.91f,.59f,1)};
    const bool LampOn[]={Starting||NeedsRecovery,Starting?Circuit->RaceClock>=-2:Car->Drive.Skidding,!Starting&&!NeedsRecovery};
    for(int I=0;I<3;++I)
    {
        FLinearColor Color=LampColors[I];Color.A=LampOn[I]?1.f:.18f;
        FCanvasNGonItem Light(FVector2D(154*Scale,(36+I*23)*Scale),FVector2D(4.5f*Scale),20,Color);Canvas->DrawItem(Light);
    }
    const TCHAR*ModeNames[]={TEXT("ARCADE"),TEXT("GRAND PRIX"),TEXT("CHRONO")};
    Text(FString::Printf(TEXT("%s / %s"),ModeNames[static_cast<int>(Car->Session.Mode)],*TrackName),177,29,10,Ice);
    const FString CircuitTitle=Track&&!Track->CircuitName.IsEmpty()?Track->CircuitName:TrackName;
    Text(CircuitTitle,176,45,24);Text(Distance,178,79,10,Silver);
    for(float X:{440.f,790.f,W-201})Line(FVector2D(X,32),FVector2D(X,88),Hairline);
    Text(Car->Session.Mode==Vroom::SessionMode::GrandPrix?TEXT("POINTS"):TEXT("SCORE"),460,29,9,Silver);
    Text(FString::Printf(TEXT("%05d"),FMath::Clamp(Car->Session.Mode==Vroom::SessionMode::GrandPrix?Car->Session.ChampionshipPoints:Car->Score,0,99999)),458,43,26);
    Text(TEXT("STAGE"),598,29,9,Silver);Text(FString::Printf(TEXT("%02d"),Car->Session.Stage+1),597,43,26);
    Text(TEXT("TOUR"),674,29,9,Silver);Text(Car->Session.LapTarget>0?FString::Printf(TEXT("%d/%d"),FMath::Min(Car->ValidLaps+1,Car->Session.LapTarget),Car->Session.LapTarget):FString::Printf(TEXT("%02d"),Car->Lap),673,43,26);
    const FString Status=NeedsRecovery?TEXT("RETOUR EN PISTE"):Car->Session.QualifyingRun?TEXT("QUALIFICATION")
        :Car->Session.Solo()?TEXT("CONTRE LA MONTRE"):FString::Printf(TEXT("POSITION %02d / 13"),Car->Session.Position);
    Text(Status,461,79,10,NeedsRecovery?RacingRed:Ice);
    const float PassX=W-621;
    if(Car->Session.Mode==Vroom::SessionMode::Arcade)
    {
        Text(TEXT("DÉPASSEMENTS"),PassX,29,9,Silver);
        Text(FString::Printf(TEXT("%02d"),Car->CarsPassed),PassX+245,27,14,Ice);
        const int Passed=FMath::Clamp(Car->CarsPassed,0,27);
        for(int I=0;I<27;++I)
        {
            const float X=PassX+(I%9)*28,Y=51+(I/9)*13;const FLinearColor C=I<Passed?Ice:FLinearColor(.38f,.62f,.66f,.25f);
            Rect(X,Y,17,3,C);Rect(X+5,Y-3,7,9,C);Rect(X+1,Y-2,3,2,C);Rect(X+13,Y-2,3,2,C);
        }
    }
    else if(Car->Session.QualifyingRun)
    {
        Text(TEXT("QUALIFICATION"),PassX,29,9,Silver);
        Text(TEXT("UN TOUR PROPRE"),PassX,46,18,Car->LapClean?Ice:RacingRed);
        Text(Record.Best>0?FString::Printf(TEXT("RECORD  %s"),*LapTime(Record.Best)):TEXT("DÉTERMINE VOTRE PLACE SUR LA GRILLE"),PassX,79,10,Silver);
    }
    else if(Car->Session.Mode==Vroom::SessionMode::GrandPrix)
    {
        Text(TEXT("POSITION EN COURSE"),PassX,29,9,Silver);
        Text(FString::Printf(TEXT("P%d / 13"),Car->Session.Position),PassX,45,22,Ice);
        Text(TEXT("VOUS"),PassX+213,50,10,Ice);
        for(int I=0;I<13;++I)
        {
            const bool Player=I+1==Car->Session.Position;const float X=PassX+I*20;
            Rect(X,78,17,15,Player?Ice:FLinearColor(.11f,.25f,.29f,.70f));
            Text(FString::FromInt(I+1),X+8.5f,81,8,Player?Panel:Silver,true);
        }
    }
    else
    {
        Text(TEXT("CONTRE LA MONTRE"),PassX,29,9,Silver);
        Text(Record.Best>0?LapTime(Record.Best):TEXT("PREMIER RECORD"),PassX,46,20,Ice);
        Text(Record.Ghost.Num()>1&&Vroom::Settings::Get().GhostEnabled?TEXT("FANTÔME DU MEILLEUR TOUR"):TEXT("ENCHAÎNER LES TOURS PROPRES"),PassX,79,10,Silver);
    }
    const float Fuel=FMath::Clamp(static_cast<float>(Car->Drive.Fuel),0.f,1.f);
    const float Tyres=FMath::Clamp(static_cast<float>(Car->Drive.TyreHealth/2048.),0.f,1.f);
    const auto Budget=Vroom::MakeResourceBudget(!Car->Session.Solo(),Car->Session.Difficulty,
        Circuit->LengthM(),Car->Session.LapTarget);
    const bool FuelWarning=Fuel<Budget.CriticalFuel, TyreWarning=Tyres<Budget.CriticalTyres;
    const float SlotX=W-177;
    const float TyreX=SlotX;
    const float FuelX=SlotX+70;
    const FLinearColor Amber(.96f,.67f,.23f,1);
    const FLinearColor TyreColor = Tyres < .15f ? RacingRed : (TyreWarning ? Amber : Ice);
    const FLinearColor FuelColor = Fuel < .15f ? RacingRed : (FuelWarning ? Amber : Ice);
    Text(TEXT("PNEUS"),TyreX,29,9,Silver);
    Text(FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Tyres*100)),TyreX,43,16,TyreColor);
    Fill(TyreX,81,58,4,Tyres,TyreColor);
    Text(TEXT("ESSENCE"),FuelX,29,9,Silver);
    Text(FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Fuel*100)),FuelX,43,16,FuelColor);
    Fill(FuelX,81,58,4,Fuel,FuelColor);
    FString Alert;
    if(Car->Pit.Servicing)Alert=FString::Printf(TEXT("STAND · %s · %.1f s"),Car->Pit.NeedTyres&&Car->Pit.NeedFuel?TEXT("PNEUS + ESSENCE"):Car->Pit.NeedTyres?TEXT("PNEUS"):TEXT("ESSENCE"),Car->Pit.Remaining);
    else if(!Car->Session.Solo()&&Car->Pit.Available)Alert=FString::Printf(TEXT("STAND · %s PNEUS · %s ESSENCE · %s : LES DEUX"),*Vroom::Settings::BindingLabel(Vroom::Action::Tyres),*Vroom::Settings::BindingLabel(Vroom::Action::Refuel),Car->MouseSteer?TEXT("A / CLIC SOURIS"):TEXT("A"));
    else if(!Car->Session.Solo()&&Car->Pit.InLane)Alert=TEXT("STANDS · S’ARRÊTER DANS L’ALLÉE");
    else if(!Car->Session.Solo()&&(FuelWarning||TyreWarning))Alert=FString::Printf(TEXT("%s%sSTANDS À DROITE APRÈS LA LIGNE"),FuelWarning?TEXT("PRÉVOIR ESSENCE · "):TEXT(""),TyreWarning?TEXT("PRÉVOIR PNEUS · "):TEXT(""));
    if(!Alert.IsEmpty()){Rounded(W*.5f-335,114,670,32,8,Panel);Text(Alert,W*.5f,121,13,Car->Pit.InLane?Ice:RacingRed,true);}
    if(Car->Session.Mode==Vroom::SessionMode::Arcade)
    {
        Text(TEXT("OBJECTIF"),W-316,29,9,Silver);
        Text(FString::Printf(TEXT("%d / %d"),Car->CarsPassed,Car->Session.TargetPasses),W-317,45,20,Ice);
        Text(LapTime(Car->Session.RemainingSeconds(Circuit->RaceClock)).Left(5),W-315,79,10,Silver);
    }

    if (Circuit->RaceClock < 0 || Vroom::StartSignalFromClock(Circuit->RaceClock) == Vroom::StartSignal::Green)
    {
        const Vroom::StartSignal Signal = Vroom::StartSignalFromClock(Circuit->RaceClock);
        const float Y = 154.f;
        Rect(W * .5f - 125, Y, 250, 72, Panel);
        const TCHAR* Caption = Signal == Vroom::StartSignal::Green ? TEXT("DÉPART")
            : Signal == Vroom::StartSignal::Orange ? TEXT("PRÊT") : TEXT("ATTENTION");
        Text(Caption, W * .5f, Y + 10, 17, Paper, true);
        const FLinearColor Lit[] = {RacingRed, FLinearColor(.96f,.55f,.08f,1), FLinearColor(.12f,.72f,.18f,1)};
        const FLinearColor Off(.16f, .018f, .012f, 1);
        const int32 LitCount = Signal == Vroom::StartSignal::Green ? 3 : Signal == Vroom::StartSignal::Orange ? 2 : 1;
        for (int32 I = 0; I < 3; ++I)
        {
            FCanvasNGonItem Lamp(FVector2D((W * .5f - 46 + I * 46) * Scale, (Y + 49) * Scale),
                FVector2D(10 * Scale), 24, I < LitCount ? Lit[I] : Off);
            Canvas->DrawItem(Lamp);
        }
    }
    else if (NeedsRecovery)
    {
        Text(TEXT("RETOUR EN PISTE…"), W * .5f, 154, 23, Paper, true);
    }

    // Lap timing remains useful in free practice, outside the original band.
    const double BodyDamage = Car->Drive.Damage.State.Total();
    if (BodyDamage > .005)
    {
        const FLinearColor Colour = BodyDamage > .4 ? RacingRed : FLinearColor(.97f, .73f, .32f, 1);
        Text(FString::Printf(TEXT("CARROSSERIE  %d %%"), FMath::RoundToInt((1 - BodyDamage) * 100)),
            24, H - 162, 12, Colour);
        Fill(24, H - 141, 150, 4, static_cast<float>(1 - BodyDamage), Colour);
    }
    Text(LapTime(FMath::Max(0.0, Circuit->RaceClock - Car->LapStart)), 24, H - 65, 23);
    const double Best=Record.Best;
    Text(Best>0?FString::Printf(TEXT("RECORD  %s"),*LapTime(Best)):TEXT("PREMIER TOUR"),24,H-35,12,Muted);
    const double LapDistance=Car->Drive.State.DistanceM-(Car->Lap-1)*Circuit->LengthM();
    const auto Delta=Vroom::Progress::LapDelta(Circuit->TrackIndex,static_cast<int>(Car->Session.Mode),LapDistance,FMath::Max(0.,Circuit->RaceClock-Car->LapStart));
    if(Delta.IsSet()&&Car->LapClean)Text(FString::Printf(TEXT("%+.3f s"),Delta.GetValue()),225,H-58,17,Delta.GetValue()>0?RacingRed:Ice);
    if (!Car->LapClean)
    {
        const FString Why = Car->LapInvalidReason.IsEmpty() ? TEXT("TOUR INVALIDÉ")
            : FString::Printf(TEXT("TOUR INVALIDÉ · %s"), *Car->LapInvalidReason.ToUpper());
        Text(Why, 24, H - 90, 12, RacingRed);
    }
    if (Car->LastLapInvalidated && !Car->LastLapInvalidReason.IsEmpty())
        Text(FString::Printf(TEXT("TOUR PRÉCÉDENT : %s"), *Car->LastLapInvalidReason.ToUpper()), 24, H - 112, 10, RacingRed);

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

    const FLinearColor LandingAmber(.97f, .73f, .32f, 1.f);
    if (!NeedsRecovery && Contact.ControlledTyreHop)
    {
        Text(TEXT("STABILISE LA RÉCEPTION"), W * .5f, H * .57f, 17, LandingAmber, true);
        Fill(W * .5f - 100, H * .57f + 29, 200, 5, static_cast<float>(Contact.LandingControl), LandingAmber);
    }
    else if (!NeedsRecovery && Car->GetLandingFeedbackTime() > 0)
        Text(Car->GetLandingQuality() >= .8 ? TEXT("RÉCEPTION PROPRE") : TEXT("RÉCEPTION À STABILISER"),
            W * .5f, H * .57f, 17, Car->GetLandingQuality() >= .8 ? Ice : LandingAmber, true);
    else if (!NeedsRecovery && (Car->Drive.Drifting || Car->Drive.BoostRemaining > 0))
    {
        const bool Boost = Car->Drive.BoostRemaining > 0 && !Car->Drive.Drifting;
        const int32 Tier = Boost ? Car->Drive.BoostTier : Car->Drive.DriftChargeTier();
        const FLinearColor Accent = Tier >= 3 ? FLinearColor(.87f,.36f,1.f,1.f)
            : Tier >= 2 ? FLinearColor(1.f,.67f,.18f,1.f) : FLinearColor(.26f,.80f,1.f,1.f);
        const float X = W * .5f - 130, Y = H * .67f;
        const float Charge = static_cast<float>(Car->Drive.DriftCharge);
        FString Feedback = TEXT("DÉRAPAGE");
        if (Boost)
        {
            Feedback = FString::Printf(TEXT("TURBO %s%s"), Tier >= 3 ? TEXT("III") : Tier == 2 ? TEXT("II") : TEXT("I"),
                Car->Drive.BoostPerfect ? TEXT(" · PARFAIT") : TEXT(""));
            // Bounded peripheral glow: no camera kick or FOV jump on turbo.
            const float Glow = FMath::Min(.12f, float(Car->Drive.BoostRemaining) * .5f);
            Rect(0,H*.30f,4,H*.48f,FLinearColor(Accent.R,Accent.G,Accent.B,Glow));
            Rect(W-4,H*.30f,4,H*.48f,FLinearColor(Accent.R,Accent.G,Accent.B,Glow));
        }
        else if (Car->Drive.DriftControl < .35)
            Feedback = Car->Drive.DriftDirection > 0 ? TEXT("CONTRE-BRAQUE À GAUCHE") : TEXT("CONTRE-BRAQUE À DROITE");
        else if (Car->Drive.DriftPerfectWindowRemaining > 0) Feedback = TEXT("RELÂCHE MAINTENANT : PARFAIT !");
        else if (Tier >= 3) Feedback = TEXT("RELÂCHE : TURBO III");
        else if (Tier >= 2) Feedback = TEXT("RELÂCHE : TURBO II");
        else if (Tier >= 1) Feedback = TEXT("RELÂCHE : TURBO I");
        else Feedback = TEXT("GLISSE MAÎTRISÉE");
        Text(Feedback, W * .5f, Y, 17, Accent, true);
        Fill(X, Y + 29, 260, 5, Boost ? static_cast<float>(Car->Drive.BoostRemaining / Car->Drive.BoostDurationSeconds())
            : Charge / Vroom::DriveModel::DriftMaxCharge, Accent);
        if (!Boost)
            for (float Threshold : {.42f, 1.f, 1.8f})
                Rect(X + 260 * Threshold / Vroom::DriveModel::DriftMaxCharge, Y + 27, 2, 9, Paper);
    }
}
