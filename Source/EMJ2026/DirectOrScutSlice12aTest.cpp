#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutSetAnchor.h"
#include "DirectOrScutHUD.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "RoomSetBase.h"
#include "CompartmentCameraRig.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/DynamicMeshComponent.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
class FSlice12aCheck : public IAutomationLatentCommand {
public:
    explicit FSlice12aCheck(FAutomationTestBase* In):T(In){}
    bool Update() override {
        if(!Started)Started=FPlatformTime::Seconds();
        if(FPlatformTime::Seconds()-Started>160){T->AddError(TEXT("Slice12a PIE timeout"));return true;}
        auto* W=GEditor->PlayWorld.Get();if(!W || !W->HasBegunPlay())return false;
        auto* S=W->GetSubsystem<UDirectOrScutSimSubsystem>();if(!S || !S->Simulation.bReady)return false;
        auto* PC=W->GetFirstPlayerController();auto* H=PC?Cast<ADirectOrScutHUD>(PC->GetHUD()):nullptr;
        if(!H || !H->GetWorkstation() || H->GetWorkstation()->GetCachedGeometry().GetLocalSize().X<100)return false;
        if(FPlatformTime::Seconds()<Ready)return false;
        S->bAutomaticAdvance=false;auto& M=S->Simulation;
        auto UI=H->GetWorkstation();
        auto Boot=[&](){
            for(int32 D=0;D<9;++D)T->TestEqual(TEXT("Interior open, exterior closed"),M.Doors[D].bIsOpen,M.Doors[D].RoomB>=0);
            for(int32 R=0;R<8;++R){T->TestFalse(TEXT("Boot room not sealed"),S->IsRoomSealed(R));T->TestTrue(TEXT("Boot room pressurized"),M.Rooms[R].bIsPressurized);
                T->TestTrue(TEXT("Room barrier registered and hidden"),S->RoomSetActors[R].IsValid() && !S->RoomSetActors[R]->ContainmentBarrier->IsVisible());}
            for(int32 I=0;I<4;++I){const auto& C=S->CrewStaging[I];
                T->TestEqual(TEXT("Boot visual room equals badge room"),C.Room,M.Bodies[I].Room);
                T->TestTrue(TEXT("Boot has anchor in current room"),S->SetAnchors.IsValidIndex(C.Anchor) && int32(S->SetAnchors[C.Anchor]->RoomId)==C.Room);
                if(S->SetAnchors.IsValidIndex(C.Anchor))T->TestTrue(TEXT("Boot body exactly on anchor"),S->GetBodyActor(I)->GetActorLocation().Equals(S->SetAnchors[C.Anchor]->GetActorLocation(),.01));
            }
        };
        auto Visible=[&](int32 I){
            ACompartmentCameraRig* Rig=nullptr;for(TActorIterator<ACompartmentCameraRig> It(W);It;++It){Rig=*It;break;}
            if(!Rig){T->AddError(TEXT("Camera rig missing"));return;}
            const int32 R=M.Bodies[I].Room;const auto* C=Rig->Captures[ACompartmentCameraRig::FeedForRoom(R)].Get();
            const FVector P=C->GetComponentTransform().InverseTransformPosition(S->GetBodyActor(I)->GetActorLocation()+FVector(0,0,90));
            const float Tan=FMath::Tan(FMath::DegreesToRadians(C->FOVAngle*.5f));
            T->TestTrue(TEXT("Crew center inside current room camera frustum"),P.X>0 && FMath::Abs(P.Y)<P.X*Tan && FMath::Abs(P.Z)<P.X*Tan*9.f/16);
            const auto Stage=S->CompartmentSets.FindChecked(ERoomId(R));const FVector Local=FRotator(0,-Stage.Yaw,0).RotateVector(S->GetBodyActor(I)->GetActorLocation()-Stage.WorldOrigin);
            T->TestTrue(TEXT("Crew inside current soundstage bounds"),FMath::Abs(Local.X)<(R==1?3000:1100) && FMath::Abs(Local.Y)<900);
        };
        auto Capture=[&](const TCHAR* Name){TArray<FColor> Pixels;FIntVector Size;const bool OK=FSlateApplication::Get().TakeScreenshot(UI->GetContentRoot(),Pixels,Size);T->TestTrue(TEXT("Screenshot captured"),OK);if(OK){TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,Png);T->TestTrue(TEXT("Screenshot saved"),FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/")+Name)));}};
        switch(Stage++){
        case 0:
            Boot();for(int32 I=0;I<4;++I)Visible(I);S->QueueConsoleCommand(TEXT("start_experiment"));S->AdvanceSteps(1);UI->ShowGrid();Ready=FPlatformTime::Seconds()+1;break;
        case 1:
            Capture(TEXT("Slice12aBootPIE.png"));
            T->TestTrue(TEXT("No boot protest spam"),!S->TerminalBuffer().Contains(TEXT("protests sealed bulkhead")));
            S->QueueConsoleCommand(TEXT("direct chen BRIDGE diagnostic_telemetry"));S->AdvanceSteps(1);
            T->TestEqual(TEXT("Player route accepted"),M.Bodies[1].AssignedRoom,2);
            for(int32 N=0;N<2400 && (M.Bodies[1].Room!=2 || M.Bodies[1].State!=EDirectOrScutUnitState::Working);++N){
                S->AdvanceSteps(1);
                T->TestEqual(TEXT("Visual and badge room synchronized each transit tick"),S->CrewStaging[1].Room,M.Bodies[1].Room);
                T->TestTrue(TEXT("Body sync each fixed step"),S->GetBodyActor(1)->GetActorLocation().Equals(S->CrewStaging[1].Position,.01));
                T->TestEqual(TEXT("Agenda does not replace player route"),M.Bodies[1].AssignedRoom,2);
            }
            T->TestTrue(TEXT("Chen arrives at BRIDGE through real station graph"),M.Bodies[1].Room==2 && M.Bodies[1].State==EDirectOrScutUnitState::Working);
            T->TestTrue(TEXT("Chen no longer listed in MED_BAY"),M.Bodies[1].Room!=3);
            Visible(1);Ready=FPlatformTime::Seconds()+1;break;
        case 2:
            Capture(TEXT("Slice12aRoutePIE.png"));S->QueueConsoleCommand(TEXT("seal BRIDGE"));S->AdvanceSteps(1);
            T->TestTrue(TEXT("Explicit seal closes bridge graph"),S->IsRoomSealed(2));
            T->TestTrue(TEXT("Seal activates visible barrier"),S->RoomSetActors[2]->ContainmentBarrier->IsVisible());
            S->QueueConsoleCommand(TEXT("direct chen MED_BAY diagnostic_telemetry"));S->AdvanceSteps(1);
            T->TestTrue(TEXT("Sealed active route blocked"),S->CrewStaging[1].Posture==ECrewStagingPosture::Blocked);
            {const uint64 Start=M.TickIndex;uint64 Last=0;int32 Count=0;S->AdvanceSteps(950);
                for(const auto& E:S->Events)if(E.Text.Contains(TEXT("Subject 1 protests sealed bulkhead"))){++Count;const uint64 Tick=FCString::Strtoui64(*E.Text.Mid(1),nullptr,10);if(Last)T->TestTrue(TEXT("Protests separated by 30 seconds"),Tick-Last>=900);Last=Tick;}
                T->TestTrue(TEXT("At most two protests per 31 seconds"),Count<=2 && Count>=1);
                T->TestEqual(TEXT("Blocked crew remains in BRIDGE"),M.Bodies[1].Room,2);(void)Start;}
            Visible(1);Ready=FPlatformTime::Seconds()+1;break;
        case 3:
            Capture(TEXT("Slice12aPIE.png"));S->QueueConsoleCommand(TEXT("open BRIDGE"));S->AdvanceSteps(1);
            T->TestFalse(TEXT("Unseal hides barrier"),S->RoomSetActors[2]->ContainmentBarrier->IsVisible());
            S->ResetExperiment();Boot();
            S->QueueConsoleCommand(TEXT("scenario medical"));S->AdvanceSteps(1);Boot();
            S->QueueConsoleCommand(TEXT("scenario behavioral"));S->AdvanceSteps(1);Boot();
            S->QueueConsoleCommand(TEXT("start_experiment"));S->AdvanceSteps(1);
            S->SecretAgendas[2].bActivated=true;
            S->QueueConsoleCommand(TEXT("direct ortiz BRIDGE diagnostic_telemetry"));S->AdvanceSteps(1);
            for(int32 N=0;N<2400 && (M.Bodies[2].Room!=2 || M.Bodies[2].State!=EDirectOrScutUnitState::Working);++N){S->AdvanceSteps(1);T->TestEqual(TEXT("Active audit agenda defers to player route"),M.Bodies[2].AssignedRoom,2);}
            T->TestTrue(TEXT("Player-directed Ortiz arrives before agenda resumes"),M.Bodies[2].Room==2 && M.Bodies[2].State==EDirectOrScutUnitState::Working);
            S->AdvanceSteps(151);
            T->TestEqual(TEXT("Deferred audit resumes after arrival dwell"),M.Bodies[2].AssignedRoom,6);
            S->ResetExperiment();Boot();return true;
        }
        return false;
    }
private:FAutomationTestBase* T;int32 Stage=0;double Ready=0,Started=0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlice12aTest,"DirectOrScut.Slice12a.StationRoutesPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSlice12aTest::RunTest(const FString&){
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSlice12aCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif

