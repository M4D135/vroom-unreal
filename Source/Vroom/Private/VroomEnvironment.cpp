#include "VroomEnvironment.h"
#include "VroomGraphicsSettings.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"

void Vroom::Environment::ApplyAtmosphere(UWorld* World, int Track)
{
    if (!World) return;
    const bool bPhotorealism = Vroom::Graphics::PhotorealismEnabled();
    Track = FMath::Clamp(Track, 0, 5);
    const float Pitch[] = {-34,-49,-30,-26,-38,-31};
    const float Yaw[] = {-38,-58,24,-20,48,-54};
    const float Fog[] = {.006f,.004f,.0065f,.007f,.008f,.0055f};
    const FLinearColor Sun[] = {FLinearColor(1,.94,.84),FLinearColor(1,.91,.75),
        FLinearColor(1,.91,.78),FLinearColor(.94,.97,1),FLinearColor(1,.95,.87),FLinearColor(.98,.98,1)};
    for (TActorIterator<ADirectionalLight> It(World); It; ++It)
    {
        It->SetActorRotation(FRotator(Pitch[Track],Yaw[Track],0));
        It->GetLightComponent()->SetLightColor(Sun[Track]);
        if (auto* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
        {
            // A stronger key and less uniform sky fill give foliage/rocks
            // depth. These are ordinary raster/VSM light settings on Mac,
            // without hardware ray tracing or an expensive real-time capture.
            Light->SetIntensity(bPhotorealism ? 6.2f : 5.f);
            Light->SetLightSourceAngle(bPhotorealism ? 1.2f : .5357f);
        }
    }
    for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
    {
        auto* Component = It->GetComponent();
        Component->SetFogDensity(Fog[Track] * (bPhotorealism ? .8f : 1.f));
        Component->SetStartDistance(bPhotorealism ? 6000.f : 9000.f);
        Component->SetFogMaxOpacity(bPhotorealism ? .55f : 1.f);
        Component->SetFogInscatteringColor(bPhotorealism
            ? FLinearColor(.18f,.24f,.30f) : FLinearColor::Black);
    }
    for (TActorIterator<ASkyLight> It(World); It; ++It)
    {
        It->GetLightComponent()->SetIntensity(bPhotorealism ? .92f : 1.2f);
        It->GetLightComponent()->RecaptureSky();
    }
}
