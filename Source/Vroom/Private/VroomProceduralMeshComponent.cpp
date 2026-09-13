#include "VroomProceduralMeshComponent.h"
#include "VroomGame.h"
#include "LocalVertexFactory.h"
#include "Materials/Material.h"
#include "PSOPrecache.h"
#include "PipelineStateCache.h"
#include "StaticMeshResources.h"
#include "UObject/UObjectIterator.h"

namespace
{
FVertexDeclarationElementList ProceduralVertexElements()
{
    // Matches FProcMeshProxySection: packed tangents/UVs, four UV channels.
    // This is the native TextRenderComponent dummy declaration pattern; no
    // GPU buffers or render commands are needed to describe the vertex input.
    FStaticMeshVertexBuffers Buffers;
    Buffers.PositionVertexBuffer.Init(1);
    Buffers.StaticMeshVertexBuffer.Init(1, 4);
    Buffers.ColorVertexBuffer.Init(1);
    Buffers.PositionVertexBuffer.VertexPosition(0) = FVector3f::ZeroVector;
    Buffers.StaticMeshVertexBuffer.SetVertexTangents(0, FVector3f(1, 0, 0), FVector3f(0, 1, 0), FVector3f(0, 0, 1));
    for (int32 UV = 0; UV < 4; ++UV)
        Buffers.StaticMeshVertexBuffer.SetVertexUV(0, UV, FVector2f::ZeroVector);
    Buffers.ColorVertexBuffer.VertexColor(0) = FColor::White;
    FLocalVertexFactory::FDataType Data;
    Buffers.PositionVertexBuffer.BindPositionVertexBuffer(nullptr, Data);
    Buffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(nullptr, Data);
    Buffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(nullptr, Data);
    Buffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(nullptr, Data, 0);
    Buffers.ColorVertexBuffer.BindColorVertexBuffer(nullptr, Data);
    FVertexDeclarationElementList Elements;
    FLocalVertexFactory::GetVertexElements(GMaxRHIFeatureLevel, EVertexInputStreamType::Default, false, Data, Elements);
    return Elements;
}
}

void UVroomProceduralMeshComponent::OnRegister()
{
    Super::OnRegister();
    // RegisterComponent creates the render state after OnRegister returns.
    // SetMaterial also calls PrecachePSOs when later sections are populated.
    PrecachePSOs();
}

void UVroomProceduralMeshComponent::CollectPSOPrecacheData(const FPSOPrecacheParams& BaseParams,
    FMaterialInterfacePSOPrecacheParamsList& OutParams)
{
    const FVertexFactoryType* Factory = &FLocalVertexFactory::StaticType;
    const bool ManualFetch = Factory->SupportsManualVertexFetch(GMaxRHIFeatureLevel);
    const FPSOPrecacheVertexFactoryData VertexData = ManualFetch
        ? FPSOPrecacheVertexFactoryData(Factory)
        : FPSOPrecacheVertexFactoryData(Factory, ProceduralVertexElements());
    // Effects assign their material before their first vertices exist. Include
    // those slots now so the first drift does not compile a new PSO mid-race.
    const int32 Count = FMath::Max(GetNumSections(), GetNumOverrideMaterials());
    for (int32 Index = 0; Index < Count; ++Index)
    {
        FMaterialInterfacePSOPrecacheParams Params;
        Params.MaterialInterface = GetMaterial(Index);
        if (!Params.MaterialInterface) Params.MaterialInterface = UMaterial::GetDefaultMaterial(MD_Surface);
        Params.PSOPrecacheParams = BaseParams;
        Params.PSOPrecacheParams.PrimitiveType = PT_TriangleList;
        Params.PSOPrecacheParams.bReverseCulling = GetRenderMatrix().Determinant() < 0;
        Params.VertexFactoryDataList.Add(VertexData);
        AddMaterialInterfacePSOPrecacheParamsToList(Params, OutParams);
    }
}

FPrimitiveSceneProxy* UVroomProceduralMeshComponent::CreateSceneProxy()
{
    if (CheckPSOPrecachingAndBoostPriority()
        && GetPSOPrecacheProxyCreationStrategy() == EPSOPrecacheProxyCreationStrategy::DelayUntilPSOPrecached)
        return nullptr;
    // UPrimitiveComponent's completion callback recreates the render state.
    return Super::CreateSceneProxy();
}

uint32 Vroom::Rendering::PendingPrecache(const UWorld* World)
{
    uint32 Pending = PipelineStateCache::NumActivePrecacheRequests();
    for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
        if (It->GetWorld() == World && It->IsRegistered() && It->IsPSOPrecaching()) ++Pending;
    for (TObjectIterator<AVroomHUD> It; It; ++It)
        if (It->GetWorld() == World && !It->IsActorBeingDestroyed() && !It->IsCanvasPreparationComplete()) ++Pending;
    return Pending;
}
