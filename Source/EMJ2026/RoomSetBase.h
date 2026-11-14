#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomSetBase.generated.h"
class UDynamicMeshComponent;
UCLASS()
class EMJ2026_API ARoomSetBase : public AActor
{
    GENERATED_BODY()
public:
    ARoomSetBase();
    UPROPERTY(EditAnywhere, Category="Soundstage", meta=(ClampMin="0",ClampMax="7")) int32 RoomId=0;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UDynamicMeshComponent> SetMesh;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    void ApplyContainmentBarrier(bool Sealed);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UDynamicMeshComponent> ContainmentBarrier;
private:
    struct FSolid { FVector Center,Size; float Yaw; };
    TArray<FSolid> Solids;
    void BuildReactor();
    void BuildBridge();
    void BuildMedical();
    void BuildObservation();
    void BuildQuarters();
    void BuildArchive();
    void BuildAirlock();
    void BuildCorridor();
    void Block(FVector P,FVector Size,int32 Material=1,float Yaw=0);
};

UCLASS()
class EMJ2026_API AReactorCoreSet : public ARoomSetBase
{
    GENERATED_BODY()
public:
    AReactorCoreSet() { RoomId=0; }
};

UCLASS()
class EMJ2026_API ARingCorridorSet : public ARoomSetBase
{
    GENERATED_BODY()
public:
    ARingCorridorSet() { RoomId=1; }
};

UCLASS()
class EMJ2026_API ABridgeSet : public ARoomSetBase
{
    GENERATED_BODY()
public:
    ABridgeSet() { RoomId=2; }
};

UCLASS()
class EMJ2026_API AMedBaySet : public ARoomSetBase
{
    GENERATED_BODY()
public:
    AMedBaySet() { RoomId=3; }
};

UCLASS()
class EMJ2026_API AObservationDeckSet : public ARoomSetBase
{
    GENERATED_BODY()
public:
    AObservationDeckSet() { RoomId=4; }
};

UCLASS()
class EMJ2026_API AQuartersMessSet : public ARoomSetBase
{
    GENERATED_BODY()
public:
    AQuartersMessSet() { RoomId=5; }
};

UCLASS()
class EMJ2026_API AServerArchiveSet : public ARoomSetBase
{
    GENERATED_BODY()
public:
    AServerArchiveSet() { RoomId=6; }
};

UCLASS()
class EMJ2026_API AAirlockChamberSet : public ARoomSetBase
{
    GENERATED_BODY()
public:
    AAirlockChamberSet() { RoomId=7; }
};
