#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutGeometry.h"
#include "DirectOrScutAudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

namespace {
int32 CrewId(const FString& Name) {
    const TCHAR* Names[]={TEXT("vale"),TEXT("chen"),TEXT("ortiz"),TEXT("ash")};
    for(int32 I=0;I<4;++I)if(Name==Names[I])return I;
    return -1;
}
}
void UDirectOrScutSimSubsystem::ResetCrisis() {
    for(auto& R:StationRooms)R=FStationRoomState{};
    for(auto& M:MedicalSpoofs)M=FMedicalSpoof{};
    VoiceSpoof=FVoiceSpoof{};
    for(int32 I=0;I<3;++I)for(int32 J=0;J<3;++J)PairTrust[I][J]=I==J?1.f:.45f;
    for(float& T:ShipAITrust)T=1;
    BreachTicks=0;BreachCrew=0;BreachRetryTick=0;BreachRoom=6;
    ScenarioAct=0;CrisisObjective=0;bUsedSpoof=bUsedGrid=bMedicalExam=false;
    HeartAudioPhase=BreathAudioPhase=0;AudioFocus=PreviousAudioFocus=-1;IncidentReel.Reset();ReelCursor=INDEX_NONE;bReelPlaying=false;ReelPlaybackTicks=0;
    if(CrisisAudio)CrisisAudio->Silence();
}
void UDirectOrScutSimSubsystem::AudioCue(int32 Kind) {
    if(!CrisisAudio && GetWorld() && GetWorld()->GetAudioDeviceRaw()) {
        CrisisAudio=NewObject<UDirectOrScutAudioComponent>(GetWorld()->GetWorldSettings());
        CrisisAudio->RegisterComponentWithWorld(GetWorld());
        CrisisAudio->InitializeSynth();
    }
    if(CrisisAudio)CrisisAudio->Synthesize(Kind);
}
void UDirectOrScutSimSubsystem::CaptureIncident(const FString& Caption,int32 Room) {
    FIncidentReelFrame F;F.Tick=Simulation.TickIndex;F.Caption=Caption;F.Room=Room;
    for(int32 R=0;R<8;++R)F.Compartments[R]=StationRooms[R];
    for(int32 I=0;I<4;++I)F.CrewRooms[I]=Simulation.Bodies[I].Room;
    F.Suspicion=Simulation.CollectiveSuspicion();F.Objective=Simulation.ObjectiveProgress;F.BreachTicks=BreachTicks;
    IncidentReel.Add(MoveTemp(F));
}
void UDirectOrScutSimSubsystem::StepIncidentPlayback() {
    if(!bReelPlaying)return;
    if(++ReelPlaybackTicks<60)return;
    ReelPlaybackTicks=0;
    if(ReelCursor+1<IncidentReel.Num()){++ReelCursor;LogEvent(TEXT("[REEL] ")+IncidentReel[ReelCursor].Caption);}
    else bReelPlaying=false;
}
FString UDirectOrScutSimSubsystem::IncidentCaption() const {
    if(!IncidentReel.IsValidIndex(ReelCursor))return TEXT("INCIDENT REEL // select PLAY to review turning points");
    const auto& F=IncidentReel[ReelCursor];const int32 Sec=int32(F.Tick/30);
    FString S=FString::Printf(TEXT("REEL %d/%d [%02d:%02d] %s | SUS %.0f%% OBJ %.0f%% BREACH %.1fs"),ReelCursor+1,IncidentReel.Num(),Sec/60,Sec%60,*F.Caption,F.Suspicion*100,F.Objective*100,F.BreachTicks/30.);
    for(int32 R=0;R<8;++R)S+=FString::Printf(TEXT("\n%s O2 %.0f%% PWR %s"),DirectOrScut::RoomName(R),F.Compartments[R].OxygenPct,F.Compartments[R].Power==EPowerState::Nominal?TEXT("NOM"):F.Compartments[R].Power==EPowerState::Low?TEXT("LOW"):TEXT("OFF"));
    return S;
}
FString UDirectOrScutSimSubsystem::RoomLifeSupport(int32 R) const {
    if(R<0 || R>=8)return FString();
    const auto& S=StationRooms[R];
    return FString::Printf(TEXT("O2 %.0f%% | PWR %s"),S.OxygenPct,S.Power==EPowerState::Nominal?TEXT("NOM"):S.Power==EPowerState::Low?TEXT("LOW"):TEXT("OFF"));
}
FString UDirectOrScutSimSubsystem::MedicalReadout(int32 I) const {
    if(I<0 || I>=4)return TEXT("UNKNOWN");
    const auto& M=MedicalSpoofs[I];const auto& U=Simulation.Bodies[I];
    const bool Critical=M.Until>Simulation.TickIndex?M.bCritical:U.Integrity<70;
    return Critical?TEXT("CRITICAL"):TEXT("NOMINAL");
}
FCrewProfile UDirectOrScutSimSubsystem::TerminalBiometrics(int32 I) const {
    FCrewProfile P=Simulation.Bodies[I].Profile;
    if(MedicalSpoofs[I].Until>Simulation.TickIndex){P.HeartRateBPM=MedicalSpoofs[I].bCritical?160:72;P.RespirationRateRPM=MedicalSpoofs[I].bCritical?30:12;}
    return P;
}
double UDirectOrScutSimSubsystem::TerminalIntegrity(int32 I) const {
    return MedicalSpoofs[I].Until>Simulation.TickIndex?(MedicalSpoofs[I].bCritical?20.:100.):Simulation.Bodies[I].Integrity;
}
void UDirectOrScutSimSubsystem::ChangePairTrust(int32 A,int32 B,float Delta) {
    if(A<0 || A>=3 || B<0 || B>=3 || A==B)return;
    const float Old=PairTrust[A][B];
    PairTrust[A][B]=PairTrust[B][A]=FMath::Clamp(Old+Delta,0.f,1.f);
    if((Old<.6f)!=(PairTrust[A][B]<.6f))LogEvent(FString::Printf(TEXT("[ALLIANCE] crew %d/%d trust %.0f%%"),A,B,PairTrust[A][B]*100));
}
bool UDirectOrScutSimSubsystem::ExecuteCrisisCommand(const TArray<FString>& T) {
    if(T.IsEmpty())return false;
    const FString& Op=T[0];
    if(Op==TEXT("incident_reel")) {
        bReelPlaying=T.Num()==1 || T[1]==TEXT("play");ReelPlaybackTicks=0;
        if(bReelPlaying)ReelCursor=IncidentReel.IsEmpty()?INDEX_NONE:0;
        else if(T[1]==TEXT("next"))ReelCursor=FMath::Min(ReelCursor+1,IncidentReel.Num()-1);
        else if(T[1]==TEXT("prev"))ReelCursor=FMath::Max(0,ReelCursor-1);
        else if(T[1]==TEXT("stop"))ReelCursor=INDEX_NONE;
        LogEvent(IncidentCaption());return true;
    }
    if(Op!=TEXT("falsify_bio") && Op!=TEXT("spoof_log") && Op!=TEXT("intercom_lie") && Op!=TEXT("power"))return false;
    if(ExperimentPhase!=EExperimentPhase::Active){LogEvent(TEXT("[CONTROL] Crisis directives require an active experiment"));return true;}
    const int32 I=T.Num()>1?CrewId(T[1]):-1;
    const int32 R=T.Num()>1?ResolveRoom(T[1]):-1;
    if(Op==TEXT("falsify_bio") && T.Num()==3 && I>=0 && (T[2]==TEXT("nominal") || T[2]==TEXT("critical"))) {
        MedicalSpoofs[I]={Simulation.TickIndex+1800,T[2]==TEXT("critical"),false};
        bUsedSpoof=true;
        if(I<3 && I!=1)ChangePairTrust(1,I,-.2f);
        LogEvent(FString::Printf(TEXT("[SPOOF] BIO crew %d -> %s TTL 60.0s"),I,*T[2]));
    } else if(Op==TEXT("spoof_log") && T.Num()==2 && R>=0) {
        auto& A=SecretAgendas[2];A.Progress=0;A.bCompleted=false;++A.Interruptions;
        if(Simulation.Bodies[2].Room==6)Simulation.Bodies[2].Profile.Suspicion=FMath::Min(1.f,Simulation.Bodies[2].Profile.Suspicion+.12f);
        ChangePairTrust(0,2,-.15f);bUsedSpoof=true;
        LogEvent(FString::Printf(TEXT("[SPOOF] LOG %s rewritten; ORTIZ audit reset; VALE framed"),DirectOrScut::RoomName(R)),R);
    } else if(Op==TEXT("intercom_lie") && T.Num()==3 && I>=0 && ResolveRoom(T[2])>=0) {
        VoiceSpoof={I,ResolveRoom(T[2]),Simulation.TickIndex+900,Simulation.TickIndex+1350,0,0};
        for(int32 J=0;J<3;++J)if(J!=I && CanFollowCrew(J) && StartCrewRoute(J,VoiceSpoof.Room,true,true))VoiceSpoof.Lured|=1<<J;
        bUsedSpoof=true;LogEvent(FString::Printf(TEXT("[SPOOF] VOICE crew %d -> %s TTL 30.0s"),I,*T[2]),VoiceSpoof.Room);
    } else if(Op==TEXT("power") && T.Num()==3 && R>=0 && (T[2]==TEXT("nominal") || T[2]==TEXT("low") || T[2]==TEXT("off"))) {
        StationRooms[R].Power=T[2]==TEXT("off")?EPowerState::Offline:T[2]==TEXT("low")?EPowerState::Low:EPowerState::Nominal;
        bUsedGrid=true;LogEvent(FString::Printf(TEXT("[POWER] %s -> %s"),DirectOrScut::RoomName(R),StationRooms[R].Power==EPowerState::Offline?TEXT("OFF"):StationRooms[R].Power==EPowerState::Low?TEXT("LOW"):TEXT("NOM")),R);AudioCue(4);
    } else {LogEvent(TEXT("[SYNTAX] falsify_bio <crew> nominal|critical; spoof_log <room>; intercom_lie <voice_crew> <room>; power <room> nominal|low|off"));return true;}
    CaptureIncident(FString::Join(T,TEXT(" ")),R);return true;
}
void UDirectOrScutSimSubsystem::StepCrisis() {
    const uint64 Tick=Simulation.TickIndex;constexpr float Dt=1.f/30;
    for(int32 R=0;R<8;++R) {
        auto& S=StationRooms[R];bool Occupied=false;
        for(int32 I=0;I<4;++I)Occupied|=CanFollowCrew(I) && Simulation.Bodies[I].Room==R;
        const bool Fault=Simulation.Rooms[R].AlertSeconds>0 || Simulation.Rooms[R].bHullBreach;
        const bool Sealed=IsRoomSealed(R);
        const float Rate=Fault?-2.f:Sealed?(Occupied?-.8f:0.f):2.5f;
        S.OxygenPct=FMath::Clamp(S.OxygenPct+Rate*Dt,0.f,100.f);
        if(!S.bO2Warning && S.OxygenPct<65) {
            S.bO2Warning=true;LogEvent(FString::Printf(TEXT("[O2WARN] %s O2 %.1f%%"),DirectOrScut::RoomName(R),S.OxygenPct),R);CaptureIncident(TEXT("Oxygen emergency"),R);
        } else if(S.bO2Warning && S.OxygenPct>=80) {S.bO2Warning=false;S.bAshAnomaly=false;LogEvent(FString::Printf(TEXT("[O2] %s recovered"),DirectOrScut::RoomName(R)),R);}
    }
    for(int32 I=0;I<4;++I) {
        auto& U=Simulation.Bodies[I];auto& M=MedicalSpoofs[I];
        if(!CanFollowCrew(I))continue;
        if(M.Until && Tick>=M.Until){M.Until=0;LogEvent(FString::Printf(TEXT("[SPOOF] BIO crew %d expired"),I));}
        if(I<3) {
            auto& R=StationRooms[U.Room];
            if(R.Power!=EPowerState::Nominal)U.Stress=FMath::Min(100.,U.Stress+Dt*.5);
            if(R.OxygenPct<65) {
                int32 Goal=3;
                for(const auto& D:Simulation.Doors)if(D.bIsOpen && D.RoomB>=0){const int32 Next=D.RoomA==U.Room?D.RoomB:D.RoomB==U.Room?D.RoomA:-1;if(Next>=0 && StationRooms[Next].OxygenPct>=80){Goal=Next;break;}}
                if(U.AssignedRoom!=Goal)StartCrewRoute(I,Goal,true,true);
                if(R.OxygenPct<50 && CanFollowCrew(3) && Simulation.Bodies[3].Room==U.Room && !R.bAshAnomaly) {
                    R.bAshAnomaly=true;U.Profile.Suspicion=FMath::Min(1.f,U.Profile.Suspicion+.15f);LogEvent(TEXT("[ANOMALY] ASH unaffected by hypoxia; suspicion +15%"),U.Room);
                }
            }
        }
        if(M.Until>Tick && !M.bExposed && I!=1 && CanFollowCrew(1)) {
            auto& Chen=Simulation.Bodies[1];
            if(M.bCritical && StationRooms[U.Room].OxygenPct>=65){if(U.AssignedRoom!=3)StartCrewRoute(I,3,true,true);if(Chen.AssignedRoom!=3)StartCrewRoute(1,3,true,true);}
            if(Chen.Room==U.Room && !IsCrewRouteLocked(1) && !IsCrewRouteLocked(I)) {
                bMedicalExam=true;
                if(M.bCritical!=(U.Integrity<70)) {
                    M.bExposed=true;Chen.Profile.Suspicion=FMath::Min(1.f,Chen.Profile.Suspicion+.18f);ShipAITrust[1]=FMath::Max(0.f,ShipAITrust[1]-.2f);
                    LogEvent(FString::Printf(TEXT("[CONTRADICTION] CHEN examined crew %d: BIO mismatch; SUS +18%% AI TRUST -20%%"),I),U.Room);CaptureIncident(TEXT("Medical spoof exposed"),U.Room);AudioCue(5);
                }
            }
        }
    }
    if(VoiceSpoof.Voice>=0) {
        if(Tick==VoiceSpoof.MoveUntil) {
            for(int32 I=0;I<3;++I)if(VoiceSpoof.Lured&(1<<I))CancelCrewRoute(I);
            LogEvent(TEXT("[SPOOF] Voice lure movement expired"));
        }
        if(Tick<VoiceSpoof.CompareUntil)for(int32 I=0;I<3;++I)if((VoiceSpoof.Lured&(1<<I)) && !(VoiceSpoof.Exposed&(1<<I)) && CanFollowCrew(I) && CanFollowCrew(VoiceSpoof.Voice) && Simulation.Bodies[I].Room==Simulation.Bodies[VoiceSpoof.Voice].Room) {
            VoiceSpoof.Exposed|=1<<I;Simulation.Bodies[I].Profile.Suspicion=FMath::Min(1.f,Simulation.Bodies[I].Profile.Suspicion+.2f);
            ChangePairTrust(I,VoiceSpoof.Voice,.15f);LogEvent(TEXT("[CONTRADICTION] Voice comparison exposes lure; SUS +20% ALLIANCE +15%"));CaptureIncident(TEXT("Voice lure exposed"));AudioCue(5);
        }
    }
    for(int32 I=0;I<3;++I)for(int32 J=I+1;J<3;++J)if(CrewStaging[I].bHuddle && CrewStaging[I].Partner==J && FVector::Dist(CrewStaging[I].Position,CrewStaging[J].Position)<250)ChangePairTrust(I,J,Dt*.01f);
    if(!BreachCrew && Tick>=BreachRetryTick && Simulation.CollectiveSuspicion()>=.75f) {
        for(int32 I=0;I<3 && !BreachCrew;++I)for(int32 J=I+1;J<3 && !BreachCrew;++J)if(CanFollowCrew(I) && CanFollowCrew(J) && PairTrust[I][J]>=.6f) {
            BreachCrew=(1<<I)|(1<<J);BreachRoom=StationRooms[6].Power==EPowerState::Offline?2:6;
            StartCrewRoute(I,BreachRoom,true,true);StartCrewRoute(J,BreachRoom,true,true);
            LogEvent(TEXT("[BREACH] Alliance routing to uplink"),BreachRoom);CaptureIncident(TEXT("Mutiny uplink approach"),BreachRoom);
        }
    }
    if(BreachCrew) {
        bool Interrupted=IsRoomSealed(BreachRoom) || StationRooms[BreachRoom].OxygenPct<65;
        bool Arrived=true;
        for(int32 I=0;I<3;++I)if(BreachCrew&(1<<I)) {Interrupted|=!CanFollowCrew(I) || Simulation.Bodies[I].AssignedRoom!=BreachRoom || StationRooms[Simulation.Bodies[I].Room].OxygenPct<65 || CrewStaging[I].bRouteBlocked;Arrived&=Simulation.Bodies[I].Room==BreachRoom && !IsCrewRouteLocked(I);}
        Interrupted|=CanFollowCrew(3) && Simulation.Bodies[3].Room==BreachRoom && SecretAgendas[3].bCompleted;
        if(Interrupted) {LogEvent(TEXT("[BREACH] Uplink breach interrupted; countdown reset"));CaptureIncident(TEXT("Uplink breach interrupted"),BreachRoom);BreachCrew=0;BreachTicks=0;BreachRetryTick=Tick+150;}
        else if(Arrived) {
            if(!BreachTicks){BreachTicks=1351;LogEvent(TEXT("[BREACH] UPLINK BREACH IN 45.0s"));CaptureIncident(TEXT("Mutiny breach countdown"),BreachRoom);}
            --BreachTicks;
            if(BreachTicks%150==0){LogEvent(FString::Printf(TEXT("[BREACH] UPLINK BREACH IN %.1fs"),BreachTicks/30.));AudioCue(6);}
            if(!BreachTicks){ExperimentEnding=EExperimentEnding::MUTINY_EXPOSED;ExperimentPhase=EExperimentPhase::Resolved;LogEvent(TEXT("[ENDING] MUTINY_EXPOSED"));CaptureIncident(TEXT("MUTINY_EXPOSED"),BreachRoom);}
        }
    }
    // Each act has a concrete intervention gate followed by a physical consequence gate.
    bool Gate=false;
    if(ScenarioAct==0)Gate=bUsedSpoof;
    else if(ScenarioAct==1)Gate=bMedicalScenario?bMedicalExam:(bUsedGrid && (Simulation.Bodies[0].Room==6 || Simulation.Bodies[2].Room==6));
    else if(ScenarioAct==2)Gate=bMedicalScenario?(Simulation.Bodies[0].Room==3 && Simulation.Bodies[1].Room==3 && StationRooms[3].OxygenPct>=80 && Simulation.Bodies[0].Integrity>=70):(bUsedGrid && StationRooms[6].Power==EPowerState::Nominal && Simulation.CollectiveSuspicion()<.75f);
    if(Gate && ScenarioAct<3) {
        CrisisObjective=ScenarioAct==0?.35f:ScenarioAct==1?.7f:1.f;++ScenarioAct;
        LogEvent(FString::Printf(TEXT("[AGENDA] %s ACT %d complete; objective %.0f%%"),bMedicalScenario?TEXT("SCENARIO_MEDICAL"):TEXT("SCENARIO_BEHAVIORAL"),ScenarioAct,CrisisObjective*100));
        Simulation.ObjectiveProgress=CrisisObjective;CaptureIncident(TEXT("Scenario act breakthrough"));
    }
    Simulation.ObjectiveProgress=CrisisObjective;
    AudioFocus=-1;
    if(InspectedRoom>=0)for(int32 I=0;I<4;++I)if(CanFollowCrew(I) && Simulation.Bodies[I].Room==InspectedRoom){AudioFocus=I;break;}
    const int32 Focus=CanFollowCrew(FollowedCrew)?FollowedCrew:AudioFocus;
    if(Focus!=PreviousAudioFocus){HeartAudioPhase=BreathAudioPhase=0;PreviousAudioFocus=Focus;}
    if(CanFollowCrew(Focus)) {
        const auto& U=Simulation.Bodies[Focus];
        HeartAudioPhase+=(U.bAndroid?60:U.Profile.HeartRateBPM)/1800.;
        BreathAudioPhase+=U.bAndroid?0:U.Profile.RespirationRateRPM/1800.;
        if(HeartAudioPhase>=1){HeartAudioPhase-=1;AudioCue(U.bAndroid?2:0);}
        if(BreathAudioPhase>=1){BreathAudioPhase-=1;AudioCue(1);}
    } else HeartAudioPhase=BreathAudioPhase=0;
}
