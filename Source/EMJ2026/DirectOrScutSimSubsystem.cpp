#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutGeometry.h"
#include "DoorActor.h"
#include "CrewUnitBlockout.h"
#include "Engine/World.h"

namespace
{
class FOfflineShipAIIntentProvider final : public IShipAIIntentProvider
{
public:
    FShipAIIntent Translate(const FString& Input,const FSimContextSnapshot&) override
    {
        FString Line=Input.TrimStartAndEnd().ToLower();
        if (Line==TEXT("calm everyone") || Line==TEXT("reassure the crew")) return {TEXT("calm"),TEXT("")};
        if (Line==TEXT("report status")) return {TEXT("status"),TEXT("")};
        if (Line.StartsWith(TEXT("send ")))
        {
            Line=Line.Mid(5).Replace(TEXT(" to "),TEXT(" "));
            return {TEXT("direct ")+Line,TEXT("telemetry maintenance")};
        }
        const TCHAR* Commands[]={TEXT("direct "),TEXT("alert "),TEXT("protocol "),TEXT("open "),TEXT("close "),TEXT("seal "),TEXT("vent "),TEXT("scut "),TEXT("flush "),TEXT("cam ")};
        for (const auto* Command:Commands) if (Line.StartsWith(Command)) return {Line,TEXT("")};
        if (Line==TEXT("calm") || Line==TEXT("status") || Line==TEXT("crew") || Line==TEXT("rooms")) return {Line,TEXT("")};
        return {};
    }
};
}
bool UDirectOrScutSimSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const auto* World=Cast<UWorld>(Outer);
    return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}
void UDirectOrScutSimSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection); ResetSurveillance(); for(int32 R=0;R<8;++R) CompartmentSets.Add(static_cast<ERoomId>(R),DefaultSetTransform(R)); Simulation.Initialize(); ConfigureScenario(false); Simulation.bExperiment=true; IntentProvider=MakeShared<FOfflineShipAIIntentProvider>();
}
bool UDirectOrScutSimSubsystem::IsTickable() const
{
    return !IsTemplate() && bAutomaticAdvance && Simulation.bReady && GetWorld() && GetWorld()->HasBegunPlay();
}
TStatId UDirectOrScutSimSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UDirectOrScutSimSubsystem,STATGROUP_Tickables); }
void UDirectOrScutSimSubsystem::Tick(float Delta)
{
    if (!FMath::IsFinite(Delta) || Delta<=0) return;
    if(ExperimentPhase!=EExperimentPhase::Active) {
        ResolvedCommandAccumulator+=Delta;
        int32 ControlBudget=240;
        while(ExperimentPhase!=EExperimentPhase::Active && ResolvedCommandAccumulator+1.e-9>=Simulation.FixedDelta && ControlBudget-->0) {
            ResolvedCommandAccumulator-=Simulation.FixedDelta;FixedStep();
        }
        return;
    }
    Simulation.Accumulator+=Delta;
    int32 Budget=240;
    while (ExperimentPhase==EExperimentPhase::Active && Simulation.Accumulator+1.e-9>=Simulation.FixedDelta && Budget-->0)
    { FixedStep(); Simulation.Accumulator=FMath::Max(0.,Simulation.Accumulator-Simulation.FixedDelta); }
    SynchronizeActors();
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
    Simulation.RegisterBody(I,FVector2D(DirectOrScut::CrewSpawnPoint(I)),I+2,Crew->bAndroid);
    Simulation.Bodies[I].bSuited=Crew->bSuited;
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
    if (ExperimentPhase!=EExperimentPhase::Active || !Simulation.SetDoorOpen(I,Open)) return false;
    if (auto* Door=DoorActors[I].Get()) Door->ApplySimulationOpen(Open);
    SynchronizeActors(); return true;
}
void UDirectOrScutSimSubsystem::SynchronizeActors()
{
    for (int32 I=0;I<9;++I) if (auto* Door=DoorActors[I].Get()) Door->ApplySimulationOpen(Simulation.Doors[I].bIsOpen);
    for (int32 I=0;I<12;++I)
    {
        if (auto* Actor=BodyActors[I].Get())
        {
            const auto& U=Simulation.Bodies[I];
            if (!U.bRegistered) continue;
            Actor->SetActorLocation(ToSetWorld(U.Room,U.Position),false);
            if(const auto* Stage=CompartmentSets.Find(static_cast<ERoomId>(U.Room)))Actor->SetActorRotation(FRotator(0,Stage->Yaw,0));
            if (U.State==EDirectOrScutUnitState::Ejected) Actor->SetActorEnableCollision(false);
            else Actor->SetActorEnableCollision(true);
        }
    }
}
void UDirectOrScutSimSubsystem::AdvanceSteps(int32 Count)
{
    for (int32 I=0;I<Count;++I) { FixedStep(); }
    SynchronizeActors();
}
void UDirectOrScutSimSubsystem::ResetSimulation()
{
    ResolvedCommandAccumulator=0;
    ++RuntimeRevision;
    ExperimentPhase=EExperimentPhase::Briefing; ResetSurveillance(); ExperimentEnding=EExperimentEnding::None;
    PeakSuspicion=0; ExposureTicks=0; ScutTargets=0; ScutEliminated=0; bScutProtected=true;
    ++EventRevision;
    PendingConsoleCommands.Reset(); MenuParents.Reset(); ActiveMenu=EShipAIScreenMenu::None; ++MenuRevision;
    PendingDirectives.Reset(); Events.Reset(); bAILink=false; RequestedFeed=-1; FollowedCrew=-1; ++FollowRevision;
    CommandRecords.Reset(); CinematicCues.Reset(); ConfigureScenario(bMedicalScenario);
    Simulation.ResetRuntime(); Simulation.Scenario.ShiftDuration=600;
    for (int32 I=0;I<9;++I) if (auto* Door=DoorActors[I].Get()) Door->ApplySimulationOpen(false);
    SynchronizeActors();
}
ADoorActor* UDirectOrScutSimSubsystem::GetDoorActor(int32 I) const { return I>=0 && I<9?DoorActors[I].Get():nullptr; }
AActor* UDirectOrScutSimSubsystem::GetBodyActor(int32 I) const { return I>=0 && I<12?BodyActors[I].Get():nullptr; }

#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
int32 UDirectOrScutSimSubsystem::ResolveRoom(const FString& Name)
{
    for (int32 R=0;R<8;++R) if (Name.Equals(DirectOrScut::RoomName(R),ESearchCase::IgnoreCase)) return R;
    if (Name.Equals(TEXT("airlock"),ESearchCase::IgnoreCase)) return 7;
    return -1;
}
void UDirectOrScutSimSubsystem::LogEvent(const FString& Text,int32 Room,bool TerminalVisible)
{
    ++EventRevision;
    Events.Add({FString::Printf(TEXT("[%06llu] %s"),Simulation.TickIndex,*Text),Room,TerminalVisible});
}
FString UDirectOrScutSimSubsystem::SubmitDirective(const FString& Input)
{
    if(ExperimentPhase==EExperimentPhase::Resolved)return TEXT("Experiment resolved. Use reset_experiment or scenario behavioral|medical.");
    FString Line=Input.TrimStartAndEnd().ToLower();
    if (Line==TEXT("llm") || Line==TEXT("ai link")) { bAILink=true; return TEXT("AI-LINK> Local offline intent provider ready. Use explicit Ship AI commands."); }
    if (Line==TEXT("cmd") || Line==TEXT("exit")) { bAILink=false; return TEXT("CMD> Deterministic control restored."); }
    if (bAILink)
    {
        if (!IntentProvider) IntentProvider=MakeShared<FOfflineShipAIIntentProvider>();
        const auto Intent=IntentProvider->Translate(Input,MakeContextSnapshot());
        if (Intent.Command.IsEmpty()) return TEXT("AI-LINK> Offline: use explicit commands or send <name|role> to <ROOM>, calm everyone, report status.");
        Line=Intent.Command.ToLower();
        if (Line.StartsWith(TEXT("direct ")) && !Intent.Pretext.IsEmpty()) Line+=TEXT(" ")+Intent.Pretext;
    }
    if (Line==TEXT("status")) return FString::Printf(TEXT("%s\nCover: %s\nShift %.1f / %.1fs | Objective %.0f%% | Suspicion %.0f%% | %s"),*Simulation.Scenario.ScenarioId,*Simulation.Scenario.CoverStoryLabel,Simulation.TickIndex/30.,Simulation.Scenario.ShiftDuration,Simulation.ObjectiveProgress*100,Simulation.CollectiveSuspicion()*100,Simulation.bObjectiveFailed?TEXT("FAILED"):Simulation.bShiftComplete?TEXT("SHIFT COMPLETE"):ExperimentPhase==EExperimentPhase::Briefing?TEXT("BRIEFING"):TEXT("ACTIVE"));
    if (Line==TEXT("crew"))
    {
        FString Result;
        const TCHAR* Names[]={TEXT("VALE / engineer"),TEXT("CHEN / medic"),TEXT("ORTIZ / technician"),TEXT("ASH / android")};
        for (int32 I=0;I<4;++I) { const auto& U=Simulation.Bodies[I]; const auto& P=U.Profile;
            Result+=FString::Printf(TEXT("%s %s %s Cmp %.2f Cps %.2f Aff %.2f Utl %.2f Sus %.2f Stress %.0f Integrity %.0f BPM %.0f RPM %.0f Pupil %.1fmm\n"),Names[I],DirectOrScut::RoomName(U.Room),DirectOrScutStateName(U.State),P.Compliance,P.Composure,P.Affinity,P.Utility,P.Suspicion,U.Stress,U.Integrity,P.HeartRateBPM,P.RespirationRateRPM,P.PupilDilationMM); }
        return Result;
    }
    if (Line==TEXT("rooms")) { FString Result; for (int32 R=0;R<8;++R) Result+=FString::Printf(TEXT("%s %s alert %.0fs\n"),DirectOrScut::RoomName(R),Simulation.Rooms[R].bIsPressurized?TEXT("PRESSURIZED"):TEXT("VACUUM"),Simulation.Rooms[R].AlertSeconds); return Result; }
    if (Line.StartsWith(TEXT("cam "))) { const FString N=Line.Mid(4); if (N.Len()==1 && N[0]>='1' && N[0]<='8') { RequestedFeed=N[0]-'0'; return TEXT("Camera selected."); } return TEXT("Camera must be 1 through 8."); }
    if(ExperimentPhase!=EExperimentPhase::Active)return TEXT("BRIEFING // use start_experiment to initialize before crew directives.");
    if (PendingDirectives.Num()>=32) return TEXT("Command queue full.");
    PendingDirectives.Add(Line); return TEXT("Directive queued for next 30 Hz tick.");
}
void UDirectOrScutSimSubsystem::ProcessDirectives()
{
    for(const auto& Line:PendingDirectives)CommandRecords.Add({Simulation.TickIndex,Line,false});
    for(const auto& Line:PendingConsoleCommands)CommandRecords.Add({Simulation.TickIndex,Line,true});
    ProcessSimulationDirectives();
    ProcessConsoleCommands();
}
void UDirectOrScutSimSubsystem::ProcessSimulationDirectives()
{
    if(ExperimentPhase!=EExperimentPhase::Active) { PendingDirectives.Reset();return; }
    for (const auto& Line:PendingDirectives)
    {
        TArray<FString> T; Line.ParseIntoArrayWS(T); if (T.IsEmpty()) continue;
        const FString Op=T[0]; int32 R=T.Num()>1?ResolveRoom(T[1]):-1;
        if (Op==TEXT("direct") && T.Num()>=3)
        {
            const TCHAR* Names[]={TEXT("vale"),TEXT("chen"),TEXT("ortiz"),TEXT("ash")};
            const TCHAR* Roles[]={TEXT("engineer"),TEXT("medic"),TEXT("technician"),TEXT("android")};
            int32 I=-1; for (int32 J=0;J<4;++J) if (T[1]==Names[J] || T[1]==Roles[J]) I=J;
            R=ResolveRoom(T[2]);
            const float Before=I>=0?Simulation.Bodies[I].Profile.Suspicion:0;
            const bool OK=Simulation.AssignTask(I,R,T.Num()>3);
            const bool Suspicious=I>=0 && Simulation.Bodies[I].Profile.Suspicion>Before;
            const TCHAR* Outcome=OK?TEXT("complied"):I>=0 && Simulation.Bodies[I].Profile.Suspicion<=.75f?TEXT("hesitated / needs physical trigger"):TEXT("refused");
            LogEvent(FString::Printf(TEXT("Ship AI: diagnostic maintenance routing %s -> %s: %s%s"),*T[1],*T[2],Outcome,Suspicious?TEXT("; suspicion increased"):TEXT("")),R);
        }
        else if (Op==TEXT("alert") && R>=0 && T.Num()==3 && (T[2]==TEXT("fault") || T[2]==TEXT("leak") || T[2]==TEXT("drill")))
        { Simulation.TriggerPretextAlert(R); LogEvent(TEXT("Ship AI: localized sensor anomaly; diagnostic personnel requested."),R); }
        else if (Op==TEXT("calm") || (Op==TEXT("protocol") && R>=0)) { Simulation.BroadcastProtocol(Op==TEXT("calm")?-1:R); LogEvent(TEXT("Ship AI: automated reassurance protocol broadcast."),R); }
        else if ((Op==TEXT("open") || Op==TEXT("close") || Op==TEXT("seal")) && T.Num()==2)
        {
            bool Any=false; const bool Open=Op==TEXT("open");
            for (int32 D=0;D<9;++D)
            {
                const auto& Door=Simulation.Doors[D];
                const bool Numeric=T[1].IsNumeric() && FCString::Atoi(*T[1])==D;
                if (!Numeric && T[1]!=TEXT("all") && !(R>=0 && (Door.RoomA==R || Door.RoomB==R))) continue;
                if (D==8 && Open) continue;
                Simulation.ToggleDoor(D,Open); Any=true;
            }
            LogEvent(Any?TEXT("Ship AI: bulkhead protocol applied."):TEXT("Invalid door or room; doors are 0-8, rooms use full names."),R);
        }
        else if (((Op==TEXT("vent") || Op==TEXT("scut")) && R==7) || (Op==TEXT("flush") && R>=0))
        {
            BeginScut(R);
            LogEvent(TEXT("Ship AI: atmospheric purge initiated. SCUT telemetry active."),R);
        }
        else LogEvent(TEXT("Syntax: direct <name|role> <ROOM> [pretext]; alert <ROOM> <fault|leak|drill>; calm; protocol <ROOM>; open/close/seal <door|ROOM|all>; vent airlock; flush <ROOM>; cam <1-8>; status; crew; rooms; llm; cmd"));
    }
    PendingDirectives.Reset();
}
FSimContextSnapshot UDirectOrScutSimSubsystem::MakeContextSnapshot() const
{
    FSimContextSnapshot S; S.Tick=Simulation.TickIndex;
    auto Writer=TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&S.CompactJson);
    Writer->WriteObjectStart(); Writer->WriteValue(TEXT("tick"),double(S.Tick)); Writer->WriteValue(TEXT("scenario"),Simulation.Scenario.ScenarioId); Writer->WriteValue(TEXT("objective"),Simulation.ObjectiveProgress); Writer->WriteValue(TEXT("suspicion"),Simulation.CollectiveSuspicion());
    Writer->WriteArrayStart(TEXT("crew"));
    for (int32 I=0;I<4;++I) { const auto& U=Simulation.Bodies[I]; const auto& P=U.Profile; Writer->WriteObjectStart(); Writer->WriteValue(TEXT("id"),I); Writer->WriteValue(TEXT("room"),U.Room); Writer->WriteValue(TEXT("state"),DirectOrScutStateName(U.State)); Writer->WriteValue(TEXT("compliance"),P.Compliance); Writer->WriteValue(TEXT("composure"),P.Composure); Writer->WriteValue(TEXT("affinity"),P.Affinity); Writer->WriteValue(TEXT("utility"),P.Utility); Writer->WriteValue(TEXT("suspicion"),P.Suspicion); Writer->WriteValue(TEXT("stress"),U.Stress); Writer->WriteValue(TEXT("integrity"),U.Integrity); Writer->WriteObjectEnd(); }
    Writer->WriteArrayEnd(); Writer->WriteArrayStart(TEXT("rooms"));
    for (int32 R=0;R<8;++R) { Writer->WriteObjectStart(); Writer->WriteValue(TEXT("name"),DirectOrScut::RoomName(R)); Writer->WriteValue(TEXT("pressurized"),Simulation.Rooms[R].bIsPressurized); Writer->WriteValue(TEXT("alert"),Simulation.Rooms[R].AlertSeconds); Writer->WriteObjectEnd(); }
    Writer->WriteArrayEnd(); Writer->WriteObjectEnd(); Writer->Close(); return S;
}

// Camera tracking is operator state; it never mutates crew orders or AI-LINK intent.
bool UDirectOrScutSimSubsystem::CanFollowCrew(int32 Crew) const
{
    if(Crew<0 || Crew>=4)return false;
    const auto& U=Simulation.Bodies[Crew];
    return U.bRegistered && U.Room>=0 && U.Room<8 && U.Integrity>0 && U.State!=EDirectOrScutUnitState::Ejected;
}
bool UDirectOrScutSimSubsystem::SetFollowedCrew(int32 Crew)
{
    if(Crew!=-1 && !CanFollowCrew(Crew))return false;
    FollowedCrew=Crew;++FollowRevision;LogEvent(Crew<0?TEXT("Follow mode stopped."):FollowStatus());return true;
}
FString UDirectOrScutSimSubsystem::FollowStatus() const
{
    if(!CanFollowCrew(FollowedCrew))return TEXT("FOLLOW OFF");
    const TCHAR* Names[]={TEXT("VALE"),TEXT("CHEN"),TEXT("ORTIZ"),TEXT("ASH")};
    return FString::Printf(TEXT("FOLLOWING: %s [%s]"),Names[FollowedCrew],DirectOrScut::RoomName(Simulation.Bodies[FollowedCrew].Room));
}

FString UDirectOrScutSimSubsystem::TerminalBuffer() const
{
    FString Result;
    for(const auto& E:Events)if(E.bTerminalVisible) { Result+=E.Text;Result+=TEXT("\n"); }
    return Result;
}
FString UDirectOrScutSimSubsystem::LatestPriorityAlert() const
{
    for(int32 I=CinematicCues.Num()-1;I>=0;--I)if(CinematicCues[I].Severity>=.7f) {
        const auto& C=CinematicCues[I];
        return FString::Printf(TEXT("[%06.1fs] %s"),C.Tick/30.,*C.Subtitle);
    }
    return TEXT("NO PRIORITY ALERTS");
}
