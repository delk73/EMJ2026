#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DoorActor.generated.h"
class UDynamicMeshComponent;
class UBoxComponent;
UCLASS()
class EMJ2026_API ADoorActor : public AActor
{
    GENERATED_BODY()
  public:
    ADoorActor();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    UFUNCTION(BlueprintCallable) void Toggle();
    UFUNCTION(BlueprintCallable) void SetOpen(bool Open);
    void ApplySimulationOpen(bool Open);
    UPROPERTY(VisibleAnywhere) int32 SimDoorIndex = INDEX_NONE;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UDynamicMeshComponent> Slab;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> ClickTarget;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UDynamicMeshComponent> Sill;
    UPROPERTY(EditAnywhere) bool bExterior = false;
    UPROPERTY(VisibleAnywhere) bool bOpen = false;

  private:
    float Slide = 0;
};
