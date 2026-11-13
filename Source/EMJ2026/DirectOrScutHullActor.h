#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DirectOrScutHullActor.generated.h"
class UDynamicMeshComponent;
UCLASS()
class EMJ2026_API ADirectOrScutHullActor : public AActor
{
    GENERATED_BODY()
public:
    ADirectOrScutHullActor();
    virtual void BeginPlay() override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UDynamicMeshComponent> Hull;
private:
    void Generate();
};
