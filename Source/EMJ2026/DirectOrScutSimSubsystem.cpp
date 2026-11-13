#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutGeometry.h"
#include "DoorActor.h"
#include "CrewUnitBlockout.h"
#include "Engine/World.h"

bool UDirectOrScutSimSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const auto* World=Cast<UWorld>(Outer);
    return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}
void UDirectOrScutSimSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection); Simulation.Initialize();
}
bool UDirectOrScutSimSubsystem::IsTickable() const
{
    return !IsTemplate() && bAutomaticAdvance && Simulation.bReady && GetWorld() && GetWorld()->HasBegunPlay();
}
TStatId UDirectOrScutSimSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UDirectOrScutSimSubsystem,STATGROUP_Tickables); }
void UDirectOrScutSimSubsystem::Tick(float Delta)
{
    Simulation.Advance(Delta); SynchronizeActors();
}
void UDirectOrScutSimSubsystem::RegisterDoor(ADoorActor* Door)
{
    const int32 I=Door->SimDoorIndex;
    if (I<0 || I>=9) return;
    DoorActors[I]=Door; Door->ApplySimulationOpen(Simulation.Doors[I].bIsOpen);
}
void UDirectOrScutSimSubsystem::RegisterCrew(ACrewUnitBlockout* Crew)
{
    const int32 I=Crew->SimUnitIndex;
    if (I<0 || I>=4) return;
    BodyActors[I]=Crew;
    Simulation.RegisterBody(I,FVector2D(Crew->GetActorLocation()),I+2,Crew->bAndroid);
    Simulation.Bodies[I].bSuited=Crew->bSuited;
}
void UDirectOrScutSimSubsystem::RegisterCargo(AActor* Actor,int32 I,int32 Room)
{
    if (I<4 || I>=12) return;
    BodyActors[I]=Actor; Simulation.RegisterBody(I,FVector2D(Actor->GetActorLocation()),Room,false,true);
}
bool UDirectOrScutSimSubsystem::RecordObstacle(int32 Room,FVector Center,FVector Size,float Yaw)
{
    // Only solid landmark sections that intersect a unit's standing silhouette.
    if (Center.Z+Size.Z/2<=10 || Center.Z-Size.Z/2>=180) return true;
    const bool Result=Simulation.AddObstacle(Room,FVector2D(Center),FVector2D(Size.X/2,Size.Y/2),Yaw);
    ensureMsgf(Result,TEXT("DIRECT OR SCUT landmark obstacle capacity exceeded"));
    return Result;
}
void UDirectOrScutSimSubsystem::FinishLayout()
{
    const bool Success=Simulation.BuildNavigation();
    ensureMsgf(Success,TEXT("DIRECT OR SCUT navigation construction failed"));
    UE_LOG(LogTemp,Display,TEXT("SCUT SIM layout: obstacles=%d waypoints=%d ready=%d"),Simulation.ObstacleCount,Simulation.NodeCount,Success);
}
bool UDirectOrScutSimSubsystem::SetDoorOpen(int32 I,bool Open)
{
    if (!Simulation.SetDoorOpen(I,Open)) return false;
    if (auto* Door=DoorActors[I].Get()) Door->ApplySimulationOpen(Open);
    SynchronizeActors(); return true;
}
void UDirectOrScutSimSubsystem::SynchronizeActors()
{
    for (int32 I=0;I<12;++I)
    {
        if (auto* Actor=BodyActors[I].Get())
        {
            const auto& U=Simulation.Bodies[I];
            if (!U.bRegistered) continue;
            Actor->SetActorLocation(FVector(U.Position,0),false);
            if (U.State==EDirectOrScutUnitState::Ejected) Actor->SetActorEnableCollision(false);
            else Actor->SetActorEnableCollision(true);
        }
    }
}
void UDirectOrScutSimSubsystem::AdvanceSteps(int32 Count)
{
    for (int32 I=0;I<Count;++I) Simulation.Step();
    SynchronizeActors();
}
void UDirectOrScutSimSubsystem::ResetSimulation()
{
    Simulation.ResetRuntime();
    for (int32 I=0;I<9;++I) if (auto* Door=DoorActors[I].Get()) Door->ApplySimulationOpen(false);
    SynchronizeActors();
}
ADoorActor* UDirectOrScutSimSubsystem::GetDoorActor(int32 I) const { return I>=0 && I<9?DoorActors[I].Get():nullptr; }
AActor* UDirectOrScutSimSubsystem::GetBodyActor(int32 I) const { return I>=0 && I<12?BodyActors[I].Get():nullptr; }
