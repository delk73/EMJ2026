#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CompartmentCameraRig.generated.h"
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
UCLASS()
class EMJ2026_API ACompartmentCameraRig : public AActor
{
    GENERATED_BODY()
public:
    ACompartmentCameraRig();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    void SetVisibleFeeds(int32 Primary,int32 Alert,bool Grid);
    static int32 RoomForFeed(int32 Feed);
    static int32 FeedForRoom(int32 Room);
    bool bCleanDevRendering=true;
    uint64 CaptureCounts[9]{};
    UPROPERTY() TArray<TObjectPtr<USceneCaptureComponent2D>> Captures;
    UPROPERTY() TArray<TObjectPtr<UTextureRenderTarget2D>> Targets;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> FeedMaterials;
private:
    int32 Active=1,PiP=-1; bool bGrid=true;
    float CaptureClock=0;
};
