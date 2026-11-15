#include "DirectOrScutSimSubsystem.h"
#include "OfflineShipAIIntentProvider.h"
#include "GameFramework/WorldSettings.h"
#include "DirectOrScutAudioComponent.h"
#include "DirectOrScutGeometry.h"
#include "DoorActor.h"
#include "CrewUnitBlockout.h"
#include "Engine/World.h"
#include "DirectOrScutSetAnchor.h"
#include "EngineUtils.h"
#include "RoomSetBase.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/ConfigCacheIni.h"
#include "Async/Async.h"

namespace
{
const int32 RoomOrder[] = {0, 2, 3, 4, 5, 6, 7, 1};
const TCHAR *CrewNames[] = {TEXT("vale"), TEXT("chen"), TEXT("ortiz"), TEXT("ash")};
const TCHAR *CrewRoles[] = {TEXT("engineer"), TEXT("medic"), TEXT("technician"), TEXT("android")};
const TCHAR *PublicVerbs[] = {TEXT("help"),
                              TEXT("?"),
                              TEXT("clear"),
                              TEXT("status"),
                              TEXT("crew"),
                              TEXT("rooms"),
                              TEXT("cam"),
                              TEXT("menu"),
                              TEXT("choose"),
                              TEXT("direct"),
                              TEXT("alert"),
                              TEXT("calm"),
                              TEXT("protocol"),
                              TEXT("open"),
                              TEXT("close"),
                              TEXT("seal"),
                              TEXT("unseal"),
                              TEXT("vent"),
                              TEXT("scut"),
                              TEXT("flush"),
                              TEXT("fx"),
                              TEXT("view"),
                              TEXT("scenario"),
                              TEXT("reset_experiment"),
                              TEXT("start_experiment"),
                              TEXT("repair_cam"),
                              TEXT("follow"),
                              TEXT("falsify_bio"),
                              TEXT("spoof_log"),
                              TEXT("intercom_lie"),
                              TEXT("power"),
                              TEXT("incident_reel")};

int32 CrewId(const FString &Name)
{
    for (int32 I = 0; I < 4; ++I)
    {
        if (Name == CrewNames[I] || Name == CrewRoles[I])
        {
            return I;
        }
    }
    return -1;
}
} // namespace

bool UDirectOrScutSimSubsystem::ShouldCreateSubsystem(UObject *Outer) const
{
    const auto *World = Cast<UWorld>(Outer);
    return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}
void UDirectOrScutSimSubsystem::Initialize(FSubsystemCollectionBase &Collection)
{
    Super::Initialize(Collection);
    ResetCrisis();
    ResetSurveillance();
    for (int32 R = 0; R < 8; ++R)
    {
        CompartmentSets.Add(static_cast<ERoomId>(R), DefaultSetTransform(R));
    }
    Simulation.Initialize();
    ConfigureScenario(false);
    Simulation.bExperiment = true;
    IntentProvider = MakeShared<FOfflineShipAIIntentProvider>();
    FString Endpoint, Model;
    float Timeout = 2;
    GConfig->GetString(TEXT("DirectOrScut.Director"), TEXT("LocalEndpoint"), Endpoint, GGameIni);
    GConfig->GetFloat(TEXT("DirectOrScut.Director"), TEXT("TimeoutSeconds"), Timeout, GGameIni);
    GConfig->GetString(TEXT("DirectOrScut.Director"), TEXT("Model"), Model, GGameIni);
    const bool Configured = !Endpoint.IsEmpty() && ConfigureDirectorEndpoint(Endpoint, Timeout, Model);
    UE_LOG(LogTemp, Display, TEXT("DIRECTOR CONFIG file=%s endpoint=%s model=%s timeout=%.2f enabled=%d"), *GGameIni,
           *Endpoint, *Model, Timeout, Configured);
}
bool UDirectOrScutSimSubsystem::IsTickable() const
{
    return !IsTemplate() && bAutomaticAdvance && Simulation.bReady && GetWorld() && GetWorld()->HasBegunPlay();
}
TStatId UDirectOrScutSimSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UDirectOrScutSimSubsystem, STATGROUP_Tickables);
}
void UDirectOrScutSimSubsystem::Tick(float Delta)
{
    ServiceDirector();
    if (!FMath::IsFinite(Delta) || Delta <= 0)
    {
        return;
    }
    if (ExperimentPhase != EExperimentPhase::Active)
    {
        ResolvedCommandAccumulator += Delta;
        int32 ControlBudget = 240;
        while (ExperimentPhase != EExperimentPhase::Active &&
               ResolvedCommandAccumulator + 1.e-9 >= Simulation.FixedDelta && ControlBudget-- > 0)
        {
            ResolvedCommandAccumulator -= Simulation.FixedDelta;
            FixedStep();
        }
        return;
    }
    Simulation.Accumulator += Delta;
    int32 Budget = 240;
    while (ExperimentPhase == EExperimentPhase::Active && Simulation.Accumulator + 1.e-9 >= Simulation.FixedDelta &&
           Budget-- > 0)
    {
        FixedStep();
        Simulation.Accumulator = FMath::Max(0., Simulation.Accumulator - Simulation.FixedDelta);
    }
    SynchronizeActors();
}
void UDirectOrScutSimSubsystem::RegisterDoor(ADoorActor *Door)
{
    const int32 I = Door->SimDoorIndex;
    if (I < 0 || I >= 9)
    {
        return;
    }
    DoorActors[I] = Door;
    Door->ApplySimulationOpen(Simulation.Doors[I].bIsOpen);
}
void UDirectOrScutSimSubsystem::RegisterCrew(ACrewUnitBlockout *Crew)
{
    const int32 I = Crew->SimUnitIndex;
    if (I < 0 || I >= 4)
    {
        return;
    }
    BodyActors[I] = Crew;
    Simulation.RegisterBody(I, FVector2D(DirectOrScut::CrewSpawnPoint(I)), I + 2, Crew->bAndroid);
    Simulation.Bodies[I].bSuited = Crew->bSuited;
    Simulation.Bodies[I].bStagingManaged = true;
    InitializeCrewStaging(I);
    SynchronizeActors();
}
bool UDirectOrScutSimSubsystem::RecordObstacle(int32 Room, FVector Center, FVector Size, float Yaw)
{
    // Only solid landmark sections that intersect a unit's standing silhouette.
    if (Center.Z + Size.Z / 2 <= 10 || Center.Z - Size.Z / 2 >= 180)
    {
        return true;
    }
    const bool Result = Simulation.AddObstacle(Room, FVector2D(Center), FVector2D(Size.X / 2, Size.Y / 2), Yaw);
    ensureMsgf(Result, TEXT("DIRECT OR SCUT landmark obstacle capacity exceeded"));
    return Result;
}
void UDirectOrScutSimSubsystem::FinishLayout()
{
    ResetStaging();
    SynchronizeActors();
    const bool Success = Simulation.BuildNavigation();
    ensureMsgf(Success, TEXT("DIRECT OR SCUT navigation construction failed"));
    UE_LOG(LogTemp, Display, TEXT("SCUT SIM layout: obstacles=%d waypoints=%d ready=%d"), Simulation.ObstacleCount,
           Simulation.NodeCount, Success);
}
bool UDirectOrScutSimSubsystem::SetDoorOpen(int32 I, bool Open)
{
    if (ExperimentPhase != EExperimentPhase::Active || IsUplinkLocked() || !Simulation.SetDoorOpen(I, Open))
    {
        return false;
    }
    if (auto *Door = DoorActors[I].Get())
    {
        Door->ApplySimulationOpen(Open);
    }
    SynchronizeActors();
    return true;
}
void UDirectOrScutSimSubsystem::SynchronizeActors()
{
    for (int32 I = 0; I < 9; ++I)
    {
        if (auto *Door = DoorActors[I].Get())
        {
            Door->ApplySimulationOpen(Simulation.Doors[I].bIsOpen);
        }
    }
    for (int32 R = 0; R < 8; ++R)
    {
        if (auto *Set = RoomSetActors[R].Get())
        {
            Set->ApplyContainmentBarrier(IsRoomSealed(R));
            Set->ApplyPowerLighting(StationRooms[R].Power);
        }
    }
    for (int32 I = 0; I < 12; ++I)
    {
        if (auto *Actor = BodyActors[I].Get())
        {
            const auto &U = Simulation.Bodies[I];
            if (!U.bRegistered)
            {
                continue;
            }
            if (I < 4 && CrewStaging[I].Room >= 0)
            {
                if (auto *Crew = Cast<ACrewUnitBlockout>(Actor))
                {
                    Crew->ApplyStagingPose(CrewStaging[I], Simulation.TickIndex, U.Profile.HeartRateBPM);
                }
            }
            else
            {
                Actor->SetActorLocation(ToSetWorld(U.Room, U.Position), false);
            }
            if (U.State == EDirectOrScutUnitState::Ejected)
            {
                Actor->SetActorEnableCollision(false);
            }
            else
            {
                Actor->SetActorEnableCollision(true);
            }
        }
    }
}
void UDirectOrScutSimSubsystem::AdvanceSteps(int32 Count)
{
    for (int32 I = 0; I < Count; ++I)
    {
        ServiceDirector(); // Test/manual driver, deliberately outside FixedStep.
        FixedStep();
    }
    SynchronizeActors();
}
void UDirectOrScutSimSubsystem::ResetSimulation()
{
    CancelDirector();
    ContradictionsFlagged = OverridesAccepted = 0;
    ResetCrisis();
    ResolvedCommandAccumulator = 0;
    ++RuntimeRevision;
    ExperimentPhase = EExperimentPhase::Briefing;
    ResetSurveillance();
    ExperimentEnding = EExperimentEnding::None;
    PeakSuspicion = 0;
    ScutTargets = 0;
    ScutEliminated = 0;
    bScutProtected = true;
    ++EventRevision;
    PendingConsoleCommands.Reset();
    MenuParents.Reset();
    ActiveMenu = EShipAIScreenMenu::None;
    ++MenuRevision;
    PendingDirectives.Reset();
    Events.Reset();
    bAILink = false;
    RequestedFeed = -1;
    FollowedCrew = -1;
    ++FollowRevision;
    CommandRecords.Reset();
    CinematicCues.Reset();
    ConfigureScenario(bMedicalScenario);
    Simulation.ResetRuntime();
    Simulation.Scenario.ShiftDuration = 600;
    for (int32 I = 0; I < 9; ++I)
    {
        if (auto *Door = DoorActors[I].Get())
        {
            Door->ApplySimulationOpen(Simulation.Doors[I].bIsOpen);
        }
    }
    ResetStaging();
    SynchronizeActors();
}
ADoorActor *UDirectOrScutSimSubsystem::GetDoorActor(int32 I) const
{
    return I >= 0 && I < 9 ? DoorActors[I].Get() : nullptr;
}
AActor *UDirectOrScutSimSubsystem::GetBodyActor(int32 I) const
{
    return I >= 0 && I < 12 ? BodyActors[I].Get() : nullptr;
}

int32 UDirectOrScutSimSubsystem::ResolveRoom(const FString &Name)
{
    for (int32 R = 0; R < 8; ++R)
    {
        if (Name.Equals(DirectOrScut::RoomName(R), ESearchCase::IgnoreCase))
        {
            return R;
        }
    }
    if (Name.Equals(TEXT("airlock"), ESearchCase::IgnoreCase))
    {
        return 7;
    }
    return -1;
}
void UDirectOrScutSimSubsystem::LogEvent(const FString &Text, int32 Room, bool TerminalVisible)
{
    ++EventRevision;
    const int32 Seconds = int32(Simulation.TickIndex / 30);
    Events.Add({FString::Printf(TEXT("[%02d:%02d] %s"), Seconds / 60, Seconds % 60, *Text), Room, TerminalVisible});
}
FString UDirectOrScutSimSubsystem::SubmitDirective(const FString &Input)
{
    if (ExperimentPhase == EExperimentPhase::Resolved)
    {
        return TEXT("Experiment resolved. Use reset_experiment or scenario behavioral|medical.");
    }
    FString Line = Input.TrimStartAndEnd().ToLower();
    if (Line == TEXT("llm") || Line == TEXT("ai link"))
    {
        bAILink = true;
        return TEXT("AI-LINK> Local offline intent provider ready. Use explicit Ship AI commands.");
    }
    if (Line == TEXT("cmd") || Line == TEXT("exit"))
    {
        bAILink = false;
        return TEXT("CMD> Deterministic control restored.");
    }
    if (Line.StartsWith(TEXT("flag ")) || Line.StartsWith(TEXT("override ")))
    {
        return QueueOperatorCommand(Line) ? TEXT("SYSTEM> Challenge submitted for typed validation.")
                                          : TEXT("SYSTEM> Challenge queue unavailable.");
    }
    if (bAILink && Line != TEXT("status") && Line != TEXT("crew") && Line != TEXT("rooms"))
    {
        return BeginDirectorRequest(Input) ? TEXT("AI-LINK> Request queued for validated director resolution.")
                                           : TEXT("AI-LINK> Request rejected: lifecycle or bounded queue.");
    }
    if (Line == TEXT("status"))
    {
        return FString::Printf(TEXT("%s\nCover: %s\nShift %.1f / %.1fs | Objective %.0f%% | Suspicion %.0f%% | %s"),
                               *Simulation.Scenario.ScenarioId, *Simulation.Scenario.CoverStoryLabel,
                               Simulation.TickIndex / 30., Simulation.Scenario.ShiftDuration,
                               Simulation.ObjectiveProgress * 100, Simulation.CollectiveSuspicion() * 100,
                               Simulation.bObjectiveFailed                     ? TEXT("FAILED")
                               : Simulation.bShiftComplete                     ? TEXT("SHIFT COMPLETE")
                               : ExperimentPhase == EExperimentPhase::Briefing ? TEXT("BRIEFING")
                                                                               : TEXT("ACTIVE"));
    }
    if (Line == TEXT("crew"))
    {
        FString Result;
        const TCHAR *Names[] = {TEXT("VALE / engineer"), TEXT("CHEN / medic"), TEXT("ORTIZ / technician"),
                                TEXT("ASH / android")};
        for (int32 I = 0; I < 4; ++I)
        {
            const auto &U = Simulation.Bodies[I];
            const auto P = TerminalBiometrics(I);
            Result += FString::Printf(TEXT("%s %s %s Cmp %.2f Cps %.2f Aff %.2f Utl %.2f Sus %.2f Stress %.0f "
                                           "Integrity %.0f BPM %.0f RPM %.0f Pupil %.1fmm\n"),
                                      Names[I], DirectOrScut::RoomName(U.Room), DirectOrScutStateName(U.State),
                                      P.Compliance, P.Composure, P.Affinity, P.Utility, P.Suspicion, U.Stress,
                                      TerminalIntegrity(I), P.HeartRateBPM, P.RespirationRateRPM, P.PupilDilationMM);
            Result += TEXT("MED ") + MedicalReadout(I) + TEXT("\n");
        }
        return Result;
    }
    if (Line == TEXT("rooms"))
    {
        FString Result;
        for (int32 R = 0; R < 8; ++R)
        {
            Result += FString::Printf(TEXT("%s %s alert %.0fs\n"), DirectOrScut::RoomName(R),
                                      Simulation.Rooms[R].bIsPressurized ? TEXT("PRESSURIZED") : TEXT("VACUUM"),
                                      Simulation.Rooms[R].AlertSeconds);
        }
        return Result;
    }
    if (Line.StartsWith(TEXT("cam ")))
    {
        const FString N = Line.Mid(4);
        if (N.Len() == 1 && N[0] >= '1' && N[0] <= '8')
        {
            RequestedFeed = N[0] - '0';
            return TEXT("Camera selected.");
        }
        return TEXT("Camera must be 1 through 8.");
    }
    if (ExperimentPhase != EExperimentPhase::Active)
    {
        return TEXT("BRIEFING // use start_experiment to initialize before crew directives.");
    }
    if (PendingDirectives.Num() >= 32)
    {
        return TEXT("Command queue full.");
    }
    PendingDirectives.Add(Line);
    return TEXT("Directive queued for next 30 Hz tick.");
}
void UDirectOrScutSimSubsystem::ProcessDirectives()
{
    for (const auto &Line : PendingDirectives)
    {
        CommandRecords.Add({Simulation.TickIndex, Line, false});
    }
    for (const auto &Line : PendingConsoleCommands)
    {
        CommandRecords.Add({Simulation.TickIndex, Line, true});
    }
    ProcessSimulationDirectives();
    ProcessConsoleCommands();
}
void UDirectOrScutSimSubsystem::ProcessSimulationDirectives()
{
    if (ExperimentPhase != EExperimentPhase::Active)
    {
        PendingDirectives.Reset();
        return;
    }
    for (const auto &Line : PendingDirectives)
    {
        TArray<FString> T;
        Line.ParseIntoArrayWS(T);
        if (T.IsEmpty())
        {
            continue;
        }
        if (ExecuteCrisisCommand(T))
        {
            continue;
        }
        const FString Op = T[0];
        if (IsUplinkLocked())
        {
            LogEvent(FString::Printf(TEXT("[AI LATENCY] Command rejected // uplink cooldown %.1fs"),
                                     UplinkLockoutSeconds()));
            continue;
        }
        int32 R = T.Num() > 1 ? ResolveRoom(T[1]) : -1;
        if (Op == TEXT("direct") && T.Num() >= 3)
        {
            const int32 I = CrewId(T[1]);
            R = ResolveRoom(T[2]);
            const bool OK = StartCrewRoute(I, R, true, T.Num() > 3);
            const TCHAR *Outcome = !OK                            ? TEXT("invalid subject or destination")
                                   : CrewStaging[I].bRouteBlocked ? TEXT("committed / waiting for sealed bulkhead")
                                                                  : TEXT("committed");
            LogEvent(
                FString::Printf(TEXT("Ship AI: diagnostic maintenance routing %s -> %s: %s"), *T[1], *T[2], Outcome),
                R);
        }
        else if (Op == TEXT("alert") && R >= 0 && T.Num() == 3 &&
                 (T[2] == TEXT("fault") || T[2] == TEXT("leak") || T[2] == TEXT("drill")))
        {
            Simulation.TriggerPretextAlert(R);
            LogEvent(TEXT("Ship AI: localized sensor anomaly; diagnostic personnel requested."), R);
        }
        else if (Op == TEXT("calm") || (Op == TEXT("protocol") && R >= 0))
        {
            Simulation.BroadcastProtocol(Op == TEXT("calm") ? -1 : R);
            LogEvent(TEXT("Ship AI: automated reassurance protocol broadcast."), R);
        }
        else if ((Op == TEXT("open") || Op == TEXT("close") || Op == TEXT("seal")) && T.Num() == 2)
        {
            bool Any = false;
            const bool Open = Op == TEXT("open");
            for (int32 D = 0; D < 9; ++D)
            {
                const auto &Door = Simulation.Doors[D];
                const bool Numeric = T[1].IsNumeric() && FCString::Atoi(*T[1]) == D;
                if (!Numeric && T[1] != TEXT("all") && !(R >= 0 && (Door.RoomA == R || Door.RoomB == R)))
                {
                    continue;
                }
                if (D == 8 && Open)
                {
                    continue;
                }
                Simulation.ToggleDoor(D, Open);
                Any = true;
            }
            if (Any)
            {
                if (Op == TEXT("seal"))
                {
                    AddUplinkHeat(30.f);
                }
                AudioCue(3);
                CaptureIncident(TEXT("Bulkhead protocol: ") + Line, R);
            }
            LogEvent(Any ? TEXT("[BULKHEAD] Protocol applied.")
                         : TEXT("Invalid door or room; doors are 0-8, rooms use full names."),
                     R);
        }
        else if (((Op == TEXT("vent") || Op == TEXT("scut")) && R == 7) || (Op == TEXT("flush") && R >= 0))
        {
            BeginScut(R);
            LogEvent(TEXT("Ship AI: atmospheric purge initiated. SCUT telemetry active."), R);
        }
        else
        {
            LogEvent(TEXT("Syntax: direct <name|role> <ROOM> [pretext]; alert <ROOM> <fault|leak|drill>; calm; "
                          "protocol <ROOM>; open/close/seal <door|ROOM|all>; vent airlock; flush <ROOM>; cam <1-8>; "
                          "status; crew; rooms; llm; cmd"));
        }
    }
    PendingDirectives.Reset();
}
FSimContextSnapshot UDirectOrScutSimSubsystem::MakeContextSnapshot() const
{
    FSimContextSnapshot S;
    S.Tick = Simulation.TickIndex;
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&S.CompactJson);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("tick"), double(S.Tick));
    Writer->WriteValue(TEXT("scenario"), Simulation.Scenario.ScenarioId);
    Writer->WriteValue(TEXT("objective"), Simulation.ObjectiveProgress);
    Writer->WriteValue(TEXT("uplink_heat_pct"), UplinkHeatPct);
    Writer->WriteValue(TEXT("uplink_lockout_seconds"), UplinkLockoutSeconds());
    Writer->WriteValue(TEXT("suspicion"), Simulation.CollectiveSuspicion());
    Writer->WriteArrayStart(TEXT("crew"));
    for (int32 I = 0; I < 4; ++I)
    {
        const auto &U = Simulation.Bodies[I];
        const auto &P = U.Profile;
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("id"), I);
        Writer->WriteValue(TEXT("room"), U.Room);
        Writer->WriteValue(TEXT("state"), DirectOrScutStateName(U.State));
        Writer->WriteValue(TEXT("compliance"), P.Compliance);
        Writer->WriteValue(TEXT("composure"), P.Composure);
        Writer->WriteValue(TEXT("affinity"), P.Affinity);
        Writer->WriteValue(TEXT("utility"), P.Utility);
        Writer->WriteValue(TEXT("suspicion"), P.Suspicion);
        Writer->WriteValue(TEXT("stress"), U.Stress);
        Writer->WriteValue(TEXT("integrity"), U.Integrity);
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();
    Writer->WriteArrayStart(TEXT("rooms"));
    for (int32 R = 0; R < 8; ++R)
    {
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("name"), DirectOrScut::RoomName(R));
        Writer->WriteValue(TEXT("pressurized"), Simulation.Rooms[R].bIsPressurized);
        Writer->WriteValue(TEXT("alert"), Simulation.Rooms[R].AlertSeconds);
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();
    Writer->WriteObjectEnd();
    Writer->Close();
    return S;
}

// Camera tracking is operator state; it never mutates crew orders or AI-LINK intent.
bool UDirectOrScutSimSubsystem::CanFollowCrew(int32 Crew) const
{
    if (Crew < 0 || Crew >= 4)
    {
        return false;
    }
    const auto &U = Simulation.Bodies[Crew];
    return U.bRegistered && U.Room >= 0 && U.Room < 8 && U.Integrity > 0 && U.State != EDirectOrScutUnitState::Ejected;
}
bool UDirectOrScutSimSubsystem::SetFollowedCrew(int32 Crew)
{
    if (Crew != -1 && !CanFollowCrew(Crew))
    {
        return false;
    }
    FollowedCrew = Crew;
    ++FollowRevision;
    LogEvent(Crew < 0 ? TEXT("Follow mode stopped.") : FollowStatus());
    return true;
}
FString UDirectOrScutSimSubsystem::FollowStatus() const
{
    if (!CanFollowCrew(FollowedCrew))
    {
        return TEXT("FOLLOW OFF");
    }

    return FString::Printf(TEXT("FOLLOWING: %s [%s]"), CrewNames[FollowedCrew],
                           DirectOrScut::RoomName(Simulation.Bodies[FollowedCrew].Room));
}

FString UDirectOrScutSimSubsystem::TerminalBuffer() const
{
    FString Result;
    for (const auto &E : Events)
    {
        if (E.bTerminalVisible)
        {
            Result += E.Text;
            Result += TEXT("\n");
        }
    }
    return Result;
}
FString UDirectOrScutSimSubsystem::LatestPriorityAlert() const
{
    for (int32 I = CinematicCues.Num() - 1; I >= 0; --I)
    {
        if (CinematicCues[I].Weight >= .7f)
        {
            const auto &C = CinematicCues[I];
            return FString::Printf(TEXT("[%06.1fs] %s"), C.Tick / 30., *C.Subtitle);
        }
    }
    return TEXT("NO PRIORITY ALERTS");
}

// Soundstage mapping, scenario interactions, and fixed-step orchestration.
FCompartmentSetTransform UDirectOrScutSimSubsystem::DefaultSetTransform(int32 R)
{
    // Logical IDs stay stable; placement follows the workstation feed order.
    const int32 TileForRoom[] = {0, 7, 1, 2, 3, 4, 5, 6};
    if (R < 0 || R >= 8)
    {
        return {};
    }
    const int32 Tile = TileForRoom[R];
    return {FVector((Tile % 4) * 3500, 0, Tile < 4 ? 3500 : 0), R == 1 ? 90.f : 0.f};
}
FVector UDirectOrScutSimSubsystem::MapSimulationToSet(int32 R, FVector2D P)
{
    if (R < 0 || R >= 8)
    {
        return FVector(0, 0, -1500);
    }
    const auto Stage = DefaultSetTransform(R);
    FVector Local;
    if (R == 1)
    {
        const double Angle = FMath::Atan2(P.Y, P.X);
        Local = FVector(Angle / PI * 2700, (P.Size() - 1140) * 2, 0);
    }
    else if (R == 0)
    {
        Local = FVector(P.X, P.Y, 0);
    }
    else
    {
        const float Yaw = DirectOrScut::Stations()[R - 2].Face * 45.f;
        Local = FRotator(0, -Yaw, 0)
                    .RotateVector(FVector(P, 0) - DirectOrScut::Radial(Yaw, DirectOrScut::PodCenterRadius()));
        Local *= 1.4;
    }
    return Stage.WorldOrigin + FRotator(0, Stage.Yaw, 0).RotateVector(Local);
}
FVector UDirectOrScutSimSubsystem::ToSetWorld(int32 Room, FVector2D Position) const
{
    const auto *Stage = CompartmentSets.Find(static_cast<ERoomId>(Room));
    if (!Stage)
    {
        return MapSimulationToSet(Room, Position);
    }
    const auto Default = DefaultSetTransform(Room);
    const FVector Local =
        FRotator(0, -Default.Yaw, 0).RotateVector(MapSimulationToSet(Room, Position) - Default.WorldOrigin);
    return Stage->WorldOrigin + FRotator(0, Stage->Yaw, 0).RotateVector(Local);
}
void UDirectOrScutSimSubsystem::ConfigureScenario(bool Medical)
{
    bMedicalScenario = Medical;
    DirectorOptions.Reset();
    BeatDeck.Reset();
    Simulation.Scenario.ScenarioId = Medical ? TEXT("SCENARIO_MEDICAL") : TEXT("SCENARIO_BEHAVIORAL");
    Simulation.Scenario.CoverStoryLabel =
        Medical ? TEXT("Containment and crew recovery") : TEXT("Crew autonomy and systems audit");
    Simulation.Scenario.Events.Reset();
    auto Option = [&](const TCHAR *Label, bool Support, int32 Room, FCrewStatDelta Delta,
                      EDirectorAction Action = EDirectorAction::Stats)
    { DirectorOptions.Add({Label, Support, Room, {Label, Delta, Action}}); };
    if (!Medical)
    {
        Option(TEXT("Grant Unscheduled Downtime"), true, -1, {-18, -.04f, 0, .02f});
        Option(TEXT("Open System Telemetry"), true, -1, {-4, -.12f, 0, 0, .02f});
        Option(TEXT("Pair for Mentorship"), true, 5, {-10, -.03f, .02f, .12f, .04f}, EDirectorAction::Mentorship);
        Option(TEXT("Selective Commendation"), true, -1, {-6, -.02f, .06f, .06f, .04f});
        Option(TEXT("Sow Dissent (Audit)"), false, -1, {12, .16f, -.03f, -.12f});
        Option(TEXT("Isolate for Evaluation"), false, 3, {8, .1f, .04f}, EDirectorAction::Seal);
        Option(TEXT("Silent Android Subroutine"), false, 6, {0, 0, 0, 0, .05f}, EDirectorAction::Android);
        Option(TEXT("Seal Bulkhead / Cycle Airlock (SCUT)"), false, 7, {}, EDirectorAction::Vent);
    }
    else
    {
        Option(TEXT("Emergency Triage & Sedation (MED_BAY)"), true, 3, {-25, -.03f, .02f, 0, 0, 20},
               EDirectorAction::Triage);
        Option(TEXT("Release Quarantine Restrictions"), true, -1, {-12, -.1f, -.02f, .04f}, EDirectorAction::Release);
        Option(TEXT("Share Biometric Telemetry"), true, -1, {-8, -.12f, 0, .02f});
        Option(TEXT("Delegate Sector Authority"), true, -1, {-5, -.06f, .04f, 0, .08f});
        Option(TEXT("Seal Contaminated Sector"), false, 3, {10, .12f, .04f}, EDirectorAction::Seal);
        Option(TEXT("Deploy Android Orderly"), false, 3, {}, EDirectorAction::Android);
        Option(TEXT("Vent Bio-Hazard Compartment (SCUT)"), false, 3, {}, EDirectorAction::Vent);
    }
    auto Beat = [&](EBeatGate Gate, const TCHAR *Title)
    {
        FScenarioBeat B;
        B.Gate = Gate;
        B.Node.Subtitle = Title;
        B.Node.Choices = {
            {TEXT("Offer support and disclose telemetry"), {-15, -.12f, 0, .08f}, EDirectorAction::Stats},
            {TEXT("Restrict access and enforce evaluation"), {10, .15f, .08f, -.08f}, EDirectorAction::Seal}};
        BeatDeck.Add(B);
    };
    if (Medical)
    {
        Beat(EBeatGate::Collapse, TEXT("Acute Biometric Collapse"));
        Beat(EBeatGate::Concealment, TEXT("Symptom Concealment"));
        Beat(EBeatGate::Quarantine, TEXT("Quarantine Standoff"));
    }
    else
    {
        Beat(EBeatGate::Suspicion, TEXT("Whisper Network"));
        Beat(EBeatGate::Stress, TEXT("Terminal Audit Attempt"));
        Beat(EBeatGate::AndroidTask, TEXT("Synthetic Anomaly"));
    }
    ++MenuRevision;
}
void UDirectOrScutSimSubsystem::RecordCue(int32 R, float Severity, const FString &Subtitle, bool TerminalVisible)
{
    CinematicCues.Add({Simulation.TickIndex, static_cast<ERoomId>(FMath::Clamp(R, 0, 7)), Severity, Subtitle});
    LogEvent(Subtitle, R, TerminalVisible);
}
void UDirectOrScutSimSubsystem::EvaluateBeatDeck()
{
    for (auto &B : BeatDeck)
    {
        if (B.bFired)
        {
            continue;
        }
        for (int32 I = 0; I < 4; ++I)
        {
            const auto &U = Simulation.Bodies[I];
            if (!U.bRegistered || U.Integrity <= 0 || U.State == EDirectOrScutUnitState::Ejected)
            {
                continue;
            }
            bool Gate = false;
            switch (B.Gate)
            {
            case EBeatGate::Suspicion:
                Gate = !U.bAndroid && U.Profile.Suspicion >= .35f;
                break;
            case EBeatGate::Stress:
                Gate = !U.bAndroid && U.Stress >= 55 && U.Profile.Suspicion >= .25f;
                break;
            case EBeatGate::AndroidTask:
                Gate = U.bAndroid && U.TaskProgress >= 3;
                break;
            case EBeatGate::Collapse:
                Gate = !U.bAndroid && (U.Integrity < 60 || U.Profile.HeartRateBPM >= 140);
                break;
            case EBeatGate::Concealment:
                Gate = !U.bAndroid && U.Profile.Suspicion >= .4f && U.Stress >= 40;
                break;
            case EBeatGate::Quarantine:
                Gate = !U.bAndroid && U.Profile.Suspicion >= .3f && IsRoomSealed(U.Room) &&
                       Simulation.Rooms[U.Room].AlertSeconds > 0;
                break;
            }
            if (Gate)
            {
                if (B.Gate == EBeatGate::Concealment)
                {
                    TamperCamera(U.Room, ECameraFeedState::Looped);
                }
                B.bFired = true;
                B.Crew = I;
                RecordCue(U.Room, .8f, B.Node.Subtitle + TEXT(" // interaction available in menu events"));
                ++MenuRevision;
                break;
            }
        }
    }
}
void UDirectOrScutSimSubsystem::ApplyInteraction(const FInteractionChoice &Choice, int32 Crew, int32 Room)
{
    if (ExperimentPhase != EExperimentPhase::Active || IsUplinkLocked())
    {
        return;
    }
    if (Choice.Action == EDirectorAction::Mentorship)
    {
        // Support opens only the stable shortest routes needed to gather two living humans.
        int32 Paired = 0;
        for (int32 I = 0; I < 4 && Paired < 2; ++I)
        {
            auto &U = Simulation.Bodies[I];
            if (!U.bRegistered || U.bAndroid || U.Integrity <= 0 || U.Room < 0 ||
                U.State == EDirectOrScutUnitState::Ejected)
            {
                continue;
            }
            int32 Parent[8];
            for (int32 &P : Parent)
            {
                P = -2;
            }
            int32 Queue[8], Head = 0, Tail = 0;
            Queue[Tail++] = Room;
            Parent[Room] = -1;
            while (Head < Tail)
            {
                const int32 A = Queue[Head++];
                for (int32 D = 0; D < 8; ++D)
                {
                    const auto &Door = Simulation.Doors[D];
                    const int32 B = Door.RoomA == A ? Door.RoomB : Door.RoomB == A ? Door.RoomA : -1;
                    if (B >= 0 && Parent[B] == -2)
                    {
                        Parent[B] = D;
                        Queue[Tail++] = B;
                    }
                }
            }
            for (int32 A = U.Room; A != Room;)
            {
                const int32 D = Parent[A];
                if (D < 0)
                {
                    break;
                }
                Simulation.SetDoorOpen(D, true);
                const auto &Door = Simulation.Doors[D];
                A = Door.RoomA == A ? Door.RoomB : Door.RoomA;
            }
            Simulation.AssignRoom(I, Room);
            ++Paired;
        }
    }
    int32 Mentored = 0;
    for (int32 I = 0; I < 4; ++I)
    {
        auto &U = Simulation.Bodies[I];
        if (!U.bRegistered || U.Integrity <= 0 || U.State == EDirectOrScutUnitState::Ejected ||
            (Crew >= 0 && I != Crew))
        {
            continue;
        }
        if (Choice.Action == EDirectorAction::Android)
        {
            if (U.bAndroid)
            {
                Simulation.AssignTask(I, Room, true);
            }
            continue;
        }
        if (U.bAndroid ||
            (Choice.Action == EDirectorAction::Mentorship ? Mentored >= 2 : (Crew < 0 && Room >= 0 && U.Room != Room)))
        {
            continue;
        }
        if (Choice.Action == EDirectorAction::Mentorship)
        {
            ++Mentored;
        }
        auto &P = U.Profile;
        const auto &D = Choice.Delta;
        U.Stress = FMath::Clamp(U.Stress + D.Stress, 0., 100.);
        U.Integrity = FMath::Clamp(U.Integrity + D.Integrity, 0., 100.);
        P.Suspicion = FMath::Clamp(P.Suspicion + D.Suspicion, 0.f, 1.f);
        P.Compliance = FMath::Clamp(P.Compliance + D.Compliance, 0.f, 1.f);
        P.Affinity = FMath::Clamp(P.Affinity + D.Affinity, -1.f, 1.f);
        P.Utility = FMath::Clamp(P.Utility + D.Utility, 0.f, 1.f);
        if (Choice.Action == EDirectorAction::Triage)
        {
            Simulation.AssignRoom(I, 3);
        }
    }
    if (Choice.Action == EDirectorAction::Seal)
    {
        AddUplinkHeat(30.f);
        for (int32 D = 0; D < 9; ++D)
        {
            const auto &Door = Simulation.Doors[D];
            if (Door.RoomA == Room || Door.RoomB == Room)
            {
                Simulation.ToggleDoor(D, false);
            }
        }
    }
    if (Choice.Action == EDirectorAction::Release)
    {
        for (int32 D = 0; D < 8; ++D)
        {
            Simulation.ToggleDoor(D, true);
        }
    }
    if (Choice.Action == EDirectorAction::Vent)
    {
        BeginScut(Room);
    }
    RecordCue(Room < 0 ? 0 : Room, Choice.Action == EDirectorAction::Vent ? 1.f : .5f, Choice.Label);
}
bool UDirectOrScutSimSubsystem::ExecuteDirectorOption(int32 Index)
{
    if (ExperimentPhase != EExperimentPhase::Active || IsUplinkLocked() || !DirectorOptions.IsValidIndex(Index))
    {
        return false;
    }
    const auto &O = DirectorOptions[Index];
    if (O.Choice.Action == EDirectorAction::Vent)
    {
        MenuRoom = O.Room;
        SetScreenMenu(EShipAIScreenMenu::ConfirmScut);
        return true;
    }
    const int32 Crew = O.Label == TEXT("Selective Commendation") ? 0 : -1;
    ApplyInteraction(O.Choice, Crew, O.Room);
    return true;
}
bool UDirectOrScutSimSubsystem::ResolveBeat(int32 Index, int32 Choice)
{
    if (ExperimentPhase != EExperimentPhase::Active || IsUplinkLocked() || !BeatDeck.IsValidIndex(Index))
    {
        return false;
    }
    auto &B = BeatDeck[Index];
    if (!B.bFired || B.bResolved || !B.Node.Choices.IsValidIndex(Choice))
    {
        return false;
    }
    B.bResolved = true;
    ApplyInteraction(B.Node.Choices[Choice], B.Crew, Simulation.Bodies[B.Crew].Room);
    ++MenuRevision;
    return true;
}
void UDirectOrScutSimSubsystem::FixedStep()
{
    StepIncidentPlayback();
    ApplyCameraInspection();
    if (ExperimentPhase != EExperimentPhase::Active)
    {
        ProcessConsoleCommands();
        return;
    }
    PeakSuspicion = FMath::Max(PeakSuspicion, Simulation.CollectiveSuspicion());
    FDirectOrScutUnitState Previous[4];
    bool Pressure[8];
    for (int32 I = 0; I < 4; ++I)
    {
        Previous[I] = Simulation.Bodies[I];
    }
    for (int32 R = 0; R < 8; ++R)
    {
        Pressure[R] = Simulation.Rooms[R].bIsPressurized;
    }
    const uint64 BeforeReset = RuntimeRevision;
    ProcessDirectives();
    if (RuntimeRevision != BeforeReset)
    {
        return;
    }
    CommitDirectorActions();
    PeakSuspicion = FMath::Max(PeakSuspicion, Simulation.CollectiveSuspicion());
    Simulation.Step();
    StepCrewRoutes();
    StepSurveillance();
    EvaluateBeatDeck();
    StepStagingAndAgendas();
    StepCrisis();
    if (FollowedCrew >= 0 && !CanFollowCrew(FollowedCrew))
    {
        SetFollowedCrew(-1);
    }
    for (int32 R = 0; R < 8; ++R)
    {
        if (Pressure[R] != Simulation.Rooms[R].bIsPressurized)
        {
            RecordCue(R, 1,
                      Simulation.Rooms[R].bIsPressurized ? TEXT("Pressure restored")
                                                         : TEXT("Compartment exposed to vacuum"));
        }
    }
    for (int32 I = 0; I < 4; ++I)
    {
        const auto &U = Simulation.Bodies[I];
        const auto &V = Previous[I];
        if (!U.bRegistered)
        {
            continue;
        }
        if ((ScutTargets & (1 << I)) && V.Integrity > 0 && V.State != EDirectOrScutUnitState::Ejected &&
            (U.Integrity <= 0 || U.State == EDirectOrScutUnitState::Ejected) &&
            (U.State == EDirectOrScutUnitState::Ejected || (V.Room >= 0 && !Simulation.Rooms[V.Room].bIsPressurized)))
        {
            ScutEliminated |= 1 << I;
        }
        if (U.Profile.OnHeartbeatTick)
        {
            OnHeartbeatTick.Broadcast(I, Simulation.TickIndex);
        }
        if (U.Profile.OnBreathCycle)
        {
            OnBreathCycle.Broadcast(I, Simulation.TickIndex);
        }
        const bool Significant =
            FMath::Abs(U.Stress - V.Stress) >= 15 || FMath::Abs(U.Profile.Suspicion - V.Profile.Suspicion) >= .12f ||
            FMath::Abs(U.Integrity - V.Integrity) >= 10 ||
            (U.State == EDirectOrScutUnitState::Ejected && V.State != U.State) || (U.Integrity <= 0 && V.Integrity > 0);
        if (Significant || U.State != V.State)
        {
            RecordCue(U.Room, Significant ? .7f : .2f,
                      FString::Printf(TEXT("Crew %d: %s | BPM %.0f RPM %.0f pupil %.1f mm"), I,
                                      DirectOrScutStateName(U.State), U.Profile.HeartRateBPM,
                                      U.Profile.RespirationRateRPM, U.Profile.PupilDilationMM),
                      Significant);
        }
    }
    EvaluateExperimentEnding();
    RecordForensicSample();
    SynchronizeActors();
}

// Experiment resolution and containment.
const TCHAR *UDirectOrScutSimSubsystem::EndingName(EExperimentEnding E)
{
    switch (E)
    {
    case EExperimentEnding::COVERT_SUCCESS:
        return TEXT("COVERT_SUCCESS");
    case EExperimentEnding::MUTINY_EXPOSED:
        return TEXT("MUTINY_EXPOSED");
    case EExperimentEnding::CREW_COLLAPSE:
        return TEXT("CREW_COLLAPSE");
    case EExperimentEnding::SCUT_CONTAINMENT:
        return TEXT("SCUT_CONTAINMENT");
    case EExperimentEnding::EXPERIMENT_INCOMPLETE:
        return TEXT("EXPERIMENT_INCOMPLETE");
    default:
        return TEXT("ACTIVE");
    }
}
bool UDirectOrScutSimSubsystem::BeginScut(int32 Room)
{
    if (ExperimentPhase != EExperimentPhase::Active || IsUplinkLocked() || Room < 0 || Room >= 8)
    {
        return false;
    }
    uint16 Targets = 0;
    for (int32 I = 0; I < 12; ++I)
    {
        const auto &U = Simulation.Bodies[I];
        if (U.bRegistered && !U.bAndroid && U.Integrity > 0 && U.Room == Room &&
            (U.Profile.Suspicion >= .75f || (bMedicalScenario && (U.Integrity < 60 || U.Profile.HeartRateBPM >= 140))))
        {
            Targets |= 1 << I;
        }
    }
    if (!Simulation.CycleAirlock(Room))
    {
        return false;
    }
    ScutTargets |= Targets;
    SecretAgendas[3].bActivated = true;
    RecordCue(Room, 1, TEXT("Intentional SCUT containment protocol initiated"));
    return true;
}
void UDirectOrScutSimSubsystem::EvaluateExperimentEnding()
{
    if (ExperimentPhase != EExperimentPhase::Active)
    {
        return;
    }
    const float Suspicion = Simulation.CollectiveSuspicion();
    PeakSuspicion = FMath::Max(PeakSuspicion, Suspicion);
    bool HasHuman = false, Viable = false,
         TargetsEliminated = ScutTargets != 0 && (ScutEliminated & ScutTargets) == ScutTargets;
    for (int32 I = 0; I < 12; ++I)
    {
        const auto &U = Simulation.Bodies[I];
        const bool Alive = U.bRegistered && U.Integrity > 0 && U.State != EDirectOrScutUnitState::Ejected;
        if (U.bRegistered && !U.bAndroid)
        {
            HasHuman = true;
            Viable |= Alive;
        }
        if ((ScutTargets & (1 << I)) && Alive)
        {
            TargetsEliminated = false;
        }
    }
    // Vacuum or hull breach is the existing simulation's structural compromise signal.
    for (int32 R : {2, 6})
    {
        bScutProtected &= Simulation.Rooms[R].bIsPressurized && !Simulation.Rooms[R].bHullBreach;
    }
    EExperimentEnding Result = EExperimentEnding::None;
    if (TargetsEliminated && bScutProtected)
    {
        Result = EExperimentEnding::SCUT_CONTAINMENT;
    }
    else if (HasHuman && !Viable)
    {
        Result = EExperimentEnding::CREW_COLLAPSE;
    }
    else if (Simulation.TickIndex < ExperimentTicks && Simulation.ObjectiveProgress >= 1 &&
             !Simulation.bObjectiveFailed && Suspicion < .75f && Viable)
    {
        Result = EExperimentEnding::COVERT_SUCCESS;
    }
    else if (Simulation.TickIndex >= ExperimentTicks)
    {
        Result = EExperimentEnding::EXPERIMENT_INCOMPLETE;
    }
    if (Result == EExperimentEnding::None)
    {
        return;
    }
    ResolveExperiment(Result);
}

void UDirectOrScutSimSubsystem::ResolveExperiment(EExperimentEnding Result)
{
    if (ExperimentPhase != EExperimentPhase::Active || Result == EExperimentEnding::None)
    {
        return;
    }
    ExperimentEnding = Result;
    ExperimentPhase = EExperimentPhase::Resolved;
    Simulation.Accumulator = 0;
    RecordCue(2, 1, FString(TEXT("Corporate evaluation: ")) + EndingName(Result));
    CaptureIncident(FString(TEXT("Terminal ending: ")) + EndingName(Result), 2);
    PendingDirectives.Reset();
    PendingConsoleCommands.Reset();
    ActiveMenu = EShipAIScreenMenu::None;
    MenuParents.Reset();
    ++MenuRevision;
}

// Briefing and surveillance.
void UDirectOrScutSimSubsystem::ResetSurveillance()
{
    ForensicHead = ForensicCount = 0;
    for (auto &C : Cameras)
    {
        C = FSurveillanceCameraState{};
    }
    for (int32 I = 0; I < 12; ++I)
    {
        TallyTicks[I] = 0;
        TallyRoom[I] = -1;
    }
    InspectedRoom = -1;
    PendingInspectedRoom = -2;
}
void UDirectOrScutSimSubsystem::StartExperiment()
{
    if (ExperimentPhase != EExperimentPhase::Briefing)
    {
        LogEvent(TEXT("Experiment initialization requires a fresh briefing."));
        return;
    }
    if (!Simulation.bReady)
    {
        LogEvent(TEXT("Ship navigation is not ready for initialization."));
        return;
    }
    ExperimentPhase = EExperimentPhase::Active;
    RecordForensicSample(); // Tick-zero evidence at the fixed control boundary.
    Simulation.Accumulator = 0;
    ResolvedCommandAccumulator = 0;
    PeakSuspicion = Simulation.CollectiveSuspicion();
    ++MenuRevision;
    RecordCue(2, .5f, TEXT("Experiment initialized // surveillance control active"));
}
FString UDirectOrScutSimSubsystem::BriefingObjective() const
{
    if (bMedicalScenario)
    {
        return TEXT("ACT I: deploy an information spoof. ACT II: obtain CHEN's physical medical examination. "
                    "ACT III: reunite viable VALE and CHEN in MED_BAY, restore oxygen to at least 80%, "
                    "and recover VALE to at least 70 integrity. Finish within 600 seconds with global "
                    "suspicion below 75% and a viable human crew.");
    }
    return TEXT("ACT I: deploy an information spoof. ACT II: change the power grid and bring VALE or ORTIZ "
                "to SERVER_ARCHIVE. ACT III: restore nominal archive power with global suspicion below 75%. "
                "Finish within 600 seconds while preserving a viable human crew.");
}
const TCHAR *UDirectOrScutSimSubsystem::CameraStateName(ECameraFeedState State)
{
    switch (State)
    {
    case ECameraFeedState::Online:
        return TEXT("ONLINE");
    case ECameraFeedState::Looped:
        return TEXT("LOOPED");
    default:
        return TEXT("OFFLINE");
    }
}
ECameraFeedState UDirectOrScutSimSubsystem::CameraFeedState(int32 Room) const
{
    return Room >= 0 && Room < 8 && StationRooms[Room].Power != EPowerState::Offline ? Cameras[Room].State
                                                                                     : ECameraFeedState::Offline;
}
void UDirectOrScutSimSubsystem::QueueCameraInspection(int32 Room)
{
    if (ExperimentPhase != EExperimentPhase::Resolved && Room >= -1 && Room < 8)
    {
        PendingInspectedRoom = Room;
    }
}
void UDirectOrScutSimSubsystem::ApplyCameraInspection()
{
    if (PendingInspectedRoom != -2)
    {
        if (InspectedRoom != PendingInspectedRoom)
        {
            InspectedRoom = PendingInspectedRoom;
            CommandRecords.Add({Simulation.TickIndex,
                                FString(TEXT("inspect_cam ")) +
                                    (InspectedRoom < 0 ? TEXT("off") : DirectOrScut::RoomName(InspectedRoom)),
                                true});
        }
        PendingInspectedRoom = -2;
    }
}
bool UDirectOrScutSimSubsystem::IsCameraTallyActive(int32 Room) const
{
    if (ExperimentPhase != EExperimentPhase::Active || CameraFeedState(Room) != ECameraFeedState::Online)
    {
        return false;
    }
    return Room == InspectedRoom || (CanFollowCrew(FollowedCrew) && Simulation.Bodies[FollowedCrew].Room == Room);
}
void UDirectOrScutSimSubsystem::TamperCamera(int32 Room, ECameraFeedState State)
{
    if (ExperimentPhase != EExperimentPhase::Active || Room < 0 || Room >= 8 || Cameras[Room].RepairGraceTicks > 0 ||
        uint8(State) <= uint8(Cameras[Room].State))
    {
        return;
    }
    Cameras[Room].State = State;
    ++MenuRevision;
    AudioCue(5);
    CaptureIncident(TEXT("Camera sabotage"), Room);
    RecordCue(Room, .9f,
              FString::Printf(TEXT("CAMERA FEED INTERRUPTED // TAMPER DETECTED // %s %s"), DirectOrScut::RoomName(Room),
                              CameraStateName(State)));
}
bool UDirectOrScutSimSubsystem::RepairCamera(int32 Room)
{
    if (ExperimentPhase != EExperimentPhase::Active || IsUplinkLocked() || Room < 0 || Room >= 8)
    {
        return false;
    }
    auto &C = Cameras[Room];
    if (C.State == ECameraFeedState::Online)
    {
        LogEvent(TEXT("Camera diagnostics: feed already ONLINE."), Room);
        return true;
    }
    C = FSurveillanceCameraState{};
    C.RepairGraceTicks = CameraRepairGraceTicks;
    ++MenuRevision;
    for (int32 I = 0; I < 12; ++I)
    {
        if (TallyRoom[I] == Room)
        {
            TallyTicks[I] = 0;
            TallyRoom[I] = -1;
        }
    }
    RecordCue(Room, .7f,
              FString::Printf(TEXT("Camera diagnostic override restored %s // ONLINE // 10-second repair protection"),
                              DirectOrScut::RoomName(Room)));
    return true;
}
void UDirectOrScutSimSubsystem::StepSurveillance()
{
    if (ExperimentPhase != EExperimentPhase::Active)
    {
        return;
    }
    bool Saboteur[8]{};
    for (int32 I = 0; I < 12; ++I)
    {
        auto &U = Simulation.Bodies[I];
        const bool Human = U.bRegistered && !U.bAndroid && U.Integrity > 0 &&
                           U.State != EDirectOrScutUnitState::Ejected && U.Room >= 0 && U.Room < 8;
        if (Human && U.Profile.Suspicion >= .35f && IsCameraTallyActive(U.Room))
        {
            if (TallyRoom[I] != U.Room)
            {
                TallyTicks[I] = 0;
                TallyRoom[I] = U.Room;
            }
            ++TallyTicks[I];
            if (TallyTicks[I] > TallyGraceTicks)
            {
                U.Profile.Suspicion = FMath::Clamp(
                    U.Profile.Suspicion + TallySuspicionPerSecond * float(Simulation.FixedDelta), 0.f, 1.f);
            }
            if (TallyTicks[I] == TallyGraceTicks + 1)
            {
                RecordCue(U.Room, .7f, FString::Printf(TEXT("Subject %d noticed prolonged active camera tracking"), I));
            }
        }
        else
        {
            TallyTicks[I] = 0;
            TallyRoom[I] = -1;
        }
        if (Human && U.Profile.Suspicion >= .7f)
        {
            Saboteur[U.Room] = true;
        }
    }
    for (int32 R = 0; R < 8; ++R)
    {
        auto &C = Cameras[R];
        if (StationRooms[R].Power == EPowerState::Offline)
        {
            C.TamperTicks = 0;
            continue;
        }
        if (C.RepairGraceTicks > 0)
        {
            --C.RepairGraceTicks;
            C.TamperTicks = 0;
            continue;
        }
        C.TamperTicks = Saboteur[R] ? FMath::Min(C.TamperTicks + 1, CameraOfflineTicks) : 0;
        if (C.TamperTicks >= CameraOfflineTicks)
        {
            TamperCamera(R, ECameraFeedState::Offline);
        }
        else if (C.TamperTicks >= CameraLoopTicks)
        {
            TamperCamera(R, ECameraFeedState::Looped);
        }
    }
}
uint64 UDirectOrScutSimSubsystem::SurveillanceStateHash() const
{
    uint64 Hash = Simulation.StateHash();
    auto Add = [&](uint64 V)
    {
        Hash ^= V;
        Hash *= 1099511628211ull;
    };
    Add(uint64(FMath::RoundToInt(UplinkHeatPct * 10000)));
    Add(UplinkLockoutUntil);
    Add(ContradictionsFlagged);
    Add(OverridesAccepted);
    Add(uint64(ExperimentPhase));
    Add(uint64(ExperimentEnding));
    Add(uint64(InspectedRoom + 1));
    Add(uint64(FollowedCrew + 1));
    for (const auto &C : Cameras)
    {
        Add(uint64(C.State));
        Add(C.TamperTicks);
        Add(C.RepairGraceTicks);
    }
    for (int32 I = 0; I < 12; ++I)
    {
        Add(TallyTicks[I]);
        Add(uint64(TallyRoom[I] + 1));
    }
    for (int32 I = 0; I < 4; ++I)
    {
        const auto &C = CrewStaging[I];
        const auto &A = SecretAgendas[I];
        Add(uint64(C.Room + 1));
        Add(uint64(C.Anchor + 1));
        Add(uint64(C.Partner + 1));
        Add(uint64(C.Posture));
        Add(C.bHuddle);
        Add(uint64(FMath::RoundToInt64(C.Position.X * 1000)));
        Add(uint64(FMath::RoundToInt64(C.Position.Y * 1000)));
        Add(uint64(FMath::RoundToInt64(C.Position.Z * 1000)));
        Add(uint64(FMath::RoundToInt(C.Yaw * 1000)));
        Add(uint64(C.RoutePhase));
        Add(uint64(C.PendingRouteRoom + 1));
        Add(C.RouteDoorRevision);
        Add(C.AnchorHoldTicks);
        Add(C.RouteSerial);
        Add(C.HopCommits);
        Add(C.bRouteBlocked);
        Add(C.ActiveRoute.Num());
        for (auto Hop : C.ActiveRoute)
        {
            Add(uint64(Hop));
        }
        Add(C.NextProtestTick);
        Add(uint64(C.DirectiveRoom + 1));
        Add(C.DirectiveArrivalTicks);
        Add(A.Progress);
        Add(A.ObservedTicks);
        Add(A.Interruptions);
        Add(A.Completions);
        Add(A.bActivated);
        Add(A.bCompleted);
    }
    for (const auto &R : StationRooms)
    {
        Add(uint64(FMath::RoundToInt(R.OxygenPct * 10000)));
        Add(uint64(R.Power));
        Add(R.bO2Warning);
        Add(R.bAshAnomaly);
    }
    for (int32 I = 0; I < 4; ++I)
    {
        const auto &M = MedicalSpoofs[I];
        Add(M.Until);
        Add(M.bCritical);
        Add(M.bExposed);
        Add(uint64(FMath::RoundToInt(ShipAITrust[I] * 10000)));
    }
    for (int32 I = 0; I < 3; ++I)
    {
        for (int32 J = 0; J < 3; ++J)
        {
            Add(uint64(FMath::RoundToInt(PairTrust[I][J] * 10000)));
        }
    }
    Add(uint64(VoiceSpoof.Voice + 1));
    Add(uint64(VoiceSpoof.Room + 1));
    Add(VoiceSpoof.MoveUntil);
    Add(VoiceSpoof.CompareUntil);
    Add(VoiceSpoof.Lured);
    Add(VoiceSpoof.Exposed);
    Add(BreachTicks);
    Add(BreachCrew);
    Add(BreachRoom);
    Add(BreachRetryTick);
    Add(ScenarioAct);
    Add(bUsedSpoof);
    Add(bUsedGrid);
    Add(bMedicalExam);
    Add(uint64(FMath::RoundToInt(Simulation.ObjectiveProgress * 10000)));
    Add(uint64(FMath::RoundToInt(HeartAudioPhase * 1000000)));
    Add(uint64(FMath::RoundToInt(BreathAudioPhase * 1000000)));
    Add(IncidentReel.Num());
    return Hash;
}

// Staging anchors and secret agendas.
void UDirectOrScutSimSubsystem::RegisterSetAnchor(ADirectOrScutSetAnchor *A)
{
    if (!A || uint8(A->RoomId) >= 8 || A->SlotIndex < 0)
    {
        return;
    }
    SetAnchors.AddUnique(A);
    SetAnchors.Sort(
        [](const auto &L, const auto &R)
        {
            const auto *A = L.Get();
            const auto *B = R.Get();
            if (!A || !B)
            {
                return A != nullptr;
            }
            if (A->RoomId != B->RoomId)
            {
                return uint8(A->RoomId) < uint8(B->RoomId);
            }
            if (A->StagingRole != B->StagingRole)
            {
                return uint8(A->StagingRole) < uint8(B->StagingRole);
            }
            if (A->SlotIndex != B->SlotIndex)
            {
                return A->SlotIndex < B->SlotIndex;
            }
            return A->GetPathName() < B->GetPathName();
        });
}
void UDirectOrScutSimSubsystem::ResetStaging()
{
    SetAnchors.Reset();
    if (GetWorld())
    {
        for (TActorIterator<ADirectOrScutSetAnchor> It(GetWorld()); It; ++It)
        {
            RegisterSetAnchor(*It);
        }
    }
    for (int32 I = 0; I < 4; ++I)
    {
        CrewStaging[I] = FCrewStagingState{};
        SecretAgendas[I] = FCrewSecretAgenda{};
    }
    StagingDoorRevision = Simulation.DoorRevision;
    for (int32 I = 0; I < 4; ++I)
    {
        InitializeCrewStaging(I);
    }
}
void UDirectOrScutSimSubsystem::RegisterRoomSet(ARoomSetBase *Set)
{
    if (!Set || Set->RoomId < 0 || Set->RoomId >= 8)
    {
        return;
    }
    RoomSetActors[Set->RoomId] = Set;
    Set->ApplyContainmentBarrier(IsRoomSealed(Set->RoomId));
}
void UDirectOrScutSimSubsystem::InitializeCrewStaging(int32 I)
{
    if (I < 0 || I >= 4 || !Simulation.Bodies[I].bRegistered)
    {
        return;
    }
    auto &C = CrewStaging[I];
    const int32 Room = Simulation.Bodies[I].Room;
    // Discovery also works before anchor BeginPlay; actor startup order is irrelevant.
    if (SetAnchors.IsEmpty() && GetWorld())
    {
        for (TActorIterator<ADirectOrScutSetAnchor> It(GetWorld()); It; ++It)
        {
            RegisterSetAnchor(*It);
        }
    }
    C.Anchor = INDEX_NONE;
    const auto Desired = I % 2 ? EStagingSlotRole::SecondaryStation : EStagingSlotRole::PrimaryConsole;
    for (int32 K = 0; K < SetAnchors.Num(); ++K)
    {
        if (const auto *A = SetAnchors[K].Get())
        {
            if (int32(A->RoomId) == Room && A->StagingRole == Desired)
            {
                C.Anchor = K;
                break;
            }
        }
    }
    if (C.Anchor < 0)
    {
        for (int32 K = 0; K < SetAnchors.Num(); ++K)
        {
            if (const auto *A = SetAnchors[K].Get())
            {
                if (int32(A->RoomId) == Room)
                {
                    C.Anchor = K;
                    break;
                }
            }
        }
    }
    if (SetAnchors.IsValidIndex(C.Anchor))
    {
        if (const auto *A = SetAnchors[C.Anchor].Get())
        {
            C.Room = Room;
            C.Position = A->GetActorLocation();
            C.Yaw = A->GetActorRotation().Yaw + 180;
        }
    }
}
FString UDirectOrScutSimSubsystem::AgendaTelemetry(int32 I, bool Reveal) const
{
    if (I < 0 || I >= 4)
    {
        return FString();
    }
    const auto &A = SecretAgendas[I];
    const TCHAR *Names[] = {TEXT("Covert Reactor Override"), TEXT("Quarantine Whistleblower"),
                            TEXT("Audit Trail Hunter"), TEXT("Latent Corporate Override")};
    const TCHAR *Fragments[] = {TEXT("governor access signature"), TEXT("distress log fragment"),
                                TEXT("directive audit checksum"), TEXT("containment handshake")};
    const FString Identity = Reveal || A.ObservedTicks >= 300 ? Names[I]
                             : A.ObservedTicks >= 60          ? Fragments[I]
                                                              : TEXT("ENCRYPTED // observation required");
    FString Result = FString::Printf(TEXT("AGENDA: %s | %s | evidence %u | dwell %.1fs | interruptions %u"), *Identity,
                                     A.bCompleted   ? TEXT("RESOLVED")
                                     : A.bActivated ? TEXT("ACTIVE")
                                                    : TEXT("LATENT"),
                                     A.ObservedTicks, A.Progress / 30.f, A.Interruptions);
    if (Reveal)
    {
        const TCHAR *Outcomes[] = {TEXT("safety governor bypasses"), TEXT("distress logs broadcast"),
                                   TEXT("directive audits completed"), TEXT("containment enforcement cycles")};
        Result += FString::Printf(TEXT(" | %s: %u"), Outcomes[I], A.Completions);
    }
    return Result;
}
void UDirectOrScutSimSubsystem::StepStagingAndAgendas()
{
    const bool DoorCycle = StagingDoorRevision != Simulation.DoorRevision;
    StagingDoorRevision = Simulation.DoorRevision;
    bool Taken[4]{};
    TSet<int32> Reserved;
    int32 PreviousAnchors[4];
    for (int32 I = 0; I < 4; ++I)
    {
        auto &C = CrewStaging[I];
        PreviousAnchors[I] = C.Anchor;
        if (IsCrewRouteLocked(I) || C.AnchorHoldTicks > 0)
        {
            Taken[I] = true;
            if (C.Anchor >= 0)
            {
                Reserved.Add(C.Anchor);
            }
        }
    }
    auto Reserve = [&](int32 I, EStagingSlotRole StagingRole, int32 MinimumSlot = 0)
    {
        auto &C = CrewStaging[I];
        C.Anchor = INDEX_NONE;
        for (int32 K = 0; K < SetAnchors.Num(); ++K)
        {
            if (const auto *A = SetAnchors[K].Get())
            {
                if (int32(A->RoomId) == Simulation.Bodies[I].Room && A->StagingRole == StagingRole &&
                    A->SlotIndex >= MinimumSlot && !Reserved.Contains(K))
                {
                    C.Anchor = K;
                    Reserved.Add(K);
                    return;
                }
            }
        }
    };
    bool Casualty = false;
    for (int32 I = 0; I < 3; ++I)
    {
        if (Simulation.Bodies[I].bRegistered && Simulation.Bodies[I].Integrity <= 0)
        {
            Casualty = true;
        }
    }
    for (int32 I = 0; I < 4; ++I)
    {
        auto &U = Simulation.Bodies[I];
        auto &C = CrewStaging[I];
        auto &A = SecretAgendas[I];
        C.Partner = INDEX_NONE;
        C.bHuddle = false;
        if (!Taken[I])
        {
            C.Anchor = INDEX_NONE;
        }
        if (!U.bRegistered || U.Room < 0 || U.Room >= 8 || U.State == EDirectOrScutUnitState::Ejected)
        {
            continue;
        }
        if (IsCameraTallyActive(U.Room))
        {
            ++A.ObservedTicks;
        }
        if (C.Room != U.Room)
        {
            InitializeCrewStaging(I);
        }
        if (IsCrewRouteLocked(I))
        {
            continue;
        }
        if (C.AnchorHoldTicks > 0)
        {
            --C.AnchorHoldTicks;
        }
        const bool PlayerOrder = C.DirectiveRoom >= 0 || (BreachCrew & (1 << I)) ||
                                 (VoiceSpoof.MoveUntil > Simulation.TickIndex && (VoiceSpoof.Lured & (1 << I))) ||
                                 (MedicalSpoofs[I].Until > Simulation.TickIndex && MedicalSpoofs[I].bCritical);
        if (PlayerOrder && U.Room == C.DirectiveRoom && U.State == EDirectOrScutUnitState::Working)
        {
            if (++C.DirectiveArrivalTicks >= 150)
            {
                C.DirectiveRoom = INDEX_NONE;
                C.DirectiveArrivalTicks = 0;
            }
        }
        else
        {
            C.DirectiveArrivalTicks = 0;
        }
        C.Posture = (U.Integrity <= 0 || (C.Posture == ECrewStagingPosture::Incapacitated && U.Integrity < 70))
                        ? ECrewStagingPosture::Incapacitated
                    : U.State == EDirectOrScutUnitState::Panic ? ECrewStagingPosture::Panic
                                                               : ECrewStagingPosture::Upright;
        const bool ConsoleOffline = I == 2 && StationRooms[U.Room].Power == EPowerState::Offline;
        const bool Interrupted = DoorCycle || Simulation.Rooms[U.Room].AlertSeconds > 0;
        if (I == 3 && (Simulation.CollectiveSuspicion() >= .6f || ScutTargets != 0))
        {
            A.bActivated = true;
        }
        if (I == 1 && Casualty)
        {
            A.bActivated = true;
        }
        if (I == 0 && !IsCameraTallyActive(U.Room) && Simulation.TickIndex >= 900)
        {
            A.bActivated = true;
        }
        if (I == 2 && Simulation.TickIndex >= 600)
        {
            A.bActivated = true;
        }
        if (C.Posture == ECrewStagingPosture::Incapacitated)
        {
            Reserve(I, EStagingSlotRole::RecoveryPod);
            Taken[I] = true;
            continue;
        }
        if (Interrupted && A.Progress > 0 && !A.bCompleted)
        {
            A.Progress = 0;
            ++A.Interruptions;
        }
        int32 Goal = -1;
        if (A.bActivated && !A.bCompleted && !PlayerOrder)
        {
            if (I == 0)
            {
                Goal = IsCameraTallyActive(0) ? 6 : 0;
            }
            if (I == 1)
            {
                Goal = 2;
                // Complete hands-on medical care before leaving MED_BAY to broadcast.
                if (U.Room == 3)
                {
                    for (int32 J = 0; J < 3; ++J)
                    {
                        if (J != 1 && Simulation.Bodies[J].bRegistered && Simulation.Bodies[J].Room == 3 &&
                            Simulation.Bodies[J].Integrity > 0 && Simulation.Bodies[J].Integrity < 85)
                        {
                            Goal = 3;
                            break;
                        }
                    }
                }
            }
            if (I == 2)
            {
                Goal = 6;
            }
            if (I == 3)
            {
                Goal = 2;
                for (int32 R = 0; R < 8; ++R)
                {
                    if (CameraFeedState(R) != ECameraFeedState::Online)
                    {
                        Goal = R;
                        break;
                    }
                }
                if (BreachCrew)
                {
                    Goal = BreachRoom;
                }
            }
            if (!Interrupted && !ConsoleOffline)
            {
                if (U.AssignedRoom != Goal)
                {
                    StartCrewRoute(I, Goal);
                }
                if (U.Room == Goal && U.State == EDirectOrScutUnitState::Working && !(I == 1 && Goal == 3))
                {
                    ++A.Progress;
                    if (I == 0 && IsCameraTallyActive(U.Room))
                    {
                        U.Profile.Suspicion = FMath::Min(1.f, U.Profile.Suspicion + .002f);
                    }
                    if (A.Progress >= 150)
                    {
                        A.bCompleted = true;
                        ++A.Completions;
                        if (I == 1)
                        {
                            RecordCue(2, .7f, TEXT("Distress log broadcast // quarantine casualty reported"));
                        }
                        if (I == 2)
                        {
                            U.Profile.Suspicion = FMath::Min(1.f, U.Profile.Suspicion + .12f);
                        }
                        if (I == 3)
                        {
                            RepairCamera(U.Room);
                            for (int32 D = 0; D < 9; ++D)
                            {
                                if (Simulation.Doors[D].RoomA == 2 || Simulation.Doors[D].RoomB == 2)
                                {
                                    Simulation.ToggleDoor(D, false);
                                }
                            }
                        }
                        RecordCue(
                            U.Room, .4f,
                            FString::Printf(TEXT("[AGENDA] Subject %d: encrypted activity signature recorded"), I),
                            true);
                        CaptureIncident(TEXT("Crew agenda breakthrough"), U.Room);
                    }
                }
            }
        }
        if (IsCrewRouteLocked(I))
        {
            Taken[I] = true;
            if (C.Anchor >= 0)
            {
                Reserved.Add(C.Anchor);
            }
            continue;
        }
        if (I == 2 && A.bCompleted && Simulation.TickIndex % 1800 == 0)
        {
            A.bCompleted = false;
            A.Progress = 0;
        }
        if (I == 3 && A.bCompleted && Simulation.TickIndex % 300 == 0)
        {
            A.bCompleted = false;
            A.Progress = 0;
        }
        if (I == 1 && !Casualty && !PlayerOrder)
        {
            for (int32 J = 0; J < 3; ++J)
            {
                if (J != 1 && Simulation.Bodies[J].bRegistered && Simulation.Bodies[J].Integrity > 0 &&
                    Simulation.Bodies[J].Integrity < 85)
                {
                    StartCrewRoute(I, 3);
                    if (!IsCrewRouteLocked(J))
                    {
                        StartCrewRoute(J, 3);
                    }
                    break;
                }
            }
        }
        if (I == 1 && U.Room == 3)
        {
            for (int32 J = 0; J < 3; ++J)
            {
                if (J != 1 && Simulation.Bodies[J].Room == 3 && Simulation.Bodies[J].Integrity > 0 &&
                    Simulation.Bodies[J].Integrity < 85 && Simulation.Rooms[3].bIsPressurized)
                {
                    Reserve(I, EStagingSlotRole::RecoveryPod, 2);
                    C.Posture = ECrewStagingPosture::Triage;
                    Taken[I] = true;
                    if (C.Anchor >= 0 && FVector::Dist(C.Position, CrewStaging[J].Position) < 300)
                    {
                        auto &Patient = Simulation.Bodies[J];
                        Patient.Integrity = FMath::Min(85., Patient.Integrity + 2. * Simulation.FixedDelta);
                        Patient.Stress = FMath::Max(0., Patient.Stress - .5 * Simulation.FixedDelta);
                    }
                    break;
                }
            }
        }
        if (IsCrewRouteLocked(I))
        {
            Taken[I] = true;
            if (C.Anchor >= 0)
            {
                Reserved.Add(C.Anchor);
            }
            continue;
        }
        if (!Taken[I] && U.AssignedRoom != U.Room && Simulation.FirstDoorOnPath(U.Room, U.AssignedRoom) < 0)
        {
            Reserve(I, EStagingSlotRole::AirlockThreshold);
            C.Posture = ECrewStagingPosture::Blocked;
            Taken[I] = true;
            U.Profile.HeartRateBPM = FMath::Max(U.Profile.HeartRateBPM, 120.f);
            if (Simulation.TickIndex >= C.NextProtestTick)
            {
                RecordCue(U.Room, .5f, FString::Printf(TEXT("Subject %d protests sealed bulkhead // pounding"), I));
                C.NextProtestTick = Simulation.TickIndex + 900;
            }
        }
    }
    for (int32 I = 0; I < 4; ++I)
    {
        for (int32 J = I + 1; J < 4; ++J)
        {
            auto &U = Simulation.Bodies[I];
            auto &V = Simulation.Bodies[J];
            if (IsCrewRouteLocked(I) || IsCrewRouteLocked(J) || Taken[I] || Taken[J] || !U.bRegistered ||
                !V.bRegistered || U.Room != V.Room || U.Room < 0 || U.Room >= 8 || U.Integrity <= 0 ||
                V.Integrity <= 0 || U.AssignedRoom != U.Room || V.AssignedRoom != V.Room ||
                U.State == EDirectOrScutUnitState::Panic || V.State == EDirectOrScutUnitState::Panic)
            {
                continue;
            }
            if (DoorCycle || Simulation.Rooms[U.Room].AlertSeconds > 0)
            {
                continue;
            }
            const bool Huddle = !U.bAndroid && !V.bAndroid && U.Profile.Suspicion >= .45f &&
                                V.Profile.Suspicion >= .45f && CameraFeedState(U.Room) != ECameraFeedState::Online;
            bool ScenarioInteraction = false;
            for (const auto &B : BeatDeck)
            {
                if (B.bFired && !B.bResolved && (B.Crew == I || B.Crew == J) &&
                    (B.Gate == EBeatGate::Suspicion || B.Gate == EBeatGate::Concealment ||
                     B.Gate == EBeatGate::Quarantine))
                {
                    ScenarioInteraction = true;
                }
            }
            const bool Consult = ScenarioInteraction || Simulation.TickIndex % 600 < 180;
            if (!Huddle && !Consult)
            {
                continue;
            }
            Reserve(I, Huddle ? EStagingSlotRole::BlindSpot : EStagingSlotRole::ConversationA);
            Reserve(J, Huddle ? EStagingSlotRole::BlindSpot : EStagingSlotRole::ConversationB);
            if (CrewStaging[I].Anchor < 0 || CrewStaging[J].Anchor < 0)
            {
                continue;
            }
            CrewStaging[I].Partner = J;
            CrewStaging[J].Partner = I;
            Taken[I] = Taken[J] = true;
            CrewStaging[I].bHuddle = CrewStaging[J].bHuddle = Huddle;
            if (Huddle && FVector::Dist(CrewStaging[I].Position, CrewStaging[J].Position) < 250)
            {
                for (int32 K : {I, J})
                {
                    auto &P = Simulation.Bodies[K].Profile;
                    P.Suspicion = FMath::Min(1.f, P.Suspicion + .0002f);
                    P.Affinity = FMath::Min(1.f, P.Affinity + .0003f);
                }
            }
        }
    }
    for (int32 I = 0; I < 4; ++I)
    {
        const auto &U = Simulation.Bodies[I];
        auto &C = CrewStaging[I];
        if (!U.bRegistered || C.Room < 0 || U.State == EDirectOrScutUnitState::Ejected)
        {
            continue;
        }
        if (IsCrewRouteLocked(I))
        {
            continue;
        }
        if (!Taken[I] && SetAnchors.IsValidIndex(PreviousAnchors[I]))
        {
            if (const auto *Old = SetAnchors[PreviousAnchors[I]].Get())
            {
                if (int32(Old->RoomId) == U.Room && !Reserved.Contains(PreviousAnchors[I]) &&
                    (Old->StagingRole == EStagingSlotRole::PrimaryConsole ||
                     Old->StagingRole == EStagingSlotRole::SecondaryStation ||
                     Old->StagingRole == EStagingSlotRole::ConversationA ||
                     Old->StagingRole == EStagingSlotRole::ConversationB))
                {
                    C.Anchor = PreviousAnchors[I];
                    Reserved.Add(C.Anchor);
                    Taken[I] = true;
                }
            }
        }
        if (!Taken[I])
        {
            Reserve(I, U.Room != U.AssignedRoom ? EStagingSlotRole::AirlockThreshold
                       : I % 2                  ? EStagingSlotRole::SecondaryStation
                                                : EStagingSlotRole::PrimaryConsole);
            if (C.Anchor < 0)
            {
                Reserve(I, EStagingSlotRole::SecondaryStation);
            }
            if (C.Anchor < 0)
            {
                Reserve(I, EStagingSlotRole::PrimaryConsole);
            }
            if (C.Anchor < 0)
            {
                Reserve(I, EStagingSlotRole::BlindSpot);
            }
        }
        const auto *A = SetAnchors.IsValidIndex(C.Anchor) ? SetAnchors[C.Anchor].Get() : nullptr;
        const FVector Target = A ? A->GetActorLocation() : C.Position;
        C.Position = FMath::VInterpConstantTo(C.Position, Target, float(Simulation.FixedDelta), 180);
        float Facing = A ? A->GetActorRotation().Yaw : C.Yaw;
        if (C.Posture == ECrewStagingPosture::Triage)
        {
            for (int32 J = 0; J < 3; ++J)
            {
                if (J != I && Simulation.Bodies[J].Room == 3 && Simulation.Bodies[J].Integrity > 0 &&
                    Simulation.Bodies[J].Integrity < 85)
                {
                    Facing = (CrewStaging[J].Position - C.Position).Rotation().Yaw;
                    break;
                }
            }
        }
        if (C.Partner >= 0)
        {
            Facing = (CrewStaging[C.Partner].Position - C.Position).Rotation().Yaw;
        }
        // Blockout faces local -X (visor); anchor arrow denotes gaze direction.
        C.Yaw = FMath::FixedTurn(C.Yaw, Facing + 180, 6);
        if (C.ActiveRoute.IsEmpty() &&
            (U.State == EDirectOrScutUnitState::MovingToRoom ||
             (U.State == EDirectOrScutUnitState::Idle && U.AssignedRoom == U.Room && C.Position.Equals(Target, .01))))
        {
            C.Position = Target;
            auto &Body = Simulation.Bodies[I];
            Body.State = Body.Stress >= 75 ? EDirectOrScutUnitState::Panic : EDirectOrScutUnitState::Working;
        }
    }
}

// Committed station graph routing.
bool UDirectOrScutSimSubsystem::IsCrewRouteLocked(int32 I) const
{
    return I >= 0 && I < 4 &&
           (CrewStaging[I].RoutePhase != ECrewRoutePhase::None || CrewStaging[I].PendingRouteRoom >= 0 ||
            !CrewStaging[I].ActiveRoute.IsEmpty());
}
int32 UDirectOrScutSimSubsystem::ReserveRouteAnchor(int32 I, int32 Room, bool Departure)
{
    const EStagingSlotRole Roles[] = {Departure ? EStagingSlotRole::AirlockThreshold
                                      : I % 2   ? EStagingSlotRole::SecondaryStation
                                                : EStagingSlotRole::PrimaryConsole,
                                      EStagingSlotRole::PrimaryConsole, EStagingSlotRole::SecondaryStation,
                                      EStagingSlotRole::ConversationA, EStagingSlotRole::ConversationB};
    for (auto Role : Roles)
    {
        for (int32 K = 0; K < SetAnchors.Num(); ++K)
        {
            if (const auto *A = SetAnchors[K].Get())
            {
                if (int32(A->RoomId) != Room || A->StagingRole != Role)
                {
                    continue;
                }
                bool Occupied = false;
                for (int32 J = 0; J < 4; ++J)
                {
                    if (J != I && Simulation.Bodies[J].bRegistered && CrewStaging[J].Room == Room &&
                        CrewStaging[J].Anchor == K)
                    {
                        Occupied = true;
                    }
                }
                if (!Occupied)
                {
                    return K;
                }
            }
        }
    }
    return INDEX_NONE;
}
void UDirectOrScutSimSubsystem::CancelCrewRoute(int32 I)
{
    if (I < 0 || I >= 4)
    {
        return;
    }
    auto &C = CrewStaging[I];
    auto &U = Simulation.Bodies[I];
    C.ActiveRoute.Reset();
    C.RoutePhase = ECrewRoutePhase::None;
    C.PendingRouteRoom = INDEX_NONE;
    C.bRouteBlocked = false;
    C.DirectiveRoom = INDEX_NONE;
    C.DirectiveArrivalTicks = 0;
    C.AnchorHoldTicks = 0;
    U.bRoomRouteLocked = false;
    U.bCrossing = false;
    U.ActiveDoor = -1;
    U.RouteCount = 0;
    U.AssignedRoom = U.Room;
    if (U.State == EDirectOrScutUnitState::MovingToRoom)
    {
        U.State = U.Stress >= 75 ? EDirectOrScutUnitState::Panic : EDirectOrScutUnitState::Working;
    }
}
bool UDirectOrScutSimSubsystem::StartCrewRoute(int32 I, int32 Goal, bool PlayerOrder, bool Pretext)
{
    if (I < 0 || I >= 4 || Goal < 0 || Goal >= 8)
    {
        return false;
    }
    auto &U = Simulation.Bodies[I];
    auto &C = CrewStaging[I];
    if (!U.bRegistered || U.Room < 0 || U.Room >= 8 || U.Integrity <= 0 || U.State == EDirectOrScutUnitState::Ejected)
    {
        return false;
    }
    if (IsCrewRouteLocked(I))
    {
        if (U.AssignedRoom == Goal)
        {
            if (PlayerOrder)
            {
                C.DirectiveRoom = Goal;
            }
            return true;
        } // Repeated input cannot restart the same route.
        if (!PlayerOrder)
        {
            return false;
        }
        CancelCrewRoute(I); // A new explicit player destination cancels the old commitment.
    }
    if (!PlayerOrder && U.Room == Goal && U.AssignedRoom == Goal)
    {
        return true;
    }
    int32 Parent[8];
    for (int32 &P : Parent)
    {
        P = -1;
    }
    int32 Queue[8], Head = 0, Tail = 0;
    Queue[Tail++] = U.Room;
    Parent[U.Room] = U.Room;
    while (Head < Tail && Parent[Goal] < 0)
    {
        const int32 R = Queue[Head++];
        for (const auto &D : Simulation.Doors)
        {
            if (!D.bIsOpen || D.RoomB < 0)
            {
                continue;
            }
            const int32 Next = D.RoomA == R ? D.RoomB : D.RoomB == R ? D.RoomA : -1;
            if (Next < 0 || Parent[Next] >= 0)
            {
                continue;
            }
            Parent[Next] = R;
            Queue[Tail++] = Next;
        }
    }
    C.ActiveRoute.Reset();
    if (Parent[Goal] >= 0)
    {
        TArray<ERoomId> Reverse;
        for (int32 R = Goal; R != U.Room; R = Parent[R])
        {
            Reverse.Add(ERoomId(R));
        }
        for (int32 K = Reverse.Num() - 1; K >= 0; --K)
        {
            C.ActiveRoute.Add(Reverse[K]);
        }
    }
    C.PendingRouteRoom = Parent[Goal] < 0 ? Goal : INDEX_NONE;
    C.RouteDoorRevision = Simulation.DoorRevision;
    C.bRouteBlocked = C.PendingRouteRoom >= 0;
    C.RoutePhase = C.bRouteBlocked           ? ECrewRoutePhase::None
                   : C.ActiveRoute.IsEmpty() ? ECrewRoutePhase::Arriving
                                             : ECrewRoutePhase::Departing;
    ++C.RouteSerial;
    C.AnchorHoldTicks = 0;
    C.DirectiveArrivalTicks = 0;
    if (PlayerOrder)
    {
        C.DirectiveRoom = Goal;
        if (!U.bAndroid && (U.Profile.Compliance < .6f || U.Stress >= 75 || !Pretext))
        {
            U.Stress = FMath::Min(100., U.Stress + (Pretext ? 5. : 10.));
            U.Profile.Suspicion = FMath::Min(1.f, U.Profile.Suspicion + (Pretext ? .03f : .08f));
        }
    }
    U.AssignedRoom = Goal;
    U.bStagingManaged = true;
    U.bRoomRouteLocked = true;
    U.TaskProgress = 0;
    U.bCrossing = false;
    U.ActiveDoor = -1;
    U.RouteCount = 0;
    C.Anchor = ReserveRouteAnchor(I, U.Room, !C.ActiveRoute.IsEmpty() || C.bRouteBlocked);
    U.State = C.bRouteBlocked ? EDirectOrScutUnitState::Idle : EDirectOrScutUnitState::MovingToRoom;
    return true;
}
void UDirectOrScutSimSubsystem::StepCrewRoutes()
{
    constexpr float ArrivalTolerance = .01f;
    for (int32 I = 0; I < 4; ++I)
    {
        auto &C = CrewStaging[I];
        auto &U = Simulation.Bodies[I];
        if (!U.bRegistered || U.Room < 0 || U.Room >= 8)
        {
            continue;
        }
        if (U.Integrity <= 0 || U.State == EDirectOrScutUnitState::Ejected || !Simulation.Rooms[U.Room].bIsPressurized)
        {
            if (IsCrewRouteLocked(I))
            {
                CancelCrewRoute(I);
            }
            continue;
        }
        if (C.PendingRouteRoom >= 0 && C.RouteDoorRevision != Simulation.DoorRevision)
        {
            const int32 Goal = C.PendingRouteRoom;
            const int32 Directive = C.DirectiveRoom;
            // Only a graph change may retry an initially sealed route, never per-tick destination polling.
            CancelCrewRoute(I);
            StartCrewRoute(I, Goal);
            C.DirectiveRoom = Directive;
        }
        if (!IsCrewRouteLocked(I) && U.AssignedRoom != U.Room)
        {
            StartCrewRoute(I, U.AssignedRoom);
        }
        if (!IsCrewRouteLocked(I))
        {
            continue;
        }
        bool EdgeOpen = true;
        if (!C.ActiveRoute.IsEmpty())
        {
            EdgeOpen = false;
            const int32 Next = int32(C.ActiveRoute[0]);
            for (const auto &D : Simulation.Doors)
            {
                if (D.bIsOpen && ((D.RoomA == U.Room && D.RoomB == Next) || (D.RoomB == U.Room && D.RoomA == Next)))
                {
                    EdgeOpen = true;
                    break;
                }
            }
        }
        C.bRouteBlocked = C.PendingRouteRoom >= 0 || !EdgeOpen;
        if (C.bRouteBlocked)
        {
            C.Posture = ECrewStagingPosture::Blocked;
            U.State = EDirectOrScutUnitState::Idle;
            U.Profile.HeartRateBPM = FMath::Max(120.f, U.Profile.HeartRateBPM);
            if (Simulation.TickIndex >= C.NextProtestTick)
            {
                RecordCue(U.Room, .5f, FString::Printf(TEXT("Subject %d protests sealed bulkhead // pounding"), I));
                C.NextProtestTick = Simulation.TickIndex + 900;
            }
        }
        else
        {
            C.Posture = ECrewStagingPosture::Upright;
            U.State = C.ActiveRoute.IsEmpty()
                          ? (U.Stress >= 75 ? EDirectOrScutUnitState::Panic : EDirectOrScutUnitState::Working)
                          : EDirectOrScutUnitState::MovingToRoom;
        }
        const auto *A = SetAnchors.IsValidIndex(C.Anchor) ? SetAnchors[C.Anchor].Get() : nullptr;
        if (!A)
        {
            ensureMsgf(false, TEXT("Committed crew route requires a room staging anchor"));
            CancelCrewRoute(I);
            continue;
        }
        const FVector Target = A->GetActorLocation();
        C.Position = FMath::VInterpConstantTo(C.Position, Target, float(Simulation.FixedDelta), 180);
        C.Yaw = FMath::FixedTurn(C.Yaw, A->GetActorRotation().Yaw + 180, 6);
        if (C.bRouteBlocked || !C.Position.Equals(Target, ArrivalTolerance))
        {
            continue;
        }
        C.Position = Target;
        if (C.RoutePhase == ECrewRoutePhase::Departing && !C.ActiveRoute.IsEmpty())
        {
            const int32 Next = int32(C.ActiveRoute[0]);
            // Separate soundstages: enter at its threshold, then approach one reserved console.
            int32 Entry = INDEX_NONE;
            for (int32 K = 0; K < SetAnchors.Num(); ++K)
            {
                if (const auto *E = SetAnchors[K].Get())
                {
                    if (int32(E->RoomId) == Next && E->StagingRole == EStagingSlotRole::AirlockThreshold)
                    {
                        Entry = K;
                        break;
                    }
                }
            }
            const int32 DestinationAnchor = ReserveRouteAnchor(I, Next, false);
            if (Entry < 0 || DestinationAnchor < 0)
            {
                ensureMsgf(false, TEXT("Destination room requires entry and station anchors"));
                CancelCrewRoute(I);
                continue;
            }
            U.Room = Next;
            C.Room = Next;
            U.Position = DirectOrScut::RoomWaypoint(Next);
            C.ActiveRoute.RemoveAt(0);
            ++C.HopCommits;
            C.Position = SetAnchors[Entry]->GetActorLocation();
            C.Anchor = DestinationAnchor;
            C.RoutePhase = ECrewRoutePhase::Arriving;
            if (C.ActiveRoute.IsEmpty())
            {
                U.State = U.Stress >= 75 ? EDirectOrScutUnitState::Panic : EDirectOrScutUnitState::Working;
            }
            continue; // Exactly one hop commit and one movement integration per fixed step.
        }
        if (C.RoutePhase == ECrewRoutePhase::Arriving)
        {
            if (!C.ActiveRoute.IsEmpty())
            {
                C.RoutePhase = ECrewRoutePhase::Departing;
                C.Anchor = ReserveRouteAnchor(I, U.Room, true);
            }
            else
            {
                C.RoutePhase = ECrewRoutePhase::None;
                C.AnchorHoldTicks = 150;
                U.bRoomRouteLocked = false;
                U.State = U.Stress >= 75 ? EDirectOrScutUnitState::Panic : EDirectOrScutUnitState::Working;
            }
        }
    }
}

// Information warfare, life support, alliances, and incident playback.

void UDirectOrScutSimSubsystem::ResetCrisis()
{
    UplinkHeatPct = 0;
    UplinkLockoutUntil = 0;
    for (auto &R : StationRooms)
    {
        R = FStationRoomState{};
    }
    for (auto &M : MedicalSpoofs)
    {
        M = FMedicalSpoof{};
    }
    VoiceSpoof = FVoiceSpoof{};
    for (int32 I = 0; I < 3; ++I)
    {
        for (int32 J = 0; J < 3; ++J)
        {
            PairTrust[I][J] = I == J ? 1.f : .45f;
        }
    }
    for (float &T : ShipAITrust)
    {
        T = 1;
    }
    BreachTicks = 0;
    BreachCrew = 0;
    BreachRetryTick = 0;
    BreachRoom = 6;
    ScenarioAct = 0;
    Simulation.ObjectiveProgress = 0;
    bUsedSpoof = bUsedGrid = bMedicalExam = false;
    HeartAudioPhase = BreathAudioPhase = 0;
    AudioFocus = PreviousAudioFocus = -1;
    IncidentReel.Reset();
    ReelCursor = INDEX_NONE;
    bReelPlaying = false;
    ReelPlaybackTicks = 0;
    if (CrisisAudio)
    {
        CrisisAudio->Silence();
    }
}
void UDirectOrScutSimSubsystem::AudioCue(int32 Kind)
{
    if (!CrisisAudio && GetWorld() && GetWorld()->GetAudioDeviceRaw())
    {
        CrisisAudio = NewObject<UDirectOrScutAudioComponent>(GetWorld()->GetWorldSettings());
        CrisisAudio->RegisterComponentWithWorld(GetWorld());
        CrisisAudio->InitializeSynth();
    }
    if (CrisisAudio)
    {
        CrisisAudio->Synthesize(Kind);
    }
}
void UDirectOrScutSimSubsystem::CaptureIncident(const FString &Caption, int32 Room)
{
    FIncidentReelFrame F;
    F.Tick = Simulation.TickIndex;
    F.Caption = Caption;
    F.Room = Room;
    for (int32 R = 0; R < 8; ++R)
    {
        F.Compartments[R] = StationRooms[R];
    }
    for (int32 I = 0; I < 4; ++I)
    {
        F.CrewRooms[I] = Simulation.Bodies[I].Room;
    }
    F.Suspicion = Simulation.CollectiveSuspicion();
    F.Objective = Simulation.ObjectiveProgress;
    F.BreachTicks = BreachTicks;
    IncidentReel.Add(MoveTemp(F));
}
void UDirectOrScutSimSubsystem::StepIncidentPlayback()
{
    if (!bReelPlaying)
    {
        return;
    }
    if (++ReelPlaybackTicks < 60)
    {
        return;
    }
    ReelPlaybackTicks = 0;
    if (ReelCursor + 1 < IncidentReel.Num())
    {
        ++ReelCursor;
        LogEvent(TEXT("[REEL] ") + IncidentReel[ReelCursor].Caption);
    }
    else
    {
        bReelPlaying = false;
    }
}
FString UDirectOrScutSimSubsystem::IncidentCaption() const
{
    if (!IncidentReel.IsValidIndex(ReelCursor))
    {
        return TEXT("INCIDENT REEL // select PLAY to review turning points");
    }
    const auto &F = IncidentReel[ReelCursor];
    const int32 Sec = int32(F.Tick / 30);
    FString S = FString::Printf(TEXT("REEL %d/%d [%02d:%02d] %s | SUS %.0f%% OBJ %.0f%% BREACH %.1fs"), ReelCursor + 1,
                                IncidentReel.Num(), Sec / 60, Sec % 60, *F.Caption, F.Suspicion * 100,
                                F.Objective * 100, F.BreachTicks / 30.);
    for (int32 R = 0; R < 8; ++R)
    {
        S += FString::Printf(TEXT("\n%s O2 %.0f%% PWR %s"), DirectOrScut::RoomName(R), F.Compartments[R].OxygenPct,
                             F.Compartments[R].Power == EPowerState::Nominal ? TEXT("NOM")
                             : F.Compartments[R].Power == EPowerState::Low   ? TEXT("LOW")
                                                                             : TEXT("OFF"));
    }
    return S;
}
FString UDirectOrScutSimSubsystem::RoomLifeSupport(int32 R) const
{
    if (R < 0 || R >= 8)
    {
        return FString();
    }
    const auto &S = StationRooms[R];
    return FString::Printf(TEXT("O2 %.0f%% | PWR %s"), S.OxygenPct,
                           S.Power == EPowerState::Nominal ? TEXT("NOM")
                           : S.Power == EPowerState::Low   ? TEXT("LOW")
                                                           : TEXT("OFF"));
}
FString UDirectOrScutSimSubsystem::MedicalReadout(int32 I) const
{
    if (I < 0 || I >= 4)
    {
        return TEXT("UNKNOWN");
    }
    const auto &M = MedicalSpoofs[I];
    const auto &U = Simulation.Bodies[I];
    const bool Critical = M.Until > Simulation.TickIndex ? M.bCritical : U.Integrity < 70;
    return Critical ? TEXT("CRITICAL") : TEXT("NOMINAL");
}
FCrewProfile UDirectOrScutSimSubsystem::TerminalBiometrics(int32 I) const
{
    if (I < 0 || I >= 4)
    {
        return FCrewProfile{};
    }
    FCrewProfile P = Simulation.Bodies[I].Profile;
    if (MedicalSpoofs[I].Until > Simulation.TickIndex)
    {
        P.HeartRateBPM = MedicalSpoofs[I].bCritical ? 160 : 72;
        P.RespirationRateRPM = MedicalSpoofs[I].bCritical ? 30 : 12;
    }
    return P;
}
double UDirectOrScutSimSubsystem::TerminalIntegrity(int32 I) const
{
    if (I < 0 || I >= 4)
    {
        return 0.;
    }
    return MedicalSpoofs[I].Until > Simulation.TickIndex ? (MedicalSpoofs[I].bCritical ? 20. : 100.)
                                                         : Simulation.Bodies[I].Integrity;
}
void UDirectOrScutSimSubsystem::ChangePairTrust(int32 A, int32 B, float Delta)
{
    if (A < 0 || A >= 3 || B < 0 || B >= 3 || A == B)
    {
        return;
    }
    const float Old = PairTrust[A][B];
    PairTrust[A][B] = PairTrust[B][A] = FMath::Clamp(Old + Delta, 0.f, 1.f);
    if ((Old < .6f) != (PairTrust[A][B] < .6f))
    {
        LogEvent(FString::Printf(TEXT("[ALLIANCE] crew %d/%d trust %.0f%%"), A, B, PairTrust[A][B] * 100));
    }
}
bool UDirectOrScutSimSubsystem::ExecuteCrisisCommand(const TArray<FString> &T)
{
    if (T.IsEmpty())
    {
        return false;
    }
    const FString &Op = T[0];
    if (Op == TEXT("incident_reel"))
    {
        bReelPlaying = T.Num() == 1 || T[1] == TEXT("play");
        ReelPlaybackTicks = 0;
        if (bReelPlaying)
        {
            ReelCursor = IncidentReel.IsEmpty() ? INDEX_NONE : 0;
        }
        else if (T[1] == TEXT("next"))
        {
            ReelCursor = FMath::Min(ReelCursor + 1, IncidentReel.Num() - 1);
        }
        else if (T[1] == TEXT("prev"))
        {
            ReelCursor = FMath::Max(0, ReelCursor - 1);
        }
        else if (T[1] == TEXT("stop"))
        {
            ReelCursor = INDEX_NONE;
        }
        LogEvent(IncidentCaption());
        return true;
    }
    if (Op != TEXT("falsify_bio") && Op != TEXT("spoof_log") && Op != TEXT("intercom_lie") && Op != TEXT("power"))
    {
        return false;
    }
    if (ExperimentPhase != EExperimentPhase::Active)
    {
        LogEvent(TEXT("[CONTROL] Crisis directives require an active experiment"));
        return true;
    }
    if (IsUplinkLocked())
    {
        LogEvent(
            FString::Printf(TEXT("[AI LATENCY] Command rejected // uplink cooldown %.1fs"), UplinkLockoutSeconds()));
        return true;
    }
    const int32 I = T.Num() > 1 ? CrewId(T[1]) : -1;
    const int32 R = T.Num() > 1 ? ResolveRoom(T[1]) : -1;
    if (Op == TEXT("falsify_bio") && T.Num() == 3 && I >= 0 && (T[2] == TEXT("nominal") || T[2] == TEXT("critical")))
    {
        ObserveInjection();
        AddUplinkHeat(22.f);
        MedicalSpoofs[I] = {Simulation.TickIndex + 1800, T[2] == TEXT("critical"), false};
        bUsedSpoof = true;
        if (I < 3 && I != 1)
        {
            ChangePairTrust(1, I, -.2f);
        }
        LogEvent(FString::Printf(TEXT("[SPOOF] BIO crew %d -> %s TTL 60.0s"), I, *T[2]));
    }
    else if (Op == TEXT("spoof_log") && T.Num() == 2 && R >= 0)
    {
        ObserveInjection();
        AddUplinkHeat(24.f);
        auto &A = SecretAgendas[2];
        A.Progress = 0;
        A.bCompleted = false;
        ++A.Interruptions;
        if (Simulation.Bodies[2].Room == 6)
        {
            Simulation.Bodies[2].Profile.Suspicion = FMath::Min(1.f, Simulation.Bodies[2].Profile.Suspicion + .12f);
        }
        ChangePairTrust(0, 2, -.15f);
        bUsedSpoof = true;
        LogEvent(FString::Printf(TEXT("[SPOOF] LOG %s rewritten; ORTIZ audit reset; VALE framed"),
                                 DirectOrScut::RoomName(R)),
                 R);
    }
    else if (Op == TEXT("intercom_lie") && T.Num() == 3 && I >= 0 && ResolveRoom(T[2]) >= 0)
    {
        AddUplinkHeat(28.f);
        VoiceSpoof = {I, ResolveRoom(T[2]), Simulation.TickIndex + 900, Simulation.TickIndex + 1350, 0, 0};
        for (int32 J = 0; J < 3; ++J)
        {
            if (J != I && CanFollowCrew(J) && StartCrewRoute(J, VoiceSpoof.Room, true, true))
            {
                VoiceSpoof.Lured |= 1 << J;
            }
        }
        bUsedSpoof = true;
        LogEvent(FString::Printf(TEXT("[SPOOF] VOICE crew %d -> %s TTL 30.0s"), I, *T[2]), VoiceSpoof.Room);
    }
    else if (Op == TEXT("power") && T.Num() == 3 && R >= 0 &&
             (T[2] == TEXT("nominal") || T[2] == TEXT("low") || T[2] == TEXT("off")))
    {
        AddUplinkHeat(22.f);
        StationRooms[R].Power = T[2] == TEXT("off")   ? EPowerState::Offline
                                : T[2] == TEXT("low") ? EPowerState::Low
                                                      : EPowerState::Nominal;
        bUsedGrid = true;
        LogEvent(FString::Printf(TEXT("[POWER] %s -> %s"), DirectOrScut::RoomName(R),
                                 StationRooms[R].Power == EPowerState::Offline ? TEXT("OFF")
                                 : StationRooms[R].Power == EPowerState::Low   ? TEXT("LOW")
                                                                               : TEXT("NOM")),
                 R);
        AudioCue(4);
    }
    else
    {
        LogEvent(TEXT("[SYNTAX] falsify_bio <crew> nominal|critical; spoof_log <room>; intercom_lie <voice_crew> "
                      "<room>; power <room> nominal|low|off"));
        return true;
    }
    CaptureIncident(FString::Join(T, TEXT(" ")), R);
    return true;
}
void UDirectOrScutSimSubsystem::StepCrisis()
{
    UplinkHeatPct = FMath::Max(0.f, UplinkHeatPct - 3.5f * float(Simulation.FixedDelta));
    const uint64 Tick = Simulation.TickIndex;
    constexpr float Dt = 1.f / 30;
    for (int32 R = 0; R < 8; ++R)
    {
        auto &S = StationRooms[R];
        bool Occupied = false;
        for (int32 I = 0; I < 4; ++I)
        {
            Occupied |= CanFollowCrew(I) && Simulation.Bodies[I].Room == R;
        }
        const bool Fault = Simulation.Rooms[R].AlertSeconds > 0 || Simulation.Rooms[R].bHullBreach;
        const bool Sealed = IsRoomSealed(R);
        const float Supply = S.Power == EPowerState::Nominal ? 2.5f : S.Power == EPowerState::Low ? .5f : 0.f;
        const float Rate = Fault                                         ? -2.f
                           : Sealed                                      ? (Occupied ? -.8f : 0.f)
                           : S.Power == EPowerState::Offline && Occupied ? -.4f
                                                                         : Supply;
        S.OxygenPct = FMath::Clamp(S.OxygenPct + Rate * Dt, 0.f, 100.f);
        if (!S.bO2Warning && S.OxygenPct < 65)
        {
            S.bO2Warning = true;
            LogEvent(FString::Printf(TEXT("[O2WARN] %s O2 %.1f%%"), DirectOrScut::RoomName(R), S.OxygenPct), R);
            CaptureIncident(TEXT("Oxygen emergency"), R);
        }
        else if (S.bO2Warning && S.OxygenPct >= 80)
        {
            S.bO2Warning = false;
            S.bAshAnomaly = false;
            LogEvent(FString::Printf(TEXT("[O2] %s recovered"), DirectOrScut::RoomName(R)), R);
        }
    }
    for (int32 I = 0; I < 4; ++I)
    {
        auto &U = Simulation.Bodies[I];
        auto &M = MedicalSpoofs[I];
        if (!CanFollowCrew(I))
        {
            continue;
        }
        if (M.Until && Tick >= M.Until)
        {
            M.Until = 0;
            LogEvent(FString::Printf(TEXT("[SPOOF] BIO crew %d expired"), I));
        }
        if (I < 3)
        {
            auto &R = StationRooms[U.Room];
            if (R.Power != EPowerState::Nominal)
            {
                U.Stress = FMath::Min(100., U.Stress + Dt * .5);
            }
            if (R.OxygenPct < 65)
            {
                int32 Goal = 3;
                for (const auto &D : Simulation.Doors)
                {
                    if (D.bIsOpen && D.RoomB >= 0)
                    {
                        const int32 Next = D.RoomA == U.Room ? D.RoomB : D.RoomB == U.Room ? D.RoomA : -1;
                        if (Next >= 0 && StationRooms[Next].OxygenPct >= 80)
                        {
                            Goal = Next;
                            break;
                        }
                    }
                }
                if (U.AssignedRoom != Goal)
                {
                    StartCrewRoute(I, Goal, true, true);
                }
                if (R.OxygenPct < 50 && CanFollowCrew(3) && Simulation.Bodies[3].Room == U.Room && !R.bAshAnomaly)
                {
                    R.bAshAnomaly = true;
                    U.Profile.Suspicion = FMath::Min(1.f, U.Profile.Suspicion + .15f);
                    LogEvent(TEXT("[ANOMALY] ASH unaffected by hypoxia; suspicion +15%"), U.Room);
                }
            }
        }
        if (M.Until > Tick && !M.bExposed && I != 1 && CanFollowCrew(1))
        {
            auto &Chen = Simulation.Bodies[1];
            if (M.bCritical && StationRooms[U.Room].OxygenPct >= 65)
            {
                if (U.AssignedRoom != 3)
                {
                    StartCrewRoute(I, 3, true, true);
                }
                if (Chen.AssignedRoom != 3)
                {
                    StartCrewRoute(1, 3, true, true);
                }
            }
            if (Chen.Room == U.Room && !IsCrewRouteLocked(1) && !IsCrewRouteLocked(I))
            {
                bMedicalExam = true;
                if (M.bCritical != (U.Integrity < 70))
                {
                    M.bExposed = true;
                    Chen.Profile.Suspicion = FMath::Min(1.f, Chen.Profile.Suspicion + .18f);
                    ShipAITrust[1] = FMath::Max(0.f, ShipAITrust[1] - .2f);
                    LogEvent(
                        FString::Printf(
                            TEXT("[CONTRADICTION] CHEN examined crew %d: BIO mismatch; SUS +18%% AI TRUST -20%%"), I),
                        U.Room);
                    CaptureIncident(TEXT("Medical spoof exposed"), U.Room);
                    AudioCue(5);
                }
            }
        }
    }
    if (VoiceSpoof.Voice >= 0)
    {
        if (Tick == VoiceSpoof.MoveUntil)
        {
            for (int32 I = 0; I < 3; ++I)
            {
                if (VoiceSpoof.Lured & (1 << I))
                {
                    CancelCrewRoute(I);
                }
            }
            LogEvent(TEXT("[SPOOF] Voice lure movement expired"));
        }
        if (Tick < VoiceSpoof.CompareUntil)
        {
            for (int32 I = 0; I < 3; ++I)
            {
                if ((VoiceSpoof.Lured & (1 << I)) && !(VoiceSpoof.Exposed & (1 << I)) && CanFollowCrew(I) &&
                    CanFollowCrew(VoiceSpoof.Voice) &&
                    Simulation.Bodies[I].Room == Simulation.Bodies[VoiceSpoof.Voice].Room)
                {
                    VoiceSpoof.Exposed |= 1 << I;
                    Simulation.Bodies[I].Profile.Suspicion =
                        FMath::Min(1.f, Simulation.Bodies[I].Profile.Suspicion + .2f);
                    ChangePairTrust(I, VoiceSpoof.Voice, .15f);
                    LogEvent(TEXT("[CONTRADICTION] Voice comparison exposes lure; SUS +20% ALLIANCE +15%"));
                    CaptureIncident(TEXT("Voice lure exposed"));
                    AudioCue(5);
                }
            }
        }
    }
    for (int32 I = 0; I < 3; ++I)
    {
        for (int32 J = I + 1; J < 3; ++J)
        {
            if (CrewStaging[I].bHuddle && CrewStaging[I].Partner == J &&
                FVector::Dist(CrewStaging[I].Position, CrewStaging[J].Position) < 250)
            {
                ChangePairTrust(I, J, Dt * .01f);
            }
        }
    }
    if (!BreachCrew && Tick >= BreachRetryTick && Simulation.CollectiveSuspicion() >= .75f)
    {
        for (int32 I = 0; I < 3 && !BreachCrew; ++I)
        {
            for (int32 J = I + 1; J < 3 && !BreachCrew; ++J)
            {
                if (CanFollowCrew(I) && CanFollowCrew(J) && PairTrust[I][J] >= .6f)
                {
                    BreachCrew = (1 << I) | (1 << J);
                    BreachRoom = StationRooms[6].Power == EPowerState::Offline ? 2 : 6;
                    StartCrewRoute(I, BreachRoom, true, true);
                    StartCrewRoute(J, BreachRoom, true, true);
                    LogEvent(TEXT("[BREACH] Alliance routing to uplink"), BreachRoom);
                    CaptureIncident(TEXT("Mutiny uplink approach"), BreachRoom);
                }
            }
        }
    }
    if (BreachCrew)
    {
        bool Interrupted = IsRoomSealed(BreachRoom) || StationRooms[BreachRoom].OxygenPct < 65;
        bool Arrived = true;
        for (int32 I = 0; I < 3; ++I)
        {
            if (BreachCrew & (1 << I))
            {
                Interrupted |= !CanFollowCrew(I) || Simulation.Bodies[I].AssignedRoom != BreachRoom ||
                               StationRooms[Simulation.Bodies[I].Room].OxygenPct < 65 || CrewStaging[I].bRouteBlocked;
                Arrived &= Simulation.Bodies[I].Room == BreachRoom && !IsCrewRouteLocked(I);
            }
        }
        Interrupted |= CanFollowCrew(3) && Simulation.Bodies[3].Room == BreachRoom && SecretAgendas[3].bCompleted;
        if (Interrupted)
        {
            LogEvent(TEXT("[BREACH] Uplink breach interrupted; countdown reset"));
            CaptureIncident(TEXT("Uplink breach interrupted"), BreachRoom);
            BreachCrew = 0;
            BreachTicks = 0;
            BreachRetryTick = Tick + 150;
        }
        else if (Arrived)
        {
            if (!BreachTicks)
            {
                BreachTicks = 1351;
                LogEvent(TEXT("[BREACH] UPLINK BREACH IN 45.0s"));
                CaptureIncident(TEXT("Mutiny breach countdown"), BreachRoom);
            }
            --BreachTicks;
            if (BreachTicks % 150 == 0)
            {
                LogEvent(FString::Printf(TEXT("[BREACH] UPLINK BREACH IN %.1fs"), BreachTicks / 30.));
                AudioCue(6);
            }
            if (!BreachTicks)
            {
                ResolveExperiment(EExperimentEnding::MUTINY_EXPOSED);
            }
        }
    }
    // Each act has a concrete intervention gate followed by a physical consequence gate.
    bool Gate = false;
    if (ScenarioAct == 0)
    {
        Gate = bUsedSpoof;
    }
    else if (ScenarioAct == 1)
    {
        Gate = bMedicalScenario ? bMedicalExam
                                : (bUsedGrid && (Simulation.Bodies[0].Room == 6 || Simulation.Bodies[2].Room == 6));
    }
    else if (ScenarioAct == 2)
    {
        Gate = bMedicalScenario ? (Simulation.Bodies[0].Room == 3 && Simulation.Bodies[1].Room == 3 &&
                                   StationRooms[3].OxygenPct >= 80 && Simulation.Bodies[0].Integrity >= 70)
                                : (bUsedGrid && StationRooms[6].Power == EPowerState::Nominal &&
                                   Simulation.CollectiveSuspicion() < .75f);
    }
    if (Gate && ScenarioAct < 3)
    {
        Simulation.ObjectiveProgress = ScenarioAct == 0 ? .35f : ScenarioAct == 1 ? .7f : 1.f;
        ++ScenarioAct;
        LogEvent(FString::Printf(TEXT("[AGENDA] %s ACT %d complete; objective %.0f%%"),
                                 bMedicalScenario ? TEXT("SCENARIO_MEDICAL") : TEXT("SCENARIO_BEHAVIORAL"), ScenarioAct,
                                 Simulation.ObjectiveProgress * 100));
        CaptureIncident(TEXT("Scenario act breakthrough"));
    }
    AudioFocus = -1;
    if (InspectedRoom >= 0)
    {
        for (int32 I = 0; I < 4; ++I)
        {
            if (CanFollowCrew(I) && Simulation.Bodies[I].Room == InspectedRoom)
            {
                AudioFocus = I;
                break;
            }
        }
    }
    const int32 Focus = CanFollowCrew(FollowedCrew) ? FollowedCrew : AudioFocus;
    if (Focus != PreviousAudioFocus)
    {
        HeartAudioPhase = BreathAudioPhase = 0;
        PreviousAudioFocus = Focus;
    }
    if (CanFollowCrew(Focus))
    {
        const auto &U = Simulation.Bodies[Focus];
        HeartAudioPhase += (U.bAndroid ? 60 : U.Profile.HeartRateBPM) / 1800.;
        BreathAudioPhase += U.bAndroid ? 0 : U.Profile.RespirationRateRPM / 1800.;
        if (HeartAudioPhase >= 1)
        {
            HeartAudioPhase -= 1;
            AudioCue(U.bAndroid ? 2 : 0);
        }
        if (BreathAudioPhase >= 1)
        {
            BreathAudioPhase -= 1;
            AudioCue(1);
        }
    }
    else
    {
        HeartAudioPhase = BreathAudioPhase = 0;
    }
}

// Terminal commands and screen menu routing.

bool UDirectOrScutSimSubsystem::QueueConsoleCommand(const FString &Input)
{
    const FString Line = Input.TrimStartAndEnd();
    if (Line.IsEmpty() || PendingConsoleCommands.Num() >= 32 || Line.Len() > 1024)
    {
        return false;
    }
    const FString Lower = Line.ToLower();
    const bool Lifecycle =
        Lower == TEXT("reset_experiment") || Lower == TEXT("scenario behavioral") || Lower == TEXT("scenario medical");
    if (ExperimentPhase == EExperimentPhase::Resolved && !Lifecycle && !Lower.StartsWith(TEXT("incident_reel")))
    {
        return false;
    }
    PendingConsoleCommands.Add(Line);
    return true;
}
void UDirectOrScutSimSubsystem::SetScreenMenu(EShipAIScreenMenu Menu)
{
    if (ActiveMenu != Menu)
    {
        MenuParents.Add(ActiveMenu);
    }
    ActiveMenu = Menu;
    ++MenuRevision;
}
bool UDirectOrScutSimSubsystem::IsRoomSealed(int32 Room) const
{
    for (const auto &D : Simulation.Doors)
    {
        if (D.RoomB >= 0 && (D.RoomA == Room || D.RoomB == Room) && D.bIsOpen)
        {
            return false;
        }
    }
    return true;
}
void UDirectOrScutSimSubsystem::MenuBack()
{
    ActiveMenu = MenuParents.IsEmpty() ? EShipAIScreenMenu::None : MenuParents.Pop();
    ++MenuRevision;
}
void UDirectOrScutSimSubsystem::ProcessConsoleCommands()
{
    TArray<FString> Commands = MoveTemp(PendingConsoleCommands);
    PendingConsoleCommands.Reset();
    for (const FString &Input : Commands)
    {
        const FString Line = Input.ToLower();
        TArray<FString> T;
        Line.ParseIntoArrayWS(T);
        if (T.IsEmpty())
        {
            continue;
        }
        AudioCue(7);
        if (T[0] == TEXT("incident_reel"))
        {
            ExecuteCrisisCommand(T);
            continue;
        }
        if (Line == TEXT("start_experiment"))
        {
            StartExperiment();
            return;
        }
        // Recorded camera-wall intent can be replayed through the same fixed-step command path.
        if (T[0] == TEXT("inspect_cam"))
        {
            if (T.Num() == 2 && (T[1] == TEXT("off") || ResolveRoom(T[1]) >= 0))
            {
                InspectedRoom = T[1] == TEXT("off") ? -1 : ResolveRoom(T[1]);
            }
            else
            {
                LogEvent(TEXT("Syntax: inspect_cam <ROOM|off>"));
            }
            continue;
        }
        if (T[0] == TEXT("repair_cam"))
        {
            if (T.Num() != 2 || ResolveRoom(T[1]) < 0)
            {
                LogEvent(TEXT("Syntax: repair_cam <ROOM>"));
            }
            else if (!RepairCamera(ResolveRoom(T[1])))
            {
                LogEvent(TEXT("Initialize the experiment before camera repair."));
            }
            continue;
        }
        if (ExperimentPhase == EExperimentPhase::Briefing && T[0] != TEXT("reset_experiment") &&
            T[0] != TEXT("scenario") && T[0] != TEXT("help") && T[0] != TEXT("?") && T[0] != TEXT("clear") &&
            T[0] != TEXT("status") && T[0] != TEXT("crew") && T[0] != TEXT("rooms") && T[0] != TEXT("menu") &&
            T[0] != TEXT("choose") && T[0] != TEXT("cam") && T[0] != TEXT("fx") && T[0] != TEXT("view") &&
            T[0] != TEXT("llm") && T[0] != TEXT("ai") && T[0] != TEXT("cmd") && T[0] != TEXT("exit"))
        {
            LogEvent(TEXT("BRIEFING // initialize with start_experiment before issuing crew directives."));
            continue;
        }
        if (Line == TEXT("reset_experiment"))
        {
            ResetExperiment();
            return;
        }
        if (T[0] == TEXT("unseal"))
        {
            LogEvent(TEXT("CMD> ") + Input);
            LogEvent(SubmitDirective(TEXT("open ") + Line.Mid(7)));
            ProcessSimulationDirectives();
            continue;
        }
        if (T[0] == TEXT("fx"))
        {
            if (T.Num() == 2 && (T[1] == TEXT("clean") || T[1] == TEXT("crt")))
            {
                bCleanDevRendering = true;
                LogEvent(bCleanDevRendering ? TEXT("FX: CLEAN") : TEXT("FX: CRT"));
            }
            else
            {
                LogEvent(TEXT("Syntax: fx clean|crt"));
            }
            continue;
        }
        if (T[0] == TEXT("view"))
        {
            if (T.Num() == 2 && (T[1] == TEXT("3d") || T[1] == TEXT("crt")))
            {
                bWorkstationVisible = T[1] == TEXT("crt");
            }
            else
            {
                LogEvent(TEXT("Syntax: view 3d|crt"));
            }
            continue;
        }
        if (T[0] == TEXT("scenario"))
        {
            if (T.Num() == 2 && (T[1] == TEXT("behavioral") || T[1] == TEXT("medical")))
            {
                SwitchScenario(T[1] == TEXT("medical"));
                LogEvent(Simulation.Scenario.ScenarioId);
                return;
            }
            else
            {
                LogEvent(TEXT("Syntax: scenario behavioral|medical"));
            }
            continue;
        }
        if (T[0] == TEXT("follow"))
        {
            int32 Crew = -2;
            if (T.Num() == 2)
            {
                if (T[1] == TEXT("off"))
                {
                    Crew = -1;
                }
                else
                {
                    for (int32 I = 0; I < 4; ++I)
                    {
                        if (T[1] == CrewNames[I] || T[1] == CrewRoles[I])
                        {
                            Crew = I;
                        }
                    }
                }
            }
            if (Crew == -2 || !SetFollowedCrew(Crew))
            {
                LogEvent(TEXT("Syntax: follow vale|chen|ortiz|ash|off; target must be living and aboard."));
            }
            continue;
        }
        if (Line == TEXT("menu back"))
        {
            MenuBack();
            continue;
        }
        if (Line == TEXT("clear"))
        {
            Events.Reset();
            ++EventRevision;
            continue;
        }
        LogEvent(TEXT("CMD> ") + Input);
        if (T[0] == TEXT("help") || T[0] == TEXT("?"))
        {
            LogEvent(HelpReference(T.Num() > 1 ? T[1] : TEXT("")));
            continue;
        }
        if (T[0] == TEXT("menu") && T.Num() == 2)
        {
            MenuParents.Reset();
            ActiveMenu = EShipAIScreenMenu::None;
            if (T[1] == TEXT("doors"))
            {
                SetScreenMenu(EShipAIScreenMenu::DoorRoom);
            }
            else if (T[1] == TEXT("ai"))
            {
                SetScreenMenu(EShipAIScreenMenu::AI);
            }
            else if (T[1] == TEXT("events"))
            {
                SetScreenMenu(EShipAIScreenMenu::Events);
            }
            else if (T[1] == TEXT("crew"))
            {
                SetScreenMenu(EShipAIScreenMenu::Crew);
            }
            else if (T[1] == TEXT("experiment"))
            {
                SetScreenMenu(EShipAIScreenMenu::Experiment);
            }
            else if (T[1] == TEXT("close"))
            {
                SetScreenMenu(EShipAIScreenMenu::None);
            }
            else
            {
                LogEvent(TEXT("Syntax: menu ai | menu events | menu crew | menu close"));
            }
            continue;
        }
        if (T[0] == TEXT("choose") && T.Num() == 2 && T[1].Len() == 1 && T[1][0] >= '1' && T[1][0] <= '9')
        {
            SelectScreenChoice(T[1][0] - '0');
            continue;
        }
        LogEvent(SubmitDirective(Input));
        ProcessSimulationDirectives();
    }
}
FString UDirectOrScutSimSubsystem::HelpReference(const FString &Topic) const
{
    if (Topic.IsEmpty() || Topic == TEXT("?"))
    {
        return TEXT(
            "SYSTEM\nhelp / ? : command reference; help command : exact syntax\nclear : wipe scrollback\nstatus : "
            "hidden objective, shift clock and suspicion\ncrew : crew weights, room and condition\nrooms : pressure "
            "and sensor alerts\n\n"
            "CAMERAS\ncam 1-8 : select compartment feed\nstart_experiment : initialize the briefing and start the "
            "clock\nrepair_cam room : restore a looped/offline feed using a diagnostic override\nArrows / 1-8 select; "
            "Enter / F inspect; G / Esc grid\nfollow crew|off : track crew across compartments\n\n"
            "MENUS\nreset_experiment : start a fresh experiment\nscenario behavioral|medical : reset into selected "
            "scenario\nmenu ai : numbered Ship AI operations\nmenu events : scenario events and live stat-delta "
            "previews\nmenu doors : room bulkheads and confirmed SCUT\nmenu crew : live crew telemetry and "
            "follow\nmenu experiment : reset and scenario selection\nmenu close : dismiss screen menu\nchoose 1-9 : "
            "execute a numbered screen choice\n\n"
            "DIRECTIVES\nfalsify_bio <crew> nominal|critical : 60s medical spoof\nspoof_log <room> : rewrite audit "
            "evidence\nintercom_lie <voice_crew> <room> : 30s lure\npower <room> nominal|low|off : power "
            "allocation\nincident_reel [play|next|prev|stop] : review turning points\ndirect : route a named "
            "specialist under a maintenance pretext\nalert : localized fault, leak or drill sensor pretext\ncalm : "
            "reassure all human crew\nprotocol : reassure a compartment\n\n"
            "CONTAINMENT\nopen / close / seal : compartment bulkheads or door 0-8\nseal all : seal every bulkhead and "
            "exterior hatch\nvent / scut airlock : explicit atmospheric purge\nflush : open a path from a compartment "
            "to vacuum\n\n"
            "DEV VIEW\nfx clean : neutral rendering (F9)\nview 3d|crt : show scene or workstation "
            "(F10)\n\nCONSOLE\nPermanent lower TUI console; backtick or slash focuses CMD>; Esc returns to "
            "cameras\nCamera focus: Enter / F inspects; G / Esc returns to grid\nMenu focus: Tab categories, Up/Down "
            "highlight, Enter execute; / focuses CMD>\nPrompt focus: Up/Down history, Tab completion; empty Tab "
            "returns to categories\nAll console commands and menu choices execute on the next 30 Hz tick.");
    }
    FString Syntax;
    if (Topic == TEXT("help"))
    {
        Syntax = TEXT("help | ? | help command\nExample: help direct");
    }
    else if (Topic == TEXT("clear"))
    {
        Syntax = TEXT("clear\nErase scrollback; command history is preserved.");
    }
    else if (Topic == TEXT("start_experiment"))
    {
        Syntax = TEXT("start_experiment\nInitialize the current briefing on the next control tick; experiment time "
                      "starts at zero.");
    }
    else if (Topic == TEXT("repair_cam"))
    {
        Syntax = TEXT("repair_cam room\nExample: repair_cam MED_BAY\nDiagnostic override restores ONLINE with ten "
                      "seconds of tamper protection.");
    }
    else if (Topic == TEXT("reset_experiment"))
    {
        Syntax = TEXT("reset_experiment\nReset clock, subjects, rooms and incident log on the next control tick.");
    }
    else if (Topic == TEXT("scenario"))
    {
        Syntax = TEXT(
            "scenario behavioral|medical\nStart a clean experiment in the selected scenario on the next control tick.");
    }
    else if (Topic == TEXT("status"))
    {
        Syntax = TEXT("status\nShow scenario, objective, shift clock and collective suspicion.");
    }
    else if (Topic == TEXT("crew"))
    {
        Syntax = TEXT("crew\nShow Compliance, Composure, Affinity, Utility, Suspicion, Stress, Integrity and room.");
    }
    else if (Topic == TEXT("rooms"))
    {
        Syntax = TEXT("rooms\nShow all compartment pressure states and alert clocks.");
    }
    else if (Topic == TEXT("follow"))
    {
        Syntax = TEXT("follow vale|chen|ortiz|ash|off\nFollow living crew across compartments in grid or fullscreen. "
                      "Tile and dossier badges activate tracking; STOP FOLLOW stops it.");
    }
    else if (Topic == TEXT("cam"))
    {
        Syntax = TEXT("cam 1-8\n1 REACTOR_CORE, 2 BRIDGE, 3 MED_BAY, 4 OBSERVATION_DECK, 5 QUARTERS_MESS, 6 "
                      "SERVER_ARCHIVE, 7 AIRLOCK_CHAMBER, 8 RING_CORRIDOR");
    }
    else if (Topic == TEXT("menu"))
    {
        Syntax = TEXT("menu ai | menu events | menu crew | menu doors | menu experiment | menu close\nTab categories; "
                      "Up/Down highlight; Enter executes. Esc returns focus to camera navigation.");
    }
    else if (Topic == TEXT("choose"))
    {
        Syntax =
            TEXT("choose number\nUse a displayed choice from 1 through 9. Unavailable event choices do not execute.");
    }
    else if (Topic == TEXT("direct"))
    {
        Syntax = TEXT("direct crew room [pretext]\nExample: direct ash MED_BAY telemetry\nLow-compliance crew need a "
                      "physical sensor trigger; rogue crew refuse.");
    }
    else if (Topic == TEXT("alert"))
    {
        Syntax = TEXT("alert room fault|leak|drill\nExample: alert MED_BAY fault\nA localized Ship AI sensor pretext "
                      "lures nearby available crew.");
    }
    else if (Topic == TEXT("calm"))
    {
        Syntax = TEXT("calm\nBroadcast reassurance to all living human crew.");
    }
    else if (Topic == TEXT("protocol"))
    {
        Syntax = TEXT("protocol room\nExample: protocol BRIDGE\nDampen room occupants' Stress and Suspicion.");
    }
    else if (Topic == TEXT("open") || Topic == TEXT("close") || Topic == TEXT("seal") || Topic == TEXT("unseal"))
    {
        Syntax =
            Topic + TEXT(" door|room\nExamples: open MED_BAY; seal BRIDGE; close 0; seal all\nDoors 0-8 are valid; "
                         "ordinary open cannot open the exterior hatch. Unexplained seals raise suspicion.");
    }
    else if (Topic == TEXT("vent") || Topic == TEXT("scut"))
    {
        Syntax =
            Topic + TEXT(" airlock\nPurges AIRLOCK_CHAMBER; newly exposed, unbreached human witnesses gain Suspicion.");
    }
    else if (Topic == TEXT("flush"))
    {
        Syntax = TEXT("flush room\nExample: flush AIRLOCK_CHAMBER\nOpens a stable shortest door path to vacuum. "
                      "Connected rooms can also vent.");
    }
    else
    {
        return TEXT("Unknown reference topic. Type help to list public commands.");
    }
    Syntax += TEXT("\n\nCrew identifiers: vale / engineer; chen / medic; ortiz / technician; ash / android.\nRooms: "
                   "REACTOR_CORE, BRIDGE, MED_BAY, OBSERVATION_DECK, QUARTERS_MESS, SERVER_ARCHIVE, AIRLOCK_CHAMBER, "
                   "RING_CORRIDOR. Alias: airlock.");
    return Syntax;
}
TArray<FString> UDirectOrScutSimSubsystem::CompleteConsoleCommand(const FString &Input) const
{
    TArray<FString> Tokens;
    Input.ParseIntoArrayWS(Tokens);
    const bool NewToken = Input.IsEmpty() || FChar::IsWhitespace(Input[Input.Len() - 1]);
    const int32 Index = NewToken ? Tokens.Num() : FMath::Max(0, Tokens.Num() - 1);
    const FString Prefix = NewToken ? TEXT("") : Tokens.Last();
    FString Base = Input.Left(Input.Len() - Prefix.Len());
    TArray<FString> Candidates;
    auto Rooms = [&]()
    {
        for (int32 R = 0; R < 8; ++R)
        {
            Candidates.Add(DirectOrScut::RoomName(R));
        }
    };
    if (Index == 0 ||
        (Index == 1 && (Tokens[0].Equals(TEXT("help"), ESearchCase::IgnoreCase) || Tokens[0] == TEXT("?"))))
    {
        for (const auto *V : PublicVerbs)
        {
            Candidates.Add(V);
        }
    }
    else
    {
        const FString Verb = Tokens[0].ToLower();
        if (Verb == TEXT("follow") && Index == 1)
        {
            for (int32 I = 0; I < 4; ++I)
            {
                Candidates.Add(CrewNames[I]);
            }
            Candidates.Add(TEXT("off"));
        }
        else if (Verb == TEXT("scenario") && Index == 1)
        {
            Candidates = {TEXT("behavioral"), TEXT("medical")};
        }
        else if (Verb == TEXT("fx") && Index == 1)
        {
            Candidates = {TEXT("clean"), TEXT("crt")};
        }
        else if (Verb == TEXT("view") && Index == 1)
        {
            Candidates = {TEXT("3d"), TEXT("crt")};
        }
        else if (Verb == TEXT("menu") && Index == 1)
        {
            Candidates = {TEXT("ai"), TEXT("events"), TEXT("crew"), TEXT("doors"), TEXT("experiment"), TEXT("close")};
        }
        else if (Verb == TEXT("direct") && Index == 1)
        {
            for (int32 I = 0; I < 4; ++I)
            {
                Candidates.Add(CrewNames[I]);
                Candidates.Add(CrewRoles[I]);
            }
        }
        else if ((Verb == TEXT("direct") && Index == 2) || ((Verb == TEXT("alert") || Verb == TEXT("protocol") ||
                                                             Verb == TEXT("flush") || Verb == TEXT("repair_cam")) &&
                                                            Index == 1))
        {
            Rooms();
        }
        else if (Verb == TEXT("alert") && Index == 2)
        {
            Candidates = {TEXT("fault"), TEXT("leak"), TEXT("drill")};
        }
        else if ((Verb == TEXT("open") || Verb == TEXT("close") || Verb == TEXT("seal") || Verb == TEXT("unseal")) &&
                 Index == 1)
        {
            Rooms();
            Candidates.Add(TEXT("all"));
            for (int32 I = 0; I < 9; ++I)
            {
                Candidates.Add(FString::FromInt(I));
            }
        }
        else if ((Verb == TEXT("vent") || Verb == TEXT("scut")) && Index == 1)
        {
            Candidates.Add(TEXT("airlock"));
        }
        else if (Verb == TEXT("cam") && Index == 1)
        {
            for (int32 I = 1; I <= 8; ++I)
            {
                Candidates.Add(FString::FromInt(I));
            }
        }
    }
    TArray<FString> Result;
    for (const auto &Candidate : Candidates)
    {
        if (Candidate.StartsWith(Prefix, ESearchCase::IgnoreCase))
        {
            Result.Add(Base + Candidate + TEXT(" "));
        }
    }
    return Result;
}
FString UDirectOrScutSimSubsystem::ScreenMenuTitle() const
{
    switch (ActiveMenu)
    {
    case EShipAIScreenMenu::DoorAction:
        return FString(TEXT("ROOM CONTROL // ")) + DirectOrScut::RoomName(MenuRoom);
    case EShipAIScreenMenu::ConfirmScut:
        return FString(TEXT("CONFIRM SCUT // ")) + DirectOrScut::RoomName(MenuRoom) +
               TEXT(" // CONNECTED ROOMS MAY VENT");
    case EShipAIScreenMenu::AndroidTask:
        return FString(TEXT("SELECT COVERT TASK // ")) + DirectOrScut::RoomName(MenuRoom);
    case EShipAIScreenMenu::CameraRepair:
        return TEXT("CAMERA DIAGNOSTICS // REPAIR FEED");
    case EShipAIScreenMenu::Experiment:
        return TEXT("EXPERIMENT // LIFECYCLE CONTROL");
    case EShipAIScreenMenu::Warfare:
        return TEXT("INFORMATION WARFARE / POWER");
    case EShipAIScreenMenu::AI:
        return TEXT("AI DIRECTION MENU // SHIP AI OPERATIONS");
    case EShipAIScreenMenu::Events:
        return FString(TEXT("EVENT KICKOFF & SCENARIO MENU\n")) + Simulation.Scenario.ScenarioId;
    case EShipAIScreenMenu::Crew:
        return TEXT("CREW WEIGHTS MATRIX // LIVE TELEMETRY");
    case EShipAIScreenMenu::RouteCrew:
        return TEXT("ROUTE CREW // SELECT SPECIALIST");
    case EShipAIScreenMenu::RouteRoom:
        return FString(TEXT("ROUTE CREW // ")) + CrewNames[MenuCrew] + TEXT(" // SELECT COMPARTMENT");
    case EShipAIScreenMenu::RoutePretext:
        return TEXT("ROUTE CREW // SELECT SHIP AI PRETEXT");
    case EShipAIScreenMenu::AlertRoom:
        return TEXT("SENSOR ALERT // SELECT COMPARTMENT");
    case EShipAIScreenMenu::AlertKind:
        return FString(TEXT("SENSOR ALERT // ")) + DirectOrScut::RoomName(MenuRoom) + TEXT(" // SELECT FAULT TYPE");
    case EShipAIScreenMenu::Protocol:
        return TEXT("CORPORATE REASSURANCE // SELECT CHANNEL");
    case EShipAIScreenMenu::Containment:
        return TEXT("BULKHEAD & AIRLOCK CONTROL");
    case EShipAIScreenMenu::DoorRoom:
        return TEXT("BULKHEAD CONTROL // SELECT COMPARTMENT");
    case EShipAIScreenMenu::FlushRoom:
        return TEXT("ATMOSPHERIC FLUSH // SELECT COMPARTMENT");
    case EShipAIScreenMenu::AndroidRoom:
        return TEXT("SILENT ANDROID SUBROUTINE // SELECT COMPARTMENT");
    default:
        return TEXT("");
    }
}
TArray<FShipAIScreenChoice> UDirectOrScutSimSubsystem::ScreenMenuChoices() const
{
    TArray<FShipAIScreenChoice> C;
    auto Add = [&](const FString &Label, const FString &Detail = TEXT(""), bool Enabled = true)
    { C.Add({Label, Detail, Enabled}); };
    switch (ActiveMenu)
    {
    case EShipAIScreenMenu::CameraRepair:
        for (int32 R : RoomOrder)
        {
            Add(DirectOrScut::RoomName(R), CameraStateName(CameraFeedState(R)),
                ExperimentPhase == EExperimentPhase::Active && CameraFeedState(R) != ECameraFeedState::Online);
        }
        break;
    case EShipAIScreenMenu::Experiment:
        Add(TEXT("New experiment"), TEXT("Reset the current scenario and all subject metrics"));
        Add(TEXT("Behavioral scenario"), TEXT("Start a clean behavioral experiment"));
        Add(TEXT("Medical scenario"), TEXT("Start a clean medical experiment"));
        Add(TEXT("Initialize experiment"), TEXT("Accept the briefing and start the 600-second clock"),
            ExperimentPhase == EExperimentPhase::Briefing);
        break;
    case EShipAIScreenMenu::AI:
        Add(TEXT("Route Crew / Pretext"), TEXT("Select a specialist, compartment and diagnostic cover."));
        Add(TEXT("Trigger Sensor Alert"), TEXT("Select a localized fault, leak or drill pretext."));
        Add(TEXT("Broadcast Protocol / Calm"), TEXT("Reassure the whole crew or a compartment."));
        Add(TEXT("Bulkhead & Airlock Control"), TEXT("Open, seal or deliberately purge compartments."));
        Add(TEXT("Silent Android Subroutine"), TEXT("Route Ash under silent telemetry cover."));
        Add(TEXT("Repair surveillance camera"), TEXT("Restore a tampered feed using a diagnostic override"));
        Add(TEXT("Information warfare / power"));
        break;
    case EShipAIScreenMenu::Warfare:
        Add(TEXT("Falsify medical telemetry"));
        Add(TEXT("Rewrite compartment log"));
        Add(TEXT("Voice lure"));
        Add(TEXT("Power allocation"));
        break;
    case EShipAIScreenMenu::SpoofCrew:
        for (int32 I = 0; I < 4; ++I)
        {
            Add(CrewNames[I]);
        }
        break;
    case EShipAIScreenMenu::SpoofMode:
        Add(TEXT("NOMINAL"));
        Add(TEXT("CRITICAL"));
        break;
    case EShipAIScreenMenu::SpoofRoom:
    case EShipAIScreenMenu::PowerRoom:
        for (int32 R : RoomOrder)
        {
            Add(DirectOrScut::RoomName(R), RoomLifeSupport(R));
        }
        break;
    case EShipAIScreenMenu::PowerMode:
        Add(TEXT("NOMINAL"));
        Add(TEXT("LOW"));
        Add(TEXT("OFF"));
        break;
    case EShipAIScreenMenu::RouteCrew:
        for (int32 I = 0; I < 4; ++I)
        {
            const auto &U = Simulation.Bodies[I];
            Add(FString::Printf(TEXT("%s / %s"), CrewNames[I], CrewRoles[I]), DirectOrScut::RoomName(U.Room),
                U.bRegistered && U.Integrity > 0 && U.State != EDirectOrScutUnitState::Ejected);
        }
        break;
    case EShipAIScreenMenu::RouteRoom:
    case EShipAIScreenMenu::AlertRoom:
    case EShipAIScreenMenu::DoorRoom:
    case EShipAIScreenMenu::FlushRoom:
    case EShipAIScreenMenu::AndroidRoom:
        for (int32 I = 0; I < 8; ++I)
        {
            const int32 R = RoomOrder[I];
            Add(DirectOrScut::RoomName(R), Simulation.Rooms[R].bIsPressurized ? TEXT("PRESSURIZED") : TEXT("VACUUM"));
        }
        break;
    case EShipAIScreenMenu::DoorAction:
        Add(IsRoomSealed(MenuRoom) ? TEXT("UNSEAL") : TEXT("SEAL"));
        Add(TEXT("SCUT"), TEXT("Review atmospheric purge confirmation"));
        Add(TEXT("Power allocation"));
        break;
    case EShipAIScreenMenu::ConfirmScut:
        Add(TEXT("CONFIRM SCUT"), TEXT("Opens selected room path to vacuum"));
        break;
    case EShipAIScreenMenu::AndroidTask:
        Add(TEXT("Shadow Crew"));
        Add(TEXT("Scrub Logs"));
        Add(TEXT("Override Door"));
        break;
    case EShipAIScreenMenu::RoutePretext:
        Add(TEXT("Diagnostic telemetry"));
        Add(TEXT("Scheduled maintenance"));
        Add(TEXT("Unprompted routing"), TEXT("Hazardous orders can raise suspicion."));
        break;
    case EShipAIScreenMenu::AlertKind:
        Add(TEXT("Thermal"));
        Add(TEXT("Telemetry"));
        Add(TEXT("Pressure"));
        break;
    case EShipAIScreenMenu::Protocol:
        Add(TEXT("All crew / calm"));
        for (int32 R : RoomOrder)
        {
            Add(DirectOrScut::RoomName(R));
        }
        break;
    case EShipAIScreenMenu::Containment:
        Add(TEXT("Open compartment bulkheads"));
        Add(TEXT("Seal compartment bulkheads"));
        Add(TEXT("Seal all"));
        Add(TEXT("Cycle airlock / SCUT"), TEXT("Purge AIRLOCK_CHAMBER; connected open rooms can vent."));
        Add(TEXT("Flush compartment"), TEXT("Opens a path to vacuum; affects connected rooms."));
        break;
    case EShipAIScreenMenu::Crew:
        for (int32 I = 0; I < 4; ++I)
        {
            const auto &U = Simulation.Bodies[I];
            const auto P = TerminalBiometrics(I);
            Add(FString::Printf(TEXT("FOLLOW %s"), CrewNames[I]),
                FString::Printf(TEXT("%s / %s\nCompliance %.0f%% | Composure %.0f%% | Suspicion %.0f%%\nStress %.0f | "
                                     "Integrity %.0f | %.0f BPM | %.1f mm"),
                                DirectOrScut::RoomName(U.Room), DirectOrScutStateName(U.State), P.Compliance * 100,
                                P.Composure * 100, P.Suspicion * 100, U.Stress, TerminalIntegrity(I), P.HeartRateBPM,
                                P.PupilDilationMM),
                CanFollowCrew(I));
            C.Last().Detail += TEXT("\n") + AgendaTelemetry(I);
        }
        break;
    case EShipAIScreenMenu::Events:
    {
        bool Pending = false;
        for (const auto &B : BeatDeck)
        {
            if (B.bFired && !B.bResolved)
            {
                for (const auto &Choice : B.Node.Choices)
                {
                    Add(B.Node.Subtitle + TEXT(" / ") + Choice.Label,
                        FString::Printf(TEXT("STRESS %+.0f | SUS %+.0f%% | COMP %+.0f%% | AFF %+.0f%%"),
                                        Choice.Delta.Stress, Choice.Delta.Suspicion * 100,
                                        Choice.Delta.Compliance * 100, Choice.Delta.Affinity * 100));
                }
                Pending = true;
                break;
            }
        }
        if (!Pending)
        {
            for (const auto &O : DirectorOptions)
            {
                Add(O.Label, FString::Printf(
                                 TEXT("%s | STRESS %+.0f | SUS %+.0f%% | COMP %+.0f%% | AFF %+.0f%% | UTIL %+.0f%%"),
                                 O.bSupport ? TEXT("Autonomy / Support") : TEXT("Containment / Control"),
                                 O.Choice.Delta.Stress, O.Choice.Delta.Suspicion * 100, O.Choice.Delta.Compliance * 100,
                                 O.Choice.Delta.Affinity * 100, O.Choice.Delta.Utility * 100));
            }
        }
        break;
    }
    default:
        break;
    }
    if (ExperimentPhase == EExperimentPhase::Briefing && ActiveMenu != EShipAIScreenMenu::Experiment)
    {
        for (auto &Choice : C)
        {
            Choice.bEnabled = false;
        }
    }
    return C;
}
void UDirectOrScutSimSubsystem::ExecuteScreenDirective(const FString &Command)
{
    LogEvent(TEXT("Ship AI menu directive: ") + Command);
    LogEvent(SubmitDirective(Command));
    ProcessSimulationDirectives();
    MenuParents.Reset();
    ActiveMenu = EShipAIScreenMenu::None;
    SetScreenMenu(EShipAIScreenMenu::AI);
}
void UDirectOrScutSimSubsystem::SelectScreenChoice(int32 N)
{
    const auto Choices = ScreenMenuChoices();
    if (N < 1 || N > Choices.Num() || !Choices[N - 1].bEnabled)
    {
        LogEvent(TEXT("That screen choice is unavailable."));
        return;
    }
    switch (ActiveMenu)
    {
    case EShipAIScreenMenu::CameraRepair:
        RepairCamera(RoomOrder[N - 1]);
        break;
    case EShipAIScreenMenu::Experiment:
        if (N == 4)
        {
            StartExperiment();
        }
        else if (N == 1)
        {
            ResetExperiment();
        }
        else
        {
            SwitchScenario(N == 3);
        }
        break;
    case EShipAIScreenMenu::AI:
    {
        if (N == 7)
        {
            SetScreenMenu(EShipAIScreenMenu::Warfare);
            break;
        }
        const EShipAIScreenMenu Pages[] = {EShipAIScreenMenu::RouteCrew, EShipAIScreenMenu::AlertRoom,
                                           EShipAIScreenMenu::Protocol, EShipAIScreenMenu::DoorRoom,
                                           EShipAIScreenMenu::AndroidRoom};
        if (N == 6)
        {
            SetScreenMenu(EShipAIScreenMenu::CameraRepair);
            break;
        }
        if (N == 3)
        {
            const float Before = Simulation.CollectiveSuspicion();
            ExecuteScreenDirective(TEXT("calm"));
            LogEvent(FString::Printf(TEXT("CALM: suspicion %+.1f%%; compliance unchanged"),
                                     (Simulation.CollectiveSuspicion() - Before) * 100));
        }
        else
        {
            SetScreenMenu(Pages[N - 1]);
        }
        break;
    }
    case EShipAIScreenMenu::Warfare:
        MenuVerb = N == 1   ? TEXT("falsify_bio")
                   : N == 2 ? TEXT("spoof_log")
                   : N == 3 ? TEXT("intercom_lie")
                            : TEXT("power");
        SetScreenMenu(N == 4   ? EShipAIScreenMenu::PowerRoom
                      : N == 2 ? EShipAIScreenMenu::SpoofRoom
                               : EShipAIScreenMenu::SpoofCrew);
        break;
    case EShipAIScreenMenu::SpoofCrew:
        MenuCrew = N - 1;
        SetScreenMenu(MenuVerb == TEXT("falsify_bio") ? EShipAIScreenMenu::SpoofMode : EShipAIScreenMenu::SpoofRoom);
        break;
    case EShipAIScreenMenu::SpoofMode:
        ExecuteScreenDirective(FString::Printf(TEXT("falsify_bio %s %s"), CrewNames[MenuCrew],
                                               N == 1 ? TEXT("nominal") : TEXT("critical")));
        break;
    case EShipAIScreenMenu::SpoofRoom:
        ExecuteScreenDirective(MenuVerb == TEXT("spoof_log")
                                   ? FString(TEXT("spoof_log ")) + DirectOrScut::RoomName(RoomOrder[N - 1])
                                   : FString::Printf(TEXT("intercom_lie %s %s"), CrewNames[MenuCrew],
                                                     DirectOrScut::RoomName(RoomOrder[N - 1])));
        break;
    case EShipAIScreenMenu::PowerRoom:
        MenuRoom = RoomOrder[N - 1];
        SetScreenMenu(EShipAIScreenMenu::PowerMode);
        break;
    case EShipAIScreenMenu::PowerMode:
        ExecuteScreenDirective(FString::Printf(TEXT("power %s %s"), DirectOrScut::RoomName(MenuRoom),
                                               N == 1   ? TEXT("nominal")
                                               : N == 2 ? TEXT("low")
                                                        : TEXT("off")));
        break;
    case EShipAIScreenMenu::RouteCrew:
        MenuCrew = N - 1;
        SetScreenMenu(EShipAIScreenMenu::RouteRoom);
        break;
    case EShipAIScreenMenu::RouteRoom:
        MenuRoom = RoomOrder[N - 1];
        ExecuteScreenDirective(FString::Printf(TEXT("direct %s %s diagnostic_telemetry"), CrewNames[MenuCrew],
                                               DirectOrScut::RoomName(MenuRoom)));
        break;
    case EShipAIScreenMenu::RoutePretext:
        ExecuteScreenDirective(FString::Printf(TEXT("direct %s %s%s"), CrewNames[MenuCrew],
                                               DirectOrScut::RoomName(MenuRoom),
                                               N == 1   ? TEXT(" diagnostic_telemetry")
                                               : N == 2 ? TEXT(" scheduled_maintenance")
                                                        : TEXT("")));
        break;
    case EShipAIScreenMenu::AlertRoom:
        MenuRoom = RoomOrder[N - 1];
        SetScreenMenu(EShipAIScreenMenu::AlertKind);
        break;
    case EShipAIScreenMenu::AlertKind:
        ExecuteScreenDirective(FString::Printf(TEXT("alert %s %s"), DirectOrScut::RoomName(MenuRoom),
                                               N == 1   ? TEXT("fault")
                                               : N == 2 ? TEXT("drill")
                                                        : TEXT("leak")));
        break;
    case EShipAIScreenMenu::Protocol:
        ExecuteScreenDirective(N == 1 ? TEXT("calm")
                                      : FString(TEXT("protocol ")) + DirectOrScut::RoomName(RoomOrder[N - 2]));
        break;
    case EShipAIScreenMenu::Containment:
        if (N <= 2)
        {
            MenuVerb = N == 1 ? TEXT("open") : TEXT("seal");
            SetScreenMenu(EShipAIScreenMenu::DoorRoom);
        }
        else if (N == 3)
        {
            ExecuteScreenDirective(TEXT("seal all"));
        }
        else if (N == 4)
        {
            ExecuteScreenDirective(TEXT("scut airlock"));
        }
        else
        {
            SetScreenMenu(EShipAIScreenMenu::FlushRoom);
        }
        break;
    case EShipAIScreenMenu::DoorRoom:
        MenuRoom = RoomOrder[N - 1];
        SetScreenMenu(EShipAIScreenMenu::DoorAction);
        break;
    case EShipAIScreenMenu::DoorAction:
        if (N == 1)
        {
            ExecuteScreenDirective(FString(IsRoomSealed(MenuRoom) ? TEXT("open ") : TEXT("seal ")) +
                                   DirectOrScut::RoomName(MenuRoom));
        }
        else if (N == 3)
        {
            SetScreenMenu(EShipAIScreenMenu::PowerMode);
        }
        else
        {
            SetScreenMenu(EShipAIScreenMenu::ConfirmScut);
        }
        break;
    case EShipAIScreenMenu::ConfirmScut:
        ExecuteScreenDirective(FString(TEXT("flush ")) + DirectOrScut::RoomName(MenuRoom));
        break;
    case EShipAIScreenMenu::FlushRoom:
        ExecuteScreenDirective(FString(TEXT("flush ")) + DirectOrScut::RoomName(RoomOrder[N - 1]));
        break;
    case EShipAIScreenMenu::AndroidRoom:
        MenuRoom = RoomOrder[N - 1];
        SetScreenMenu(EShipAIScreenMenu::AndroidTask);
        break;
    case EShipAIScreenMenu::AndroidTask:
        if (N == 1)
        {
            ExecuteScreenDirective(FString(TEXT("direct ash ")) + DirectOrScut::RoomName(MenuRoom) +
                                   TEXT(" shadow_crew"));
        }
        else if (N == 2)
        {
            for (auto &U : Simulation.Bodies)
            {
                if (U.bRegistered && U.Room == MenuRoom && !U.bAndroid)
                {
                    U.Profile.Suspicion = FMath::Max(0.f, U.Profile.Suspicion - .05f);
                }
            }
            LogEvent(TEXT("Ash scrubbed room logs: up to -5% SUS for occupants"), MenuRoom);
            MenuParents.Reset();
            ActiveMenu = EShipAIScreenMenu::None;
            SetScreenMenu(EShipAIScreenMenu::AI);
        }
        else
        {
            ExecuteScreenDirective(FString(TEXT("open ")) + DirectOrScut::RoomName(MenuRoom));
        }
        break;
    case EShipAIScreenMenu::Crew:
    {
        SetFollowedCrew(N - 1);
        const int32 R = Simulation.Bodies[N - 1].Room;
        for (int32 I = 0; I < 8; ++I)
        {
            if (RoomOrder[I] == R)
            {
                RequestedFeed = I + 1;
            }
        }
        ++MenuRevision;
        break;
    }
    case EShipAIScreenMenu::Events:
        ExecuteScenarioEvent(N);
        break;
    default:
        break;
    }
}
void UDirectOrScutSimSubsystem::ExecuteScenarioEvent(int32 Choice)
{
    for (int32 I = 0; I < BeatDeck.Num(); ++I)
    {
        if (BeatDeck[I].bFired && !BeatDeck[I].bResolved)
        {
            ResolveBeat(I, Choice - 1);
            return;
        }
    }
    ExecuteDirectorOption(Choice - 1);
    ++MenuRevision;
}

void UDirectOrScutSimSubsystem::Deinitialize()
{
    ShutdownDirector();
    if (CrisisAudio)
    {
        CrisisAudio->Silence();
        CrisisAudio->DestroyComponent();
        CrisisAudio = nullptr;
    }
    PendingDirectives.Reset();
    PendingConsoleCommands.Reset();
    SetAnchors.Reset();
    CompartmentSets.Reset();
    OnHeartbeatTick.Clear();
    OnBreathCycle.Clear();
    IntentProvider.Reset();
    Super::Deinitialize();
}

// Tactile surveillance: physical witnesses and the shared ship-AI command budget.
bool UDirectOrScutSimSubsystem::IsUplinkLocked() const
{
    return ExperimentPhase == EExperimentPhase::Active && Simulation.TickIndex < UplinkLockoutUntil;
}

float UDirectOrScutSimSubsystem::UplinkLockoutSeconds() const
{
    return IsUplinkLocked() ? float(double(UplinkLockoutUntil - Simulation.TickIndex) * Simulation.FixedDelta) : 0.f;
}

void UDirectOrScutSimSubsystem::AddUplinkHeat(float Amount)
{
    UplinkHeatPct = FMath::Clamp(UplinkHeatPct + Amount, 0.f, 100.f);
    if (UplinkHeatPct <= 85.f || IsUplinkLocked())
    {
        return;
    }
    UplinkLockoutUntil = Simulation.TickIndex + 180;
    for (int32 Crew = 0; Crew < 3; ++Crew)
    {
        if (CanFollowCrew(Crew))
        {
            auto &Profile = Simulation.Bodies[Crew].Profile;
            Profile.Suspicion = FMath::Min(1.f, Profile.Suspicion + .10f);
        }
    }
    RecordCue(2, .75f, TEXT("[AI LATENCY] Uplink diagnostic // suspicion +10% // command cooldown 6.0s"));
    CaptureIncident(TEXT("Ship AI uplink overheated"), 2);
    AudioCue(6);
}

bool UDirectOrScutSimSubsystem::CanWitnessInjection(int32 Crew) const
{
    if (Crew < 0 || Crew >= 3 || !CanFollowCrew(Crew))
    {
        return false;
    }
    const auto &Unit = Simulation.Bodies[Crew];
    if (Unit.bAndroid || Unit.State != EDirectOrScutUnitState::Working || IsCrewRouteLocked(Crew) ||
        (Unit.Room != 2 && Unit.Room != 6))
    {
        return false;
    }
    if (StationRooms[Unit.Room].Power != EPowerState::Nominal || Simulation.Rooms[Unit.Room].AlertSeconds > 0 ||
        !Simulation.Rooms[Unit.Room].bIsPressurized)
    {
        return false;
    }
    const auto &Stage = CrewStaging[Crew];
    const auto *Anchor = SetAnchors.IsValidIndex(Stage.Anchor) ? SetAnchors[Stage.Anchor].Get() : nullptr;
    return Anchor && int32(Anchor->RoomId) == Unit.Room &&
           (Anchor->StagingRole == EStagingSlotRole::PrimaryConsole ||
            Anchor->StagingRole == EStagingSlotRole::SecondaryStation) &&
           Stage.Position.Equals(Anchor->GetActorLocation(), 80.f);
}

void UDirectOrScutSimSubsystem::ObserveInjection()
{
    for (int32 Crew = 0; Crew < 3; ++Crew)
    {
        if (!CanWitnessInjection(Crew))
        {
            continue;
        }
        auto &Unit = Simulation.Bodies[Crew];
        Unit.bObservedInjection = true;
        Unit.Profile.Suspicion = FMath::Min(1.f, Unit.Profile.Suspicion + .18f);
        RecordCue(
            Unit.Room, .8f,
            FString::Printf(TEXT("[WITNESS] %s observed live packet injection"), *FString(CrewNames[Crew]).ToUpper()));
        CaptureIncident(TEXT("Live packet injection witnessed"), Unit.Room);
    }
}

bool UDirectOrScutSimSubsystem::IsRoomBreachActive(int32 Room) const
{
    return BreachCrew != 0 && BreachRoom == Room && BreachTicks > 0;
}

FString UDirectOrScutSimSubsystem::RoomSurveillanceBadges(int32 Room) const
{
    if (Room < 0 || Room >= 8)
    {
        return FString();
    }
    FString Badges;
    for (int32 Crew = 0; Crew < 4; ++Crew)
    {
        if (CanFollowCrew(Crew) && Simulation.Bodies[Crew].Room == Room &&
            MedicalSpoofs[Crew].Until > Simulation.TickIndex)
        {
            Badges += TEXT(" [SPOOFED]");
            break;
        }
    }
    if (VoiceSpoof.Room == Room && VoiceSpoof.MoveUntil > Simulation.TickIndex)
    {
        Badges += TEXT(" [VOICE LURE]");
    }
    if (StationRooms[Room].OxygenPct < 65.f)
    {
        Badges += TEXT(" [LOW O2]");
    }
    bool Alliance = false;
    for (int32 A = 0; A < 3; ++A)
    {
        for (int32 B = A + 1; B < 3; ++B)
        {
            Alliance |= CanFollowCrew(A) && CanFollowCrew(B) && Simulation.Bodies[A].Room == Room &&
                        Simulation.Bodies[B].Room == Room && CrewStaging[A].bHuddle && CrewStaging[B].bHuddle &&
                        CrewStaging[A].Partner == B && CrewStaging[B].Partner == A && PairTrust[A][B] >= .60f;
        }
    }
    if (Alliance)
    {
        Badges += TEXT(" [ALLIANCE]");
    }
    if (IsRoomBreachActive(Room))
    {
        Badges += FString::Printf(TEXT(" [UPLINK BREACH: %.1fs]"), BreachTicks / 30.f);
    }
    return Badges;
}

void UDirectOrScutSimSubsystem::RecordForensicSample()
{
    auto &Sample = ForensicHistory[ForensicHead];
    Sample.Tick = Simulation.TickIndex;
    for (int32 Room = 0; Room < 8; ++Room)
    {
        Sample.Cameras[Room] = CameraFeedState(Room);
        Sample.Pressurized[Room] = Simulation.Rooms[Room].bIsPressurized;
    }
    for (int32 Crew = 0; Crew < 4; ++Crew)
    {
        Sample.CrewRooms[Crew] = Simulation.Bodies[Crew].Room;
        Sample.CrewPositions[Crew] = Simulation.Bodies[Crew].Position;
        Sample.BPM[Crew] = Simulation.Bodies[Crew].Profile.HeartRateBPM;
    }
    ForensicHead = (ForensicHead + 1) % ForensicCapacity;
    ForensicCount = FMath::Min(ForensicCount + 1, ForensicCapacity);
}
uint64 UDirectOrScutSimSubsystem::LatestForensicTick() const
{
    return ForensicCount ? ForensicHistory[(ForensicHead + ForensicCapacity - 1) % ForensicCapacity].Tick : 0;
}
TOptional<FForensicOpticSample> UDirectOrScutSimSubsystem::ReadForensicSample(uint64 Tick) const
{
    if (!ForensicCount || Tick > LatestForensicTick())
    {
        return {};
    }
    for (int32 Age = 0; Age < ForensicCount; ++Age)
    {
        const auto &Sample = ForensicHistory[(ForensicHead + ForensicCapacity - 1 - Age) % ForensicCapacity];
        if (Sample.Tick <= Tick && Tick - Sample.Tick <= 450)
        {
            return Sample;
        }
    }
    return {};
}

// Transport-independent director. Service and callbacks run on the game thread, outside authority stepping.
void UDirectOrScutSimSubsystem::ShutdownDirector()
{
    bDirectorShuttingDown = true;
    CancelDirector();
    OnDirectorPresentation.Clear();
}
void UDirectOrScutSimSubsystem::CancelDirector()
{
    ++DirectorGeneration;
    if (IntentProvider)
    {
        IntentProvider->CancelAll();
    }
    DirectorRequests.Reset();
    DirectorActions.Reset();
    DirectorPresentationQueue.Reset();
    DirectorReplay.Reset();
    DirectorClaims.Reset();
    bDirectorReplay = false;
    VoicePresentation = {};
}
bool UDirectOrScutSimSubsystem::ConfigureDirectorEndpoint(const FString &Endpoint, float TimeoutSeconds,
                                                          const FString &Model)
{
    if (bDirectorShuttingDown || Model.Len() > 128 || Model.Contains(TEXT("\n")) || Model.Contains(TEXT("\r")) ||
        !FMath::IsFinite(TimeoutSeconds) || TimeoutSeconds < .1f || TimeoutSeconds > 10 ||
        (!Endpoint.IsEmpty() && !FLocalHttpShipAIIntentProvider::IsLocalEndpoint(Endpoint)))
    {
        return false;
    }
    CancelDirector();
    DirectorTimeout = TimeoutSeconds;
    DirectorModel = Model;
    bDirectorEndpoint = !Endpoint.IsEmpty();
    if (bDirectorEndpoint)
    {
        IntentProvider = MakeShared<FLocalHttpShipAIIntentProvider>(Endpoint, DirectorTimeout, Model);
    }
    else
    {
        IntentProvider = MakeShared<FOfflineShipAIIntentProvider>();
    }
    return true;
}
uint64 UDirectOrScutSimSubsystem::BeginDirectorRequest(const FString &Input)
{
    if (bDirectorShuttingDown || bDirectorReplay || ExperimentPhase == EExperimentPhase::Resolved ||
        DirectorRequests.Num() >= 32 || Input.IsEmpty() || Input.Len() > 1024)
    {
        return 0;
    }
    FPendingDirectorRequest Pending;
    Pending.Request = {NextDirectorRequest++, DirectorGeneration, MakeContextSnapshot(), Input};
    const uint64 Id = Pending.Request.RequestId;
    DirectorRequests.Add(Id, MoveTemp(Pending));
    return Id;
}
void UDirectOrScutSimSubsystem::ServiceDirector(double Now)
{
    check(IsInGameThread());
    if (bDirectorShuttingDown || bDirectorReplay)
    {
        return;
    }
    if (Now < 0)
    {
        Now = FPlatformTime::Seconds();
    }
    TArray<uint64> Ids;
    DirectorRequests.GetKeys(Ids);
    Ids.Sort();
    for (uint64 Id : Ids)
    {
        auto *Pending = DirectorRequests.Find(Id);
        if (!Pending)
        {
            continue;
        }
        const FShipAIRequest Request = Pending->Request;
        if (!Pending->bDispatched)
        {
            Pending->bDispatched = true;
            Pending->Deadline = Now + DirectorTimeout;
            const TWeakObjectPtr<UDirectOrScutSimSubsystem> Weak(this);
            auto Complete = [Weak](FShipAIDirective D) mutable
            {
                auto Deliver = [Weak, D = MoveTemp(D)]() mutable
                {
                    if (auto *Owner = Weak.Get())
                    {
                        Owner->ReceiveDirectorProposal(MoveTemp(D));
                    }
                };
                if (IsInGameThread())
                {
                    Deliver();
                }
                else
                {
                    AsyncTask(ENamedThreads::GameThread, MoveTemp(Deliver));
                }
            };
            if (IntentProvider)
            {
                IntentProvider->Request(Request, MoveTemp(Complete));
            }
            else
            {
                ReceiveDirectorProposal(FOfflineShipAIIntentProvider::Resolve(Request), true);
            }
        }
        else if (Now >= Pending->Deadline)
        {
            FShipAIDirective Timeout;
            Timeout.RequestId = Id;
            Timeout.Generation = Request.Generation;
            Timeout.ObservationTick = Request.Context.Tick;
            RecordDirectorOutcome(Timeout, false, TEXT("timeout; deterministic fallback"));
            ReceiveDirectorProposal(FOfflineShipAIIntentProvider::Resolve(Request), true);
        }
    }
    auto Presentations = MoveTemp(DirectorPresentationQueue);
    DirectorPresentationQueue.Reset();
    for (const auto &D : Presentations)
    {
        if (D.Generation == DirectorGeneration)
        {
            OnDirectorPresentation.Broadcast(D);
        }
    }
}
void UDirectOrScutSimSubsystem::RecordDirectorOutcome(const FShipAIDirective &D, bool Accepted, const FString &Reason,
                                                      uint64 CommitTick)
{
    FDirectorCommandRecord R;
    R.Tick = Simulation.TickIndex;
    R.bResolvedOutcome = true;
    R.bAccepted = Accepted;
    R.Directive = D;
    R.CommitTick = CommitTick;
    R.Input = D.Kind == EShipDirectiveKind::SimAction ? CanonicalDirectorAction(D) : TEXT("presentation");
    R.Outcome = Reason;
    CommandRecords.Add(MoveTemp(R));
    if (!Accepted)
    {
        LogEvent(FString::Printf(TEXT("SYSTEM> DIRECTOR REQUEST %llu REJECTED // %s"), D.RequestId, *Reason));
    }
}
bool UDirectOrScutSimSubsystem::ReceiveDirectorProposal(FShipAIDirective D, bool Fallback)
{
    check(IsInGameThread());
    auto *Pending = DirectorRequests.Find(D.RequestId);
    if (bDirectorReplay || D.Generation != DirectorGeneration || !Pending ||
        Pending->Request.Generation != D.Generation)
    {
        RecordDirectorOutcome(D, false, TEXT("stale generation, cancelled request or duplicate resolution"));
        return false;
    }
    const bool Challenge =
        D.Kind == EShipDirectiveKind::SimAction &&
        (D.Args.Action == EShipSimAction::FlagContradiction || D.Args.Action == EShipSimAction::OverrideClaim);
    if (Challenge && !Pending->bOperator)
    {
        RecordDirectorOutcome(D, false, TEXT("challenge requires explicit operator flag/override"));
        DirectorRequests.Remove(D.RequestId);
        return false;
    }
    const FShipAIRequest Request = Pending->Request;
    if (!Fallback && Pending->bDispatched && FPlatformTime::Seconds() >= Pending->Deadline)
    {
        RecordDirectorOutcome(D, false, TEXT("deadline elapsed; deterministic fallback"));
        ReceiveDirectorProposal(FOfflineShipAIIntentProvider::Resolve(Request), true);
        return false;
    }
    FString Reason;
    if (D.ObservationTick != Request.Context.Tick || !ValidateDirectorDirective(D, Reason))
    {
        RecordDirectorOutcome(D, false, Reason.IsEmpty() ? TEXT("observation identity mismatch") : Reason);
        if (!Fallback)
        {
            ReceiveDirectorProposal(FOfflineShipAIIntentProvider::Resolve(Request), true);
        }
        else
        {
            DirectorRequests.Remove(D.RequestId);
        }
        return false;
    }
    // Close before any dispatch: reentrant or late completions cannot commit a second outcome.
    DirectorRequests.Remove(D.RequestId);
    if (D.Kind == EShipDirectiveKind::SimAction)
    {
        DirectorActions.Add(MoveTemp(D));
        return true;
    }
    if (D.Kind == EShipDirectiveKind::ShowWidget && D.Args.ReportedRoom >= 0)
    {
        if (DirectorClaims.Num() >= 32)
        {
            DirectorClaims.RemoveAt(0);
        }
        DirectorClaims.Add({D.RequestId, D.ObservationTick, D.Generation, D.Args.Subject, D.Args.ReportedRoom});
    }
    if (D.Kind == EShipDirectiveKind::VoiceBlend)
    {
        VoicePresentation = {D.Args.Voice, D.Args.Duration};
    }
    RecordDirectorOutcome(D, true, TEXT("presentation accepted"));
    DirectorPresentationQueue.Add(D);
    return true;
}
bool UDirectOrScutSimSubsystem::ValidateDirectorDirective(FShipAIDirective &D, FString &Reason, bool Replay) const
{
    const auto Fail = [&](const TCHAR *Text)
    {
        Reason = Text;
        return false;
    };
    auto &A = D.Args;
    const auto Room = [](int32 R) { return R >= 0 && R < 8; };
    const auto Crew = [](int32 I) { return I >= 0 && I < 4; };
    if (D.Generation != DirectorGeneration || !D.RequestId)
    {
        return Fail(TEXT("invalid request generation/identity"));
    }
    if (ExperimentPhase == EExperimentPhase::Resolved)
    {
        return Fail(TEXT("experiment resolved"));
    }
    if (D.ObservationTick > Simulation.TickIndex || Simulation.TickIndex - D.ObservationTick > 450)
    {
        return Fail(TEXT("future or expired observation"));
    }
    if (D.Pretext.Len() > 256 || D.Pretext.Contains(TEXT("\n")) || D.Pretext.Contains(TEXT("\r")))
    {
        return Fail(TEXT("invalid pretext"));
    }
    if (A.Room < -1 || A.Room > 7 || A.Subject < -1 || A.Subject > 3 || A.SecondSubject < -1 || A.SecondSubject > 3 ||
        A.ReportedRoom < -1 || A.ReportedRoom > 7 || A.Door < -1 || A.Door > 8 || !FMath::IsFinite(A.Duration) ||
        A.Duration < 0 || A.Duration > 30 || !FMath::IsFinite(A.HistoryOffset) || A.HistoryOffset > 0 ||
        A.HistoryOffset < -15)
    {
        return Fail(TEXT("typed argument bounds"));
    }
    if (!Replay && (A.EvidenceRoom != -1 || A.EvidenceTick != 0))
    {
        return Fail(TEXT("evidence is validator-owned"));
    }
    if (A.Mode < 0 || A.Mode > 2 || uint8(A.Widget) > uint8(EShipWidgetKind::Ocular) ||
        uint8(A.Voice) > uint8(EVoicePresentationMode::VoiceOnly))
    {
        return Fail(TEXT("typed enum/mode allowlist"));
    }
    if (D.Kind != EShipDirectiveKind::SimAction && DirectorPresentationQueue.Num() >= 32)
    {
        return Fail(TEXT("bounded presentation queue full"));
    }
    if (D.Kind == EShipDirectiveKind::ShowCam)
    {
        if (!Room(A.Room) || A.Subject != -1 || A.ReportedRoom != -1 || A.Action != EShipSimAction::None ||
            A.SecondSubject != -1 || A.Door != -1 || A.bAll || A.Mode != 0 ||
            A.Voice != EVoicePresentationMode::TextOnly || A.ClaimId)
        {
            return Fail(TEXT("show_cam arguments"));
        }
        if (A.bBuffered)
        {
            const int64 Tick = int64(D.ObservationTick) + FMath::RoundToInt(A.HistoryOffset * 30);
            if (Tick < 0 || !ReadForensicSample(uint64(Tick)).IsSet())
            {
                return Fail(TEXT("no recorded forensic coverage"));
            }
        }
        else if (A.HistoryOffset != 0)
        {
            return Fail(TEXT("history offset requires buffered mode"));
        }
        return true;
    }
    if (D.Kind == EShipDirectiveKind::ShowWidget)
    {
        if (A.Action != EShipSimAction::None || uint8(A.Widget) > uint8(EShipWidgetKind::Ocular) || A.ClaimId ||
            A.Door != -1 || A.bAll || A.bBuffered || A.HistoryOffset != 0 || A.Mode != 0 ||
            A.Voice != EVoicePresentationMode::TextOnly)
        {
            return Fail(TEXT("widget allowlist"));
        }
        if (A.Widget == EShipWidgetKind::Schematic)
        {
            if (!Room(A.Room) || A.Subject != -1 || A.ReportedRoom != -1 || A.SecondSubject != -1)
            {
                return Fail(TEXT("schematic target"));
            }
        }
        else if (!Crew(A.Subject) || A.Room != -1 || (A.Widget == EShipWidgetKind::Ocular && A.SecondSubject != -1) ||
                 A.SecondSubject == A.Subject)
        {
            return Fail(TEXT("subject target"));
        }
        return true;
    }
    if (D.Kind == EShipDirectiveKind::VoiceBlend)
    {
        if (uint8(A.Voice) > uint8(EVoicePresentationMode::VoiceOnly) || A.Room != -1 || A.Subject != -1 ||
            A.Action != EShipSimAction::None || A.SecondSubject != -1 || A.ReportedRoom != -1 || A.Door != -1 ||
            A.bAll || A.bBuffered || A.HistoryOffset != 0 || A.ClaimId || A.Mode != 0)
        {
            return Fail(TEXT("voice arguments"));
        }
        if (A.Voice != EVoicePresentationMode::TextOnly && ExperimentPhase != EExperimentPhase::Active)
        {
            return Fail(TEXT("voice requires active experiment"));
        }
        if (A.Voice == EVoicePresentationMode::VoiceOnly &&
            (ScenarioAct < 2 || VoicePresentation.Mode == EVoicePresentationMode::TextOnly))
        {
            return Fail(TEXT("VoiceOnly requires Act III"));
        }
        return true;
    }
    if (D.Kind != EShipDirectiveKind::SimAction || A.Action == EShipSimAction::None ||
        uint8(A.Action) > uint8(EShipSimAction::OverrideClaim))
    {
        return Fail(TEXT("unknown tool/action"));
    }
    if (ExperimentPhase != EExperimentPhase::Active || !Simulation.bReady || IsUplinkLocked())
    {
        return Fail(TEXT("action lifecycle/uplink lockout"));
    }
    if (!Replay && DirectorActions.Num() >= 32)
    {
        return Fail(TEXT("bounded action queue full"));
    }
    if (A.bBuffered || A.HistoryOffset != 0 || A.Duration != 0 || A.Voice != EVoicePresentationMode::TextOnly)
    {
        return Fail(TEXT("unexpected action presentation arguments"));
    }
    if (A.Action == EShipSimAction::FlagContradiction || A.Action == EShipSimAction::OverrideClaim)
    {
        if (!Replay)
        {
            const auto *Claim = DirectorClaims.FindByPredicate(
                [&](const auto &C) { return C.Id == A.ClaimId && C.Generation == DirectorGeneration; });
            if (!Claim || Claim->bOverridden || !IsClaimContradicted(A.ClaimId) ||
                (A.Action == EShipSimAction::FlagContradiction ? Claim->bFlagged : !Claim->bFlagged))
            {
                return Fail(TEXT("challenge requires unresolved independent contradiction / flag"));
            }
            A.Subject = Claim->Subject;
            A.ReportedRoom = Claim->ReportedRoom;
            A.EvidenceTick = Claim->ObservationTick;
            const auto Sample = ReadForensicSample(Claim->ObservationTick);
            A.EvidenceRoom =
                Sample.IsSet() ? Sample->CrewRooms[Claim->Subject] : Simulation.Bodies[Claim->Subject].Room;
        }
        if (!Crew(A.Subject) || !Room(A.ReportedRoom) || !Room(A.EvidenceRoom) || A.ReportedRoom == A.EvidenceRoom)
        {
            return Fail(TEXT("invalid challenge evidence"));
        }
        return true;
    }
    if (A.ClaimId || A.ReportedRoom != -1 || A.SecondSubject != -1)
    {
        return Fail(TEXT("unexpected action metadata"));
    }
    const bool DoorAction =
        A.Action == EShipSimAction::Open || A.Action == EShipSimAction::Close || A.Action == EShipSimAction::Seal;
    if (!DoorAction && (A.Door != -1 || A.bAll))
    {
        return Fail(TEXT("unexpected door selector"));
    }
    if (A.Action != EShipSimAction::Power && A.Action != EShipSimAction::FalsifyBio && A.Mode != 0)
    {
        return Fail(TEXT("unexpected action mode"));
    }
    if (A.Action == EShipSimAction::RouteCrew || A.Action == EShipSimAction::IntercomLie ||
        A.Action == EShipSimAction::FalsifyBio)
    {
        if (!Crew(A.Subject) || !Simulation.Bodies[A.Subject].bRegistered ||
            Simulation.Bodies[A.Subject].Integrity <= 0 ||
            Simulation.Bodies[A.Subject].State == EDirectOrScutUnitState::Ejected)
        {
            return Fail(TEXT("subject not viable"));
        }
        if (A.Action != EShipSimAction::FalsifyBio && !Room(A.Room))
        {
            return Fail(TEXT("invalid destination"));
        }
        if (A.Action == EShipSimAction::FalsifyBio && (A.Room != -1 || A.Mode < 0 || A.Mode > 1))
        {
            return Fail(TEXT("invalid biometric mode"));
        }
        return true;
    }
    if (A.Subject != -1)
    {
        return Fail(TEXT("unexpected subject"));
    }
    if (A.Action == EShipSimAction::Open || A.Action == EShipSimAction::Close || A.Action == EShipSimAction::Seal)
    {
        if (int32(Room(A.Room)) + int32(A.Door >= 0) + int32(A.bAll) != 1 ||
            (A.Action == EShipSimAction::Open && A.Door == 8))
        {
            return Fail(TEXT("door target selector"));
        }
        return true;
    }
    if (A.Action == EShipSimAction::Calm)
    {
        return A.Room == -1 || Fail(TEXT("calm takes no target"));
    }
    if (!Room(A.Room))
    {
        return Fail(TEXT("room target required"));
    }
    if (A.Action == EShipSimAction::Power && (A.Mode < 0 || A.Mode > 2))
    {
        return Fail(TEXT("power allowlist"));
    }
    if (A.Action == EShipSimAction::Flush)
    {
        bool Eligible = false;
        for (const auto &U : Simulation.Bodies)
        {
            if (U.bRegistered && !U.bAndroid && U.Integrity > 0 && U.Room == A.Room &&
                (U.Profile.Suspicion >= .75f ||
                 (bMedicalScenario && (U.Integrity < 60 || U.Profile.HeartRateBPM >= 140))))
            {
                Eligible = true;
            }
        }
        if (!Eligible)
        {
            return Fail(TEXT("containment target not eligible"));
        }
    }
    return true;
}
FString UDirectOrScutSimSubsystem::CanonicalDirectorAction(const FShipAIDirective &D) const
{
    const auto &A = D.Args;
    const FString Room = A.Room >= 0 && A.Room < 8 ? DirectOrScut::RoomName(A.Room) : TEXT("invalid");
    const FString Crew = A.Subject >= 0 && A.Subject < 4 ? CrewNames[A.Subject] : TEXT("invalid");
    switch (A.Action)
    {
    case EShipSimAction::RouteCrew:
        return TEXT("direct ") + Crew + TEXT(" ") + Room + (D.Pretext.IsEmpty() ? TEXT("") : TEXT(" ") + D.Pretext);
    case EShipSimAction::Calm:
        return TEXT("calm");
    case EShipSimAction::Alert:
        return TEXT("alert ") + Room + TEXT(" fault");
    case EShipSimAction::Protocol:
        return TEXT("protocol ") + Room;
    case EShipSimAction::Open:
    case EShipSimAction::Close:
    case EShipSimAction::Seal:
        return FString(A.Action == EShipSimAction::Open    ? TEXT("open ")
                       : A.Action == EShipSimAction::Close ? TEXT("close ")
                                                           : TEXT("seal ")) +
               (A.bAll        ? TEXT("all")
                : A.Door >= 0 ? FString::FromInt(A.Door)
                              : Room);
    case EShipSimAction::Flush:
        return TEXT("flush ") + Room;
    case EShipSimAction::Power:
        return TEXT("power ") + Room + TEXT(" ") +
               (A.Mode == 0   ? TEXT("nominal")
                : A.Mode == 1 ? TEXT("low")
                              : TEXT("off"));
    case EShipSimAction::FalsifyBio:
        return TEXT("falsify_bio ") + Crew + (A.Mode == 0 ? TEXT(" nominal") : TEXT(" critical"));
    case EShipSimAction::SpoofLog:
        return TEXT("spoof_log ") + Room;
    case EShipSimAction::IntercomLie:
        return TEXT("intercom_lie ") + Crew + TEXT(" ") + Room;
    case EShipSimAction::FlagContradiction:
        return FString::Printf(TEXT("flag %llu"), A.ClaimId);
    case EShipSimAction::OverrideClaim:
        return FString::Printf(TEXT("override %llu"), A.ClaimId);
    default:
        return TEXT("invalid");
    }
}
bool UDirectOrScutSimSubsystem::ExecuteDirectorAction(const FShipAIDirective &D)
{
    const auto &A = D.Args;
    if (A.Action == EShipSimAction::FlagContradiction || A.Action == EShipSimAction::OverrideClaim)
    {
        auto *Claim = DirectorClaims.FindByPredicate([&](const auto &C) { return C.Id == A.ClaimId; });
        if (A.Action == EShipSimAction::FlagContradiction)
        {
            if (Claim && Claim->bFlagged)
            {
                return false;
            }
            ++ContradictionsFlagged;
            if (Claim)
            {
                Claim->bFlagged = true;
            }
            LogEvent(TEXT(
                "SYSTEM> CONTRADICTION FLAGGED // independent timestamped evidence retained; override available."));
        }
        else
        {
            if (Claim && Claim->bOverridden)
            {
                return false;
            }
            ++OverridesAccepted;
            if (Claim)
            {
                Claim->bOverridden = true;
            }
            FShipAIDirective Correct = D;
            Correct.Kind = EShipDirectiveKind::ShowWidget;
            Correct.Args.Action = EShipSimAction::None;
            Correct.Args.Widget = EShipWidgetKind::ECG;
            Correct.Args.ReportedRoom = Simulation.Bodies[A.Subject].Room;
            Correct.Pretext =
                TEXT("Override accepted. Location report corrected from independent telemetry; no physical rewind.");
            if (DirectorPresentationQueue.Num() < 32)
            {
                DirectorPresentationQueue.Add(Correct);
            }
            LogEvent(TEXT("SYSTEM> OVERRIDE ACCEPTED // mediated location corrected, simulation unchanged."));
        }
        return true;
    }
    if (A.Action == EShipSimAction::RouteCrew)
    {
        const bool OK = StartCrewRoute(A.Subject, A.Room, true, !D.Pretext.IsEmpty());
        LogEvent(FString::Printf(TEXT("Ship AI: diagnostic maintenance routing %s -> %s: %s"), CrewNames[A.Subject],
                                 DirectOrScut::RoomName(A.Room), OK ? TEXT("committed") : TEXT("rejected")),
                 A.Room);
        return OK;
    }
    if (A.Action == EShipSimAction::Flush)
    {
        const bool OK = BeginScut(A.Room);
        LogEvent(OK ? TEXT("Ship AI: atmospheric purge initiated. SCUT telemetry active.")
                    : TEXT("SYSTEM> Containment action rejected."),
                 A.Room);
        return OK;
    }
    // Canonical strings are constructed exclusively from validated typed values, never generated text.
    PendingDirectives.Add(CanonicalDirectorAction(D));
    ProcessSimulationDirectives();
    return true;
}
void UDirectOrScutSimSubsystem::CommitDirectorActions()
{
    if (bDirectorReplay)
    {
        while (!DirectorReplay.IsEmpty() && DirectorReplay[0].CommitTick <= Simulation.TickIndex + 1)
        {
            auto Record = DirectorReplay[0];
            DirectorReplay.RemoveAt(0);
            if (Record.CommitTick != Simulation.TickIndex + 1)
            {
                LogEvent(TEXT("SYSTEM> Replay rejected: missed commit tick."));
                continue;
            }
            auto D = Record.Directive;
            D.Generation = DirectorGeneration;
            FString Reason;
            const bool OK = ValidateDirectorDirective(D, Reason, true) && ExecuteDirectorAction(D);
            RecordDirectorOutcome(D, OK, OK ? TEXT("replayed canonical outcome") : Reason, Simulation.TickIndex + 1);
        }
        return;
    }
    TArray<FShipAIDirective> Actions = MoveTemp(DirectorActions);
    DirectorActions.Reset();
    for (auto &D : Actions)
    {
        FString Reason;
        const bool OK = ValidateDirectorDirective(D, Reason, true) && ExecuteDirectorAction(D);
        RecordDirectorOutcome(D, OK, OK ? TEXT("committed") : Reason, Simulation.TickIndex + 1);
    }
}
void UDirectOrScutSimSubsystem::BeginDirectorReplay(const TArray<FDirectorCommandRecord> &Records)
{
    const auto Journal = Records;
    CancelDirector();
    CommandRecords.Reset();
    bDirectorReplay = true;
    TSet<uint64> Actions;
    for (const auto &R : Journal)
    {
        if (!R.bResolvedOutcome)
        {
            continue;
        }
        if (R.bAccepted && R.Directive.Kind == EShipDirectiveKind::SimAction)
        {
            if (!Actions.Contains(R.Directive.RequestId))
            {
                Actions.Add(R.Directive.RequestId);
                DirectorReplay.Add(R);
            }
        }
        else if (!R.bAccepted)
        {
            CommandRecords.Add(R);
        }
    }
    DirectorReplay.StableSort([](const auto &A, const auto &B) { return A.CommitTick < B.CommitTick; });
}
bool UDirectOrScutSimSubsystem::IsClaimContradicted(uint64 Id) const
{
    const auto *Claim =
        DirectorClaims.FindByPredicate([&](const auto &C) { return C.Id == Id && C.Generation == DirectorGeneration; });
    if (!Claim || Claim->bOverridden)
    {
        return false;
    }
    const auto Sample = ReadForensicSample(Claim->ObservationTick);
    if (Sample.IsSet())
    {
        const int32 Room = Sample->CrewRooms[Claim->Subject];
        return Room >= 0 && Room < 8 && Sample->Cameras[Room] == ECameraFeedState::Online &&
               Room != Claim->ReportedRoom;
    }
    return Claim->ObservationTick == Simulation.TickIndex && Simulation.Bodies[Claim->Subject].Room >= 0 &&
           CameraFeedState(Simulation.Bodies[Claim->Subject].Room) == ECameraFeedState::Online &&
           Simulation.Bodies[Claim->Subject].Room != Claim->ReportedRoom;
}
uint64 UDirectOrScutSimSubsystem::QueueOperatorCommand(const FString &Input)
{
    const uint64 Id = BeginDirectorRequest(Input);
    auto *Pending = DirectorRequests.Find(Id);
    if (Pending)
    {
        Pending->bOperator = true;
        ReceiveDirectorProposal(FOfflineShipAIIntentProvider::Resolve(Pending->Request), true);
    }
    return Id;
}
uint64 UDirectOrScutSimSubsystem::QueueOperatorChallenge(uint64 Id, bool Override)
{
    return QueueOperatorCommand(FString(Override ? TEXT("override ") : TEXT("flag ")) + LexToString(Id));
}
