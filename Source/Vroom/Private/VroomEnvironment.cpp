#include "VroomEnvironment.h"
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
    }
    for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
    {
        It->GetComponent()->SetFogDensity(Fog[Track]);
        It->GetComponent()->SetStartDistance(9000);
    }
    for (TActorIterator<ASkyLight> It(World); It; ++It)
        It->GetLightComponent()->RecaptureSky();
}
