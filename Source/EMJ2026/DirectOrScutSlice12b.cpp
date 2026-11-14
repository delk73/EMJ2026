#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutSetAnchor.h"
#include "DirectOrScutGeometry.h"

bool UDirectOrScutSimSubsystem::IsCrewRouteLocked(int32 I) const
{
    return I>=0 && I<4 && (CrewStaging[I].RoutePhase!=ECrewRoutePhase::None || CrewStaging[I].PendingRouteRoom>=0 || !CrewStaging[I].ActiveRoute.IsEmpty());
}
int32 UDirectOrScutSimSubsystem::ReserveRouteAnchor(int32 I,int32 Room,bool Departure)
{
    const EStagingSlotRole Roles[]={Departure?EStagingSlotRole::AirlockThreshold:I%2?EStagingSlotRole::SecondaryStation:EStagingSlotRole::PrimaryConsole,
        EStagingSlotRole::PrimaryConsole,EStagingSlotRole::SecondaryStation,EStagingSlotRole::ConversationA,EStagingSlotRole::ConversationB};
    for(auto Role:Roles)for(int32 K=0;K<SetAnchors.Num();++K)if(const auto* A=SetAnchors[K].Get()){
        if(int32(A->RoomId)!=Room || A->StagingRole!=Role)continue;
        bool Occupied=false;
        for(int32 J=0;J<4;++J)if(J!=I && Simulation.Bodies[J].bRegistered && CrewStaging[J].Room==Room && CrewStaging[J].Anchor==K)Occupied=true;
        if(!Occupied)return K;
    }
    return INDEX_NONE;
}
void UDirectOrScutSimSubsystem::CancelCrewRoute(int32 I)
{
    if(I<0 || I>=4)return;
    auto& C=CrewStaging[I];auto& U=Simulation.Bodies[I];
    C.ActiveRoute.Reset();C.RoutePhase=ECrewRoutePhase::None;C.PendingRouteRoom=INDEX_NONE;C.bRouteBlocked=false;
    C.DirectiveRoom=INDEX_NONE;C.DirectiveArrivalTicks=0;C.AnchorHoldTicks=0;
    U.bRoomRouteLocked=false;U.bCrossing=false;U.ActiveDoor=-1;U.RouteCount=0;U.AssignedRoom=U.Room;
    if(U.State==EDirectOrScutUnitState::MovingToRoom)U.State=U.Stress>=75?EDirectOrScutUnitState::Panic:EDirectOrScutUnitState::Working;
}
bool UDirectOrScutSimSubsystem::StartCrewRoute(int32 I,int32 Goal,bool PlayerOrder,bool Pretext)
{
    if(I<0 || I>=4 || Goal<0 || Goal>=8)return false;
    auto& U=Simulation.Bodies[I];auto& C=CrewStaging[I];
    if(!U.bRegistered || U.Room<0 || U.Room>=8 || U.Integrity<=0 || U.State==EDirectOrScutUnitState::Ejected)return false;
    if(IsCrewRouteLocked(I)){
        if(U.AssignedRoom==Goal){if(PlayerOrder)C.DirectiveRoom=Goal;return true;} // Repeated input cannot restart the same route.
        if(!PlayerOrder)return false;
        CancelCrewRoute(I); // A new explicit player destination cancels the old commitment.
    }
    if(!PlayerOrder && U.Room==Goal && U.AssignedRoom==Goal)return true;
    int32 Parent[8];for(int32& P:Parent)P=-1;
    int32 Queue[8],Head=0,Tail=0;Queue[Tail++]=U.Room;Parent[U.Room]=U.Room;
    while(Head<Tail && Parent[Goal]<0){
        const int32 R=Queue[Head++];
        for(const auto& D:Simulation.Doors){
            if(!D.bIsOpen || D.RoomB<0)continue;
            const int32 Next=D.RoomA==R?D.RoomB:D.RoomB==R?D.RoomA:-1;
            if(Next<0 || Parent[Next]>=0)continue;
            Parent[Next]=R;Queue[Tail++]=Next;
        }
    }
    C.ActiveRoute.Reset();
    if(Parent[Goal]>=0){
        TArray<ERoomId> Reverse;for(int32 R=Goal;R!=U.Room;R=Parent[R])Reverse.Add(ERoomId(R));
        for(int32 K=Reverse.Num()-1;K>=0;--K)C.ActiveRoute.Add(Reverse[K]);
    }
    C.PendingRouteRoom=Parent[Goal]<0?Goal:INDEX_NONE;C.RouteDoorRevision=Simulation.DoorRevision;
    C.bRouteBlocked=C.PendingRouteRoom>=0;C.RoutePhase=C.bRouteBlocked?ECrewRoutePhase::None:C.ActiveRoute.IsEmpty()?ECrewRoutePhase::Arriving:ECrewRoutePhase::Departing;
    ++C.RouteSerial;C.AnchorHoldTicks=0;C.DirectiveArrivalTicks=0;
    if(PlayerOrder){
        C.DirectiveRoom=Goal;
        if(!U.bAndroid && (U.Profile.Compliance<.6f || U.Stress>=75 || !Pretext)){
            U.Stress=FMath::Min(100.,U.Stress+(Pretext?5.:10.));
            U.Profile.Suspicion=FMath::Min(1.f,U.Profile.Suspicion+(Pretext?.03f:.08f));
        }
    }
    U.AssignedRoom=Goal;U.bStagingManaged=true;U.bRoomRouteLocked=true;U.TaskProgress=0;
    U.bCrossing=false;U.ActiveDoor=-1;U.RouteCount=0;
    C.Anchor=ReserveRouteAnchor(I,U.Room,!C.ActiveRoute.IsEmpty() || C.bRouteBlocked);
    U.State=C.bRouteBlocked?EDirectOrScutUnitState::Idle:EDirectOrScutUnitState::MovingToRoom;
    return true;
}
void UDirectOrScutSimSubsystem::StepCrewRoutes()
{
    constexpr float ArrivalTolerance=.01f;
    for(int32 I=0;I<4;++I){
        auto& C=CrewStaging[I];auto& U=Simulation.Bodies[I];
        if(!U.bRegistered || U.Room<0 || U.Room>=8)continue;
        if(U.Integrity<=0 || U.State==EDirectOrScutUnitState::Ejected || !Simulation.Rooms[U.Room].bIsPressurized){
            if(IsCrewRouteLocked(I))CancelCrewRoute(I);
            continue;
        }
        if(C.PendingRouteRoom>=0 && C.RouteDoorRevision!=Simulation.DoorRevision){
            const int32 Goal=C.PendingRouteRoom;const int32 Directive=C.DirectiveRoom;
            // Only a graph change may retry an initially sealed route, never per-tick destination polling.
            CancelCrewRoute(I);StartCrewRoute(I,Goal);C.DirectiveRoom=Directive;
        }
        if(!IsCrewRouteLocked(I) && U.AssignedRoom!=U.Room)StartCrewRoute(I,U.AssignedRoom);
        if(!IsCrewRouteLocked(I))continue;
        bool EdgeOpen=true;
        if(!C.ActiveRoute.IsEmpty()){
            EdgeOpen=false;const int32 Next=int32(C.ActiveRoute[0]);
            for(const auto& D:Simulation.Doors)if(D.bIsOpen && ((D.RoomA==U.Room && D.RoomB==Next)||(D.RoomB==U.Room && D.RoomA==Next))){EdgeOpen=true;break;}
        }
        C.bRouteBlocked=C.PendingRouteRoom>=0 || !EdgeOpen;
        if(C.bRouteBlocked){
            C.Posture=ECrewStagingPosture::Blocked;U.State=EDirectOrScutUnitState::Idle;
            U.Profile.HeartRateBPM=FMath::Max(120.f,U.Profile.HeartRateBPM);
            if(Simulation.TickIndex>=C.NextProtestTick){RecordCue(U.Room,.5f,FString::Printf(TEXT("Subject %d protests sealed bulkhead // pounding"),I));C.NextProtestTick=Simulation.TickIndex+900;}
        } else {
            C.Posture=ECrewStagingPosture::Upright;
            U.State=C.ActiveRoute.IsEmpty()?(U.Stress>=75?EDirectOrScutUnitState::Panic:EDirectOrScutUnitState::Working):EDirectOrScutUnitState::MovingToRoom;
        }
        const auto* A=SetAnchors.IsValidIndex(C.Anchor)?SetAnchors[C.Anchor].Get():nullptr;
        if(!A){ensureMsgf(false,TEXT("Committed crew route requires a room staging anchor"));CancelCrewRoute(I);continue;}
        const FVector Target=A->GetActorLocation();
        C.Position=FMath::VInterpConstantTo(C.Position,Target,float(Simulation.FixedDelta),180);
        C.Yaw=FMath::FixedTurn(C.Yaw,A->GetActorRotation().Yaw+180,6);
        if(C.bRouteBlocked || !C.Position.Equals(Target,ArrivalTolerance))continue;
        C.Position=Target;
        if(C.RoutePhase==ECrewRoutePhase::Departing && !C.ActiveRoute.IsEmpty()){
            const int32 Next=int32(C.ActiveRoute[0]);
            // Separate soundstages: enter at its threshold, then approach one reserved console.
            int32 Entry=INDEX_NONE;
            for(int32 K=0;K<SetAnchors.Num();++K)if(const auto* E=SetAnchors[K].Get())if(int32(E->RoomId)==Next && E->StagingRole==EStagingSlotRole::AirlockThreshold){Entry=K;break;}
            const int32 DestinationAnchor=ReserveRouteAnchor(I,Next,false);
            if(Entry<0 || DestinationAnchor<0){ensureMsgf(false,TEXT("Destination room requires entry and station anchors"));CancelCrewRoute(I);continue;}
            U.Room=Next;C.Room=Next;U.Position=DirectOrScut::RoomWaypoint(Next);
            C.ActiveRoute.RemoveAt(0);++C.HopCommits;
            C.Position=SetAnchors[Entry]->GetActorLocation();C.Anchor=DestinationAnchor;C.RoutePhase=ECrewRoutePhase::Arriving;
            if(C.ActiveRoute.IsEmpty())U.State=U.Stress>=75?EDirectOrScutUnitState::Panic:EDirectOrScutUnitState::Working;
            continue; // Exactly one hop commit and one movement integration per fixed step.
        }
        if(C.RoutePhase==ECrewRoutePhase::Arriving){
            if(!C.ActiveRoute.IsEmpty()){
                C.RoutePhase=ECrewRoutePhase::Departing;C.Anchor=ReserveRouteAnchor(I,U.Room,true);
            } else {
                C.RoutePhase=ECrewRoutePhase::None;C.AnchorHoldTicks=150;U.bRoomRouteLocked=false;
                U.State=U.Stress>=75?EDirectOrScutUnitState::Panic:EDirectOrScutUnitState::Working;
            }
        }
    }
}
