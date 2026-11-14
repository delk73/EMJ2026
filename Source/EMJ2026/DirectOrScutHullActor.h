#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DirectOrScutHullActor.generated.h"
class UChildActorComponent;
UCLASS()
class EMJ2026_API ADirectOrScutHullActor : public AActor
{
    GENERATED_BODY()
  public:
    ADirectOrScutHullActor();
    virtual void OnConstruction(const FTransform &Transform) override;
    virtual void BeginPlay() override;
    UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UChildActorComponent>> RoomSets;
};
