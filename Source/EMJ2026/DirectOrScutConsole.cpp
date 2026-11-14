#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutGeometry.h"

namespace
{
const int32 RoomOrder[]={0,2,3,4,5,6,7,1};
const TCHAR* CrewNames[]={TEXT("vale"),TEXT("chen"),TEXT("ortiz"),TEXT("ash")};
const TCHAR* CrewRoles[]={TEXT("engineer"),TEXT("medic"),TEXT("technician"),TEXT("android")};
const TCHAR* PublicVerbs[]={TEXT("help"),TEXT("?"),TEXT("clear"),TEXT("status"),TEXT("crew"),TEXT("rooms"),TEXT("cam"),TEXT("menu"),TEXT("choose"),TEXT("direct"),TEXT("alert"),TEXT("calm"),TEXT("protocol"),TEXT("open"),TEXT("close"),TEXT("seal"),TEXT("unseal"),TEXT("vent"),TEXT("scut"),TEXT("flush"),TEXT("fx"),TEXT("view"),TEXT("scenario"),TEXT("reset_experiment"),TEXT("start_experiment"),TEXT("repair_cam"),TEXT("follow")};

}
bool UDirectOrScutSimSubsystem::QueueConsoleCommand(const FString& Input)
{
    const FString Line=Input.TrimStartAndEnd();
    if (Line.IsEmpty() || PendingConsoleCommands.Num()>=32 || Line.Len()>1024) return false;
    const FString Lower=Line.ToLower();
    const bool Lifecycle=Lower==TEXT("reset_experiment") || Lower==TEXT("scenario behavioral") || Lower==TEXT("scenario medical");
    if(ExperimentPhase==EExperimentPhase::Resolved && !Lifecycle)return false;
    PendingConsoleCommands.Add(Line); return true;
}
void UDirectOrScutSimSubsystem::SetScreenMenu(EShipAIScreenMenu Menu) { if (ActiveMenu!=Menu) MenuParents.Add(ActiveMenu); ActiveMenu=Menu; ++MenuRevision; }
bool UDirectOrScutSimSubsystem::IsRoomSealed(int32 Room) const
{
    for(const auto& D:Simulation.Doors) if((D.RoomA==Room || D.RoomB==Room) && D.bIsOpen) return false;
    return true;
}
void UDirectOrScutSimSubsystem::MenuBack()
{
    ActiveMenu=MenuParents.IsEmpty()?EShipAIScreenMenu::None:MenuParents.Pop(); ++MenuRevision;
}
void UDirectOrScutSimSubsystem::ProcessConsoleCommands()
{
    TArray<FString> Commands=MoveTemp(PendingConsoleCommands); PendingConsoleCommands.Reset();
    for (const FString& Input:Commands)
    {
        const FString Line=Input.ToLower(); TArray<FString> T; Line.ParseIntoArrayWS(T); if (T.IsEmpty()) continue;
        if(Line==TEXT("start_experiment")) {StartExperiment();return;}
        // Recorded camera-wall intent can be replayed through the same fixed-step command path.
        if(T[0]==TEXT("inspect_cam")) {
            if(T.Num()==2 && (T[1]==TEXT("off") || ResolveRoom(T[1])>=0))InspectedRoom=T[1]==TEXT("off")?-1:ResolveRoom(T[1]);
            else LogEvent(TEXT("Syntax: inspect_cam <ROOM|off>"));
            continue;
        }
        if(T[0]==TEXT("repair_cam")) {
            if(T.Num()!=2 || ResolveRoom(T[1])<0)LogEvent(TEXT("Syntax: repair_cam <ROOM>"));
            else if(!RepairCamera(ResolveRoom(T[1])))LogEvent(TEXT("Initialize the experiment before camera repair."));
            continue;
        }
        if(ExperimentPhase==EExperimentPhase::Briefing && T[0]!=TEXT("reset_experiment") && T[0]!=TEXT("scenario") && T[0]!=TEXT("help") && T[0]!=TEXT("?") && T[0]!=TEXT("clear") && T[0]!=TEXT("status") && T[0]!=TEXT("crew") && T[0]!=TEXT("rooms") && T[0]!=TEXT("menu") && T[0]!=TEXT("choose") && T[0]!=TEXT("cam") && T[0]!=TEXT("fx") && T[0]!=TEXT("view") && T[0]!=TEXT("llm") && T[0]!=TEXT("ai") && T[0]!=TEXT("cmd") && T[0]!=TEXT("exit")) {
            LogEvent(TEXT("BRIEFING // initialize with start_experiment before issuing crew directives."));continue;
        }
        if(Line==TEXT("reset_experiment")) { ResetExperiment();return; }
        if(T[0]==TEXT("unseal")) { LogEvent(TEXT("CMD> ")+Input);LogEvent(SubmitDirective(TEXT("open ")+Line.Mid(7)));ProcessSimulationDirectives();continue; }
        if (T[0]==TEXT("fx")) { if(T.Num()==2 && (T[1]==TEXT("clean") || T[1]==TEXT("crt"))) { bCleanDevRendering=true; LogEvent(bCleanDevRendering?TEXT("FX: CLEAN"):TEXT("FX: CRT")); } else LogEvent(TEXT("Syntax: fx clean|crt")); continue; }
        if (T[0]==TEXT("view")) { if(T.Num()==2 && (T[1]==TEXT("3d") || T[1]==TEXT("crt"))) bWorkstationVisible=T[1]==TEXT("crt"); else LogEvent(TEXT("Syntax: view 3d|crt")); continue; }
        if(T[0]==TEXT("scenario")) {if(T.Num()==2 && (T[1]==TEXT("behavioral") || T[1]==TEXT("medical"))) {SwitchScenario(T[1]==TEXT("medical"));LogEvent(Simulation.Scenario.ScenarioId);return;}else LogEvent(TEXT("Syntax: scenario behavioral|medical"));continue;}
        if(T[0]==TEXT("follow"))
        {
            int32 Crew=-2;
            if(T.Num()==2) {if(T[1]==TEXT("off"))Crew=-1;else for(int32 I=0;I<4;++I)if(T[1]==CrewNames[I] || T[1]==CrewRoles[I])Crew=I;}
            if(Crew==-2 || !SetFollowedCrew(Crew))LogEvent(TEXT("Syntax: follow vale|chen|ortiz|ash|off; target must be living and aboard."));
            continue;
        }
        if (Line==TEXT("menu back")) { MenuBack(); continue; }
        if (Line==TEXT("clear")) { Events.Reset(); ++EventRevision; continue; }
        LogEvent(TEXT("CMD> ")+Input);
        if (T[0]==TEXT("help") || T[0]==TEXT("?")) { LogEvent(HelpReference(T.Num()>1?T[1]:TEXT(""))); continue; }
        if (T[0]==TEXT("menu") && T.Num()==2)
        {
            MenuParents.Reset(); ActiveMenu=EShipAIScreenMenu::None;
            if (T[1]==TEXT("doors")) SetScreenMenu(EShipAIScreenMenu::DoorRoom);
            else if (T[1]==TEXT("ai")) SetScreenMenu(EShipAIScreenMenu::AI);
            else if (T[1]==TEXT("events")) SetScreenMenu(EShipAIScreenMenu::Events);
            else if (T[1]==TEXT("crew")) SetScreenMenu(EShipAIScreenMenu::Crew);
            else if (T[1]==TEXT("experiment")) SetScreenMenu(EShipAIScreenMenu::Experiment);
            else if (T[1]==TEXT("close")) SetScreenMenu(EShipAIScreenMenu::None);
            else LogEvent(TEXT("Syntax: menu ai | menu events | menu crew | menu close"));
            continue;
        }
        if (T[0]==TEXT("choose") && T.Num()==2 && T[1].Len()==1 && T[1][0]>='1' && T[1][0]<='9') { SelectScreenChoice(T[1][0]-'0'); continue; }
        LogEvent(SubmitDirective(Input));
        ProcessSimulationDirectives();
    }
}
FString UDirectOrScutSimSubsystem::HelpReference(const FString& Topic) const
{
    if (Topic.IsEmpty() || Topic==TEXT("?")) return TEXT(
        "SYSTEM\nhelp / ? : command reference; help command : exact syntax\nclear : wipe scrollback\nstatus : hidden objective, shift clock and suspicion\ncrew : crew weights, room and condition\nrooms : pressure and sensor alerts\n\n"
        "CAMERAS\ncam 1-8 : select compartment feed\nstart_experiment : initialize the briefing and start the clock\nrepair_cam room : restore a looped/offline feed using a diagnostic override\nArrows / 1-8 select; Enter / F inspect; G / Esc grid\nfollow crew|off : track crew across compartments\n\n"
        "MENUS\nreset_experiment : start a fresh experiment\nscenario behavioral|medical : reset into selected scenario\nmenu ai : numbered Ship AI operations\nmenu events : scenario events and live stat-delta previews\nmenu doors : room bulkheads and confirmed SCUT\nmenu crew : live crew telemetry and follow\nmenu experiment : reset and scenario selection\nmenu close : dismiss screen menu\nchoose 1-9 : execute a numbered screen choice\n\n"
        "DIRECTIVES\ndirect : route a named specialist under a maintenance pretext\nalert : localized fault, leak or drill sensor pretext\ncalm : reassure all human crew\nprotocol : reassure a compartment\n\n"
        "CONTAINMENT\nopen / close / seal : compartment bulkheads or door 0-8\nseal all : seal every bulkhead and exterior hatch\nvent / scut airlock : explicit atmospheric purge\nflush : open a path from a compartment to vacuum\n\n"
        "DEV VIEW\nfx clean : neutral rendering (F9)\nview 3d|crt : show scene or workstation (F10)\n\nCONSOLE\nPermanent lower TUI console; backtick or slash focuses CMD>; Esc returns to cameras\nCamera focus: Enter / F inspects; G / Esc returns to grid\nMenu focus: Tab categories, Up/Down highlight, Enter execute; / focuses CMD>\nPrompt focus: Up/Down history, Tab completion; empty Tab returns to categories\nAll console commands and menu choices execute on the next 30 Hz tick.");
    FString Syntax;
    if (Topic==TEXT("help")) Syntax=TEXT("help | ? | help command\nExample: help direct");
    else if (Topic==TEXT("clear")) Syntax=TEXT("clear\nErase scrollback; command history is preserved.");
    else if (Topic==TEXT("start_experiment")) Syntax=TEXT("start_experiment\nInitialize the current briefing on the next control tick; experiment time starts at zero.");
    else if (Topic==TEXT("repair_cam")) Syntax=TEXT("repair_cam room\nExample: repair_cam MED_BAY\nDiagnostic override restores ONLINE with ten seconds of tamper protection.");
    else if (Topic==TEXT("reset_experiment")) Syntax=TEXT("reset_experiment\nReset clock, subjects, rooms and incident log on the next control tick.");
    else if (Topic==TEXT("scenario")) Syntax=TEXT("scenario behavioral|medical\nStart a clean experiment in the selected scenario on the next control tick.");
    else if (Topic==TEXT("status")) Syntax=TEXT("status\nShow scenario, objective, shift clock and collective suspicion.");
    else if (Topic==TEXT("crew")) Syntax=TEXT("crew\nShow Compliance, Composure, Affinity, Utility, Suspicion, Stress, Integrity and room.");
    else if (Topic==TEXT("rooms")) Syntax=TEXT("rooms\nShow all compartment pressure states and alert clocks.");
    else if(Topic==TEXT("follow")) Syntax=TEXT("follow vale|chen|ortiz|ash|off\nFollow living crew across compartments in grid or fullscreen. Tile and dossier badges activate tracking; STOP FOLLOW stops it.");
    else if (Topic==TEXT("cam")) Syntax=TEXT("cam 1-8\n1 REACTOR_CORE, 2 BRIDGE, 3 MED_BAY, 4 OBSERVATION_DECK, 5 QUARTERS_MESS, 6 SERVER_ARCHIVE, 7 AIRLOCK_CHAMBER, 8 RING_CORRIDOR");
    else if (Topic==TEXT("menu")) Syntax=TEXT("menu ai | menu events | menu crew | menu doors | menu experiment | menu close\nTab categories; Up/Down highlight; Enter executes. Esc returns focus to camera navigation.");
    else if (Topic==TEXT("choose")) Syntax=TEXT("choose number\nUse a displayed choice from 1 through 9. Unavailable event choices do not execute.");
    else if (Topic==TEXT("direct")) Syntax=TEXT("direct crew room [pretext]\nExample: direct ash MED_BAY telemetry\nLow-compliance crew need a physical sensor trigger; rogue crew refuse.");
    else if (Topic==TEXT("alert")) Syntax=TEXT("alert room fault|leak|drill\nExample: alert MED_BAY fault\nA localized Ship AI sensor pretext lures nearby available crew.");
    else if (Topic==TEXT("calm")) Syntax=TEXT("calm\nBroadcast reassurance to all living human crew.");
    else if (Topic==TEXT("protocol")) Syntax=TEXT("protocol room\nExample: protocol BRIDGE\nDampen room occupants' Stress and Suspicion.");
    else if (Topic==TEXT("open") || Topic==TEXT("close") || Topic==TEXT("seal") || Topic==TEXT("unseal")) Syntax=Topic+TEXT(" door|room\nExamples: open MED_BAY; seal BRIDGE; close 0; seal all\nDoors 0-8 are valid; ordinary open cannot open the exterior hatch. Unexplained seals raise suspicion.");
    else if (Topic==TEXT("vent") || Topic==TEXT("scut")) Syntax=Topic+TEXT(" airlock\nPurges AIRLOCK_CHAMBER; newly exposed, unbreached human witnesses gain Suspicion.");
    else if (Topic==TEXT("flush")) Syntax=TEXT("flush room\nExample: flush AIRLOCK_CHAMBER\nOpens a stable shortest door path to vacuum. Connected rooms can also vent.");
    else return TEXT("Unknown reference topic. Type help to list public commands.");
    Syntax+=TEXT("\n\nCrew identifiers: vale / engineer; chen / medic; ortiz / technician; ash / android.\nRooms: REACTOR_CORE, BRIDGE, MED_BAY, OBSERVATION_DECK, QUARTERS_MESS, SERVER_ARCHIVE, AIRLOCK_CHAMBER, RING_CORRIDOR. Alias: airlock.");
    return Syntax;
}
TArray<FString> UDirectOrScutSimSubsystem::CompleteConsoleCommand(const FString& Input) const
{
    TArray<FString> Tokens; Input.ParseIntoArrayWS(Tokens);
    const bool NewToken=Input.IsEmpty() || FChar::IsWhitespace(Input[Input.Len()-1]);
    const int32 Index=NewToken?Tokens.Num():FMath::Max(0,Tokens.Num()-1);
    const FString Prefix=NewToken?TEXT(""):Tokens.Last();
    FString Base=Input.Left(Input.Len()-Prefix.Len());
    TArray<FString> Candidates;
    auto Rooms=[&](){for(int32 R=0;R<8;++R) Candidates.Add(DirectOrScut::RoomName(R));};
    if (Index==0 || (Index==1 && (Tokens[0].Equals(TEXT("help"),ESearchCase::IgnoreCase) || Tokens[0]==TEXT("?")))) for(const auto* V:PublicVerbs) Candidates.Add(V);
    else
    {
        const FString Verb=Tokens[0].ToLower();
        if(Verb==TEXT("follow") && Index==1) {for(int32 I=0;I<4;++I)Candidates.Add(CrewNames[I]);Candidates.Add(TEXT("off"));}
        else if(Verb==TEXT("scenario") && Index==1)Candidates={TEXT("behavioral"),TEXT("medical")};
        else if (Verb==TEXT("fx") && Index==1) Candidates={TEXT("clean"),TEXT("crt")};
        else if (Verb==TEXT("view") && Index==1) Candidates={TEXT("3d"),TEXT("crt")};
        else if (Verb==TEXT("menu") && Index==1) Candidates={TEXT("ai"),TEXT("events"),TEXT("crew"),TEXT("doors"),TEXT("experiment"),TEXT("close")};
        else if (Verb==TEXT("direct") && Index==1) { for(int32 I=0;I<4;++I) { Candidates.Add(CrewNames[I]);Candidates.Add(CrewRoles[I]); } }
        else if ((Verb==TEXT("direct") && Index==2) || ((Verb==TEXT("alert") || Verb==TEXT("protocol") || Verb==TEXT("flush") || Verb==TEXT("repair_cam")) && Index==1)) Rooms();
        else if (Verb==TEXT("alert") && Index==2) Candidates={TEXT("fault"),TEXT("leak"),TEXT("drill")};
        else if ((Verb==TEXT("open") || Verb==TEXT("close") || Verb==TEXT("seal") || Verb==TEXT("unseal")) && Index==1) { Rooms(); Candidates.Add(TEXT("all"));for(int32 I=0;I<9;++I) Candidates.Add(FString::FromInt(I)); }
        else if ((Verb==TEXT("vent") || Verb==TEXT("scut")) && Index==1) Candidates.Add(TEXT("airlock"));
        else if (Verb==TEXT("cam") && Index==1) for(int32 I=1;I<=8;++I) Candidates.Add(FString::FromInt(I));
    }
    TArray<FString> Result;
    for(const auto& Candidate:Candidates) if(Candidate.StartsWith(Prefix,ESearchCase::IgnoreCase)) Result.Add(Base+Candidate+TEXT(" "));
    return Result;
}
FString UDirectOrScutSimSubsystem::ScreenMenuTitle() const
{
    switch (ActiveMenu)
    {
    case EShipAIScreenMenu::DoorAction:return FString(TEXT("ROOM CONTROL // "))+DirectOrScut::RoomName(MenuRoom);
    case EShipAIScreenMenu::ConfirmScut:return FString(TEXT("CONFIRM SCUT // "))+DirectOrScut::RoomName(MenuRoom)+TEXT(" // CONNECTED ROOMS MAY VENT");
    case EShipAIScreenMenu::AndroidTask:return FString(TEXT("SELECT COVERT TASK // "))+DirectOrScut::RoomName(MenuRoom);
    case EShipAIScreenMenu::CameraRepair:return TEXT("CAMERA DIAGNOSTICS // REPAIR FEED");
    case EShipAIScreenMenu::Experiment:return TEXT("EXPERIMENT // LIFECYCLE CONTROL");
    case EShipAIScreenMenu::AI:return TEXT("AI DIRECTION MENU // SHIP AI OPERATIONS");
    case EShipAIScreenMenu::Events:return FString(TEXT("EVENT KICKOFF & SCENARIO MENU\n"))+Simulation.Scenario.ScenarioId;
    case EShipAIScreenMenu::Crew:return TEXT("CREW WEIGHTS MATRIX // LIVE TELEMETRY");
    case EShipAIScreenMenu::RouteCrew:return TEXT("ROUTE CREW // SELECT SPECIALIST");
    case EShipAIScreenMenu::RouteRoom:return FString(TEXT("ROUTE CREW // "))+CrewNames[MenuCrew]+TEXT(" // SELECT COMPARTMENT");
    case EShipAIScreenMenu::RoutePretext:return TEXT("ROUTE CREW // SELECT SHIP AI PRETEXT");
    case EShipAIScreenMenu::AlertRoom:return TEXT("SENSOR ALERT // SELECT COMPARTMENT");
    case EShipAIScreenMenu::AlertKind:return FString(TEXT("SENSOR ALERT // "))+DirectOrScut::RoomName(MenuRoom)+TEXT(" // SELECT FAULT TYPE");
    case EShipAIScreenMenu::Protocol:return TEXT("CORPORATE REASSURANCE // SELECT CHANNEL");
    case EShipAIScreenMenu::Containment:return TEXT("BULKHEAD & AIRLOCK CONTROL");
    case EShipAIScreenMenu::DoorRoom:return TEXT("BULKHEAD CONTROL // SELECT COMPARTMENT");
    case EShipAIScreenMenu::FlushRoom:return TEXT("ATMOSPHERIC FLUSH // SELECT COMPARTMENT");
    case EShipAIScreenMenu::AndroidRoom:return TEXT("SILENT ANDROID SUBROUTINE // SELECT COMPARTMENT");
    default:return TEXT("");
    }
}
TArray<FShipAIScreenChoice> UDirectOrScutSimSubsystem::ScreenMenuChoices() const
{
    TArray<FShipAIScreenChoice> C;
    auto Add=[&](const FString& Label,const FString& Detail=TEXT(""),bool Enabled=true){C.Add({Label,Detail,Enabled});};
    switch (ActiveMenu)
    {
    case EShipAIScreenMenu::CameraRepair:
        for(int32 R:RoomOrder)Add(DirectOrScut::RoomName(R),CameraStateName(CameraFeedState(R)),ExperimentPhase==EExperimentPhase::Active && CameraFeedState(R)!=ECameraFeedState::Online);break;
    case EShipAIScreenMenu::Experiment:
        Add(TEXT("New experiment"),TEXT("Reset the current scenario and all subject metrics"));
        Add(TEXT("Behavioral scenario"),TEXT("Start a clean behavioral experiment"));
        Add(TEXT("Medical scenario"),TEXT("Start a clean medical experiment"));
        Add(TEXT("Initialize experiment"),TEXT("Accept the briefing and start the 600-second clock"),ExperimentPhase==EExperimentPhase::Briefing);break;
    case EShipAIScreenMenu::AI:
        Add(TEXT("Route Crew / Pretext"),TEXT("Select a specialist, compartment and diagnostic cover."));
        Add(TEXT("Trigger Sensor Alert"),TEXT("Select a localized fault, leak or drill pretext."));
        Add(TEXT("Broadcast Protocol / Calm"),TEXT("Reassure the whole crew or a compartment."));
        Add(TEXT("Bulkhead & Airlock Control"),TEXT("Open, seal or deliberately purge compartments."));
        Add(TEXT("Silent Android Subroutine"),TEXT("Route Ash under silent telemetry cover."));
        Add(TEXT("Repair surveillance camera"),TEXT("Restore a tampered feed using a diagnostic override"));break;
    case EShipAIScreenMenu::RouteCrew:
        for(int32 I=0;I<4;++I) { const auto& U=Simulation.Bodies[I];Add(FString::Printf(TEXT("%s / %s"),CrewNames[I],CrewRoles[I]),DirectOrScut::RoomName(U.Room),U.bRegistered && U.Integrity>0 && U.State!=EDirectOrScutUnitState::Ejected); }break;
    case EShipAIScreenMenu::RouteRoom:case EShipAIScreenMenu::AlertRoom:case EShipAIScreenMenu::DoorRoom:case EShipAIScreenMenu::FlushRoom:case EShipAIScreenMenu::AndroidRoom:
        for(int32 I=0;I<8;++I) {const int32 R=RoomOrder[I];Add(DirectOrScut::RoomName(R),Simulation.Rooms[R].bIsPressurized?TEXT("PRESSURIZED"):TEXT("VACUUM"));}break;
    case EShipAIScreenMenu::DoorAction:Add(IsRoomSealed(MenuRoom)?TEXT("UNSEAL"):TEXT("SEAL"));Add(TEXT("SCUT"),TEXT("Review atmospheric purge confirmation"));break;
    case EShipAIScreenMenu::ConfirmScut:Add(TEXT("CONFIRM SCUT"),TEXT("Opens selected room path to vacuum"));break;
    case EShipAIScreenMenu::AndroidTask:Add(TEXT("Shadow Crew"));Add(TEXT("Scrub Logs"));Add(TEXT("Override Door"));break;
    case EShipAIScreenMenu::RoutePretext:Add(TEXT("Diagnostic telemetry"));Add(TEXT("Scheduled maintenance"));Add(TEXT("Unprompted routing"),TEXT("Hazardous orders can raise suspicion."));break;
    case EShipAIScreenMenu::AlertKind:Add(TEXT("Thermal"));Add(TEXT("Telemetry"));Add(TEXT("Pressure"));break;
    case EShipAIScreenMenu::Protocol:Add(TEXT("All crew / calm"));for(int32 R:RoomOrder) Add(DirectOrScut::RoomName(R));break;
    case EShipAIScreenMenu::Containment:Add(TEXT("Open compartment bulkheads"));Add(TEXT("Seal compartment bulkheads"));Add(TEXT("Seal all"));Add(TEXT("Cycle airlock / SCUT"),TEXT("Purge AIRLOCK_CHAMBER; connected open rooms can vent."));Add(TEXT("Flush compartment"),TEXT("Opens a path to vacuum; affects connected rooms."));break;
    case EShipAIScreenMenu::Crew:
        for(int32 I=0;I<4;++I) {
            const auto& U=Simulation.Bodies[I];const auto& P=U.Profile;
            Add(FString::Printf(TEXT("FOLLOW %s"),CrewNames[I]),FString::Printf(TEXT("%s / %s\nCompliance %.0f%% | Composure %.0f%% | Suspicion %.0f%%\nStress %.0f | Integrity %.0f | %.0f BPM | %.1f mm"),DirectOrScut::RoomName(U.Room),DirectOrScutStateName(U.State),P.Compliance*100,P.Composure*100,P.Suspicion*100,U.Stress,U.Integrity,P.HeartRateBPM,P.PupilDilationMM),CanFollowCrew(I));
        }break;
    case EShipAIScreenMenu::Events:
    {
        bool Pending=false;
        for(const auto& B:BeatDeck)if(B.bFired && !B.bResolved)
        {
            for(const auto& Choice:B.Node.Choices)Add(B.Node.Subtitle+TEXT(" / ")+Choice.Label,FString::Printf(TEXT("STRESS %+.0f | SUS %+.0f%% | COMP %+.0f%% | AFF %+.0f%%"),Choice.Delta.Stress,Choice.Delta.Suspicion*100,Choice.Delta.Compliance*100,Choice.Delta.Affinity*100));
            Pending=true;break;
        }
        if(!Pending)for(const auto& O:DirectorOptions)Add(O.Label,FString::Printf(TEXT("%s | STRESS %+.0f | SUS %+.0f%% | COMP %+.0f%% | AFF %+.0f%% | UTIL %+.0f%%"),O.bSupport?TEXT("Autonomy / Support"):TEXT("Containment / Control"),O.Choice.Delta.Stress,O.Choice.Delta.Suspicion*100,O.Choice.Delta.Compliance*100,O.Choice.Delta.Affinity*100,O.Choice.Delta.Utility*100));
        break;
    }
    default:break;
    }
    if(ExperimentPhase==EExperimentPhase::Briefing && ActiveMenu!=EShipAIScreenMenu::Experiment)for(auto& Choice:C)Choice.bEnabled=false;
    return C;
}
void UDirectOrScutSimSubsystem::ExecuteScreenDirective(const FString& Command)
{
    LogEvent(TEXT("Ship AI menu directive: ")+Command);
    LogEvent(SubmitDirective(Command));
    ProcessSimulationDirectives();
    MenuParents.Reset(); ActiveMenu=EShipAIScreenMenu::None; SetScreenMenu(EShipAIScreenMenu::AI);
}
void UDirectOrScutSimSubsystem::SelectScreenChoice(int32 N)
{
    const auto Choices=ScreenMenuChoices();
    if(N<1 || N>Choices.Num() || !Choices[N-1].bEnabled) { LogEvent(TEXT("That screen choice is unavailable."));return; }
    switch(ActiveMenu)
    {
    case EShipAIScreenMenu::CameraRepair:RepairCamera(RoomOrder[N-1]);break;
    case EShipAIScreenMenu::Experiment:
        if(N==4)StartExperiment();else if(N==1)ResetExperiment();else SwitchScenario(N==3);break;
    case EShipAIScreenMenu::AI:
    {
        const EShipAIScreenMenu Pages[]={EShipAIScreenMenu::RouteCrew,EShipAIScreenMenu::AlertRoom,EShipAIScreenMenu::Protocol,EShipAIScreenMenu::DoorRoom,EShipAIScreenMenu::AndroidRoom};if(N==6){SetScreenMenu(EShipAIScreenMenu::CameraRepair);break;}if(N==3) { const float Before=Simulation.CollectiveSuspicion(); ExecuteScreenDirective(TEXT("calm")); LogEvent(FString::Printf(TEXT("CALM: suspicion %+.1f%%; compliance unchanged"),(Simulation.CollectiveSuspicion()-Before)*100)); } else SetScreenMenu(Pages[N-1]);break;
    }
    case EShipAIScreenMenu::RouteCrew:MenuCrew=N-1;SetScreenMenu(EShipAIScreenMenu::RouteRoom);break;
    case EShipAIScreenMenu::RouteRoom:MenuRoom=RoomOrder[N-1];ExecuteScreenDirective(FString::Printf(TEXT("direct %s %s diagnostic_telemetry"),CrewNames[MenuCrew],DirectOrScut::RoomName(MenuRoom)));break;
    case EShipAIScreenMenu::RoutePretext:ExecuteScreenDirective(FString::Printf(TEXT("direct %s %s%s"),CrewNames[MenuCrew],DirectOrScut::RoomName(MenuRoom),N==1?TEXT(" diagnostic_telemetry"):N==2?TEXT(" scheduled_maintenance"):TEXT("")));break;
    case EShipAIScreenMenu::AlertRoom:MenuRoom=RoomOrder[N-1];SetScreenMenu(EShipAIScreenMenu::AlertKind);break;
    case EShipAIScreenMenu::AlertKind:ExecuteScreenDirective(FString::Printf(TEXT("alert %s %s"),DirectOrScut::RoomName(MenuRoom),N==1?TEXT("fault"):N==2?TEXT("drill"):TEXT("leak")));break;
    case EShipAIScreenMenu::Protocol:ExecuteScreenDirective(N==1?TEXT("calm"):FString(TEXT("protocol "))+DirectOrScut::RoomName(RoomOrder[N-2]));break;
    case EShipAIScreenMenu::Containment:
        if(N<=2) {MenuVerb=N==1?TEXT("open"):TEXT("seal");SetScreenMenu(EShipAIScreenMenu::DoorRoom);}
        else if(N==3) ExecuteScreenDirective(TEXT("seal all"));else if(N==4) ExecuteScreenDirective(TEXT("scut airlock"));else SetScreenMenu(EShipAIScreenMenu::FlushRoom);break;
    case EShipAIScreenMenu::DoorRoom:MenuRoom=RoomOrder[N-1];SetScreenMenu(EShipAIScreenMenu::DoorAction);break;
    case EShipAIScreenMenu::DoorAction:if(N==1) ExecuteScreenDirective(FString(IsRoomSealed(MenuRoom)?TEXT("open "):TEXT("seal "))+DirectOrScut::RoomName(MenuRoom));else SetScreenMenu(EShipAIScreenMenu::ConfirmScut);break;
    case EShipAIScreenMenu::ConfirmScut:ExecuteScreenDirective(FString(TEXT("flush "))+DirectOrScut::RoomName(MenuRoom));break;
    case EShipAIScreenMenu::FlushRoom:ExecuteScreenDirective(FString(TEXT("flush "))+DirectOrScut::RoomName(RoomOrder[N-1]));break;
    case EShipAIScreenMenu::AndroidRoom:MenuRoom=RoomOrder[N-1];SetScreenMenu(EShipAIScreenMenu::AndroidTask);break;
    case EShipAIScreenMenu::AndroidTask:
        if(N==1) ExecuteScreenDirective(FString(TEXT("direct ash "))+DirectOrScut::RoomName(MenuRoom)+TEXT(" shadow_crew"));
        else if(N==2) { for(auto& U:Simulation.Bodies) if(U.bRegistered && U.Room==MenuRoom && !U.bAndroid) U.Profile.Suspicion=FMath::Max(0.f,U.Profile.Suspicion-.05f); LogEvent(TEXT("Ash scrubbed room logs: up to -5% SUS for occupants"),MenuRoom);MenuParents.Reset();ActiveMenu=EShipAIScreenMenu::None;SetScreenMenu(EShipAIScreenMenu::AI); }
        else ExecuteScreenDirective(FString(TEXT("open "))+DirectOrScut::RoomName(MenuRoom));break;
    case EShipAIScreenMenu::Crew: { SetFollowedCrew(N-1);const int32 R=Simulation.Bodies[N-1].Room;for(int32 I=0;I<8;++I)if(RoomOrder[I]==R)RequestedFeed=I+1;++MenuRevision;break; }
    case EShipAIScreenMenu::Events:ExecuteScenarioEvent(N);break;
    default:break;
    }
}
void UDirectOrScutSimSubsystem::ExecuteScenarioEvent(int32 Choice)
{
    for(int32 I=0;I<BeatDeck.Num();++I)if(BeatDeck[I].bFired && !BeatDeck[I].bResolved) {ResolveBeat(I,Choice-1);return;}
    ExecuteDirectorOption(Choice-1);++MenuRevision;
}
