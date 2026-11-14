#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "UObject/StrongObjectPtr.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutHUD.h"
#include "DirectOrScutGeometry.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"

class FSlice11cCheck : public IAutomationLatentCommand
{
public:
    explicit FSlice11cCheck(FAutomationTestBase* T):Test(T){}
    static TSharedPtr<SWidget> Find(const TSharedRef<SWidget>& W,FName Tag) {
        if(W->GetTag()==Tag)return W;
        auto* C=W->GetChildren();for(int32 I=0;I<C->Num();++I)if(auto R=Find(C->GetChildAt(I),Tag))return R;
        return nullptr;
    }
    bool Update() override {
        const double Now=FPlatformTime::Seconds();if(!Started)Started=Now;
        if(Now-Started>100){Test->AddError(TEXT("Slice11c PIE timeout"));return true;}
        auto* W=GEditor->PlayWorld.Get();if(!W || !W->HasBegunPlay())return false;
        auto* S=W->GetSubsystem<UDirectOrScutSimSubsystem>();auto* PC=W->GetFirstPlayerController();auto* H=PC?Cast<ADirectOrScutHUD>(PC->GetHUD()):nullptr;
        if(!S || !S->Simulation.bReady || !H || !H->GetWorkstation())return false;
        auto UI=H->GetWorkstation();if(UI->GetCachedGeometry().GetLocalSize().X<100 || Now-Last<.6)return false;Last=Now;
        auto& App=FSlateApplication::Get();auto Root=UI->GetContentRoot();auto& M=S->Simulation;
        auto Key=[&](FKey K){App.ProcessKeyDownEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0));App.ProcessKeyUpEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0));};
        auto Click=[&](FName Tag){auto B=Find(Root,Tag);Test->TestTrue(TEXT("Clickable surveillance target exists"),B.IsValid());if(!B)return;const auto& G=B->GetCachedGeometry();const FVector2D P=G.LocalToAbsolute(G.GetLocalSize()*.5);auto Win=App.FindWidgetWindow(B.ToSharedRef());if(!Win){Test->AddError(TEXT("Surveillance target window missing"));return;}App.ProcessMouseMoveEvent(FPointerEvent(0,P,P-FVector2D(1,0),TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()));App.ProcessMouseButtonDownEvent(Win->GetNativeWindow(),FPointerEvent(0,P,P,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));App.ProcessMouseButtonUpEvent(FPointerEvent(0,P,P,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));};
        auto Command=[&](const TCHAR* C){auto P=StaticCastSharedPtr<SEditableTextBox>(Find(UI->GetContentRoot(),TEXT("ScutPrompt")));App.SetKeyboardFocus(P);P->SetText(FText::FromString(C));Key(EKeys::Enter);};
        auto Capture=[&](const TCHAR* Name){TArray<FColor> Pixels;FIntVector Size;const bool OK=App.TakeScreenshot(UI->GetContentRoot(),Pixels,Size);Test->TestTrue(TEXT("Surveillance screenshot captured"),OK);if(OK){TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,Png);Test->TestTrue(TEXT("Screenshot saved"),FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/")+Name)));}};
        UE_LOG(LogTemp,Display,TEXT("Slice11c stage %d"),Stage);
        switch(Stage++) {
        case 0: {
            S->bAutomaticAdvance=false;Test->TestTrue(TEXT("Cold start is Briefing"),S->ExperimentPhase==EExperimentPhase::Briefing);Test->TestEqual(TEXT("Cold clock zero"),M.TickIndex,uint64(0));
            const uint64 Hash=S->SurveillanceStateHash();S->AdvanceSteps(900);S->Tick(1);Test->TestEqual(TEXT("Briefing freezes full simulation and surveillance state"),S->SurveillanceStateHash(),Hash);
            S->QueueConsoleCommand(TEXT("seal all"));S->AdvanceSteps(1);Test->TestEqual(TEXT("Briefing refuses state-changing directives"),S->SurveillanceStateHash(),Hash);
            auto Card=Find(Root,TEXT("ScutBriefingCard"));Test->TestTrue(TEXT("Briefing card visible"),Card.IsValid() && Card->GetVisibility()==EVisibility::Visible);Test->TestTrue(TEXT("Covert objective names target and room"),S->BriefingObjective().Contains(TEXT("VALE")) && S->BriefingObjective().Contains(TEXT("AIRLOCK_CHAMBER")));
            break;
        }
        case 1:
            Test->TestTrue(TEXT("Briefing metadata has settled layout"),Find(Root,TEXT("ScutBriefingScenario"))->GetCachedGeometry().GetLocalSize().Y>5 && Find(Root,TEXT("ScutBriefingCover"))->GetCachedGeometry().GetLocalSize().Y>5 && Find(Root,TEXT("ScutBriefingObjective"))->GetCachedGeometry().GetLocalSize().Y>5);
            Capture(TEXT("Slice11cBriefingPIE.png"));Click(TEXT("ScutInitializeExperiment"));Test->TestTrue(TEXT("Initialize click waits for 30Hz control tick"),S->ExperimentPhase==EExperimentPhase::Briefing);S->AdvanceSteps(1);Test->TestTrue(TEXT("Initialize enters Active"),S->ExperimentPhase==EExperimentPhase::Active);Test->TestEqual(TEXT("Initialization preserves zero clock"),M.TickIndex,uint64(0));UI->SelectFeed(2,false);UI->ExpandSelected();Test->TestFalse(TEXT("Inspection intent deferred"),S->IsCameraTallyActive(2));M.Bodies[0].Profile.Suspicion=.4f;S->AdvanceSteps(1);Test->TestTrue(TEXT("Expanded room has authoritative live tally"),S->IsCameraTallyActive(2));break;
        case 2:Test->TestTrue(TEXT("Briefing card hidden after initialization"),Find(Root,TEXT("ScutBriefingCard"))->GetVisibility()==EVisibility::Collapsed);UI->ShowGrid();S->AdvanceSteps(1);Test->TestFalse(TEXT("Passive grid removes inspection tally"),S->IsCameraTallyActive(2));break;
        case 3: {
            Click(TEXT("ScutFollowCrew0Feed2"));S->AdvanceSteps(1);Test->TestTrue(TEXT("Crew follow activates tally in grid"),UI->bGrid && S->IsCameraTallyActive(2));
            auto Label=StaticCastSharedPtr<STextBlock>(Find(Root,TEXT("ScutFeedLabel2")));Test->TestTrue(TEXT("Live tally badge displayed"),Label->GetText().ToString().Contains(TEXT("REC // TALLY LIVE")));
            auto Fresh=[&](){TStrongObjectPtr<UDirectOrScutSimSubsystem> P(NewObject<UDirectOrScutSimSubsystem>(W));P->Simulation=M;P->ResetExperiment();P->QueueConsoleCommand(TEXT("start_experiment"));P->AdvanceSteps(1);return P;};
            auto A=Fresh(),B=Fresh();A->Simulation.Bodies[0].Profile.Suspicion=.35f;B->Simulation.Bodies[0].Profile.Suspicion=.35f;A->QueueCameraInspection(2);
            A->AdvanceSteps(150);B->AdvanceSteps(150);Test->TestEqual(TEXT("No tally penalty through first 150 ticks"),A->Simulation.Bodies[0].Profile.Suspicion,B->Simulation.Bodies[0].Profile.Suspicion);
            A->AdvanceSteps(150);B->AdvanceSteps(150);Test->TestTrue(TEXT("Five seconds of post-grace tracking adds 2.5 percentage points"),FMath::IsNearlyEqual(A->Simulation.Bodies[0].Profile.Suspicion-B->Simulation.Bodies[0].Profile.Suspicion,.025f,.0001f));
            A->Simulation.Bodies[0].Profile.Suspicion=.34f;B->Simulation.Bodies[0].Profile.Suspicion=.34f;A->AdvanceSteps(180);B->AdvanceSteps(180);Test->TestEqual(TEXT("Below 35 percent receives no penalty"),A->Simulation.Bodies[0].Profile.Suspicion,B->Simulation.Bodies[0].Profile.Suspicion);
            A->QueueCameraInspection(-1);A->Simulation.Bodies[0].Profile.Suspicion=.4f;B->Simulation.Bodies[0].Profile.Suspicion=.4f;A->AdvanceSteps(300);B->AdvanceSteps(300);Test->TestEqual(TEXT("Passive grid receives no surveillance penalty"),A->Simulation.Bodies[0].Profile.Suspicion,B->Simulation.Bodies[0].Profile.Suspicion);
            A->QueueCameraInspection(2);A->AdvanceSteps(150);B->AdvanceSteps(150);Test->TestEqual(TEXT("Returning to inspection restarts tally grace"),A->Simulation.Bodies[0].Profile.Suspicion,B->Simulation.Bodies[0].Profile.Suspicion);
            auto Low=Fresh();Low->Simulation.Bodies[0].Profile.Suspicion=.699f;Low->AdvanceSteps(600);Test->TestTrue(TEXT("Below 70 percent never tampers"),Low->CameraFeedState(2)==ECameraFeedState::Online);
            auto Dead=Fresh();Dead->Simulation.Bodies[0].Integrity=0;Dead->Simulation.Bodies[0].Profile.Suspicion=.9f;Dead->AdvanceSteps(600);Test->TestTrue(TEXT("Incapacitated subjects cannot tamper"),Dead->CameraFeedState(2)==ECameraFeedState::Online);
            auto Android=Fresh();Android->Simulation.Bodies[3].Profile.Suspicion=.9f;Android->QueueCameraInspection(5);Android->AdvanceSteps(600);Test->TestTrue(TEXT("Android never accumulates tally suspicion or tampers"),Android->Simulation.Bodies[3].Profile.Suspicion==0 && Android->CameraFeedState(5)==ECameraFeedState::Online);
            auto F=Fresh();F->Simulation.Bodies[0].Profile.Suspicion=.4f;F->QueueConsoleCommand(TEXT("follow vale"));F->AdvanceSteps(1);Test->TestTrue(TEXT("Follow tally computed without UI tick"),F->IsCameraTallyActive(2));F->Simulation.Bodies[0].Room=F->Simulation.Bodies[0].AssignedRoom=3;F->Simulation.Bodies[0].Position=DirectOrScut::RoomWaypoint(3);F->AdvanceSteps(1);Test->TestTrue(TEXT("Follow tally moves across compartments authoritatively"),F->IsCameraTallyActive(3) && !F->IsCameraTallyActive(2));
            auto C=Fresh();C->Simulation.Bodies[0].Profile.Suspicion=.7f;C->AdvanceSteps(149);Test->TestTrue(TEXT("Sabotage waits 150 ticks"),C->CameraFeedState(2)==ECameraFeedState::Online);C->AdvanceSteps(1);Test->TestTrue(TEXT("70 percent subject loops local feed"),C->CameraFeedState(2)==ECameraFeedState::Looped);C->AdvanceSteps(150);Test->TestTrue(TEXT("Sustained sabotage escalates Offline"),C->CameraFeedState(2)==ECameraFeedState::Offline);
            C->QueueConsoleCommand(TEXT("repair_cam BRIDGE"));Test->TestTrue(TEXT("Repair deferred"),C->CameraFeedState(2)==ECameraFeedState::Offline);C->AdvanceSteps(1);Test->TestTrue(TEXT("Diagnostic repair restores Online"),C->CameraFeedState(2)==ECameraFeedState::Online);C->AdvanceSteps(299);Test->TestTrue(TEXT("Repair protection lasts 300 ticks"),C->CameraFeedState(2)==ECameraFeedState::Online);C->AdvanceSteps(149);Test->TestTrue(TEXT("Saboteur must build a new dwell after repair grace"),C->CameraFeedState(2)==ECameraFeedState::Online);C->AdvanceSteps(1);Test->TestTrue(TEXT("Sabotage can resume after repair protection"),C->CameraFeedState(2)==ECameraFeedState::Looped);
            auto Native=Fresh(),Replay=Fresh();Native->Simulation.Bodies[0].Profile.Suspicion=.4f;Replay->Simulation.Bodies[0].Profile.Suspicion=.4f;Native->QueueCameraInspection(2);Native->AdvanceSteps(300);for(const auto& Record:Native->CommandRecords)if(Record.Input.StartsWith(TEXT("inspect_cam ")))Replay->QueueConsoleCommand(Record.Input);Replay->AdvanceSteps(300);Test->TestEqual(TEXT("Recorded camera intent replays deterministic surveillance state"),Native->SurveillanceStateHash(),Replay->SurveillanceStateHash());
            auto D=Fresh(),E=Fresh();D->Simulation.Bodies[0].Profile.Suspicion=.4f;E->Simulation.Bodies[0].Profile.Suspicion=.4f;D->Simulation.Bodies[1].Profile.Suspicion=.7f;E->Simulation.Bodies[1].Profile.Suspicion=.7f;D->QueueCameraInspection(2);E->QueueCameraInspection(2);for(int32 I=0;I<600;++I)D->Tick(1.f/30);for(int32 I=0;I<1200;++I)E->Tick(1.f/60);Test->TestEqual(TEXT("30/60Hz frame partitions reproduce simulation/tally/tamper hash"),D->SurveillanceStateHash(),E->SurveillanceStateHash());
            S->QueueConsoleCommand(TEXT("follow off"));S->AdvanceSteps(1);M.Bodies[0].Profile.Suspicion=.7f;S->AdvanceSteps(150);Test->TestTrue(TEXT("PIE bridge feed looped"),S->CameraFeedState(2)==ECameraFeedState::Looped);break;
        }
        case 4: {
            Test->TestTrue(TEXT("Looped feed replaced by interruption slate"),Find(Root,TEXT("ScutFeedInterrupted2"))->GetVisibility()==EVisibility::Visible && Find(Root,TEXT("ScutFeedImage2"))->GetVisibility()==EVisibility::Hidden);Capture(TEXT("Slice11cLoopedPIE.png"));S->AdvanceSteps(150);Test->TestTrue(TEXT("PIE feed offline"),S->CameraFeedState(2)==ECameraFeedState::Offline);Command(TEXT("repair_cam unknown"));S->AdvanceSteps(1);Test->TestTrue(TEXT("Invalid repair cannot affect feed"),S->CameraFeedState(2)==ECameraFeedState::Offline);Command(TEXT("repair_cam BRIDGE"));Test->TestTrue(TEXT("CLI repair waits"),S->CameraFeedState(2)==ECameraFeedState::Offline);S->AdvanceSteps(1);Test->TestTrue(TEXT("CLI repair online"),S->CameraFeedState(2)==ECameraFeedState::Online);M.Bodies[1].Profile.Suspicion=.7f;S->AdvanceSteps(150);Test->TestTrue(TEXT("Second room sabotaged independently"),S->CameraFeedState(3)==ECameraFeedState::Looped);Click(TEXT("ScutCategory0"));S->AdvanceSteps(1);break;
        }
        case 5:Test->TestTrue(TEXT("Repaired video is restored"),Find(Root,TEXT("ScutFeedImage2"))->GetVisibility()==EVisibility::Visible);Click(TEXT("ScutMenuChoice6"));S->AdvanceSteps(1);Test->TestTrue(TEXT("Repair submenu reachable through TUI"),S->ActiveMenu==EShipAIScreenMenu::CameraRepair);break;
        case 6:Click(TEXT("ScutMenuChoice3"));S->AdvanceSteps(1);Test->TestTrue(TEXT("TUI diagnostic restores MED_BAY"),S->CameraFeedState(3)==ECameraFeedState::Online);// Apply sealing before setting the dwell suspicion: sealing itself adds 16% suspicion.
            S->QueueConsoleCommand(TEXT("seal OBSERVATION_DECK"));S->AdvanceSteps(2);
            // Surveillance dwell fixture explicitly places its subjects; open graph travel is tested in Slice12a.
            M.RegisterBody(0,FVector2D(DirectOrScut::CrewSpawnPoint(0)),2,false);
            M.RegisterBody(2,FVector2D(DirectOrScut::CrewSpawnPoint(2)),4,false);
            M.Bodies[0].Profile.Suspicion=.4f;M.Bodies[2].Profile.Suspicion=.7f;S->QueueConsoleCommand(TEXT("follow vale"));S->AdvanceSteps(301);UI->SetConsoleOpen(false);UI->ShowGrid();break;
        case 7: {
            Test->TestTrue(TEXT("Observation dwell produces offline slate"),S->CameraFeedState(4)==ECameraFeedState::Offline);Test->TestTrue(TEXT("Bridge follow tally remains live"),S->IsCameraTallyActive(2));Capture(TEXT("Slice11cPIE.png"));Command(TEXT("reset_experiment"));S->AdvanceSteps(1);Test->TestTrue(TEXT("Reset returns Briefing"),S->ExperimentPhase==EExperimentPhase::Briefing);Test->TestEqual(TEXT("Reset clock zero"),M.TickIndex,uint64(0));for(int32 R=0;R<8;++R)Test->TestTrue(TEXT("Reset clears feed sabotage and tallies"),S->CameraFeedState(R)==ECameraFeedState::Online && !S->IsCameraTallyActive(R));const auto Hash=S->SurveillanceStateHash();S->AdvanceSteps(300);Test->TestEqual(TEXT("Reset briefing stays frozen"),S->SurveillanceStateHash(),Hash);
            Command(TEXT("scenario medical"));S->AdvanceSteps(1);Test->TestTrue(TEXT("Scenario switch stays in Briefing"),S->bMedicalScenario && S->ExperimentPhase==EExperimentPhase::Briefing);break;
        }
        case 8:Capture(TEXT("Slice11cMedicalBriefingPIE.png"));Command(TEXT("start_experiment"));Test->TestTrue(TEXT("CLI init does not bypass queue"),S->ExperimentPhase==EExperimentPhase::Briefing);S->AdvanceSteps(1);Test->TestTrue(TEXT("CLI initializes medical run at zero"),S->ExperimentPhase==EExperimentPhase::Active && M.TickIndex==0);M.Bodies[1].Profile.Suspicion=.4f;M.Bodies[1].Stress=40;S->AdvanceSteps(1);Test->TestTrue(TEXT("Symptom concealment scenario beat loops a feed"),S->CameraFeedState(3)==ECameraFeedState::Looped);M.ObjectiveProgress=1;S->AdvanceSteps(1);Test->TestTrue(TEXT("Active still transitions to Resolved"),S->ExperimentPhase==EExperimentPhase::Resolved);{const auto Hash=S->SurveillanceStateHash();S->AdvanceSteps(300);Test->TestEqual(TEXT("Resolved state freezes surveillance and simulation"),S->SurveillanceStateHash(),Hash);}S->QueueConsoleCommand(TEXT("reset_experiment"));S->AdvanceSteps(1);break;
        case 9:UI->SetConsoleOpen(false);App.SetKeyboardFocus(UI);Key(EKeys::Enter);Test->TestTrue(TEXT("Enter initialization deferred"),S->ExperimentPhase==EExperimentPhase::Briefing);S->AdvanceSteps(1);Test->TestTrue(TEXT("Enter accepts briefing"),S->ExperimentPhase==EExperimentPhase::Active && M.TickIndex==0);return true;
        }
        return false;
    }
private:FAutomationTestBase* Test;double Started=0,Last=0;int32 Stage=0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlice11cTest,"DirectOrScut.Slice11c.BriefingSurveillancePIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSlice11cTest::RunTest(const FString&) {
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSlice11cCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
