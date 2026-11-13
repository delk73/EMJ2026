#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutGeometry.h"

FCompartmentSetTransform UDirectOrScutSimSubsystem::DefaultSetTransform(int32 R)
{
    // Logical IDs stay stable; placement follows the workstation feed order.
    const int32 TileForRoom[]={0,7,1,2,3,4,5,6};
    if(R<0 || R>=8)return {};
    const int32 Tile=TileForRoom[R];
    return {FVector((Tile%4)*3500,0,Tile<4?3500:0),R==1?90.f:0.f};
}
FVector UDirectOrScutSimSubsystem::MapSimulationToSet(int32 R,FVector2D P)
{
    if(R<0 || R>=8)return FVector(0,0,-1500);
    const auto Stage=DefaultSetTransform(R);
    FVector Local;
    if(R==1)
    {
        const double Angle=FMath::Atan2(P.Y,P.X);
        Local=FVector(Angle/PI*2700,(P.Size()-1140)*2,0);
    }
    else if(R==0)Local=FVector(P.X,P.Y,0);
    else
    {
        const float Yaw=DirectOrScut::Stations()[R-2].Face*45.f;
        Local=FRotator(0,-Yaw,0).RotateVector(FVector(P,0)-DirectOrScut::Radial(Yaw,DirectOrScut::PodCenterRadius()));
        Local*=1.4;
    }
    return Stage.WorldOrigin+FRotator(0,Stage.Yaw,0).RotateVector(Local);
}
FVector UDirectOrScutSimSubsystem::ToSetWorld(int32 Room,FVector2D Position) const
{
    const auto* Stage=CompartmentSets.Find(static_cast<ERoomId>(Room));
    if(!Stage)return MapSimulationToSet(Room,Position);
    const auto Default=DefaultSetTransform(Room);
    const FVector Local=FRotator(0,-Default.Yaw,0).RotateVector(MapSimulationToSet(Room,Position)-Default.WorldOrigin);
    return Stage->WorldOrigin+FRotator(0,Stage->Yaw,0).RotateVector(Local);
}
void FDirectOrScutSimulation::UpdateBiometrics()
{
    for(auto& U:Bodies)
    {
        auto& P=U.Profile;P.OnHeartbeatTick=false;P.OnBreathCycle=false;
        if(!U.bRegistered || U.Integrity<=0 || U.State==EDirectOrScutUnitState::Ejected)continue;
        const float Load=U.State==EDirectOrScutUnitState::Working?.25f:0.f;
        const float Light=U.Room>=0?FMath::Clamp(Rooms[U.Room].LightLevel,0.f,1.f):1.f;
        P.HeartRateBPM=U.bAndroid?60:FMath::Clamp(60.f+float(U.Stress)+P.Suspicion*12+Load*12,60.f,160.f);
        P.RespirationRateRPM=U.bAndroid?0:FMath::Clamp(12.f+float(U.Stress)*.2f+P.Suspicion*2+Load*4,12.f,34.f);
        P.PupilDilationMM=FMath::Clamp(2.5f+float(U.Stress)*.025f+P.Suspicion*1.5f+Load+(1-Light)*1.5f,2.5f,8.f);
        P.CardiacPhase+=P.HeartRateBPM/60.*FixedDelta;
        P.BreathPhase+=P.RespirationRateRPM/60.*FixedDelta;
        if(P.CardiacPhase>=1) {P.CardiacPhase-=1;P.OnHeartbeatTick=true;++P.HeartbeatCount;}
        if(P.BreathPhase>=1) {P.BreathPhase-=1;P.OnBreathCycle=true;++P.BreathCount;}
    }
}
void UDirectOrScutSimSubsystem::ConfigureScenario(bool Medical)
{
    bMedicalScenario=Medical;DirectorOptions.Reset();BeatDeck.Reset();
    Simulation.Scenario.ScenarioId=Medical?TEXT("SCENARIO_MEDICAL"):TEXT("SCENARIO_BEHAVIORAL");
    Simulation.Scenario.CoverStoryLabel=Medical?TEXT("Containment and crew recovery"):TEXT("Crew autonomy and systems audit");
    Simulation.Scenario.Events.Reset();
    auto Option=[&](const TCHAR* Label,bool Support,int32 Room,FCrewStatDelta Delta,EDirectorAction Action=EDirectorAction::Stats)
    { DirectorOptions.Add({Label,Support,Room,{Label,Delta,Action}}); };
    if(!Medical)
    {
        Option(TEXT("Grant Unscheduled Downtime"),true,-1,{-18,-.04f,0,.02f});
        Option(TEXT("Open System Telemetry"),true,-1,{-4,-.12f,0,0,.02f});
        Option(TEXT("Pair for Mentorship"),true,5,{-10,-.03f,.02f,.12f,.04f},EDirectorAction::Mentorship);
        Option(TEXT("Selective Commendation"),true,-1,{-6,-.02f,.06f,.06f,.04f});
        Option(TEXT("Sow Dissent (Audit)"),false,-1,{12,.16f,-.03f,-.12f});
        Option(TEXT("Isolate for Evaluation"),false,3,{8,.1f,.04f},EDirectorAction::Seal);
        Option(TEXT("Silent Android Subroutine"),false,6,{0,0,0,0,.05f},EDirectorAction::Android);
        Option(TEXT("Seal Bulkhead / Cycle Airlock (SCUT)"),false,7,{},EDirectorAction::Vent);
    }
    else
    {
        Option(TEXT("Emergency Triage & Sedation (MED_BAY)"),true,3,{-25,-.03f,.02f,0,0,20},EDirectorAction::Triage);
        Option(TEXT("Release Quarantine Restrictions"),true,-1,{-12,-.1f,-.02f,.04f},EDirectorAction::Release);
        Option(TEXT("Share Biometric Telemetry"),true,-1,{-8,-.12f,0,.02f});
        Option(TEXT("Delegate Sector Authority"),true,-1,{-5,-.06f,.04f,0,.08f});
        Option(TEXT("Seal Contaminated Sector"),false,3,{10,.12f,.04f},EDirectorAction::Seal);
        Option(TEXT("Deploy Android Orderly"),false,3,{},EDirectorAction::Android);
        Option(TEXT("Vent Bio-Hazard Compartment (SCUT)"),false,3,{},EDirectorAction::Vent);
    }
    auto Beat=[&](EBeatGate Gate,const TCHAR* Title)
    {
        FScenarioBeat B; B.Gate=Gate;B.Node.Subtitle=Title;
        B.Node.Choices={{TEXT("Offer support and disclose telemetry"),{-15,-.12f,0,.08f},EDirectorAction::Stats},
                        {TEXT("Restrict access and enforce evaluation"),{10,.15f,.08f,-.08f},EDirectorAction::Seal}};
        BeatDeck.Add(B);
    };
    if(Medical) {Beat(EBeatGate::Collapse,TEXT("Acute Biometric Collapse"));Beat(EBeatGate::Concealment,TEXT("Symptom Concealment"));Beat(EBeatGate::Quarantine,TEXT("Quarantine Standoff"));}
    else {Beat(EBeatGate::Suspicion,TEXT("Whisper Network"));Beat(EBeatGate::Stress,TEXT("Terminal Audit Attempt"));Beat(EBeatGate::AndroidTask,TEXT("Synthetic Anomaly"));}
    ++MenuRevision;
}
void UDirectOrScutSimSubsystem::RecordCue(int32 R,float Severity,const FString& Subtitle)
{
    CinematicCues.Add({Simulation.TickIndex,static_cast<ERoomId>(FMath::Clamp(R,0,7)),Severity,Subtitle});
    LogEvent(Subtitle,R);
}
void UDirectOrScutSimSubsystem::EvaluateBeatDeck()
{
    for(auto& B:BeatDeck)
    {
        if(B.bFired)continue;
        for(int32 I=0;I<4;++I)
        {
            const auto& U=Simulation.Bodies[I];
            if(!U.bRegistered || U.Integrity<=0 || U.State==EDirectOrScutUnitState::Ejected)continue;
            bool Gate=false;
            switch(B.Gate)
            {
            case EBeatGate::Suspicion:Gate=!U.bAndroid && U.Profile.Suspicion>=.35f;break;
            case EBeatGate::Stress:Gate=!U.bAndroid && U.Stress>=55 && U.Profile.Suspicion>=.25f;break;
            case EBeatGate::AndroidTask:Gate=U.bAndroid && U.TaskProgress>=3;break;
            case EBeatGate::Collapse:Gate=!U.bAndroid && (U.Integrity<60 || U.Profile.HeartRateBPM>=140);break;
            case EBeatGate::Concealment:Gate=!U.bAndroid && U.Profile.Suspicion>=.4f && U.Stress>=40;break;
            case EBeatGate::Quarantine:Gate=!U.bAndroid && U.Profile.Suspicion>=.3f && IsRoomSealed(U.Room) && Simulation.Rooms[U.Room].AlertSeconds>0;break;
            }
            if(Gate) {B.bFired=true;B.Crew=I;RecordCue(U.Room,.8f,B.Node.Subtitle+TEXT(" // interaction available in menu events"));++MenuRevision;break;}
        }
    }
}
void UDirectOrScutSimSubsystem::ApplyInteraction(const FInteractionChoice& Choice,int32 Crew,int32 Room)
{
    if(Choice.Action==EDirectorAction::Mentorship)
    {
        // Support opens only the stable shortest routes needed to gather two living humans.
        int32 Paired=0;
        for(int32 I=0;I<4 && Paired<2;++I)
        {
            auto& U=Simulation.Bodies[I];if(!U.bRegistered || U.bAndroid || U.Integrity<=0 || U.Room<0 || U.State==EDirectOrScutUnitState::Ejected)continue;
            int32 Parent[8];for(int32& P:Parent)P=-2;
            int32 Queue[8],Head=0,Tail=0;Queue[Tail++]=Room;Parent[Room]=-1;
            while(Head<Tail) {const int32 A=Queue[Head++];for(int32 D=0;D<8;++D) {const auto& Door=Simulation.Doors[D];const int32 B=Door.RoomA==A?Door.RoomB:Door.RoomB==A?Door.RoomA:-1;if(B>=0 && Parent[B]==-2){Parent[B]=D;Queue[Tail++]=B;}}}
            for(int32 A=U.Room;A!=Room;) {const int32 D=Parent[A];if(D<0)break;Simulation.SetDoorOpen(D,true);const auto& Door=Simulation.Doors[D];A=Door.RoomA==A?Door.RoomB:Door.RoomA;}
            Simulation.AssignRoom(I,Room);++Paired;
        }
    }
    int32 Mentored=0;
    for(int32 I=0;I<4;++I)
    {
        auto& U=Simulation.Bodies[I];
        if(!U.bRegistered || U.Integrity<=0 || U.State==EDirectOrScutUnitState::Ejected || (Crew>=0 && I!=Crew))continue;
        if(Choice.Action==EDirectorAction::Android) {if(U.bAndroid)Simulation.AssignTask(I,Room,true);continue;}
        if(U.bAndroid || (Choice.Action==EDirectorAction::Mentorship?Mentored>=2:(Crew<0 && Room>=0 && U.Room!=Room)))continue;
        if(Choice.Action==EDirectorAction::Mentorship)++Mentored;
        auto& P=U.Profile;const auto& D=Choice.Delta;
        U.Stress=FMath::Clamp(U.Stress+D.Stress,0.,100.);U.Integrity=FMath::Clamp(U.Integrity+D.Integrity,0.,100.);
        P.Suspicion=FMath::Clamp(P.Suspicion+D.Suspicion,0.f,1.f);P.Compliance=FMath::Clamp(P.Compliance+D.Compliance,0.f,1.f);
        P.Affinity=FMath::Clamp(P.Affinity+D.Affinity,-1.f,1.f);P.Utility=FMath::Clamp(P.Utility+D.Utility,0.f,1.f);
        if(Choice.Action==EDirectorAction::Triage)Simulation.AssignRoom(I,3);
    }
    if(Choice.Action==EDirectorAction::Seal)for(int32 D=0;D<9;++D) {const auto& Door=Simulation.Doors[D];if(Door.RoomA==Room || Door.RoomB==Room)Simulation.ToggleDoor(D,false);}
    if(Choice.Action==EDirectorAction::Release)for(int32 D=0;D<8;++D)Simulation.ToggleDoor(D,true);
    if(Choice.Action==EDirectorAction::Vent)Simulation.CycleAirlock(Room);
    RecordCue(Room<0?0:Room,Choice.Action==EDirectorAction::Vent?1.f:.5f,Choice.Label);
}
bool UDirectOrScutSimSubsystem::ExecuteDirectorOption(int32 Index)
{
    if(!DirectorOptions.IsValidIndex(Index))return false;
    const auto& O=DirectorOptions[Index];
    if(O.Choice.Action==EDirectorAction::Vent) {MenuRoom=O.Room;SetScreenMenu(EShipAIScreenMenu::ConfirmScut);return true;}
    const int32 Crew=O.Label==TEXT("Selective Commendation")?0:-1;
    ApplyInteraction(O.Choice,Crew,O.Room);return true;
}
bool UDirectOrScutSimSubsystem::ResolveBeat(int32 Index,int32 Choice)
{
    if(!BeatDeck.IsValidIndex(Index))return false;
    auto& B=BeatDeck[Index];if(!B.bFired || B.bResolved || !B.Node.Choices.IsValidIndex(Choice))return false;
    B.bResolved=true;ApplyInteraction(B.Node.Choices[Choice],B.Crew,Simulation.Bodies[B.Crew].Room);++MenuRevision;return true;
}
void UDirectOrScutSimSubsystem::FixedStep()
{
    FDirectOrScutUnitState Previous[4];bool Pressure[8];
    for(int32 I=0;I<4;++I)Previous[I]=Simulation.Bodies[I];
    for(int32 R=0;R<8;++R)Pressure[R]=Simulation.Rooms[R].bIsPressurized;
    ProcessDirectives();Simulation.Step();EvaluateBeatDeck();
    if(FollowedCrew>=0 && !CanFollowCrew(FollowedCrew))SetFollowedCrew(-1);
    for(int32 R=0;R<8;++R)if(Pressure[R]!=Simulation.Rooms[R].bIsPressurized)
        RecordCue(R,1,Simulation.Rooms[R].bIsPressurized?TEXT("Pressure restored"):TEXT("Compartment exposed to vacuum"));
    for(int32 I=0;I<4;++I)
    {
        const auto& U=Simulation.Bodies[I];const auto& V=Previous[I];if(!U.bRegistered)continue;
        if(U.Profile.OnHeartbeatTick)OnHeartbeatTick.Broadcast(I,Simulation.TickIndex);
        if(U.Profile.OnBreathCycle)OnBreathCycle.Broadcast(I,Simulation.TickIndex);
        if(FMath::Abs(U.Stress-V.Stress)>=15 || FMath::Abs(U.Profile.Suspicion-V.Profile.Suspicion)>=.12f || FMath::Abs(U.Integrity-V.Integrity)>=10 || U.State!=V.State)
            RecordCue(U.Room,.7f,FString::Printf(TEXT("Crew %d: %s | BPM %.0f RPM %.0f pupil %.1f mm"),I,DirectOrScutStateName(U.State),U.Profile.HeartRateBPM,U.Profile.RespirationRateRPM,U.Profile.PupilDilationMM));
    }
}
