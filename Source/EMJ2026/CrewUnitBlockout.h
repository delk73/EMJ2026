#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "CrewUnitBlockout.generated.h"
class UDynamicMeshComponent;
struct FDirectOrScutUnitState;
struct FCrewStagingState;
UCLASS()
class EMJ2026_API ACrewUnitBlockout : public APawn
{
    GENERATED_BODY()
  public:
    ACrewUnitBlockout();
    virtual void BeginPlay() override;
    UPROPERTY(EditAnywhere) bool bAndroid = false;
    UPROPERTY(EditAnywhere) bool bSuited = false;
    UPROPERTY(VisibleAnywhere) int32 SimUnitIndex = INDEX_NONE;
    UFUNCTION(BlueprintCallable) bool DirectToRoom(int32 Room);
    const FDirectOrScutUnitState *GetSimulationState() const;
    void ApplyStagingPose(const FCrewStagingState &Staging, uint64 Tick, float BPM);
    UPROPERTY(EditAnywhere) int32 SuitSlot = 3;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UDynamicMeshComponent> Body;
};
