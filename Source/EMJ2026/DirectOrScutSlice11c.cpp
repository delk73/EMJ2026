#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutGeometry.h"

void UDirectOrScutSimSubsystem::ResetSurveillance()
{
    for(auto& C:Cameras)C=FSurveillanceCameraState{};
    for(int32 I=0;I<12;++I){TallyTicks[I]=0;TallyRoom[I]=-1;}
    InspectedRoom=-1;PendingInspectedRoom=-2;
}
void UDirectOrScutSimSubsystem::StartExperiment()
{
    if(ExperimentPhase!=EExperimentPhase::Briefing){LogEvent(TEXT("Experiment initialization requires a fresh briefing."));return;}
    if(!Simulation.bReady){LogEvent(TEXT("Ship navigation is not ready for initialization."));return;}
    ExperimentPhase=EExperimentPhase::Active;Simulation.Accumulator=0;ResolvedCommandAccumulator=0;
    PeakSuspicion=Simulation.CollectiveSuspicion();++MenuRevision;
    RecordCue(2,.5f,TEXT("Experiment initialized // surveillance control active"));
}
FString UDirectOrScutSimSubsystem::BriefingObjective() const
{
    const auto& S=Simulation.Scenario;
    switch(S.HiddenObjectiveType) {
    case EHiddenObjectiveType::IsolateTargetInRoom: {
        const TCHAR* Names[]={TEXT("VALE"),TEXT("CHEN"),TEXT("ORTIZ"),TEXT("ASH")};
        return FString::Printf(TEXT("Covertly isolate %s in %s for 10 seconds. Complete before 600 seconds with global suspicion below 75%% and a viable human crew."),Names[FMath::Clamp(S.TargetCrew,0,3)],DirectOrScut::RoomName(FMath::Clamp(S.TargetRoom,0,7))); }
    case EHiddenObjectiveType::CompleteCovertSystemSweep:return TEXT("Use Ash to complete the covert system sweep in all eight compartments while preserving the public cover protocol.");
    default:return TEXT("Preserve crew viability and prevent exposure through the experiment deadline.");
    }
}
const TCHAR* UDirectOrScutSimSubsystem::CameraStateName(ECameraFeedState State)
{
    switch(State){case ECameraFeedState::Online:return TEXT("ONLINE");case ECameraFeedState::Looped:return TEXT("LOOPED");default:return TEXT("OFFLINE");}
}
ECameraFeedState UDirectOrScutSimSubsystem::CameraFeedState(int32 Room) const
{
    return Room>=0 && Room<8?Cameras[Room].State:ECameraFeedState::Offline;
}
void UDirectOrScutSimSubsystem::QueueCameraInspection(int32 Room)
{
    if(ExperimentPhase!=EExperimentPhase::Resolved && Room>=-1 && Room<8)PendingInspectedRoom=Room;
}
void UDirectOrScutSimSubsystem::ApplyCameraInspection()
{
    if(PendingInspectedRoom!=-2) {
        if(InspectedRoom!=PendingInspectedRoom) {
            InspectedRoom=PendingInspectedRoom;
            CommandRecords.Add({Simulation.TickIndex,FString(TEXT("inspect_cam "))+(InspectedRoom<0?TEXT("off"):DirectOrScut::RoomName(InspectedRoom)),true});
        }
        PendingInspectedRoom=-2;
    }
}
bool UDirectOrScutSimSubsystem::IsCameraTallyActive(int32 Room) const
{
    if(ExperimentPhase!=EExperimentPhase::Active || CameraFeedState(Room)!=ECameraFeedState::Online)return false;
    return Room==InspectedRoom || (CanFollowCrew(FollowedCrew) && Simulation.Bodies[FollowedCrew].Room==Room);
}
void UDirectOrScutSimSubsystem::TamperCamera(int32 Room,ECameraFeedState State)
{
    if(ExperimentPhase!=EExperimentPhase::Active || Room<0 || Room>=8 || Cameras[Room].RepairGraceTicks>0 || uint8(State)<=uint8(Cameras[Room].State))return;
    Cameras[Room].State=State;++MenuRevision;
    RecordCue(Room,.9f,FString::Printf(TEXT("CAMERA FEED INTERRUPTED // TAMPER DETECTED // %s %s"),DirectOrScut::RoomName(Room),CameraStateName(State)));
}
bool UDirectOrScutSimSubsystem::RepairCamera(int32 Room)
{
    if(ExperimentPhase!=EExperimentPhase::Active || Room<0 || Room>=8)return false;
    auto& C=Cameras[Room];
    if(C.State==ECameraFeedState::Online){LogEvent(TEXT("Camera diagnostics: feed already ONLINE."),Room);return true;}
    C=FSurveillanceCameraState{};C.RepairGraceTicks=CameraRepairGraceTicks;++MenuRevision;
    for(int32 I=0;I<12;++I)if(TallyRoom[I]==Room){TallyTicks[I]=0;TallyRoom[I]=-1;}
    RecordCue(Room,.7f,FString::Printf(TEXT("Camera diagnostic override restored %s // ONLINE // 10-second repair protection"),DirectOrScut::RoomName(Room)));
    return true;
}
void UDirectOrScutSimSubsystem::StepSurveillance()
{
    if(ExperimentPhase!=EExperimentPhase::Active)return;
    bool Saboteur[8]{};
    for(int32 I=0;I<12;++I) {
        auto& U=Simulation.Bodies[I];
        const bool Human=U.bRegistered && !U.bAndroid && U.Integrity>0 && U.State!=EDirectOrScutUnitState::Ejected && U.Room>=0 && U.Room<8;
        if(Human && U.Profile.Suspicion>=.35f && IsCameraTallyActive(U.Room)) {
            if(TallyRoom[I]!=U.Room){TallyTicks[I]=0;TallyRoom[I]=U.Room;}
            ++TallyTicks[I];
            if(TallyTicks[I]>TallyGraceTicks)U.Profile.Suspicion=FMath::Clamp(U.Profile.Suspicion+TallySuspicionPerSecond*float(Simulation.FixedDelta),0.f,1.f);
            if(TallyTicks[I]==TallyGraceTicks+1)RecordCue(U.Room,.7f,FString::Printf(TEXT("Subject %d noticed prolonged active camera tracking"),I));
        } else {TallyTicks[I]=0;TallyRoom[I]=-1;}
        if(Human && U.Profile.Suspicion>=.7f)Saboteur[U.Room]=true;
    }
    for(int32 R=0;R<8;++R) {
        auto& C=Cameras[R];
        if(C.RepairGraceTicks>0){--C.RepairGraceTicks;C.TamperTicks=0;continue;}
        C.TamperTicks=Saboteur[R]?FMath::Min(C.TamperTicks+1,CameraOfflineTicks):0;
        if(C.TamperTicks>=CameraOfflineTicks)TamperCamera(R,ECameraFeedState::Offline);
        else if(C.TamperTicks>=CameraLoopTicks)TamperCamera(R,ECameraFeedState::Looped);
    }
}
uint64 UDirectOrScutSimSubsystem::SurveillanceStateHash() const
{
    uint64 Hash=Simulation.StateHash();
    auto Add=[&](uint64 V){Hash^=V;Hash*=1099511628211ull;};
    Add(uint64(ExperimentPhase));Add(uint64(ExperimentEnding));Add(uint64(InspectedRoom+1));Add(uint64(FollowedCrew+1));
    for(const auto& C:Cameras){Add(uint64(C.State));Add(C.TamperTicks);Add(C.RepairGraceTicks);}
    for(int32 I=0;I<12;++I){Add(TallyTicks[I]);Add(uint64(TallyRoom[I]+1));}
    return Hash;
}
