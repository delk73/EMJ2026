#pragma once
#include "CoreMinimal.h"

// Plain fixed-capacity simulation. No UObject access, clocks, physics queries or heap use in Step().
enum class EDirectOrScutUnitState : uint8 { Idle, MovingToRoom, Working, Panic, Ejected };
const TCHAR* DirectOrScutStateName(EDirectOrScutUnitState State);

struct FDoorState
{
    int32 RoomA=-1,RoomB=-1;
    FVector2D Position=FVector2D::ZeroVector,Normal=FVector2D::ZeroVector;
    bool bIsOpen=false;
};
struct FCrewProfile
{
    float HeartRateBPM=60, RespirationRateRPM=12, PupilDilationMM=2.5f;
    double CardiacPhase=0, BreathPhase=0;
    uint64 HeartbeatCount=0, BreathCount=0;
    bool OnHeartbeatTick=false, OnBreathCycle=false;
    float Compliance=.7f, Composure=.6f, Affinity=.3f, Utility=.7f, Suspicion=.02f;
};
enum class EHiddenObjectiveType : uint8 { IsolateTargetInRoom, CompleteCovertSystemSweep, SurviveShiftUnexposed };
struct FScenarioEvent { uint64 Tick=0; int32 Room=0; bool bCorporateDirective=false; };
struct FScenarioConfig
{
    FString ScenarioId=TEXT("SCENARIO_01: TELEMETRY RELAY // COVERT CONTAINMENT");
    FString CoverStoryLabel=TEXT("Routine automated telemetry relay maintenance");
    EHiddenObjectiveType HiddenObjectiveType=EHiddenObjectiveType::IsolateTargetInRoom;
    int32 TargetCrew=0, TargetRoom=7;
    float ShiftDuration=600;
    TArray<FScenarioEvent> Events={{900,3,false},{2700,0,true}};
};
struct FCompartmentState { bool bIsPressurized=true, bLockdown=false; float LightLevel=1, AlertSeconds=0; bool bHullBreach=false; };
struct FDirectOrScutUnitState
{
    FVector2D Position=FVector2D::ZeroVector,SuctionVelocity=FVector2D::ZeroVector;
    FVector2D InitialPosition=FVector2D::ZeroVector;
    int32 Room=0,AssignedRoom=0,InitialRoom=0;
    EDirectOrScutUnitState State=EDirectOrScutUnitState::Idle;
    double Stress=0,Integrity=100;
    FCrewProfile Profile;
    double TaskProgress=0;
    bool bAndroid=false,bSuited=false,bRegistered=false;
    int32 ActiveDoor=-1,DestinationRoom=-2,RouteCount=0,RouteCursor=0;
    uint32 RouteRevision=MAX_uint32;
    FVector2D Route[64]{};
    FVector2D RouteTarget=FVector2D::ZeroVector;
    double PanicClock=0;
    int32 PanicRoom=0,CrossFromRoom=-1,CrossToRoom=-1;
    bool bCrossing=false;
    bool bStagingManaged=false,bRoomRouteLocked=false;
    FVector2D WanderTarget=FVector2D::ZeroVector;
};
struct FDirectOrScutObstacle
{
    int32 Room=-1;
    FVector2D Center=FVector2D::ZeroVector,HalfSize=FVector2D::ZeroVector,Axis=FVector2D(1,0);
};

class FDirectOrScutSimulation
{
public:
    static constexpr int32 RoomCount=8,DoorCount=9,MaxBodies=12,MaxObstacles=256,MaxNodes=512;
    static constexpr double FixedDelta=1.0/30.0;
    FCompartmentState Rooms[RoomCount];
    FDoorState Doors[DoorCount];
    FDirectOrScutUnitState Bodies[MaxBodies];
    uint64 TickIndex=0;
    uint32 DoorRevision=0,RandomState=1970;
    double Accumulator=0;
    bool bReady=false;
    bool bExperiment=false;
    FScenarioConfig Scenario;
    float ObjectiveProgress=0;
    bool bShiftComplete=false,bObjectiveFailed=false;
    uint8 SweptRooms=0;
    float CollectiveSuspicion() const;
    bool AssignTask(int32 Unit,int32 Room,bool Pretext);
    void TriggerPretextAlert(int32 Room,bool Breach=false);
    void BroadcastProtocol(int32 Room=-1);
    bool ToggleDoor(int32 Door,bool Open);
    bool CycleAirlock(int32 TargetRoom=7);
    void UpdatePsychology();
    void UpdateBiometrics();
    int32 ObstacleCount=0,NodeCount=0;

    void Initialize();
    void ResetRuntime();
    bool RegisterBody(int32 Index,FVector2D Position,int32 Room,bool Android=false);
    bool AddObstacle(int32 Room,FVector2D Center,FVector2D HalfSize,float Yaw);
    bool BuildNavigation();
    bool SetDoorOpen(int32 Door,bool Open);
    bool AssignRoom(int32 Unit,int32 Room);
    bool ForcePanic(int32 Unit);
    int32 ForceRandomHumanPanic();
    bool CalmUnit(int32 Unit);
    int32 FirstDoorOnPath(int32 From,int32 To) const;
    void RecomputePressure();
    void Advance(double DeltaSeconds);
    void Step();
    double ShuttleETA() const { return 60.0-static_cast<double>(TickIndex%1800)/30.0; }
    bool PointClear(int32 Room,FVector2D Point) const;
    bool SegmentClear(int32 Room,FVector2D A,FVector2D B) const;
    uint64 StateHash() const;

private:
    struct FNode { FVector2D Position=FVector2D::ZeroVector; int32 Room=-1; };
    FDirectOrScutObstacle Obstacles[MaxObstacles];
    FNode Nodes[MaxNodes];
    uint64 Adjacency[MaxNodes][MaxNodes/64]{};
    int32 VacuumDoor[RoomCount]{};
    uint32 NextRandom();
    bool AddNode(int32 Room,FVector2D Position);
    bool PlanRoute(FDirectOrScutUnitState& Unit,FVector2D Target);
    bool FollowRoute(FDirectOrScutUnitState& Unit,double Speed,bool Suction);
    void UpdateBody(FDirectOrScutUnitState& Unit);
    void SeparateThreshold(int32 Door);
};
