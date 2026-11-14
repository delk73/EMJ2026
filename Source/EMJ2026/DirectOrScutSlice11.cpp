#include "DirectOrScutSimSubsystem.h"

const TCHAR* UDirectOrScutSimSubsystem::EndingName(EExperimentEnding E)
{
    switch(E) {
    case EExperimentEnding::COVERT_SUCCESS:return TEXT("COVERT_SUCCESS");
    case EExperimentEnding::MUTINY_EXPOSED:return TEXT("MUTINY_EXPOSED");
    case EExperimentEnding::CREW_COLLAPSE:return TEXT("CREW_COLLAPSE");
    case EExperimentEnding::SCUT_CONTAINMENT:return TEXT("SCUT_CONTAINMENT");
    case EExperimentEnding::EXPERIMENT_INCOMPLETE:return TEXT("EXPERIMENT_INCOMPLETE");
    default:return TEXT("ACTIVE"); }
}
bool UDirectOrScutSimSubsystem::BeginScut(int32 Room)
{
    if(ExperimentPhase!=EExperimentPhase::Active || Room<0 || Room>=8)return false;
    uint16 Targets=0;
    for(int32 I=0;I<12;++I) {
        const auto& U=Simulation.Bodies[I];
        if(U.bRegistered && !U.bAndroid && U.Integrity>0 && U.Room==Room &&
           (U.Profile.Suspicion>=.75f || (bMedicalScenario && (U.Integrity<60 || U.Profile.HeartRateBPM>=140))))Targets|=1<<I;
    }
    if(!Simulation.CycleAirlock(Room))return false;
    ScutTargets|=Targets;SecretAgendas[3].bActivated=true;
    RecordCue(Room,1,TEXT("Intentional SCUT containment protocol initiated"));
    return true;
}
void UDirectOrScutSimSubsystem::EvaluateExperimentEnding()
{
    if(ExperimentPhase!=EExperimentPhase::Active)return;
    const float Suspicion=Simulation.CollectiveSuspicion();
    PeakSuspicion=FMath::Max(PeakSuspicion,Suspicion);
    ExposureTicks=Suspicion>=.85f?ExposureTicks+1:0;
    bool HasHuman=false,Viable=false,TargetsEliminated=ScutTargets!=0 && (ScutEliminated&ScutTargets)==ScutTargets;
    for(int32 I=0;I<12;++I) {
        const auto& U=Simulation.Bodies[I];
        const bool Alive=U.bRegistered && U.Integrity>0 && U.State!=EDirectOrScutUnitState::Ejected;
        if(U.bRegistered && !U.bAndroid){HasHuman=true;Viable|=Alive;}
        if((ScutTargets&(1<<I)) && Alive)TargetsEliminated=false;
    }
    // Vacuum or hull breach is the existing simulation's structural compromise signal.
    for(int32 R:{2,6})bScutProtected&=Simulation.Rooms[R].bIsPressurized && !Simulation.Rooms[R].bHullBreach;
    EExperimentEnding Result=EExperimentEnding::None;
    if(TargetsEliminated && bScutProtected)Result=EExperimentEnding::SCUT_CONTAINMENT;
    else if(HasHuman && !Viable)Result=EExperimentEnding::CREW_COLLAPSE;
    else if(Simulation.TickIndex<ExperimentTicks && Simulation.ObjectiveProgress>=1 && !Simulation.bObjectiveFailed && Suspicion<.75f && Viable)Result=EExperimentEnding::COVERT_SUCCESS;
    else if(Simulation.TickIndex>=ExperimentTicks)Result=EExperimentEnding::EXPERIMENT_INCOMPLETE;
    if(Result==EExperimentEnding::None)return;
    ExperimentEnding=Result;ExperimentPhase=EExperimentPhase::Resolved;Simulation.Accumulator=0;
    RecordCue(2,1,FString(TEXT("Corporate evaluation: "))+EndingName(Result));
    CaptureIncident(FString(TEXT("Terminal ending: "))+EndingName(Result),2);
    PendingDirectives.Reset();PendingConsoleCommands.Reset();
    ActiveMenu=EShipAIScreenMenu::None;MenuParents.Reset();++MenuRevision;
}
