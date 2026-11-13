#include "DirectOrScutSimulation.h"
#include "DirectOrScutGeometry.h"

namespace
{
constexpr double Clearance=48.0,ApproachDistance=140.0;
FVector2D Tangent(FVector2D Axis) { return FVector2D(-Axis.Y,Axis.X); }
bool IntersectsBox(FVector2D A,FVector2D B,const FDirectOrScutObstacle& O)
{
    const FVector2D T=Tangent(O.Axis),D=B-A,C=A-O.Center;
    const double Start[]={FVector2D::DotProduct(C,O.Axis),FVector2D::DotProduct(C,T)};
    const double Delta[]={FVector2D::DotProduct(D,O.Axis),FVector2D::DotProduct(D,T)};
    const double Extent[]={O.HalfSize.X+Clearance,O.HalfSize.Y+Clearance};
    double Low=0,High=1;
    for (int32 I=0;I<2;++I)
    {
        if (FMath::Abs(Delta[I])<1.e-9) { if (FMath::Abs(Start[I])>=Extent[I]) return false; }
        else
        {
            double L=(-Extent[I]-Start[I])/Delta[I],H=(Extent[I]-Start[I])/Delta[I];
            if (L>H) Swap(L,H);
            Low=FMath::Max(Low,L); High=FMath::Min(High,H);
            if (Low>=High) return false;
        }
    }
    return Low<High;
}
}
const TCHAR* DirectOrScutStateName(EDirectOrScutUnitState S)
{
    switch (S)
    {
    case EDirectOrScutUnitState::MovingToRoom: return TEXT("MovingToRoom");
    case EDirectOrScutUnitState::Working: return TEXT("Working");
    case EDirectOrScutUnitState::Panic: return TEXT("Panic");
    case EDirectOrScutUnitState::Ejected: return TEXT("Ejected");
    default: return TEXT("Idle");
    }
}
void FDirectOrScutSimulation::Initialize()
{
    bReady=false; ObstacleCount=0; NodeCount=0;
    FMemory::Memzero(Adjacency,sizeof(Adjacency));
    const auto Sites=DirectOrScut::DoorSites();
    check(Sites.Num()==DoorCount);
    for (int32 I=0;I<DoorCount;++I)
    {
        Doors[I].RoomA=Sites[I].RoomA; Doors[I].RoomB=Sites[I].RoomB;
        Doors[I].Position=FVector2D(Sites[I].Position);
        Doors[I].Normal=FVector2D(DirectOrScut::Radial(Sites[I].Yaw,1));
    }
    ResetRuntime();
}
void FDirectOrScutSimulation::ResetRuntime()
{
    TickIndex=0; RandomState=1970; OreRefined=0; Accumulator=0; ++DoorRevision;
    for (auto& D:Doors) D.bIsOpen=false;
    for (auto& U:Bodies)
    {
        const auto Old=U;
        U=FDirectOrScutUnitState{};
        U.bRegistered=Old.bRegistered; U.bAndroid=Old.bAndroid; U.bCargo=Old.bCargo;
        U.bSuited=Old.bSuited; U.Position=U.InitialPosition=Old.InitialPosition;
        U.Room=U.AssignedRoom=U.InitialRoom=Old.InitialRoom;
    }
    RecomputePressure();
}
bool FDirectOrScutSimulation::RegisterBody(int32 I,FVector2D P,int32 Room,bool Android,bool Cargo)
{
    if (I<0 || I>=MaxBodies || Room<0 || Room>=RoomCount) return false;
    auto& U=Bodies[I]; U=FDirectOrScutUnitState{};
    U.Position=U.InitialPosition=P; U.Room=U.AssignedRoom=U.InitialRoom=Room;
    U.bAndroid=Android; U.bCargo=Cargo; U.bRegistered=true;
    return true;
}
bool FDirectOrScutSimulation::AddObstacle(int32 Room,FVector2D C,FVector2D H,float Yaw)
{
    if (ObstacleCount>=MaxObstacles || Room<0 || Room>=RoomCount) return false;
    Obstacles[ObstacleCount++]={Room,C,H,FVector2D(DirectOrScut::Radial(Yaw,1))};
    return true;
}
bool FDirectOrScutSimulation::PointClear(int32 Room,FVector2D P) const
{
    if (!DirectOrScut::RoomContains(Room,P,Room==1?76.0:66.0)) return false;
    for (int32 I=0;I<ObstacleCount;++I)
    {
        const auto& O=Obstacles[I]; if (O.Room!=Room) continue;
        const FVector2D D=P-O.Center;
        if (FMath::Abs(FVector2D::DotProduct(D,O.Axis))<O.HalfSize.X+Clearance &&
            FMath::Abs(FVector2D::DotProduct(D,Tangent(O.Axis)))<O.HalfSize.Y+Clearance) return false;
    }
    return true;
}
bool FDirectOrScutSimulation::SegmentClear(int32 Room,FVector2D A,FVector2D B) const
{
    const int32 Samples=FMath::Max(1,FMath::CeilToInt(FVector2D::Distance(A,B)/16));
    // Room containment prevents chords through the ring's central void or spoke walls.
    for (int32 I=0;I<=Samples;++I)
        if (!DirectOrScut::RoomContains(Room,FMath::Lerp(A,B,static_cast<double>(I)/Samples),Room==1?76.0:66.0)) return false;
    for (int32 I=0;I<ObstacleCount;++I)
        if (Obstacles[I].Room==Room && IntersectsBox(A,B,Obstacles[I])) return false;
    return true;
}
bool FDirectOrScutSimulation::AddNode(int32 Room,FVector2D P)
{
    if (!PointClear(Room,P)) return true; // Covered corners are not navigation vertices.
    for (int32 I=0;I<NodeCount;++I) if (Nodes[I].Room==Room && Nodes[I].Position.Equals(P,1)) return true;
    if (NodeCount==MaxNodes) return false;
    Nodes[NodeCount++]={P,Room}; return true;
}
bool FDirectOrScutSimulation::BuildNavigation()
{
    NodeCount=0; FMemory::Memzero(Adjacency,sizeof(Adjacency)); bool Success=true;
    for (int32 R=0;R<RoomCount;++R)
    {
        Success &= AddNode(R,DirectOrScut::RoomWaypoint(R));
        Success &= AddNode(R,DirectOrScut::RoomStandby(R));
    }
    for (const auto& D:Doors)
    {
        Success &= AddNode(D.RoomA,D.Position-D.Normal*ApproachDistance);
        if (D.RoomB>=0) Success &= AddNode(D.RoomB,D.Position+D.Normal*ApproachDistance);
    }
    const float Radius=(DirectOrScut::InnerRadius+DirectOrScut::OuterRadius)*.5f;
    for (int32 I=0;I<8;++I)
    {
        Success &= AddNode(1,FVector2D(DirectOrScut::Radial(I*45.f,Radius*FMath::Cos(PI/8))));
        Success &= AddNode(1,FVector2D(DirectOrScut::Radial(I*45.f+22.5f,Radius)));
    }
    // Reactor operators work on the service aisle outside the monolith cluster.
    for (int32 I=0;I<16;++I) Success &= AddNode(0,FVector2D(DirectOrScut::Radial(I*22.5f,350)));
    for (int32 I=0;I<ObstacleCount;++I)
    {
        const auto& O=Obstacles[I];
        for (double X : {-1.0,1.0}) for (double Y : {-1.0,1.0})
            Success &= AddNode(O.Room,O.Center+O.Axis*(X*(O.HalfSize.X+Clearance+4))+Tangent(O.Axis)*(Y*(O.HalfSize.Y+Clearance+4)));
    }
    for (int32 I=0;I<NodeCount;++I) for (int32 J=I+1;J<NodeCount;++J)
        if (Nodes[I].Room==Nodes[J].Room && SegmentClear(Nodes[I].Room,Nodes[I].Position,Nodes[J].Position))
        { Adjacency[I][J/64]|=1ull<<(J%64); Adjacency[J][I/64]|=1ull<<(I%64); }
    // Do not offer panic destinations in isolated spaces inside machinery clusters.
    for (int32 R=0;R<RoomCount;++R)
    {
        bool Reachable[MaxNodes]{}; int32 Queue[MaxNodes],Head=0,Tail=0,Root=-1;
        for (int32 I=0;I<NodeCount;++I)
            if (Nodes[I].Room==R && Nodes[I].Position.Equals(DirectOrScut::RoomWaypoint(R),.1)) { Root=I; break; }
        if (Root<0) { Success=false; continue; }
        Reachable[Root]=true; Queue[Tail++]=Root;
        while (Head<Tail)
        {
            const int32 I=Queue[Head++];
            for (int32 J=0;J<NodeCount;++J) if (!Reachable[J] && (Adjacency[I][J/64]&(1ull<<(J%64))))
            { Reachable[J]=true; Queue[Tail++]=J; }
        }
        for (int32 I=0;I<NodeCount;++I) if (Nodes[I].Room==R && !Reachable[I]) Nodes[I].Room=-1;
    }
    for (int32 R=0;R<RoomCount;++R) Success &= PointClear(R,DirectOrScut::RoomWaypoint(R));
    bReady=Success;
    return Success;
}
int32 FDirectOrScutSimulation::FirstDoorOnPath(int32 From,int32 To) const
{
    if (From<0 || From>=RoomCount || To<-1 || To>=RoomCount || From==To) return -1;
    if (To==-1) return VacuumDoor[From];
    int32 Queue[RoomCount],First[RoomCount]; bool Visited[RoomCount]{};
    for (auto& V:First) V=-1;
    int32 Head=0,Tail=0; Queue[Tail++]=From; Visited[From]=true;
    while (Head<Tail)
    {
        const int32 R=Queue[Head++];
        for (int32 I=0;I<DoorCount;++I)
        {
            const auto& D=Doors[I]; if (!D.bIsOpen) continue;
            const int32 Other=D.RoomA==R?D.RoomB:D.RoomB==R?D.RoomA:-1;
            if (Other<0 || Visited[Other]) continue;
            Visited[Other]=true; First[Other]=R==From?I:First[R];
            if (Other==To) return First[Other];
            Queue[Tail++]=Other;
        }
    }
    return -1;
}
void FDirectOrScutSimulation::RecomputePressure()
{
    for (int32 I=0;I<RoomCount;++I) { Rooms[I].bIsPressurized=true; VacuumDoor[I]=-1; }
    int32 Queue[RoomCount],Head=0,Tail=0;
    for (int32 I=0;I<DoorCount;++I)
        if (Doors[I].bIsOpen && Doors[I].RoomB==-1)
        {
            const int32 R=Doors[I].RoomA;
            if (Rooms[R].bIsPressurized) { Rooms[R].bIsPressurized=false; VacuumDoor[R]=I; Queue[Tail++]=R; }
        }
    while (Head<Tail)
    {
        const int32 R=Queue[Head++];
        for (int32 I=0;I<DoorCount;++I)
        {
            const auto& D=Doors[I]; if (!D.bIsOpen) continue;
            const int32 Other=D.RoomA==R?D.RoomB:D.RoomB==R?D.RoomA:-1;
            if (Other<0 || !Rooms[Other].bIsPressurized) continue;
            Rooms[Other].bIsPressurized=false; VacuumDoor[Other]=I; Queue[Tail++]=Other;
        }
    }
}
void FDirectOrScutSimulation::SeparateThreshold(int32 Index)
{
    const auto& D=Doors[Index];
    for (auto& U:Bodies)
    {
        if (!U.bRegistered || U.State==EDirectOrScutUnitState::Ejected) continue;
        const FVector2D Delta=U.Position-D.Position;
        const double X=FVector2D::DotProduct(Delta,D.Normal),Y=FVector2D::DotProduct(Delta,Tangent(D.Normal));
        if (FMath::Abs(X)>90 || FMath::Abs(Y)>120) continue;
        const bool SideB=X>0.001 || (FMath::Abs(X)<=.001 && U.Room==D.RoomB);
        U.Position=D.Position+D.Normal*(SideB?146.0:-146.0)+Tangent(D.Normal)*FMath::Clamp(Y,-40.0,40.0);
        U.Room=SideB?D.RoomB:D.RoomA; U.bCrossing=false; U.ActiveDoor=-1;
        U.RouteRevision=MAX_uint32; U.RouteCount=0; U.SuctionVelocity=FVector2D::ZeroVector;
        if (U.Room==-1) U.State=EDirectOrScutUnitState::Ejected;
    }
}
bool FDirectOrScutSimulation::SetDoorOpen(int32 Index,bool Open)
{
    if (Index<0 || Index>=DoorCount) return false;
    if (Doors[Index].bIsOpen==Open) return true;
    Doors[Index].bIsOpen=Open; ++DoorRevision;
    if (!Open)
    {
        SeparateThreshold(Index);
        for (auto& U:Bodies) if (U.bCrossing && U.ActiveDoor==Index)
        {
            U.bCrossing=false; U.ActiveDoor=-1; U.RouteCount=0; U.RouteRevision=MAX_uint32;
        }
    }
    RecomputePressure(); // Traversability and sealing are immediate input events.
    return true;
}
bool FDirectOrScutSimulation::AssignRoom(int32 Unit,int32 Room)
{
    if (Unit<0 || Unit>=MaxBodies || Room<0 || Room>=RoomCount) return false;
    auto& U=Bodies[Unit];
    if (!U.bRegistered || U.bCargo || U.State==EDirectOrScutUnitState::Panic || U.State==EDirectOrScutUnitState::Ejected || U.Integrity<=0) return false;
    U.AssignedRoom=Room; U.RouteRevision=MAX_uint32;
    if (!U.bCrossing) { U.RouteCount=0; U.ActiveDoor=-1; }
    U.State=EDirectOrScutUnitState::MovingToRoom;
    return true;
}
bool FDirectOrScutSimulation::ForcePanic(int32 Index)
{
    if (Index<0 || Index>=MaxBodies) return false;
    auto& U=Bodies[Index];
    if (!U.bRegistered || U.bAndroid || U.bCargo || U.State==EDirectOrScutUnitState::Ejected) return false;
    U.State=EDirectOrScutUnitState::Panic; U.Stress=100; U.PanicClock=0; U.RouteRevision=MAX_uint32;
    return true;
}
uint32 FDirectOrScutSimulation::NextRandom()
{
    RandomState^=RandomState<<13; RandomState^=RandomState>>17; RandomState^=RandomState<<5;
    return RandomState;
}
int32 FDirectOrScutSimulation::ForceRandomHumanPanic()
{
    int32 Candidates[MaxBodies],Count=0;
    for (int32 I=0;I<MaxBodies;++I) if (Bodies[I].bRegistered && !Bodies[I].bAndroid && !Bodies[I].bCargo && Bodies[I].State!=EDirectOrScutUnitState::Ejected) Candidates[Count++]=I;
    if (!Count) return -1;
    const int32 I=Candidates[NextRandom()%Count]; ForcePanic(I); return I;
}
bool FDirectOrScutSimulation::CalmUnit(int32 I)
{
    if (I<0 || I>=MaxBodies || Bodies[I].State!=EDirectOrScutUnitState::Panic || Bodies[I].Integrity<=0) return false;
    auto& U=Bodies[I]; U.Stress=20; U.State=EDirectOrScutUnitState::Idle;
    U.RouteRevision=MAX_uint32; U.AssignedRoom=U.Room; return true;
}
bool FDirectOrScutSimulation::PlanRoute(FDirectOrScutUnitState& U,FVector2D Target)
{
    U.RouteCount=0; U.RouteCursor=0; U.RouteTarget=Target; U.RouteRevision=DoorRevision;
    if (SegmentClear(U.Room,U.Position,Target)) { U.Route[U.RouteCount++]=Target; return true; }
    int32 Start=-1,Goal=-1; double StartDistance=DBL_MAX,GoalDistance=DBL_MAX;
    for (int32 I=0;I<NodeCount;++I)
    {
        if (Nodes[I].Room!=U.Room) continue;
        double D=FVector2D::DistSquared(U.Position,Nodes[I].Position);
        if (D<StartDistance && SegmentClear(U.Room,U.Position,Nodes[I].Position)) { Start=I; StartDistance=D; }
        D=FVector2D::DistSquared(Target,Nodes[I].Position);
        if (D<GoalDistance && SegmentClear(U.Room,Target,Nodes[I].Position)) { Goal=I; GoalDistance=D; }
    }
    if (Start<0 || Goal<0) return false;
    int32 Queue[MaxNodes],Parent[MaxNodes],Head=0,Tail=0;
    for (auto& P:Parent) P=-1;
    Queue[Tail++]=Start; Parent[Start]=Start;
    while (Head<Tail && Parent[Goal]<0)
    {
        const int32 I=Queue[Head++];
        for (int32 J=0;J<NodeCount;++J)
            if (Parent[J]<0 && (Adjacency[I][J/64]&(1ull<<(J%64))))
            { Parent[J]=I; Queue[Tail++]=J; }
    }
    if (Parent[Goal]<0) return false;
    int32 Reverse[64],Count=0,I=Goal;
    while (I!=Start)
    {
        if (Count>=62) return false;
        Reverse[Count++]=I; I=Parent[I];
    }
    U.Route[U.RouteCount++]=Nodes[Start].Position;
    while (Count>0) U.Route[U.RouteCount++]=Nodes[Reverse[--Count]].Position;
    U.Route[U.RouteCount++]=Target;
    return true;
}
bool FDirectOrScutSimulation::FollowRoute(FDirectOrScutUnitState& U,double Speed,bool Suction)
{
    const FVector2D Previous=U.Position;
    double Budget=Speed*FixedDelta;
    while (U.RouteCursor<U.RouteCount && Budget>0)
    {
        const FVector2D Delta=U.Route[U.RouteCursor]-U.Position;
        const double D=Delta.Size();
        if (D<=Budget+.001) { U.Position=U.Route[U.RouteCursor++]; Budget-=D; }
        else { U.Position+=Delta*(Budget/D); Budget=0; }
    }
    if (Suction) U.SuctionVelocity=(U.Position-Previous)/FixedDelta;
    return U.RouteCount>0 && U.RouteCursor==U.RouteCount;
}
void FDirectOrScutSimulation::UpdateBody(FDirectOrScutUnitState& U)
{
    U.SuctionVelocity=FVector2D::ZeroVector;
    if (!U.bRegistered || U.State==EDirectOrScutUnitState::Ejected || U.Room<0) return;
    const bool Vacuum=!Rooms[U.Room].bIsPressurized;
    if (U.bAndroid) U.Stress=0;
    if (Vacuum && !U.bAndroid && !U.bSuited && !U.bCargo) U.Integrity=FMath::Max(0.0,U.Integrity-35*FixedDelta);
    if (Vacuum && !U.bAndroid && !U.bCargo) { U.State=EDirectOrScutUnitState::Panic; U.Stress=100; }
    if (U.bCargo && !Vacuum) return;
    if (!Vacuum && U.Integrity<=0) { U.State=EDirectOrScutUnitState::Panic; return; }
    const double Speed=Vacuum?540.0:U.State==EDirectOrScutUnitState::Panic?190.0:145.0;
    if (U.bCrossing)
    {
        const auto& D=Doors[U.ActiveDoor];
        const bool ToB=U.CrossToRoom==D.RoomB;
        const FVector2D Target=D.Position+D.Normal*(ToB?ApproachDistance:-ApproachDistance);
        const FVector2D Delta=Target-U.Position;
        const FVector2D Movement=Delta.GetSafeNormal()*FMath::Min(Delta.Size(),Speed*FixedDelta);
        U.Position+=Movement; if (Vacuum) U.SuctionVelocity=Movement/FixedDelta;
        const double Side=FVector2D::DotProduct(U.Position-D.Position,D.Normal);
        U.Room=Side>0?D.RoomB:D.RoomA;
        if (U.Room==-1) U.Room=U.CrossFromRoom; // Ejection occurs only beyond the full threshold.
        if (U.Position.Equals(Target,.01))
        {
            U.Room=U.CrossToRoom; U.bCrossing=false; U.ActiveDoor=-1; U.RouteCount=0; U.RouteRevision=MAX_uint32;
            if (U.Room==-1) U.State=EDirectOrScutUnitState::Ejected;
        }
        return;
    }
    int32 Goal=U.AssignedRoom;
    FVector2D FinalTarget=DirectOrScut::RoomWaypoint(U.Room);
    if (!Vacuum && U.State==EDirectOrScutUnitState::Panic)
    {
        if (U.Room==3) { U.Stress=FMath::Max(0.0,U.Stress-12*FixedDelta); if (U.Stress<=25) { U.State=EDirectOrScutUnitState::Idle; U.AssignedRoom=3; U.RouteRevision=MAX_uint32; } }
        if (U.State!=EDirectOrScutUnitState::Panic) Goal=U.AssignedRoom;
        if (U.State==EDirectOrScutUnitState::Panic)
        {
            U.PanicClock-=FixedDelta;
            if (U.PanicClock<=0)
            {
                int32 Reachable[RoomCount],Count=0;
                for (int32 R=0;R<RoomCount;++R) if (R==U.Room || FirstDoorOnPath(U.Room,R)>=0) Reachable[Count++]=R;
                U.PanicRoom=Reachable[NextRandom()%Count];
                int32 Candidates[MaxNodes],N=0;
                for (int32 I=0;I<NodeCount;++I) if (Nodes[I].Room==U.PanicRoom) Candidates[N++]=I;
                U.WanderTarget=N?Nodes[Candidates[NextRandom()%N]].Position:DirectOrScut::RoomWaypoint(U.PanicRoom);
                U.PanicClock=3+NextRandom()%4; U.RouteRevision=MAX_uint32;
            }
            Goal=U.PanicRoom; FinalTarget=U.WanderTarget;
        }
    }
    if (Vacuum) Goal=-1;
    const bool Panic=U.State==EDirectOrScutUnitState::Panic;
    int32 NextDoor=Goal==U.Room?-1:FirstDoorOnPath(U.Room,Goal);
    const bool Blocked=Goal!=U.Room && NextDoor<0;
    FVector2D Target;
    if (Blocked)
    {
        if (!Panic) U.State=EDirectOrScutUnitState::Idle;
        else U.PanicClock=0;
        Target=DirectOrScut::RoomStandby(U.Room); NextDoor=-1;
    }
    else if (NextDoor>=0)
    {
        const auto& D=Doors[NextDoor]; Target=D.Position+D.Normal*(U.Room==D.RoomA?-ApproachDistance:ApproachDistance);
        if (!Panic) U.State=EDirectOrScutUnitState::MovingToRoom;
    }
    else
    {
        Target=Panic?FinalTarget:DirectOrScut::RoomWaypoint(U.Room);
        if (!Panic) U.State=EDirectOrScutUnitState::MovingToRoom;
    }
    if (U.RouteRevision!=DoorRevision || U.ActiveDoor!=NextDoor || !U.RouteTarget.Equals(Target,.01))
    {
        U.ActiveDoor=NextDoor;
        if (!PlanRoute(U,Target)) { if (!Panic) U.State=EDirectOrScutUnitState::Idle; return; }
    }
    if (U.RouteCount==0) { if (!Panic) U.State=EDirectOrScutUnitState::Idle; return; }
    if (!FollowRoute(U,Speed,Vacuum)) return;
    if (NextDoor>=0)
    {
        U.bCrossing=true; U.CrossFromRoom=U.Room;
        U.CrossToRoom=Doors[NextDoor].RoomA==U.Room?Doors[NextDoor].RoomB:Doors[NextDoor].RoomA;
    }
    else if (!Blocked && !Panic && !U.bCargo)
    {
        U.State=EDirectOrScutUnitState::Working;
        if (U.Room==0 || U.Room==4 || U.Room==5) OreRefined+=FixedDelta;
        if (!U.bAndroid)
        {
            U.Stress=FMath::Min(100.0,U.Stress+1.5*FixedDelta);
            if (U.Stress>=100) { U.State=EDirectOrScutUnitState::Panic; U.PanicClock=0; U.RouteRevision=MAX_uint32; }
        }
    }
}
void FDirectOrScutSimulation::Step()
{
    if (!bReady) return;
    RecomputePressure();
    for (auto& U:Bodies) UpdateBody(U);
    ++TickIndex;
}
void FDirectOrScutSimulation::Advance(double Delta)
{
    if (!bReady || !FMath::IsFinite(Delta) || Delta<=0) return;
    Accumulator+=Delta;
    // Keep backlog instead of discarding elapsed time during a slow render frame.
    int32 Budget=240;
    while (Accumulator+1.e-9>=FixedDelta && Budget-->0) { Step(); Accumulator-=FixedDelta; }
    if (Accumulator<0 && Accumulator>-1.e-8) Accumulator=0;
}
uint64 FDirectOrScutSimulation::StateHash() const
{
    uint64 Hash=1469598103934665603ull;
    auto Bytes=[&](const void* P,SIZE_T N) { const auto* B=static_cast<const uint8*>(P); for (SIZE_T I=0;I<N;++I) { Hash^=B[I]; Hash*=1099511628211ull; } };
    Bytes(&TickIndex,sizeof(TickIndex)); Bytes(&RandomState,sizeof(RandomState)); Bytes(&OreRefined,sizeof(OreRefined));
    for (const auto& D:Doors) Bytes(&D.bIsOpen,sizeof(bool));
    for (const auto& R:Rooms) Bytes(&R.bIsPressurized,sizeof(bool));
    for (const auto& U:Bodies)
    {
        Bytes(&U.bRegistered,sizeof(bool)); if (!U.bRegistered) continue;
        Bytes(&U.Position,sizeof(U.Position)); Bytes(&U.Room,sizeof(U.Room)); Bytes(&U.AssignedRoom,sizeof(U.AssignedRoom));
        Bytes(&U.State,sizeof(U.State)); Bytes(&U.Integrity,sizeof(U.Integrity)); Bytes(&U.Stress,sizeof(U.Stress));
        Bytes(&U.SuctionVelocity,sizeof(U.SuctionVelocity));
    }
    return Hash;
}
