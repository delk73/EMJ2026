#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomSetBase.generated.h"
class UDynamicMeshComponent;
enum class EPowerState : uint8;
UCLASS()
class EMJ2026_API ARoomSetBase : public AActor
{
    GENERATED_BODY()
  public:
    ARoomSetBase();
    UPROPERTY(EditAnywhere, Category = "Soundstage", meta = (ClampMin = "0", ClampMax = "7")) int32 RoomId = 0;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UDynamicMeshComponent> SetMesh;
    virtual void OnConstruction(const FTransform &Transform) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    void ApplyContainmentBarrier(bool Sealed);
    void ApplyPowerLighting(EPowerState Power);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UDynamicMeshComponent> ContainmentBarrier;

  private:
    struct FSolid
    {
        FVector Center, Size;
        float Yaw;
    };
    TArray<FSolid> Solids;
    uint8 AppliedLightingPower = 255;
    void BuildReactor();
    void BuildBridge();
    void BuildMedical();
    void BuildObservation();
    void BuildQuarters();
    void BuildArchive();
    void BuildAirlock();
    void BuildCorridor();
    void Block(FVector P, FVector Size, int32 Material = 1, float Yaw = 0);
};
