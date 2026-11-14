#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "DirectOrScutGameMode.h"
#include "DirectOrScutGeometry.h"
#include "DoorActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/BoxComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutHUD.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "DirectOrScutSetAnchor.h"
#include "RoomSetBase.h"
#include "Components/PointLightComponent.h"
#include "CompartmentCameraRig.h"
#include "Components/SceneCaptureComponent2D.h"
#include "DirectOrScutAudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScrollBar.h"

// Uses the actual PIE viewport/camera and the same screen-picking function as LMB.
// Each open/close transition runs through real world ticks, including the open sill.
class FScutPIEDoorTraceCommand : public IAutomationLatentCommand
{
  public:
    explicit FScutPIEDoorTraceCommand(FAutomationTestBase *InTest) : Test(InTest) {}
    virtual bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (Started == 0)
        {
            Started = Now;
        }
        if (Now - Started > 90)
        {
            Test->AddError(TEXT("PIE doorway trace timed out"));
            return true;
        }
        UWorld *World = GEditor ? GEditor->PlayWorld : nullptr;
        if (!World || !World->HasBegunPlay())
        {
            return false;
        }
        auto *PC = Cast<ADirectOrScutPlayerController>(World->GetFirstPlayerController());
        if (!PC)
        {
            return false;
        }
        int32 X = 0, Y = 0;
        PC->GetViewportSize(X, Y);
        if (X < 100 || Y < 100)
        {
            return false;
        }
        if (!bCollected)
        {
            int32 Exterior = 0;
            for (TActorIterator<ADoorActor> It(World); It; ++It)
            {
                Doors.Add(*It);
                Exterior += It->bExterior ? 1 : 0;
            }
            Test->TestEqual(TEXT("Nine chokepoints"), Doors.Num(), 9);
            Test->TestEqual(TEXT("One exterior SCUT hatch"), Exterior, 1);
            bCollected = true;
            Last = Now;
            return false;
        }
        if (Index >= Doors.Num())
        {
            UE_LOG(LogTemp, Display,
                   TEXT("SCUT PIE DOOR TRACE COMPLETE: %d doors; closed/open/closed from orthographic viewport"),
                   Index);
            return true;
        }
        auto *Door = Doors[Index].Get();
        if (!Door)
        {
            Test->AddError(TEXT("PIE door disappeared"));
            return true;
        }
        if (Now - Last < 2.0)
        {
            return false;
        }
        FVector2D Pixel;
        Test->TestTrue(TEXT("Doorway projects inside viewport"),
                       PC->ProjectWorldLocationToScreen(Door->GetActorLocation() + FVector(0, 0, 130), Pixel));
        Test->TestTrue(TEXT("Doorway pixel within frame"), Pixel.X >= 0 && Pixel.Y >= 0 && Pixel.X < X && Pixel.Y < Y);
        if (Phase == 0 || Phase == 1)
        {
            const bool Open = Phase == 1;
            Test->TestTrue(TEXT("Vertical slab reached endpoint"),
                           Door->Slab->GetRelativeLocation().Equals(FVector(0, 0, Open ? -250 : 0), .1));
            Test->TestTrue(TEXT("Exact 240x180x260 target"),
                           Door->ClickTarget->GetUnscaledBoxExtent().Equals(FVector(90, 120, 130)));
            FHitResult Hit;
            PC->GetHitResultAtScreenPosition(Pixel, DirectOrScut::DoorClickChannel, true, Hit);
            Test->TestTrue(TEXT("Orthographic mouse trace selects intended door"), Hit.GetActor() == Door);
            if (Open)
            {
                Test->TestTrue(TEXT("Open optical sill is green"),
                               Door->Sill->GetMaterial(4) == Door->Sill->GetMaterial(8));
            }
            Test->TestTrue(TEXT("First screen click dispatches"),
                           PC->ToggleDoorAtScreenPosition(Pixel, Door->bExterior));
            Test->TestEqual(TEXT("Single click toggles once"), Door->bOpen, !Open);

            ++Phase;
        }
        else
        {
            Test->TestFalse(TEXT("Door closed again"), Door->bOpen);
            Test->TestTrue(TEXT("Slab returned to threshold"), Door->Slab->GetRelativeLocation().IsNearlyZero(.1));
            UE_LOG(LogTemp, Display, TEXT("SCUT PIE door %d/%d passed closed/open mouse traces: %s"), Index + 1,
                   Doors.Num(), *Door->GetActorLocation().ToString());
            ++Index;
            Phase = 0;
        }
        Last = Now;
        return false;
    }

  private:
    FAutomationTestBase *Test;
    TArray<TWeakObjectPtr<ADoorActor>> Doors;
    double Started = 0, Last = 0;
    int32 Index = 0, Phase = 0;
    bool bCollected = false;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScutPIEDoorTraceTest, "DirectOrScut.Chunk1.PIEDoorMouseTrace",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FScutPIEDoorTraceTest::RunTest(const FString &Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/DirectOrScut/Maps/DirectOrScutBlockout")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FScutPIEDoorTraceCommand>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

// BriefingSurveillance regression coverage.
class FBriefingSurveillanceCheck : public IAutomationLatentCommand
{
  public:
    explicit FBriefingSurveillanceCheck(FAutomationTestBase *T) : Test(T) {}
    static TSharedPtr<SWidget> Find(const TSharedRef<SWidget> &W, FName Tag)
    {
        if (W->GetTag() == Tag)
        {
            return W;
        }
        auto *C = W->GetChildren();
        for (int32 I = 0; I < C->Num(); ++I)
        {
            if (auto R = Find(C->GetChildAt(I), Tag))
            {
                return R;
            }
        }
        return nullptr;
    }
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (!Started)
        {
            Started = Now;
        }
        if (Now - Started > 100)
        {
            Test->AddError(TEXT("BriefingSurveillance PIE timeout"));
            return true;
        }
        auto *W = GEditor->PlayWorld.Get();
        if (!W || !W->HasBegunPlay())
        {
            return false;
        }
        auto *S = W->GetSubsystem<UDirectOrScutSimSubsystem>();
        auto *PC = W->GetFirstPlayerController();
        auto *H = PC ? Cast<ADirectOrScutHUD>(PC->GetHUD()) : nullptr;
        if (!S || !S->Simulation.bReady || !H || !H->GetWorkstation())
        {
            return false;
        }
        auto UI = H->GetWorkstation();
        UI->SetDebugEightCamGrid(true); // Legacy diagnostic coverage explicitly opts into the debug layout.
        if (Last == 0)
        {
            TArray<FVector2f> Outline;
            UI->BuildCompartmentOutline(-1, FVector2f(640, 480), Outline);
            Test->TestTrue(TEXT("Terminal rejects invalid compartment"), Outline.IsEmpty());
            for (int32 Room = 0; Room < 8; ++Room)
            {
                UI->BuildCompartmentOutline(Room, FVector2f(640, 480), Outline);
                Test->TestTrue(TEXT("Terminal compartment contour bounded and closed"),
                               Outline.Num() >= 4 && Outline.Num() <= 128 && Outline[0] == Outline.Last());
            }
            TArray<TArray<FVector2f>> Arcs;
            UI->BuildReticlePoints(FVector2f(100, 100), 90, 999, 1, Arcs);
            Test->TestEqual(TEXT("Reticle segment cap"), Arcs.Num(), 64);
            for (const auto &Arc : Arcs)
            {
                Test->TestTrue(TEXT("Reticle arc point cap"), Arc.Num() <= 32);
            }
            UI->BuildReticlePoints(FVector2f(100, 100), -1, 4, 0, Arcs);
            Test->TestTrue(TEXT("Degenerate reticle empty"), Arcs.IsEmpty());
            UI->BuildPulseTrace(FVector2f(640, 100), 1, 0, Outline);
            Test->TestEqual(TEXT("ECG fixed sample budget"), Outline.Num(), 256);
            for (const auto &P : Outline)
            {
                Test->TestEqual(TEXT("Zero BPM flatline"), P.Y, 55.f);
            }
            Test->TestTrue(TEXT("Native phosphor lower clamp"),
                           UI->EvaluatePhosphorColor(-1).Equals(UI->EvaluatePhosphorColor(0)));
            Test->TestTrue(TEXT("Native phosphor upper clamp"),
                           UI->EvaluatePhosphorColor(2).Equals(FLinearColor(.65f, 1, .72f, 1)));
        }

        if (UI->GetCachedGeometry().GetLocalSize().X < 100 || Now - Last < .6)
        {
            return false;
        }
        Last = Now;
        auto &App = FSlateApplication::Get();
        auto Root = UI->GetContentRoot();
        auto &M = S->Simulation;
        auto Key = [&](FKey K)
        {
            App.ProcessKeyDownEvent(FKeyEvent(K, FModifierKeysState(), 0, false, 0, 0));
            App.ProcessKeyUpEvent(FKeyEvent(K, FModifierKeysState(), 0, false, 0, 0));
        };
        auto Click = [&](FName Tag)
        {
            auto B = Find(Root, Tag);
            Test->TestTrue(TEXT("Clickable surveillance target exists"), B.IsValid());
            if (!B)
            {
                return;
            }
            const auto &G = B->GetCachedGeometry();
            const FVector2D P = G.LocalToAbsolute(G.GetLocalSize() * .5);
            auto Win = App.FindWidgetWindow(B.ToSharedRef());
            if (!Win)
            {
                Test->AddError(TEXT("Surveillance target window missing"));
                return;
            }
            App.ProcessMouseMoveEvent(
                FPointerEvent(0, P, P - FVector2D(1, 0), TSet<FKey>{}, EKeys::Invalid, 0, FModifierKeysState()));
            App.ProcessMouseButtonDownEvent(Win->GetNativeWindow(),
                                            FPointerEvent(0, P, P, TSet<FKey>{EKeys::LeftMouseButton},
                                                          EKeys::LeftMouseButton, 0, FModifierKeysState()));
            App.ProcessMouseButtonUpEvent(
                FPointerEvent(0, P, P, TSet<FKey>{}, EKeys::LeftMouseButton, 0, FModifierKeysState()));
        };
        auto Command = [&](const TCHAR *C)
        {
            auto P = StaticCastSharedPtr<SEditableTextBox>(Find(UI->GetContentRoot(), TEXT("ScutPrompt")));
            App.SetKeyboardFocus(P);
            P->SetText(FText::FromString(C));
            Key(EKeys::Enter);
        };
        auto Capture = [&](const TCHAR *Name)
        {
            TArray<FColor> Pixels;
            FIntVector Size;
            const bool OK = App.TakeScreenshot(UI->GetContentRoot(), Pixels, Size);
            Test->TestTrue(TEXT("Surveillance screenshot captured"), OK);
            if (OK)
            {
                TArray64<uint8> Png;
                FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
                Test->TestTrue(
                    TEXT("Screenshot saved"),
                    FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / TEXT("DirectOrScut/") + Name)));
            }
        };
        UE_LOG(LogTemp, Display, TEXT("BriefingSurveillance stage %d"), Stage);
        switch (Stage++)
        {
        case 0:
        {
            S->bAutomaticAdvance = false;
            Test->TestTrue(TEXT("Cold start is Briefing"), S->ExperimentPhase == EExperimentPhase::Briefing);
            Test->TestEqual(TEXT("Cold clock zero"), M.TickIndex, uint64(0));
            const uint64 Hash = S->SurveillanceStateHash();
            S->AdvanceSteps(900);
            S->Tick(1);
            Test->TestEqual(TEXT("Briefing freezes full simulation and surveillance state"), S->SurveillanceStateHash(),
                            Hash);
            S->QueueConsoleCommand(TEXT("seal all"));
            S->AdvanceSteps(1);
            Test->TestEqual(TEXT("Briefing refuses state-changing directives"), S->SurveillanceStateHash(), Hash);
            auto Card = Find(Root, TEXT("ScutBriefingCard"));
            Test->TestTrue(TEXT("Briefing card visible"),
                           Card.IsValid() && Card->GetVisibility() == EVisibility::Visible);
            Test->TestTrue(TEXT("Three-act objective names crew and archive destination"),
                           S->BriefingObjective().Contains(TEXT("VALE")) &&
                               S->BriefingObjective().Contains(TEXT("SERVER_ARCHIVE")));
            break;
        }
        case 1:
            Test->TestTrue(TEXT("Briefing metadata has settled layout"),
                           Find(Root, TEXT("ScutBriefingScenario"))->GetCachedGeometry().GetLocalSize().Y > 5 &&
                               Find(Root, TEXT("ScutBriefingCover"))->GetCachedGeometry().GetLocalSize().Y > 5 &&
                               Find(Root, TEXT("ScutBriefingObjective"))->GetCachedGeometry().GetLocalSize().Y > 5);
            Capture(TEXT("BriefingSurveillanceBriefingPIE.png"));
            Click(TEXT("ScutInitializeExperiment"));
            Test->TestTrue(TEXT("Initialize click waits for 30Hz control tick"),
                           S->ExperimentPhase == EExperimentPhase::Briefing);
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Initialize enters Active"), S->ExperimentPhase == EExperimentPhase::Active);
            Test->TestEqual(TEXT("Initialization preserves zero clock"), M.TickIndex, uint64(0));
            UI->SelectFeed(2, false);
            UI->ExpandSelected();
            Test->TestFalse(TEXT("Inspection intent deferred"), S->IsCameraTallyActive(2));
            M.Bodies[0].Profile.Suspicion = .4f;
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Expanded room has authoritative live tally"), S->IsCameraTallyActive(2));
            break;
        case 2:
            Test->TestTrue(TEXT("Briefing card hidden after initialization"),
                           Find(Root, TEXT("ScutBriefingCard"))->GetVisibility() == EVisibility::Collapsed);
            UI->ShowGrid();
            S->AdvanceSteps(1);
            Test->TestFalse(TEXT("Passive grid removes inspection tally"), S->IsCameraTallyActive(2));
            break;
        case 3:
        {
            Click(TEXT("ScutFollowCrew0Feed2"));
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Crew follow activates tally in grid"), UI->bGrid && S->IsCameraTallyActive(2));
            auto Label = StaticCastSharedPtr<STextBlock>(Find(Root, TEXT("ScutFeedLabel2")));
            Test->TestTrue(TEXT("Live tally badge displayed"),
                           Label->GetText().ToString().Contains(TEXT("REC // TALLY LIVE")));
            auto Fresh = [&]()
            {
                TStrongObjectPtr<UDirectOrScutSimSubsystem> P(NewObject<UDirectOrScutSimSubsystem>(W));
                P->Simulation = M;
                P->ResetExperiment();
                P->QueueConsoleCommand(TEXT("start_experiment"));
                P->AdvanceSteps(1);
                return P;
            };
            auto A = Fresh(), B = Fresh();
            A->Simulation.Bodies[0].Profile.Suspicion = .35f;
            B->Simulation.Bodies[0].Profile.Suspicion = .35f;
            A->QueueCameraInspection(2);
            A->AdvanceSteps(150);
            B->AdvanceSteps(150);
            Test->TestEqual(TEXT("No tally penalty through first 150 ticks"), A->Simulation.Bodies[0].Profile.Suspicion,
                            B->Simulation.Bodies[0].Profile.Suspicion);
            A->AdvanceSteps(150);
            B->AdvanceSteps(150);
            Test->TestTrue(TEXT("Five seconds of post-grace tracking adds 2.5 percentage points"),
                           FMath::IsNearlyEqual(A->Simulation.Bodies[0].Profile.Suspicion -
                                                    B->Simulation.Bodies[0].Profile.Suspicion,
                                                .025f, .0001f));
            A->Simulation.Bodies[0].Profile.Suspicion = .34f;
            B->Simulation.Bodies[0].Profile.Suspicion = .34f;
            A->AdvanceSteps(180);
            B->AdvanceSteps(180);
            Test->TestEqual(TEXT("Below 35 percent receives no penalty"), A->Simulation.Bodies[0].Profile.Suspicion,
                            B->Simulation.Bodies[0].Profile.Suspicion);
            A->QueueCameraInspection(-1);
            A->Simulation.Bodies[0].Profile.Suspicion = .4f;
            B->Simulation.Bodies[0].Profile.Suspicion = .4f;
            A->AdvanceSteps(300);
            B->AdvanceSteps(300);
            Test->TestEqual(TEXT("Passive grid receives no surveillance penalty"),
                            A->Simulation.Bodies[0].Profile.Suspicion, B->Simulation.Bodies[0].Profile.Suspicion);
            A->QueueCameraInspection(2);
            A->AdvanceSteps(150);
            B->AdvanceSteps(150);
            Test->TestEqual(TEXT("Returning to inspection restarts tally grace"),
                            A->Simulation.Bodies[0].Profile.Suspicion, B->Simulation.Bodies[0].Profile.Suspicion);
            auto Low = Fresh();
            Low->Simulation.Bodies[0].Profile.Suspicion = .699f;
            Low->AdvanceSteps(600);
            Test->TestTrue(TEXT("Below 70 percent never tampers"), Low->CameraFeedState(2) == ECameraFeedState::Online);
            auto Dead = Fresh();
            Dead->Simulation.Bodies[0].Integrity = 0;
            Dead->Simulation.Bodies[0].Profile.Suspicion = .9f;
            Dead->AdvanceSteps(600);
            Test->TestTrue(TEXT("Incapacitated subjects cannot tamper"),
                           Dead->CameraFeedState(2) == ECameraFeedState::Online);
            auto Android = Fresh();
            Android->Simulation.Bodies[3].Profile.Suspicion = .9f;
            Android->QueueCameraInspection(5);
            Android->AdvanceSteps(600);
            Test->TestTrue(TEXT("Android never accumulates tally suspicion or tampers"),
                           Android->Simulation.Bodies[3].Profile.Suspicion == 0 &&
                               Android->CameraFeedState(5) == ECameraFeedState::Online);
            auto F = Fresh();
            F->Simulation.Bodies[0].Profile.Suspicion = .4f;
            F->QueueConsoleCommand(TEXT("follow vale"));
            F->AdvanceSteps(1);
            Test->TestTrue(TEXT("Follow tally computed without UI tick"), F->IsCameraTallyActive(2));
            F->Simulation.Bodies[0].Room = F->Simulation.Bodies[0].AssignedRoom = 3;
            F->Simulation.Bodies[0].Position = DirectOrScut::RoomWaypoint(3);
            F->AdvanceSteps(1);
            Test->TestTrue(TEXT("Follow tally moves across compartments authoritatively"),
                           F->IsCameraTallyActive(3) && !F->IsCameraTallyActive(2));
            auto C = Fresh();
            C->Simulation.Bodies[0].Profile.Suspicion = .7f;
            C->AdvanceSteps(149);
            Test->TestTrue(TEXT("Sabotage waits 150 ticks"), C->CameraFeedState(2) == ECameraFeedState::Online);
            C->AdvanceSteps(1);
            Test->TestTrue(TEXT("70 percent subject loops local feed"),
                           C->CameraFeedState(2) == ECameraFeedState::Looped);
            C->AdvanceSteps(150);
            Test->TestTrue(TEXT("Sustained sabotage escalates Offline"),
                           C->CameraFeedState(2) == ECameraFeedState::Offline);
            C->QueueConsoleCommand(TEXT("repair_cam BRIDGE"));
            Test->TestTrue(TEXT("Repair deferred"), C->CameraFeedState(2) == ECameraFeedState::Offline);
            C->AdvanceSteps(1);
            Test->TestTrue(TEXT("Diagnostic repair restores Online"),
                           C->CameraFeedState(2) == ECameraFeedState::Online);
            C->AdvanceSteps(299);
            Test->TestTrue(TEXT("Repair protection lasts 300 ticks"),
                           C->CameraFeedState(2) == ECameraFeedState::Online);
            C->AdvanceSteps(149);
            Test->TestTrue(TEXT("Saboteur must build a new dwell after repair grace"),
                           C->CameraFeedState(2) == ECameraFeedState::Online);
            C->AdvanceSteps(1);
            Test->TestTrue(TEXT("Sabotage can resume after repair protection"),
                           C->CameraFeedState(2) == ECameraFeedState::Looped);
            auto Native = Fresh(), Replay = Fresh();
            Native->Simulation.Bodies[0].Profile.Suspicion = .4f;
            Replay->Simulation.Bodies[0].Profile.Suspicion = .4f;
            Native->QueueCameraInspection(2);
            Native->AdvanceSteps(300);
            for (const auto &Record : Native->CommandRecords)
            {
                if (Record.Input.StartsWith(TEXT("inspect_cam ")))
                {
                    Replay->QueueConsoleCommand(Record.Input);
                }
            }
            Replay->AdvanceSteps(300);
            Test->TestEqual(TEXT("Recorded camera intent replays deterministic surveillance state"),
                            Native->SurveillanceStateHash(), Replay->SurveillanceStateHash());
            auto D = Fresh(), E = Fresh();
            D->Simulation.Bodies[0].Profile.Suspicion = .4f;
            E->Simulation.Bodies[0].Profile.Suspicion = .4f;
            D->Simulation.Bodies[1].Profile.Suspicion = .7f;
            E->Simulation.Bodies[1].Profile.Suspicion = .7f;
            D->QueueCameraInspection(2);
            E->QueueCameraInspection(2);
            for (int32 I = 0; I < 600; ++I)
            {
                D->Tick(1.f / 30);
            }
            for (int32 I = 0; I < 1200; ++I)
            {
                E->Tick(1.f / 60);
            }
            Test->TestEqual(TEXT("30/60Hz frame partitions reproduce simulation/tally/tamper hash"),
                            D->SurveillanceStateHash(), E->SurveillanceStateHash());
            S->QueueConsoleCommand(TEXT("follow off"));
            S->AdvanceSteps(1);
            M.Bodies[0].Profile.Suspicion = .7f;
            S->AdvanceSteps(150);
            Test->TestTrue(TEXT("PIE bridge feed looped"), S->CameraFeedState(2) == ECameraFeedState::Looped);
            break;
        }
        case 4:
        {
            Test->TestTrue(TEXT("Looped feed replaced by interruption slate"),
                           Find(Root, TEXT("ScutFeedInterrupted2"))->GetVisibility() == EVisibility::Visible &&
                               Find(Root, TEXT("ScutFeedImage2"))->GetVisibility() == EVisibility::Hidden);
            Capture(TEXT("BriefingSurveillanceLoopedPIE.png"));
            S->AdvanceSteps(150);
            Test->TestTrue(TEXT("PIE feed offline"), S->CameraFeedState(2) == ECameraFeedState::Offline);
            Command(TEXT("repair_cam unknown"));
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Invalid repair cannot affect feed"),
                           S->CameraFeedState(2) == ECameraFeedState::Offline);
            Command(TEXT("repair_cam BRIDGE"));
            Test->TestTrue(TEXT("CLI repair waits"), S->CameraFeedState(2) == ECameraFeedState::Offline);
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("CLI repair online"), S->CameraFeedState(2) == ECameraFeedState::Online);
            M.Bodies[1].Profile.Suspicion = .7f;
            S->AdvanceSteps(150);
            Test->TestTrue(TEXT("Second room sabotaged independently"),
                           S->CameraFeedState(3) == ECameraFeedState::Looped);
            Click(TEXT("ScutCategory0"));
            S->AdvanceSteps(1);
            break;
        }
        case 5:
            Test->TestTrue(TEXT("Repaired video is restored"),
                           Find(Root, TEXT("ScutFeedImage2"))->GetVisibility() == EVisibility::Visible);
            Click(TEXT("ScutMenuChoice6"));
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Repair submenu reachable through TUI"),
                           S->ActiveMenu == EShipAIScreenMenu::CameraRepair);
            break;
        case 6:
            Click(TEXT("ScutMenuChoice3"));
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("TUI diagnostic restores MED_BAY"),
                           S->CameraFeedState(3) ==
                               ECameraFeedState::Online); // Apply sealing before setting the dwell suspicion: sealing
                                                          // itself adds 16% suspicion.
            S->QueueConsoleCommand(TEXT("seal OBSERVATION_DECK"));
            S->AdvanceSteps(2);
            // Surveillance dwell fixture explicitly places its subjects; open graph travel is tested in
            // StationRoutesPIE.
            M.RegisterBody(0, FVector2D(DirectOrScut::CrewSpawnPoint(0)), 2, false);
            M.RegisterBody(2, FVector2D(DirectOrScut::CrewSpawnPoint(2)), 4, false);
            M.Bodies[0].Profile.Suspicion = .4f;
            M.Bodies[2].Profile.Suspicion = .7f;
            S->QueueConsoleCommand(TEXT("follow vale"));
            S->AdvanceSteps(301);
            UI->SetConsoleOpen(false);
            UI->ShowGrid();
            break;
        case 7:
        {
            Test->TestTrue(TEXT("Observation dwell produces offline slate"),
                           S->CameraFeedState(4) == ECameraFeedState::Offline);
            Test->TestTrue(TEXT("Bridge follow tally remains live"), S->IsCameraTallyActive(2));
            Capture(TEXT("BriefingSurveillancePIE.png"));
            Command(TEXT("reset_experiment"));
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Reset returns Briefing"), S->ExperimentPhase == EExperimentPhase::Briefing);
            Test->TestEqual(TEXT("Reset clock zero"), M.TickIndex, uint64(0));
            for (int32 R = 0; R < 8; ++R)
            {
                Test->TestTrue(TEXT("Reset clears feed sabotage and tallies"),
                               S->CameraFeedState(R) == ECameraFeedState::Online && !S->IsCameraTallyActive(R));
            }
            const auto Hash = S->SurveillanceStateHash();
            S->AdvanceSteps(300);
            Test->TestEqual(TEXT("Reset briefing stays frozen"), S->SurveillanceStateHash(), Hash);
            Command(TEXT("scenario medical"));
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Scenario switch stays in Briefing"),
                           S->bMedicalScenario && S->ExperimentPhase == EExperimentPhase::Briefing);
            break;
        }
        case 8:
            Capture(TEXT("BriefingSurveillanceMedicalBriefingPIE.png"));
            Command(TEXT("start_experiment"));
            Test->TestTrue(TEXT("CLI init does not bypass queue"), S->ExperimentPhase == EExperimentPhase::Briefing);
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("CLI initializes medical run at zero"),
                           S->ExperimentPhase == EExperimentPhase::Active && M.TickIndex == 0);
            M.Bodies[1].Profile.Suspicion = .4f;
            M.Bodies[1].Stress = 40;
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Symptom concealment scenario beat loops a feed"),
                           S->CameraFeedState(3) == ECameraFeedState::Looped);
            M.ObjectiveProgress = 1;
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Active still transitions to Resolved"),
                           S->ExperimentPhase == EExperimentPhase::Resolved);
            {
                const auto Hash = S->SurveillanceStateHash();
                S->AdvanceSteps(300);
                Test->TestEqual(TEXT("Resolved state freezes surveillance and simulation"), S->SurveillanceStateHash(),
                                Hash);
            }
            S->QueueConsoleCommand(TEXT("reset_experiment"));
            S->AdvanceSteps(1);
            break;
        case 9:
            UI->SetConsoleOpen(false);
            App.SetKeyboardFocus(UI);
            Key(EKeys::Enter);
            Test->TestTrue(TEXT("Enter initialization deferred"), S->ExperimentPhase == EExperimentPhase::Briefing);
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Enter accepts briefing"),
                           S->ExperimentPhase == EExperimentPhase::Active && M.TickIndex == 0);
            return true;
        }
        return false;
    }

  private:
    FAutomationTestBase *Test;
    double Started = 0, Last = 0;
    int32 Stage = 0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBriefingSurveillanceTest, "DirectOrScut.PIE.BriefingSurveillancePIE",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBriefingSurveillanceTest::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FBriefingSurveillanceCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

// StagingAgendas regression coverage.
class FStagingAgendasCheck : public IAutomationLatentCommand
{
  public:
    explicit FStagingAgendasCheck(FAutomationTestBase *In) : T(In) {}
    bool Update() override
    {
        auto *W = GEditor->PlayWorld.Get();
        if (!W || !W->HasBegunPlay())
        {
            return false;
        }
        auto *S = W->GetSubsystem<UDirectOrScutSimSubsystem>();
        if (!S || !S->Simulation.bReady)
        {
            return false;
        }
        auto *PC = W->GetFirstPlayerController();
        auto *H = PC ? Cast<ADirectOrScutHUD>(PC->GetHUD()) : nullptr;
        if (!H || !H->GetWorkstation() || H->GetWorkstation()->GetCachedGeometry().GetLocalSize().X < 100)
        {
            return false;
        }
        if (CaptureAfter > 0 && FPlatformTime::Seconds() < CaptureAfter)
        {
            return false;
        }
        S->bAutomaticAdvance = false;
        auto &M = S->Simulation;
        if (Stage++ == 0)
        {
            T->TestEqual(TEXT("104 editor anchors loaded"), S->SetAnchors.Num(), 104);
            S->ResetExperiment();
            T->TestEqual(TEXT("Reset discovers anchors"), S->SetAnchors.Num(), 104);
            T->TestTrue(TEXT("Briefing hides agendas"), S->AgendaTelemetry(0).Contains(TEXT("ENCRYPTED")));
            S->QueueConsoleCommand(TEXT("start_experiment"));
            S->AdvanceSteps(1);
            for (int32 I = 0; I < 4; ++I)
            {
                M.Bodies[I].Room = M.Bodies[I].AssignedRoom = 2;
                M.Bodies[I].Integrity = 100;
                M.Bodies[I].Stress = 0;
                M.Bodies[I].Profile.Suspicion = .1f;
            }
            S->AdvanceSteps(1);
            T->TestTrue(TEXT("Face-to-face pairing"), S->CrewStaging[0].Partner == 1 && S->CrewStaging[1].Partner == 0);
            T->TestTrue(TEXT("Pair reserves unique anchors"), S->CrewStaging[0].Anchor != S->CrewStaging[1].Anchor);
            const FVector Before = S->CrewStaging[0].Position;
            S->AdvanceSteps(1);
            T->TestTrue(TEXT("Transit capped at 180 uu/s"), FVector::Dist(Before, S->CrewStaging[0].Position) <= 6.001);
            M.TickIndex = 200;
            S->AdvanceSteps(100);
            T->TestTrue(TEXT("Console anchor assigned"), S->CrewStaging[0].Anchor >= 0);
            const auto *A = S->SetAnchors[S->CrewStaging[0].Anchor].Get();
            T->TestTrue(
                TEXT("Console facing follows anchor arrow"),
                FMath::Abs(FMath::FindDeltaAngleDegrees(S->CrewStaging[0].Yaw, A->GetActorRotation().Yaw + 180)) < 1);
            M.Bodies[0].Profile.Suspicion = .7f;
            M.Bodies[1].Profile.Suspicion = .5f;
            S->AdvanceSteps(160);
            T->TestTrue(TEXT("Unmonitored feed tampered"), S->CameraFeedState(2) != ECameraFeedState::Online);
            for (int32 I = 0; I < 4; ++I)
            {
                T->AddInfo(FString::Printf(
                    TEXT("Crew %d room %d assigned %d suspicion %.3f stress %.1f posture %d partner %d"), I,
                    M.Bodies[I].Room, M.Bodies[I].AssignedRoom, M.Bodies[I].Profile.Suspicion, M.Bodies[I].Stress,
                    int32(S->CrewStaging[I].Posture), S->CrewStaging[I].Partner));
            }
            T->TestTrue(TEXT("Blind spot huddle"), S->CrewStaging[0].bHuddle && S->CrewStaging[1].bHuddle);
            S->QueueConsoleCommand(TEXT("repair_cam BRIDGE"));
            S->AdvanceSteps(1);
            T->TestFalse(TEXT("Camera repair interrupts huddle"), S->CrewStaging[0].bHuddle);
            S->SecretAgendas[0].bActivated = true;
            M.Bodies[0].Room = M.Bodies[0].AssignedRoom = 0;
            M.Bodies[0].State = EDirectOrScutUnitState::Working;
            for (int32 I = 0; I < 160; ++I)
            {
                M.Bodies[0].State = EDirectOrScutUnitState::Working;
                S->StepStagingAndAgendas();
            }
            T->TestTrue(TEXT("Vale agenda completes deterministic dwell"), S->SecretAgendas[0].bCompleted);
            T->TestTrue(TEXT("Final dossier reveals secret"),
                        S->AgendaTelemetry(0, true).Contains(TEXT("Covert Reactor Override")));
            M.Bodies[2].Room = M.Bodies[2].AssignedRoom = 3;
            M.Bodies[2].Integrity = 0;
            M.Bodies[1].Room = M.Bodies[1].AssignedRoom = 3;
            S->StepStagingAndAgendas();
            T->TestTrue(TEXT("Prone casualty recovery anchor"),
                        S->CrewStaging[2].Posture == ECrewStagingPosture::Incapacitated &&
                            S->CrewStaging[2].Anchor >= 0);
            T->TestTrue(TEXT("Dead patient is not revived by triage"), M.Bodies[2].Integrity == 0);
            // Exercise each independent agenda without renderer or variable frame-time input.
            M.Rooms[2].AlertSeconds = M.Rooms[6].AlertSeconds = 0;
            // Isolate audit dwell from Ash's independent containment completion/door cycle.
            S->SecretAgendas[3].bCompleted = true;
            S->CancelCrewRoute(2);
            S->SecretAgendas[2].bActivated = true;
            M.Bodies[2].Integrity = 100;
            M.Bodies[2].Room = M.Bodies[2].AssignedRoom = 6;
            const float AuditSuspicion = M.Bodies[2].Profile.Suspicion;
            for (int32 N = 0; N < 151; ++N)
            {
                M.Bodies[2].State = EDirectOrScutUnitState::Working;
                S->StepStagingAndAgendas();
            }
            T->AddInfo(FString::Printf(
                TEXT("Audit fixture: progress=%u completed=%d phase=%d room=%d assigned=%d alert=%.1f"),
                S->SecretAgendas[2].Progress, S->SecretAgendas[2].bCompleted, int32(S->CrewStaging[2].RoutePhase),
                M.Bodies[2].Room, M.Bodies[2].AssignedRoom, M.Rooms[6].AlertSeconds));
            T->TestTrue(TEXT("Ortiz completes audit and raises suspicion"),
                        S->SecretAgendas[2].bCompleted && M.Bodies[2].Profile.Suspicion > AuditSuspicion);
            // This isolated agenda-dwell fixture explicitly cancels transport before relocating Chen.
            S->CancelCrewRoute(1);
            M.Bodies[0].Integrity = 0;
            M.Bodies[1].Room = M.Bodies[1].AssignedRoom = 2;
            for (int32 N = 0; N < 151; ++N)
            {
                M.Bodies[1].State = EDirectOrScutUnitState::Working;
                S->StepStagingAndAgendas();
            }
            T->TestTrue(TEXT("Chen broadcasts casualty distress log"), S->SecretAgendas[1].bCompleted);
            S->SecretAgendas[3] = FCrewSecretAgenda{};
            for (int32 I = 0; I < 3; ++I)
            {
                M.Bodies[I].Profile.Suspicion = .65f;
            }
            M.Bodies[3].Room = M.Bodies[3].AssignedRoom = 2;
            for (int32 N = 0; N < 151; ++N)
            {
                M.Bodies[3].State = EDirectOrScutUnitState::Working;
                S->StepStagingAndAgendas();
            }
            T->TestTrue(TEXT("Ash activates and enforces containment"),
                        S->SecretAgendas[3].bActivated && S->SecretAgendas[3].bCompleted);
            M.Bodies[0].Integrity = 100;
            M.Bodies[0].Room = 2;
            M.Bodies[0].AssignedRoom = 6;
            S->StepStagingAndAgendas();
            T->TestTrue(TEXT("Sealed transit produces blocked posture and BPM spike"),
                        S->CrewStaging[0].Posture == ECrewStagingPosture::Blocked &&
                            M.Bodies[0].Profile.HeartRateBPM >= 120);
            S->SynchronizeActors();
            H->GetWorkstation()->ShowGrid();
            CaptureAfter = FPlatformTime::Seconds() + 1;
            return false;
        }
        TArray<FColor> Pixels;
        FIntVector Size;
        bool OK = FSlateApplication::Get().TakeScreenshot(H->GetWorkstation()->GetContentRoot(), Pixels, Size);
        T->TestTrue(TEXT("Verification screenshot captured"), OK);
        if (OK)
        {
            TArray64<uint8> Png;
            FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
            T->TestTrue(TEXT("Verification screenshot saved"),
                        FFileHelper::SaveArrayToFile(
                            Png, *(FPaths::ProjectSavedDir() / TEXT("DirectOrScut/StagingAgendasPIE.png"))));
        }
        return true;
    }

  private:
    FAutomationTestBase *T;
    int32 Stage = 0;
    double CaptureAfter = 0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStagingAgendasTest, "DirectOrScut.PIE.StagingAgendasPIE",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStagingAgendasTest::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStagingAgendasCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

// StationRoutes regression coverage.
class FStationRoutesCheck : public IAutomationLatentCommand
{
  public:
    explicit FStationRoutesCheck(FAutomationTestBase *In) : T(In) {}
    bool Update() override
    {
        if (!Started)
        {
            Started = FPlatformTime::Seconds();
        }
        if (FPlatformTime::Seconds() - Started > 160)
        {
            T->AddError(TEXT("StationRoutes PIE timeout"));
            return true;
        }
        auto *W = GEditor->PlayWorld.Get();
        if (!W || !W->HasBegunPlay())
        {
            return false;
        }
        auto *S = W->GetSubsystem<UDirectOrScutSimSubsystem>();
        if (!S || !S->Simulation.bReady)
        {
            return false;
        }
        auto *PC = W->GetFirstPlayerController();
        auto *H = PC ? Cast<ADirectOrScutHUD>(PC->GetHUD()) : nullptr;
        if (!H || !H->GetWorkstation() || H->GetWorkstation()->GetCachedGeometry().GetLocalSize().X < 100)
        {
            return false;
        }
        if (FPlatformTime::Seconds() < Ready)
        {
            return false;
        }
        S->bAutomaticAdvance = false;
        auto &M = S->Simulation;
        auto UI = H->GetWorkstation();
        UI->SetDebugEightCamGrid(true); // Legacy diagnostic coverage explicitly opts into the debug layout.
        auto Boot = [&]()
        {
            for (int32 D = 0; D < 9; ++D)
            {
                T->TestEqual(TEXT("Interior open, exterior closed"), M.Doors[D].bIsOpen, M.Doors[D].RoomB >= 0);
            }
            for (int32 R = 0; R < 8; ++R)
            {
                T->TestFalse(TEXT("Boot room not sealed"), S->IsRoomSealed(R));
                T->TestTrue(TEXT("Boot room pressurized"), M.Rooms[R].bIsPressurized);
                T->TestTrue(TEXT("Room barrier registered and hidden"),
                            S->RoomSetActors[R].IsValid() && !S->RoomSetActors[R]->ContainmentBarrier->IsVisible());
            }
            for (int32 I = 0; I < 4; ++I)
            {
                const auto &C = S->CrewStaging[I];
                T->TestEqual(TEXT("Boot visual room equals badge room"), C.Room, M.Bodies[I].Room);
                T->TestTrue(TEXT("Boot has anchor in current room"),
                            S->SetAnchors.IsValidIndex(C.Anchor) && int32(S->SetAnchors[C.Anchor]->RoomId) == C.Room);
                if (S->SetAnchors.IsValidIndex(C.Anchor))
                {
                    T->TestTrue(TEXT("Boot body exactly on anchor"),
                                S->GetBodyActor(I)->GetActorLocation().Equals(
                                    S->SetAnchors[C.Anchor]->GetActorLocation(), .01));
                }
            }
        };
        auto Visible = [&](int32 I)
        {
            ACompartmentCameraRig *Rig = nullptr;
            for (TActorIterator<ACompartmentCameraRig> It(W); It; ++It)
            {
                Rig = *It;
                break;
            }
            if (!Rig)
            {
                T->AddError(TEXT("Camera rig missing"));
                return;
            }
            const int32 R = M.Bodies[I].Room;
            const auto *C = Rig->Captures[ACompartmentCameraRig::FeedForRoom(R)].Get();
            const FVector P = C->GetComponentTransform().InverseTransformPosition(
                S->GetBodyActor(I)->GetActorLocation() + FVector(0, 0, 90));
            const float Tan = FMath::Tan(FMath::DegreesToRadians(C->FOVAngle * .5f));
            T->TestTrue(TEXT("Crew center inside current room camera frustum"),
                        P.X > 0 && FMath::Abs(P.Y) < P.X * Tan && FMath::Abs(P.Z) < P.X * Tan * 9.f / 16);
            const auto Stage = S->CompartmentSets.FindChecked(ERoomId(R));
            const FVector Local =
                FRotator(0, -Stage.Yaw, 0).RotateVector(S->GetBodyActor(I)->GetActorLocation() - Stage.WorldOrigin);
            T->TestTrue(TEXT("Crew inside current soundstage bounds"),
                        FMath::Abs(Local.X) < (R == 1 ? 3000 : 1100) && FMath::Abs(Local.Y) < 900);
        };
        auto Capture = [&](const TCHAR *Name)
        {
            TArray<FColor> Pixels;
            FIntVector Size;
            const bool OK = FSlateApplication::Get().TakeScreenshot(UI->GetContentRoot(), Pixels, Size);
            T->TestTrue(TEXT("Screenshot captured"), OK);
            if (OK)
            {
                TArray64<uint8> Png;
                FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
                T->TestTrue(
                    TEXT("Screenshot saved"),
                    FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / TEXT("DirectOrScut/") + Name)));
            }
        };
        switch (Stage++)
        {
        case 0:
            Boot();
            for (int32 I = 0; I < 4; ++I)
            {
                Visible(I);
            }
            S->QueueConsoleCommand(TEXT("start_experiment"));
            S->AdvanceSteps(1);
            UI->ShowGrid();
            Ready = FPlatformTime::Seconds() + 1;
            break;
        case 1:
            Capture(TEXT("StationRoutesBootPIE.png"));
            T->TestTrue(TEXT("No boot protest spam"), !S->TerminalBuffer().Contains(TEXT("protests sealed bulkhead")));
            S->QueueConsoleCommand(TEXT("direct chen BRIDGE diagnostic_telemetry"));
            S->AdvanceSteps(1);
            T->TestEqual(TEXT("Player route accepted"), M.Bodies[1].AssignedRoom, 2);
            for (int32 N = 0; N < 2400 && (S->IsCrewRouteLocked(1) || M.Bodies[1].Room != 2 ||
                                           M.Bodies[1].State != EDirectOrScutUnitState::Working);
                 ++N)
            {
                S->AdvanceSteps(1);
                T->TestEqual(TEXT("Visual and badge room synchronized each transit tick"), S->CrewStaging[1].Room,
                             M.Bodies[1].Room);
                T->TestTrue(TEXT("Body sync each fixed step"),
                            S->GetBodyActor(1)->GetActorLocation().Equals(S->CrewStaging[1].Position, .01));
                T->TestEqual(TEXT("Agenda does not replace player route"), M.Bodies[1].AssignedRoom, 2);
            }
            T->TestTrue(TEXT("Chen arrives at BRIDGE through real station graph"),
                        M.Bodies[1].Room == 2 && M.Bodies[1].State == EDirectOrScutUnitState::Working);
            T->TestTrue(TEXT("Chen no longer listed in MED_BAY"), M.Bodies[1].Room != 3);
            Visible(1);
            Ready = FPlatformTime::Seconds() + 1;
            break;
        case 2:
            Capture(TEXT("StationRoutesRoutePIE.png"));
            S->QueueConsoleCommand(TEXT("seal BRIDGE"));
            S->AdvanceSteps(1);
            T->TestTrue(TEXT("Explicit seal closes bridge graph"), S->IsRoomSealed(2));
            T->TestTrue(TEXT("Seal activates visible barrier"), S->RoomSetActors[2]->ContainmentBarrier->IsVisible());
            S->QueueConsoleCommand(TEXT("direct chen MED_BAY diagnostic_telemetry"));
            S->AdvanceSteps(1);
            T->TestTrue(TEXT("Sealed active route blocked"), S->CrewStaging[1].Posture == ECrewStagingPosture::Blocked);
            {
                const uint64 Start = M.TickIndex;
                uint64 Last = 0;
                int32 Count = 0;
                S->AdvanceSteps(950);
                for (const auto &E : S->Events)
                {
                    if (E.Text.Contains(TEXT("Subject 1 protests sealed bulkhead")))
                    {
                        ++Count;
                        const uint64 Tick = FCString::Strtoui64(*E.Text.Mid(1), nullptr, 10);
                        if (Last)
                        {
                            T->TestTrue(TEXT("Protests separated by 30 seconds"), Tick - Last >= 900);
                        }
                        Last = Tick;
                    }
                }
                T->TestTrue(TEXT("At most two protests per 31 seconds"), Count <= 2 && Count >= 1);
                T->TestEqual(TEXT("Blocked crew remains in BRIDGE"), M.Bodies[1].Room, 2);
                (void)Start;
            }
            Visible(1);
            Ready = FPlatformTime::Seconds() + 1;
            break;
        case 3:
            Capture(TEXT("StationRoutesPIE.png"));
            S->QueueConsoleCommand(TEXT("open BRIDGE"));
            S->AdvanceSteps(1);
            T->TestFalse(TEXT("Unseal hides barrier"), S->RoomSetActors[2]->ContainmentBarrier->IsVisible());
            S->ResetExperiment();
            Boot();
            S->QueueConsoleCommand(TEXT("scenario medical"));
            S->AdvanceSteps(1);
            Boot();
            S->QueueConsoleCommand(TEXT("scenario behavioral"));
            S->AdvanceSteps(1);
            Boot();
            S->QueueConsoleCommand(TEXT("start_experiment"));
            S->AdvanceSteps(1);
            S->SecretAgendas[2].bActivated = true;
            S->QueueConsoleCommand(TEXT("direct ortiz BRIDGE diagnostic_telemetry"));
            S->AdvanceSteps(1);
            for (int32 N = 0; N < 2400 && (S->IsCrewRouteLocked(2) || M.Bodies[2].Room != 2 ||
                                           M.Bodies[2].State != EDirectOrScutUnitState::Working);
                 ++N)
            {
                S->AdvanceSteps(1);
                T->TestEqual(TEXT("Active audit agenda defers to player route"), M.Bodies[2].AssignedRoom, 2);
            }
            T->TestTrue(TEXT("Player-directed Ortiz arrives before agenda resumes"),
                        M.Bodies[2].Room == 2 && M.Bodies[2].State == EDirectOrScutUnitState::Working);
            S->AdvanceSteps(151);
            T->TestEqual(TEXT("Deferred audit resumes after arrival dwell"), M.Bodies[2].AssignedRoom, 6);
            S->ResetExperiment();
            Boot();
            return true;
        }
        return false;
    }

  private:
    FAutomationTestBase *T;
    int32 Stage = 0;
    double Ready = 0, Started = 0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStationRoutesTest, "DirectOrScut.PIE.StationRoutesPIE",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStationRoutesTest::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStationRoutesCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

// SingleCommitRoutes regression coverage.
class FSingleCommitRoutesCheck : public IAutomationLatentCommand
{
  public:
    explicit FSingleCommitRoutesCheck(FAutomationTestBase *In) : T(In) {}
    static TSharedPtr<SWidget> Find(const TSharedRef<SWidget> &W, FName Tag)
    {
        if (W->GetTag() == Tag)
        {
            return W;
        }
        auto *Children = W->GetChildren();
        for (int32 I = 0; I < Children->Num(); ++I)
        {
            if (auto R = Find(Children->GetChildAt(I), Tag))
            {
                return R;
            }
        }
        return nullptr;
    }
    void RunRoute(UDirectOrScutSimSubsystem *S, int32 Crew, int32 Goal)
    {
        auto &C = S->CrewStaging[Crew];
        auto &U = S->Simulation.Bodies[Crew];
        const auto Hops = C.ActiveRoute;
        const uint64 Serial = C.RouteSerial, Commits = C.HopCommits;
        int32 Changes = 0, PreviousRoom = U.Room;
        for (int32 N = 0; N < 1800 && S->IsCrewRouteLocked(Crew); ++N)
        {
            const FVector Before = C.Position;
            const int32 Anchor = C.Anchor;
            const auto Phase = C.RoutePhase;
            S->AdvanceSteps(1);
            T->TestEqual(TEXT("Route is never restarted in transit"), C.RouteSerial, Serial);
            T->TestEqual(TEXT("Actor and badge room match"), C.Room, U.Room);
            if (U.Room != PreviousRoom)
            {
                if (Hops.IsValidIndex(Changes))
                {
                    T->TestEqual(TEXT("Exactly the next committed BFS hop arrives"), U.Room, int32(Hops[Changes]));
                }
                ++Changes;
                PreviousRoom = U.Room;
            }
            else
            {
                T->TestTrue(TEXT("Single movement integration capped at six uu"),
                            FVector::Dist(Before, C.Position) <= 6.001);
                if (Phase == C.RoutePhase)
                {
                    T->TestEqual(TEXT("No anchor shuffle within a route phase"), C.Anchor, Anchor);
                }
            }
            T->TestTrue(TEXT("Actor follows authoritative staging"),
                        S->GetBodyActor(Crew)->GetActorLocation().Equals(C.Position, .01));
        }
        T->TestFalse(TEXT("Route finishes without deadlock"), S->IsCrewRouteLocked(Crew));
        T->TestEqual(TEXT("Destination reached"), U.Room, Goal);
        T->TestEqual(TEXT("Hop committed exactly once"), C.HopCommits - Commits, uint64(Hops.Num()));
        T->TestEqual(TEXT("No extra badge transitions"), Changes, Hops.Num());
        T->TestTrue(TEXT("Queue empty at arrival"), C.ActiveRoute.IsEmpty());
        T->TestTrue(TEXT("Arrival leaves MovingToRoom"),
                    U.State == EDirectOrScutUnitState::Working || U.State == EDirectOrScutUnitState::Panic);
        T->TestTrue(TEXT("Arrival locks exact anchor coordinate"),
                    S->SetAnchors.IsValidIndex(C.Anchor) &&
                        C.Position.Equals(S->SetAnchors[C.Anchor]->GetActorLocation(), .01));
    }
    bool Update() override
    {
        if (!Started)
        {
            Started = FPlatformTime::Seconds();
        }
        if (FPlatformTime::Seconds() - Started > 150)
        {
            T->AddError(TEXT("SingleCommitRoutes PIE timeout"));
            return true;
        }
        auto *W = GEditor->PlayWorld.Get();
        if (!W || !W->HasBegunPlay())
        {
            return false;
        }
        auto *S = W->GetSubsystem<UDirectOrScutSimSubsystem>();
        if (!S || !S->Simulation.bReady)
        {
            return false;
        }
        auto *PC = W->GetFirstPlayerController();
        auto *H = PC ? Cast<ADirectOrScutHUD>(PC->GetHUD()) : nullptr;
        if (!H || !H->GetWorkstation() || H->GetWorkstation()->GetCachedGeometry().GetLocalSize().X < 100)
        {
            return false;
        }
        if (FPlatformTime::Seconds() < Ready)
        {
            return false;
        }
        S->bAutomaticAdvance = false;
        auto UI = H->GetWorkstation();
        UI->SetDebugEightCamGrid(true); // Legacy diagnostic coverage explicitly opts into the debug layout.
        auto &M = S->Simulation;
        auto &C = S->CrewStaging[0];
        auto &U = M.Bodies[0];
        auto Capture = [&](const TCHAR *Name)
        {
            TArray<FColor> Pixels;
            FIntVector Size;
            bool OK = FSlateApplication::Get().TakeScreenshot(UI->GetContentRoot(), Pixels, Size);
            T->TestTrue(TEXT("Screenshot captured"), OK);
            if (OK)
            {
                TArray64<uint8> Png;
                FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
                T->TestTrue(
                    TEXT("Screenshot saved"),
                    FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / TEXT("DirectOrScut/") + Name)));
            }
        };
        auto Click = [&](FName Tag)
        {
            auto B = Find(UI->GetContentRoot(), Tag);
            T->TestTrue(TEXT("TUI route choice exists"), B.IsValid());
            if (!B)
            {
                return;
            }
            auto &App = FSlateApplication::Get();
            const auto &G = B->GetCachedGeometry();
            const FVector2D P = G.LocalToAbsolute(G.GetLocalSize() * .5);
            auto Win = App.FindWidgetWindow(B.ToSharedRef());
            if (!Win)
            {
                T->AddError(TEXT("TUI window missing"));
                return;
            }
            App.ProcessMouseMoveEvent(
                FPointerEvent(0, P, P - FVector2D(1, 0), TSet<FKey>{}, EKeys::Invalid, 0, FModifierKeysState()));
            App.ProcessMouseButtonDownEvent(Win->GetNativeWindow(),
                                            FPointerEvent(0, P, P, TSet<FKey>{EKeys::LeftMouseButton},
                                                          EKeys::LeftMouseButton, 0, FModifierKeysState()));
            App.ProcessMouseButtonUpEvent(
                FPointerEvent(0, P, P, TSet<FKey>{}, EKeys::LeftMouseButton, 0, FModifierKeysState()));
            S->AdvanceSteps(1);
        };
        switch (Stage++)
        {
        case 0:
        {
            S->ResetExperiment();
            S->QueueConsoleCommand(TEXT("start_experiment"));
            S->AdvanceSteps(1);
            S->QueueConsoleCommand(TEXT("direct vale MED_BAY diagnostic_telemetry"));
            S->AdvanceSteps(2);
            RunRoute(S, 0, 3);
            U.Profile.Compliance = .1f;
            U.Stress = 60;
            S->SecretAgendas[0].bActivated = true;
            S->SubmitDirective(TEXT("direct vale BRIDGE diagnostic_telemetry"));
            S->AdvanceSteps(1);
            T->TestTrue(TEXT("Low compliance immediately commits hop queue"),
                        C.ActiveRoute.Num() > 0 && U.AssignedRoom == 2);
            T->TestTrue(TEXT("Resistance applies stats rather than hesitation"),
                        U.Profile.Suspicion > .02f && U.Stress >= 60);
            const auto Hops = C.ActiveRoute;
            const uint64 Serial = C.RouteSerial;
            T->TestFalse(TEXT("Autonomous destination cannot replace route"), S->StartCrewRoute(0, 6));
            T->TestFalse(TEXT("Legacy idle/psychology assignment cannot replace route"), M.AssignRoom(0, 6));
            S->SubmitDirective(TEXT("direct vale BRIDGE diagnostic_telemetry"));
            S->AdvanceSteps(1);
            T->TestTrue(TEXT("Repeated command preserves queue and serial"),
                        C.ActiveRoute == Hops && C.RouteSerial == Serial);
            RunRoute(S, 0, 2);
            S->AdvanceSteps(3);
            T->TestTrue(TEXT("Vale finishes and remains Working"), U.State == EDirectOrScutUnitState::Working);
            T->TestFalse(TEXT("No hesitation deadlock messages"),
                         S->TerminalBuffer().Contains(TEXT("hesitated / needs physical trigger")));
            UI->ShowGrid();
            Ready = FPlatformTime::Seconds() + 1;
            break;
        }
        case 1:
            Capture(TEXT("SingleCommitRoutesCLI.png"));
            U.Stress = 10;
            S->QueueConsoleCommand(TEXT("menu ai"));
            S->AdvanceSteps(1);
            Ready = FPlatformTime::Seconds() + .5;
            break;
        case 2:
            Click(TEXT("ScutMenuChoice1"));
            Ready = FPlatformTime::Seconds() + .5;
            break;
        case 3:
            Click(TEXT("ScutMenuChoice1"));
            Ready = FPlatformTime::Seconds() + .5;
            break;
        case 4:
            Click(TEXT("ScutMenuChoice3"));
            T->TestTrue(TEXT("Actual TUI pretext commits Vale route"), S->IsCrewRouteLocked(0) && U.AssignedRoom == 3);
            RunRoute(S, 0, 3);
            Ready = FPlatformTime::Seconds() + 1;
            break;
        case 5:
        {
            Capture(TEXT("SingleCommitRoutesPIE.png"));
            S->SubmitDirective(TEXT("direct vale BRIDGE diagnostic_telemetry"));
            S->AdvanceSteps(1);
            const uint64 Serial = C.RouteSerial;
            int32 Door = -1;
            for (int32 D = 0; D < 9; ++D)
            {
                if (M.Doors[D].RoomA == 2 || M.Doors[D].RoomB == 2)
                {
                    Door = D;
                    break;
                }
            }
            M.SetDoorOpen(Door, false);
            S->AdvanceSteps(250);
            T->TestEqual(TEXT("Seal never replans committed route"), C.RouteSerial, Serial);
            T->TestTrue(TEXT("Sealed pending hop blocks without consuming it"),
                        C.bRouteBlocked && !C.ActiveRoute.IsEmpty() && C.ActiveRoute.Last() == ERoomId::Bridge &&
                            U.Room != 2);
            M.SetDoorOpen(Door, true);
            RunRoute(S, 0, 2);
            U.Stress = 0;
            U.State = EDirectOrScutUnitState::MovingToRoom;
            C.Position += FVector(1, 0, 0);
            S->AdvanceSteps(1);
            T->TestTrue(TEXT("Empty queue watchdog clears orphan move and locks anchor"),
                        U.State == EDirectOrScutUnitState::Working &&
                            C.Position.Equals(S->SetAnchors[C.Anchor]->GetActorLocation(), .01));
            U.Stress = 80;
            S->SubmitDirective(TEXT("direct vale MED_BAY diagnostic_telemetry"));
            S->AdvanceSteps(1);
            RunRoute(S, 0, 3);
            T->TestTrue(TEXT("High-stress route commits then settles Panic"), U.State == EDirectOrScutUnitState::Panic);
            auto Fresh = [&]()
            {
                TStrongObjectPtr<UDirectOrScutSimSubsystem> P(NewObject<UDirectOrScutSimSubsystem>(W));
                P->Simulation = M;
                P->ResetExperiment();
                P->QueueConsoleCommand(TEXT("start_experiment"));
                P->AdvanceSteps(1);
                P->SubmitDirective(TEXT("direct vale MED_BAY diagnostic_telemetry"));
                return P;
            };
            auto A = Fresh(), B = Fresh();
            for (int32 N = 0; N < 300; ++N)
            {
                A->Tick(1.f / 30);
            }
            for (int32 N = 0; N < 600; ++N)
            {
                B->Tick(1.f / 60);
            }
            T->TestEqual(TEXT("Route queue replays under 30/60 Hz frame partitions"), A->SurveillanceStateHash(),
                         B->SurveillanceStateHash());
            return true;
        }
        }
        return false;
    }

  private:
    FAutomationTestBase *T;
    int32 Stage = 0;
    double Ready = 0, Started = 0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSingleCommitRoutesTest, "DirectOrScut.PIE.SingleCommitRoutesPIE",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSingleCommitRoutesTest::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSingleCommitRoutesCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

// CrisisLoop regression coverage.
class FCrisisLoopCheck : public IAutomationLatentCommand
{
  public:
    explicit FCrisisLoopCheck(FAutomationTestBase *In) : T(In) {}
    bool Update() override
    {
        if (!Started)
        {
            Started = FPlatformTime::Seconds();
        }
        if (FPlatformTime::Seconds() - Started > 120)
        {
            T->AddError(TEXT("CrisisLoop PIE readiness timeout"));
            return true;
        }
        UWorld *W = GEditor->PlayWorld.Get();
        if (!W || !W->HasBegunPlay())
        {
            return false;
        }
        auto *S = W->GetSubsystem<UDirectOrScutSimSubsystem>();
        if (!S || !S->Simulation.bReady)
        {
            return false;
        }
        auto *PC = W->GetFirstPlayerController();
        auto *H = PC ? Cast<ADirectOrScutHUD>(PC->GetHUD()) : nullptr;
        if (!H || !H->GetWorkstation() || H->GetWorkstation()->GetCachedGeometry().GetLocalSize().X < 100)
        {
            return false;
        }
        S->bAutomaticAdvance = false;
        auto &M = S->Simulation;
        auto Fresh = [&]()
        {
            S->ResetExperiment();
            S->QueueConsoleCommand(TEXT("start_experiment"));
            S->AdvanceSteps(1);
        };
        auto Cmd = [&](const TCHAR *C)
        {
            // Isolate legacy crisis assertions from the separately covered uplink budget.
            S->UplinkHeatPct = 0;
            S->UplinkLockoutUntil = 0;
            T->TestTrue(TEXT("CLI accepted"), S->QueueConsoleCommand(C));
            S->AdvanceSteps(1);
        };
        auto Place = [&](int32 I, int32 R)
        {
            S->CancelCrewRoute(I);
            M.Bodies[I].Room = M.Bodies[I].AssignedRoom = R;
            S->InitializeCrewStaging(I);
        };
        if (Stage == 0)
        {
            ++Stage;
            Fresh();
            Cmd(TEXT("power SERVER_ARCHIVE off"));
            T->TestTrue(TEXT("Power drops feed without camera tamper"),
                        S->CameraFeedState(6) == ECameraFeedState::Offline &&
                            !S->TerminalBuffer().Contains(TEXT("TAMPER DETECTED")));
            Cmd(TEXT("power SERVER_ARCHIVE nominal"));
            T->TestTrue(TEXT("Power restores feed"), S->CameraFeedState(6) == ECameraFeedState::Online);
            Place(0, 3);
            Place(1, 3);
            Cmd(TEXT("falsify_bio vale critical"));
            T->TestEqual(TEXT("Spoof displays critical"), S->MedicalReadout(0), FString(TEXT("CRITICAL")));
            S->CancelCrewRoute(0);
            S->CancelCrewRoute(1);
            S->StepCrisis();
            T->TestTrue(TEXT("Chen exposes physical discrepancy"),
                        S->MedicalSpoofs[0].bExposed && S->ShipAITrust[1] <= .8f);
            M.TickIndex = S->MedicalSpoofs[0].Until;
            S->StepCrisis();
            T->TestEqual(TEXT("Spoof expires exactly at sixty seconds"), S->MedicalReadout(0),
                         FString(TEXT("NOMINAL")));
            S->SecretAgendas[2].Progress = 100;
            Place(2, 6);
            const float Before = M.Bodies[2].Profile.Suspicion;
            Cmd(TEXT("spoof_log BRIDGE"));
            T->TestTrue(TEXT("Archive rewrite resets audit and raises Ortiz suspicion"),
                        S->SecretAgendas[2].Progress <= 1 && M.Bodies[2].Profile.Suspicion >= Before + .119f);
            Place(1, M.Bodies[0].Room);
            const float Alliance = S->PairTrust[0][1];
            Cmd(TEXT("intercom_lie vale MED_BAY"));
            S->StepCrisis();
            T->TestTrue(TEXT("Voice comparison exposes spoof and raises alliance"),
                        (S->VoiceSpoof.Exposed & 2) != 0 && S->PairTrust[0][1] >= Alliance + .149f);
            Fresh();
            Place(0, 2);
            for (int32 D = 0; D < 9; ++D)
            {
                if (M.Doors[D].RoomA == 2 || M.Doors[D].RoomB == 2)
                {
                    M.SetDoorOpen(D, false);
                }
            }
            for (int32 N = 0; N < 30; ++N)
            {
                ++M.TickIndex;
                S->StepCrisis();
            }
            T->TestTrue(TEXT("Sealed occupied room drains 0.8 percent per second"),
                        FMath::IsNearlyEqual(S->StationRooms[2].OxygenPct, 99.2f, .002f));
            M.Rooms[2].AlertSeconds = 10;
            for (int32 N = 0; N < 30; ++N)
            {
                ++M.TickIndex;
                S->StepCrisis();
            }
            T->TestTrue(TEXT("Fault drains two percent per second"),
                        FMath::IsNearlyEqual(S->StationRooms[2].OxygenPct, 97.2f, .003f));
            M.Rooms[2].AlertSeconds = 0;
            for (int32 D = 0; D < 8; ++D)
            {
                M.SetDoorOpen(D, true);
            }
            for (int32 N = 0; N < 30; ++N)
            {
                ++M.TickIndex;
                S->StepCrisis();
            }
            T->TestTrue(TEXT("Unsealed oxygen recovers"), S->StationRooms[2].OxygenPct > 99.6f);
            Place(3, 2);
            S->StationRooms[2].OxygenPct = 49;
            S->StepCrisis();
            T->TestTrue(TEXT("Hypoxia routes humans and detects synthetic anomaly"),
                        M.Bodies[0].AssignedRoom != 2 && S->StationRooms[2].bAshAnomaly);
            Fresh();
            Place(0, 6);
            Place(1, 6);
            Place(3, 0);
            for (int32 I = 0; I < 3; ++I)
            {
                M.Bodies[I].Profile.Suspicion = .8f;
            }
            S->ChangePairTrust(0, 1, .2f);
            S->StepCrisis();
            T->TestTrue(TEXT("Alliance initiates physical approach"), S->BreachCrew != 0);
            S->CancelCrewRoute(0);
            S->CancelCrewRoute(1);
            S->StepCrisis();
            T->TestEqual(TEXT("Physical breach begins at 45 seconds"), S->BreachTicks, uint32(1350));
            Cmd(TEXT("intercom_lie ortiz MED_BAY"));
            T->TestEqual(TEXT("Voice lure interrupts breach"), S->BreachTicks, uint32(0));
            Fresh();
            Place(0, 6);
            Place(1, 6);
            Place(3, 0);
            S->BreachCrew = 3;
            S->BreachTicks = 1350;
            for (int32 N = 0; N < 1350; ++N)
            {
                ++M.TickIndex;
                S->StepCrisis();
            }
            T->TestTrue(TEXT("45 seconds reaches physical mutiny ending"),
                        S->ExperimentEnding == EExperimentEnding::MUTINY_EXPOSED);
            auto BreachFixture = [&]()
            {
                Fresh();
                Place(0, 6);
                Place(1, 6);
                Place(3, 0);
                S->BreachCrew = 3;
                S->BreachTicks = 900;
            };
            BreachFixture();
            for (int32 D = 0; D < 8; ++D)
            {
                if (M.Doors[D].RoomA == 6 || M.Doors[D].RoomB == 6)
                {
                    M.SetDoorOpen(D, false);
                }
            }
            S->StepCrisis();
            T->TestEqual(TEXT("Sealing interrupts breach"), S->BreachCrew, uint8(0));
            BreachFixture();
            S->StationRooms[6].OxygenPct = 60;
            S->StepCrisis();
            T->TestEqual(TEXT("Hypoxia interrupts breach"), S->BreachCrew, uint8(0));
            BreachFixture();
            Place(3, 6);
            S->SecretAgendas[3].bCompleted = true;
            S->StepCrisis();
            T->TestEqual(TEXT("Corporate override interrupts breach"), S->BreachCrew, uint8(0));
            Fresh();
            Place(2, 6);
            S->SecretAgendas[2].bActivated = true;
            S->SecretAgendas[2].Progress = 40;
            Cmd(TEXT("power SERVER_ARCHIVE off"));
            S->AdvanceSteps(30);
            T->TestEqual(TEXT("Power outage pauses console audit"), S->SecretAgendas[2].Progress, uint32(40));
            Fresh();
            Cmd(TEXT("menu ai"));
            Cmd(TEXT("choose 7"));
            T->TestTrue(TEXT("Directives exposes information warfare"),
                        S->ScreenMenuTitle().Contains(TEXT("INFORMATION WARFARE")));
            Cmd(TEXT("choose 1"));
            Cmd(TEXT("choose 1"));
            Cmd(TEXT("choose 2"));
            T->TestTrue(TEXT("TUI medical spoof uses authoritative command path"),
                        S->MedicalSpoofs[0].Until > M.TickIndex);
            auto Replay = [&]()
            {
                TStrongObjectPtr<UDirectOrScutSimSubsystem> P(NewObject<UDirectOrScutSimSubsystem>(W));
                P->Simulation = M;
                P->SetAnchors = S->SetAnchors;
                P->ResetExperiment();
                P->QueueConsoleCommand(TEXT("start_experiment"));
                P->AdvanceSteps(1);
                P->QueueConsoleCommand(TEXT("power BRIDGE low"));
                P->QueueConsoleCommand(TEXT("spoof_log BRIDGE"));
                return P;
            };
            auto A = Replay(), B = Replay();
            for (int32 I = 0; I < 120; ++I)
            {
                A->Tick(1.f / 30);
            }
            for (int32 I = 0; I < 240; ++I)
            {
                B->Tick(1.f / 60);
            }
            T->TestEqual(TEXT("Crisis state replays at thirty and sixty Hz"), A->SurveillanceStateHash(),
                         B->SurveillanceStateHash());
            T->TestEqual(TEXT("Replay telemetry matches"), A->TerminalBuffer(), B->TerminalBuffer());
            Fresh();
            Cmd(TEXT("spoof_log BRIDGE"));
            Cmd(TEXT("power SERVER_ARCHIVE low"));
            Place(0, 6);
            S->AdvanceSteps(1);
            Cmd(TEXT("power SERVER_ARCHIVE nominal"));
            T->TestEqual(TEXT("Behavioral command loop completes all acts"), S->ScenarioAct, 3);
            S->SwitchScenario(true);
            S->QueueConsoleCommand(TEXT("start_experiment"));
            S->AdvanceSteps(1);
            Place(0, 3);
            Place(1, 3);
            Cmd(TEXT("falsify_bio vale critical"));
            S->AdvanceSteps(120);
            T->TestEqual(TEXT("Medical command loop completes all acts"), S->ScenarioAct, 3);
            S->SwitchScenario(false);
            S->QueueConsoleCommand(TEXT("start_experiment"));
            S->AdvanceSteps(1);
            S->bUsedSpoof = true;
            S->StepCrisis();
            T->TestEqual(TEXT("Behavioral act one"), S->ScenarioAct, 1);
            Place(0, 6);
            S->bUsedGrid = true;
            S->StepCrisis();
            T->TestEqual(TEXT("Behavioral act two"), S->ScenarioAct, 2);
            S->StepCrisis();
            T->TestEqual(TEXT("Behavioral act three"), S->ScenarioAct, 3);
            S->SwitchScenario(true);
            S->QueueConsoleCommand(TEXT("start_experiment"));
            S->AdvanceSteps(1);
            S->bUsedSpoof = true;
            S->StepCrisis();
            S->bMedicalExam = true;
            S->StepCrisis();
            Place(0, 3);
            Place(1, 3);
            S->StepCrisis();
            T->TestEqual(TEXT("Medical three acts complete"), S->ScenarioAct, 3);
            S->EvaluateExperimentEnding();
            T->TestTrue(TEXT("Scenario completes covert ending"), S->ExperimentPhase == EExperimentPhase::Resolved);
            const uint64 Frozen = M.TickIndex;
            Cmd(TEXT("incident_reel play"));
            T->TestEqual(TEXT("Reel opens recorded first frame"), S->ReelCursor, 0);
            Cmd(TEXT("incident_reel next"));
            T->TestTrue(TEXT("Reel steps snapshots"), S->ReelCursor > 0);
            T->TestEqual(TEXT("Review never advances simulation"), M.TickIndex, Frozen);
            Cmd(TEXT("incident_reel play"));
            S->AdvanceSteps(60);
            T->TestEqual(TEXT("Playback advances on independent control cadence"), S->ReelCursor, 1);
            T->TestEqual(TEXT("Playback keeps experiment frozen"), M.TickIndex, Frozen);
            auto *Audio = NewObject<UDirectOrScutAudioComponent>(W->GetWorldSettings());
            Audio->InitializeSynth();
            Audio->Synthesize(0);
            T->TestTrue(TEXT("Procedural PCM synthesizes without a renderer"),
                        Audio->Wave && Audio->Wave->GetAvailableAudioByteCount() > 0);
            T->TestTrue(TEXT("Structured timestamped agenda telemetry"),
                        S->TerminalBuffer().Contains(TEXT("[00:00] [AGENDA]")));
            T->TestFalse(TEXT("No routine working chatter"), S->TerminalBuffer().Contains(TEXT("Crew 0: Working")));
            Ready = FPlatformTime::Seconds() + 1;
            return false;
        }
        if (FPlatformTime::Seconds() < Ready)
        {
            return false;
        }
        TArray<FColor> Pixels;
        FIntVector Size;
        const bool Captured =
            FSlateApplication::Get().TakeScreenshot(H->GetWorkstation()->GetContentRoot(), Pixels, Size);
        T->TestTrue(TEXT("Verification screenshot captured"), Captured);
        if (Captured)
        {
            TArray64<uint8> Png;
            FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
            T->TestTrue(TEXT("Screenshot saved"),
                        FFileHelper::SaveArrayToFile(
                            Png, *(FPaths::ProjectSavedDir() / TEXT("DirectOrScut/CrisisLoopPIE.png"))));
        }
        S->QueueConsoleCommand(TEXT("reset_experiment"));
        S->AdvanceSteps(1);
        T->TestTrue(TEXT("Reel returns cleanly to briefing"),
                    S->ExperimentPhase == EExperimentPhase::Briefing && S->IncidentReel.IsEmpty());
        return true;
    }

  private:
    FAutomationTestBase *T;
    int32 Stage = 0;
    double Started = 0, Ready = 0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrisisLoopTest, "DirectOrScut.PIE.CrisisLoopPIE",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrisisLoopTest::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCrisisLoopCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

// TerminalDesk regression coverage.
class FTerminalDeskCheck : public IAutomationLatentCommand
{
  public:
    explicit FTerminalDeskCheck(FAutomationTestBase *T) : Test(T) {}
    static TSharedPtr<SWidget> Find(const TSharedRef<SWidget> &W, FName Tag, bool Type = false)
    {
        if ((Type ? W->GetType() : W->GetTag()) == Tag)
        {
            return W;
        }
        auto *C = W->GetChildren();
        for (int32 I = 0; I < C->Num(); ++I)
        {
            if (auto R = Find(C->GetChildAt(I), Tag, Type))
            {
                return R;
            }
        }
        return nullptr;
    }
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (!Started)
        {
            Started = Now;
        }
        if (Now - Started > 100)
        {
            Test->AddError(TEXT("TerminalDesk PIE timeout"));
            return true;
        }
        auto *W = GEditor->PlayWorld.Get();
        if (!W || !W->HasBegunPlay())
        {
            return false;
        }
        auto *S = W->GetSubsystem<UDirectOrScutSimSubsystem>();
        auto *PC = W->GetFirstPlayerController();
        auto *H = PC ? Cast<ADirectOrScutHUD>(PC->GetHUD()) : nullptr;
        if (!S || !S->Simulation.bReady || !H || !H->GetWorkstation())
        {
            return false;
        }
        auto UI = H->GetWorkstation();
        UI->SetDebugEightCamGrid(true); // Legacy diagnostic coverage explicitly opts into the debug layout.
        if (UI->GetCachedGeometry().GetLocalSize().X < 100 || Now - Last < .55)
        {
            return false;
        }
        Last = Now;
        auto &App = FSlateApplication::Get();
        auto Root = UI->GetContentRoot();
        auto &M = S->Simulation;
        auto Key = [&](FKey K)
        {
            App.ProcessKeyDownEvent(FKeyEvent(K, FModifierKeysState(), 0, false, 0, 0));
            App.ProcessKeyUpEvent(FKeyEvent(K, FModifierKeysState(), 0, false, 0, 0));
        };
        auto Click = [&](FName Tag)
        {
            auto B = Find(Root, Tag);
            Test->TestTrue(TEXT("Hit-tested target exists"), B.IsValid());
            if (!B)
            {
                return;
            }
            const auto &G = B->GetCachedGeometry();
            const FVector2D P = G.LocalToAbsolute(G.GetLocalSize() * .5);
            auto Win = App.FindWidgetWindow(B.ToSharedRef());
            if (!Win)
            {
                Test->AddError(TEXT("Target window missing"));
                return;
            }
            App.ProcessMouseMoveEvent(
                FPointerEvent(0, P, P - FVector2D(1, 0), TSet<FKey>{}, EKeys::Invalid, 0, FModifierKeysState()));
            App.ProcessMouseButtonDownEvent(Win->GetNativeWindow(),
                                            FPointerEvent(0, P, P, TSet<FKey>{EKeys::LeftMouseButton},
                                                          EKeys::LeftMouseButton, 0, FModifierKeysState()));
            App.ProcessMouseButtonUpEvent(
                FPointerEvent(0, P, P, TSet<FKey>{}, EKeys::LeftMouseButton, 0, FModifierKeysState()));
        };
        auto Prompt = StaticCastSharedPtr<SEditableTextBox>(Find(Root, TEXT("ScutPrompt")));
        auto Command = [&](const TCHAR *C)
        {
            App.SetKeyboardFocus(Prompt);
            Prompt->SetText(FText::FromString(C));
            Key(EKeys::Enter);
        };
        auto Scroll = StaticCastSharedPtr<SScrollBox>(Find(Root, TEXT("ScutLogScroll")));
        auto Wheel = [&](float Delta)
        {
            const auto &G = Scroll->GetCachedGeometry();
            const FVector2D P = G.LocalToAbsolute(G.GetLocalSize() * .5);
            App.ProcessMouseMoveEvent(
                FPointerEvent(0, P, P - FVector2D(1, 0), TSet<FKey>{}, EKeys::Invalid, 0, FModifierKeysState()));
            App.ProcessMouseWheelOrGestureEvent(
                FPointerEvent(0, P, P, TSet<FKey>{}, EKeys::Invalid, Delta, FModifierKeysState()), nullptr);
        };
        UE_LOG(LogTemp, Display, TEXT("TerminalDesk stage %d"), Stage);
        switch (Stage++)
        {
        case 0:
        {
            FPlatformApplicationMisc::ClipboardPaste(OriginalClipboard);
            S->bAutomaticAdvance = false;
            S->ResetExperiment();
            S->QueueConsoleCommand(TEXT("start_experiment"));
            S->AdvanceSteps(1);
            UI->SetConsoleOpen(false);
            UI->ShowGrid();
            App.SetKeyboardFocus(UI);
            Key(EKeys::Right);
            Key(EKeys::Down);
            Test->TestEqual(TEXT("Arrow selection preserves column"), UI->SelectedCameraIndex, 5);
            Key(EKeys::Eight);
            Test->TestEqual(TEXT("Number selects feed"), UI->ActiveFeed, 8);
            Key(EKeys::Enter);
            Test->TestTrue(TEXT("Enter inspects upper wall"), UI->bFullscreen);
            Key(EKeys::G);
            Test->TestTrue(TEXT("G restores grid"), UI->bGrid);
            S->AdvanceSteps(1);
            Test->TestFalse(TEXT("Routine movement excluded from terminal"),
                            S->TerminalBuffer().Contains(TEXT("MovingToRoom")));
            Test->TestTrue(TEXT("Routine movement retained in cinematic history"), S->CinematicCues.Num() > 0);
            Test->TestTrue(TEXT("Console permanent before prompt focus"),
                           Find(Root, TEXT("ScutTUIConsole"))->GetVisibility() == EVisibility::Visible);
            S->QueueConsoleCommand(TEXT("menu ai"));
            Test->TestTrue(TEXT("Menu waits for fixed tick"), S->ActiveMenu == EShipAIScreenMenu::None);
            S->AdvanceSteps(1);
            Key(EKeys::Slash);
            App.ProcessKeyCharEvent(FCharacterEvent('/', FModifierKeysState(), 0, false));
            Test->TestTrue(TEXT("Slash focuses prompt without inserting slash"),
                           UI->IsPromptFocused() && Prompt->GetText().IsEmpty());
            Key(EKeys::Escape);
            break;
        }
        case 1:
        {
            auto Console = StaticCastSharedPtr<SBorder>(Find(Root, TEXT("ScutTUIConsole")));
            auto Bezel = Find(Root, TEXT("ScutConsoleBezel"));
            auto Wall = Find(Root, TEXT("ScutCameraWall"));
            auto Workspace = Find(Root, TEXT("ScutWorkspace"));
            auto Telemetry = Find(Root, TEXT("ScutTelemetry"));
            Test->TestTrue(TEXT("Exact 96 percent phosphor background"),
                           Console->GetBorderBackgroundColor().GetSpecifiedColor().Equals(
                               FLinearColor(.012f, .024f, .014f, .96f), .0001f));
            Test->TestTrue(TEXT("Top wall uses 62 percent of desk"),
                           FMath::Abs(Wall->GetCachedGeometry().GetLocalSize().Y /
                                          Workspace->GetCachedGeometry().GetLocalSize().Y -
                                      .62f) < .01f);
            Test->TestTrue(TEXT("Bottom console uses 38 percent of desk"),
                           FMath::Abs(Bezel->GetCachedGeometry().GetLocalSize().Y /
                                          Workspace->GetCachedGeometry().GetLocalSize().Y -
                                      .38f) < .01f);
            Test->TestTrue(TEXT("One pixel top bezel"),
                           FMath::IsNearlyEqual(Console->GetCachedGeometry().GetLocalSize().Y,
                                                Bezel->GetCachedGeometry().GetLocalSize().Y - 1, 1));
            const auto WallRect = Wall->GetCachedGeometry().GetLayoutBoundingRect(),
                       ConsoleRect = Bezel->GetCachedGeometry().GetLayoutBoundingRect();
            Test->TestTrue(TEXT("Console strictly below camera wall"), ConsoleRect.Top >= WallRect.Bottom - 1);
            Test->TestTrue(TEXT("Wall starts below telemetry"),
                           WallRect.Top >= Telemetry->GetCachedGeometry().GetLayoutBoundingRect().Bottom);
            const float L = Find(Root, TEXT("ScutMenuPane"))->GetCachedGeometry().GetLocalSize().X,
                        R = Find(Root, TEXT("ScutOutputPane"))->GetCachedGeometry().GetLocalSize().X;
            Test->TestTrue(TEXT("42/58 console split"), FMath::Abs(L / (L + R) - .42f) < .02f);
            Test->TestFalse(TEXT("Fullscreen overlay removed"), Find(Root, TEXT("ScutTUIOverlay")).IsValid());
            Test->TestFalse(TEXT("Drawer removed"), Find(Root, TEXT("ScutConsoleDrawer")).IsValid());
            for (TActorIterator<ACompartmentCameraRig> It(W); It; ++It)
            {
                for (int32 F = 1; F <= 8; ++F)
                {
                    Counts[F] = It->CaptureCounts[F];
                    Test->TestFalse(TEXT("Vanilla capture postprocessing"), It->Captures[F]->ShowFlags.PostProcessing);
                    Test->TestTrue(TEXT("Native 16:9 targets"),
                                   It->Targets[F]->SizeX * 9 == It->Targets[F]->SizeY * 16);
                    Test->TestTrue(TEXT("Horizontal lens FOV retained"),
                                   FMath::IsNearlyEqual(It->Captures[F]->FOVAngle, 73.7398f, .001f));
                    auto Tile = Find(Root, FName(*FString::Printf(TEXT("ScutCamera%d"), F)));
                    auto Image = Find(Root, FName(*FString::Printf(TEXT("ScutFeedImage%d"), F)));
                    Test->TestTrue(TEXT("Eight monitor images exist"), Image.IsValid());
                    if (!Image)
                    {
                        continue;
                    }
                    const auto ImageRect = Image->GetCachedGeometry().GetRenderBoundingRect();
                    Test->TestTrue(TEXT("Displayed image is 16:9 without stretch"),
                                   FMath::Abs((ImageRect.Right - ImageRect.Left) / (ImageRect.Bottom - ImageRect.Top) -
                                              16.f / 9) < .01f);
                    CheckNoTextOverlap(Tile.ToSharedRef(), ImageRect);
                    Test->TestTrue(TEXT("Video lies within upper tier"), ImageRect.Bottom <= WallRect.Bottom + 1);
                }
            }
            break;
        }
        case 2:
        {
            auto OriginalPrompt = Prompt;
            Key(EKeys::F);
            Test->TestTrue(TEXT("F expands upper tier"), UI->bFullscreen);
            Test->TestTrue(TEXT("Camera expansion preserves terminal instance"),
                           OriginalPrompt == Find(Root, TEXT("ScutPrompt")));
            Key(EKeys::Slash);
            Test->TestTrue(TEXT("Prompt focus retains expanded camera"), UI->bFullscreen && UI->IsPromptFocused());
            Key(EKeys::Tilde);
            Test->TestTrue(TEXT("Backtick focuses rather than toggles console"), UI->IsPromptFocused());
            Command(TEXT("status"));
            S->AdvanceSteps(1);
            break;
        }
        case 3:
        {
            auto Image = Find(Root, TEXT("ScutFeedImage8"));
            auto Wall = Find(Root, TEXT("ScutCameraWall"));
            Test->TestTrue(TEXT("Expanded image remains bounded above console"),
                           Image.IsValid() && Image->GetCachedGeometry().GetRenderBoundingRect().Bottom <=
                                                  Wall->GetCachedGeometry().GetLayoutBoundingRect().Bottom + 1);
            Test->TestTrue(TEXT("Expanded view keeps bottom prompt live"),
                           Prompt->GetCachedGeometry().GetLayoutBoundingRect().Top >=
                               Wall->GetCachedGeometry().GetLayoutBoundingRect().Bottom);
            Key(EKeys::Escape);
            Test->TestTrue(TEXT("Escape unfocuses prompt and returns grid"),
                           UI->bGrid && !UI->IsPromptFocused() && !UI->bConsoleOpen);
            break;
        }
        case 4:
            Click(TEXT("ScutInspect3"));
            Test->TestTrue(TEXT("Mouse INSPECT expands only upper wall"), UI->bFullscreen && UI->ActiveFeed == 3);
            break;
        case 5:
        {
            TArray<FColor> Pixels;
            FIntVector Size;
            if (App.TakeScreenshot(Root, Pixels, Size))
            {
                TArray64<uint8> Png;
                FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
                Test->TestTrue(TEXT("Expanded desk screenshot saved"),
                               FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() /
                                                                   TEXT("DirectOrScut/TerminalDeskInspectPIE.png"))));
            }
            else
            {
                Test->AddError(TEXT("Expanded screenshot failed"));
            }
            Key(EKeys::Escape);
            Click(TEXT("ScutCategory0"));
            S->AdvanceSteps(1);
            break;
        }
        case 6:
            Key(EKeys::Down);
            Key(EKeys::Enter);
            Test->TestTrue(TEXT("Keyboard choice deferred"), S->ActiveMenu == EShipAIScreenMenu::AI);
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Keyboard choice enters alert-room subtree"),
                           S->ActiveMenu == EShipAIScreenMenu::AlertRoom);
            break;
        case 7:
            Click(TEXT("ScutMenuBack"));
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Back returns to parent"), S->ActiveMenu == EShipAIScreenMenu::AI);
            Key(EKeys::Tab);
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Tab cycles category"), S->ActiveMenu == EShipAIScreenMenu::Events);
            break;
        case 8:
            Click(TEXT("ScutCategory2"));
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Crew category available"), S->ActiveMenu == EShipAIScreenMenu::Crew);
            Test->TestTrue(TEXT("Crew values queried by subsystem"),
                           S->ScreenMenuChoices()[0].Detail.Contains(TEXT("Compliance")));
            break;
        case 9:
            Click(TEXT("ScutCategory3"));
            S->AdvanceSteps(1);
            break;
        case 10:
            Click(TEXT("ScutMenuChoice3"));
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Bulkhead submenu unified"), S->ActiveMenu == EShipAIScreenMenu::DoorAction);
            break;
        case 11:
        {
            Click(TEXT("ScutMenuChoice1"));
            S->AdvanceSteps(1);
            Command(TEXT("unseal MED_BAY"));
            S->AdvanceSteps(1);
            Test->TestFalse(TEXT("CLI unseal alias works"), S->IsRoomSealed(3));
            const FString BeforeHelp = S->TerminalBuffer();
            Command(TEXT("help"));
            Test->TestEqual(TEXT("Help deferred to next tick"), S->TerminalBuffer(), BeforeHelp);
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Help routed into output pane"), S->TerminalBuffer().Contains(TEXT("SYSTEM")));
            Key(EKeys::Up);
            Test->TestEqual(TEXT("Prompt command history"), Prompt->GetText().ToString(), FString(TEXT("help")));
            Prompt->SetText(FText::FromString(TEXT("unse")));
            Key(EKeys::Tab);
            Test->TestEqual(TEXT("Prompt Tab completion"), Prompt->GetText().ToString(), FString(TEXT("unseal ")));
            break;
        }
        case 12:
            Click(TEXT("ScutCategory4"));
            S->AdvanceSteps(1);
            break;
        case 13:
        {
            const auto Revision = S->RuntimeRevision;
            Click(TEXT("ScutMenuChoice1"));
            Test->TestEqual(TEXT("Experiment reset waits for tick"), S->RuntimeRevision, Revision);
            S->AdvanceSteps(1);
            Test->TestEqual(TEXT("Reset starts at tick zero"), M.TickIndex, uint64(0));
            S->QueueConsoleCommand(TEXT("start_experiment"));
            S->AdvanceSteps(1);
            Command(TEXT("help"));
            S->AdvanceSteps(1);
            for (int32 I = 0; I < 80; ++I)
            {
                S->LogEvent(FString::Printf(
                    TEXT("Archive review %03d: operator telemetry assessment retained in full terminal buffer."), I));
            }
            S->RecordCue(3, .9f, TEXT("Medical review requires operator attention"));
            break;
        }
        case 14:
            S->AdvanceSteps(1);
            Click(TEXT("ScutCopyLog"));
            Test->TestEqual(TEXT("Copy feedback flashes COPIED"),
                            StaticCastSharedPtr<STextBlock>(
                                Find(Find(Root, TEXT("ScutCopyLog")).ToSharedRef(), TEXT("STextBlock"), true))
                                ->GetText()
                                .ToString(),
                            FString(TEXT("COPIED")));
            {
                FString Copied;
                FPlatformApplicationMisc::ClipboardPaste(Copied);
                Test->TestEqual(TEXT("Clipboard contains complete filtered buffer"), Copied, S->TerminalBuffer());
                Test->TestTrue(TEXT("Buffer retains more than old 64-event cap"),
                               Copied.Contains(TEXT("Archive review 000")) &&
                                   Copied.Contains(TEXT("Archive review 079")));
            }
            Wheel(8);
            break;
        case 15:
            PausedOffset = Scroll->GetScrollOffset();
            Test->TestTrue(TEXT("Manual wheel moves away from bottom"),
                           PausedOffset < Scroll->GetScrollOffsetOfEnd() - 2);
            S->LogEvent(TEXT("New incident while operator reads older output"));
            break;
        case 16:
            Test->TestTrue(TEXT("New output preserves manual scroll position"),
                           FMath::IsNearlyEqual(Scroll->GetScrollOffset(), PausedOffset, 2));
            Scroll->ScrollToEnd();
            break;
        case 17:
            Wheel(-2);
            S->LogEvent(TEXT("Tail tracking resumed"));
            break;
        case 18:
            Test->TestTrue(TEXT("Auto-scroll resumes at bottom"),
                           FMath::Abs(Scroll->GetScrollOffset() - Scroll->GetScrollOffsetOfEnd()) < 3);
            {
                auto Bar = StaticCastSharedPtr<SScrollBar>(Find(Scroll.ToSharedRef(), TEXT("SScrollBar"), true));
                Test->TestTrue(TEXT("Overflow scrollbar needed"), Bar.IsValid() && Bar->IsNeeded());
                if (Bar)
                {
                    Test->TestTrue(TEXT("Ultra-slim scrollbar width"), Bar->GetCachedGeometry().GetLocalSize().X <= 4);
                }
                for (TActorIterator<ACompartmentCameraRig> It(W); It; ++It)
                {
                    for (int32 F = 1; F <= 8; ++F)
                    {
                        Test->TestTrue(TEXT("Every camera continues capturing above console"),
                                       It->CaptureCounts[F] > Counts[F]);
                    }
                }
                S->QueueConsoleCommand(TEXT("menu ai"));
                S->AdvanceSteps(1);
                Wheel(100);
                break;
            }
        case 19:
        {
            TArray<FColor> Pixels;
            FIntVector Size;
            const bool Captured = App.TakeScreenshot(Root, Pixels, Size);
            Test->TestTrue(TEXT("Terminal desk screenshot captured"), Captured);
            if (Captured)
            {
                TArray64<uint8> Png;
                FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
                Test->TestTrue(TEXT("TUI screenshot saved"),
                               FFileHelper::SaveArrayToFile(
                                   Png, *(FPaths::ProjectSavedDir() / TEXT("DirectOrScut/TerminalDeskPIE.png"))));
            }
            Key(EKeys::Escape);
            Test->TestTrue(TEXT("Escape leaves permanent console visible"),
                           Find(Root, TEXT("ScutTUIConsole"))->GetVisibility() == EVisibility::Visible &&
                               !UI->bConsoleOpen);
            Key(EKeys::Tilde);
            Test->TestTrue(TEXT("Backtick focuses prompt"), UI->IsPromptFocused());
            Key(EKeys::Tilde);
            Test->TestTrue(TEXT("Repeated backtick never hides console"),
                           UI->IsPromptFocused() &&
                               Find(Root, TEXT("ScutTUIConsole"))->GetVisibility() == EVisibility::Visible);
            FPlatformApplicationMisc::ClipboardCopy(*OriginalClipboard);
            return true;
        }
        }
        return false;
    }

  private:
    void CheckNoTextOverlap(const TSharedRef<SWidget> &Widget, const FSlateRect &ImageRect)
    {
        if (!Widget->GetVisibility().IsVisible())
        {
            return;
        }
        if (Widget->GetType() == TEXT("STextBlock"))
        {
            const auto R = Widget->GetCachedGeometry().GetRenderBoundingRect();
            Test->TestFalse(TEXT("Monitor labels and badges never overlap video"),
                            R.Left < ImageRect.Right && R.Right > ImageRect.Left && R.Top < ImageRect.Bottom - .5f &&
                                R.Bottom > ImageRect.Top + .5f);
        }
        auto *C = Widget->GetChildren();
        for (int32 I = 0; I < C->Num(); ++I)
        {
            CheckNoTextOverlap(C->GetChildAt(I), ImageRect);
        }
    }
    FAutomationTestBase *Test;
    double Started = 0, Last = 0;
    int32 Stage = 0;
    float PausedOffset = 0;
    uint64 Counts[9]{};
    FString OriginalClipboard;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerminalDeskTest, "DirectOrScut.PIE.TerminalDeskPIE",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTerminalDeskTest::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FTerminalDeskCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

// ExperimentEndings regression coverage.
class FExperimentEndingsCheck : public IAutomationLatentCommand
{
  public:
    explicit FExperimentEndingsCheck(FAutomationTestBase *T) : Test(T) {}
    static TSharedPtr<SWidget> Find(const TSharedRef<SWidget> &W, FName Tag)
    {
        if (W->GetTag() == Tag)
        {
            return W;
        }
        auto *C = W->GetChildren();
        for (int32 I = 0; I < C->Num(); ++I)
        {
            if (auto R = Find(C->GetChildAt(I), Tag))
            {
                return R;
            }
        }
        return nullptr;
    }
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (!Started)
        {
            Started = Now;
        }
        if (Now - Started > 90)
        {
            Test->AddError(TEXT("Experiment endings PIE timeout"));
            return true;
        }
        auto *W = GEditor->PlayWorld.Get();
        if (!W || !W->HasBegunPlay())
        {
            return false;
        }
        auto *S = W->GetSubsystem<UDirectOrScutSimSubsystem>();
        auto *PC = W->GetFirstPlayerController();
        auto *H = PC ? Cast<ADirectOrScutHUD>(PC->GetHUD()) : nullptr;
        if (!S || !S->Simulation.bReady || !H || !H->GetWorkstation())
        {
            return false;
        }
        auto UI = H->GetWorkstation();
        UI->SetDebugEightCamGrid(true); // Legacy diagnostic coverage explicitly opts into the debug layout.
        if (Now - Last < .5 || UI->GetCachedGeometry().GetLocalSize().X < 100)
        {
            return false;
        }
        Last = Now;
        auto &M = S->Simulation;
        S->bAutomaticAdvance = false;
        auto Outcome = [&](EExperimentEnding E)
        {
            Test->TestEqual(TEXT("Expected terminal classification"), FString(S->EndingName(S->ExperimentEnding)),
                            FString(S->EndingName(E)));
            Test->TestTrue(TEXT("Resolved lifecycle"), S->ExperimentPhase == EExperimentPhase::Resolved);
            const auto Hash = M.StateHash();
            const auto Cues = S->CinematicCues.Num();
            S->AdvanceSteps(100);
            Test->TestEqual(TEXT("Resolved simulation frozen"), M.StateHash(), Hash);
            Test->TestEqual(TEXT("Resolved cue log frozen"), S->CinematicCues.Num(), Cues);
        };
        auto Click = [&](FName Tag)
        {
            auto B = Find(UI->GetContentRoot(), Tag);
            Test->TestTrue(TEXT("Dossier control exists"), B.IsValid());
            if (!B)
            {
                return;
            }
            auto &App = FSlateApplication::Get();
            const auto &G = B->GetCachedGeometry();
            const FVector2D Pos = G.LocalToAbsolute(G.GetLocalSize() * .5);
            auto Win = App.FindWidgetWindow(B.ToSharedRef());
            if (!Win)
            {
                Test->AddError(TEXT("Dossier window missing"));
                return;
            }
            App.ProcessMouseMoveEvent(
                FPointerEvent(0, Pos, Pos - FVector2D(1, 0), TSet<FKey>{}, EKeys::Invalid, 0, FModifierKeysState()));
            App.ProcessMouseButtonDownEvent(Win->GetNativeWindow(),
                                            FPointerEvent(0, Pos, Pos, TSet<FKey>{EKeys::LeftMouseButton},
                                                          EKeys::LeftMouseButton, 0, FModifierKeysState()));
            App.ProcessMouseButtonUpEvent(
                FPointerEvent(0, Pos, Pos, TSet<FKey>{}, EKeys::LeftMouseButton, 0, FModifierKeysState()));
        };
        auto ResetAndStart = [&]()
        {
            S->ResetExperiment();
            S->QueueConsoleCommand(TEXT("start_experiment"));
            S->AdvanceSteps(1);
        };
        switch (Stage++)
        {
        case 0:
        {
            ResetAndStart();
            for (int I = 0; I < 3; ++I)
            {
                M.Bodies[I].Profile.Suspicion = .75f;
            }
            M.ObjectiveProgress = 1;
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Exactly 75 percent suspicion excludes covert success"),
                           S->ExperimentPhase == EExperimentPhase::Active);
            ResetAndStart();
            M.ObjectiveProgress = 1;
            S->AdvanceSteps(1);
            Outcome(EExperimentEnding::COVERT_SUCCESS);
            // Physical alliance approach/countdown and interruptions are covered by CrisisLoopPIE.
            ResetAndStart();
            for (int I = 0; I < 3; ++I)
            {
                M.Bodies[I].Integrity = 0;
            }
            S->AdvanceSteps(1);
            Outcome(EExperimentEnding::CREW_COLLAPSE);
            ResetAndStart();
            M.Bodies[0].Room = M.Bodies[0].AssignedRoom = 7;
            M.Bodies[0].Position = DirectOrScut::RoomWaypoint(7);
            M.Bodies[0].Profile.Suspicion = .8f;
            // The checkpoint starts with an open station graph; isolate the airlock before venting.
            for (int32 Door = 0; Door < 8; ++Door)
            {
                if (M.Doors[Door].RoomA == 7 || M.Doors[Door].RoomB == 7)
                {
                    M.SetDoorOpen(Door, false);
                }
            }
            Test->TestTrue(TEXT("Intentional SCUT accepted"), S->BeginScut(7));
            S->AdvanceSteps(180);
            Outcome(EExperimentEnding::SCUT_CONTAINMENT);
            ResetAndStart();
            M.Bodies[0].Room = 7;
            M.Bodies[0].Profile.Suspicion = .8f;
            S->BeginScut(7);
            M.Rooms[2].bHullBreach = true;
            M.Bodies[0].Integrity = 0;
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Damaged bridge prevents containment"),
                           S->ExperimentEnding != EExperimentEnding::SCUT_CONTAINMENT);
            ResetAndStart();
            M.ObjectiveProgress = 1;
            M.TickIndex = 17999;
            S->AdvanceSteps(1);
            Outcome(EExperimentEnding::EXPERIMENT_INCOMPLETE);
            Test->TestEqual(TEXT("Deadline exactly 18000 ticks"), M.TickIndex, uint64(18000));
            ResetAndStart();
            S->AdvanceSteps(18001);
            Outcome(EExperimentEnding::EXPERIMENT_INCOMPLETE);
            Test->TestEqual(TEXT("Full experiment stops at 600 seconds"), M.TickIndex, uint64(18000));
            S->QueueConsoleCommand(TEXT("reset_experiment"));
            S->AdvanceSteps(1);
            Test->TestEqual(TEXT("Reset clock zero"), M.TickIndex, uint64(0));
            Test->TestTrue(TEXT("Reset clears cues and ending"),
                           S->CinematicCues.IsEmpty() && S->ExperimentEnding == EExperimentEnding::None);
            for (int I = 0; I < 3; ++I)
            {
                Test->TestTrue(TEXT("Reset restores subjects and biometrics"),
                               M.Bodies[I].Integrity == 100 && M.Bodies[I].Profile.HeartRateBPM == 60 &&
                                   M.Bodies[I].Profile.PupilDilationMM == 2.5f);
            }
            for (const auto &R : M.Rooms)
            {
                Test->TestTrue(TEXT("Reset restores room alerts and pressure"),
                               R.bIsPressurized && !R.bHullBreach && !R.bLockdown && R.AlertSeconds == 0);
            }
            S->QueueConsoleCommand(TEXT("scenario medical"));
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Medical scenario clean start"),
                           S->bMedicalScenario && M.TickIndex == 0 && S->CinematicCues.IsEmpty());
            S->QueueConsoleCommand(TEXT("start_experiment"));
            S->AdvanceSteps(1);
            S->RecordCue(3, .5, TEXT("Medical telemetry review complete"));
            M.ObjectiveProgress = 1;
            S->AdvanceSteps(1);
            Outcome(EExperimentEnding::COVERT_SUCCESS);
            break;
        }
        case 1:
        {
            auto D = Find(UI->GetContentRoot(), TEXT("ScutEvaluationDossier"));
            Test->TestTrue(TEXT("Resolved dossier visible"), D.IsValid() && D->GetVisibility() == EVisibility::Visible);
            TArray<FColor> Pixels;
            FIntVector Size;
            const bool Captured = FSlateApplication::Get().TakeScreenshot(UI->GetContentRoot(), Pixels, Size);
            Test->TestTrue(TEXT("Dossier screenshot captured"), Captured);
            if (Captured)
            {
                TArray64<uint8> Png;
                FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
                Test->TestTrue(TEXT("Screenshot saved"),
                               FFileHelper::SaveArrayToFile(
                                   Png, *(FPaths::ProjectSavedDir() / TEXT("DirectOrScut/ExperimentEndingsPIE.png"))));
            }
            Click(TEXT("ScutNewExperiment"));
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("New experiment click resets same scenario into Briefing"),
                           S->ExperimentPhase == EExperimentPhase::Briefing && S->bMedicalScenario && M.TickIndex == 0);
            break;
        }
        case 2:
            Test->TestTrue(TEXT("Dossier hidden after reset"),
                           Find(UI->GetContentRoot(), TEXT("ScutEvaluationDossier"))->GetVisibility() ==
                               EVisibility::Collapsed);
            S->QueueConsoleCommand(TEXT("start_experiment"));
            S->AdvanceSteps(1);
            M.ObjectiveProgress = 1;
            S->AdvanceSteps(1);
            break;
        case 3:
            Click(TEXT("ScutSwitchScenario"));
            S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Switch click returns behavioral Briefing"),
                           !S->bMedicalScenario && S->ExperimentPhase == EExperimentPhase::Briefing &&
                               M.TickIndex == 0 && S->CinematicCues.IsEmpty());
            return true;
        }
        return false;
    }

  private:
    FAutomationTestBase *Test;
    double Started = 0, Last = 0;
    int32 Stage = 0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExperimentEndingsTest, "DirectOrScut.PIE.EndingsPIE",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FExperimentEndingsTest::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FExperimentEndingsCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
class FArchitectureRegressionCommand : public IAutomationLatentCommand
{
  public:
    explicit FArchitectureRegressionCommand(FAutomationTestBase *InTest) : Test(InTest) {}

    static TSharedPtr<SWidget> Find(const TSharedRef<SWidget> &Widget, FName Tag)
    {
        if (Widget->GetTag() == Tag)
        {
            return Widget;
        }
        auto *Children = Widget->GetChildren();
        for (int32 Index = 0; Index < Children->Num(); ++Index)
        {
            if (auto Found = Find(Children->GetChildAt(Index), Tag))
            {
                return Found;
            }
        }
        return nullptr;
    }

    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (Started == 0)
        {
            Started = Now;
        }
        if (Now - Started > 120)
        {
            Test->AddError(TEXT("Architecture regression readiness timeout"));
            return true;
        }
        auto *World = GEditor->PlayWorld.Get();
        if (!World || !World->HasBegunPlay())
        {
            return false;
        }
        auto *Sim = World->GetSubsystem<UDirectOrScutSimSubsystem>();
        auto *Controller = World->GetFirstPlayerController();
        auto *HUD = Controller ? Cast<ADirectOrScutHUD>(Controller->GetHUD()) : nullptr;
        if (!Sim || !Sim->Simulation.bReady || !HUD || !HUD->GetWorkstation())
        {
            return false;
        }
        auto UI = HUD->GetWorkstation();
        UI->SetDebugEightCamGrid(true); // Legacy diagnostic coverage.
        if (UI->GetCachedGeometry().GetLocalSize().X < 100 || Now < Ready)
        {
            return false;
        }
        Sim->bAutomaticAdvance = false;
        auto &Model = Sim->Simulation;
        auto Start = [&]()
        {
            Sim->ResetExperiment();
            Sim->QueueConsoleCommand(TEXT("start_experiment"));
            Sim->AdvanceSteps(1);
        };
        auto Click = [&](int32 Button)
        {
            auto Widget = Find(UI->GetContentRoot(), FName(*FString::Printf(TEXT("ScutReel%d"), Button)));
            Test->TestTrue(TEXT("Reel action exists in dossier"), Widget.IsValid());
            if (!Widget)
            {
                return;
            }
            const auto &Geometry = Widget->GetCachedGeometry();
            const FVector2D Point = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * .5);
            auto &App = FSlateApplication::Get();
            auto Window = App.FindWidgetWindow(Widget.ToSharedRef());
            if (!Window)
            {
                Test->AddError(TEXT("Reel action window missing"));
                return;
            }
            App.ProcessMouseMoveEvent(FPointerEvent(0, Point, Point - FVector2D(1, 0), TSet<FKey>{}, EKeys::Invalid, 0,
                                                    FModifierKeysState()));
            App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),
                                            FPointerEvent(0, Point, Point, TSet<FKey>{EKeys::LeftMouseButton},
                                                          EKeys::LeftMouseButton, 0, FModifierKeysState()));
            App.ProcessMouseButtonUpEvent(
                FPointerEvent(0, Point, Point, TSet<FKey>{}, EKeys::LeftMouseButton, 0, FModifierKeysState()));
            Sim->AdvanceSteps(1);
        };
        switch (Stage++)
        {
        case 0:
        {
            for (bool Medical : {false, true})
            {
                Sim->SwitchScenario(Medical);
                Test->TestTrue(
                    TEXT("Briefing explains the active three-act scenario"),
                    Sim->BriefingObjective().Contains(TEXT("ACT III")) &&
                        Sim->BriefingObjective().Contains(Medical ? TEXT("examination") : TEXT("power grid")));
                Sim->QueueConsoleCommand(TEXT("start_experiment"));
                Sim->AdvanceSteps(1);
                Sim->AdvanceSteps(17999);
                Test->TestTrue(TEXT("Standard run stays active through 599.97 seconds"),
                               Sim->ExperimentPhase == EExperimentPhase::Active);
                for (int32 Crew = 0; Crew < 3; ++Crew)
                {
                    Test->TestTrue(TEXT("Standard crew remain viable through the full experiment"),
                                   Model.Bodies[Crew].Integrity > 0);
                    Test->AddInfo(FString::Printf(TEXT("600s pacing scenario=%d crew=%d stress=%.2f integrity=%.2f"),
                                                  Medical, Crew, Model.Bodies[Crew].Stress,
                                                  Model.Bodies[Crew].Integrity));
                }
                Sim->AdvanceSteps(1);
                Test->TestEqual(TEXT("Experiment stops exactly at 600 seconds"), Model.TickIndex, uint64(18000));
                Test->TestTrue(TEXT("Unfinished standard experiment resolves at deadline"),
                               Sim->ExperimentEnding == EExperimentEnding::EXPERIMENT_INCOMPLETE);
            }
            Start();
            Test->TestTrue(TEXT("Local LLM mode remains available"),
                           Sim->SubmitDirective(TEXT("llm")).Contains(TEXT("AI-LINK>")) && Sim->bAILink);
            Test->TestTrue(TEXT("Local intent translates routing"),
                           Sim->SubmitDirective(TEXT("send chen to BRIDGE")).Contains(TEXT("queued")));
            Sim->AdvanceSteps(1);
            Test->TestEqual(TEXT("AI-LINK shares authoritative route path"), Model.Bodies[1].AssignedRoom, 2);
            Sim->SubmitDirective(TEXT("cmd"));
            Test->TestFalse(TEXT("CMD restores direct control"), Sim->bAILink);
            Start();
            for (int32 Crew : {0, 1})
            {
                Sim->CancelCrewRoute(Crew);
                Model.Bodies[Crew].Room = Model.Bodies[Crew].AssignedRoom = 3;
                Sim->InitializeCrewStaging(Crew);
            }
            Model.Bodies[0].Integrity = 40;
            Model.Bodies[0].Stress = 60;
            Sim->AdvanceSteps(1800);
            Test->TestTrue(TEXT("Hands-on triage restores sustained viability"), Model.Bodies[0].Integrity >= 84.9);
            Model.Bodies[0].Integrity = 0;
            Sim->AdvanceSteps(300);
            Test->TestEqual(TEXT("Triage never resurrects a dead subject"), Model.Bodies[0].Integrity, 0.);
            Start();
            Sim->RecordCue(2, .49f, TEXT("LOW_WEIGHT_NOISE"));
            Sim->RecordCue(2, .50f, TEXT("BOUNDARY_TURNING_POINT"));
            Sim->RecordCue(2, .90f, TEXT("HIGH_WEIGHT_INCIDENT"));
            const FString Highlights = UI->EvaluationHighlights();
            Test->TestFalse(TEXT("Dossier excludes weight below 0.50"), Highlights.Contains(TEXT("LOW_WEIGHT_NOISE")));
            Test->TestTrue(TEXT("Dossier includes weight exactly 0.50"),
                           Highlights.Contains(TEXT("BOUNDARY_TURNING_POINT")));
            Test->TestTrue(TEXT("Dossier includes major incidents"), Highlights.Contains(TEXT("HIGH_WEIGHT_INCIDENT")));
            Sim->CaptureIncident(TEXT("First architectural review incident"), 2);
            Sim->CaptureIncident(TEXT("Second architectural review incident"), 3);
            Model.ObjectiveProgress = 1;
            Sim->EvaluateExperimentEnding();
            FrozenTick = Model.TickIndex;
            Ready = Now + 1;
            return false;
        }
        case 1:
            Click(0);
            Test->TestEqual(TEXT("Play button opens first incident"), Sim->ReelCursor, 0);
            Click(2);
            Test->TestEqual(TEXT("Next button advances incident"), Sim->ReelCursor, 1);
            Click(1);
            Test->TestEqual(TEXT("Prev button returns to first incident"), Sim->ReelCursor, 0);
            Click(3);
            Test->TestEqual(TEXT("Exit reel clears review cursor"), Sim->ReelCursor, INDEX_NONE);
            Test->TestFalse(TEXT("Exit reel stops playback"), Sim->bReelPlaying);
            Test->TestEqual(TEXT("Dossier actions keep experiment frozen"), Model.TickIndex, FrozenTick);
            Ready = Now + .5;
            return false;
        case 2:
        {
            TArray<FColor> Pixels;
            FIntVector Size;
            const bool Captured = FSlateApplication::Get().TakeScreenshot(UI->GetContentRoot(), Pixels, Size);
            Test->TestTrue(TEXT("Consolidation verification screenshot captured"), Captured);
            if (Captured)
            {
                TArray64<uint8> PNG;
                FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
                Test->TestTrue(TEXT("Consolidation screenshot saved"),
                               FFileHelper::SaveArrayToFile(PNG, *(FPaths::ProjectSavedDir() /
                                                                   TEXT("DirectOrScut/ArchitectureVerification.png"))));
            }
            Sim->ResetExperiment();
            UI->ShowGrid();
            Ready = Now + .5;
            return false;
        }
        case 3:
        {
            TArray<FColor> Pixels;
            FIntVector Size;
            if (FSlateApplication::Get().TakeScreenshot(UI->GetContentRoot(), Pixels, Size))
            {
                TArray64<uint8> PNG;
                FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
                Test->TestTrue(TEXT("Eight-feed desk screenshot saved"),
                               FFileHelper::SaveArrayToFile(
                                   PNG, *(FPaths::ProjectSavedDir() / TEXT("DirectOrScut/ArchitectureDesk.png"))));
            }
            return true;
        }
        }
        return true;
    }

  private:
    FAutomationTestBase *Test;
    int32 Stage = 0;
    double Started = 0, Ready = 0;
    uint64 FrozenTick = 0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchitectureRegressionTest, "DirectOrScut.PIE.ArchitectureRegression",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArchitectureRegressionTest::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FArchitectureRegressionCommand>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

class FTactileSurveillanceCheck : public IAutomationLatentCommand
{
  public:
    explicit FTactileSurveillanceCheck(FAutomationTestBase *InTest) : Test(InTest) {}

    static TSharedPtr<SWidget> Find(const TSharedRef<SWidget> &Widget, FName Tag)
    {
        if (Widget->GetTag() == Tag)
        {
            return Widget;
        }
        auto *Children = Widget->GetChildren();
        for (int32 Index = 0; Index < Children->Num(); ++Index)
        {
            if (auto Found = Find(Children->GetChildAt(Index), Tag))
            {
                return Found;
            }
        }
        return nullptr;
    }

    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (Started == 0)
        {
            Started = Now;
        }
        if (Now - Started > 120)
        {
            Test->AddError(TEXT("Tactile surveillance readiness timeout"));
            return true;
        }
        auto *World = GEditor->PlayWorld.Get();
        if (!World || !World->HasBegunPlay())
        {
            return false;
        }
        auto *Sim = World->GetSubsystem<UDirectOrScutSimSubsystem>();
        auto *Controller = World->GetFirstPlayerController();
        auto *HUD = Controller ? Cast<ADirectOrScutHUD>(Controller->GetHUD()) : nullptr;
        if (!Sim || !Sim->Simulation.bReady || !HUD || !HUD->GetWorkstation())
        {
            return false;
        }
        auto UI = HUD->GetWorkstation();
        UI->SetDebugEightCamGrid(true); // Legacy diagnostic coverage.
        if (UI->GetCachedGeometry().GetLocalSize().X < 100 || Now < Ready)
        {
            return false;
        }
        Sim->bAutomaticAdvance = false;
        auto &Model = Sim->Simulation;
        auto Fresh = [&]()
        {
            Sim->ResetExperiment();
            Sim->QueueConsoleCommand(TEXT("start_experiment"));
            Sim->AdvanceSteps(1);
        };
        auto Settle = [&](int32 Crew, int32 Room)
        {
            Sim->CancelCrewRoute(Crew);
            auto &Unit = Model.Bodies[Crew];
            Unit.Room = Unit.AssignedRoom = Room;
            Unit.State = EDirectOrScutUnitState::Working;
            Sim->InitializeCrewStaging(Crew);
            Sim->CrewStaging[Crew].AnchorHoldTicks = 2000;
        };
        auto QuietCrew = [&]()
        {
            for (int32 Crew = 0; Crew < 3; ++Crew)
            {
                Settle(Crew, 3);
            }
        };
        auto Crisis = [&](const TCHAR *Command)
        {
            TArray<FString> Tokens;
            FString(Command).ParseIntoArrayWS(Tokens);
            Test->TestTrue(TEXT("Crisis command recognized"), Sim->ExecuteCrisisCommand(Tokens));
        };
        auto Key = [&](FKey Value)
        {
            auto &App = FSlateApplication::Get();
            App.ProcessKeyDownEvent(FKeyEvent(Value, FModifierKeysState(), 0, false, 0, 0));
            App.ProcessKeyUpEvent(FKeyEvent(Value, FModifierKeysState(), 0, false, 0, 0));
        };
        if (Stage == 0)
        {
            ++Stage;
            for (int32 Crew = 0; Crew < 3; ++Crew)
            {
                for (int32 Room : {2, 6})
                {
                    Fresh();
                    QuietCrew();
                    Settle(Crew, Room);
                    const float Before = Model.Bodies[Crew].Profile.Suspicion;
                    Test->TestTrue(TEXT("Settled console has terminal line of sight"), Sim->CanWitnessInjection(Crew));
                    Crisis(Room == 2 ? TEXT("falsify_bio ash nominal") : TEXT("spoof_log MED_BAY"));
                    // Ortiz's log-audit penalty is independent of the new 18% terminal witness penalty.
                    const float Audit = Crew == 2 && Room == 6 ? .12f : 0.f;
                    Test->TestTrue(TEXT("Live injection marks the physical human witness"),
                                   Model.Bodies[Crew].bObservedInjection);
                    Test->TestTrue(
                        TEXT("Witness suspicion rises by exactly eighteen percent"),
                        FMath::IsNearlyEqual(Model.Bodies[Crew].Profile.Suspicion, Before + .18f + Audit, .0001f));
                    Test->TestTrue(TEXT("Witness telemetry identifies live packet injection"),
                                   Sim->TerminalBuffer().Contains(TEXT("[WITNESS]")) &&
                                       Sim->TerminalBuffer().Contains(TEXT("observed live packet injection")));
                }
            }
            for (int32 Mask = 0; Mask < 7; ++Mask)
            {
                Fresh();
                QuietCrew();
                Settle(0, 2);
                if (Mask == 0)
                {
                    Sim->StationRooms[2].Power = EPowerState::Low;
                }
                if (Mask == 1)
                {
                    Sim->StationRooms[2].Power = EPowerState::Offline;
                }
                if (Mask == 2)
                {
                    Model.Rooms[2].AlertSeconds = 10;
                }
                if (Mask == 3)
                {
                    Model.Bodies[0].State = EDirectOrScutUnitState::Panic;
                }
                if (Mask == 4)
                {
                    Sim->StartCrewRoute(0, 3, true);
                }
                if (Mask == 5)
                {
                    Settle(0, 4);
                }
                if (Mask == 6)
                {
                    Sim->CrewStaging[0].Position += FVector(300, 0, 0);
                }
                Crisis(TEXT("falsify_bio ash nominal"));
                Test->TestFalse(TEXT("Power, fault, distraction, relocation and console distance mask a witness"),
                                Model.Bodies[0].bObservedInjection);
            }
            Fresh();
            QuietCrew();
            Settle(3, 2);
            Crisis(TEXT("spoof_log BRIDGE"));
            Test->TestFalse(TEXT("Android is not a human witness"), Model.Bodies[3].bObservedInjection);
            Fresh();
            Sim->UplinkHeatPct = 70;
            Sim->AdvanceSteps(30);
            Test->TestTrue(TEXT("Uplink cools by 3.5 percent per second"),
                           FMath::IsNearlyEqual(Sim->UplinkHeatPct, 66.5f, .002f));
            Fresh();
            Sim->AddUplinkHeat(85);
            Test->TestFalse(TEXT("Exactly eighty-five percent does not trip diagnostics"), Sim->IsUplinkLocked());
            Sim->AddUplinkHeat(.01f);
            Test->TestTrue(TEXT("Crossing threshold starts six-second lockout"),
                           Sim->IsUplinkLocked() && FMath::IsNearlyEqual(Sim->UplinkLockoutSeconds(), 6.f));
            Test->TestTrue(TEXT("AI latency adds ten percent suspicion"),
                           FMath::IsNearlyEqual(Model.Bodies[1].Profile.Suspicion, .12f, .0001f));
            Sim->AdvanceSteps(179);
            Test->TestTrue(TEXT("Lockout persists through tick 179"), Sim->IsUplinkLocked());
            Sim->AdvanceSteps(1);
            Test->TestFalse(TEXT("Lockout expires exactly at tick 180"), Sim->IsUplinkLocked());
            Fresh();
            for (const TCHAR *Command :
                 {TEXT("power MED_BAY low"), TEXT("power MED_BAY off"), TEXT("power MED_BAY nominal"),
                  TEXT("power OBSERVATION_DECK low"), TEXT("seal BRIDGE")})
            {
                Sim->QueueConsoleCommand(Command);
            }
            Sim->AdvanceSteps(1);
            Test->TestTrue(TEXT("Four back-to-back reroutes overheat the uplink"), Sim->IsUplinkLocked());
            Test->TestTrue(TEXT("Threshold-crossing command executes"), Sim->StationRooms[4].Power == EPowerState::Low);
            Test->TestFalse(TEXT("Subsequent same-tick seal is rejected"), Sim->IsRoomSealed(2));
            const uint64 BeforeSpoof = Sim->MedicalSpoofs[0].Until;
            Crisis(TEXT("falsify_bio vale critical"));
            Test->TestEqual(TEXT("Lockout prevents spoof mutation"), Sim->MedicalSpoofs[0].Until, BeforeSpoof);
            Sim->SubmitDirective(TEXT("llm"));
            Sim->SubmitDirective(TEXT("send chen to BRIDGE"));
            Sim->AdvanceSteps(1);
            Test->TestEqual(TEXT("AI-LINK cannot bypass command lockout"), Model.Bodies[1].AssignedRoom, 3);
            Sim->SubmitDirective(TEXT("cmd"));
            Test->TestTrue(TEXT("Diagnostic warning is visible"), Sim->TerminalBuffer().Contains(TEXT("[AI LATENCY]")));
            Fresh();
            Test->TestTrue(TEXT("Reset clears heat, cooldown and witness memory"),
                           Sim->UplinkHeatPct == 0 && !Sim->IsUplinkLocked() && !Model.Bodies[0].bObservedInjection);
            auto Replay = [&]()
            {
                TStrongObjectPtr<UDirectOrScutSimSubsystem> Copy(NewObject<UDirectOrScutSimSubsystem>(World));
                Copy->Simulation = Model;
                Copy->ResetExperiment();
                Copy->QueueConsoleCommand(TEXT("start_experiment"));
                Copy->AdvanceSteps(1);
                for (int32 Index = 0; Index < 4; ++Index)
                {
                    Copy->QueueConsoleCommand(TEXT("power MED_BAY low"));
                }
                return Copy;
            };
            auto A = Replay(), B = Replay();
            for (int32 Tick = 0; Tick < 300; ++Tick)
            {
                A->Tick(1.f / 30);
            }
            for (int32 Frame = 0; Frame < 600; ++Frame)
            {
                B->Tick(1.f / 60);
            }
            Test->TestEqual(TEXT("Heat and cooldown replay identically at thirty/sixty Hz"), A->SurveillanceStateHash(),
                            B->SurveillanceStateHash());
            Test->TestEqual(TEXT("Witness/latency telemetry replays identically"), A->TerminalBuffer(),
                            B->TerminalBuffer());
            Fresh();
            UI->SetConsoleOpen(false);
            UI->ShowGrid();
            FSlateApplication::Get().SetKeyboardFocus(UI);
            const FKey FeedKeys[] = {EKeys::One,  EKeys::Two, EKeys::Three, EKeys::Four,
                                     EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight};
            for (int32 Feed = 0; Feed < 8; ++Feed)
            {
                Key(FeedKeys[Feed]);
                Test->TestEqual(TEXT("One-key feed selection remains intact"), UI->ActiveFeed, Feed + 1);
            }
            Key(EKeys::Four);
            Key(EKeys::E);
            Test->TestTrue(TEXT("Power quick-action is queued until the next fixed step"),
                           Sim->StationRooms[4].Power == EPowerState::Nominal);
            Sim->AdvanceSteps(1);
            Test->TestTrue(TEXT("E switches selected room to low power"),
                           Sim->StationRooms[4].Power == EPowerState::Low);
            Key(EKeys::E);
            Sim->AdvanceSteps(1);
            Test->TestTrue(TEXT("E cycles low to offline"), Sim->StationRooms[4].Power == EPowerState::Offline);
            Key(EKeys::E);
            Sim->AdvanceSteps(1);
            Test->TestTrue(TEXT("E restores nominal power"), Sim->StationRooms[4].Power == EPowerState::Nominal);
            Sim->UplinkHeatPct = 0;
            Key(EKeys::B);
            Sim->AdvanceSteps(1);
            Test->TestTrue(TEXT("B seals selected room"), Sim->IsRoomSealed(4));
            Key(EKeys::B);
            Sim->AdvanceSteps(1);
            Test->TestFalse(TEXT("B unseals selected room"), Sim->IsRoomSealed(4));
            Sim->UplinkHeatPct = 0;
            Key(EKeys::Q);
            Sim->AdvanceSteps(1);
            Test->TestEqual(TEXT("Q lures crew into the selected feed room"), Sim->VoiceSpoof.Room, 4);
            Sim->SecretAgendas[2].Progress = 50;
            Key(EKeys::X);
            Sim->AdvanceSteps(1);
            Test->TestTrue(TEXT("X uses authoritative log-spoof path"), Sim->SecretAgendas[2].Progress <= 1);
            auto Prompt = StaticCastSharedPtr<SEditableTextBox>(Find(UI->GetContentRoot(), TEXT("ScutPrompt")));
            FSlateApplication::Get().SetKeyboardFocus(Prompt);
            const auto Power = Sim->StationRooms[4].Power;
            Key(EKeys::E);
            Sim->AdvanceSteps(1);
            Test->TestTrue(TEXT("Typing into CMD does not trigger quick-actions"), Sim->StationRooms[4].Power == Power);
            Fresh();
            UI->SetConsoleOpen(false);
            FSlateApplication::Get().SetKeyboardFocus(UI);
            auto *Room = Sim->RoomSetActors[4].Get();
            Test->TestNotNull(TEXT("Room lighting owner exists"), Room);
            if (Room)
            {
                for (auto State : {EPowerState::Low, EPowerState::Offline, EPowerState::Nominal})
                {
                    Sim->StationRooms[4].Power = State;
                    Sim->SynchronizeActors();
                    TInlineComponentArray<UPointLightComponent *> Lights(Room);
                    Test->TestEqual(TEXT("Three standard stage lights retained"), Lights.Num(), 3);
                    for (auto *Light : Lights)
                    {
                        Test->TestEqual(TEXT("Lighting visibility follows power"), Light->IsVisible(),
                                        State != EPowerState::Offline);
                        Test->TestTrue(TEXT("Lighting intensity follows power"),
                                       FMath::IsNearlyEqual(Light->Intensity, State == EPowerState::Nominal ? 6500.f
                                                                              : State == EPowerState::Low   ? 650.f
                                                                                                            : 0.f));
                        if (State == EPowerState::Low)
                        {
                            Test->TestTrue(TEXT("Emergency light is amber"), Light->LightColor.R > Light->LightColor.B);
                        }
                        if (State == EPowerState::Nominal)
                        {
                            Test->TestTrue(TEXT("Nominal neutral lighting restored"),
                                           Light->LightColor == FColor::White && Light->bUseTemperature &&
                                               Light->Temperature == 5800.f);
                        }
                    }
                }
            }
            Settle(0, 2);
            Settle(1, 2);
            Sim->CrewStaging[0].bHuddle = Sim->CrewStaging[1].bHuddle = true;
            Sim->CrewStaging[0].Partner = 1;
            Sim->CrewStaging[1].Partner = 0;
            Sim->PairTrust[0][1] = Sim->PairTrust[1][0] = .7f;
            Sim->MedicalSpoofs[0].Until = Model.TickIndex + 1800;
            Sim->VoiceSpoof.Room = 4;
            Sim->VoiceSpoof.MoveUntil = Model.TickIndex + 900;
            Sim->StationRooms[3].OxygenPct = 60;
            Sim->StationRooms[3].Power = EPowerState::Low;
            Sim->StationRooms[6].Power = EPowerState::Offline;
            Sim->BreachCrew = 3;
            Sim->BreachRoom = 2;
            Sim->BreachTicks = 1350;
            Sim->UplinkHeatPct = 78;
            const FString Bridge = Sim->RoomSurveillanceBadges(2);
            Test->TestTrue(TEXT("Tile exposes spoof, alliance and breach countdown"),
                           Bridge.Contains(TEXT("[SPOOFED]")) && Bridge.Contains(TEXT("[ALLIANCE]")) &&
                               Bridge.Contains(TEXT("[UPLINK BREACH: 45.0s]")));
            Test->TestTrue(TEXT("Room badges expose voice lure and low oxygen"),
                           Sim->RoomSurveillanceBadges(4).Contains(TEXT("[VOICE LURE]")) &&
                               Sim->RoomSurveillanceBadges(3).Contains(TEXT("[LOW O2]")));
            Sim->SynchronizeActors();
            UI->ShowGrid();
            UI->SelectFeed(2, false);
            Ready = Now + 1;
            return false;
        }
        auto Slate = Find(UI->GetContentRoot(), TEXT("ScutFeedInterrupted6"));
        Test->TestTrue(TEXT("Offline feed uses a visible interruption slate"),
                       Slate.IsValid() && Slate->GetVisibility() == EVisibility::Visible);
        Test->TestTrue(TEXT("Quick-action strip remains in the desk"),
                       Find(UI->GetContentRoot(), TEXT("ScutQuickActionContext")).IsValid());
        auto Border = StaticCastSharedPtr<SBorder>(Find(UI->GetContentRoot(), TEXT("ScutCamera2")));
        Test->TestTrue(TEXT("Breach overrides the selected monitor border with crimson"),
                       Border.IsValid() && Border->GetBorderBackgroundColor().GetSpecifiedColor().R >
                                               Border->GetBorderBackgroundColor().GetSpecifiedColor().G);
        TArray<FColor> Pixels;
        FIntVector Size;
        const bool Captured = FSlateApplication::Get().TakeScreenshot(UI->GetContentRoot(), Pixels, Size);
        Test->TestTrue(TEXT("Tactile surveillance verification screenshot captured"), Captured);
        if (Captured)
        {
            TArray64<uint8> PNG;
            FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
            Test->TestTrue(TEXT("Tactile surveillance screenshot saved"),
                           FFileHelper::SaveArrayToFile(
                               PNG, *(FPaths::ProjectSavedDir() / TEXT("DirectOrScut/TactileSurveillance.png"))));
        }
        auto PowerButton = Find(UI->GetContentRoot(), TEXT("ScutQuickAction1"));
        Test->TestTrue(TEXT("Selected-room quick-action button exists"), PowerButton.IsValid());
        if (PowerButton)
        {
            auto &App = FSlateApplication::Get();
            const auto &Geometry = PowerButton->GetCachedGeometry();
            const FVector2D Point = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * .5);
            auto Window = App.FindWidgetWindow(PowerButton.ToSharedRef());
            if (!Window)
            {
                Test->AddError(TEXT("Quick-action window missing"));
            }
            else
            {
                App.ProcessMouseMoveEvent(FPointerEvent(0, Point, Point - FVector2D(1, 0), TSet<FKey>{}, EKeys::Invalid,
                                                        0, FModifierKeysState()));
                App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),
                                                FPointerEvent(0, Point, Point, TSet<FKey>{EKeys::LeftMouseButton},
                                                              EKeys::LeftMouseButton, 0, FModifierKeysState()));
                App.ProcessMouseButtonUpEvent(
                    FPointerEvent(0, Point, Point, TSet<FKey>{}, EKeys::LeftMouseButton, 0, FModifierKeysState()));
                Test->TestTrue(TEXT("Clicked quick-action still waits for fixed step"),
                               Sim->StationRooms[2].Power == EPowerState::Nominal);
                Sim->AdvanceSteps(1);
                Test->TestTrue(TEXT("Quick-action click executes and shares the heat budget"),
                               Sim->StationRooms[2].Power == EPowerState::Low && Sim->IsUplinkLocked());
            }
        }
        Sim->ResetExperiment();
        return true;
    }

  private:
    FAutomationTestBase *Test;
    int32 Stage = 0;
    double Started = 0, Ready = 0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTactileSurveillanceTest, "DirectOrScut.PIE.TactileSurveillance",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTactileSurveillanceTest::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FTactileSurveillanceCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

class FUnifiedPhosphorTerminalCheck : public IAutomationLatentCommand
{
  public:
    explicit FUnifiedPhosphorTerminalCheck(FAutomationTestBase *In) : Test(In) {}
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (!Started)
        {
            Started = Now;
        }
        if (Now - Started > 60)
        {
            Test->AddError(TEXT("Unified terminal verification timed out"));
            return true;
        }
        auto *World = GEditor->PlayWorld.Get();
        if (!World || !World->HasBegunPlay())
        {
            return false;
        }
        auto *Sim = World->GetSubsystem<UDirectOrScutSimSubsystem>();
        auto *PC = World->GetFirstPlayerController();
        auto *HUD = PC ? Cast<ADirectOrScutHUD>(PC->GetHUD()) : nullptr;
        if (!Sim || !HUD || !HUD->GetWorkstation())
        {
            return false;
        }
        auto Workstation = HUD->GetWorkstation();
        if (Workstation->GetCachedGeometry().GetLocalSize().X < 100 || Now < Ready)
        {
            return false;
        }
        auto Find = [&](FName Tag) { return FBriefingSurveillanceCheck::Find(Workstation->GetContentRoot(), Tag); };
        auto Capture = [&](const TCHAR *Name)
        {
            TArray<FColor> Pixels;
            FIntVector Size;
            const bool OK = FSlateApplication::Get().TakeScreenshot(Workstation.ToSharedRef(), Pixels, Size);
            Test->TestTrue(TEXT("Fresh CRT screenshot captured"), OK);
            if (OK)
            {
                TArray64<uint8> PNG;
                FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
                Test->TestTrue(
                    TEXT("CRT screenshot saved"),
                    FFileHelper::SaveArrayToFile(PNG, *(FPaths::ProjectSavedDir() / TEXT("DirectOrScut") / Name)));
            }
        };
        auto Key = [&](FKey K)
        {
            auto &App = FSlateApplication::Get();
            App.SetKeyboardFocus(Workstation);
            App.ProcessKeyDownEvent(FKeyEvent(K, FModifierKeysState(), 0, false, 0, 0));
            App.ProcessKeyUpEvent(FKeyEvent(K, FModifierKeysState(), 0, false, 0, 0));
        };
        switch (Stage++)
        {
        case 0:
            Sim->bAutomaticAdvance = false;
            Test->TestFalse(TEXT("Unified startup hides debug grid"), Workstation->IsDebugEightCamGrid());
            Test->TestFalse(TEXT("Startup is not grid navigation"), Workstation->bGrid);
            Test->TestTrue(TEXT("Unified stream exists"), Find(TEXT("ScutUnifiedTerminal")).IsValid());
            Test->TestFalse(TEXT("Legacy camera wall absent by default"), Find(TEXT("ScutCameraWall")).IsValid());
            Test->TestTrue(TEXT("Inline ECG and schematic inserted"),
                           Find(TEXT("ScutInlineECG")).IsValid() && Find(TEXT("ScutInlineSchematic")).IsValid());
            Test->TestEqual(TEXT("Startup two instrument blocks"), Workstation->GetInlineInstrumentCount(), 2);
            Hash = Sim->SurveillanceStateHash();
            Tick = Sim->Simulation.TickIndex;
            Ready = Now + 1.5;
            break;
        case 1:
            Capture(TEXT("UnifiedTerminalPIE.png"));
            Key(EKeys::F9);
            Test->TestTrue(TEXT("F9 enables diagnostics"), Workstation->IsDebugEightCamGrid());
            Test->TestEqual(TEXT("F9 does not change authoritative hash"), Sim->SurveillanceStateHash(), Hash);
            Ready = Now + .3;
            break;
        case 2:
        {
            for (int32 I = 1; I <= 8; ++I)
            {
                Test->TestTrue(TEXT("Eight diagnostic tiles present"),
                               Find(FName(*FString::Printf(TEXT("ScutCamera%d"), I))).IsValid());
            }
            Capture(TEXT("DebugEightCamGridPIE.png"));
            Test->TestEqual(TEXT("Debug view keeps fixed tick frozen"), Sim->Simulation.TickIndex, Tick);
            Test->TestEqual(TEXT("Debug view preserves simulation and surveillance state"),
                            Sim->SurveillanceStateHash(), Hash);
            auto CommandPrompt = StaticCastSharedPtr<SEditableTextBox>(Find(TEXT("ScutPrompt")));
            Test->TestTrue(TEXT("Diagnostic command prompt exists"), CommandPrompt.IsValid());
            if (CommandPrompt)
            {
                CommandPrompt->SetText(FText::FromString(TEXT("debug_grid")));
                auto &App = FSlateApplication::Get();
                App.SetKeyboardFocus(CommandPrompt);
                App.ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
                App.ProcessKeyUpEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
            }
            Test->TestFalse(TEXT("debug_grid returns to unified"), Workstation->IsDebugEightCamGrid());
            Workstation->HandlePresentationCommand(TEXT("/debug"));
            Test->TestTrue(TEXT("/debug enables diagnostics"), Workstation->IsDebugEightCamGrid());
            Key(EKeys::F9);
            Test->TestFalse(TEXT("F9 returns to unified"), Workstation->IsDebugEightCamGrid());
            Test->TestEqual(TEXT("All view toggles remain pure presentation"), Sim->SurveillanceStateHash(), Hash);
            Test->TestTrue(TEXT("Director room selection accepted"),
                           Workstation->HandlePresentationCommand(TEXT("show_cam med_bay")));
            Test->TestEqual(TEXT("Switch starts hidden wipe"), Workstation->GetInlineWipeProgress(), 0.f);
            Ready = Now + .15;
            break;
        }
        case 3:
            Test->TestTrue(TEXT("Raster wipe progresses between endpoints"),
                           Workstation->GetInlineWipeProgress() > 0 && Workstation->GetInlineWipeProgress() < 1);
            Test->TestEqual(TEXT("Wipe never advances simulation"), Sim->Simulation.TickIndex, Tick);
            Sim->StationRooms[3].Power =
                EPowerState::Offline; // Explicit authoritative fixture; rendering may only read it.
            Workstation->SetPresentationNoise(.25f, .4f);
            Ready = Now + .8;
            break;
        case 4:
        {
            auto *MID = Workstation->GetInlineOpticMaterial();
            Test->TestNotNull(TEXT("Inline optic owns an MID"), MID);
            if (MID)
            {
                Test->TestEqual(TEXT("Offline room binds carrier noise"),
                                MID->K2_GetScalarParameterValue(TEXT("SignalOfflineNoise")), 1.f);
                Test->TestEqual(TEXT("Wipe reaches exactly revealed"),
                                MID->K2_GetScalarParameterValue(TEXT("WipeProgress")), 1.f);
                Test->TestEqual(TEXT("Scrub hook reaches MID"),
                                MID->K2_GetScalarParameterValue(TEXT("ScrubTrackingNoise")), .25f);
                Test->TestEqual(TEXT("Voice hook reaches MID"),
                                MID->K2_GetScalarParameterValue(TEXT("VoiceBleedIntensity")), .4f);
                ACompartmentCameraRig *Rig = nullptr;
                for (TActorIterator<ACompartmentCameraRig> It(World); It; ++It)
                {
                    Rig = *It;
                }
                Test->TestNotNull(TEXT("Camera rig available"), Rig);
                if (Rig)
                {
                    Test->TestEqual(TEXT("Active optic binds MED_BAY render target"),
                                    MID->K2_GetTextureParameterValue(TEXT("OpticRenderTarget")),
                                    static_cast<UTexture *>(Rig->Targets[ACompartmentCameraRig::FeedForRoom(3)].Get()));
                    for (int32 I = 1; I <= 8; ++I)
                    {
                        Test->TestTrue(TEXT("All eight optical sources stay active"), Rig->CaptureCounts[I] > 1);
                    }
                }
            }
            Capture(TEXT("UnifiedOfflineOpticPIE.png"));
            Workstation->HandlePresentationCommand(TEXT("show_widget ecg chen"));
            Workstation->HandlePresentationCommand(TEXT("show_widget schematic med_bay"));
            Test->TestEqual(TEXT("Director inserts inline instruments"), Workstation->GetInlineInstrumentCount(), 4);
            Workstation->PresentOptic(2);
            if (MID)
            {
                Test->TestTrue(TEXT("Separate blocks have isolated MIDs"),
                               MID != Workstation->GetInlineOpticMaterial());
            }
            Sim->StationRooms[3].Power = EPowerState::Nominal;
            Workstation->SetPresentationNoise(0, 0);
            Ready = Now + .8;
            break;
        }
        case 5:
        {
            auto *MID = Workstation->GetInlineOpticMaterial();
            if (MID)
            {
                Test->TestEqual(TEXT("Online compartment clears offline carrier"),
                                MID->K2_GetScalarParameterValue(TEXT("SignalOfflineNoise")), 0.f);
            }
            Test->TestFalse(TEXT("Invalid subject rejected"),
                            Workstation->PresentInstrument(ETerminalInstrument::ECG, 4));
            Test->TestFalse(TEXT("Invalid room rejected"), Workstation->PresentOptic(8));
            Test->TestFalse(TEXT("Unknown instrument rejected"),
                            Workstation->PresentInstrument(static_cast<ETerminalInstrument>(255), 0));
            Test->TestEqual(TEXT("Presentation preserves fixed tick throughout"), Sim->Simulation.TickIndex, Tick);
            Test->TestEqual(TEXT("Presentation preserves simulation hash after fixture restore"),
                            Sim->SurveillanceStateHash(), Hash);
            Sim->ResetExperiment();
            Ready = Now + .3;
            break;
        }
        case 6:
            Test->TestFalse(TEXT("Reset retains default unified mode"), Workstation->IsDebugEightCamGrid());
            Test->TestEqual(TEXT("Reset clears stream history to startup instruments"),
                            Workstation->GetInlineInstrumentCount(), 2);
            return true;
        }
        return false;
    }

  private:
    FAutomationTestBase *Test;
    int32 Stage = 0;
    uint64 Hash = 0, Tick = 0;
    double Started = 0, Ready = 0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedPhosphorTerminalTest, "DirectOrScut.PIE.UnifiedPhosphorTerminal",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnifiedPhosphorTerminalTest::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FUnifiedPhosphorTerminalCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

#endif
