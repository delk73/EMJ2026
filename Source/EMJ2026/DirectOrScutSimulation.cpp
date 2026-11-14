#include "DirectOrScutSimulation.h"
#include "DirectOrScutGeometry.h"

namespace
{
constexpr double Clearance = 48.0, ApproachDistance = 140.0;
FVector2D Tangent(FVector2D Axis)
{
    return FVector2D(-Axis.Y, Axis.X);
}
bool IntersectsBox(FVector2D A, FVector2D B, const FDirectOrScutObstacle &O)
{
    const FVector2D T = Tangent(O.Axis), D = B - A, C = A - O.Center;
    const double Start[] = {FVector2D::DotProduct(C, O.Axis), FVector2D::DotProduct(C, T)};
    const double Delta[] = {FVector2D::DotProduct(D, O.Axis), FVector2D::DotProduct(D, T)};
    const double Extent[] = {O.HalfSize.X + Clearance, O.HalfSize.Y + Clearance};
    double Low = 0, High = 1;
    for (int32 I = 0; I < 2; ++I)
    {
        if (FMath::Abs(Delta[I]) < 1.e-9)
        {
            if (FMath::Abs(Start[I]) >= Extent[I])
            {
                return false;
            }
        }
        else
        {
            double L = (-Extent[I] - Start[I]) / Delta[I], H = (Extent[I] - Start[I]) / Delta[I];
            if (L > H)
            {
                Swap(L, H);
            }
            Low = FMath::Max(Low, L);
            High = FMath::Min(High, H);
            if (Low >= High)
            {
                return false;
            }
        }
    }
    return Low < High;
}
} // namespace
const TCHAR *DirectOrScutStateName(EDirectOrScutUnitState S)
{
    switch (S)
    {
    case EDirectOrScutUnitState::MovingToRoom:
        return TEXT("MovingToRoom");
    case EDirectOrScutUnitState::Working:
        return TEXT("Working");
    case EDirectOrScutUnitState::Panic:
        return TEXT("Panic");
    case EDirectOrScutUnitState::Ejected:
        return TEXT("Ejected");
    default:
        return TEXT("Idle");
    }
}
void FDirectOrScutSimulation::Initialize()
{
    bReady = false;
    ObstacleCount = 0;
    NodeCount = 0;
    FMemory::Memzero(Adjacency, sizeof(Adjacency));
    const auto Sites = DirectOrScut::DoorSites();
    check(Sites.Num() == DoorCount);
    for (int32 I = 0; I < DoorCount; ++I)
    {
        Doors[I].RoomA = Sites[I].RoomA;
        Doors[I].RoomB = Sites[I].RoomB;
        Doors[I].Position = FVector2D(Sites[I].Position);
        Doors[I].Normal = FVector2D(DirectOrScut::Radial(Sites[I].Yaw, 1));
    }
    ResetRuntime();
}
void FDirectOrScutSimulation::ResetRuntime()
{
    ObjectiveProgress = 0;
    bShiftComplete = false;
    bObjectiveFailed = false;
    SweptRooms = 0;
    for (auto &R : Rooms)
    {
        R = FCompartmentState{};
    }
    TickIndex = 0;
    RandomState = 1970;
    Accumulator = 0;
    ++DoorRevision;
    // Interior station graph boots traversable. The exterior vacuum edge stays closed.
    for (auto &D : Doors)
    {
        D.bIsOpen = D.RoomB >= 0;
    }
    for (auto &U : Bodies)
    {
        const auto Old = U;
        U = FDirectOrScutUnitState{};
        U.bRegistered = Old.bRegistered;
        U.bAndroid = Old.bAndroid;
        U.Profile = FCrewProfile{};
        const int32 I = int32(&U - Bodies);
        U.Profile.Compliance = U.bAndroid ? 1.f : .45f + .18f * I;
        U.Profile.Composure = U.bAndroid ? 1.f : .35f + .2f * I;
        U.Profile.Affinity = U.bAndroid ? -.15f : (I == 1 ? -.4f : .7f);
        U.Profile.Utility = U.bAndroid ? 1.f : .6f + .1f * I;
        U.Profile.Suspicion = U.bAndroid ? 0 : .02f;
        U.Profile.RespirationRateRPM = U.bAndroid ? 0 : 12;
        U.bStagingManaged = Old.bStagingManaged;
        U.bRoomRouteLocked = false;
        U.bSuited = Old.bSuited;
        U.Position = U.InitialPosition = Old.InitialPosition;
        U.Room = U.AssignedRoom = U.InitialRoom = Old.InitialRoom;
    }
    RecomputePressure();
}
bool FDirectOrScutSimulation::RegisterBody(int32 I, FVector2D P, int32 Room, bool Android)
{
    if (I < 0 || I >= MaxBodies || Room < 0 || Room >= RoomCount)
    {
        return false;
    }
    auto &U = Bodies[I];
    U = FDirectOrScutUnitState{};
    U.Position = U.InitialPosition = P;
    U.Room = U.AssignedRoom = U.InitialRoom = Room;
    U.bAndroid = Android;
    U.bRegistered = true;
    U.Profile.Compliance = Android ? 1.f : .45f + .18f * I;
    U.Profile.Composure = Android ? 1.f : .35f + .2f * I;
    U.Profile.Affinity = Android ? -.15f : (I == 1 ? -.4f : .7f);
    U.Profile.Utility = Android ? 1.f : .6f + .1f * I;
    U.Profile.Suspicion = Android ? 0 : .02f;
    U.Profile.RespirationRateRPM = Android ? 0 : 12;
    return true;
}
bool FDirectOrScutSimulation::AddObstacle(int32 Room, FVector2D C, FVector2D H, float Yaw)
{
    if (ObstacleCount >= MaxObstacles || Room < 0 || Room >= RoomCount)
    {
        return false;
    }
    Obstacles[ObstacleCount++] = {Room, C, H, FVector2D(DirectOrScut::Radial(Yaw, 1))};
    return true;
}
bool FDirectOrScutSimulation::PointClear(int32 Room, FVector2D P) const
{
    if (!DirectOrScut::RoomContains(Room, P, Room == 1 ? 76.0 : 66.0))
    {
        return false;
    }
    for (int32 I = 0; I < ObstacleCount; ++I)
    {
        const auto &O = Obstacles[I];
        if (O.Room != Room)
        {
            continue;
        }
        const FVector2D D = P - O.Center;
        if (FMath::Abs(FVector2D::DotProduct(D, O.Axis)) < O.HalfSize.X + Clearance &&
            FMath::Abs(FVector2D::DotProduct(D, Tangent(O.Axis))) < O.HalfSize.Y + Clearance)
        {
            return false;
        }
    }
    return true;
}
bool FDirectOrScutSimulation::SegmentClear(int32 Room, FVector2D A, FVector2D B) const
{
    const int32 Samples = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(A, B) / 16));
    // Room containment prevents chords through the ring's central void or spoke walls.
    for (int32 I = 0; I <= Samples; ++I)
    {
        if (!DirectOrScut::RoomContains(Room, FMath::Lerp(A, B, static_cast<double>(I) / Samples),
                                        Room == 1 ? 76.0 : 66.0))
        {
            return false;
        }
    }
    for (int32 I = 0; I < ObstacleCount; ++I)
    {
        if (Obstacles[I].Room == Room && IntersectsBox(A, B, Obstacles[I]))
        {
            return false;
        }
    }
    return true;
}
bool FDirectOrScutSimulation::AddNode(int32 Room, FVector2D P)
{
    if (!PointClear(Room, P))
    {
        return true; // Covered corners are not navigation vertices.
    }
    for (int32 I = 0; I < NodeCount; ++I)
    {
        if (Nodes[I].Room == Room && Nodes[I].Position.Equals(P, 1))
        {
            return true;
        }
    }
    if (NodeCount == MaxNodes)
    {
        return false;
    }
    Nodes[NodeCount++] = {P, Room};
    return true;
}
bool FDirectOrScutSimulation::BuildNavigation()
{
    NodeCount = 0;
    FMemory::Memzero(Adjacency, sizeof(Adjacency));
    bool Success = true;
    for (int32 R = 0; R < RoomCount; ++R)
    {
        Success &= AddNode(R, DirectOrScut::RoomWaypoint(R));
        Success &= AddNode(R, DirectOrScut::RoomStandby(R));
    }
    for (const auto &D : Doors)
    {
        Success &= AddNode(D.RoomA, D.Position - D.Normal * ApproachDistance);
        if (D.RoomB >= 0)
        {
            Success &= AddNode(D.RoomB, D.Position + D.Normal * ApproachDistance);
        }
    }
    const float Radius = (DirectOrScut::InnerRadius + DirectOrScut::OuterRadius) * .5f;
    for (int32 I = 0; I < 8; ++I)
    {
        Success &= AddNode(1, FVector2D(DirectOrScut::Radial(I * 45.f, Radius * FMath::Cos(PI / 8))));
        Success &= AddNode(1, FVector2D(DirectOrScut::Radial(I * 45.f + 22.5f, Radius)));
    }
    // Reactor operators work on the service aisle outside the monolith cluster.
    for (int32 I = 0; I < 16; ++I)
    {
        Success &= AddNode(0, FVector2D(DirectOrScut::Radial(I * 22.5f, 350)));
    }
    for (int32 I = 0; I < ObstacleCount; ++I)
    {
        const auto &O = Obstacles[I];
        for (double X : {-1.0, 1.0})
        {
            for (double Y : {-1.0, 1.0})
            {
                Success &= AddNode(O.Room, O.Center + O.Axis * (X * (O.HalfSize.X + Clearance + 4)) +
                                               Tangent(O.Axis) * (Y * (O.HalfSize.Y + Clearance + 4)));
            }
        }
    }
    for (int32 I = 0; I < NodeCount; ++I)
    {
        for (int32 J = I + 1; J < NodeCount; ++J)
        {
            if (Nodes[I].Room == Nodes[J].Room && SegmentClear(Nodes[I].Room, Nodes[I].Position, Nodes[J].Position))
            {
                Adjacency[I][J / 64] |= 1ull << (J % 64);
                Adjacency[J][I / 64] |= 1ull << (I % 64);
            }
        }
    }
    // Do not offer panic destinations in isolated spaces inside machinery clusters.
    for (int32 R = 0; R < RoomCount; ++R)
    {
        bool Reachable[MaxNodes]{};
        int32 Queue[MaxNodes], Head = 0, Tail = 0, Root = -1;
        for (int32 I = 0; I < NodeCount; ++I)
        {
            if (Nodes[I].Room == R && Nodes[I].Position.Equals(DirectOrScut::RoomWaypoint(R), .1))
            {
                Root = I;
                break;
            }
        }
        if (Root < 0)
        {
            Success = false;
            continue;
        }
        Reachable[Root] = true;
        Queue[Tail++] = Root;
        while (Head < Tail)
        {
            const int32 I = Queue[Head++];
            for (int32 J = 0; J < NodeCount; ++J)
            {
                if (!Reachable[J] && (Adjacency[I][J / 64] & (1ull << (J % 64))))
                {
                    Reachable[J] = true;
                    Queue[Tail++] = J;
                }
            }
        }
        for (int32 I = 0; I < NodeCount; ++I)
        {
            if (Nodes[I].Room == R && !Reachable[I])
            {
                Nodes[I].Room = -1;
            }
        }
    }
    for (int32 R = 0; R < RoomCount; ++R)
    {
        Success &= PointClear(R, DirectOrScut::RoomWaypoint(R));
    }
    bReady = Success;
    return Success;
}
int32 FDirectOrScutSimulation::FirstDoorOnPath(int32 From, int32 To) const
{
    if (From < 0 || From >= RoomCount || To < -1 || To >= RoomCount || From == To)
    {
        return -1;
    }
    if (To == -1)
    {
        return VacuumDoor[From];
    }
    int32 Queue[RoomCount], First[RoomCount];
    bool Visited[RoomCount]{};
    for (auto &V : First)
    {
        V = -1;
    }
    int32 Head = 0, Tail = 0;
    Queue[Tail++] = From;
    Visited[From] = true;
    while (Head < Tail)
    {
        const int32 R = Queue[Head++];
        for (int32 I = 0; I < DoorCount; ++I)
        {
            const auto &D = Doors[I];
            if (!D.bIsOpen)
            {
                continue;
            }
            const int32 Other = D.RoomA == R ? D.RoomB : D.RoomB == R ? D.RoomA : -1;
            if (Other < 0 || Visited[Other])
            {
                continue;
            }
            Visited[Other] = true;
            First[Other] = R == From ? I : First[R];
            if (Other == To)
            {
                return First[Other];
            }
            Queue[Tail++] = Other;
        }
    }
    return -1;
}
void FDirectOrScutSimulation::RecomputePressure()
{
    for (int32 I = 0; I < RoomCount; ++I)
    {
        Rooms[I].bIsPressurized = true;
        VacuumDoor[I] = -1;
    }
    int32 Queue[RoomCount], Head = 0, Tail = 0;
    for (int32 I = 0; I < DoorCount; ++I)
    {
        if (Doors[I].bIsOpen && Doors[I].RoomB == -1)
        {
            const int32 R = Doors[I].RoomA;
            if (Rooms[R].bIsPressurized)
            {
                Rooms[R].bIsPressurized = false;
                VacuumDoor[R] = I;
                Queue[Tail++] = R;
            }
        }
    }
    while (Head < Tail)
    {
        const int32 R = Queue[Head++];
        for (int32 I = 0; I < DoorCount; ++I)
        {
            const auto &D = Doors[I];
            if (!D.bIsOpen)
            {
                continue;
            }
            const int32 Other = D.RoomA == R ? D.RoomB : D.RoomB == R ? D.RoomA : -1;
            if (Other < 0 || !Rooms[Other].bIsPressurized)
            {
                continue;
            }
            Rooms[Other].bIsPressurized = false;
            VacuumDoor[Other] = I;
            Queue[Tail++] = Other;
        }
    }
}
void FDirectOrScutSimulation::SeparateThreshold(int32 Index)
{
    const auto &D = Doors[Index];
    for (auto &U : Bodies)
    {
        if (!U.bRegistered || U.State == EDirectOrScutUnitState::Ejected)
        {
            continue;
        }
        const FVector2D Delta = U.Position - D.Position;
        const double X = FVector2D::DotProduct(Delta, D.Normal), Y = FVector2D::DotProduct(Delta, Tangent(D.Normal));
        if (FMath::Abs(X) > 90 || FMath::Abs(Y) > 120)
        {
            continue;
        }
        const bool SideB = X > 0.001 || (FMath::Abs(X) <= .001 && U.Room == D.RoomB);
        U.Position =
            D.Position + D.Normal * (SideB ? 146.0 : -146.0) + Tangent(D.Normal) * FMath::Clamp(Y, -40.0, 40.0);
        U.Room = SideB ? D.RoomB : D.RoomA;
        U.bCrossing = false;
        U.ActiveDoor = -1;
        U.RouteRevision = MAX_uint32;
        U.RouteCount = 0;
        U.SuctionVelocity = FVector2D::ZeroVector;
        if (U.Room == -1)
        {
            U.State = EDirectOrScutUnitState::Ejected;
        }
    }
}
bool FDirectOrScutSimulation::SetDoorOpen(int32 Index, bool Open)
{
    if (Index < 0 || Index >= DoorCount)
    {
        return false;
    }
    if (Doors[Index].bIsOpen == Open)
    {
        return true;
    }
    Doors[Index].bIsOpen = Open;
    ++DoorRevision;
    if (!Open)
    {
        SeparateThreshold(Index);
        for (auto &U : Bodies)
        {
            if (U.bCrossing && U.ActiveDoor == Index)
            {
                U.bCrossing = false;
                U.ActiveDoor = -1;
                U.RouteCount = 0;
                U.RouteRevision = MAX_uint32;
            }
        }
    }
    RecomputePressure(); // Traversability and sealing are immediate input events.
    return true;
}
bool FDirectOrScutSimulation::AssignRoom(int32 Unit, int32 Room)
{
    if (Unit < 0 || Unit >= MaxBodies || Room < 0 || Room >= RoomCount)
    {
        return false;
    }
    auto &U = Bodies[Unit];
    if (!U.bRegistered || U.bRoomRouteLocked || U.State == EDirectOrScutUnitState::Panic ||
        U.State == EDirectOrScutUnitState::Ejected || U.Integrity <= 0)
    {
        return false;
    }
    U.TaskProgress = 0;
    U.AssignedRoom = Room;
    U.RouteRevision = MAX_uint32;
    if (!U.bCrossing)
    {
        U.RouteCount = 0;
        U.ActiveDoor = -1;
    }
    U.State = EDirectOrScutUnitState::MovingToRoom;
    return true;
}
bool FDirectOrScutSimulation::ForcePanic(int32 Index)
{
    if (Index < 0 || Index >= MaxBodies)
    {
        return false;
    }
    auto &U = Bodies[Index];
    if (!U.bRegistered || U.bAndroid || U.State == EDirectOrScutUnitState::Ejected)
    {
        return false;
    }
    U.State = EDirectOrScutUnitState::Panic;
    U.Stress = 100;
    U.PanicClock = 0;
    U.RouteRevision = MAX_uint32;
    return true;
}
uint32 FDirectOrScutSimulation::NextRandom()
{
    RandomState ^= RandomState << 13;
    RandomState ^= RandomState >> 17;
    RandomState ^= RandomState << 5;
    return RandomState;
}
int32 FDirectOrScutSimulation::ForceRandomHumanPanic()
{
    int32 Candidates[MaxBodies], Count = 0;
    for (int32 I = 0; I < MaxBodies; ++I)
    {
        if (Bodies[I].bRegistered && !Bodies[I].bAndroid && Bodies[I].State != EDirectOrScutUnitState::Ejected)
        {
            Candidates[Count++] = I;
        }
    }
    if (!Count)
    {
        return -1;
    }
    const int32 I = Candidates[NextRandom() % Count];
    ForcePanic(I);
    return I;
}
bool FDirectOrScutSimulation::CalmUnit(int32 I)
{
    if (I < 0 || I >= MaxBodies || Bodies[I].State != EDirectOrScutUnitState::Panic || Bodies[I].Integrity <= 0)
    {
        return false;
    }
    auto &U = Bodies[I];
    U.Stress = 20;
    U.State = EDirectOrScutUnitState::Idle;
    U.RouteRevision = MAX_uint32;
    U.AssignedRoom = U.Room;
    return true;
}
bool FDirectOrScutSimulation::PlanRoute(FDirectOrScutUnitState &U, FVector2D Target)
{
    U.RouteCount = 0;
    U.RouteCursor = 0;
    U.RouteTarget = Target;
    U.RouteRevision = DoorRevision;
    if (SegmentClear(U.Room, U.Position, Target))
    {
        U.Route[U.RouteCount++] = Target;
        return true;
    }
    int32 Start = -1, Goal = -1;
    double StartDistance = DBL_MAX, GoalDistance = DBL_MAX;
    for (int32 I = 0; I < NodeCount; ++I)
    {
        if (Nodes[I].Room != U.Room)
        {
            continue;
        }
        double D = FVector2D::DistSquared(U.Position, Nodes[I].Position);
        if (D < StartDistance && SegmentClear(U.Room, U.Position, Nodes[I].Position))
        {
            Start = I;
            StartDistance = D;
        }
        D = FVector2D::DistSquared(Target, Nodes[I].Position);
        if (D < GoalDistance && SegmentClear(U.Room, Target, Nodes[I].Position))
        {
            Goal = I;
            GoalDistance = D;
        }
    }
    if (Start < 0 || Goal < 0)
    {
        return false;
    }
    int32 Queue[MaxNodes], Parent[MaxNodes], Head = 0, Tail = 0;
    for (auto &P : Parent)
    {
        P = -1;
    }
    Queue[Tail++] = Start;
    Parent[Start] = Start;
    while (Head < Tail && Parent[Goal] < 0)
    {
        const int32 I = Queue[Head++];
        for (int32 J = 0; J < NodeCount; ++J)
        {
            if (Parent[J] < 0 && (Adjacency[I][J / 64] & (1ull << (J % 64))))
            {
                Parent[J] = I;
                Queue[Tail++] = J;
            }
        }
    }
    if (Parent[Goal] < 0)
    {
        return false;
    }
    int32 Reverse[64], Count = 0, I = Goal;
    while (I != Start)
    {
        if (Count >= 62)
        {
            return false;
        }
        Reverse[Count++] = I;
        I = Parent[I];
    }
    U.Route[U.RouteCount++] = Nodes[Start].Position;
    while (Count > 0)
    {
        U.Route[U.RouteCount++] = Nodes[Reverse[--Count]].Position;
    }
    U.Route[U.RouteCount++] = Target;
    return true;
}
bool FDirectOrScutSimulation::FollowRoute(FDirectOrScutUnitState &U, double Speed, bool Suction)
{
    const FVector2D Previous = U.Position;
    double Budget = Speed * FixedDelta;
    while (U.RouteCursor < U.RouteCount && Budget > 0)
    {
        const FVector2D Delta = U.Route[U.RouteCursor] - U.Position;
        const double D = Delta.Size();
        if (D <= Budget + .001)
        {
            U.Position = U.Route[U.RouteCursor++];
            Budget -= D;
        }
        else
        {
            U.Position += Delta * (Budget / D);
            Budget = 0;
        }
    }
    if (Suction)
    {
        U.SuctionVelocity = (U.Position - Previous) / FixedDelta;
    }
    return U.RouteCount > 0 && U.RouteCursor == U.RouteCount;
}
void FDirectOrScutSimulation::UpdateBody(FDirectOrScutUnitState &U)
{
    U.SuctionVelocity = FVector2D::ZeroVector;
    if (!U.bRegistered || U.State == EDirectOrScutUnitState::Ejected || U.Room < 0)
    {
        return;
    }
    const bool Vacuum = !Rooms[U.Room].bIsPressurized;
    if (U.bAndroid)
    {
        U.Stress = 0;
    }
    if (Vacuum && !U.bAndroid && !U.bSuited)
    {
        U.Integrity = FMath::Max(0.0, U.Integrity - 35 * FixedDelta);
    }
    if (Vacuum && !U.bAndroid)
    {
        U.State = EDirectOrScutUnitState::Panic;
        U.Stress = 100;
    }
    if (!Vacuum && U.Integrity <= 0)
    {
        U.State = EDirectOrScutUnitState::Panic;
        return;
    }
    if (U.bStagingManaged && !Vacuum)
    {
        if (!U.bRoomRouteLocked && U.State == EDirectOrScutUnitState::Panic && U.Room == 3)
        {
            U.Stress = FMath::Max(0., U.Stress - 12 * FixedDelta);
            if (U.Stress <= 25)
            {
                U.State = EDirectOrScutUnitState::Working;
                U.AssignedRoom = 3;
            }
        }
        if (!U.bRoomRouteLocked && U.State == EDirectOrScutUnitState::Working)
        {
            if (bExperiment)
            {
                U.TaskProgress += FixedDelta * U.Profile.Utility;
            }
            // Psychology owns experiment stress drift; movement only advances task work.
        }
        return;
    }
    const double Speed = Vacuum ? 540.0
                         : U.State == EDirectOrScutUnitState::Panic
                             ? 190.0
                             : 145.0 * (bExperiment ? (.5 + U.Profile.Utility * .5) : 1.);
    if (U.bCrossing)
    {
        const auto &D = Doors[U.ActiveDoor];
        const bool ToB = U.CrossToRoom == D.RoomB;
        const FVector2D Target = D.Position + D.Normal * (ToB ? ApproachDistance : -ApproachDistance);
        const FVector2D Delta = Target - U.Position;
        const FVector2D Movement = Delta.GetSafeNormal() * FMath::Min(Delta.Size(), Speed * FixedDelta);
        U.Position += Movement;
        if (Vacuum)
        {
            U.SuctionVelocity = Movement / FixedDelta;
        }
        // Publish the room change only after the complete threshold transit.
        // Camera badges and anchor projection consume this same authoritative room.
        if (U.Position.Equals(Target, .01))
        {
            U.Room = U.CrossToRoom;
            U.bCrossing = false;
            U.ActiveDoor = -1;
            U.RouteCount = 0;
            U.RouteRevision = MAX_uint32;
            if (U.Room == -1)
            {
                U.State = EDirectOrScutUnitState::Ejected;
            }
        }
        return;
    }
    int32 Goal = U.AssignedRoom;
    FVector2D FinalTarget = DirectOrScut::RoomWaypoint(U.Room);
    if (!Vacuum && U.State == EDirectOrScutUnitState::Panic)
    {
        if (U.Room == 3)
        {
            U.Stress = FMath::Max(0.0, U.Stress - 12 * FixedDelta);
            if (U.Stress <= 25)
            {
                U.State = EDirectOrScutUnitState::Idle;
                U.AssignedRoom = 3;
                U.RouteRevision = MAX_uint32;
            }
        }
        if (U.State != EDirectOrScutUnitState::Panic)
        {
            Goal = U.AssignedRoom;
        }
        if (U.State == EDirectOrScutUnitState::Panic)
        {
            U.PanicClock -= FixedDelta;
            if (U.PanicClock <= 0)
            {
                int32 Reachable[RoomCount], Count = 0;
                for (int32 R = 0; R < RoomCount; ++R)
                {
                    if (R == U.Room || FirstDoorOnPath(U.Room, R) >= 0)
                    {
                        Reachable[Count++] = R;
                    }
                }
                U.PanicRoom = Reachable[NextRandom() % Count];
                int32 Candidates[MaxNodes], N = 0;
                for (int32 I = 0; I < NodeCount; ++I)
                {
                    if (Nodes[I].Room == U.PanicRoom)
                    {
                        Candidates[N++] = I;
                    }
                }
                U.WanderTarget =
                    N ? Nodes[Candidates[NextRandom() % N]].Position : DirectOrScut::RoomWaypoint(U.PanicRoom);
                U.PanicClock = 3 + NextRandom() % 4;
                U.RouteRevision = MAX_uint32;
            }
            Goal = U.PanicRoom;
            FinalTarget = U.WanderTarget;
        }
    }
    if (Vacuum)
    {
        Goal = -1;
    }
    const bool Panic = U.State == EDirectOrScutUnitState::Panic;
    int32 NextDoor = Goal == U.Room ? -1 : FirstDoorOnPath(U.Room, Goal);
    const bool Blocked = Goal != U.Room && NextDoor < 0;
    FVector2D Target;
    if (Blocked)
    {
        if (!Panic)
        {
            U.State = EDirectOrScutUnitState::Idle;
        }
        else
        {
            U.PanicClock = 0;
        }
        Target = DirectOrScut::RoomStandby(U.Room);
        NextDoor = -1;
    }
    else if (NextDoor >= 0)
    {
        const auto &D = Doors[NextDoor];
        Target = D.Position + D.Normal * (U.Room == D.RoomA ? -ApproachDistance : ApproachDistance);
        if (!Panic)
        {
            U.State = EDirectOrScutUnitState::MovingToRoom;
        }
    }
    else
    {
        Target = Panic ? FinalTarget : DirectOrScut::RoomWaypoint(U.Room);
        if (!Panic)
        {
            U.State = EDirectOrScutUnitState::MovingToRoom;
        }
    }
    if (U.RouteRevision != DoorRevision || U.ActiveDoor != NextDoor || !U.RouteTarget.Equals(Target, .01))
    {
        U.ActiveDoor = NextDoor;
        if (!PlanRoute(U, Target))
        {
            if (!Panic)
            {
                U.State = EDirectOrScutUnitState::Idle;
            }
            return;
        }
    }
    if (U.RouteCount == 0)
    {
        if (!Panic)
        {
            U.State = EDirectOrScutUnitState::Idle;
        }
        return;
    }
    if (!FollowRoute(U, Speed, Vacuum))
    {
        return;
    }
    if (NextDoor >= 0)
    {
        U.bCrossing = true;
        U.CrossFromRoom = U.Room;
        U.CrossToRoom = Doors[NextDoor].RoomA == U.Room ? Doors[NextDoor].RoomB : Doors[NextDoor].RoomA;
    }
    else if (!Blocked && !Panic)
    {
        U.State = EDirectOrScutUnitState::Working;
        if (bExperiment)
        {
            U.TaskProgress += FixedDelta * U.Profile.Utility;
        }
        if (!U.bAndroid)
        {
            U.Stress = FMath::Min(100.0, U.Stress + 1.5 * FixedDelta);
            if (U.Stress >= 100)
            {
                U.State = EDirectOrScutUnitState::Panic;
                U.PanicClock = 0;
                U.RouteRevision = MAX_uint32;
            }
        }
    }
}
void FDirectOrScutSimulation::Step()
{
    if (!bReady)
    {
        return;
    }
    RecomputePressure();
    for (auto &U : Bodies)
    {
        UpdateBody(U);
    }
    if (bExperiment)
    {
        UpdatePsychology();
    }
    UpdateBiometrics();
    ++TickIndex;
}
void FDirectOrScutSimulation::Advance(double Delta)
{
    if (!bReady || !FMath::IsFinite(Delta) || Delta <= 0)
    {
        return;
    }
    Accumulator += Delta;
    // Keep backlog instead of discarding elapsed time during a slow render frame.
    int32 Budget = 240;
    while (Accumulator + 1.e-9 >= FixedDelta && Budget-- > 0)
    {
        Step();
        Accumulator -= FixedDelta;
    }
    if (Accumulator < 0 && Accumulator > -1.e-8)
    {
        Accumulator = 0;
    }
}
uint64 FDirectOrScutSimulation::StateHash() const
{
    uint64 Hash = 1469598103934665603ull;
    auto Bytes = [&](const void *P, SIZE_T N)
    {
        const auto *B = static_cast<const uint8 *>(P);
        for (SIZE_T I = 0; I < N; ++I)
        {
            Hash ^= B[I];
            Hash *= 1099511628211ull;
        }
    };
    Bytes(&bExperiment, sizeof(bExperiment));
    Bytes(&ObjectiveProgress, sizeof(ObjectiveProgress));
    Bytes(&bShiftComplete, sizeof(bShiftComplete));
    Bytes(&bObjectiveFailed, sizeof(bObjectiveFailed));
    Bytes(&SweptRooms, sizeof(SweptRooms));
    for (const auto &U : Bodies)
    {
        Bytes(&U.Profile.Compliance, sizeof(float));
        Bytes(&U.Profile.Composure, sizeof(float));
        Bytes(&U.Profile.Affinity, sizeof(float));
        Bytes(&U.Profile.Utility, sizeof(float));
        Bytes(&U.Profile.Suspicion, sizeof(float));
        Bytes(&U.TaskProgress, sizeof(double));
    }
    for (const auto &U : Bodies)
    {
        const auto &P = U.Profile;
        Bytes(&P.HeartRateBPM, sizeof(float));
        Bytes(&P.RespirationRateRPM, sizeof(float));
        Bytes(&P.PupilDilationMM, sizeof(float));
        Bytes(&P.CardiacPhase, sizeof(double));
        Bytes(&P.BreathPhase, sizeof(double));
        Bytes(&P.HeartbeatCount, sizeof(uint64));
        Bytes(&P.BreathCount, sizeof(uint64));
    }
    for (const auto &R : Rooms)
    {
        Bytes(&R.AlertSeconds, sizeof(float));
        Bytes(&R.bHullBreach, sizeof(bool));
        Bytes(&R.bLockdown, sizeof(bool));
        Bytes(&R.LightLevel, sizeof(float));
    }
    Bytes(&TickIndex, sizeof(TickIndex));
    Bytes(&RandomState, sizeof(RandomState));
    for (const auto &D : Doors)
    {
        Bytes(&D.bIsOpen, sizeof(bool));
    }
    for (const auto &R : Rooms)
    {
        Bytes(&R.bIsPressurized, sizeof(bool));
    }
    for (const auto &U : Bodies)
    {
        Bytes(&U.bStagingManaged, sizeof(bool));
        Bytes(&U.bRoomRouteLocked, sizeof(bool));
        Bytes(&U.bRegistered, sizeof(bool));
        if (!U.bRegistered)
        {
            continue;
        }
        Bytes(&U.Position, sizeof(U.Position));
        Bytes(&U.Room, sizeof(U.Room));
        Bytes(&U.AssignedRoom, sizeof(U.AssignedRoom));
        Bytes(&U.State, sizeof(U.State));
        Bytes(&U.Integrity, sizeof(U.Integrity));
        Bytes(&U.Stress, sizeof(U.Stress));
        Bytes(&U.bObservedInjection, sizeof(U.bObservedInjection));
        Bytes(&U.SuctionVelocity, sizeof(U.SuctionVelocity));
    }
    return Hash;
}

float FDirectOrScutSimulation::CollectiveSuspicion() const
{
    float Sum = 0;
    int32 N = 0;
    for (const auto &U : Bodies)
    {
        if (U.bRegistered && !U.bAndroid && U.State != EDirectOrScutUnitState::Ejected && U.Integrity > 0)
        {
            Sum += U.Profile.Suspicion;
            ++N;
        }
    }
    return N ? Sum / N : 0;
}
bool FDirectOrScutSimulation::AssignTask(int32 I, int32 R, bool Pretext)
{
    if (I < 0 || I >= 4 || R < 0 || R >= 8)
    {
        return false;
    }
    auto &U = Bodies[I];
    if (!U.bRegistered || U.Integrity <= 0 || U.State == EDirectOrScutUnitState::Ejected)
    {
        return false;
    }
    if (!U.bAndroid)
    {
        if (!Pretext && (!Rooms[R].bIsPressurized || R == 7 || R == 0))
        {
            U.Profile.Suspicion = FMath::Min(1.f, U.Profile.Suspicion + .18f);
        }
        if (U.Profile.Suspicion > .75f || (U.Profile.Compliance < .6f && Rooms[R].AlertSeconds <= 0))
        {
            return false;
        }
    }
    return AssignRoom(I, R);
}
void FDirectOrScutSimulation::TriggerPretextAlert(int32 R, bool Breach)
{
    if (R < 0 || R >= 8)
    {
        return;
    }
    Rooms[R].AlertSeconds = 30;
    Rooms[R].bHullBreach = Breach;
    for (int32 I = 0; I < 4; ++I)
    {
        if (Bodies[I].Room == R || Bodies[I].Room == 1)
        {
            AssignTask(I, R, true);
        }
    }
}
void FDirectOrScutSimulation::BroadcastProtocol(int32 R)
{
    for (auto &U : Bodies)
    {
        if (U.bRegistered && !U.bAndroid && U.State != EDirectOrScutUnitState::Ejected && (R < 0 || U.Room == R))
        {
            U.Stress = FMath::Max(0., U.Stress - 18);
            U.Profile.Suspicion = FMath::Max(0.f, U.Profile.Suspicion - .08f);
        }
    }
}
void FDirectOrScutSimulation::UpdatePsychology()
{

    for (const auto &E : Scenario.Events)
    {
        if (TickIndex == E.Tick)
        {
            if (E.bCorporateDirective)
            {
                BroadcastProtocol(E.Room);
            }
            else
            {
                TriggerPretextAlert(E.Room);
            }
        }
    }
    float Previous[4];
    for (int32 I = 0; I < 4; ++I)
    {
        Previous[I] = Bodies[I].Profile.Suspicion;
    }
    for (int32 I = 0; I < 4; ++I)
    {
        auto &U = Bodies[I];
        if (!U.bRegistered || U.State == EDirectOrScutUnitState::Ejected || U.Integrity <= 0)
        {
            continue;
        }
        if (U.bAndroid)
        {
            U.Stress = 0;
            U.Profile.Suspicion = 0;
            continue;
        }
        const bool Alert = U.Room >= 0 && Rooms[U.Room].AlertSeconds > 0;
        // Soundstage experiments span ten minutes. Keep baseline workload below the panic threshold
        // over that window; explicit alerts, orders, isolation and deception remain consequential.
        const double EnvironmentLoad = U.bStagingManaged ? (Alert ? .8 : .04) : (Alert ? 6. : .5);
        const double Workload = U.bStagingManaged && U.State == EDirectOrScutUnitState::Working ? .035 : 0.;
        U.Stress =
            FMath::Clamp(U.Stress + (EnvironmentLoad * (1. - U.Profile.Composure) + Workload) * FixedDelta, 0., 100.);
        for (int32 J = 0; J < 4; ++J)
        {
            if (J != I && Bodies[J].bRegistered && Bodies[J].Room == U.Room && Bodies[J].Integrity > 0 &&
                Bodies[J].State != EDirectOrScutUnitState::Ejected)
            {
                const auto &V = Bodies[J];
                U.Stress =
                    FMath::Clamp(U.Stress - V.Profile.Affinity * (U.bStagingManaged ? .08 : 2.) * FixedDelta, 0., 100.);
                if (Previous[J] > .5f && V.Profile.Affinity > 0)
                {
                    U.Profile.Suspicion += V.Profile.Affinity * .025f * float(FixedDelta);
                }
                if (V.bAndroid && V.AssignedRoom == 7 && V.State == EDirectOrScutUnitState::MovingToRoom)
                {
                    U.Profile.Suspicion += .02f * float(FixedDelta);
                }
            }
        }
        U.Profile.Suspicion = FMath::Clamp(U.Profile.Suspicion, 0.f, 1.f);
        if (U.bRoomRouteLocked)
        {
            continue; // A committed route owns destination and state until arrival/cancellation.
        }
        if ((U.bStagingManaged ? U.Stress >= 75 : U.Stress > 40 + 60 * U.Profile.Composure) &&
            U.State != EDirectOrScutUnitState::Panic)
        {
            ForcePanic(I);
        }
        if (U.Profile.Suspicion > .75f && U.State != EDirectOrScutUnitState::Panic)
        {
            const int32 OverrideRoom = I % 2 ? 0 : 2;
            if (U.AssignedRoom != OverrideRoom)
            {
                AssignRoom(I, OverrideRoom);
            }
            // A rogue attempts one physical bulkhead override each simulated second.
            if (TickIndex % 30 == 0)
            {
                for (int32 D = 0; D < 8; ++D)
                {
                    if (!Doors[D].bIsOpen && (Doors[D].RoomA == U.Room || Doors[D].RoomB == U.Room))
                    {
                        SetDoorOpen(D, true);
                        break;
                    }
                }
            }
        }
    }
    for (auto &R : Rooms)
    {
        R.AlertSeconds = FMath::Max(0.f, R.AlertSeconds - float(FixedDelta));
    }
    // The workstation's three-act controller owns objective progress for staged crews.
    // Legacy standalone simulation scenarios retain their original objective rules below.
    for (const auto &Unit : Bodies)
    {
        if (Unit.bRegistered && Unit.bStagingManaged)
        {
            bShiftComplete = (TickIndex + 1) * FixedDelta >= Scenario.ShiftDuration;
            return;
        }
    }
    if (bShiftComplete)
    {
        return;
    }
    const auto &Target = Bodies[FMath::Clamp(Scenario.TargetCrew, 0, 3)];
    const int32 R = FMath::Clamp(Scenario.TargetRoom, 0, 7);
    if (Target.State == EDirectOrScutUnitState::Ejected || Target.Integrity <= 0)
    {
        bObjectiveFailed = true;
    }
    if (Scenario.HiddenObjectiveType == EHiddenObjectiveType::IsolateTargetInRoom)
    {
        bool Isolated = Target.bRegistered && Target.Room == R && Target.Integrity > 0;
        for (const auto &D : Doors)
        {
            if ((D.RoomA == R || D.RoomB == R) && D.bIsOpen)
            {
                Isolated = false;
            }
        }
        if (Isolated && !bObjectiveFailed)
        {
            ObjectiveProgress = FMath::Min(1.f, ObjectiveProgress + float(FixedDelta / 10));
        }
    }
    else if (Scenario.HiddenObjectiveType == EHiddenObjectiveType::CompleteCovertSystemSweep)
    {
        for (const auto &U : Bodies)
        {
            if (U.bRegistered && U.bAndroid && U.Room >= 0 && U.State == EDirectOrScutUnitState::Working &&
                U.TaskProgress > 3)
            {
                SweptRooms |= 1 << U.Room;
            }
        }
        int32 N = 0;
        for (int32 I = 0; I < 8; ++I)
        {
            N += (SweptRooms >> I) & 1;
        }
        ObjectiveProgress = N / 8.f;
    }
    else
    {
        if (CollectiveSuspicion() > .75f)
        {
            bObjectiveFailed = true;
        }
        ObjectiveProgress =
            bObjectiveFailed ? 0 : FMath::Min(1.f, float((TickIndex + 1) * FixedDelta / Scenario.ShiftDuration));
    }
    bShiftComplete = (TickIndex + 1) * FixedDelta >= Scenario.ShiftDuration;
}

bool FDirectOrScutSimulation::ToggleDoor(int32 D, bool Open)
{
    if (D < 0 || D >= DoorCount)
    {
        return false;
    }
    const auto &Door = Doors[D];
    if (Door.RoomA >= 0)
    {
        Rooms[Door.RoomA].bLockdown = !Open;
    }
    if (Door.RoomB >= 0)
    {
        Rooms[Door.RoomB].bLockdown = !Open;
    }
    if (Door.bIsOpen != Open && !Open)
    {
        for (auto &U : Bodies)
        {
            if (U.bRegistered && !U.bAndroid && U.State != EDirectOrScutUnitState::Ejected && U.Integrity > 0 &&
                (U.Room == Door.RoomA || U.Room == Door.RoomB) && U.Room >= 0 && Rooms[U.Room].AlertSeconds <= 0)
            {
                U.Profile.Suspicion = FMath::Min(1.f, U.Profile.Suspicion + .16f);
            }
        }
    }
    return SetDoorOpen(D, Open);
}
bool FDirectOrScutSimulation::CycleAirlock(int32 R)
{
    if (R < 0 || R >= RoomCount)
    {
        return false;
    }
    // Open a stable shortest path to vacuum; connected compartments share the risk.
    int32 Parent[8];
    for (int32 &P : Parent)
    {
        P = -2;
    }
    int32 Queue[8], Head = 0, Tail = 0;
    Queue[Tail++] = 7;
    Parent[7] = -1;
    while (Head < Tail)
    {
        const int32 A = Queue[Head++];
        for (int32 D = 0; D < 8; ++D)
        {
            const auto &Door = Doors[D];
            const int32 B = Door.RoomA == A ? Door.RoomB : Door.RoomB == A ? Door.RoomA : -1;
            if (B >= 0 && Parent[B] == -2)
            {
                Parent[B] = D;
                Queue[Tail++] = B;
            }
        }
    }
    bool PreviouslyExposed[8];
    for (int32 I = 0; I < 8; ++I)
    {
        PreviouslyExposed[I] = !Rooms[I].bIsPressurized;
    }
    for (int32 A = R; A != 7 && A >= 0;)
    {
        const int32 D = Parent[A];
        if (D < 0)
        {
            return false;
        }
        SetDoorOpen(D, true);
        A = Doors[D].RoomA == A ? Doors[D].RoomB : Doors[D].RoomA;
    }
    SetDoorOpen(8, true);
    for (auto &U : Bodies)
    {
        if (U.bRegistered && !U.bAndroid && U.State != EDirectOrScutUnitState::Ejected && U.Integrity > 0 &&
            U.Room >= 0 && !PreviouslyExposed[U.Room] && !Rooms[U.Room].bIsPressurized && !Rooms[U.Room].bHullBreach)
        {
            U.Profile.Suspicion = FMath::Min(1.f, U.Profile.Suspicion + .45f);
        }
    }
    return true;
}

// Crew biometrics.
void FDirectOrScutSimulation::UpdateBiometrics()
{
    for (auto &U : Bodies)
    {
        auto &P = U.Profile;
        P.OnHeartbeatTick = false;
        P.OnBreathCycle = false;
        if (!U.bRegistered || U.Integrity <= 0 || U.State == EDirectOrScutUnitState::Ejected)
        {
            continue;
        }
        const float Load = U.State == EDirectOrScutUnitState::Working ? .25f : 0.f;
        const float Light = U.Room >= 0 ? FMath::Clamp(Rooms[U.Room].LightLevel, 0.f, 1.f) : 1.f;
        P.HeartRateBPM =
            U.bAndroid ? 60 : FMath::Clamp(60.f + float(U.Stress) + P.Suspicion * 12 + Load * 12, 60.f, 160.f);
        P.RespirationRateRPM =
            U.bAndroid ? 0 : FMath::Clamp(12.f + float(U.Stress) * .2f + P.Suspicion * 2 + Load * 4, 12.f, 34.f);
        P.PupilDilationMM =
            FMath::Clamp(2.5f + float(U.Stress) * .025f + P.Suspicion * 1.5f + Load + (1 - Light) * 1.5f, 2.5f, 8.f);
        P.CardiacPhase += P.HeartRateBPM / 60. * FixedDelta;
        P.BreathPhase += P.RespirationRateRPM / 60. * FixedDelta;
        if (P.CardiacPhase >= 1)
        {
            P.CardiacPhase -= 1;
            P.OnHeartbeatTick = true;
            ++P.HeartbeatCount;
        }
        if (P.BreathPhase >= 1)
        {
            P.BreathPhase -= 1;
            P.OnBreathCycle = true;
            ++P.BreathCount;
        }
    }
}
