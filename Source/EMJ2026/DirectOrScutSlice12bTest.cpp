#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "UObject/StrongObjectPtr.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutSetAnchor.h"
#include "DirectOrScutHUD.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
class FSlice12bCheck : public IAutomationLatentCommand {
public:
    explicit FSlice12bCheck(FAutomationTestBase* In):T(In){}
    static TSharedPtr<SWidget> Find(const TSharedRef<SWidget>& W,FName Tag){
        if(W->GetTag()==Tag)return W;auto* Children=W->GetChildren();
        for(int32 I=0;I<Children->Num();++I)if(auto R=Find(Children->GetChildAt(I),Tag))return R;return nullptr;
    }
    void RunRoute(UDirectOrScutSimSubsystem* S,int32 Crew,int32 Goal){
        auto& C=S->CrewStaging[Crew];auto& U=S->Simulation.Bodies[Crew];
        const auto Hops=C.ActiveRoute;const uint64 Serial=C.RouteSerial,Commits=C.HopCommits;
        int32 Changes=0,PreviousRoom=U.Room;
        for(int32 N=0;N<1800 && S->IsCrewRouteLocked(Crew);++N){
            const FVector Before=C.Position;const int32 Anchor=C.Anchor;const auto Phase=C.RoutePhase;
            S->AdvanceSteps(1);
            T->TestEqual(TEXT("Route is never restarted in transit"),C.RouteSerial,Serial);
            T->TestEqual(TEXT("Actor and badge room match"),C.Room,U.Room);
            if(U.Room!=PreviousRoom){
                if(Hops.IsValidIndex(Changes))T->TestEqual(TEXT("Exactly the next committed BFS hop arrives"),U.Room,int32(Hops[Changes]));
                ++Changes;PreviousRoom=U.Room;
            } else {
                T->TestTrue(TEXT("Single movement integration capped at six uu"),FVector::Dist(Before,C.Position)<=6.001);
                if(Phase==C.RoutePhase)T->TestEqual(TEXT("No anchor shuffle within a route phase"),C.Anchor,Anchor);
            }
            T->TestTrue(TEXT("Actor follows authoritative staging"),S->GetBodyActor(Crew)->GetActorLocation().Equals(C.Position,.01));
        }
        T->TestFalse(TEXT("Route finishes without deadlock"),S->IsCrewRouteLocked(Crew));
        T->TestEqual(TEXT("Destination reached"),U.Room,Goal);
        T->TestEqual(TEXT("Hop committed exactly once"),C.HopCommits-Commits,uint64(Hops.Num()));
        T->TestEqual(TEXT("No extra badge transitions"),Changes,Hops.Num());
        T->TestTrue(TEXT("Queue empty at arrival"),C.ActiveRoute.IsEmpty());
        T->TestTrue(TEXT("Arrival leaves MovingToRoom"),U.State==EDirectOrScutUnitState::Working || U.State==EDirectOrScutUnitState::Panic);
        T->TestTrue(TEXT("Arrival locks exact anchor coordinate"),S->SetAnchors.IsValidIndex(C.Anchor) && C.Position.Equals(S->SetAnchors[C.Anchor]->GetActorLocation(),.01));
    }
    bool Update() override {
        if(!Started)Started=FPlatformTime::Seconds();
        if(FPlatformTime::Seconds()-Started>150){T->AddError(TEXT("Slice12b PIE timeout"));return true;}
        auto* W=GEditor->PlayWorld.Get();if(!W || !W->HasBegunPlay())return false;
        auto* S=W->GetSubsystem<UDirectOrScutSimSubsystem>();if(!S || !S->Simulation.bReady)return false;
        auto* PC=W->GetFirstPlayerController();auto* H=PC?Cast<ADirectOrScutHUD>(PC->GetHUD()):nullptr;
        if(!H || !H->GetWorkstation() || H->GetWorkstation()->GetCachedGeometry().GetLocalSize().X<100)return false;
        if(FPlatformTime::Seconds()<Ready)return false;
        S->bAutomaticAdvance=false;auto UI=H->GetWorkstation();auto& M=S->Simulation;auto& C=S->CrewStaging[0];auto& U=M.Bodies[0];
        auto Capture=[&](const TCHAR* Name){TArray<FColor> Pixels;FIntVector Size;bool OK=FSlateApplication::Get().TakeScreenshot(UI->GetContentRoot(),Pixels,Size);T->TestTrue(TEXT("Screenshot captured"),OK);if(OK){TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,Png);T->TestTrue(TEXT("Screenshot saved"),FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/")+Name)));}};
        auto Click=[&](FName Tag){auto B=Find(UI->GetContentRoot(),Tag);T->TestTrue(TEXT("TUI route choice exists"),B.IsValid());if(!B)return;auto& App=FSlateApplication::Get();const auto& G=B->GetCachedGeometry();const FVector2D P=G.LocalToAbsolute(G.GetLocalSize()*.5);auto Win=App.FindWidgetWindow(B.ToSharedRef());if(!Win){T->AddError(TEXT("TUI window missing"));return;}
            App.ProcessMouseMoveEvent(FPointerEvent(0,P,P-FVector2D(1,0),TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()));
            App.ProcessMouseButtonDownEvent(Win->GetNativeWindow(),FPointerEvent(0,P,P,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
            App.ProcessMouseButtonUpEvent(FPointerEvent(0,P,P,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));S->AdvanceSteps(1);};
        switch(Stage++){
        case 0:{
            S->ResetExperiment();S->QueueConsoleCommand(TEXT("start_experiment"));S->AdvanceSteps(1);
            S->QueueConsoleCommand(TEXT("direct vale MED_BAY diagnostic_telemetry"));S->AdvanceSteps(2);RunRoute(S,0,3);
            U.Profile.Compliance=.1f;U.Stress=60;S->SecretAgendas[0].bActivated=true;
            S->SubmitDirective(TEXT("direct vale BRIDGE diagnostic_telemetry"));S->AdvanceSteps(1);
            T->TestTrue(TEXT("Low compliance immediately commits hop queue"),C.ActiveRoute.Num()>0 && U.AssignedRoom==2);
            T->TestTrue(TEXT("Resistance applies stats rather than hesitation"),U.Profile.Suspicion>.02f && U.Stress>=60);
            const auto Hops=C.ActiveRoute;const uint64 Serial=C.RouteSerial;
            T->TestFalse(TEXT("Autonomous destination cannot replace route"),S->StartCrewRoute(0,6));
            T->TestFalse(TEXT("Legacy idle/psychology assignment cannot replace route"),M.AssignRoom(0,6));
            S->SubmitDirective(TEXT("direct vale BRIDGE diagnostic_telemetry"));S->AdvanceSteps(1);
            T->TestTrue(TEXT("Repeated command preserves queue and serial"),C.ActiveRoute==Hops && C.RouteSerial==Serial);
            RunRoute(S,0,2);S->AdvanceSteps(3);T->TestTrue(TEXT("Vale finishes and remains Working"),U.State==EDirectOrScutUnitState::Working);
            T->TestFalse(TEXT("No hesitation deadlock messages"),S->TerminalBuffer().Contains(TEXT("hesitated / needs physical trigger")));
            UI->ShowGrid();Ready=FPlatformTime::Seconds()+1;break;
        }
        case 1:Capture(TEXT("Slice12bCLI.png"));U.Stress=10;S->QueueConsoleCommand(TEXT("menu ai"));S->AdvanceSteps(1);Ready=FPlatformTime::Seconds()+.5;break;
        case 2:Click(TEXT("ScutMenuChoice1"));Ready=FPlatformTime::Seconds()+.5;break;
        case 3:Click(TEXT("ScutMenuChoice1"));Ready=FPlatformTime::Seconds()+.5;break;
        case 4:
            Click(TEXT("ScutMenuChoice3"));T->TestTrue(TEXT("Actual TUI pretext commits Vale route"),S->IsCrewRouteLocked(0) && U.AssignedRoom==3);RunRoute(S,0,3);
            Ready=FPlatformTime::Seconds()+1;break;
        case 5:{
            Capture(TEXT("Slice12bPIE.png"));
            S->SubmitDirective(TEXT("direct vale BRIDGE diagnostic_telemetry"));S->AdvanceSteps(1);
            const uint64 Serial=C.RouteSerial;
            int32 Door=-1;for(int32 D=0;D<9;++D)if(M.Doors[D].RoomA==2 || M.Doors[D].RoomB==2){Door=D;break;}
            M.SetDoorOpen(Door,false);S->AdvanceSteps(250);
            T->TestEqual(TEXT("Seal never replans committed route"),C.RouteSerial,Serial);
            T->TestTrue(TEXT("Sealed pending hop blocks without consuming it"),C.bRouteBlocked && !C.ActiveRoute.IsEmpty() && C.ActiveRoute.Last()==ERoomId::Bridge && U.Room!=2);
            M.SetDoorOpen(Door,true);RunRoute(S,0,2);
            U.Stress=0;U.State=EDirectOrScutUnitState::MovingToRoom;C.Position+=FVector(1,0,0);S->AdvanceSteps(1);
            T->TestTrue(TEXT("Empty queue watchdog clears orphan move and locks anchor"),U.State==EDirectOrScutUnitState::Working && C.Position.Equals(S->SetAnchors[C.Anchor]->GetActorLocation(),.01));
            U.Stress=80;S->SubmitDirective(TEXT("direct vale MED_BAY diagnostic_telemetry"));S->AdvanceSteps(1);RunRoute(S,0,3);
            T->TestTrue(TEXT("High-stress route commits then settles Panic"),U.State==EDirectOrScutUnitState::Panic);
            auto Fresh=[&](){TStrongObjectPtr<UDirectOrScutSimSubsystem> P(NewObject<UDirectOrScutSimSubsystem>(W));P->Simulation=M;P->ResetExperiment();P->QueueConsoleCommand(TEXT("start_experiment"));P->AdvanceSteps(1);P->SubmitDirective(TEXT("direct vale MED_BAY diagnostic_telemetry"));return P;};
            auto A=Fresh(),B=Fresh();for(int32 N=0;N<300;++N)A->Tick(1.f/30);for(int32 N=0;N<600;++N)B->Tick(1.f/60);
            T->TestEqual(TEXT("Route queue replays under 30/60 Hz frame partitions"),A->SurveillanceStateHash(),B->SurveillanceStateHash());
            return true;
        }
        }
        return false;
    }
private:FAutomationTestBase* T;int32 Stage=0;double Ready=0,Started=0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlice12bTest,"DirectOrScut.Slice12b.SingleCommitRoutesPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSlice12bTest::RunTest(const FString&){
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSlice12bCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
