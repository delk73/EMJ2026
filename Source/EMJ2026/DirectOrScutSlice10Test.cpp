#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutHullActor.h"
#include "DirectOrScutGameMode.h"
#include "DirectOrScutGeometry.h"
#include "RoomSetBase.h"
#include "CompartmentCameraRig.h"
#include "DirectOrScutHUD.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "UDynamicMesh.h"

class FSoundstageEditorCheck : public IAutomationLatentCommand
{
public:
    explicit FSoundstageEditorCheck(FAutomationTestBase* T):Test(T) {}
    bool Update() override
    {
        auto* W=GEditor->GetEditorWorldContext().World();
        Test->TestTrue(TEXT("Saved map game mode"),W->GetWorldSettings()->DefaultGameMode==ADirectOrScutGameMode::StaticClass());
        int32 Sets=0,Rigs=0;
        for(TActorIterator<ARoomSetBase> It(W);It;++It)
        {
            ++Sets;Test->TestTrue(TEXT("Room identity survives reconstruction"),It->GetActorLocation().Equals(UDirectOrScutSimSubsystem::DefaultSetTransform(It->RoomId).WorldOrigin,1));Test->TestTrue(TEXT("Procedural geometry visible before Play"),It->SetMesh->GetDynamicMesh()->GetTriangleCount()>0);
            const int32 Components=It->GetInstanceComponents().Num();It->OnConstruction(It->GetActorTransform());
            Test->TestEqual(TEXT("Construction is idempotent"),It->GetInstanceComponents().Num(),Components);
        }
        for(TActorIterator<ACompartmentCameraRig> It(W);It;++It)++Rigs;
        Test->TestEqual(TEXT("Eight editor sets"),Sets,8);Test->TestEqual(TEXT("One editor rig"),Rigs,1);return true;
    }
private: FAutomationTestBase* Test;
};
class FSoundstagePIECheck : public IAutomationLatentCommand
{
public:
    explicit FSoundstagePIECheck(FAutomationTestBase* T):Test(T) {}
    bool Update() override
    {
        if(!Started)Started=FPlatformTime::Seconds();
        if(FPlatformTime::Seconds()-Started>100) {Test->AddError(TEXT("Soundstage PIE timed out"));return true;}
        auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W || !W->HasBegunPlay())return false;
        auto* S=W->GetSubsystem<UDirectOrScutSimSubsystem>();if(!S || !S->Simulation.bReady || (Stage==0 && S->Simulation.TickIndex<30))return false;
        auto* PC=Cast<ADirectOrScutPlayerController>(W->GetFirstPlayerController());
        auto* H=PC?Cast<ADirectOrScutHUD>(PC->GetHUD()):nullptr;if(!H || !H->GetWorkstation())return false;
        if(Stage==0)
        {
            S->bAutomaticAdvance=false;S->ResetSimulation();auto& M=S->Simulation;
            int32 Sets=0,Rigs=0;for(TActorIterator<ARoomSetBase> It(W);It;++It)++Sets;
            Test->TestEqual(TEXT("PIE did not duplicate saved sets"),Sets,8);
            for(TActorIterator<ACompartmentCameraRig> It(W);It;++It)
            {
                ++Rigs;for(int32 F=1;F<=8;++F)
                {
                    Test->TestTrue(TEXT("All eight feeds captured"),It->CaptureCounts[F]>0);
                    Test->TestNull(TEXT("Feed uses direct texture, no shader"),It->FeedMaterials[F].Get());
                    Test->TestEqual(TEXT("No post-process blend"),It->Captures[F]->PostProcessBlendWeight,0.f);
                    Test->TestTrue(TEXT("24mm FOV"),FMath::IsNearlyEqual(It->Captures[F]->FOVAngle,73.7398f));
                }
            }
            Test->TestEqual(TEXT("PIE reuses saved rig"),Rigs,1);
            Test->TestEqual(TEXT("Eight registry entries"),S->CompartmentSets.Num(),8);
            for(int32 A=0;A<8;++A)for(int32 B=A+1;B<8;++B)
                Test->TestTrue(TEXT("Stages separated by at least 3500 uu"),FVector::Distance(S->CompartmentSets[static_cast<ERoomId>(A)].WorldOrigin,S->CompartmentSets[static_cast<ERoomId>(B)].WorldOrigin)>=3500);
            for(int32 D=0;D<8;++D)S->SetDoorOpen(D,true);
            Test->TestEqual(TEXT("BFS preserves bridge to med bay adjacency"),M.FirstDoorOnPath(2,3),0);
            S->SetDoorOpen(8,true);for(const auto& R:M.Rooms)Test->TestFalse(TEXT("Open graph propagates vacuum"),R.bIsPressurized);
            S->SetDoorOpen(0,false);Test->TestTrue(TEXT("Sealed bridge isolates vacuum"),M.Rooms[2].bIsPressurized);Test->TestFalse(TEXT("Connected med bay remains vacuum"),M.Rooms[3].bIsPressurized);
            S->ResetSimulation();
            M.Bodies[0].Stress=20;M.Bodies[0].Profile.Suspicion=.2f;M.UpdateBiometrics();const float LitPupil=M.Bodies[0].Profile.PupilDilationMM;
            M.Rooms[M.Bodies[0].Room].LightLevel=0;M.UpdateBiometrics();Test->TestTrue(TEXT("Pupil responds to room light"),M.Bodies[0].Profile.PupilDilationMM>LitPupil);
            S->ResetSimulation();
            M.Bodies[0].Stress=100;M.Bodies[0].Profile.Suspicion=.8f;S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Human triad bounded"),M.Bodies[0].Profile.HeartRateBPM>=60 && M.Bodies[0].Profile.HeartRateBPM<=160 && M.Bodies[0].Profile.RespirationRateRPM>=12 && M.Bodies[0].Profile.RespirationRateRPM<=34 && M.Bodies[0].Profile.PupilDilationMM>=2.5f && M.Bodies[0].Profile.PupilDilationMM<=8);
            Test->TestTrue(TEXT("Whisper and audit gates fired"),S->BeatDeck[0].bFired && S->BeatDeck[1].bFired);
            const auto Cues=S->CinematicCues.Num();S->AdvanceSteps(1);Test->TestEqual(TEXT("Beat markers one shot"),S->CinematicCues.Num(),Cues);
            Test->TestTrue(TEXT("Interaction resolves"),S->ResolveBeat(0,0));Test->TestFalse(TEXT("Resolved interaction cannot repeat"),S->ResolveBeat(0,0));
            M.Bodies[3].TaskProgress=4;S->AdvanceSteps(1);Test->TestTrue(TEXT("Synthetic anomaly gate"),S->BeatDeck[2].bFired);
            int32 Heartbeats=0,Breaths=0;
            const auto HeartHandle=S->OnHeartbeatTick.AddLambda([&](int32,uint64){++Heartbeats;});
            const auto BreathHandle=S->OnBreathCycle.AddLambda([&](int32,uint64){++Breaths;});
            S->AdvanceSteps(180);S->OnHeartbeatTick.Remove(HeartHandle);S->OnBreathCycle.Remove(BreathHandle);
            Test->TestTrue(TEXT("Discrete audio binding delegates"),Heartbeats>0 && Breaths>0);Test->TestTrue(TEXT("Cardiac crossings accumulate"),M.Bodies[0].Profile.HeartbeatCount>0);
            Test->TestTrue(TEXT("Respiratory crossings accumulate"),M.Bodies[0].Profile.BreathCount>0);
            Test->TestEqual(TEXT("Android diagnostic pulse"),M.Bodies[3].Profile.HeartRateBPM,60.f);Test->TestEqual(TEXT("Android has no respiration"),M.Bodies[3].Profile.RespirationRateRPM,0.f);
            S->QueueConsoleCommand(TEXT("scenario medical"));S->AdvanceSteps(1);Test->TestTrue(TEXT("Medical selected at fixed tick"),S->bMedicalScenario);
            M.Bodies[0].Integrity=50;M.Bodies[0].Stress=65;M.Bodies[0].Profile.Suspicion=.5f;M.Rooms[M.Bodies[0].Room].AlertSeconds=20;S->AdvanceSteps(1);
            for(const auto& B:S->BeatDeck)Test->TestTrue(TEXT("Medical condition gate"),B.bFired);
            S->ResetSimulation();Test->TestTrue(TEXT("Reset clears marker log"),S->CinematicCues.IsEmpty());
            S->QueueConsoleCommand(TEXT("scenario behavioral"));S->AdvanceSteps(1);S->QueueConsoleCommand(TEXT("menu events"));S->AdvanceSteps(1);
            Test->TestEqual(TEXT("Behavioral operations"),S->ScreenMenuChoices().Num(),8);
            Test->TestTrue(TEXT("SCUT requires confirmation"),S->ExecuteDirectorOption(7));Test->TestFalse(TEXT("No vacuum before confirmation"),M.Doors[8].bIsOpen);
            S->QueueConsoleCommand(TEXT("choose 1"));S->AdvanceSteps(1);Test->TestTrue(TEXT("Confirmed vacuum authoritative"),M.Doors[8].bIsOpen && !M.Rooms[7].bIsPressurized);
            Test->TestTrue(TEXT("Command transcript records ticks"),!S->CommandRecords.IsEmpty());Test->TestTrue(TEXT("Cinematic pressure marker logged"),!S->CinematicCues.IsEmpty());
            S->ResetSimulation();S->ExecuteDirectorOption(2);
            Test->TestEqual(TEXT("Mentorship gathers first human"),M.Bodies[0].AssignedRoom,5);
            Test->TestEqual(TEXT("Mentorship gathers second human"),M.Bodies[1].AssignedRoom,5);
            S->ResetSimulation();
            S->QueueConsoleCommand(TEXT("scenario behavioral"));S->AdvanceSteps(1);S->QueueConsoleCommand(TEXT("menu events"));S->AdvanceSteps(1);S->QueueConsoleCommand(TEXT("choose 5"));S->AdvanceSteps(150);
            const auto Records=S->CommandRecords;const uint64 Expected=M.StateHash();const uint64 End=M.TickIndex;
            S->ResetSimulation();for(uint64 T=0;T<End;++T) {for(const auto& R:Records)if(R.Tick==T) {if(R.bConsole)S->QueueConsoleCommand(R.Input);else S->SubmitDirective(R.Input);}S->AdvanceSteps(1);}
            Test->TestEqual(TEXT("Timestamped input replay reproduces state"),M.StateHash(),Expected);
            auto A=MakeUnique<FDirectOrScutSimulation>(M),B=MakeUnique<FDirectOrScutSimulation>(M);
            for(int32 I=0;I<150;++I)A->Advance(1./30);for(int32 I=0;I<300;++I)B->Advance(1./60);
            Test->TestEqual(TEXT("30/60 Hz partition including biometrics"),A->StateHash(),B->StateHash());
            S->ResetSimulation();H->GetWorkstation()->ShowGrid();
            Last=FPlatformTime::Seconds();++Stage;return false;
        }
        if(FPlatformTime::Seconds()-Last<2)return false;
        if(Stage==1)
        {
            TArray<FColor> Pixels;FIntVector Size;
            const bool Captured=FSlateApplication::Get().TakeScreenshot(H->GetWorkstation()->GetContentRoot(),Pixels,Size);
            Test->TestTrue(TEXT("Workstation verification screenshot captured"),Captured);
            if(Captured)
            {
                TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,Png);
                Test->TestTrue(TEXT("Screenshot saved"),FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice10PIE.png"))));
            }
            S->QueueConsoleCommand(TEXT("view 3d"));S->AdvanceSteps(1);++Stage;Last=FPlatformTime::Seconds();return false;}
        Test->TestFalse(TEXT("F10 scene state"),S->bWorkstationVisible);Test->TestTrue(TEXT("Free fly pawn active"),PC->GetPawn() && PC->GetViewTarget()==PC->GetPawn());
        S->QueueConsoleCommand(TEXT("view crt"));S->AdvanceSteps(1);return true;
    }
private: FAutomationTestBase* Test;double Started=0,Last=0;int32 Stage=0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlice10Test,"DirectOrScut.Slice10.SoundstagePIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSlice10Test::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSoundstageEditorCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSoundstagePIECheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
