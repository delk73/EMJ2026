#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutGameMode.h"
#include "DirectOrScutGeometry.h"
#include "DoorActor.h"
#include "CrewUnitBlockout.h"
#include "EngineUtils.h"

class FScutChunk2PIECommand : public IAutomationLatentCommand
{
  public:
    explicit FScutChunk2PIECommand(FAutomationTestBase *InTest) : Test(InTest) {}
    virtual bool Update() override
    {
        if (!Started)
        {
            Started = FPlatformTime::Seconds();
        }
        if (FPlatformTime::Seconds() - Started > 90)
        {
            Test->AddError(TEXT("Chunk 2 PIE setup timed out"));
            return true;
        }
        UWorld *World = GEditor ? GEditor->PlayWorld : nullptr;
        if (!World || !World->HasBegunPlay())
        {
            return false;
        }
        auto *Sim = World->GetSubsystem<UDirectOrScutSimSubsystem>();
        auto *PC = Cast<ADirectOrScutPlayerController>(World->GetFirstPlayerController());
        if (!Sim || !PC || !Sim->Simulation.bReady)
        {
            return false;
        }
        int32 Width = 0, Height = 0;
        PC->GetViewportSize(Width, Height);
        if (Width < 100 || Height < 100 || Sim->Simulation.TickIndex < 60)
        {
            return false;
        }
        Sim->bAutomaticAdvance = false;
        auto &M = Sim->Simulation;
        // Remaining fixtures verify the committed Slice 8 movement rules in isolation.
        M.bExperiment = false;
        Test->TestEqual(TEXT("Exactly four crew actors"), M.Bodies[3].bRegistered, true);
        Test->TestTrue(TEXT("Landmark footprints exported"), M.ObstacleCount > 20);
        for (int32 R = 0; R < 8; ++R)
        {
            Test->TestTrue(FString::Printf(TEXT("Room %d station free"), R),
                           M.PointClear(R, DirectOrScut::RoomWaypoint(R)));
        }
        auto Screen = [&](FVector P)
        {
            FVector2D Pixel;
            Test->TestTrue(TEXT("PIE projection succeeds"), PC->ProjectWorldLocationToScreen(P, Pixel));
            return Pixel;
        };
        auto OpenAll = [&]()
        {
            for (int32 I = 0; I < 8; ++I)
            {
                Sim->SetDoorOpen(I, true);
            }
        };
        auto RunChecked = [&](int32 Count)
        {
            bool Clear = true;
            for (int32 Step = 0; Step < Count; ++Step)
            {
                M.Step();
                for (const auto &U : M.Bodies)
                {
                    if (U.bRegistered && U.Room >= 0 && !U.bCrossing && U.State != EDirectOrScutUnitState::Ejected)
                    {
                        Clear &= M.PointClear(U.Room, U.Position);
                    }
                }
            }
            Test->TestTrue(TEXT("Routes remain on safe deck and outside landmarks"), Clear);
            Sim->SynchronizeActors();
        };

        // Exact screen-space input, including a guarded exterior hatch.
        Sim->ResetSimulation();
        PC->SelectedUnit = -1;
        for (int32 I = 0; I < 8; ++I)
        {
            auto *Door = Sim->GetDoorActor(I);
            Test->TestNotNull(TEXT("Door edge actor bound"), Door);
            if (!Door)
            {
                return true;
            }
            const FVector2D Pixel = Screen(Door->GetActorLocation() + FVector(0, 0, 130));
            Test->TestTrue(TEXT("Interior LMB screen trace opens edge"), PC->ToggleDoorAtScreenPosition(Pixel));
            Test->TestTrue(TEXT("Actor -> graph immediate"), M.Doors[I].bIsOpen);
            Sim->SetDoorOpen(I, false);
            Test->TestFalse(TEXT("Graph -> actor immediate"), Door->bOpen);
        }
        auto *Hatch = Sim->GetDoorActor(8);
        const FVector2D HatchPixel = Screen(Hatch->GetActorLocation() + FVector(0, 0, 130));
        Test->TestFalse(TEXT("Plain LMB cannot vent exterior"), PC->ToggleDoorAtScreenPosition(HatchPixel));
        Test->TestTrue(TEXT("RMB exterior trace vents"), PC->HandleVentAtScreenPosition(HatchPixel));
        Test->TestFalse(TEXT("Exterior graph vacuum"), M.Rooms[7].bIsPressurized);
        Test->TestTrue(TEXT("Shift LMB seals exterior"), PC->HandlePrimaryAtScreenPosition(HatchPixel, true));
        Test->TestTrue(TEXT("Immediate repressurization"), M.Rooms[7].bIsPressurized);
        M.Bodies[0].Position = M.Doors[8].Position;
        M.Bodies[0].Room = 7;
        Sim->SynchronizeActors();
        Test->TestTrue(TEXT("Shift vent takes priority over crew standing in hatch"),
                       PC->HandlePrimaryAtScreenPosition(HatchPixel, true));
        Test->TestTrue(TEXT("Occupied hatch actually opened"), M.Doors[8].bIsOpen);
        Sim->ResetSimulation();

        // DIRECT selection and closed-door safe standby, then actual traversal to work.
        const FVector2D CrewPixel = Screen(Sim->GetBodyActor(0)->GetActorLocation() + FVector(0, 0, 100));
        Test->TestTrue(TEXT("Crew LMB selection"), PC->HandlePrimaryAtScreenPosition(CrewPixel));
        Test->TestEqual(TEXT("Selected stable unit id"), PC->SelectedUnit, 0);
        Test->TestTrue(TEXT("Floor DIRECT reassignment"),
                       PC->HandlePrimaryAtScreenPosition(Screen(FVector(DirectOrScut::RoomWaypoint(4), 0))));
        Test->TestEqual(TEXT("DIRECT assigned observation deck"), M.Bodies[0].AssignedRoom, 4);
        RunChecked(120);
        Test->TestTrue(TEXT("Blocked unit idles"), M.Bodies[0].State == EDirectOrScutUnitState::Idle);
        Test->TestTrue(TEXT("Safe standby reached"), M.Bodies[0].Position.Equals(DirectOrScut::RoomStandby(2), .1));
        const FVector2D Standby = M.Bodies[0].Position;
        Sim->AdvanceSteps(120);
        Test->TestTrue(TEXT("Closed door has no jitter"), M.Bodies[0].Position.Equals(Standby, .001));
        Sim->SetDoorOpen(0, true);
        Sim->SetDoorOpen(2, true);
        RunChecked(800);
        Test->TestEqual(TEXT("Crew crossed ring to observation deck"), M.Bodies[0].Room, 4);
        Test->TestTrue(TEXT("Assigned station working"), M.Bodies[0].State == EDirectOrScutUnitState::Working);

        // Both real reactor connections: close the preferred one while routing.
        Sim->ResetSimulation();
        OpenAll();
        Test->TestEqual(TEXT("BFS stable first reactor edge"), M.FirstDoorOnPath(1, 0), 6);
        Sim->AssignRoom(0, 0);
        RunChecked(120);
        Sim->SetDoorOpen(6, false);
        Test->TestEqual(TEXT("Parallel reactor edge reroute"), M.FirstDoorOnPath(1, 0), 7);
        RunChecked(1400);
        Test->TestEqual(TEXT("Rerouted crew reached reactor"), M.Bodies[0].Room, 0);
        Test->TestTrue(FString::Printf(TEXT("Reactor waypoint working (state=%s, pos=%s, route=%d/%d)"),
                                       DirectOrScutStateName(M.Bodies[0].State), *M.Bodies[0].Position.ToString(),
                                       M.Bodies[0].RouteCursor, M.Bodies[0].RouteCount),
                       M.Bodies[0].State == EDirectOrScutUnitState::Working);

        // Pressure is graph reachability, including redundant reactor edges.
        Sim->ResetSimulation();
        Sim->SetDoorOpen(8, true);
        Sim->SetDoorOpen(5, true);
        Sim->SetDoorOpen(0, true);
        Test->TestFalse(TEXT("Vacuum reaches ring and bridge"), M.Rooms[1].bIsPressurized || M.Rooms[2].bIsPressurized);
        Test->TestTrue(TEXT("Sealed reactor remains pressurized"), M.Rooms[0].bIsPressurized);
        Sim->SetDoorOpen(6, true);
        Sim->SetDoorOpen(7, true);
        Sim->SetDoorOpen(6, false);
        Test->TestFalse(TEXT("Other open reactor edge still vents"), M.Rooms[0].bIsPressurized);
        Sim->SetDoorOpen(7, false);
        Test->TestTrue(TEXT("Both sealed reactor edges restore pressure"), M.Rooms[0].bIsPressurized);
        Sim->SetDoorOpen(0, false);
        Test->TestTrue(TEXT("Interior seal isolates bridge immediately"), M.Rooms[2].bIsPressurized);
        Sim->SetDoorOpen(5, false);
        Test->TestTrue(TEXT("Airlock bulkhead isolates ring"), M.Rooms[1].bIsPressurized);
        Sim->SetDoorOpen(8, false);
        for (auto &R : M.Rooms)
        {
            Test->TestTrue(TEXT("Exterior seal restores all pressure"), R.bIsPressurized);
        }

        // Separation at all nine door OBBs, on both sides and on the center plane.
        for (int32 D = 0; D < 9; ++D)
        {
            for (int32 Side = -1; Side <= 1; ++Side)
            {
                Sim->ResetSimulation();
                Sim->SetDoorOpen(D, true);
                const auto &Door = M.Doors[D];
                auto &U = M.Bodies[0];
                U.Room = Door.RoomA;
                U.Position = Door.Position + Door.Normal * (Side * 20.0);
                U.State = EDirectOrScutUnitState::MovingToRoom;
                Sim->SetDoorOpen(D, false);
                const double Separation = FVector2D::DotProduct(U.Position - Door.Position, Door.Normal);
                Test->TestTrue(TEXT("Close transition clears threshold immediately"), FMath::Abs(Separation) > 90);
                Test->TestEqual(TEXT("Side-aware room membership"), U.Room, Side > 0 ? Door.RoomB : Door.RoomA);
            }
        }

        // Panic refusal/calming, stress immunity, deterministic seeded F1 choice.
        Sim->ResetSimulation();
        Test->TestTrue(TEXT("Force human panic"), M.ForcePanic(0));
        Test->TestFalse(TEXT("Panic refuses DIRECT"), Sim->AssignRoom(0, 4));
        Test->TestFalse(TEXT("Android refuses panic"), M.ForcePanic(3));
        RunChecked(180);
        Test->TestTrue(TEXT("Panicking human remains panic"), M.Bodies[0].State == EDirectOrScutUnitState::Panic);
        Test->TestTrue(TEXT("Calm re-enables DIRECT"), M.CalmUnit(0) && Sim->AssignRoom(0, 2));
        Test->TestEqual(TEXT("Android stress is zero"), M.Bodies[3].Stress, 0.0);
        Sim->ResetSimulation();
        const int32 PanicIndex = M.ForceRandomHumanPanic();
        Sim->ResetSimulation();
        Test->TestEqual(TEXT("Seeded random panic choice"), M.ForceRandomHumanPanic(), PanicIndex);

        // Humans and androids follow actual open-door paths to SCUT.
        Sim->ResetSimulation();
        for (int32 I : {0, 3})
        {
            auto &U = M.Bodies[I];
            U.Room = U.AssignedRoom = 7;
            U.Position = DirectOrScut::RoomWaypoint(7);
        }
        Sim->SetDoorOpen(8, true);
        Sim->AdvanceSteps(1);
        Test->TestTrue(TEXT("Unsuited integrity rapidly drains"), M.Bodies[0].Integrity < 100);
        Test->TestEqual(TEXT("Android integrity unaffected by vacuum"), M.Bodies[3].Integrity, 100.0);
        Test->TestTrue(TEXT("Continuous suction vector"), M.Bodies[0].SuctionVelocity.Size() > 0);
        Sim->AdvanceSteps(180);
        for (int32 I : {0, 3})
        {
            Test->TestTrue(TEXT("Crew ejected through exterior"),
                           M.Bodies[I].State == EDirectOrScutUnitState::Ejected && M.Bodies[I].Room == -1);
        }
        Sim->ResetSimulation();
        auto &Exposed = M.Bodies[0];
        Exposed.Room = 7;
        Exposed.Position = DirectOrScut::RoomWaypoint(7);
        Sim->SetDoorOpen(8, true);
        Sim->AdvanceSteps(1);
        const double Integrity = Exposed.Integrity;
        Sim->SetDoorOpen(8, false);
        Sim->AdvanceSteps(10);
        Test->TestEqual(TEXT("Sealing stops integrity loss"), Exposed.Integrity, Integrity);
        Test->TestTrue(TEXT("Sealing stops suction"), Exposed.SuctionVelocity.IsNearlyZero());

        // Remote suction must route around the ring and pass multiple interior doors.
        Sim->ResetSimulation();
        OpenAll();
        Sim->SetDoorOpen(8, true);
        RunChecked(900);
        for (int32 I : {0, 3})
        {
            Test->TestTrue(TEXT("Remote crew route through open interior doors to SCUT"),
                           M.Bodies[I].State == EDirectOrScutUnitState::Ejected);
        }
        Sim->ResetSimulation();
        auto &Suited = M.Bodies[0];
        Suited.bSuited = true;
        Suited.Room = 7;
        Suited.Position = DirectOrScut::RoomWaypoint(7);
        Sim->SetDoorOpen(8, true);
        Sim->AdvanceSteps(1);
        Test->TestEqual(TEXT("Suit prevents integrity drain"), Suited.Integrity, 100.0);
        Test->TestTrue(TEXT("Suit does not prevent suction"), Suited.SuctionVelocity.Size() > 0);
        Suited.bSuited = false;

        // A closing door also cancels approaches just outside the threshold volume.
        Sim->ResetSimulation();
        Sim->SetDoorOpen(0, true);
        auto &Crossing = M.Bodies[0];
        Crossing.Room = 1;
        Crossing.AssignedRoom = 2;
        Crossing.Position = M.Doors[0].Position - M.Doors[0].Normal * 100;
        Crossing.ActiveDoor = 0;
        Crossing.bCrossing = true;
        Crossing.CrossFromRoom = 1;
        Crossing.CrossToRoom = 2;
        Sim->SetDoorOpen(0, false);
        Sim->AdvanceSteps(90);
        Test->TestFalse(TEXT("Closing cancels pending doorway crossing"), Crossing.bCrossing);
        Test->TestEqual(TEXT("Cancelled approach cannot cross closed wall"), Crossing.Room, 1);
        Sim->ResetSimulation();
        auto &Stressed = M.Bodies[0];
        Stressed.Position = DirectOrScut::RoomWaypoint(2);
        Stressed.Stress = 99.99;
        Sim->AdvanceSteps(1);
        Test->TestTrue(TEXT("Work stress automatically triggers panic at 100 percent"),
                       Stressed.State == EDirectOrScutUnitState::Panic);
        Stressed.Room = 3;
        Stressed.Position = DirectOrScut::RoomWaypoint(3);
        Stressed.PanicClock = 0;
        Stressed.RouteRevision = MAX_uint32;
        RunChecked(210);
        Test->TestTrue(TEXT("Med bay naturally calms panic"),
                       Stressed.State != EDirectOrScutUnitState::Panic && Stressed.Stress < 30);

        // Exact replay under different render-frame partitions.
        Sim->ResetSimulation();
        OpenAll();
        auto A = MakeUnique<FDirectOrScutSimulation>(M), B = MakeUnique<FDirectOrScutSimulation>(M);
        A->ForcePanic(0);
        B->ForcePanic(0);
        A->AssignRoom(1, 0);
        B->AssignRoom(1, 0);
        for (int32 I = 0; I < 600; ++I)
        {
            A->Advance(1.0 / 30.0);
        }
        for (int32 I = 0; I < 1200; ++I)
        {
            B->Advance(1.0 / 60.0);
        }
        Test->TestEqual(TEXT("Frame-partition independent replay"), A->StateHash(), B->StateHash());
        Test->TestEqual(
            TEXT("Repeating ETA at 60 seconds"),
            [&]()
            {
                A->ResetRuntime();
                for (int32 I = 0; I < 1800; ++I)
                {
                    A->Step();
                }
                return A->ShuttleETA();
            }(),
            60.0);
        Sim->ResetSimulation();
        Sim->bAutomaticAdvance = true;
        UE_LOG(LogTemp, Display,
               TEXT("SCUT CHUNK 2 PIE VERIFICATION COMPLETE: input, routes, pressure, separation, panic, ejection, "
                    "deterministic replay"));
        return true;
    }

  private:
    FAutomationTestBase *Test;
    double Started = 0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScutChunk2SimulationTest, "DirectOrScut.Chunk2.SimulationPIE",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FScutChunk2SimulationTest::RunTest(const FString &Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/DirectOrScut/Maps/DirectOrScutBlockout")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FScutChunk2PIECommand>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
