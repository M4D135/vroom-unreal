#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "VroomProceduralMeshComponent.generated.h"

// The engine's procedural mesh component does not collect material PSOs.
// Keep its geometry/rendering, adding the same precache lifecycle as static meshes.
UCLASS()
class VROOM_API UVroomProceduralMeshComponent : public UProceduralMeshComponent
{
    GENERATED_BODY()
public:
    virtual void OnRegister() override;
    virtual void CollectPSOPrecacheData(const FPSOPrecacheParams& BaseParams,
        FMaterialInterfacePSOPrecacheParamsList& OutParams) override;
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
};

namespace Vroom::Rendering
{
    // Includes component collection jobs as well as submitted native PSO jobs.
    VROOM_API uint32 PendingPrecache(const UWorld* World);
}
