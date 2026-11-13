#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DirectOrScutSimulation.h"
#include "DirectOrScutSimSubsystem.generated.h"

class ADoorActor;
class ACrewUnitBlockout;
UCLASS()
class EMJ2026_API UDirectOrScutSimSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override;
    FDirectOrScutSimulation Simulation;
    bool bAutomaticAdvance=true;
    void RegisterDoor(ADoorActor* Door);
    void RegisterCrew(ACrewUnitBlockout* Crew);
    void RegisterCargo(AActor* Actor,int32 Index,int32 Room);
    bool RecordObstacle(int32 Room,FVector Center,FVector Size,float Yaw);
    void FinishLayout();
    bool SetDoorOpen(int32 Index,bool Open);
    bool AssignRoom(int32 Unit,int32 Room) { return Simulation.AssignRoom(Unit,Room); }
    void ResetSimulation();
    void AdvanceSteps(int32 Count);
    void SynchronizeActors();
    ADoorActor* GetDoorActor(int32 Index) const;
    AActor* GetBodyActor(int32 Index) const;
private:
    TWeakObjectPtr<ADoorActor> DoorActors[9];
    TWeakObjectPtr<AActor> BodyActors[12];
};
