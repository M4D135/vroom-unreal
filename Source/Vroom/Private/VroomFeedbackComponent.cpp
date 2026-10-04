#include "VroomFeedbackComponent.h"
#include "VroomProceduralMeshComponent.h"
#include "VroomGame.h"
#include "VroomGraphicsSettings.h"
#include "VroomPlayerSettings.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

UVroomFeedbackComponent::UVroomFeedbackComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UVroomFeedbackComponent::BeginPlay()
{
    Super::BeginPlay();
    Car = Cast<AVroomCar>(GetOwner());
    if (!Car) { SetComponentTickEnabled(false); return; }
    AddTickPrerequisiteActor(Car);
    auto Mesh = [&](const TCHAR* Name, const TCHAR* Material)
    {
        auto* M = NewObject<UVroomProceduralMeshComponent>(Car, Name);
        M->SetAbsolute(true, true, true);
        M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        M->SetCastShadow(false);
        M->bAffectDistanceFieldLighting = false;
        M->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, Material));
        M->RegisterComponent();
        return M;
    };
    Marks = Mesh(TEXT("TyreTraces"), TEXT("/Game/Materials/M_TyreTrace"));
    Smoke = Mesh(TEXT("DriftSmoke"), TEXT("/Game/Materials/M_DriftSmoke"));
    Sparks = Mesh(TEXT("DriftSparks"), TEXT("/Game/Materials/M_DriftSpark"));
    Strips.Reserve(512); Puffs.Reserve(48);
    SparkPool.Reserve(48);
    Phases.Reserve(12);
    AudioSamples.SetNumZeroed(1600);
    Wave = NewObject<USoundWaveProcedural>(Car);
    Wave->SetSampleRate(48000); Wave->NumChannels = 2;
    Wave->Duration = INDEFINITELY_LOOPING_DURATION;
    Audio = NewObject<UAudioComponent>(Car, TEXT("RaceStereo"));
    Audio->bAutoActivate = false;
    Audio->SetSound(Wave); Audio->RegisterComponent();
    for (int32 Block = 0; Block < 3; ++Block) UpdateRaceSound();
    Audio->Play();
}

void UVroomFeedbackComponent::ResetForRace()
{
    Strips.Reset(); Puffs.Reset(); SparkPool.Reset();
    StripCursor = PuffCursor = SparkCursor = 0;
    Clock = VisualClock = LastMarkClock = 0;
    ShiftEnvelope = CueEnvelope = 0;
    LastChargeTier = MaxSeenTier = 0;
    WasMarking = SeenMarks = SeenSmoke = SeenBoost = SeenSparks = SeenPerfect = ReportedTest = false;
    StartTone = {};
    if (Marks) Marks->ClearAllMeshSections();
    if (Smoke) Smoke->ClearAllMeshSections();
    if (Sparks) Sparks->ClearAllMeshSections();
}

void UVroomFeedbackComponent::TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt, Type, Function);
    if (!Car || !Car->Circuit) return;
    const bool RaceEnabled = Car->Circuit->Running && !Car->Circuit->Paused && !Car->PreparingCircuit;
    const bool Active = RaceEnabled && Car->Circuit->RaceClock >= 0;
    if (LastTrack != Car->Circuit->TrackIndex)
    {
        LastTrack = Car->Circuit->TrackIndex;
        ResetForRace();
    }
    if (RaceEnabled)
    {
        const Vroom::StartSignal Signal = Vroom::StartSignalFromClock(Car->Circuit->RaceClock);
        if (Car->Circuit->StartSignalPlayed != static_cast<int32>(Signal))
        {
            Car->Circuit->StartSignalPlayed = static_cast<int32>(Signal);
            const auto Beep = Vroom::BeepFor(Signal);
            if (Beep.Duration > 0)
            {
                StartTone.Trigger(Signal);
                UE_LOG(LogTemp, Display, TEXT("VROOM: Start beep signal=%d frequency=%.0f duration=%.2f raceClock=%.3f"),
                    static_cast<int32>(Signal), Beep.Frequency, Beep.Duration, Car->Circuit->RaceClock);
            }
        }
    }
    if (Active) UpdateEffects(FMath::Clamp(Dt, 0.f, .1f));
    else WasMarking = false;
    SeenMarks |= Strips.Num() > 0;
    SeenSmoke |= Puffs.Num() > 0;
    SeenBoost |= Car->Drive.BoostRemaining > 0;
    SeenSparks |= !SparkPool.IsEmpty();
    SeenPerfect |= Car->Drive.BoostPerfect;
    MaxSeenTier = FMath::Max(MaxSeenTier, Car->Drive.BoostTier);
    FString DriftScenario;
    FParse::Value(FCommandLine::Get(), TEXT("VroomDriftScenario="), DriftScenario);
    const double ReportAfter = DriftScenario == TEXT("expert") ? 4.6 : 3.2;
    if (!ReportedTest && Clock >= ReportAfter && FParse::Param(FCommandLine::Get(), TEXT("VroomDriftTest")))
    {
        ReportedTest = true;
        const bool Unskilled = DriftScenario == TEXT("unskilled");
        const bool Straight = DriftScenario == TEXT("straight");
        const bool Passed = Straight ? !SeenMarks && !SeenSmoke && !SeenBoost
            : SeenMarks && SeenSmoke && (SeenBoost != Unskilled) && Marks->GetMaterial(0) && Smoke->GetMaterial(0);
        UE_LOG(LogTemp, Display, TEXT("VROOM: Drift feedback %s marks=%d smoke=%d boost=%d"),
            Passed ? TEXT("PASS") : TEXT("FAIL"), SeenMarks, SeenSmoke, SeenBoost);
        if (Unskilled)
            UE_LOG(LogTemp, Display, TEXT("VROOM: Drift skill denied %s"), !SeenBoost ? TEXT("PASS") : TEXT("FAIL"));
        if (DriftScenario == TEXT("expert"))
        {
            const bool SparkMaterialReady = Sparks && Sparks->GetMaterial(0)
                && Sparks->GetMaterial(0)->GetName() == TEXT("M_DriftSpark");
            UE_LOG(LogTemp, Display, TEXT("VROOM: Expert drift %s tier=%d perfect=%d sparks=%d"),
                MaxSeenTier == 3 && SeenPerfect && SeenSparks && SparkMaterialReady ? TEXT("PASS") : TEXT("FAIL"), MaxSeenTier, SeenPerfect, SeenSparks);
        }
    }
    const auto& Settings = Vroom::Settings::Get();
    Audio->SetVolumeMultiplier(RaceEnabled ? .55f * Settings.MasterVolume * Settings.EffectsVolume : 0.f);
    // Refill a bounded number of small blocks. One block per tick would
    // underrun the procedural stream when the player selects a 30 FPS cap.
    for (int32 Block = 0; Block < 3; ++Block) UpdateRaceSound();
}

void UVroomFeedbackComponent::UpdateEffects(float Dt)
{
    Clock += Dt; VisualClock += Dt;
    // Fixed visual update budget; vertices remain in world space between ticks.
    if (VisualClock < 1.0 / 30.0) return;
    VisualClock = FMath::Fmod(VisualClock, 1.0 / 30.0);
    const auto* Effects = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.EffectsQuality"));
    const int32 Quality = Effects ? Effects->GetInt() : 1;
    const auto& D = Car->Drive;
    const bool OnRoad = FMath::Abs(D.State.LateralM) < Car->Circuit->HalfWidthM;
    const bool Marking = D.Skidding && D.State.SpeedMps > 5 && OnRoad && D.HopHeightM < .03;
    const int32 ChargeTier = D.DriftChargeTier();
    const bool Energized = OnRoad && Quality > 0 && (ChargeTier > 0 || D.BoostRemaining > 0);
    if (Marking) LastMarkClock = Clock;
    if (!Marking && !Energized && SparkPool.IsEmpty() && (Strips.IsEmpty() || Clock - LastMarkClock > 25) && (Puffs.IsEmpty() || Clock - LastMarkClock > 1))
    {
        if (!Strips.IsEmpty()) { Strips.Reset(); StripCursor = 0; Marks->ClearAllMeshSections(); }
        if (!Puffs.IsEmpty()) { Puffs.Reset(); PuffCursor = 0; Smoke->ClearAllMeshSections(); }
        WasMarking = false;
        return;
    }
    const FVector Forward = Car->GetActorForwardVector(), Right = Car->GetActorRightVector();
    const FVector Rear = Car->GetActorLocation() - Forward * 153;
    for (int Side = 0; Side < 2; ++Side)
    {
        FVector P = Rear + Right * (Side ? 91.f : -91.f);
        // Car origin already includes track elevation; keep strips just above asphalt.
        P.Z = Car->Circuit->Position(D.State.DistanceM - 1.53, D.State.LateralM).Z + 2.5;
        if (Marking && WasMarking && FVector::DistSquared(P, LastWheels[Side]) < 90000)
        {
            const FVector W = Right * 15;
            const FStrip Strip{LastWheels[Side] - W, LastWheels[Side] + W, P + W, P - W, Clock};
            if (Strips.Num() < 512) Strips.Add(Strip);
            else { Strips[StripCursor] = Strip; StripCursor = (StripCursor + 1) % 512; }
        }
        LastWheels[Side] = P;
        if (Energized)
        {
            const int32 Tier = D.BoostRemaining > 0 ? D.BoostTier : ChargeTier;
            const float Phase = float(Clock * 71 + Side * 2);
            const FSpark Spark{P + FVector(0, 0, 12),
                -Forward * (D.BoostRemaining > 0 ? 230.f : 95.f) + Right * (Side ? 1.f : -1.f) * (45.f + 35.f * FMath::Sin(Phase))
                    + FVector(0, 0, 90 + 25 * FMath::Cos(Phase)), Clock, D.BoostRemaining > 0 ? 12.f : 7.f, Tier};
            if (SparkPool.Num() < 48) SparkPool.Add(Spark);
            else { SparkPool[SparkCursor] = Spark; SparkCursor = (SparkCursor + 1) % 48; }
        }
        if (Marking && D.SkidAmount > .16 && Quality > 0)
        {
            const FPuff Puff{P + FVector(0, 0, 18), Clock, float(FMath::Clamp(D.SkidAmount, .0, 1.0))};
            if (Puffs.Num() < 48) Puffs.Add(Puff);
            else { Puffs[PuffCursor] = Puff; PuffCursor = (PuffCursor + 1) % 48; }
        }
    }
    WasMarking = Marking;
    TArray<FVector> Vertices, Normals;
    TArray<int32> Indices;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    auto Quad = [&](FVector A, FVector B, FVector C, FVector E, float Alpha)
    {
        int32 Start = Vertices.Num(); Vertices.Append({A, B, C, E});
        Indices.Append({Start, Start + 1, Start + 2, Start, Start + 2, Start + 3});
        UV.Append({FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)});
        for (int I = 0; I < 4; ++I) { Normals.Add(FVector::UpVector); Colors.Add(FLinearColor(1, 1, 1, Alpha)); }
    };
    for (const auto& S : Strips)
    {
        const float Opacity = FMath::Clamp(float((24 - (Clock - S.Birth)) / 5), 0.f, .58f);
        if (Opacity > 0) Quad(S.A, S.B, S.C, S.D, Opacity);
    }
    auto Upload = [&](UProceduralMeshComponent* Mesh)
    {
        if (Vertices.IsEmpty()) { if (Mesh->GetNumSections()) Mesh->ClearAllMeshSections(); return; }
        const auto* Section = Mesh->GetProcMeshSection(0);
        if (Section && Section->ProcVertexBuffer.Num() == Vertices.Num())
            Mesh->UpdateMeshSection_LinearColor(0, Vertices, Normals, UV, Colors, Tangents);
        else Mesh->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, UV, Colors, Tangents, false);
    };
    Upload(Marks);
    Vertices.Reset(); Indices.Reset(); Normals.Reset(); UV.Reset(); Colors.Reset();
    const FVector CameraRight = Car->Camera->GetRightVector(), CameraUp = Car->Camera->GetUpVector();
    for (const auto& P : Puffs)
    {
        const double Age = Clock - P.Birth;
        if (Age > .95) continue;
        const FVector Center = P.Position + FVector(0, 0, Age * 65);
        const double Size = 17 + Age * 68;
        const FVector R = CameraRight * Size, U = CameraUp * Size;
        Quad(Center - R - U, Center + R - U, Center + R + U, Center - R + U,
            P.Strength * float((1 - Age / .95) * .26));
    }
    Upload(Smoke);
    Vertices.Reset(); Indices.Reset(); Normals.Reset(); UV.Reset(); Colors.Reset();
    int32 LiveSparks = 0;
    for (const auto& Spark : SparkPool)
    {
        const double Age = Clock - Spark.Birth;
        if (Age > .36) continue;
        ++LiveSparks;
        const FVector Center = Spark.Position + Spark.Velocity * Age - FVector(0, 0, 190 * Age * Age);
        const FVector R = CameraRight * Spark.Size, U = CameraUp * Spark.Size;
        const int32 Start = Colors.Num();
        const float Fade = 1.f - float(Age / .36);
        Quad(Center - R - U, Center + R - U, Center + R + U, Center - R + U, Fade * .8f);
        const FLinearColor Tint = Spark.Tier >= 3 ? FLinearColor(1.5f,.18f,2.f)
            : Spark.Tier >= 2 ? FLinearColor(2.f,.65f,.06f) : FLinearColor(.10f,.85f,2.f);
        for (int32 I = Start; I < Colors.Num(); ++I) Colors[I] = FLinearColor(Tint.R, Tint.G, Tint.B, Fade * .8f);
    }
    if (!LiveSparks && !Energized) { SparkPool.Reset(); SparkCursor = 0; }
    Upload(Sparks);
}

void UVroomFeedbackComponent::UpdateRaceSound()
{
    // About one output frame per block: the start cue does not sit behind the
    // old 50–100 ms queue. The PCM buffer is reused for the whole race.
    if (!Wave || Wave->GetAvailableAudioByteCount() > 3200) return;
    constexpr int32 Frames = 800;
    auto& Samples = AudioSamples;
    if (!Car || !Car->Circuit)
    {
        FMemory::Memzero(Samples.GetData(), Samples.Num() * sizeof(int16));
        Wave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * 2);
        return;
    }
    auto* Track = Car->Circuit.Get();
    const int32 Count = Track->OpponentS.Num();
    Phases.SetNumZeroed(Count);
    struct FVoice { double Rate, Left, Right; };
    TArray<FVoice, TInlineAllocator<16>> Voices;
    for (int32 I = 0; I < Count; ++I)
    {
        const FVector Local = Car->GetActorTransform().InverseTransformPosition(Track->Position(Track->OpponentS[I], Track->OpponentLanes[I]));
        const double Distance = Local.Size() / 100;
        const double Pan = FMath::Clamp(double(Local.Y) / FMath::Max(180.0, double(Local.Size2D())), -1.0, 1.0);
        const double Gain = Distance < 130 ? .26 / (1 + Distance * Distance / 100) : 0;
        const double Closing = (Track->OpponentSpeeds[I] - Car->Drive.State.SpeedMps) * (Local.X > 0 ? -1 : 1);
        const double Doppler = FMath::Clamp(343.0 / (343 - Closing), .8, 1.25);
        const double Hz = (72 + FMath::Fmod(Track->OpponentSpeeds[I] * 3.1, 110)) * Doppler;
        Voices.Add({Hz * UE_TWO_PI / 48000, Gain * std::sqrt((1 - Pan) * .5), Gain * std::sqrt((1 + Pan) * .5)});
    }
    if (LastGear != Car->Drive.State.Gear) { ShiftEnvelope = 1; LastGear = Car->Drive.State.Gear; }
    const int Tier = Car->Drive.DriftChargeTier();
    if (Tier > LastChargeTier) CueEnvelope = 1;
    LastChargeTier = Tier;
    const double Surface = FMath::Abs(Car->Drive.State.LateralM) > Track->HalfWidthM
        ? FMath::Clamp(Car->Drive.State.SpeedMps / 50, 0.0, 1.0) * .22 : .015 * Car->Drive.State.SpeedMps / 70;
    for (int32 Frame = 0; Frame < Frames; ++Frame)
    {
        double Left = 0, Right = 0;
        for (int32 I = 0; I < Voices.Num(); ++I)
        {
            if (Voices[I].Left == 0 && Voices[I].Right == 0) continue;
            Phases[I] = FMath::Fmod(Phases[I] + Voices[I].Rate, UE_TWO_PI);
            const double Tone = std::sin(Phases[I]) * .62 + std::sin(Phases[I] * 3) * .23 + std::sin(Phases[I] * 6) * .1;
            Left += Tone * Voices[I].Left; Right += Tone * Voices[I].Right;
        }
        NoiseSeed = NoiseSeed * 1664525u + 1013904223u;
        const double Noise = double(NoiseSeed >> 8) / 8388607.5 - 1;
        NoisePhase += (Noise - NoisePhase) * .12;
        CuePhase = FMath::Fmod(CuePhase + (Tier >= 3 ? 1760 : Tier == 2 ? 1320 : 880) * UE_TWO_PI / 48000, UE_TWO_PI);
        const double Effects = NoisePhase * Surface + Noise * ShiftEnvelope * .13
            + std::sin(CuePhase) * CueEnvelope * .07 + StartTone.NextSample() * .58;
        ShiftEnvelope *= .9975; CueEnvelope *= .9994;
        Samples[Frame * 2] = int16(FMath::Clamp((Left + Effects) * 24000, -30000.0, 30000.0));
        Samples[Frame * 2 + 1] = int16(FMath::Clamp((Right + Effects) * 24000, -30000.0, 30000.0));
    }
    Wave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * 2);
}
