#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutSetAnchor.h"
#include "DirectOrScutHUD.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
class FSlice12Check : public IAutomationLatentCommand {
public:
    explicit FSlice12Check(FAutomationTestBase* In):T(In){}
    bool Update() override {
        auto* W=GEditor->PlayWorld.Get();if(!W || !W->HasBegunPlay())return false;
        auto* S=W->GetSubsystem<UDirectOrScutSimSubsystem>();if(!S || !S->Simulation.bReady)return false;
        auto* PC=W->GetFirstPlayerController();auto* H=PC?Cast<ADirectOrScutHUD>(PC->GetHUD()):nullptr;
        if(!H || !H->GetWorkstation() || H->GetWorkstation()->GetCachedGeometry().GetLocalSize().X<100)return false;
        if(CaptureAfter>0 && FPlatformTime::Seconds()<CaptureAfter)return false;
        S->bAutomaticAdvance=false;
        auto& M=S->Simulation;
        if(Stage++==0){
            T->TestEqual(TEXT("104 editor anchors loaded"),S->SetAnchors.Num(),104);
            S->ResetExperiment();T->TestEqual(TEXT("Reset discovers anchors"),S->SetAnchors.Num(),104);
            T->TestTrue(TEXT("Briefing hides agendas"),S->AgendaTelemetry(0).Contains(TEXT("ENCRYPTED")));
            S->QueueConsoleCommand(TEXT("start_experiment"));S->AdvanceSteps(1);
            for(int32 I=0;I<4;++I){M.Bodies[I].Room=M.Bodies[I].AssignedRoom=2;M.Bodies[I].Integrity=100;M.Bodies[I].Stress=0;M.Bodies[I].Profile.Suspicion=.1f;}
            S->AdvanceSteps(1);
            T->TestTrue(TEXT("Face-to-face pairing"),S->CrewStaging[0].Partner==1 && S->CrewStaging[1].Partner==0);
            T->TestTrue(TEXT("Pair reserves unique anchors"),S->CrewStaging[0].Anchor!=S->CrewStaging[1].Anchor);
            const FVector Before=S->CrewStaging[0].Position;S->AdvanceSteps(1);
            T->TestTrue(TEXT("Transit capped at 180 uu/s"),FVector::Dist(Before,S->CrewStaging[0].Position)<=6.001);
            M.TickIndex=200;S->AdvanceSteps(100);
            T->TestTrue(TEXT("Console anchor assigned"),S->CrewStaging[0].Anchor>=0);
            const auto* A=S->SetAnchors[S->CrewStaging[0].Anchor].Get();
            T->TestTrue(TEXT("Console facing follows anchor arrow"),FMath::Abs(FMath::FindDeltaAngleDegrees(S->CrewStaging[0].Yaw,A->GetActorRotation().Yaw+180))<1);
            M.Bodies[0].Profile.Suspicion=.7f;M.Bodies[1].Profile.Suspicion=.5f;S->AdvanceSteps(160);
            T->TestTrue(TEXT("Unmonitored feed tampered"),S->CameraFeedState(2)!=ECameraFeedState::Online);
            for(int32 I=0;I<4;++I)T->AddInfo(FString::Printf(TEXT("Crew %d room %d assigned %d suspicion %.3f stress %.1f posture %d partner %d"),I,M.Bodies[I].Room,M.Bodies[I].AssignedRoom,M.Bodies[I].Profile.Suspicion,M.Bodies[I].Stress,int32(S->CrewStaging[I].Posture),S->CrewStaging[I].Partner));
            T->TestTrue(TEXT("Blind spot huddle"),S->CrewStaging[0].bHuddle && S->CrewStaging[1].bHuddle);
            S->QueueConsoleCommand(TEXT("repair_cam BRIDGE"));S->AdvanceSteps(1);
            T->TestFalse(TEXT("Camera repair interrupts huddle"),S->CrewStaging[0].bHuddle);
            S->SecretAgendas[0].bActivated=true;M.Bodies[0].Room=M.Bodies[0].AssignedRoom=0;M.Bodies[0].State=EDirectOrScutUnitState::Working;
            for(int32 I=0;I<160;++I){M.Bodies[0].State=EDirectOrScutUnitState::Working;S->StepStagingAndAgendas();}
            T->TestTrue(TEXT("Vale agenda completes deterministic dwell"),S->SecretAgendas[0].bCompleted);
            T->TestTrue(TEXT("Final dossier reveals secret"),S->AgendaTelemetry(0,true).Contains(TEXT("Covert Reactor Override")));
            M.Bodies[2].Room=M.Bodies[2].AssignedRoom=3;M.Bodies[2].Integrity=0;
            M.Bodies[1].Room=M.Bodies[1].AssignedRoom=3;S->StepStagingAndAgendas();
            T->TestTrue(TEXT("Prone casualty recovery anchor"),S->CrewStaging[2].Posture==ECrewStagingPosture::Incapacitated && S->CrewStaging[2].Anchor>=0);
            T->TestTrue(TEXT("Chen triage posture"),S->CrewStaging[1].Posture==ECrewStagingPosture::Triage);
            // Exercise each independent agenda without renderer or variable frame-time input.
            M.Rooms[2].AlertSeconds=M.Rooms[6].AlertSeconds=0;
            S->SecretAgendas[2].bActivated=true;M.Bodies[2].Integrity=100;M.Bodies[2].Room=M.Bodies[2].AssignedRoom=6;
            const float AuditSuspicion=M.Bodies[2].Profile.Suspicion;
            for(int32 N=0;N<151;++N){M.Bodies[2].State=EDirectOrScutUnitState::Working;S->StepStagingAndAgendas();}
            T->TestTrue(TEXT("Ortiz completes audit and raises suspicion"),S->SecretAgendas[2].bCompleted && M.Bodies[2].Profile.Suspicion>AuditSuspicion);
            M.Bodies[0].Integrity=0;M.Bodies[1].Room=M.Bodies[1].AssignedRoom=2;
            for(int32 N=0;N<151;++N){M.Bodies[1].State=EDirectOrScutUnitState::Working;S->StepStagingAndAgendas();}
            T->TestTrue(TEXT("Chen broadcasts casualty distress log"),S->SecretAgendas[1].bCompleted);
            for(int32 I=0;I<3;++I)M.Bodies[I].Profile.Suspicion=.65f;
            M.Bodies[3].Room=M.Bodies[3].AssignedRoom=2;
            for(int32 N=0;N<151;++N){M.Bodies[3].State=EDirectOrScutUnitState::Working;S->StepStagingAndAgendas();}
            T->TestTrue(TEXT("Ash activates and enforces containment"),S->SecretAgendas[3].bActivated && S->SecretAgendas[3].bCompleted);
            M.Bodies[0].Integrity=100;M.Bodies[0].Room=2;M.Bodies[0].AssignedRoom=6;S->StepStagingAndAgendas();
            T->TestTrue(TEXT("Sealed transit produces blocked posture and BPM spike"),S->CrewStaging[0].Posture==ECrewStagingPosture::Blocked && M.Bodies[0].Profile.HeartRateBPM>=120);
            S->SynchronizeActors();H->GetWorkstation()->ShowGrid();CaptureAfter=FPlatformTime::Seconds()+1;return false;
        }
        TArray<FColor> Pixels;FIntVector Size;
        bool OK=FSlateApplication::Get().TakeScreenshot(H->GetWorkstation()->GetContentRoot(),Pixels,Size);
        T->TestTrue(TEXT("Verification screenshot captured"),OK);
        if(OK){TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,Png);T->TestTrue(TEXT("Verification screenshot saved"),FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice12PIE.png"))));}
        return true;
    }
private:FAutomationTestBase* T;int32 Stage=0;double CaptureAfter=0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlice12Test,"DirectOrScut.Slice12.StagingAgendasPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSlice12Test::RunTest(const FString&){
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSlice12Check>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
