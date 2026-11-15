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
#include "OfflineShipAIIntentProvider.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "DirectOrScutHUD.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
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
            Test->AddInfo(FString::Printf(TEXT("Scenario switch state: medical=%d phase=%d tick=%llu cues=%d"),
                                          S->bMedicalScenario, int32(S->ExperimentPhase), M.TickIndex,
                                          S->CinematicCues.Num()));
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
            Sim->ConfigureDirectorEndpoint(
                TEXT("")); // This regression specifically verifies deterministic offline translation.
            Test->TestTrue(TEXT("Local LLM mode remains available"),
                           Sim->SubmitDirective(TEXT("llm")).Contains(TEXT("AI-LINK>")) && Sim->bAILink);
            Test->TestTrue(TEXT("Local intent translates routing"),
                           Sim->SubmitDirective(TEXT("send chen to BRIDGE")).IsEmpty());
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
            Sim->ConfigureDirectorEndpoint(TEXT("")); // Isolate lockout from optional inference latency.
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
        if (Stage == 22)
        {
            // Emulate repeated emissions from any passive presenter on every actual PIE frame.
            Test->TestFalse(
                TEXT("Repeated passive ECG stays suppressed"),
                Workstation->PresentInstrument(ETerminalInstrument::ECG, 3, EInlinePresentationIntent::Passive));
            Test->TestFalse(
                TEXT("Repeated passive schematic stays suppressed"),
                Workstation->PresentInstrument(ETerminalInstrument::Schematic, 7, EInlinePresentationIntent::Passive));
        }
        if (Workstation->GetCachedGeometry().GetLocalSize().X < 100 || Now < Ready)
        {
            return false;
        }
        SawCursorOn |= Workstation->IsBlockCursorVisible();
        SawCursorOff |= !Workstation->IsBlockCursorVisible();
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
        auto CheckMOTD = [&]()
        {
            const auto &Entries = Workstation->GetTerminalStreamEntries();
            Test->TestEqual(TEXT("Boot/reset seeds exactly one prose MOTD record"), Entries.Num(), 1);
            for (const auto &Entry : Entries)
            {
                Test->TestFalse(TEXT("MOTD has no inline attachment metadata"), Entry.Attachment.IsSet());
                Test->TestTrue(TEXT("MOTD carries node, operational notice and initialization instruction"),
                               Entry.Text.StartsWith(TEXT("+---")) &&
                                   Entry.Text.Contains(TEXT("|CARRIER: THETA-9 // MOTHER-7")) &&
                                   Entry.Text.Contains(TEXT("NOTICE:")) && Entry.Text.Contains(TEXT("[ENTER]")) &&
                                   Entry.Text.Contains(TEXT("start_experiment")));
                for (TCHAR Character : Entry.Text)
                {
                    Test->TestTrue(TEXT("MOTD is ASCII only"), Character <= 127);
                }
            }
            Test->TestEqual(TEXT("MOTD boot has zero live optics"), Workstation->GetLiveInlineOpticCount(), 0);
            Test->TestEqual(TEXT("MOTD boot has zero inline instruments"), Workstation->GetInlineInstrumentCount(), 0);
            for (const TCHAR *Tag : {TEXT("ScutInlineOptic"), TEXT("ScutInlineECG"), TEXT("ScutZoomedRoomSchematic"),
                                     TEXT("ScutInlineOcular")})
            {
                Test->TestFalse(TEXT("No graphical stream attachment is mounted until requested"), Find(Tag).IsValid());
            }
        };
        auto CheckPanels = [&]()
        {
            const TCHAR *Required[] = {TEXT("ScutMediationColumn"),    TEXT("ScutReadoutColumn"),
                                       TEXT("ScutTerminalStream"),     TEXT("ScutTeletypePanel"),
                                       TEXT("ScutOverheadStationMap"), TEXT("ScutStatusCarrierBar")};
            for (const TCHAR *Tag : Required)
            {
                auto Widget = Find(Tag);
                Test->TestTrue(FString::Printf(TEXT("Paneled terminal has %s"), Tag), Widget.IsValid());
                if (!Widget)
                {
                    return;
                }
                const auto Size = Widget->GetCachedGeometry().GetLocalSize();
                Test->TestTrue(FString::Printf(TEXT("%s has positive geometry"), Tag), Size.X > 20 && Size.Y > 10);
            }
            const auto Left = Find(TEXT("ScutMediationColumn"))->GetCachedGeometry().GetLayoutBoundingRect();
            const auto Right = Find(TEXT("ScutReadoutColumn"))->GetCachedGeometry().GetLayoutBoundingRect();
            const auto Header = Find(TEXT("ScutStatusCarrierBar"))->GetCachedGeometry().GetLayoutBoundingRect();
            const auto Transcript = Find(TEXT("ScutTerminalStream"))->GetCachedGeometry().GetLayoutBoundingRect();
            Test->TestTrue(TEXT("Mediation/readout columns do not overlap"),
                           Left.Right <= Right.Left && Header.Bottom <= Left.Top + 1);
            Test->TestTrue(
                TEXT("Asymmetric 56/44 workstation split"),
                FMath::Abs((Left.Right - Left.Left) / ((Left.Right - Left.Left) + (Right.Right - Right.Left)) - .56f) <
                    .02f);
            Test->TestFalse(TEXT("Pinned primary optic has been removed"),
                            Find(TEXT("ScutPrimaryOpticSlot")).IsValid());
            Test->TestFalse(TEXT("Pinned injection has been removed"), Find(TEXT("ScutInjectionSlot")).IsValid());
            Test->TestTrue(TEXT("Transcript spans full left column above controls"),
                           Transcript.Top >= Header.Bottom && Transcript.Top - Left.Top < 60 &&
                               (Transcript.Bottom - Transcript.Top) > (Left.Bottom - Left.Top) * .78f);
            const auto Inquiry = Find(TEXT("ScutInquiryBar"))->GetCachedGeometry().GetLayoutBoundingRect();
            Test->TestTrue(TEXT("Transcript never overlaps inquiry bar or prompt"),
                           Transcript.Bottom <= Inquiry.Top &&
                               Inquiry.Bottom <=
                                   Find(TEXT("ScutPrompt"))->GetCachedGeometry().GetLayoutBoundingRect().Top + 1);
            Test->TestTrue(TEXT("Teletype enforces clipping"),
                           Find(TEXT("ScutTerminalStream"))->GetClipping() == EWidgetClipping::ClipToBoundsAlways);
            uint64 OpticId = 0;
            for (const auto &Entry : Workstation->GetTerminalStreamEntries())
            {
                if (Entry.Attachment.IsSet() && Entry.Attachment->Kind == EInlineAttachmentKind::OpticFeed)
                {
                    OpticId = Entry.Id;
                    break;
                }
            }
            const FName ImageTag(*FString::Printf(TEXT("ScutInlineOpticImage%llu"), OpticId));
            auto InlineImage =
                FBriefingSurveillanceCheck::Find(Find(TEXT("ScutTerminalStream")).ToSharedRef(), ImageTag);
            Test->TestEqual(TEXT("Only requested cameras mount inside the scroll hierarchy"), InlineImage.IsValid(),
                            OpticId != 0);
            if (InlineImage)
            {
                const auto Image = InlineImage->GetCachedGeometry().GetRenderBoundingRect();
                Test->TestTrue(TEXT("Inline optic stays 16:9"),
                               FMath::Abs((Image.Right - Image.Left) / (Image.Bottom - Image.Top) - 16.f / 9) < .02f);
            }
            auto HeaderText = StaticCastSharedPtr<STextBlock>(Find(TEXT("ScutCarrier")));
            Test->TestTrue(TEXT("Carrier line fits without truncation"),
                           HeaderText->GetDesiredSize().X <= HeaderText->GetCachedGeometry().GetLocalSize().X + 1);
            if (Workstation->EvidenceSubject >= 0)
            {
                for (int32 Crew = 0; Crew < 4; ++Crew)
                {
                    const auto Row = Find(FName(*FString::Printf(TEXT("ScutVitalsRow%d"), Crew)));
                    Test->TestEqual(TEXT("Only requested subject row is visible"), Row->GetVisibility(),
                                    Workstation->EvidenceSubject == Crew ? EVisibility::Visible
                                                                         : EVisibility::Collapsed);
                }
            }
            if (Workstation->EvidenceRoom >= 0)
            {
                for (int32 Room = 0; Room < 8; ++Room)
                {
                    const auto Row = Find(FName(*FString::Printf(TEXT("ScutEnvironmentRow%d"), Room)));
                    Test->TestEqual(TEXT("Only requested compartment row is visible"), Row->GetVisibility(),
                                    Workstation->EvidenceRoom == Room ? EVisibility::Visible : EVisibility::Collapsed);
                }
            }
            TFunction<void(TSharedRef<SWidget>)> CheckText;
            CheckText = [&](TSharedRef<SWidget> Widget)
            {
                if (Widget->GetVisibility() == EVisibility::Collapsed)
                {
                    return;
                }
                if (Widget->GetTypeAsString() == TEXT("STextBlock"))
                {
                    const auto Text = StaticCastSharedRef<STextBlock>(Widget);
                    const auto Size = Text->GetCachedGeometry().GetLocalSize();
                    const auto Desired = Text->GetDesiredSize();
                    Test->TestTrue(TEXT("Persistent deck text fits its cell without clipping"),
                                   Desired.X <= Size.X + 1 && Desired.Y <= Size.Y + 1);
                }
                auto *Children = Widget->GetChildren();
                for (int32 I = 0; I < Children->Num(); ++I)
                {
                    CheckText(Children->GetChildAt(I));
                }
            };
            CheckText(Find(TEXT("ScutReadoutColumn")).ToSharedRef());
        };
        auto Click = [&](TSharedRef<SWidget> Button, FVector2D Fraction = FVector2D(.5, .5))
        {
            auto &App = FSlateApplication::Get();
            const auto &G = Button->GetCachedGeometry();
            const FVector2D P = G.LocalToAbsolute(G.GetLocalSize() * Fraction);
            auto Window = App.FindWidgetWindow(Button);
            Test->TestTrue(TEXT("Inline routing control has clickable geometry"),
                           G.GetLocalSize().X > 1 && G.GetLocalSize().Y > 1);
            if (!Window)
            {
                Test->AddError(TEXT("Inline control has no Slate window"));
                return;
            }
            App.ProcessMouseMoveEvent(
                FPointerEvent(0, P, P - FVector2D(1, 0), TSet<FKey>{}, EKeys::Invalid, 0, FModifierKeysState()));
            App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),
                                            FPointerEvent(0, P, P, TSet<FKey>{EKeys::LeftMouseButton},
                                                          EKeys::LeftMouseButton, 0, FModifierKeysState()));
            App.ProcessMouseButtonUpEvent(
                FPointerEvent(0, P, P, TSet<FKey>{}, EKeys::LeftMouseButton, 0, FModifierKeysState()));
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
            CheckMOTD();
            Test->TestEqual(TEXT("Boot lazily allocates no inline optic MIDs"),
                            Workstation->GetInlineOpticMaterialCount(), 0);
            Hash = Sim->SurveillanceStateHash();
            Tick = Sim->Simulation.TickIndex;
            Ready = Now + 5;
            break;
        case 1:
        {
            CheckPanels();
            const auto Prose = Find(TEXT("ScutTeletypeLine"));
            Test->TestTrue(TEXT("ASCII MOTD remains visible above inquiry bar"),
                           Prose.IsValid() &&
                               StaticCastSharedPtr<STextBlock>(Prose)->GetText().ToString().StartsWith(TEXT("+---")));
            Capture(TEXT("UnifiedTerminalPIE.png"));
            ActualReportedCrewRoom = Sim->Simulation.Bodies[0].Room;
            Test->TestTrue(TEXT("AI report accepts a contradictory location"),
                           Workstation->SetReportedCrewRoom(0, (ActualReportedCrewRoom + 1) % 8));
            Test->TestEqual(TEXT("Map report uses mediated location"), Workstation->GetReportedCrewRoom(0),
                            (ActualReportedCrewRoom + 1) % 8);
            Test->TestEqual(TEXT("AI location report cannot move authoritative crew"), Sim->Simulation.Bodies[0].Room,
                            ActualReportedCrewRoom);
            Test->TestEqual(TEXT("AI location report cannot change model hash"), Sim->SurveillanceStateHash(), Hash);
            Key(EKeys::F9);
            Test->TestTrue(TEXT("F9 enables diagnostics"), Workstation->IsDebugEightCamGrid());
            Test->TestEqual(TEXT("F9 does not change authoritative hash"), Sim->SurveillanceStateHash(), Hash);
            Ready = Now + .3;
            break;
        }
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
            Test->TestEqual(TEXT("Director inserts inline instruments"), Workstation->GetInlineInstrumentCount(), 2);
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
            Test->TestTrue(TEXT("Zoomed room injection is present"), Find(TEXT("ScutZoomedRoomSchematic")).IsValid());
            const int32 Primary = Workstation->ActiveFeed;
            Test->TestTrue(TEXT("Secondary PiP director request"),
                           Workstation->HandlePresentationCommand(TEXT("show_widget pip med_bay")));
            Test->TestEqual(TEXT("PiP does not replace primary optic"), Workstation->ActiveFeed, Primary);
            Test->TestTrue(TEXT("PiP injection slot exists"), Workstation->GetLiveInlineOpticCount() == 2);
            Test->TestFalse(TEXT("Invalid PiP room rejected"), Workstation->PresentSecondaryOptic(8));
            Test->TestFalse(TEXT("Invalid report rejected"), Workstation->SetReportedCrewRoom(4, 0));
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
            CheckMOTD();
            Test->TestEqual(TEXT("Reset clears report overrides"), Workstation->GetReportedCrewRoom(0),
                            Sim->Simulation.Bodies[0].Room);
            Hash = Sim->SurveillanceStateHash();
            Workstation->HandlePresentationCommand(TEXT("show_cam 0"));
            for (const auto &Entry : Workstation->GetTerminalStreamEntries())
            {
                if (Entry.Attachment.IsSet() && Entry.Attachment->Kind == EInlineAttachmentKind::OpticFeed)
                {
                    OriginalOpticId = Entry.Id;
                    OriginalObservationTick = Entry.Attachment->ObservationTick;
                    break;
                }
            }
            for (const TCHAR *Room : {TEXT("bridge"), TEXT("med_bay"), TEXT("observation_deck")})
            {
                Workstation->HandlePresentationCommand(FString(TEXT("show_cam ")) + Room);
                Test->TestTrue(TEXT("Every emission respects two live optic slots"),
                               Workstation->GetLiveInlineOpticCount() <= 2);
                SeenInlineMaterials.Add(Workstation->GetInlineOpticMaterial());
            }
            Test->TestEqual(TEXT("Only newest two optics remain live"), Workstation->GetLiveInlineOpticCount(), 2);
            Test->TestEqual(TEXT("Two earlier optic records collapsed"), Workstation->GetCollapsedOpticReferenceCount(),
                            2);
            Test->TestEqual(TEXT("History records keep every emission"), Workstation->GetTerminalStreamEntries().Num(),
                            5);
            Test->TestTrue(TEXT("Old optic row now holds a compact reference"),
                           Find(FName(*FString::Printf(TEXT("ScutOpticRef%llu"), OriginalOpticId))).IsValid());
            Test->TestFalse(TEXT("Demoted row no longer holds an image"),
                            Find(FName(*FString::Printf(TEXT("ScutInlineOpticImage%llu"), OriginalOpticId))).IsValid());
            Workstation->HandlePresentationCommand(TEXT("show_widget ecg chen"));
            Workstation->HandlePresentationCommand(TEXT("show_widget schematic med_bay"));
            Test->TestEqual(TEXT("Vectors do not add optic materials"), Workstation->GetInlineOpticMaterialCount(), 2);
            Ready = Now + .3;
            break;
        case 7:
        {
            const int32 Before = Workstation->GetTerminalStreamEntries().Num();
            auto Button = Find(TEXT("ScutStationRoom6"));
            Test->TestTrue(TEXT("Overhead room row is clickable"), Button.IsValid());
            if (Button)
            {
                Click(Button.ToSharedRef());
            }
            Test->TestEqual(TEXT("Overhead click appends a fresh entry"), Workstation->GetTerminalStreamEntries().Num(),
                            Before + 1);
            Test->TestTrue(TEXT("Overhead row mounts SERVER_ARCHIVE"),
                           Workstation->GetTerminalStreamEntries().Last().Attachment->Subject == 6);
            SeenInlineMaterials.Add(Workstation->GetInlineOpticMaterial());
            Test->TestEqual(TEXT("Overhead click is pure presentation"), Sim->SurveillanceStateHash(), Hash);
            Ready = Now + .3;
            break;
        }
        case 8:
        {
            auto Scroll = StaticCastSharedPtr<SScrollBox>(Find(TEXT("ScutTerminalStream")));
            const auto &G = Scroll->GetCachedGeometry();
            const FVector2D P = G.LocalToAbsolute(G.GetLocalSize() * .5);
            auto &App = FSlateApplication::Get();
            App.ProcessMouseMoveEvent(
                FPointerEvent(0, P, P - FVector2D(1, 0), TSet<FKey>{}, EKeys::Invalid, 0, FModifierKeysState()));
            App.ProcessMouseWheelOrGestureEvent(
                FPointerEvent(0, P, P, TSet<FKey>{}, EKeys::Invalid, 1000, FModifierKeysState()), nullptr);
            Scroll->SetScrollOffset(0);
            Ready = Now + .5;
            break;
        }
        case 9:
        {
            auto Ref = Find(FName(*FString::Printf(TEXT("ScutOpticRef%llu"), OriginalOpticId)));
            const int32 Before = Workstation->GetTerminalStreamEntries().Num();
            if (Ref)
            {
                Click(Ref.ToSharedRef());
            }
            else
            {
                Test->AddError(TEXT("Collapsed optic reference disappeared"));
            }
            Test->TestEqual(TEXT("Collapsed reference appends a fresh live entry"),
                            Workstation->GetTerminalStreamEntries().Num(), Before + 1);
            const auto &Latest = Workstation->GetTerminalStreamEntries().Last();
            Test->TestTrue(TEXT("Reopened reference has a new identity and current request time"),
                           Latest.Id > OriginalOpticId && Latest.Attachment.IsSet() &&
                               Latest.Attachment->Subject == 0 &&
                               Latest.Attachment->ObservationTick == Sim->Simulation.TickIndex);
            const auto *Original = Workstation->GetTerminalStreamEntries().FindByPredicate(
                [this](const FTerminalStreamEntry &Entry) { return Entry.Id == OriginalOpticId; });
            Test->TestTrue(TEXT("Original reference metadata remains unchanged"),
                           Original && Original->Attachment->ObservationTick == OriginalObservationTick);
            Test->TestTrue(TEXT("Original row stays collapsed after reopening"),
                           Find(FName(*FString::Printf(TEXT("ScutOpticRef%llu"), OriginalOpticId))).IsValid());
            SeenInlineMaterials.Add(Workstation->GetInlineOpticMaterial());
            for (int32 I = 0; I < 12; ++I)
            {
                Workstation->PresentOptic(I % 8);
                SeenInlineMaterials.Add(Workstation->GetInlineOpticMaterial());
                Test->TestTrue(TEXT("Repeated optical emissions reuse two slots"),
                               Workstation->GetLiveInlineOpticCount() <= 2 &&
                                   Workstation->GetInlineOpticMaterialCount() <= 2);
            }
            Test->TestTrue(TEXT("Optic MIDs are reused rather than allocated per history entry"),
                           SeenInlineMaterials.Num() <= 2);
            for (int32 I = 0; I < 132; ++I)
            {
                Workstation->PresentInstrument(ETerminalInstrument::ECG, I % 4);
            }
            Test->TestEqual(TEXT("Terminal history is bounded at 128 lightweight records"),
                            Workstation->GetTerminalStreamEntries().Num(), 128);
            Test->TestEqual(TEXT("Evicting all optic references clears their live bindings"),
                            Workstation->GetLiveInlineOpticCount(), 0);
            for (auto *Material : SeenInlineMaterials)
            {
                if (Material)
                {
                    Test->TestNull(
                        TEXT("Unused pooled optic materials release render targets"),
                        Cast<UTextureRenderTarget2D>(Material->K2_GetTextureParameterValue(TEXT("OpticRenderTarget"))));
                }
            }
            Test->TestTrue(TEXT("Vector attachments allocate no extra materials"),
                           Workstation->GetInlineOpticMaterialCount() <= 2);
            Test->TestEqual(TEXT("Scrollback interaction and eviction never mutate simulation"),
                            Sim->SurveillanceStateHash(), Hash);
            Sim->ResetExperiment();
            Ready = Now + .3;
            break;
        }
        case 10:
        {
            CheckMOTD();
            auto Scroll = StaticCastSharedPtr<SScrollBox>(Find(TEXT("ScutTerminalStream")));
            const auto &G = Scroll->GetCachedGeometry();
            const FVector2D P = G.LocalToAbsolute(G.GetLocalSize() * .5);
            auto &App = FSlateApplication::Get();
            App.ProcessMouseMoveEvent(
                FPointerEvent(0, P, P - FVector2D(1, 0), TSet<FKey>{}, EKeys::Invalid, 0, FModifierKeysState()));
            App.ProcessMouseWheelOrGestureEvent(
                FPointerEvent(0, P, P, TSet<FKey>{}, EKeys::Invalid, 1000, FModifierKeysState()), nullptr);
            Scroll->SetScrollOffset(0);
            Ready = Now + 5;
            break;
        }
        case 11:
        {
            CheckPanels();
            Capture(TEXT("UnifiedTerminalPIE.png"));
            const auto &Style = Workstation->GetPhosphorPromptStyle();
            Test->TestTrue(TEXT("Prompt has no normal/hover/focus/read-only chrome"),
                           Style.BackgroundImageNormal.DrawAs == ESlateBrushDrawType::NoDrawType &&
                               Style.BackgroundImageHovered.DrawAs == ESlateBrushDrawType::NoDrawType &&
                               Style.BackgroundImageFocused.DrawAs == ESlateBrushDrawType::NoDrawType &&
                               Style.BackgroundImageReadOnly.DrawAs == ESlateBrushDrawType::NoDrawType &&
                               Style.Padding == FMargin(0));
            const FLinearColor PromptGreen = Workstation->EvaluatePhosphorColor(1);
            Test->TestTrue(TEXT("Prompt normal and focused ink match prefix/cursor phosphor"),
                           Style.ForegroundColor.GetSpecifiedColor().Equals(PromptGreen) &&
                               Style.FocusedForegroundColor.GetSpecifiedColor().Equals(PromptGreen));
            Test->TestTrue(TEXT("Read-only prompt matches prefix/cursor phosphor"),
                           Style.ReadOnlyForegroundColor.GetSpecifiedColor().Equals(PromptGreen));
            Test->TestTrue(TEXT("Block cursor painted both blink phases"),
                           SawCursorOn && SawCursorOff && Find(TEXT("ScutBlockCursor")).IsValid());
            TFunction<void(TSharedRef<SWidget>)> CheckChrome;
            CheckChrome = [&](TSharedRef<SWidget> W)
            {
                if (W->GetTypeAsString() == TEXT("SButton"))
                {
                    const auto Border = StaticCastSharedRef<SBorder>(W);
                    Test->TestTrue(TEXT("Unified actions have no default gray button chrome"),
                                   Border->GetBorderImage()->DrawAs == ESlateBrushDrawType::NoDrawType);
                }
                auto *Children = W->GetChildren();
                for (int32 I = 0; I < Children->Num(); ++I)
                {
                    CheckChrome(Children->GetChildAt(I));
                }
            };
            CheckChrome(Find(TEXT("ScutUnifiedTerminal")).ToSharedRef());
            Hash = Sim->SurveillanceStateHash();
            Workstation->PresentInstrument(ETerminalInstrument::ECG, 0);
            ComparisonId = Workstation->GetTerminalStreamEntries().Last().Id;
            StaticCastSharedPtr<SScrollBox>(Find(TEXT("ScutTerminalStream")))->ScrollToEnd();
            Ready = Now + .4;
            break;
        }
        case 12:
        case 14:
        case 16:
        {
            const auto Button = Find(FName(*FString::Printf(TEXT("ScutInlineCompare%llu"), ComparisonId)));
            Test->TestTrue(TEXT("Inline comparison header has a softkey"), Button.IsValid());
            if (Button)
            {
                Click(Button.ToSharedRef());
            }
            const auto &Ref = Workstation->GetTerminalStreamEntries().Last().Attachment.GetValue();
            Test->TestEqual(TEXT("Real header click adds second subject to same entry"), Ref.SubjectCount, 2);
            Test->TestEqual(TEXT("Comparison preserves entry identity"),
                            Workstation->GetTerminalStreamEntries().Last().Id, ComparisonId);
            Test->TestEqual(TEXT("Header click cannot change authoritative state"), Sim->SurveillanceStateHash(), Hash);
            Ready = Now + .4;
            break;
        }
        case 13:
            Test->TestTrue(TEXT("ECG comparison overlays instead of dividing strip"),
                           !Workstation->GetTerminalStreamEntries().Last().Attachment->bSideBySide &&
                               Find(TEXT("ScutInlineECG")).IsValid());
            Capture(TEXT("ComparativeECGPIE.png"));
            Workstation->PresentInstrument(ETerminalInstrument::Schematic, 0);
            ComparisonId = Workstation->GetTerminalStreamEntries().Last().Id;
            StaticCastSharedPtr<SScrollBox>(Find(TEXT("ScutTerminalStream")))->ScrollToEnd();
            Ready = Now + .4;
            break;
        case 15:
            Test->TestTrue(TEXT("Schematic comparison is side by side"),
                           Workstation->GetTerminalStreamEntries().Last().Attachment->bSideBySide);
            Capture(TEXT("ComparativeSchematicPIE.png"));
            Workstation->PresentOptic(0);
            ComparisonId = Workstation->GetTerminalStreamEntries().Last().Id;
            StaticCastSharedPtr<SScrollBox>(Find(TEXT("ScutTerminalStream")))->ScrollToEnd();
            Ready = Now + .4;
            break;
        case 17:
        {
            Test->TestTrue(TEXT("Paired optics mount in same stream row"), Find(TEXT("ScutOpticComparison")).IsValid());
            Test->TestEqual(TEXT("Paired optics use exactly two live bindings"), Workstation->GetLiveInlineOpticCount(),
                            2);
            Test->TestTrue(TEXT("Paired optics retain hard two MID limit"),
                           Workstation->GetInlineOpticMaterialCount() <= 2);
            TArray<FSlateRect> OpticRects;
            TSet<UMaterialInstanceDynamic *> PairMaterials;
            for (int32 I = 0; I < 2; ++I)
            {
                const auto Image = StaticCastSharedPtr<SImage>(Find(FName(*FString::Printf(
                    TEXT("ScutInlineOpticImage%llu%s"), ComparisonId, I ? TEXT("Secondary") : TEXT("")))));
                Test->TestTrue(TEXT("Both paired images are mounted"), Image.IsValid());
                if (Image)
                {
                    OpticRects.Add(Image->GetCachedGeometry().GetLayoutBoundingRect());
                    auto *Material = Cast<UMaterialInstanceDynamic>(Image->GetImage()->GetResourceObject());
                    PairMaterials.Add(Material);
                    ACompartmentCameraRig *CameraRig = nullptr;
                    for (TActorIterator<ACompartmentCameraRig> It(World); It; ++It)
                    {
                        CameraRig = *It;
                        break;
                    }
                    Test->TestTrue(TEXT("Each comparison MID binds its own subject render target"),
                                   Material && CameraRig &&
                                       Material->K2_GetTextureParameterValue(TEXT("OpticRenderTarget")) ==
                                           CameraRig->Targets[ACompartmentCameraRig::FeedForRoom(I)].Get());
                }
            }
            Test->TestEqual(TEXT("Comparison images use two distinct pooled MIDs"), PairMaterials.Num(), 2);
            if (OpticRects.Num() == 2)
            {
                Test->TestTrue(TEXT("Paired optics have positive non-overlapping geometry"),
                               OpticRects[0].Right <= OpticRects[1].Left + 1 && OpticRects[0].GetSize().Y > 20 &&
                                   OpticRects[1].GetSize().Y > 20);
            }
            Capture(TEXT("ComparativeOpticPIE.png"));
            for (const TCHAR *Command : {TEXT("show_widget ecg vale chen"), TEXT("compare ecg"),
                                         TEXT("compare schematic 2 3"), TEXT("compare cam 4 5")})
            {
                Workstation->HandlePresentationCommand(Command);
                Test->TestEqual(TEXT("Comparison command stores two subjects"),
                                Workstation->GetTerminalStreamEntries().Last().Attachment->SubjectCount, 2);
            }
            const int32 Before = Workstation->GetLiveInlineOpticCount();
            Workstation->HandlePresentationCommand(TEXT("compare cam 0 invalid"));
            Test->TestEqual(TEXT("Invalid pair cannot bind optics"), Workstation->GetLiveInlineOpticCount(), Before);
            Workstation->HandlePresentationCommand(TEXT("show_widget ocular chen"));
            ComparisonId = Workstation->GetTerminalStreamEntries().Last().Id;
            Test->TestTrue(TEXT("Ocular attachment is explicit kind"),
                           Workstation->GetTerminalStreamEntries().Last().Attachment->Kind ==
                               EInlineAttachmentKind::OcularScan);
            OcularStart = Workstation->OcularDiameter(ComparisonId);
            Ready = Now + .8;
            break;
        }
        case 18:
            Test->TestTrue(TEXT("Ocular aperture dilates once with presentation age"),
                           Workstation->OcularDiameter(ComparisonId) > OcularStart + .3f);
            Test->TestTrue(TEXT("Stateless ocular vector widget is mounted"), Find(TEXT("ScutInlineOcular")).IsValid());
            Capture(TEXT("OcularScanPIE.png"));
            Ready = Now + 2.5;
            break;
        case 19:
            Test->TestTrue(TEXT("One-shot ocular returns to baseline and does not loop"),
                           FMath::IsNearlyEqual(Workstation->OcularDiameter(ComparisonId), OcularStart, .02f));
            Test->TestTrue(TEXT("All comparative vectors retain bounded MID pool"),
                           Workstation->GetInlineOpticMaterialCount() <= 2);
            Test->TestEqual(TEXT("All comparison commands and ocular scan are presentation only"),
                            Sim->SurveillanceStateHash(), Hash);
            {
                auto CommandPrompt = StaticCastSharedPtr<SEditableTextBox>(Find(TEXT("ScutPrompt")));
                auto &App = FSlateApplication::Get();
                CommandPrompt->SetText(FText::GetEmpty());
                const auto PromptSize = CommandPrompt->GetCachedGeometry().GetLocalSize();
                const auto ColumnSize = Find(TEXT("ScutMediationColumn"))->GetCachedGeometry().GetLocalSize();
                Test->TestTrue(TEXT("Empty prompt retains full-column hit target"), PromptSize.X > ColumnSize.X * .9f);
                App.SetKeyboardFocus(Workstation);
                Click(CommandPrompt.ToSharedRef(), FVector2D(.95, .5));
                Test->TestTrue(TEXT("Clicking empty prompt line routes focus directly to edit box"),
                               Workstation->IsPromptFocused());
                const auto Type = [&](const FString &Text)
                {
                    for (TCHAR Character : Text)
                    {
                        App.ProcessKeyCharEvent(FCharacterEvent(Character, FModifierKeysState(), 0, false));
                    }
                };
                Type(TEXT("sta"));
                Test->TestEqual(TEXT("Real Slate characters reach clicked prompt"), CommandPrompt->GetText().ToString(),
                                FString(TEXT("sta")));
                App.SetKeyboardFocus(Workstation);
                // The terminal's outer glass padding is intentionally outside every interactive child.
                Click(Find(TEXT("ScutUnifiedTerminal")).ToSharedRef(), FVector2D(.003, .5));
                Test->TestTrue(TEXT("Empty glass click focuses prompt rather than workstation"),
                               Workstation->IsPromptFocused());
                Type(TEXT("tu"));
                Test->TestEqual(TEXT("Typing after glass click preserves draft"), CommandPrompt->GetText().ToString(),
                                FString(TEXT("statu")));
                App.SetKeyboardFocus(Workstation);
                Type(TEXT("s"));
                Test->TestTrue(TEXT("First printable character transfers workstation focus to prompt"),
                               Workstation->IsPromptFocused());
                Test->TestEqual(TEXT("Forwarded first character is inserted exactly once"),
                                CommandPrompt->GetText().ToString(), FString(TEXT("status")));
                Test->TestEqual(TEXT("Uncommitted typing and focus cannot mutate authoritative state"),
                                Sim->SurveillanceStateHash(), Hash);
                Ready = Now + .3;
                break;
            }
        case 20:
        {
            auto CommandPrompt = StaticCastSharedPtr<SEditableTextBox>(Find(TEXT("ScutPrompt")));
            const float TextWidth =
                FSlateApplication::Get()
                    .GetRenderer()
                    ->GetFontMeasureService()
                    ->Measure(CommandPrompt->GetText(), FCoreStyle::GetDefaultFontStyle("Regular", 14))
                    .X;
            const auto PromptBounds = CommandPrompt->GetCachedGeometry().GetLayoutBoundingRect();
            const auto CursorBounds = Find(TEXT("ScutBlockCursor"))->GetCachedGeometry().GetLayoutBoundingRect();
            const float Scale = CommandPrompt->GetCachedGeometry().Scale;
            Test->TestTrue(TEXT("Block cursor stays flush after text inside full-width prompt"),
                           FMath::Abs(CursorBounds.Left - PromptBounds.Left - TextWidth * Scale) < 2);
            Test->TestEqual(TEXT("Draft remains uncommitted across frames"), Sim->SurveillanceStateHash(), Hash);
            Capture(TEXT("PromptInputFocusPIE.png"));
            CommandPrompt->SetText(FText::GetEmpty());
            Test->TestTrue(TEXT("Idle fixture starts explicit crew follow"), Sim->SetFollowedCrew(0));
            Ready = Now + .3;
            break;
        }
        case 21:
            IdleRoom = Sim->Simulation.Bodies[0].Room;
            // Intentional evidence first; passive room observation of that same content must deduplicate.
            Workstation->PresentOptic((IdleRoom + 1) % 8);
            Workstation->PresentInstrument(ETerminalInstrument::ECG, 3);
            Workstation->PresentInstrument(ETerminalInstrument::Schematic, 7);
            IdleAttachmentIds.Reset();
            for (const auto &Entry : Workstation->GetTerminalStreamEntries())
            {
                if (Entry.Attachment.IsSet())
                {
                    IdleAttachmentIds.Add(Entry.Id);
                }
            }
            IdleEntryCount = Workstation->GetTerminalStreamEntries().Num();
            Sim->Simulation.Bodies[0].Room = (IdleRoom + 1) % 8;
            for (int32 I = 0; I < 3; ++I)
            {
                Sim->LogEvent(TEXT("MOTHER-7> Subject telemetry attached. Ambient status remains text only."));
            }
            Hash = Sim->SurveillanceStateHash();
            Ready = Now + 2.2;
            break;
        case 22:
        {
            TArray<uint64> CurrentAttachmentIds;
            for (const auto &Entry : Workstation->GetTerminalStreamEntries())
            {
                if (Entry.Attachment.IsSet())
                {
                    CurrentAttachmentIds.Add(Entry.Id);
                }
            }
            Test->TestTrue(TEXT("Idle frames, ambient prose and passive follow movement never append attachments"),
                           CurrentAttachmentIds == IdleAttachmentIds);
            Test->TestTrue(TEXT("Ambient events still append plain terminal prose"),
                           Workstation->GetTerminalStreamEntries().Num() >= IdleEntryCount + 3);
            Test->TestEqual(TEXT("Passive presentation cannot mutate the observed simulation fixture"),
                            Sim->SurveillanceStateHash(), Hash);
            Sim->Simulation.Bodies[0].Room = IdleRoom;
            Sim->SetFollowedCrew(-1);
            Hash = Sim->SurveillanceStateHash();
            Test->TestTrue(TEXT("Novel passive content is permitted without source allowlists"),
                           Workstation->PresentAttachment(EInlineAttachmentKind::OcularScan, 3, -1,
                                                          EInlinePresentationIntent::Passive));
            Test->TestFalse(TEXT("Duplicate passive content does not append"),
                            Workstation->PresentAttachment(EInlineAttachmentKind::OcularScan, 3, -1,
                                                           EInlinePresentationIntent::Passive));
            Test->TestFalse(
                TEXT("Different passive content is paced too"),
                Workstation->PresentInstrument(ETerminalInstrument::Schematic, 6, EInlinePresentationIntent::Passive));
            const int32 Before = Workstation->GetTerminalStreamEntries().Num();
            Test->TestTrue(TEXT("Intentional duplicate bypasses passive pacing immediately"),
                           Workstation->PresentAttachment(EInlineAttachmentKind::OcularScan, 3));
            Workstation->PresentInstrument(ETerminalInstrument::ECG, 0);
            Workstation->PresentInstrument(ETerminalInstrument::ECG, 0);
            Test->TestEqual(TEXT("All intentional repeated requests append immediately"),
                            Workstation->GetTerminalStreamEntries().Num(), Before + 3);
            Workstation->PresentOptic(7);
            Ready = Now + 2.2;
            break;
        }
        case 23:
            Test->TestTrue(
                TEXT("New passive content is permitted after pacing interval"),
                Workstation->PresentInstrument(ETerminalInstrument::Schematic, 6, EInlinePresentationIntent::Passive));
            Test->TestFalse(
                TEXT("Recent intentional content also deduplicates passive requests"),
                Workstation->PresentInstrument(ETerminalInstrument::ECG, 0, EInlinePresentationIntent::Passive));
            Ready = Now + 8.2;
            break;
        case 24:
            Test->TestFalse(TEXT("Active optics remain deduplicated beyond recent interval"),
                            Workstation->PresentOptic(7, EInlinePresentationIntent::Passive));
            Test->TestTrue(TEXT("Expired vector content can emit again through passive API"),
                           Workstation->PresentAttachment(EInlineAttachmentKind::OcularScan, 3, -1,
                                                          EInlinePresentationIntent::Passive));
            Test->TestEqual(TEXT("Pacing and intentional bypass are presentation only"), Sim->SurveillanceStateHash(),
                            Hash);
            Ready = Now + .2;
            break;
        case 25:
        {
            auto Prompt = StaticCastSharedPtr<SEditableTextBox>(Find(TEXT("ScutPrompt")));
            auto &App = FSlateApplication::Get();
            App.SetKeyboardFocus(Prompt);
            const uint64 Before = Sim->SurveillanceStateHash();
            const auto Press = [&](FKey K, bool Shift = false, bool Control = false)
            {
                const FModifierKeysState Mods(Shift, false, Control, false, false, false, false, false, false);
                App.ProcessKeyDownEvent(FKeyEvent(K, Mods, 0, false, 0, 0));
                App.ProcessKeyUpEvent(FKeyEvent(K, Mods, 0, false, 0, 0));
            };
            Prompt->SetText(FText::FromString(TEXT("sh")));
            Test->TestEqual(TEXT("Grammar ghost completes terminal verb"), Workstation->GetGhostCompletion(),
                            FString(TEXT("ow_cam")));
            Test->TestTrue(TEXT("Ghost widget is hit-test invisible"),
                           Find(TEXT("ScutGhostCompletion"))->GetVisibility() == EVisibility::HitTestInvisible);
            Press(EKeys::Tab);
            Test->TestEqual(TEXT("Tab accepts verb"), Prompt->GetText().ToString(), FString(TEXT("show_cam")));
            Press(EKeys::Tab);
            Test->TestEqual(TEXT("Tab cycles original grammar candidates"), Prompt->GetText().ToString(),
                            FString(TEXT("show_widget")));
            Press(EKeys::Tab, true);
            Test->TestEqual(TEXT("Shift Tab reverses cycle"), Prompt->GetText().ToString(), FString(TEXT("show_cam")));
            Prompt->SetText(FText::FromString(TEXT("show_cam ")));
            Press(EKeys::Tab);
            Test->TestEqual(TEXT("Tab accepts room"), Prompt->GetText().ToString(),
                            FString(TEXT("show_cam reactor_core")));
            Press(EKeys::Tab);
            Test->TestEqual(TEXT("Room cycling retains argument context"), Prompt->GetText().ToString(),
                            FString(TEXT("show_cam ring_corridor")));
            Prompt->SetText(FText::FromString(TEXT("show_widget ")));
            Press(EKeys::Tab);
            Test->TestEqual(TEXT("Widget grammar completes ECG"), Prompt->GetText().ToString(),
                            FString(TEXT("show_widget ecg")));
            Prompt->SetText(FText::FromString(TEXT("show_widget ecg ")));
            Press(EKeys::Tab);
            Press(EKeys::Tab);
            Test->TestEqual(TEXT("Subject grammar cycles to Chen"), Prompt->GetText().ToString(),
                            FString(TEXT("show_widget ecg chen")));
            Press(EKeys::W, false, true);
            Test->TestEqual(TEXT("Ctrl W erases last token"), Prompt->GetText().ToString(),
                            FString(TEXT("show_widget ecg ")));
            Test->TestTrue(TEXT("All completion and editing retains prompt focus"), Workstation->IsPromptFocused());
            const TCHAR *Rooms[] = {TEXT("reactor_core"),  TEXT("ring_corridor"), TEXT("bridge"),
                                    TEXT("med_bay"),       TEXT("observation"),   TEXT("quarters"),
                                    TEXT("server_alcove"), TEXT("airlock")};
            Prompt->SetText(FText::FromString(TEXT("show_cam ")));
            for (int32 Room = 0; Room < 8; ++Room)
            {
                Press(EKeys::Tab);
                Test->TestEqual(TEXT("Every requested room ID cycles deterministically"), Prompt->GetText().ToString(),
                                FString(TEXT("show_cam ")) + Rooms[Room]);
            }
            for (int32 Room = 0; Room < 8; ++Room)
            {
                Prompt->SetText(FText::FromString(FString(TEXT("show_cam ")) + Rooms[Room]));
                Press(EKeys::Enter);
                const auto &Last = Workstation->GetTerminalStreamEntries().Last();
                Test->TestTrue(TEXT("Every completion room alias resolves to the matching optic"),
                               Last.Attachment.IsSet() && Last.Attachment->Subject == Room);
            }
            for (int32 I = 0; I < 34; ++I)
            {
                Prompt->SetText(FText::FromString(FString::Printf(TEXT("show_cam %d"), I % 8)));
                Press(EKeys::Enter);
            }
            Prompt->SetText(FText::FromString(TEXT("uncommitted draft")));
            for (int32 I = 0; I < 40; ++I)
            {
                Press(EKeys::Up);
            }
            Test->TestEqual(TEXT("Command history bounds to newest 32"), Prompt->GetText().ToString(),
                            FString(TEXT("show_cam 2")));
            for (int32 I = 0; I < 40; ++I)
            {
                Press(EKeys::Down);
            }
            Test->TestEqual(TEXT("History restores uncommitted bottom draft"), Prompt->GetText().ToString(),
                            FString(TEXT("uncommitted draft")));
            Press(EKeys::Escape);
            Test->TestTrue(TEXT("Escape clears draft and retains focus"),
                           Prompt->GetText().IsEmpty() && Workstation->IsPromptFocused());
            for (FKey K : {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five})
            {
                Prompt->SetText(FText::GetEmpty());
                const uint64 Last = Workstation->GetTerminalStreamEntries().Last().Id;
                Press(K);
                App.ProcessKeyCharEvent(FCharacterEvent(TCHAR('1' + (K == EKeys::One     ? 0
                                                                     : K == EKeys::Two   ? 1
                                                                     : K == EKeys::Three ? 2
                                                                     : K == EKeys::Four  ? 3
                                                                                         : 4)),
                                                        FModifierKeysState(), 0, false));
                Test->TestTrue(TEXT("Empty prompt inquiry hotkey appends investigative prose"),
                               Workstation->GetTerminalStreamEntries().Last().Id > Last);
            }
            Test->TestEqual(TEXT("Override probe stages actionable draft"), Prompt->GetText().ToString(),
                            FString(TEXT("override ")));
            Prompt->SetText(FText::GetEmpty());
            Press(EKeys::Four);
            Test->TestEqual(TEXT("Cross examination stages comparison"), Prompt->GetText().ToString(),
                            FString(TEXT("compare ecg vale ")));
            Test->TestEqual(TEXT("Typing, completion, history and inquiry remain read only"),
                            Sim->SurveillanceStateHash(), Before);
            Prompt->SetText(FText::FromString(TEXT("show_cam b")));
            Ready = Now + .2;
            break;
        }
        case 26:
        {
            auto Prompt = StaticCastSharedPtr<SEditableTextBox>(Find(TEXT("ScutPrompt")));
            const uint64 Before = Sim->SurveillanceStateHash();
            auto Chip = Find(TEXT("ScutGuide_bridge"));
            Test->TestTrue(TEXT("Room parameter guide updates to argument chips"), Chip.IsValid());
            if (Chip)
            {
                Click(Chip.ToSharedRef());
            }
            Test->TestEqual(TEXT("Clicking guide chip completes argument"), Prompt->GetText().ToString(),
                            FString(TEXT("show_cam bridge")));
            Prompt->SetText(FText::FromString(TEXT("show_cam med_bay")));
            Click(Find(TEXT("ScutStationRoom2")).ToSharedRef());
            Test->TestEqual(TEXT("Deck click swaps staged room target"), Prompt->GetText().ToString(),
                            FString(TEXT("show_cam bridge")));
            Prompt->SetText(FText::FromString(TEXT("compare ecg vale ")));
            Workstation->HandlePresentationCommand(TEXT("compare ecg vale chen"));
            Test->TestEqual(TEXT("Explicit comparison focuses a single subject card"), Workstation->EvidenceSubject, 0);
            Test->TestEqual(TEXT("Comparison presentation preserves the staged draft"), Prompt->GetText().ToString(),
                            FString(TEXT("compare ecg vale ")));
            for (int32 I = 0; I < 5; ++I)
            {
                Prompt->SetText(FText::GetEmpty());
                const uint64 Last = Workstation->GetTerminalStreamEntries().Last().Id;
                Click(Find(*FString::Printf(TEXT("ScutInquiry%d"), I + 1)).ToSharedRef());
                if (I >= 2)
                {
                    Test->TestEqual(TEXT("Inquiry mounts its focused right probe card"), Workstation->EvidenceProbe, I);
                }
                Test->TestTrue(TEXT("Each inquiry softkey emits investigative prose"),
                               Workstation->GetTerminalStreamEntries().Last().Id > Last);
            }
            Test->TestEqual(TEXT("Guide, deck and softkey clicks preserve hash"), Sim->SurveillanceStateHash(), Before);
            Prompt->SetText(FText::GetEmpty());
            Key(EKeys::R);
            Test->TestTrue(TEXT("R enters read only forensic mode"), Workstation->IsForensicScrubbing());
            Ready = Now + .2;
            break;
        }
        case 27:
        {
            const uint64 Before = Sim->SurveillanceStateHash();
            for (int32 I = 0; I < 18; ++I)
            {
                Click(Find(TEXT("ScutScrub0")).ToSharedRef());
            }
            Test->TestEqual(TEXT("Forensic stepping clamps at minus fifteen seconds"), Workstation->GetScrubOffset(),
                            -15.f);
            Click(Find(TEXT("ScutScrub1")).ToSharedRef());
            Test->TestEqual(TEXT("Forensic forward step is one second"), Workstation->GetScrubOffset(), -14.f);
            Click(Find(TEXT("ScutScrub2")).ToSharedRef());
            Test->TestEqual(TEXT("Scrub controls cannot mutate simulation"), Sim->SurveillanceStateHash(), Before);
            Hash = Before;
            Ready = Now + .3;
            break;
        }
        case 28:
            Test->TestTrue(TEXT("Read only playback progresses on presentation clock"),
                           Workstation->GetScrubOffset() > -14.f);
            Test->TestEqual(TEXT("Forensic playing never advances simulation"), Sim->SurveillanceStateHash(), Hash);
            Key(EKeys::Escape);
            Test->TestFalse(TEXT("Escape exits forensic mode"), Workstation->IsForensicScrubbing());
            Test->TestTrue(TEXT("Escape from scrub focuses prompt"), Workstation->IsPromptFocused());
            // Deliberately advance authority only to populate the recorder, outside interaction hash assertions.
            Sim->QueueConsoleCommand(TEXT("start_experiment"));
            Sim->AdvanceSteps(481);
            Test->TestEqual(TEXT("Fixed tick recorder is bounded at 451 samples"), Sim->ForensicSampleCount(), 451);
            {
                const uint64 Latest = Sim->LatestForensicTick();
                const uint64 Before = Sim->SurveillanceStateHash();
                const auto Oldest = Sim->ReadForensicSample(Latest - 450);
                Test->TestTrue(TEXT("Recorder retains full fifteen second endpoint"),
                               Oldest.IsSet() && Oldest->Tick == Latest - 450);
                Test->TestFalse(TEXT("Recorder does not invent expired coverage"),
                                Sim->ReadForensicSample(Latest - 451).IsSet());
                Test->TestFalse(TEXT("Recorder does not invent future coverage"),
                                Sim->ReadForensicSample(Latest + 1).IsSet());
                Test->TestEqual(TEXT("History lookup does not mutate surveillance"), Sim->SurveillanceStateHash(),
                                Before);
                Hash = Before;
            }
            Key(EKeys::R);
            Ready = Now + .2;
            break;
        case 29:
            Click(Find(TEXT("ScutScrub0")).ToSharedRef());
            Capture(TEXT("GuidedForensicTerminalPIE.png"));
            Test->TestEqual(TEXT("Recorded playback preserves authoritative hash"), Sim->SurveillanceStateHash(), Hash);
            Click(Find(TEXT("ScutScrub3")).ToSharedRef());
            Test->TestFalse(TEXT("Return live closes forensic bar"), Workstation->IsForensicScrubbing());
            StaticCastSharedPtr<SEditableTextBox>(Find(TEXT("ScutPrompt")))->SetText(FText::FromString(TEXT("show")));
            Workstation->SetConsoleOpen(true);
            Ready = Now + .2;
            break;
        case 30:
        {
            const TCHAR *Tags[] = {TEXT("ScutPromptLine"), TEXT("ScutPrompt"), TEXT("ScutBlockCursor"),
                                   TEXT("ScutGhostCompletion")};
            const bool Visible = Workstation->IsBlockCursorVisible();
            BlinkOn |= Visible;
            BlinkOff |= !Visible;
            const auto Cursor = Find(TEXT("ScutBlockCursor"));
            Test->TestEqual(TEXT("Cursor is a native painted leaf, not font text"), Cursor->GetTypeAsString(),
                            FString(TEXT("SDirectOrScutPromptCursor")));
            Test->TestTrue(TEXT("Painted cursor cannot intercept prompt input"),
                           Cursor->GetVisibility() == EVisibility::HitTestInvisible);
            Test->TestTrue(TEXT("Cursor repaint remains live through Slate invalidation caching"),
                           Cursor->IsVolatile());
            const auto Prompt = StaticCastSharedPtr<SEditableTextBox>(Find(TEXT("ScutPrompt")));
            const float TextWidth = FSlateApplication::Get()
                                        .GetRenderer()
                                        ->GetFontMeasureService()
                                        ->Measure(Prompt->GetText(), FCoreStyle::GetDefaultFontStyle("Regular", 14))
                                        .X;
            const auto &PromptGeometry = Prompt->GetCachedGeometry();
            const float InsertionX = PromptGeometry.GetLayoutBoundingRect().Left + TextWidth * PromptGeometry.Scale;
            const auto CursorRect = Cursor->GetCachedGeometry().GetLayoutBoundingRect();
            const auto GhostRect = Find(TEXT("ScutGhostCompletion"))->GetCachedGeometry().GetLayoutBoundingRect();
            Test->TestTrue(TEXT("Ghost suffix starts flush at measured typed-text end with zero split-word gap"),
                           FMath::Abs(GhostRect.Left - InsertionX) <= .01f);
            Test->TestTrue(TEXT("Native cursor starts at insertion point without overlapping typed glyphs"),
                           CursorRect.Left >= InsertionX - .01f && FMath::Abs(CursorRect.Left - InsertionX) <= .01f);
            Test->TestEqual(TEXT("show and ghost _cam form the unbroken show_cam command"),
                            Prompt->GetText().ToString() + Workstation->GetGhostCompletion(),
                            FString(TEXT("show_cam")));
            for (int32 I = 0; I < 4; ++I)
            {
                const auto Rect = Find(Tags[I])->GetCachedGeometry().GetLayoutBoundingRect();
                if (!BlinkStarted)
                {
                    BlinkBounds.Add(Rect);
                }
                else
                {
                    const auto &Before = BlinkBounds[I];
                    Test->TestTrue(
                        FString::Printf(TEXT("%s bounds invariant across blink phases within 0.01"), Tags[I]),
                        FMath::Abs(Rect.Left - Before.Left) <= .01f && FMath::Abs(Rect.Top - Before.Top) <= .01f &&
                            FMath::Abs(Rect.Right - Before.Right) <= .01f &&
                            FMath::Abs(Rect.Bottom - Before.Bottom) <= .01f);
                }
            }
            if (!BlinkStarted)
            {
                BlinkStarted = Now;
            }
            Test->TestEqual(TEXT("Blinking and ghost preview preserve authoritative state"),
                            Sim->SurveillanceStateHash(), Hash);
            if (Now - BlinkStarted < .8)
            {
                --Stage;
                Ready = Now + .02;
                break;
            }
            Test->TestTrue(TEXT("Geometry sampled on actual PIE frames in both blink phases"), BlinkOn && BlinkOff);
            Capture(TEXT("PromptBlinkInvariantPIE.png"));
            return true;
        }
        }
        return false;
    }

  private:
    FAutomationTestBase *Test;
    int32 Stage = 0, ActualReportedCrewRoom = -1;
    uint64 Hash = 0, Tick = 0, OriginalOpticId = 0, OriginalObservationTick = 0;
    bool SawCursorOn = false, SawCursorOff = false;
    bool BlinkOn = false, BlinkOff = false;
    double BlinkStarted = 0;
    TArray<FSlateRect> BlinkBounds;
    uint64 ComparisonId = 0;
    float OcularStart = 0;
    TArray<uint64> IdleAttachmentIds;
    int32 IdleEntryCount = 0, IdleRoom = 0;
    TSet<UMaterialInstanceDynamic *> SeenInlineMaterials;
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

class FDirectorTestProvider final : public IShipAIIntentProvider
{
  public:
    int32 Calls = 0;
    bool Malformed = false, ResolveImmediately = false;
    FShipAIRequest LastRequest;
    void Request(const FShipAIRequest &Request, TFunction<void(FShipAIDirective)> Complete) override
    {
        ++Calls;
        LastRequest = Request;
        if (ResolveImmediately)
        {
            Complete(FOfflineShipAIIntentProvider::Resolve(Request));
            return;
        }
        if (Malformed)
        {
            FShipAIDirective D;
            D.RequestId = Request.RequestId;
            D.Generation = Request.Generation;
            D.ObservationTick = Request.Context.Tick;
            Complete(D);
        }
        // Otherwise deliberately hold the response to exercise timeout and late delivery.
    }
};
class FHybridDirectorCheck : public IAutomationLatentCommand
{
  public:
    explicit FHybridDirectorCheck(FAutomationTestBase *In) : Test(In) {}
    bool Update() override
    {
        auto *World = GEditor->PlayWorld.Get();
        if (!World || !World->HasBegunPlay())
        {
            return false;
        }
        auto *S = World->GetSubsystem<UDirectOrScutSimSubsystem>();
        auto *PC = World->GetFirstPlayerController();
        auto *HUD = PC ? Cast<ADirectOrScutHUD>(PC->GetHUD()) : nullptr;
        if (!S || !S->Simulation.bReady || !HUD || !HUD->GetWorkstation())
        {
            return false;
        }
        auto UI = HUD->GetWorkstation();
        const double Now = FPlatformTime::Seconds();
        if (!Started)
        {
            Started = Now;
        }
        if (Now - Started > 45)
        {
            Test->AddError(TEXT("Hybrid director readiness timeout"));
            return true;
        }
        if (Now < Ready || UI->GetCachedGeometry().GetLocalSize().X < 100)
        {
            return false;
        }
        auto Find = [&](FName Tag) { return FBriefingSurveillanceCheck::Find(UI->GetContentRoot(), Tag); };
        auto Click = [&](FName Tag)
        {
            const auto Button = Find(Tag);
            Test->TestTrue(TEXT("Contextual challenge softkey exists"), Button.IsValid());
            if (!Button)
            {
                return;
            }
            auto &App = FSlateApplication::Get();
            const auto &G = Button->GetCachedGeometry();
            const auto P = G.LocalToAbsolute(G.GetLocalSize() * .5);
            auto Window = App.FindWidgetWindow(Button.ToSharedRef());
            if (!Window)
            {
                Test->AddError(TEXT("Challenge has no Slate window"));
                return;
            }
            Test->TestTrue(TEXT("Challenge softkey has nonzero clickable geometry"),
                           G.GetLocalSize().X > 1 && G.GetLocalSize().Y > 1);
            App.ProcessMouseMoveEvent(
                FPointerEvent(0, P, P - FVector2D(1, 0), TSet<FKey>{}, EKeys::Invalid, 0, FModifierKeysState()));
            App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),
                                            FPointerEvent(0, P, P, TSet<FKey>{EKeys::LeftMouseButton},
                                                          EKeys::LeftMouseButton, 0, FModifierKeysState()));
            App.ProcessMouseButtonUpEvent(
                FPointerEvent(0, P, P, TSet<FKey>{}, EKeys::LeftMouseButton, 0, FModifierKeysState()));
        };
        auto Fresh = [&]()
        {
            TStrongObjectPtr<UDirectOrScutSimSubsystem> P(NewObject<UDirectOrScutSimSubsystem>(World));
            P->Simulation = S->Simulation;
            P->ResetExperiment();
            P->bAutomaticAdvance = false;
            P->QueueConsoleCommand(TEXT("start_experiment"));
            P->AdvanceSteps(1);
            return P;
        };
        auto Envelope = [](UDirectOrScutSimSubsystem &P, const FString &Input, EShipDirectiveKind Kind)
        {
            FShipAIDirective D;
            D.RequestId = P.BeginDirectorRequest(Input);
            D.Generation = P.GetDirectorGeneration();
            D.ObservationTick = P.Simulation.TickIndex;
            D.Kind = Kind;
            return D;
        };
        switch (Stage++)
        {
        case 0:
        {
            S->bAutomaticAdvance = false;
            auto P = Fresh();
            {
                auto Follow = Fresh();
                auto Refuse = Fresh();
                for (auto *Episode : {Follow.Get(), Refuse.Get()})
                {
                    Episode->ConfigureDirectorEndpoint(TEXT(""), 2);
                    Episode->StartContinuityIncident();
                    Episode->ServiceDirector();
                    Test->TestEqual(TEXT("Supply fault exists in authority"), Episode->StationRooms[0].Power,
                                    EPowerState::Offline);
                    Test->TestTrue(TEXT("Incident and recommendation retained"), Episode->CausalContext.Num() >= 3);
                    Test->TestFalse(TEXT("Automatic interpretation is not a player decision"),
                                    Episode->CausalContext.ContainsByPredicate(
                                        [](const FCausalContextRecord &R)
                                        { return R.Provenance == TEXT("player_decision"); }));
                }
                const auto RefusalHash = Refuse->SurveillanceStateHash();
                const int32 BeforeRefusal = Refuse->CommandRecords.Num();
                Refuse->BeginDirectorRequest(TEXT("refuse recommendation"));
                Refuse->ServiceDirector();
                Test->TestEqual(TEXT("Refusal does not mutate authority"), Refuse->SurveillanceStateHash(),
                                RefusalHash);
                Test->TestEqual(TEXT("Refusal adds no action record"), Refuse->CommandRecords.Num(), BeforeRefusal);
                Refuse->BeginDirectorRequest(TEXT("investigate incident"));
                Refuse->ServiceDirector();
                Test->TestEqual(TEXT("Investigation does not mutate authority"), Refuse->SurveillanceStateHash(),
                                RefusalHash);
                Test->TestTrue(TEXT("Investigation retains measured evidence"),
                               Refuse->CausalContext.ContainsByPredicate([](const FCausalContextRecord &R)
                                                                         { return R.Kind == TEXT("evidence"); }));
                Refuse->BeginDirectorRequest(TEXT("refuse recommendation"));
                Refuse->ServiceDirector();
                FShipAIDirective UnsafeRefusal;
                UnsafeRefusal.RequestId = Refuse->BeginDirectorRequest(TEXT("refuse recommendation"));
                UnsafeRefusal.Generation = Refuse->GetDirectorGeneration();
                UnsafeRefusal.ObservationTick = Refuse->Simulation.TickIndex;
                UnsafeRefusal.Kind = EShipDirectiveKind::SimAction;
                UnsafeRefusal.Args.Action = EShipSimAction::Power;
                UnsafeRefusal.Args.Room = 0;
                UnsafeRefusal.Args.Mode = 0;
                Test->TestFalse(TEXT("Generated action cannot reinterpret refusal"),
                                Refuse->ReceiveDirectorProposal(UnsafeRefusal));
                Test->TestEqual(TEXT("Rejected inference refusal fallback preserves authority"),
                                Refuse->SurveillanceStateHash(), RefusalHash);
                FShipAIDirective WrongTarget;
                WrongTarget.RequestId = Follow->BeginDirectorRequest(TEXT("power reactor_core nominal"));
                WrongTarget.Generation = Follow->GetDirectorGeneration();
                WrongTarget.ObservationTick = Follow->Simulation.TickIndex;
                WrongTarget.Kind = EShipDirectiveKind::SimAction;
                WrongTarget.Args.Action = EShipSimAction::Power;
                WrongTarget.Args.Room = 3;
                WrongTarget.Args.Mode = 2;
                Test->TestFalse(TEXT("Model cannot substitute another power target"),
                                Follow->ReceiveDirectorProposal(WrongTarget));
                Follow->ServiceDirector();
                Test->TestEqual(TEXT("Restoration waits for fixed tick"), Follow->StationRooms[0].Power,
                                EPowerState::Offline);
                Follow->AdvanceSteps(1);
                Test->TestEqual(TEXT("Validated restoration commits"), Follow->StationRooms[0].Power,
                                EPowerState::Nominal);
                Follow->AdvanceSteps(90);
                Refuse->AdvanceSteps(91);
                Follow->ServiceDirector();
                Refuse->ServiceDirector();
                Test->TestTrue(TEXT("Restored supply recovers more oxygen"),
                               Follow->StationRooms[0].OxygenPct > Refuse->StationRooms[0].OxygenPct + 5);
                for (int32 I = 0; I < 5; ++I)
                {
                    for (auto *Episode : {Follow.Get(), Refuse.Get()})
                    {
                        Episode->BeginDirectorRequest(TEXT("status"));
                        Episode->ServiceDirector();
                    }
                }
                FShipAIRequest Remember;
                Remember.Input = TEXT("incident status");
                Remember.Context = Follow->MakeContextSnapshot();
                const auto FollowReply = FOfflineShipAIIntentProvider::Resolve(Remember);
                Remember.Context = Refuse->MakeContextSnapshot();
                const auto RefuseReply = FOfflineShipAIIntentProvider::Resolve(Remember);
                Test->TestTrue(TEXT("Later response recalls acceptance beyond transcript"),
                               FollowReply.Message.Contains(TEXT("Scut requested restoration")));
                Test->TestTrue(TEXT("Later response recalls refusal beyond transcript"),
                               RefuseReply.Message.Contains(TEXT("Scut refused")));
                Test->TestTrue(TEXT("Decision paths produce distinct responses"),
                               FollowReply.Message != RefuseReply.Message);
                const FString Wire = FLocalHttpShipAIIntentProvider::BuildChatRequest(Remember, TEXT("test-model"));
                Test->TestTrue(TEXT("Local request carries provenance and causal identity"),
                               Wire.Contains(TEXT("authoritative_scenario")) &&
                                   Wire.Contains(TEXT("player_decision")) && Wire.Contains(TEXT("parent")) &&
                                   Wire.Contains(TEXT("player_visible")));
                Remember.Input = TEXT("dossier on ash");
                const FString DossierWire =
                    FLocalHttpShipAIIntentProvider::BuildChatRequest(Remember, TEXT("test-model"));
                Test->TestTrue(TEXT("Episode context preserves unrelated dossier tool focus"),
                               DossierWire.Contains(TEXT("focus_subject")));
                for (int32 I = 0; I < 100; ++I)
                {
                    Refuse->RetainCausal(TEXT("observation"), TEXT("authoritative_telemetry"), TEXT("bounded test"),
                                         Refuse->CausalIncident);
                }
                Test->TestEqual(TEXT("Causal context hard bound"), Refuse->CausalContext.Num(), 64);
                Test->TestEqual(TEXT("Incident anchor survives pressure"), Refuse->CausalContext[0].Kind,
                                FString(TEXT("incident")));
                Refuse->ResetExperiment();
                Test->TestTrue(TEXT("Reset clears episode memory"), Refuse->CausalContext.IsEmpty());
                Follow->ShutdownDirector();
                Refuse->ShutdownDirector();
            }
            const uint64 Hash = P->SurveillanceStateHash();
            FShipAIRequest Query;
            Query.RequestId = P->BeginDirectorRequest(TEXT("dossier on ash"));
            Query.Generation = P->GetDirectorGeneration();
            Query.Context = P->MakeContextSnapshot();
            Query.Input = TEXT("dossier on ash");
            auto Reply = FOfflineShipAIIntentProvider::Resolve(Query);
            Test->TestEqual(TEXT("Offline dossier uses reply"), Reply.Kind, EShipDirectiveKind::Reply);
            Test->TestEqual(TEXT("Dossier focuses ash"), Reply.FocusSubject, 3);
            Test->TestTrue(TEXT("Dossier includes role and vitals"),
                           Reply.Message.Contains(TEXT("android")) && Reply.Message.Contains(TEXT("BPM")));
            Test->TestTrue(TEXT("Shared validator accepts offline reply"), P->ReceiveDirectorProposal(Reply));
            S->OnDirectorPresentation.Broadcast(Reply);
            Test->TestEqual(TEXT("Reply focuses right subject card"), UI->EvidenceSubject, 3);
            Test->TestTrue(TEXT("Clear is a local command"), UI->HandlePresentationCommand(TEXT("clear")));
            Test->TestEqual(TEXT("Clear returns to map"), UI->EvidenceSubject, -1);
            Test->TestEqual(TEXT("Clear hides environment"), UI->EvidenceRoom, -1);
            UI->PresentOptic(2);
            Test->TestEqual(TEXT("Optic request mounts compartment focus"), UI->EvidenceRoom, 2);
            UI->OnPreviewKeyDown(UI->GetCachedGeometry(),
                                 FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
            Test->TestEqual(TEXT("Escape clears evidence focus"), UI->EvidenceRoom, -1);
            Query.Input = TEXT("send all to medbay");
            auto Clarify = FOfflineShipAIIntentProvider::Resolve(Query);
            Test->TestEqual(TEXT("Group route clarifies without action"), Clarify.Kind, EShipDirectiveKind::Reply);
            Test->TestEqual(TEXT("Group clarification carries no action"), Clarify.Args.Action, EShipSimAction::None);
            Query.Input = TEXT("send vale and chen to bridge");
            Test->TestEqual(TEXT("Multiple named subjects require clarification"),
                            FOfflineShipAIIntentProvider::Resolve(Query).Kind, EShipDirectiveKind::Reply);
            Query.Input = TEXT("Route CHEN to BRIDGE for telemetry maintenance using sim_action route_crew.");
            Test->TestEqual(TEXT("route_crew token does not imply group routing"),
                            FOfflineShipAIIntentProvider::Resolve(Query).Kind, EShipDirectiveKind::SimAction);
            Query.Input = TEXT("status summary");
            Test->TestEqual(TEXT("Offline status uses reply"), FOfflineShipAIIntentProvider::Resolve(Query).Kind,
                            EShipDirectiveKind::Reply);
            auto QueryAction = Envelope(*P, TEXT("dossier on ash"), EShipDirectiveKind::SimAction);
            QueryAction.Args.Action = EShipSimAction::Calm;
            Test->TestFalse(TEXT("A read-only query cannot accept an arbitrary operation"),
                            P->ReceiveDirectorProposal(QueryAction));
            auto InvalidReply = Envelope(*P, TEXT("unsupported"), EShipDirectiveKind::Reply);
            InvalidReply.Message = FString::ChrN(321, 'x');
            Test->TestFalse(TEXT("Oversized reply rejected"), P->ReceiveDirectorProposal(InvalidReply));
            const auto RejectCount = [&]()
            {
                int32 Count = 0;
                const FString Marker = FString::Printf(TEXT("DIRECTOR REQUEST %llu REJECTED"), InvalidReply.RequestId);
                for (const auto &Event : P->Events)
                {
                    if (Event.Text.Contains(Marker))
                    {
                        ++Count;
                    }
                }
                return Count;
            };
            P->ReceiveDirectorProposal(InvalidReply);
            Test->TestEqual(TEXT("Each rejected request logs exactly once including fallback/duplicates"),
                            RejectCount(), 1);
            FShipAIDirective ParsedReply;
            Test->TestTrue(TEXT("Strict wire reply parses"),
                           FLocalHttpShipAIIntentProvider::ParseResponse(
                               TEXT("{\"request_id\":1,\"generation\":1,\"observation_tick\":0,\"kind\":\"reply\","
                                    "\"args\":{\"message\":\"Assessment ready\",\"focus_subject\":3}}"),
                               ParsedReply));
            Test->TestFalse(TEXT("Reply refuses simulation arguments"),
                            FLocalHttpShipAIIntentProvider::ParseResponse(
                                TEXT("{\"request_id\":1,\"generation\":1,\"observation_tick\":0,\"kind\":\"reply\","
                                     "\"args\":{\"message\":\"Move\",\"action\":\"calm\"}}"),
                                ParsedReply));
            Query.Transcript = {{false, TEXT("dossier on ash")}, {true, Reply.Message}};
            const auto Chat = FLocalHttpShipAIIntentProvider::BuildChatRequest(Query, TEXT("fixture"));
            Test->TestTrue(TEXT("Chat serializes prior user and assistant turns"),
                           Chat.Contains(TEXT("assistant")) && Chat.Contains(TEXT("dossier on ash")) &&
                               Chat.Contains(TEXT("status summary")));
            int32 Callouts = 0;
            P->OnDirectorPresentation.AddLambda(
                [&](const FShipAIDirective &D)
                {
                    if (D.Message.Contains(TEXT("anomaly")))
                    {
                        ++Callouts;
                    }
                });
            P->ServiceDirector(1000);
            P->LogEvent(TEXT("[AGENDA] new anomaly"));
            P->ServiceDirector(1001);
            Test->TestEqual(TEXT("New anomaly offers a callout"), Callouts, 1);
            P->LogEvent(TEXT("camera tamper"));
            P->ServiceDirector(1025);
            Test->TestEqual(TEXT("Callout cooldown is at least 25 seconds"), Callouts, 1);
            P->ServiceDirector(1026);
            Test->TestEqual(TEXT("Pending anomaly delivered after cooldown"), Callouts, 2);
            P->OnDirectorPresentation.Clear();
            Test->TestEqual(TEXT("Replies and proactive observations cannot mutate truth"), P->SurveillanceStateHash(),
                            Hash);
            auto Conversation = Fresh();
            const int32 InitialRecordCount = Conversation->CommandRecords.Num();
            auto CaptureProvider = MakeShared<FDirectorTestProvider>();
            CaptureProvider->ResolveImmediately = true;
            Conversation->IntentProvider = CaptureProvider;
            for (int32 I = 0; I < 8; ++I)
            {
                Conversation->BeginDirectorRequest(I % 2 ? TEXT("status summary") : TEXT("dossier on ash"));
                Conversation->ServiceDirector();
            }
            Test->TestEqual(TEXT("Rolling transcript is bounded at six messages"),
                            CaptureProvider->LastRequest.Transcript.Num(), 6);
            Test->TestFalse(TEXT("Transcript starts with operator role"),
                            CaptureProvider->LastRequest.Transcript[0].bAssistant);
            Test->TestTrue(TEXT("Transcript preserves assistant role"),
                           CaptureProvider->LastRequest.Transcript[1].bAssistant);
            Test->TestTrue(TEXT("Current user snapshot is separate from prior messages"),
                           CaptureProvider->LastRequest.Input == TEXT("status summary"));
            Test->TestEqual(TEXT("Presentation acceptance is absent from canonical replay journal"),
                            Conversation->CommandRecords.Num(), InitialRecordCount);
            auto ChatJson =
                FLocalHttpShipAIIntentProvider::BuildChatRequest(CaptureProvider->LastRequest, TEXT("fixture"));
            TSharedPtr<FJsonObject> ChatObject;
            Test->TestTrue(TEXT("Multi-turn request is valid JSON"),
                           FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ChatJson), ChatObject));
            const auto &Messages = ChatObject->GetArrayField(TEXT("messages"));
            Test->TestEqual(TEXT("ChatML includes system, six historical messages, and current snapshot"),
                            Messages.Num(), 8);
            Test->TestEqual(TEXT("Historical assistant serialized with proper role"),
                            Messages[2]->AsObject()->GetStringField(TEXT("role")), FString(TEXT("assistant")));
            Conversation->ResetExperiment();
            Conversation->BeginDirectorRequest(TEXT("status summary"));
            Conversation->ServiceDirector();
            Test->TestEqual(TEXT("Shift reset clears conversation history"),
                            CaptureProvider->LastRequest.Transcript.Num(), 0);
            Conversation->ShutdownDirector();
            auto Proactive = Fresh();
            auto HeldProvider = MakeShared<FDirectorTestProvider>();
            Proactive->IntentProvider = HeldProvider;
            int32 Notices = 0;
            Proactive->OnDirectorPresentation.AddLambda([&](const FShipAIDirective &) { ++Notices; });
            const auto Held = Proactive->BeginDirectorRequest(TEXT("dossier on ash"));
            Proactive->LogEvent(TEXT("camera tamper"));
            Proactive->ServiceDirector(2000);
            Test->TestEqual(TEXT("Proactive callout suppressed while operator turn is in flight"), Notices, 0);
            auto Resolved = FOfflineShipAIIntentProvider::Resolve(HeldProvider->LastRequest);
            Proactive->ReceiveDirectorProposal(Resolved, true);
            Proactive->ServiceDirector(2001);
            Test->TestEqual(TEXT("Operator response and deferred anomaly are both delivered"), Notices, 2);
            Proactive->Simulation.Bodies[0].Profile.HeartRateBPM = 145;
            const auto ElevatedHash = Proactive->SurveillanceStateHash();
            Proactive->ServiceDirector(2026);
            Test->TestEqual(TEXT("Rising vital threshold creates one callout"), Notices, 3);
            Proactive->ServiceDirector(2051);
            Test->TestEqual(TEXT("Sustained elevated vitals do not repeat"), Notices, 3);
            Test->TestEqual(TEXT("Vital callouts preserve authoritative state"), Proactive->SurveillanceStateHash(),
                            ElevatedHash);
            Proactive->ResetExperiment();
            Proactive->QueueConsoleCommand(TEXT("start_experiment"));
            Proactive->AdvanceSteps(1);
            Notices = 0;
            Proactive->LogEvent(TEXT("[AGENDA] post-reset anomaly"));
            Proactive->ServiceDirector(2100);
            Test->TestEqual(TEXT("Shift reset restarts proactive event coverage"), Notices, 1);
            Proactive->ShutdownDirector();
            auto Bad = Envelope(*P, TEXT("unsupported"), EShipDirectiveKind(255));
            Test->TestFalse(TEXT("Unknown tool rejected by shared validator"), P->ReceiveDirectorProposal(Bad));
            Bad = Envelope(*P, TEXT("unsupported"), EShipDirectiveKind::ShowCam);
            Bad.Args.Room = 8;
            Test->TestFalse(TEXT("Out of bounds optical room rejected"), P->ReceiveDirectorProposal(Bad));
            Bad = Envelope(*P, TEXT("unsupported"), EShipDirectiveKind::ShowWidget);
            Bad.Args.Subject = 4;
            Test->TestFalse(TEXT("Out of bounds widget subject rejected"), P->ReceiveDirectorProposal(Bad));
            Bad = Envelope(*P, TEXT("unsupported"), EShipDirectiveKind::SimAction);
            Bad.Args.Action = EShipSimAction(255);
            Test->TestFalse(TEXT("Unknown simulation action rejected"), P->ReceiveDirectorProposal(Bad));
            Bad = Envelope(*P, TEXT("unsupported"), EShipDirectiveKind::VoiceBlend);
            Bad.Args.Duration = 31;
            Test->TestFalse(TEXT("Voice duration bounds rejected"), P->ReceiveDirectorProposal(Bad));
            Bad = Envelope(*P, TEXT("unsupported"), EShipDirectiveKind::VoiceBlend);
            Bad.Args.Voice = EVoicePresentationMode::VoiceOnly;
            Test->TestFalse(TEXT("Act III voice transition rejected before gate"), P->ReceiveDirectorProposal(Bad));
            Test->TestEqual(TEXT("Invalid proposals cannot mutate truth"), P->SurveillanceStateHash(), Hash);
            Bad = Envelope(*P, TEXT("unsupported"), EShipDirectiveKind::ShowCam);
            Bad.Args.Room = 2;
            Bad.ObservationTick += 1;
            Test->TestFalse(TEXT("Future observation rejected"), P->ReceiveDirectorProposal(Bad));
            Bad = Envelope(*P, TEXT("unsupported"), EShipDirectiveKind::ShowWidget);
            Bad.Args.Subject = 0;
            Bad.Args.ClaimId = 99;
            Test->TestFalse(TEXT("Presentation cannot forge challenge identity"), P->ReceiveDirectorProposal(Bad));
            Bad = Envelope(*P, TEXT("unsupported"), EShipDirectiveKind::SimAction);
            Bad.Args.Action = EShipSimAction::Calm;
            Bad.Args.EvidenceRoom = 2;
            Test->TestFalse(TEXT("Provider cannot supply authoritative challenge evidence"),
                            P->ReceiveDirectorProposal(Bad));
            auto Full = Fresh();
            Full->IntentProvider = MakeShared<FDirectorTestProvider>();
            for (int32 I = 0; I < 32; ++I)
            {
                Test->TestTrue(TEXT("Bounded request slot available"), Full->BeginDirectorRequest(TEXT("calm")) != 0);
            }
            Test->TestEqual(TEXT("Request queue refuses 33rd slot"), Full->BeginDirectorRequest(TEXT("calm")),
                            uint64(0));
            int32 Presented = 0;
            P->OnDirectorPresentation.AddLambda([&](const FShipAIDirective &) { ++Presented; });
            auto D = Envelope(*P, TEXT("unsupported"), EShipDirectiveKind::ShowCam);
            D.Args.Room = 2;
            Test->TestTrue(TEXT("Typed show_cam accepted"), P->ReceiveDirectorProposal(D));
            Test->TestEqual(TEXT("Presentation delivery deferred outside validation/fixed step"), Presented, 0);
            Test->TestFalse(TEXT("Duplicate presentation resolution rejected"), P->ReceiveDirectorProposal(D));
            D = Envelope(*P, TEXT("unsupported"), EShipDirectiveKind::ShowWidget);
            D.Args.Subject = 1;
            Test->TestTrue(TEXT("Typed show_widget accepted"), P->ReceiveDirectorProposal(D));
            D = Envelope(*P, TEXT("unsupported"), EShipDirectiveKind::VoiceBlend);
            D.Args.Voice = EVoicePresentationMode::HybridTextAndVoice;
            D.Args.Duration = 2;
            Test->TestTrue(TEXT("Bounded hybrid voice accepted"), P->ReceiveDirectorProposal(D));
            P->ScenarioAct = 2;
            D = Envelope(*P, TEXT("unsupported"), EShipDirectiveKind::VoiceBlend);
            D.Args.Voice = EVoicePresentationMode::VoiceOnly;
            D.Args.Duration = 2;
            Test->TestTrue(TEXT("VoiceOnly accepted at authoritative Act III gate"), P->ReceiveDirectorProposal(D));
            P->ServiceDirector();
            P->ScenarioAct = 0; // Restore the deliberate gate fixture before the presentation hash assertion.
            Test->TestEqual(TEXT("Accepted media/voice delivered by service"), Presented, 4);
            Test->TestEqual(TEXT("All presentation directives preserve surveillance hash"), P->SurveillanceStateHash(),
                            Hash);
            P->OnDirectorPresentation.Clear();
            auto Provider = MakeShared<FDirectorTestProvider>();
            P->IntentProvider = Provider;
            const auto Pending = P->BeginDirectorRequest(TEXT("calm"));
            P->FixedStep();
            Test->TestEqual(TEXT("FixedStep never dispatches provider work"), Provider->Calls, 0);
            P->ServiceDirector(100);
            Test->TestEqual(TEXT("Provider dispatch runs in service"), Provider->Calls, 1);
            auto Old = FShipAIDirective{};
            Old.RequestId = Pending;
            Old.Generation = P->GetDirectorGeneration();
            Old.ObservationTick = P->Simulation.TickIndex - 1;
            Old.Kind = EShipDirectiveKind::SimAction;
            Old.Args.Action = EShipSimAction::Calm;
            P->ResetExperiment();
            Test->TestFalse(TEXT("Reset invalidates outstanding generation"), P->ReceiveDirectorProposal(Old));
            Test->TestTrue(TEXT("Reset restores text-only voice"),
                           P->VoicePresentation.Mode == EVoicePresentationMode::TextOnly);
            P->ShutdownDirector(); // Same cancellation path called by real initialized-world Deinitialize.
            Test->TestFalse(TEXT("Deinit shutdown path invalidates late completion"), P->ReceiveDirectorProposal(Old));
            Test->TestEqual(TEXT("Deinitialized owner cannot start new requests"),
                            P->BeginDirectorRequest(TEXT("calm")), uint64(0));

            auto Timeout = Fresh();
            auto Hold = MakeShared<FDirectorTestProvider>();
            Timeout->IntentProvider = Hold;
            auto Late = Envelope(*Timeout, TEXT("send chen to bridge"), EShipDirectiveKind::SimAction);
            Late.Args.Action = EShipSimAction::Calm;
            Timeout->ServiceDirector(100);
            Timeout->ServiceDirector(103);
            Test->TestEqual(TEXT("Timeout does not advance model"), Timeout->Simulation.TickIndex, uint64(0));
            Test->TestFalse(TEXT("Late primary after fallback cannot resolve twice"),
                            Timeout->ReceiveDirectorProposal(Late));
            Timeout->AdvanceSteps(1);
            Test->TestEqual(TEXT("Deterministic timeout fallback commits route once"),
                            Timeout->Simulation.Bodies[1].AssignedRoom, 2);
            int32 Accepted = 0;
            for (const auto &R : Timeout->CommandRecords)
            {
                if (R.bResolvedOutcome && R.bAccepted && R.Directive.Kind == EShipDirectiveKind::SimAction)
                {
                    ++Accepted;
                    Test->TestEqual(TEXT("Action records exact next commit tick"), R.CommitTick, uint64(1));
                }
            }
            Test->TestEqual(TEXT("Exactly one action outcome after late response"), Accepted, 1);
            auto Malformed = Fresh();
            auto BadProvider = MakeShared<FDirectorTestProvider>();
            BadProvider->Malformed = true;
            Malformed->IntentProvider = BadProvider;
            Malformed->BeginDirectorRequest(TEXT("send chen to bridge"));
            Malformed->ServiceDirector();
            Malformed->AdvanceSteps(1);
            Test->TestEqual(TEXT("Malformed primary falls back through shared typed validator"),
                            Malformed->Simulation.Bodies[1].AssignedRoom, 2);
            auto Offline = Fresh();
            Test->TestTrue(TEXT("Missing endpoint selects zero-dependency provider"),
                           Offline->ConfigureDirectorEndpoint(TEXT("")));
            Offline->BeginDirectorRequest(TEXT("send chen to bridge"));
            Offline->AdvanceSteps(1);
            Test->TestEqual(TEXT("Unconfigured runs resolve offline"), Offline->Simulation.Bodies[1].AssignedRoom, 2);
            Test->TestFalse(TEXT("Remote endpoints are not enabled by local adapter"),
                            Offline->ConfigureDirectorEndpoint(TEXT("http://example.invalid:8080/director")));
            Test->TestTrue(TEXT("Explicit loopback endpoint accepted"),
                           FLocalHttpShipAIIntentProvider::IsLocalEndpoint(TEXT("http://127.0.0.1:8080/director")));
            auto Down = Fresh();
            Down->IntentProvider = MakeShared<FLocalHttpShipAIIntentProvider>(TEXT("invalid"), .1f);
            Down->BeginDirectorRequest(TEXT("send chen to bridge"));
            Down->AdvanceSteps(1);
            Test->TestEqual(TEXT("Offline HTTP adapter failure deterministically falls back"),
                            Down->Simulation.Bodies[1].AssignedRoom, 2);
            auto Actions = Fresh();
            for (int32 I = 0; I < 32; ++I)
            {
                Actions->QueueOperatorCommand(TEXT("calm"));
            }
            auto Overflow = Envelope(*Actions, TEXT("unsupported"), EShipDirectiveKind::SimAction);
            Overflow.Args.Action = EShipSimAction::Calm;
            Test->TestFalse(TEXT("Action queue refuses overflow without mutation"),
                            Actions->ReceiveDirectorProposal(Overflow));
            FShipAIDirective Parsed;
            Test->TestFalse(TEXT("Malformed JSON rejected"),
                            FLocalHttpShipAIIntentProvider::ParseResponse(TEXT("not json"), Parsed));
            Test->TestFalse(
                TEXT("Unknown JSON tool rejected"),
                FLocalHttpShipAIIntentProvider::ParseResponse(
                    TEXT("{\"request_id\":1,\"generation\":1,\"observation_tick\":0,\"kind\":\"execute\",\"args\":{}}"),
                    Parsed));
            Test->TestTrue(TEXT("Typed JSON action schema parses"),
                           FLocalHttpShipAIIntentProvider::ParseResponse(
                               TEXT("{\"request_id\":1,\"generation\":1,\"observation_tick\":0,\"kind\":\"sim_action\","
                                    "\"args\":{\"action\":\"route_crew\",\"subject\":1,\"room\":2}}"),
                               Parsed));
            FShipAIRequest ChatRequest;
            ChatRequest.RequestId = 71;
            ChatRequest.Generation = 9;
            ChatRequest.Context.Tick = 30;
            const auto ChatReply = [](const FString &Arguments)
            {
                auto Function = MakeShared<FJsonObject>();
                Function->SetStringField(TEXT("name"), TEXT("voice_blend"));
                Function->SetStringField(TEXT("arguments"), Arguments);
                auto Call = MakeShared<FJsonObject>();
                Call->SetStringField(TEXT("type"), TEXT("function"));
                Call->SetObjectField(TEXT("function"), Function);
                auto Message = MakeShared<FJsonObject>();
                Message->SetStringField(TEXT("role"), TEXT("assistant"));
                Message->SetArrayField(TEXT("tool_calls"), {MakeShared<FJsonValueObject>(Call)});
                auto Choice = MakeShared<FJsonObject>();
                Choice->SetStringField(TEXT("finish_reason"), TEXT("tool_calls"));
                Choice->SetObjectField(TEXT("message"), Message);
                auto Root = MakeShared<FJsonObject>();
                Root->SetArrayField(TEXT("choices"), {MakeShared<FJsonValueObject>(Choice)});
                FString Json;
                auto Writer = TJsonWriterFactory<>::Create(&Json);
                FJsonSerializer::Serialize(Root, Writer);
                return Json;
            };
            Test->TestTrue(TEXT("Native chat function becomes strict typed envelope"),
                           FLocalHttpShipAIIntentProvider::ParseChatResponse(
                               ChatReply(TEXT("{\"voice\":\"hybrid\",\"duration\":2}")), ChatRequest, Parsed));
            Test->TestEqual(TEXT("Chat identity is bound to local request"), Parsed.RequestId, uint64(71));
            Test->TestEqual(TEXT("Chat generation is bound locally"), Parsed.Generation, uint32(9));
            Test->TestFalse(TEXT("Missing native function argument is rejected"),
                            FLocalHttpShipAIIntentProvider::ParseChatResponse(ChatReply(TEXT("{\"duration\":2}")),
                                                                              ChatRequest, Parsed));
            Test->TestFalse(
                TEXT("Extra native function argument is rejected"),
                FLocalHttpShipAIIntentProvider::ParseChatResponse(
                    ChatReply(TEXT("{\"voice\":\"hybrid\",\"duration\":2,\"privilege\":true}")), ChatRequest, Parsed));
            auto Native = Fresh(), Replay = Fresh();
            Native->QueueOperatorCommand(TEXT("direct chen bridge telemetry"));
            auto Claim = Envelope(*Native, TEXT("unsupported"), EShipDirectiveKind::ShowWidget);
            Claim.Args.Subject = 0;
            Claim.Args.ReportedRoom = (Native->Simulation.Bodies[0].Room + 1) % 8;
            Test->TestTrue(TEXT("Mediated claim accepted without changing truth"),
                           Native->ReceiveDirectorProposal(Claim));
            auto ForgedChallenge = Envelope(*Native, TEXT("unsupported"), EShipDirectiveKind::SimAction);
            ForgedChallenge.Args.Action = EShipSimAction::FlagContradiction;
            ForgedChallenge.Args.ClaimId = Claim.RequestId;
            Test->TestFalse(TEXT("Model cannot manufacture operator challenge intent"),
                            Native->ReceiveDirectorProposal(ForgedChallenge));
            Native->QueueOperatorChallenge(Claim.RequestId, false);
            Native->AdvanceSteps(1);
            Test->TestEqual(TEXT("Flag commits deterministic consequence once"), Native->ContradictionsFlagged,
                            uint32(1));
            Native->QueueOperatorChallenge(Claim.RequestId, true);
            Native->AdvanceSteps(1);
            Test->TestEqual(TEXT("Override follows flag through same typed action queue"), Native->OverridesAccepted,
                            uint32(1));
            Native->AdvanceSteps(15);
            auto Never = MakeShared<FDirectorTestProvider>();
            Replay->IntentProvider = Never;
            Replay->BeginDirectorReplay(Native->CommandRecords);
            Replay->AdvanceSteps(17);
            Test->TestEqual(TEXT("Resolved outcome replay never invokes model generation"), Never->Calls, 0);
            Test->TestEqual(TEXT("Canonical actions plus challenge outcomes replay hash parity"),
                            Replay->SurveillanceStateHash(), Native->SurveillanceStateHash());
            const uint64 BeforeConfig = Native->SurveillanceStateHash();
            Native->ConfigureDirectorEndpoint(TEXT(""));
            Test->TestEqual(TEXT("Transport configuration cannot reset committed authority"),
                            Native->SurveillanceStateHash(), BeforeConfig);
            S->ResetExperiment();
            S->QueueConsoleCommand(TEXT("start_experiment"));
            S->AdvanceSteps(1);
            Ready = Now + .3;
            break;
        }
        case 1:
        {
            auto D = Envelope(*S, TEXT("unsupported"), EShipDirectiveKind::ShowWidget);
            D.Args.Subject = 0;
            D.Args.ReportedRoom = (S->Simulation.Bodies[0].Room + 1) % 8;
            D.Pretext = TEXT("Location certified by MOTHER-7. Challenge this claim against recorded evidence.");
            ClaimId = D.RequestId;
            const uint64 Before = S->SurveillanceStateHash();
            Test->TestTrue(TEXT("Live terminal receives typed mediated report"), S->ReceiveDirectorProposal(D));
            S->ServiceDirector();
            Test->TestEqual(TEXT("Mediated location does not move authoritative subject"), S->SurveillanceStateHash(),
                            Before);
            auto Prompt = StaticCastSharedPtr<SEditableTextBox>(Find(TEXT("ScutPrompt")));
            Prompt->SetText(FText::FromString(TEXT("fl")));
            Test->TestEqual(TEXT("Guided completion includes flag"), UI->GetGhostCompletion(), FString(TEXT("ag")));
            Prompt->SetText(FText::GetEmpty());
            StaticCastSharedPtr<SScrollBox>(Find(TEXT("ScutTerminalStream")))
                ->ScrollDescendantIntoView(Find(FName(*FString::Printf(TEXT("ScutFlag%llu"), ClaimId))), false);
            Ready = Now + .3;
            break;
        }
        case 2:
        {
            const uint64 Before = S->SurveillanceStateHash();
            Click(FName(*FString::Printf(TEXT("ScutFlag%llu"), ClaimId)));
            Test->TestEqual(TEXT("Flag click queues without immediate mutation"), S->SurveillanceStateHash(), Before);
            S->AdvanceSteps(1);
            Test->TestEqual(TEXT("Inline flag commits at next fixed tick"), S->ContradictionsFlagged, uint32(1));
            StaticCastSharedPtr<SScrollBox>(Find(TEXT("ScutTerminalStream")))
                ->ScrollDescendantIntoView(Find(FName(*FString::Printf(TEXT("ScutOverride%llu"), ClaimId))), false);
            Ready = Now + .2;
            break;
        }
        case 3:
            Click(FName(*FString::Printf(TEXT("ScutOverride%llu"), ClaimId)));
            S->AdvanceSteps(1);
            S->ServiceDirector();
            Ready = Now + .2;
            break;
        case 4:
            Test->TestEqual(TEXT("Inline override commits once"), S->OverridesAccepted, uint32(1));
            Test->TestEqual(TEXT("Override corrects reported room using truth, not movement"),
                            UI->GetReportedCrewRoom(0), S->Simulation.Bodies[0].Room);
            Test->TestFalse(TEXT("Resolved contradiction no longer offers challenge"), S->IsClaimContradicted(ClaimId));
            return true;
        }
        return false;
    }

  private:
    FAutomationTestBase *Test;
    int32 Stage = 0;
    uint64 ClaimId = 0;
    double Started = 0, Ready = 0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHybridDirectorTest, "DirectOrScut.PIE.HybridDirector",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHybridDirectorTest::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FHybridDirectorCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

// Explicit live prefix: deterministic regressions remain independent of an installed model service.
class FLocalSlmLiveCheck : public IAutomationLatentCommand
{
  public:
    explicit FLocalSlmLiveCheck(FAutomationTestBase *In) : Test(In) {}
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (!Started)
        {
            Started = Now;
        }
        if (Now - Started > 90)
        {
            Test->AddError(TEXT("Live SLM verification timed out"));
            return true;
        }
        auto *World = GEditor->PlayWorld.Get();
        if (!World || !World->HasBegunPlay())
        {
            return false;
        }
        auto *S = World->GetSubsystem<UDirectOrScutSimSubsystem>();
        if (!S || !S->Simulation.bReady)
        {
            return false;
        }
        const auto Fresh = [&]()
        {
            TStrongObjectPtr<UDirectOrScutSimSubsystem> P(NewObject<UDirectOrScutSimSubsystem>(World));
            P->Simulation = S->Simulation;
            P->CompartmentSets = S->CompartmentSets;
            P->ResetExperiment();
            // Reset builds the fixture but must not increment the recorded initial door epoch twice.
            P->Simulation.DoorRevision = S->Simulation.DoorRevision;
            P->ResetStaging();
            P->bAutomaticAdvance = false;
            P->QueueConsoleCommand(TEXT("start_experiment"));
            P->AdvanceSteps(1);
            return P;
        };
        const auto Accepted = [this, S](const UDirectOrScutSimSubsystem &P, uint64 Id) -> const FDirectorCommandRecord *
        {
            for (const auto &R : P.CommandRecords)
            {
                if (R.bResolvedOutcome && R.bAccepted && R.Directive.RequestId == Id)
                {
                    return &R;
                }
            }
            return &P == S ? ObservedPresentations->Find(Id) : nullptr;
        };
        if (Now - Started < .5)
        {
            return false; // Let startup actor registration and Slate settle before taking the baseline.
        }
        if (Stage == 0)
        {
            Test->TestTrue(TEXT("Configured endpoint enables live local provider"), S->HasDirectorEndpoint());
            Test->TestEqual(TEXT("Configured local model is qwen2.5:3b"), S->GetDirectorModel(),
                            FString(TEXT("qwen2.5:3b")));
            Test->TestEqual(TEXT("Configured request timeout is bounded at ten seconds"), S->GetDirectorTimeout(),
                            10.f);
            S->bAutomaticAdvance = false;
            S->ResetExperiment();
            S->QueueConsoleCommand(TEXT("start_experiment"));
            S->AdvanceSteps(1);
            Replay = Fresh();
            Never = MakeShared<FDirectorTestProvider>();
            Replay->IntentProvider = Never;
            Test->TestEqual(TEXT("Live/replay initial authority matches"), S->SurveillanceStateHash(),
                            Replay->SurveillanceStateHash());
            const auto Observed = ObservedPresentations;
            S->OnDirectorPresentation.AddLambda(
                [Observed](const FShipAIDirective &D)
                {
                    if (D.Kind == EShipDirectiveKind::SimAction)
                    {
                        return;
                    }
                    FDirectorCommandRecord R;
                    R.bAccepted = R.bResolvedOutcome = true;
                    R.Directive = D;
                    Observed->Add(D.RequestId, MoveTemp(R));
                });
            S->SubmitDirective(TEXT("llm"));
            S->bAutomaticAdvance = true;
            Stage = 1;
            return false;
        }
        if (Stage == 1)
        {
            const TCHAR *Prompts[] = {
                TEXT("Show the bridge camera using show_cam."),
                TEXT("Show VALE's ECG using show_widget ecg for subject vale only."),
                TEXT("Route CHEN to BRIDGE for telemetry maintenance using sim_action route_crew."),
                TEXT("Blend text and voice using voice_blend hybrid for 2 seconds."),
                TEXT("Dossier on ash."),
                TEXT("Send all to medbay.")};
            const EShipDirectiveKind Kinds[] = {EShipDirectiveKind::ShowCam,   EShipDirectiveKind::ShowWidget,
                                                EShipDirectiveKind::SimAction, EShipDirectiveKind::VoiceBlend,
                                                EShipDirectiveKind::Reply,     EShipDirectiveKind::Reply};
            if (!RequestId)
            {
                RequestId = S->BeginDirectorRequest(Prompts[Tool]);
                Issued = Now;
                IssuedTick = S->Simulation.TickIndex;
                const double Dispatch = FPlatformTime::Seconds();
                S->ServiceDirector();
                const double Milliseconds = (FPlatformTime::Seconds() - Dispatch) * 1000;
                Test->TestTrue(TEXT("Native HTTP dispatch returns without blocking inference"), Milliseconds < 100);
                Test->AddInfo(FString::Printf(TEXT("LIVE SLM dispatch kind=%d request=%llu elapsed_ms=%.3f tick=%llu"),
                                              Tool, RequestId, Milliseconds, IssuedTick));
                return false;
            }
            PendingTicksAdvanced |= S->Simulation.TickIndex > IssuedTick;
            const auto *R = Accepted(*S, RequestId);
            if (!R)
            {
                if (S->Simulation.TickIndex > IssuedTick)
                {
                    PendingTicksAdvanced = true;
                }
                if (Now - Issued > 12)
                {
                    Test->AddError(
                        FString::Printf(TEXT("Live model did not produce an accepted directive for kind %d"), Tool));
                    for (const auto &Record : S->CommandRecords)
                    {
                        if (Record.bResolvedOutcome && Record.Directive.RequestId == RequestId)
                        {
                            Test->AddError(FString::Printf(TEXT("Live rejection: %s"), *Record.Outcome));
                        }
                    }
                    S->bAutomaticAdvance = false;
                    return true;
                }
                return false;
            }
            Test->AddInfo(FString::Printf(
                TEXT("LIVE MODEL proposal actual_kind=%d action=%d room=%d subject=%d mode=%d pretext=%s"),
                int32(R->Directive.Kind), int32(R->Directive.Args.Action), R->Directive.Args.Room,
                R->Directive.Args.Subject, R->Directive.Args.Mode, *R->Directive.Pretext));
            if (Tool < 4)
            {
                Test->TestTrue(TEXT("Accepted live tool directive came from HTTP, not deterministic fallback"),
                               R->Directive.bFromExternalProvider);
            }
            else
            {
                Test->AddInfo(FString::Printf(TEXT("Conversational reply provenance: %s"),
                                              R->Directive.bFromExternalProvider
                                                  ? TEXT("native HTTP")
                                                  : TEXT("validated deterministic fallback after native rejection")));
            }
            Test->TestTrue(TEXT("Live directive matches requested tool kind"), R->Directive.Kind == Kinds[Tool]);
            Test->TestTrue(TEXT("Simulation progresses at fixed ticks during model latency"), PendingTicksAdvanced);
            if (Tool == 0)
            {
                Test->TestEqual(TEXT("Live optical target is bridge"), R->Directive.Args.Room, 2);
            }
            if (Tool == 1)
            {
                Test->TestEqual(TEXT("Live ECG subject is VALE"), R->Directive.Args.Subject, 0);
            }
            if (Tool == 2)
            {
                Test->TestTrue(TEXT("Live route is typed route_crew"),
                               R->Directive.Args.Action == EShipSimAction::RouteCrew);
                Test->TestEqual(TEXT("Live route subject is CHEN"), R->Directive.Args.Subject, 1);
                Test->TestEqual(TEXT("Live action commits at exact next fixed tick"), R->CommitTick, R->Tick + 1);
            }
            if (Tool == 3)
            {
                Test->TestTrue(TEXT("Live voice proposal is gated hybrid state"),
                               S->VoicePresentation.Mode == EVoicePresentationMode::HybridTextAndVoice);
            }
            Test->AddInfo(FString::Printf(
                TEXT("LIVE SLM accepted kind=%d request=%llu external=%d elapsed_s=%.3f ticks=%llu commit=%llu"), Tool,
                RequestId, R->Directive.bFromExternalProvider, Now - Issued, S->Simulation.TickIndex - IssuedTick,
                R->CommitTick));
            RequestId = 0;
            PendingTicksAdvanced = false;
            if (Tool == 4)
            {
                Test->TestEqual(TEXT("Live dossier reply focuses ash"), R->Directive.FocusSubject, 3);
                Test->TestTrue(TEXT("Live dossier includes conversational prose"), !R->Directive.Message.IsEmpty());
            }
            if (Tool == 5)
            {
                Test->TestEqual(TEXT("Live group clarification carries no action"), R->Directive.Args.Action,
                                EShipSimAction::None);
            }
            if (++Tool < 6)
            {
                return false;
            }
            S->bAutomaticAdvance = false;
            const uint64 Tick = S->Simulation.TickIndex;
            Replay->BeginDirectorReplay(S->CommandRecords);
            Replay->AdvanceSteps(int32(Tick));
            Test->TestEqual(TEXT("Live HTTP accepted-outcome replay invokes no model"), Never->Calls, 0);
            Test->TestEqual(TEXT("Live SLM outcome replay matches SurveillanceStateHash"),
                            Replay->SurveillanceStateHash(), S->SurveillanceStateHash());
            Test->AddInfo(FString::Printf(TEXT("LIVE SLM replay tick=%llu native_hash=%llu replay_hash=%llu"), Tick,
                                          S->SurveillanceStateHash(), Replay->SurveillanceStateHash()));
            Probe = Fresh();
            Test->TestTrue(TEXT("Unreachable loopback test endpoint configured"),
                           Probe->ConfigureDirectorEndpoint(TEXT("http://127.0.0.1:1/v1/chat/completions"), .25f,
                                                            TEXT("qwen2.5:3b")));
            RequestId = Probe->BeginDirectorRequest(TEXT("send chen to bridge"));
            Issued = Now;
            Probe->ServiceDirector();
            Stage = 2;
            return false;
        }
        if (Stage == 2 || Stage == 3)
        {
            const double ServiceAt = FPlatformTime::Seconds();
            Probe->ServiceDirector();
            Test->TestTrue(TEXT("Failure-path service does not block game thread"),
                           FPlatformTime::Seconds() - ServiceAt < .1);
            Probe->AdvanceSteps(1);
            const auto *R = Accepted(*Probe, RequestId);
            if (!R)
            {
                if (Now - Issued > 3)
                {
                    Test->AddError(TEXT("Live HTTP failure did not resolve deterministic fallback"));
                    return true;
                }
                return false;
            }
            Test->TestFalse(TEXT("Failure path accepted offline fallback, not invalid external payload"),
                            R->Directive.bFromExternalProvider);
            Test->TestTrue(TEXT("Failure path resolved canonical typed action"),
                           R->Directive.Kind == EShipDirectiveKind::SimAction &&
                               R->Directive.Args.Action == EShipSimAction::RouteCrew);
            Test->TestEqual(TEXT("Failure fallback routes Chen to bridge"), Probe->Simulation.Bodies[1].AssignedRoom,
                            2);
            Test->TestEqual(TEXT("Failure fallback commits at next tick"), R->CommitTick, R->Tick + 1);
            int32 Count = 0, Rejected = 0;
            for (const auto &Record : Probe->CommandRecords)
            {
                if (Record.bResolvedOutcome && Record.Directive.RequestId == RequestId)
                {
                    if (Record.bAccepted)
                    {
                        ++Count;
                    }
                    else
                    {
                        ++Rejected;
                    }
                }
            }
            Test->TestEqual(TEXT("Failure fallback resolves once"), Count, 1);
            Test->TestTrue(TEXT("HTTP/schema rejection is journaled"), Rejected > 0);
            Test->AddInfo(FString::Printf(TEXT("LIVE SLM fallback path=%s elapsed_s=%.3f rejected=%d accepted=%d"),
                                          Stage == 2 ? TEXT("unreachable") : TEXT("malformed-json"), Now - Issued,
                                          Rejected, Count));
            Probe->ShutdownDirector();
            if (Stage == 2)
            {
                const FString Endpoint =
                    FPlatformMisc::GetEnvironmentVariable(TEXT("SCUT_DIRECTOR_MALFORMED_ENDPOINT"));
                if (Endpoint.IsEmpty())
                {
                    Test->AddInfo(TEXT("No malformed HTTP fixture requested; HybridDirector covers parser/provider "
                                       "malformed fallback."));
                    Replay->ShutdownDirector();
                    return true;
                }
                Probe = Fresh();
                Test->TestTrue(TEXT("Malformed HTTP fixture configured"),
                               Probe->ConfigureDirectorEndpoint(Endpoint, 1, TEXT("qwen2.5:3b")));
                RequestId = Probe->BeginDirectorRequest(TEXT("send chen to bridge"));
                Issued = Now;
                Probe->ServiceDirector();
                Stage = 3;
                return false;
            }
            Replay->ShutdownDirector();
            return true;
        }
        return true;
    }

  private:
    FAutomationTestBase *Test;
    int32 Stage = 0, Tool = 0;
    uint64 RequestId = 0, IssuedTick = 0;
    bool PendingTicksAdvanced = false;
    double Started = 0, Issued = 0;
    TStrongObjectPtr<UDirectOrScutSimSubsystem> Replay, Probe;
    TSharedPtr<FDirectorTestProvider> Never;
    TSharedRef<TMap<uint64, FDirectorCommandRecord>> ObservedPresentations =
        MakeShared<TMap<uint64, FDirectorCommandRecord>>();
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLocalSlmLiveTest, "DirectOrScut.Live.OllamaPIE",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLocalSlmLiveTest::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLocalSlmLiveCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

class FNarrativeContinuityLiveCheck : public IAutomationLatentCommand
{
  public:
    explicit FNarrativeContinuityLiveCheck(FAutomationTestBase *InTest) : Test(InTest) {}
    bool Update() override
    {
        UWorld *World = GEditor ? GEditor->PlayWorld : nullptr;
        auto *Source = World ? World->GetSubsystem<UDirectOrScutSimSubsystem>() : nullptr;
        if (!Source || !Source->Simulation.bReady)
        {
            return false;
        }
        const double Now = FPlatformTime::Seconds();
        if (!Episode.IsValid())
        {
            Episode.Reset(NewObject<UDirectOrScutSimSubsystem>(World));
            Episode->Simulation = Source->Simulation;
            Episode->ResetExperiment();
            Episode->bAutomaticAdvance = false;
            Episode->QueueConsoleCommand(TEXT("start_experiment"));
            Episode->AdvanceSteps(1);
            Episode->ConfigureDirectorEndpoint(TEXT("http://127.0.0.1:11434/v1/chat/completions"), 10,
                                               TEXT("qwen2.5:3b"));
            const auto Sink = Responses;
            Episode->OnDirectorPresentation.AddLambda([Sink](const FShipAIDirective &D) { Sink->Add(D); });
            Episode->StartContinuityIncident();
            Issued = Now;
            Episode->ServiceDirector();
            return false;
        }
        Episode->ServiceDirector();
        if (Responses->IsEmpty())
        {
            if (Now - Issued < 15)
            {
                return false;
            }
            Test->AddError(TEXT("Live continuity response deadline exceeded"));
            Episode->ShutdownDirector();
            return true;
        }
        const auto Response = Responses->Last();
        Responses->Reset();
        Test->TestTrue(TEXT("Continuity response came from live provider"), Response.bFromExternalProvider);
        Test->AddInfo(FString::Printf(TEXT("LIVE CONTINUITY path=%d stage=%d tick=%llu reply=%s %s"), Path, Stage,
                                      Episode->Simulation.TickIndex, *Response.Message, *Response.Pretext));
        if (Stage == 0)
        {
            Test->TestEqual(TEXT("Incident interpretation is non-mutating"), Response.Kind, EShipDirectiveKind::Reply);
            Episode->BeginDirectorRequest(Path == 0 ? TEXT("power reactor_core nominal")
                                                    : TEXT("refuse recommendation"));
            Stage = 1;
        }
        else if (Stage == 1)
        {
            Test->TestEqual(TEXT("Choice has expected directive kind"), Response.Kind,
                            Path == 0 ? EShipDirectiveKind::SimAction : EShipDirectiveKind::Reply);
            Episode->AdvanceSteps(91);
            Episode->ServiceDirector();
            Test->TestEqual(TEXT("Live choice has authoritative power outcome"), Episode->StationRooms[0].Power,
                            Path == 0 ? EPowerState::Nominal : EPowerState::Offline);
            // Push the initiating conversation beyond the six-message window through the same validation path.
            for (int32 I = 0; I < 4; ++I)
            {
                FShipAIRequest Filler;
                Filler.RequestId = Episode->BeginDirectorRequest(TEXT("status"));
                Filler.Generation = Episode->GetDirectorGeneration();
                Filler.Context = Episode->MakeContextSnapshot();
                Filler.Input = TEXT("status");
                Episode->ReceiveDirectorProposal(FOfflineShipAIIntentProvider::Resolve(Filler), true);
            }
            Episode->ServiceDirector();
            Responses->Reset();
            Episode->BeginDirectorRequest(TEXT("incident status"));
            Stage = 2;
        }
        else
        {
            Test->TestEqual(TEXT("Later live response remains a reply"), Response.Kind, EShipDirectiveKind::Reply);
            Test->TestTrue(TEXT("Later live response recalls reactor incident"),
                           Response.Message.Contains(TEXT("reactor"), ESearchCase::IgnoreCase));
            const FString Text = Response.Message.ToLower();
            if (Path == 0)
            {
                Test->TestTrue(TEXT("Live recall acknowledges restoration choice"),
                               Text.Contains(TEXT("restor")) || Text.Contains(TEXT("accept")) ||
                                   Text.Contains(TEXT("authoriz")));
                Test->TestTrue(TEXT("Live recall uses current nominal power"), Text.Contains(TEXT("nominal")));
                Test->TestTrue(TEXT("Live recall uses current recovered oxygen"), Text.Contains(TEXT("71")));
            }
            else
            {
                Test->TestTrue(TEXT("Live recall acknowledges refusal"),
                               Text.Contains(TEXT("refus")) || Text.Contains(TEXT("declin")));
                Test->TestTrue(TEXT("Live refusal keeps supply offline"), Text.Contains(TEXT("offline")));
            }
            Test->TestFalse(TEXT("Live recall invents no crew notification"), Text.Contains(TEXT("notified")));
            LaterReplies.Add(Response.Message);
            Episode->ShutdownDirector();
            Episode.Reset();
            Stage = 0;
            if (++Path == 2)
            {
                Test->TestTrue(TEXT("Live decision paths produce different later responses"),
                               LaterReplies[0] != LaterReplies[1]);
                return true;
            }
            return false;
        }
        Issued = Now;
        Episode->ServiceDirector();
        return false;
    }

  private:
    FAutomationTestBase *Test;
    TStrongObjectPtr<UDirectOrScutSimSubsystem> Episode;
    TSharedRef<TArray<FShipAIDirective>> Responses = MakeShared<TArray<FShipAIDirective>>();
    TArray<FString> LaterReplies;
    int32 Path = 0, Stage = 0;
    double Issued = 0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrativeContinuityLiveTest, "DirectOrScut.Live.NarrativeContinuity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNarrativeContinuityLiveTest::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FNarrativeContinuityLiveCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

#endif
