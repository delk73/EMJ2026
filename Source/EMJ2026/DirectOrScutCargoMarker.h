#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DirectOrScutCargoMarker.generated.h"
class UDynamicMeshComponent;
UCLASS()
class EMJ2026_API ADirectOrScutCargoMarker : public AActor
{
    GENERATED_BODY()
public:
    ADirectOrScutCargoMarker();
    virtual void BeginPlay() override;
    UPROPERTY(EditAnywhere) int32 SimBodyIndex=4;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UDynamicMeshComponent> Mesh;
};
