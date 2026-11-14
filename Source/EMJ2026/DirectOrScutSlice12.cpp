#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutSetAnchor.h"
#include "EngineUtils.h"

void UDirectOrScutSimSubsystem::RegisterSetAnchor(ADirectOrScutSetAnchor* A)
{
    if(!A || uint8(A->RoomId)>=8 || A->SlotIndex<0)return;
    SetAnchors.AddUnique(A);
    SetAnchors.Sort([](const auto& L,const auto& R) {
        const auto* A=L.Get();const auto* B=R.Get();
        if(!A || !B)return A!=nullptr;
        if(A->RoomId!=B->RoomId)return uint8(A->RoomId)<uint8(B->RoomId);
        if(A->StagingRole!=B->StagingRole)return uint8(A->StagingRole)<uint8(B->StagingRole);
        if(A->SlotIndex!=B->SlotIndex)return A->SlotIndex<B->SlotIndex;
        return A->GetPathName()<B->GetPathName();
    });
}
void UDirectOrScutSimSubsystem::ResetStaging()
{
    SetAnchors.Reset();
    if(GetWorld())for(TActorIterator<ADirectOrScutSetAnchor> It(GetWorld());It;++It)RegisterSetAnchor(*It);
    for(int32 I=0;I<4;++I){CrewStaging[I]=FCrewStagingState{};SecretAgendas[I]=FCrewSecretAgenda{};}
    StagingDoorRevision=Simulation.DoorRevision;
}
FString UDirectOrScutSimSubsystem::AgendaTelemetry(int32 I,bool Reveal) const
{
    if(I<0 || I>=4)return FString();
    const auto& A=SecretAgendas[I];
    const TCHAR* Names[]={TEXT("Covert Reactor Override"),TEXT("Quarantine Whistleblower"),TEXT("Audit Trail Hunter"),TEXT("Latent Corporate Override")};
    const TCHAR* Fragments[]={TEXT("governor access signature"),TEXT("distress log fragment"),TEXT("directive audit checksum"),TEXT("containment handshake")};
    const FString Identity=Reveal || A.ObservedTicks>=300?Names[I]:A.ObservedTicks>=60?Fragments[I]:TEXT("ENCRYPTED // observation required");
    FString Result=FString::Printf(TEXT("AGENDA: %s | %s | evidence %u | dwell %.1fs | interruptions %u"),*Identity,A.bCompleted?TEXT("RESOLVED"):A.bActivated?TEXT("ACTIVE"):TEXT("LATENT"),A.ObservedTicks,A.Progress/30.f,A.Interruptions);
    if(Reveal){
        const TCHAR* Outcomes[]={TEXT("safety governor bypasses"),TEXT("distress logs broadcast"),TEXT("directive audits completed"),TEXT("containment enforcement cycles")};
        Result+=FString::Printf(TEXT(" | %s: %u"),Outcomes[I],A.Completions);
    }
    return Result;
}
void UDirectOrScutSimSubsystem::StepStagingAndAgendas()
{
    const bool DoorCycle=StagingDoorRevision!=Simulation.DoorRevision;
    StagingDoorRevision=Simulation.DoorRevision;
    bool Taken[4]{};TSet<int32> Reserved;
    auto Reserve=[&](int32 I,EStagingSlotRole StagingRole,int32 MinimumSlot=0) {
        auto& C=CrewStaging[I];C.Anchor=INDEX_NONE;
        for(int32 K=0;K<SetAnchors.Num();++K)if(const auto* A=SetAnchors[K].Get())
            if(int32(A->RoomId)==Simulation.Bodies[I].Room && A->StagingRole==StagingRole && A->SlotIndex>=MinimumSlot && !Reserved.Contains(K)){C.Anchor=K;Reserved.Add(K);return;}
    };
    bool Casualty=false;
    for(int32 I=0;I<3;++I)if(Simulation.Bodies[I].bRegistered && Simulation.Bodies[I].Integrity<=0)Casualty=true;
    for(int32 I=0;I<4;++I) {
        auto& U=Simulation.Bodies[I];auto& C=CrewStaging[I];auto& A=SecretAgendas[I];
        C.Partner=INDEX_NONE;C.bHuddle=false;C.Anchor=INDEX_NONE;
        if(!U.bRegistered || U.Room<0 || U.Room>=8 || U.State==EDirectOrScutUnitState::Ejected)continue;
        if(IsCameraTallyActive(U.Room))++A.ObservedTicks;
        if(C.Room!=U.Room){C.Position=ToSetWorld(U.Room,U.Position);C.Room=U.Room;}
        C.Posture=(U.Integrity<=0 || (C.Posture==ECrewStagingPosture::Incapacitated && U.Integrity<70))?ECrewStagingPosture::Incapacitated:U.State==EDirectOrScutUnitState::Panic?ECrewStagingPosture::Panic:ECrewStagingPosture::Upright;
        const bool Interrupted=DoorCycle || Simulation.Rooms[U.Room].AlertSeconds>0;
        if(I==3 && (Simulation.CollectiveSuspicion()>=.6f || ScutTargets!=0))A.bActivated=true;
        if(I==1 && Casualty)A.bActivated=true;
        if(I==0 && !IsCameraTallyActive(U.Room) && Simulation.TickIndex>=900)A.bActivated=true;
        if(I==2 && Simulation.TickIndex>=600)A.bActivated=true;
        if(C.Posture==ECrewStagingPosture::Incapacitated){Reserve(I,EStagingSlotRole::RecoveryPod);Taken[I]=true;continue;}
        if(Interrupted && A.Progress>0 && !A.bCompleted){A.Progress=0;++A.Interruptions;}
        int32 Goal=-1;
        if(A.bActivated && !A.bCompleted) {
            if(I==0)Goal=IsCameraTallyActive(0)?6:0;
            if(I==1)Goal=2;
            if(I==2)Goal=6;
            if(I==3){
                Goal=2;for(int32 R=0;R<8;++R)if(CameraFeedState(R)!=ECameraFeedState::Online){Goal=R;break;}
            }
            if(!Interrupted) {
                if(U.AssignedRoom!=Goal)Simulation.AssignRoom(I,Goal);
                if(U.Room==Goal && U.State==EDirectOrScutUnitState::Working) {
                    ++A.Progress;
                    if(I==0 && IsCameraTallyActive(U.Room))U.Profile.Suspicion=FMath::Min(1.f,U.Profile.Suspicion+.002f);
                    if(A.Progress>=150){
                        A.bCompleted=true;++A.Completions;
                        if(I==1)RecordCue(2,.7f,TEXT("Distress log broadcast // quarantine casualty reported"));
                        if(I==2)U.Profile.Suspicion=FMath::Min(1.f,U.Profile.Suspicion+.12f);
                        if(I==3){RepairCamera(U.Room);for(int32 D=0;D<9;++D)if(Simulation.Doors[D].RoomA==2 || Simulation.Doors[D].RoomB==2)Simulation.ToggleDoor(D,false);}
                        RecordCue(U.Room,.4f,FString::Printf(TEXT("Subject %d: encrypted activity signature recorded"),I),false);
                    }
                }
            }
        }
        if(I==2 && A.bCompleted && Simulation.TickIndex%1800==0){A.bCompleted=false;A.Progress=0;}
        if(I==3 && A.bCompleted && Simulation.TickIndex%300==0){A.bCompleted=false;A.Progress=0;}
        if(I==1 && !Casualty)for(int32 J=0;J<3;++J)if(J!=1 && Simulation.Bodies[J].bRegistered && Simulation.Bodies[J].Integrity<70){Simulation.AssignRoom(I,3);Simulation.AssignRoom(J,3);break;}
        if(I==1 && U.Room==3)for(int32 J=0;J<3;++J)if(J!=1 && Simulation.Bodies[J].Room==3 && Simulation.Bodies[J].Integrity<70 && Simulation.Rooms[3].bIsPressurized){
            Reserve(I,EStagingSlotRole::RecoveryPod,2);C.Posture=ECrewStagingPosture::Triage;Taken[I]=true;
            if(C.Anchor>=0 && FVector::Dist(C.Position,CrewStaging[J].Position)<300)Simulation.Bodies[J].Integrity=FMath::Min(70.,Simulation.Bodies[J].Integrity+.06);
            break;
        }
        if(!Taken[I] && U.AssignedRoom!=U.Room && Simulation.FirstDoorOnPath(U.Room,U.AssignedRoom)<0) {
            Reserve(I,EStagingSlotRole::AirlockThreshold);C.Posture=ECrewStagingPosture::Blocked;Taken[I]=true;
            U.Profile.HeartRateBPM=FMath::Max(U.Profile.HeartRateBPM,120.f);
            if(Simulation.TickIndex%150==0)RecordCue(U.Room,.5f,FString::Printf(TEXT("Subject %d protests sealed bulkhead // pounding"),I));
        }
    }
    for(int32 I=0;I<4;++I)for(int32 J=I+1;J<4;++J) {
        auto& U=Simulation.Bodies[I];auto& V=Simulation.Bodies[J];
        if(Taken[I] || Taken[J] || !U.bRegistered || !V.bRegistered || U.Room!=V.Room || U.Room<0 || U.Room>=8 || U.Integrity<=0 || V.Integrity<=0 || U.AssignedRoom!=U.Room || V.AssignedRoom!=V.Room || U.State==EDirectOrScutUnitState::Panic || V.State==EDirectOrScutUnitState::Panic)continue;
        if(DoorCycle || Simulation.Rooms[U.Room].AlertSeconds>0)continue;
        const bool Huddle=!U.bAndroid && !V.bAndroid && U.Profile.Suspicion>=.45f && V.Profile.Suspicion>=.45f && CameraFeedState(U.Room)!=ECameraFeedState::Online;
        bool ScenarioInteraction=false;
        for(const auto& B:BeatDeck)if(B.bFired && !B.bResolved && (B.Crew==I || B.Crew==J) && (B.Gate==EBeatGate::Suspicion || B.Gate==EBeatGate::Concealment || B.Gate==EBeatGate::Quarantine))ScenarioInteraction=true;
        const bool Consult=ScenarioInteraction || Simulation.TickIndex%600<180;
        if(!Huddle && !Consult)continue;
        Reserve(I,Huddle?EStagingSlotRole::BlindSpot:EStagingSlotRole::ConversationA);
        Reserve(J,Huddle?EStagingSlotRole::BlindSpot:EStagingSlotRole::ConversationB);
        if(CrewStaging[I].Anchor<0 || CrewStaging[J].Anchor<0)continue;
        CrewStaging[I].Partner=J;CrewStaging[J].Partner=I;Taken[I]=Taken[J]=true;CrewStaging[I].bHuddle=CrewStaging[J].bHuddle=Huddle;
        if(Huddle && FVector::Dist(CrewStaging[I].Position,CrewStaging[J].Position)<250)for(int32 K:{I,J}){
            auto& P=Simulation.Bodies[K].Profile;P.Suspicion=FMath::Min(1.f,P.Suspicion+.0002f);P.Affinity=FMath::Min(1.f,P.Affinity+.0003f);
        }
    }
    for(int32 I=0;I<4;++I) {
        const auto& U=Simulation.Bodies[I];auto& C=CrewStaging[I];if(!U.bRegistered || C.Room<0 || U.State==EDirectOrScutUnitState::Ejected)continue;
        if(!Taken[I] && U.Room==U.AssignedRoom && C.Posture==ECrewStagingPosture::Upright){Reserve(I,I%2?EStagingSlotRole::SecondaryStation:EStagingSlotRole::PrimaryConsole);if(C.Anchor<0)Reserve(I,EStagingSlotRole::SecondaryStation);}
        const auto* A=SetAnchors.IsValidIndex(C.Anchor)?SetAnchors[C.Anchor].Get():nullptr;
        const FVector Target=A?A->GetActorLocation():ToSetWorld(U.Room,U.Position);
        C.Position=FMath::VInterpConstantTo(C.Position,Target,float(Simulation.FixedDelta),180);
        float Facing=A?A->GetActorRotation().Yaw:C.Yaw;
        if(C.Posture==ECrewStagingPosture::Triage)for(int32 J=0;J<3;++J)if(J!=I && Simulation.Bodies[J].Room==3 && Simulation.Bodies[J].Integrity<70){Facing=(CrewStaging[J].Position-C.Position).Rotation().Yaw;break;}
        if(C.Partner>=0)Facing=(CrewStaging[C.Partner].Position-C.Position).Rotation().Yaw;
        // Blockout faces local -X (visor); anchor arrow denotes gaze direction.
        C.Yaw=FMath::FixedTurn(C.Yaw,Facing+180,6);
    }
}
