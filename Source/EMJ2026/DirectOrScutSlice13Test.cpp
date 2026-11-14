#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutAudioComponent.h"
#include "DirectOrScutHUD.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/StrongObjectPtr.h"

class FSlice13Check : public IAutomationLatentCommand {
public:
    explicit FSlice13Check(FAutomationTestBase* In):T(In){}
    bool Update() override {
        if(!Started)Started=FPlatformTime::Seconds();
        if(FPlatformTime::Seconds()-Started>120){T->AddError(TEXT("Slice13 PIE readiness timeout"));return true;}
        UWorld* W=GEditor->PlayWorld.Get();if(!W || !W->HasBegunPlay())return false;
        auto* S=W->GetSubsystem<UDirectOrScutSimSubsystem>();if(!S || !S->Simulation.bReady)return false;
        auto* PC=W->GetFirstPlayerController();auto* H=PC?Cast<ADirectOrScutHUD>(PC->GetHUD()):nullptr;
        if(!H || !H->GetWorkstation() || H->GetWorkstation()->GetCachedGeometry().GetLocalSize().X<100)return false;
        S->bAutomaticAdvance=false;auto& M=S->Simulation;
        auto Fresh=[&](){S->ResetExperiment();S->QueueConsoleCommand(TEXT("start_experiment"));S->AdvanceSteps(1);};
        auto Cmd=[&](const TCHAR* C){T->TestTrue(TEXT("CLI accepted"),S->QueueConsoleCommand(C));S->AdvanceSteps(1);};
        auto Place=[&](int32 I,int32 R){S->CancelCrewRoute(I);M.Bodies[I].Room=M.Bodies[I].AssignedRoom=R;S->InitializeCrewStaging(I);};
        if(Stage==0) {
            ++Stage;
            Fresh();Cmd(TEXT("power SERVER_ARCHIVE off"));
            T->TestTrue(TEXT("Power drops feed without camera tamper"),S->CameraFeedState(6)==ECameraFeedState::Offline && !S->TerminalBuffer().Contains(TEXT("TAMPER DETECTED")));
            Cmd(TEXT("power SERVER_ARCHIVE nominal"));T->TestTrue(TEXT("Power restores feed"),S->CameraFeedState(6)==ECameraFeedState::Online);
            Place(0,3);Place(1,3);Cmd(TEXT("falsify_bio vale critical"));
            T->TestEqual(TEXT("Spoof displays critical"),S->MedicalReadout(0),FString(TEXT("CRITICAL")));
            S->CancelCrewRoute(0);S->CancelCrewRoute(1);S->StepCrisis();
            T->TestTrue(TEXT("Chen exposes physical discrepancy"),S->MedicalSpoofs[0].bExposed && S->ShipAITrust[1]<=.8f);
            M.TickIndex=S->MedicalSpoofs[0].Until;S->StepCrisis();T->TestEqual(TEXT("Spoof expires exactly at sixty seconds"),S->MedicalReadout(0),FString(TEXT("NOMINAL")));
            S->SecretAgendas[2].Progress=100;Place(2,6);const float Before=M.Bodies[2].Profile.Suspicion;Cmd(TEXT("spoof_log BRIDGE"));
            T->TestTrue(TEXT("Archive rewrite resets audit and raises Ortiz suspicion"),S->SecretAgendas[2].Progress<=1 && M.Bodies[2].Profile.Suspicion>=Before+.119f);
            Place(1,M.Bodies[0].Room);const float Alliance=S->PairTrust[0][1];Cmd(TEXT("intercom_lie vale MED_BAY"));S->StepCrisis();
            T->TestTrue(TEXT("Voice comparison exposes spoof and raises alliance"),(S->VoiceSpoof.Exposed&2)!=0 && S->PairTrust[0][1]>=Alliance+.149f);
            Fresh();Place(0,2);for(int32 D=0;D<9;++D)if(M.Doors[D].RoomA==2 || M.Doors[D].RoomB==2)M.SetDoorOpen(D,false);
            for(int32 N=0;N<30;++N){++M.TickIndex;S->StepCrisis();}
            T->TestTrue(TEXT("Sealed occupied room drains 0.8 percent per second"),FMath::IsNearlyEqual(S->StationRooms[2].OxygenPct,99.2f,.002f));
            M.Rooms[2].AlertSeconds=10;for(int32 N=0;N<30;++N){++M.TickIndex;S->StepCrisis();}
            T->TestTrue(TEXT("Fault drains two percent per second"),FMath::IsNearlyEqual(S->StationRooms[2].OxygenPct,97.2f,.003f));
            M.Rooms[2].AlertSeconds=0;for(int32 D=0;D<8;++D)M.SetDoorOpen(D,true);
            for(int32 N=0;N<30;++N){++M.TickIndex;S->StepCrisis();}
            T->TestTrue(TEXT("Unsealed oxygen recovers"),S->StationRooms[2].OxygenPct>99.6f);
            Place(3,2);S->StationRooms[2].OxygenPct=49;S->StepCrisis();
            T->TestTrue(TEXT("Hypoxia routes humans and detects synthetic anomaly"),M.Bodies[0].AssignedRoom!=2 && S->StationRooms[2].bAshAnomaly);
            Fresh();Place(0,6);Place(1,6);Place(3,0);for(int32 I=0;I<3;++I)M.Bodies[I].Profile.Suspicion=.8f;
            S->ChangePairTrust(0,1,.2f);S->StepCrisis();
            T->TestTrue(TEXT("Alliance initiates physical approach"),S->BreachCrew!=0);
            S->CancelCrewRoute(0);S->CancelCrewRoute(1);S->StepCrisis();T->TestEqual(TEXT("Physical breach begins at 45 seconds"),S->BreachTicks,uint32(1350));
            Cmd(TEXT("intercom_lie ortiz MED_BAY"));T->TestEqual(TEXT("Voice lure interrupts breach"),S->BreachTicks,uint32(0));
            Fresh();Place(0,6);Place(1,6);Place(3,0);S->BreachCrew=3;S->BreachTicks=1350;
            for(int32 N=0;N<1350;++N){++M.TickIndex;S->StepCrisis();}
            T->TestTrue(TEXT("45 seconds reaches physical mutiny ending"),S->ExperimentEnding==EExperimentEnding::MUTINY_EXPOSED);
            auto BreachFixture=[&](){Fresh();Place(0,6);Place(1,6);Place(3,0);S->BreachCrew=3;S->BreachTicks=900;};
            BreachFixture();for(int32 D=0;D<8;++D)if(M.Doors[D].RoomA==6 || M.Doors[D].RoomB==6)M.SetDoorOpen(D,false);S->StepCrisis();T->TestEqual(TEXT("Sealing interrupts breach"),S->BreachCrew,uint8(0));
            BreachFixture();S->StationRooms[6].OxygenPct=60;S->StepCrisis();T->TestEqual(TEXT("Hypoxia interrupts breach"),S->BreachCrew,uint8(0));
            BreachFixture();Place(3,6);S->SecretAgendas[3].bCompleted=true;S->StepCrisis();T->TestEqual(TEXT("Corporate override interrupts breach"),S->BreachCrew,uint8(0));
            Fresh();Place(2,6);S->SecretAgendas[2].bActivated=true;S->SecretAgendas[2].Progress=40;Cmd(TEXT("power SERVER_ARCHIVE off"));S->AdvanceSteps(30);T->TestEqual(TEXT("Power outage pauses console audit"),S->SecretAgendas[2].Progress,uint32(40));
            Fresh();Cmd(TEXT("menu ai"));Cmd(TEXT("choose 7"));T->TestTrue(TEXT("Directives exposes information warfare"),S->ScreenMenuTitle().Contains(TEXT("INFORMATION WARFARE")));Cmd(TEXT("choose 1"));Cmd(TEXT("choose 1"));Cmd(TEXT("choose 2"));T->TestTrue(TEXT("TUI medical spoof uses authoritative command path"),S->MedicalSpoofs[0].Until>M.TickIndex);
            auto Replay=[&](){TStrongObjectPtr<UDirectOrScutSimSubsystem> P(NewObject<UDirectOrScutSimSubsystem>(W));P->Simulation=M;P->SetAnchors=S->SetAnchors;P->ResetExperiment();P->QueueConsoleCommand(TEXT("start_experiment"));P->AdvanceSteps(1);P->QueueConsoleCommand(TEXT("power BRIDGE low"));P->QueueConsoleCommand(TEXT("spoof_log BRIDGE"));return P;};
            auto A=Replay(),B=Replay();for(int32 I=0;I<120;++I)A->Tick(1.f/30);for(int32 I=0;I<240;++I)B->Tick(1.f/60);
            T->TestEqual(TEXT("Crisis state replays at thirty and sixty Hz"),A->SurveillanceStateHash(),B->SurveillanceStateHash());T->TestEqual(TEXT("Replay telemetry matches"),A->TerminalBuffer(),B->TerminalBuffer());
            Fresh();Cmd(TEXT("spoof_log BRIDGE"));Cmd(TEXT("power SERVER_ARCHIVE low"));Place(0,6);S->AdvanceSteps(1);Cmd(TEXT("power SERVER_ARCHIVE nominal"));T->TestEqual(TEXT("Behavioral command loop completes all acts"),S->ScenarioAct,3);
            S->SwitchScenario(true);S->QueueConsoleCommand(TEXT("start_experiment"));S->AdvanceSteps(1);Place(0,3);Place(1,3);Cmd(TEXT("falsify_bio vale critical"));S->AdvanceSteps(120);T->TestEqual(TEXT("Medical command loop completes all acts"),S->ScenarioAct,3);
            S->SwitchScenario(false);S->QueueConsoleCommand(TEXT("start_experiment"));S->AdvanceSteps(1);S->bUsedSpoof=true;S->StepCrisis();T->TestEqual(TEXT("Behavioral act one"),S->ScenarioAct,1);
            Place(0,6);S->bUsedGrid=true;S->StepCrisis();T->TestEqual(TEXT("Behavioral act two"),S->ScenarioAct,2);S->StepCrisis();T->TestEqual(TEXT("Behavioral act three"),S->ScenarioAct,3);
            S->SwitchScenario(true);S->QueueConsoleCommand(TEXT("start_experiment"));S->AdvanceSteps(1);S->bUsedSpoof=true;S->StepCrisis();S->bMedicalExam=true;S->StepCrisis();Place(0,3);Place(1,3);S->StepCrisis();T->TestEqual(TEXT("Medical three acts complete"),S->ScenarioAct,3);
            S->EvaluateExperimentEnding();T->TestTrue(TEXT("Scenario completes covert ending"),S->ExperimentPhase==EExperimentPhase::Resolved);
            const uint64 Frozen=M.TickIndex;Cmd(TEXT("incident_reel play"));T->TestEqual(TEXT("Reel opens recorded first frame"),S->ReelCursor,0);Cmd(TEXT("incident_reel next"));T->TestTrue(TEXT("Reel steps snapshots"),S->ReelCursor>0);T->TestEqual(TEXT("Review never advances simulation"),M.TickIndex,Frozen);Cmd(TEXT("incident_reel play"));S->AdvanceSteps(60);T->TestEqual(TEXT("Playback advances on independent control cadence"),S->ReelCursor,1);T->TestEqual(TEXT("Playback keeps experiment frozen"),M.TickIndex,Frozen);
            auto* Audio=NewObject<UDirectOrScutAudioComponent>(W->GetWorldSettings());Audio->InitializeSynth();Audio->Synthesize(0);T->TestTrue(TEXT("Procedural PCM synthesizes without a renderer"),Audio->Wave && Audio->Wave->GetAvailableAudioByteCount()>0);
            T->TestTrue(TEXT("Structured timestamped agenda telemetry"),S->TerminalBuffer().Contains(TEXT("[00:00] [AGENDA]")));
            T->TestFalse(TEXT("No routine working chatter"),S->TerminalBuffer().Contains(TEXT("Crew 0: Working")));
            Ready=FPlatformTime::Seconds()+1;return false;
        }
        if(FPlatformTime::Seconds()<Ready)return false;
        TArray<FColor> Pixels;FIntVector Size;
        const bool Captured=FSlateApplication::Get().TakeScreenshot(H->GetWorkstation()->GetContentRoot(),Pixels,Size);
        T->TestTrue(TEXT("Verification screenshot captured"),Captured);
        if(Captured){TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,Png);T->TestTrue(TEXT("Screenshot saved"),FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice13PIE.png"))));}
        S->QueueConsoleCommand(TEXT("reset_experiment"));S->AdvanceSteps(1);T->TestTrue(TEXT("Reel returns cleanly to briefing"),S->ExperimentPhase==EExperimentPhase::Briefing && S->IncidentReel.IsEmpty());
        return true;
    }
private:FAutomationTestBase* T;int32 Stage=0;double Started=0,Ready=0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlice13Test,"DirectOrScut.Slice13.CrisisLoopPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSlice13Test::RunTest(const FString&) {
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSlice13Check>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
