#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutSetAnchor.generated.h"
UENUM()
enum class EStagingSlotRole : uint8
{
    PrimaryConsole,
    SecondaryStation,
    ConversationA,
    ConversationB,
    BlindSpot,
    RecoveryPod,
    AirlockThreshold
};
UCLASS()
class EMJ2026_API ADirectOrScutSetAnchor : public AActor
{
    GENERATED_BODY()
  public:
    ADirectOrScutSetAnchor();
    UPROPERTY(EditAnywhere, Category = "Staging") ERoomId RoomId = ERoomId::ReactorCore;
    UPROPERTY(EditAnywhere, Category = "Staging", meta = (DisplayName = "Role"))
    EStagingSlotRole StagingRole = EStagingSlotRole::PrimaryConsole;
    UPROPERTY(EditAnywhere, Category = "Staging", meta = (ClampMin = "0")) int32 SlotIndex = 0;
    virtual void OnConstruction(const FTransform &Transform) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void BeginPlay() override;
    static void PopulateDefaults(UWorld *World);
};
