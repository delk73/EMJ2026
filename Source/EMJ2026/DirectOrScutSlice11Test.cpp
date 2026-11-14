#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutHUD.h"
#include "DirectOrScutGeometry.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"

class FSlice11Check : public IAutomationLatentCommand
{
public:
    explicit FSlice11Check(FAutomationTestBase* T):Test(T){}
    static TSharedPtr<SWidget> Find(const TSharedRef<SWidget>& W,FName Tag) {
        if(W->GetTag()==Tag)return W;
        auto* C=W->GetChildren();for(int32 I=0;I<C->Num();++I)if(auto R=Find(C->GetChildAt(I),Tag))return R;
        return nullptr;
    }
    bool Update() override {
        const double Now=FPlatformTime::Seconds();if(!Started)Started=Now;
        if(Now-Started>90){Test->AddError(TEXT("Slice 11 PIE timeout"));return true;}
        auto* W=GEditor->PlayWorld.Get();if(!W || !W->HasBegunPlay())return false;
        auto* S=W->GetSubsystem<UDirectOrScutSimSubsystem>();auto* PC=W->GetFirstPlayerController();
        auto* H=PC?Cast<ADirectOrScutHUD>(PC->GetHUD()):nullptr;
        if(!S || !S->Simulation.bReady || !H || !H->GetWorkstation())return false;
        auto UI=H->GetWorkstation();if(Now-Last<.5 || UI->GetCachedGeometry().GetLocalSize().X<100)return false;
        Last=Now;auto& M=S->Simulation;S->bAutomaticAdvance=false;
        auto Outcome=[&](EExperimentEnding E){Test->TestTrue(TEXT("Expected terminal classification"),S->ExperimentEnding==E);Test->TestTrue(TEXT("Resolved lifecycle"),S->ExperimentPhase==EExperimentPhase::Resolved);const auto Hash=M.StateHash();const auto Cues=S->CinematicCues.Num();S->AdvanceSteps(100);Test->TestEqual(TEXT("Resolved simulation frozen"),M.StateHash(),Hash);Test->TestEqual(TEXT("Resolved cue log frozen"),S->CinematicCues.Num(),Cues);};
        auto Click=[&](FName Tag){auto B=Find(UI->GetContentRoot(),Tag);Test->TestTrue(TEXT("Dossier control exists"),B.IsValid());if(!B)return;auto& App=FSlateApplication::Get();const auto& G=B->GetCachedGeometry();const FVector2D Pos=G.LocalToAbsolute(G.GetLocalSize()*.5);auto Win=App.FindWidgetWindow(B.ToSharedRef());if(!Win){Test->AddError(TEXT("Dossier window missing"));return;}App.ProcessMouseMoveEvent(FPointerEvent(0,Pos,Pos-FVector2D(1,0),TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()));App.ProcessMouseButtonDownEvent(Win->GetNativeWindow(),FPointerEvent(0,Pos,Pos,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));App.ProcessMouseButtonUpEvent(FPointerEvent(0,Pos,Pos,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));};
        auto ResetAndStart=[&](){S->ResetExperiment();S->QueueConsoleCommand(TEXT("start_experiment"));S->AdvanceSteps(1);};
        switch(Stage++) {
        case 0: {
            ResetAndStart();for(int I=0;I<3;++I)M.Bodies[I].Profile.Suspicion=.75f;M.ObjectiveProgress=1;S->AdvanceSteps(1);Test->TestTrue(TEXT("Exactly 75 percent suspicion excludes covert success"),S->ExperimentPhase==EExperimentPhase::Active);
            ResetAndStart();M.ObjectiveProgress=1;S->AdvanceSteps(1);Outcome(EExperimentEnding::COVERT_SUCCESS);
            ResetAndStart();for(int I=0;I<3;++I)M.Bodies[I].Profile.Suspicion=.86f;
            S->AdvanceSteps(89);Test->TestTrue(TEXT("Sustained exposure waits 90 ticks"),S->ExperimentPhase==EExperimentPhase::Active);
            M.Bodies[0].Profile.Suspicion=0;S->AdvanceSteps(1);Test->TestEqual(TEXT("Exposure streak resets"),S->ExposureTicks,uint64(0));
            for(int I=0;I<3;++I)M.Bodies[I].Profile.Suspicion=.86f;S->AdvanceSteps(90);Outcome(EExperimentEnding::MUTINY_EXPOSED);
            ResetAndStart();M.Bodies[0].Profile.Suspicion=.8f;M.Bodies[0].Stress=60;S->EvaluateBeatDeck();Test->TestTrue(TEXT("Bridge exposure beat resolves"),S->ResolveBeat(1,1));S->AdvanceSteps(1);Outcome(EExperimentEnding::MUTINY_EXPOSED);
            ResetAndStart();for(int I=0;I<3;++I)M.Bodies[I].Integrity=0;S->AdvanceSteps(1);Outcome(EExperimentEnding::CREW_COLLAPSE);
            ResetAndStart();M.Bodies[0].Room=M.Bodies[0].AssignedRoom=7;M.Bodies[0].Position=DirectOrScut::RoomWaypoint(7);M.Bodies[0].Profile.Suspicion=.8f;Test->TestTrue(TEXT("Intentional SCUT accepted"),S->BeginScut(7));S->AdvanceSteps(180);Outcome(EExperimentEnding::SCUT_CONTAINMENT);
            ResetAndStart();M.Bodies[0].Room=7;M.Bodies[0].Profile.Suspicion=.8f;S->BeginScut(7);M.Rooms[2].bHullBreach=true;M.Bodies[0].Integrity=0;S->AdvanceSteps(1);Test->TestTrue(TEXT("Damaged bridge prevents containment"),S->ExperimentEnding!=EExperimentEnding::SCUT_CONTAINMENT);
            ResetAndStart();M.ObjectiveProgress=1;M.TickIndex=17999;S->AdvanceSteps(1);Outcome(EExperimentEnding::EXPERIMENT_INCOMPLETE);Test->TestEqual(TEXT("Deadline exactly 18000 ticks"),M.TickIndex,uint64(18000));
            ResetAndStart();S->AdvanceSteps(18001);Outcome(EExperimentEnding::EXPERIMENT_INCOMPLETE);Test->TestEqual(TEXT("Full experiment stops at 600 seconds"),M.TickIndex,uint64(18000));
            S->QueueConsoleCommand(TEXT("reset_experiment"));S->AdvanceSteps(1);Test->TestEqual(TEXT("Reset clock zero"),M.TickIndex,uint64(0));Test->TestTrue(TEXT("Reset clears cues and ending"),S->CinematicCues.IsEmpty() && S->ExperimentEnding==EExperimentEnding::None);
            for(int I=0;I<3;++I)Test->TestTrue(TEXT("Reset restores subjects and biometrics"),M.Bodies[I].Integrity==100 && M.Bodies[I].Profile.HeartRateBPM==60 && M.Bodies[I].Profile.PupilDilationMM==2.5f);
            for(const auto& R:M.Rooms)Test->TestTrue(TEXT("Reset restores room alerts and pressure"),R.bIsPressurized && !R.bHullBreach && !R.bLockdown && R.AlertSeconds==0);
            S->QueueConsoleCommand(TEXT("scenario medical"));S->AdvanceSteps(1);Test->TestTrue(TEXT("Medical scenario clean start"),S->bMedicalScenario && M.TickIndex==0 && S->CinematicCues.IsEmpty());
            S->QueueConsoleCommand(TEXT("start_experiment"));S->AdvanceSteps(1);S->RecordCue(3,.5,TEXT("Medical telemetry review complete"));M.ObjectiveProgress=1;S->AdvanceSteps(1);Outcome(EExperimentEnding::COVERT_SUCCESS);break;
        }
        case 1: {
            auto D=Find(UI->GetContentRoot(),TEXT("ScutEvaluationDossier"));Test->TestTrue(TEXT("Resolved dossier visible"),D.IsValid() && D->GetVisibility()==EVisibility::Visible);
            TArray<FColor> Pixels;FIntVector Size;const bool Captured=FSlateApplication::Get().TakeScreenshot(UI->GetContentRoot(),Pixels,Size);Test->TestTrue(TEXT("Dossier screenshot captured"),Captured);
            if(Captured){TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,Png);Test->TestTrue(TEXT("Screenshot saved"),FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice11PIE.png"))));}
            Click(TEXT("ScutNewExperiment"));S->AdvanceSteps(1);Test->TestTrue(TEXT("New experiment click resets same scenario into Briefing"),S->ExperimentPhase==EExperimentPhase::Briefing && S->bMedicalScenario && M.TickIndex==0);break;
        }
        case 2:Test->TestTrue(TEXT("Dossier hidden after reset"),Find(UI->GetContentRoot(),TEXT("ScutEvaluationDossier"))->GetVisibility()==EVisibility::Collapsed);S->QueueConsoleCommand(TEXT("start_experiment"));S->AdvanceSteps(1);M.ObjectiveProgress=1;S->AdvanceSteps(1);break;
        case 3:Click(TEXT("ScutSwitchScenario"));S->AdvanceSteps(1);Test->TestTrue(TEXT("Switch click returns behavioral Briefing"),!S->bMedicalScenario && S->ExperimentPhase==EExperimentPhase::Briefing && M.TickIndex==0 && S->CinematicCues.IsEmpty());return true;
        }
        return false;
    }
private:FAutomationTestBase* Test;double Started=0,Last=0;int32 Stage=0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlice11Test,"DirectOrScut.Slice11.EndingsPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSlice11Test::RunTest(const FString&) {
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSlice11Check>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
