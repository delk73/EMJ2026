#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutHUD.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "CompartmentCameraRig.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"
class FSlice9PIE : public IAutomationLatentCommand
{
public:
    FAutomationTestBase* Test; double Started=FPlatformTime::Seconds();
    explicit FSlice9PIE(FAutomationTestBase* T):Test(T) {}
    bool Update() override
    {
        if (FPlatformTime::Seconds()-Started>90) { Test->AddError(TEXT("Slice 9 PIE startup timed out")); return true; }
        UWorld* W=GEditor?GEditor->PlayWorld.Get():nullptr; if (!W || !W->HasBegunPlay()) return false;
        auto* S=W->GetSubsystem<UDirectOrScutSimSubsystem>(); if (!S || !S->Simulation.bReady || S->Simulation.TickIndex<60) return false;
        auto* HUD=Cast<ADirectOrScutHUD>(W->GetFirstPlayerController()->GetHUD()); if (!HUD || !HUD->GetWorkstation()) return false;
        auto UI=HUD->GetWorkstation(); ACompartmentCameraRig* Rig=nullptr; for (TActorIterator<ACompartmentCameraRig> It(W);It;++It) { Rig=*It; break; }
        Test->TestNotNull(TEXT("Camera rig in PIE"),Rig); if (!Rig) return true;
        S->bAutomaticAdvance=false; S->ResetSimulation(); auto& M=S->Simulation;
        Test->TestEqual(TEXT("Nine feeds"),Rig->Captures.Num(),9);
        for (int32 F=1;F<9;++F) { Test->TestEqual(TEXT("Level 145uu camera"),Rig->Captures[F]->GetComponentLocation().Z,145.); Test->TestNotNull(TEXT("Procedural feed shader"),Rig->FeedMaterials[F].Get()); }
        auto Key=[&](FKey K) { UI->OnPreviewKeyDown(FGeometry(),FKeyEvent(K,FModifierKeysState(),0,false,0,0)); };
        const FKey Keys[]={EKeys::Zero,EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight};
        for (int32 F=1;F<9;++F) { Key(Keys[F]); Test->TestEqual(TEXT("Feed hotkey"),UI->ActiveFeed,F); }
        Key(EKeys::Eight); Test->TestTrue(TEXT("Double tap fullscreen"),UI->bFullscreen); Key(EKeys::Escape); Test->TestFalse(TEXT("Escape restores split"),UI->bFullscreen);
        Key(EKeys::G); Test->TestFalse(TEXT("G expands selected feed"),UI->bGrid); Key(EKeys::Tab); Test->TestTrue(TEXT("Tab restores grid"),UI->bGrid); Key(EKeys::F); Test->TestTrue(TEXT("F fullscreen"),UI->bFullscreen); Key(EKeys::Escape);
        Rig->SetVisibleFeeds(3,7,false); Rig->Tick(.1f); Test->TestEqual(TEXT("Primary 1024 wide"),Rig->Targets[3]->SizeX,1024); Test->TestEqual(TEXT("PiP 512 wide"),Rig->Targets[7]->SizeX,512);
        Rig->SetVisibleFeeds(0,7,true); Rig->Tick(.1f); for (int32 F=1;F<=8;++F) Test->TestEqual(TEXT("Multiplexer 512 wide"),Rig->Targets[F]->SizeX,512);
        S->SubmitDirective(TEXT("llm")); Test->TestTrue(TEXT("AI link entered"),S->bAILink); Test->TestTrue(TEXT("Offline response"),S->SubmitDirective(TEXT("Please explain the ship")).Contains(TEXT("Offline"))); S->SubmitDirective(TEXT("cmd")); Test->TestFalse(TEXT("CMD restored"),S->bAILink);
        S->SubmitDirective(TEXT("direct vale airlock")); Test->TestEqual(TEXT("Commands wait for fixed tick"),M.Bodies[0].AssignedRoom,2); S->AdvanceSteps(1); Test->TestEqual(TEXT("Low compliance hesitates without trigger"),M.Bodies[0].AssignedRoom,2);
        M.Bodies[0].Room=1; S->SubmitDirective(TEXT("alert airlock fault")); S->AdvanceSteps(1); Test->TestEqual(TEXT("Physical alert lures low compliance"),M.Bodies[0].AssignedRoom,7);
        M.Bodies[0].Room=2; S->SetDoorOpen(0,true); const float Before=M.Bodies[0].Profile.Suspicion; S->SubmitDirective(TEXT("seal bridge")); S->AdvanceSteps(1); Test->TestTrue(TEXT("Unexplained seal suspicion"),M.Bodies[0].Profile.Suspicion>Before);
        M.Bodies[0].Room=7; M.Bodies[0].Position=M.Bodies[0].InitialPosition; const float PreVent=M.Bodies[0].Profile.Suspicion; S->SubmitDirective(TEXT("scut airlock")); S->AdvanceSteps(1); Test->TestTrue(TEXT("SCUT suspicion"),M.Bodies[0].Profile.Suspicion>PreVent); Test->TestFalse(TEXT("Airlock vents"),M.Rooms[7].bIsPressurized);
        Test->TestEqual(TEXT("Android suspicion immune"),M.Bodies[3].Profile.Suspicion,0.f);
        S->ResetSimulation(); M.Bodies[0].Room=7; M.Bodies[0].Position=FVector2D(0,-1700); S->AdvanceSteps(90); Test->TestTrue(TEXT("Hidden isolation objective advances"),M.ObjectiveProgress>0);
        const auto Snapshot=S->MakeContextSnapshot(); Test->TestTrue(TEXT("Prompt snapshot includes psychology and objective"),Snapshot.CompactJson.Contains(TEXT("composure")) && Snapshot.CompactJson.Contains(TEXT("objective")));
        S->ResetSimulation(); auto A=MakeUnique<FDirectOrScutSimulation>(M),B=MakeUnique<FDirectOrScutSimulation>(M); for (int32 I=0;I<300;++I) A->Advance(1./30); for (int32 I=0;I<600;++I) B->Advance(1./60); Test->TestEqual(TEXT("30/60Hz frame partition replay with psychology"),A->StateHash(),B->StateHash());
        S->ResetSimulation();
        M.Bodies[0].Room=2; M.Bodies[1].Room=2; M.Bodies[0].Profile.Suspicion=.9f; M.Bodies[0].Profile.Affinity=1;
        const float CoworkerBefore=M.Bodies[1].Profile.Suspicion; S->AdvanceSteps(30);
        Test->TestTrue(TEXT("Suspicious affinity spreads to shared-room coworkers"),M.Bodies[1].Profile.Suspicion>CoworkerBefore);
        Test->TestTrue(TEXT("Rogue attempts a physical bulkhead override"),M.Doors[0].bIsOpen);
        S->ResetSimulation(); M.Bodies[0].Profile.Composure=0; M.Bodies[1].Profile.Composure=1; M.Bodies[0].Room=2; M.Bodies[1].Room=3; M.Rooms[2].AlertSeconds=M.Rooms[3].AlertSeconds=30;
        S->AdvanceSteps(30); Test->TestTrue(TEXT("Low composure builds alarm stress faster"),M.Bodies[0].Stress>M.Bodies[1].Stress);
        M.Bodies[0].State=EDirectOrScutUnitState::Ejected; M.Bodies[0].Room=-1; S->AdvanceSteps(30); Test->TestFalse(TEXT("Ejected roster member cannot return"),M.AssignTask(0,2,true));
        S->ResetSimulation(); S->bAutomaticAdvance=true; return true;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlice9Test,"DirectOrScut.Slice9.WorkstationPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSlice9Test::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/DirectOrScut/Maps/DirectOrScutBlockout")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSlice9PIE>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand()); return true;
}
#endif

