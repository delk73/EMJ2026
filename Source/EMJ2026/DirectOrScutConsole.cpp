#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutGeometry.h"

namespace
{
const int32 RoomOrder[]={0,2,3,4,5,6,7,1};
const TCHAR* CrewNames[]={TEXT("vale"),TEXT("chen"),TEXT("ortiz"),TEXT("ash")};
const TCHAR* CrewRoles[]={TEXT("engineer"),TEXT("medic"),TEXT("technician"),TEXT("android")};
const TCHAR* PublicVerbs[]={TEXT("help"),TEXT("?"),TEXT("clear"),TEXT("status"),TEXT("crew"),TEXT("rooms"),TEXT("cam"),TEXT("menu"),TEXT("choose"),TEXT("direct"),TEXT("alert"),TEXT("calm"),TEXT("protocol"),TEXT("open"),TEXT("close"),TEXT("seal"),TEXT("vent"),TEXT("scut"),TEXT("flush")};
bool LivingHuman(const FDirectOrScutUnitState& U) { return U.bRegistered && !U.bCargo && !U.bAndroid && U.Integrity>0 && U.State!=EDirectOrScutUnitState::Ejected; }
struct FEventPlan
{
    uint8 Affected=0;
    int32 Room=-1;
    FCrewProfile Profiles[4];
    double Stress[4]{};
    float SuspicionDelta=0,StressDelta=0,ComplianceDelta=0,AffinityDelta=0,UtilityDelta=0;
};
// Both UI previews and fixed-tick execution use this pure plan, including eligibility and clamps.
FEventPlan PlanEvent(const FDirectOrScutSimulation& M,int32 Choice)
{
    FEventPlan P;
    if (Choice==1) P.Room=3;
    if (Choice==3)
        for (int32 R=0;R<8 && P.Room<0;++R)
        {
            int32 Count=0; for (int32 I=0;I<4;++I) Count+=LivingHuman(M.Bodies[I]) && M.Bodies[I].Room==R?1:0;
            if (Count>=2) P.Room=R;
        }
    for (int32 I=0;I<4;++I)
    {
        const auto& U=M.Bodies[I]; P.Profiles[I]=U.Profile; P.Stress[I]=U.Stress;
        if (!LivingHuman(U)) continue;
        const bool Affects=Choice==2 || (Choice==1 && U.Room==3) || (Choice==3 && P.Room>=0 && U.Room==P.Room) || (Choice==4 && P.Affected==0);
        if (!Affects) continue;
        P.Affected|=1<<I; auto& W=P.Profiles[I]; auto& S=P.Stress[I];
        if (Choice==1) { S+=6*(1-W.Composure); W.Compliance+=.03f; W.Suspicion-=.025f; }
        if (Choice==2) { S-=18; W.Suspicion-=.08f; }
        if (Choice==3) { S-=6; W.Affinity+=.1f; W.Suspicion-=.02f; }
        if (Choice==4) { P.Room=U.Room; S+=4; W.Utility+=.05f; W.Compliance+=.03f; W.Suspicion+=.01f; }
        W.Compliance=FMath::Clamp(W.Compliance,0.f,1.f); W.Affinity=FMath::Clamp(W.Affinity,-1.f,1.f);
        W.Utility=FMath::Clamp(W.Utility,0.f,1.f); W.Suspicion=FMath::Clamp(W.Suspicion,0.f,1.f); S=FMath::Clamp(S,0.,100.);
        P.SuspicionDelta+=W.Suspicion-U.Profile.Suspicion; P.StressDelta+=float(S-U.Stress);
        P.ComplianceDelta+=W.Compliance-U.Profile.Compliance; P.AffinityDelta+=W.Affinity-U.Profile.Affinity; P.UtilityDelta+=W.Utility-U.Profile.Utility;
    }
    return P;
}
}
bool UDirectOrScutSimSubsystem::QueueConsoleCommand(const FString& Input)
{
    const FString Line=Input.TrimStartAndEnd();
    if (Line.IsEmpty() || PendingConsoleCommands.Num()>=32 || Line.Len()>1024) return false;
    PendingConsoleCommands.Add(Line); return true;
}
void UDirectOrScutSimSubsystem::SetScreenMenu(EShipAIScreenMenu Menu) { ActiveMenu=Menu; ++MenuRevision; }
void UDirectOrScutSimSubsystem::ProcessConsoleCommands()
{
    TArray<FString> Commands=MoveTemp(PendingConsoleCommands); PendingConsoleCommands.Reset();
    for (const FString& Input:Commands)
    {
        const FString Line=Input.ToLower(); TArray<FString> T; Line.ParseIntoArrayWS(T); if (T.IsEmpty()) continue;
        if (Line==TEXT("clear")) { Events.Reset(); ++EventRevision; continue; }
        LogEvent(TEXT("CMD> ")+Input);
        if (T[0]==TEXT("help") || T[0]==TEXT("?")) { LogEvent(HelpReference(T.Num()>1?T[1]:TEXT(""))); continue; }
        if (T[0]==TEXT("menu") && T.Num()==2)
        {
            if (T[1]==TEXT("ai")) SetScreenMenu(EShipAIScreenMenu::AI);
            else if (T[1]==TEXT("events")) SetScreenMenu(EShipAIScreenMenu::Events);
            else if (T[1]==TEXT("crew")) SetScreenMenu(EShipAIScreenMenu::Crew);
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
        "CAMERAS\ncam 1-8 : select compartment feed\n1-8 select; F / double tap / double click inspect; G / Tab / Esc grid\n\n"
        "MENUS\nmenu ai : numbered Ship AI operations\nmenu events : scenario events and live stat-delta previews\nmenu crew : live crew weights matrix\nmenu close : dismiss screen menu\nchoose 1-9 : execute a numbered screen choice\n\n"
        "DIRECTIVES\ndirect : route a named specialist under a maintenance pretext\nalert : localized fault, leak or drill sensor pretext\ncalm : reassure all human crew\nprotocol : reassure a compartment\n\n"
        "CONTAINMENT\nopen / close / seal : compartment bulkheads or door 0-8\nseal all : seal every bulkhead and exterior hatch\nvent / scut airlock : explicit atmospheric purge\nflush : open a path from a compartment to vacuum\n\n"
        "CONSOLE\nBacktick opens/retracts; Shift+backtick toggles 55% / 90% height\nEnter / slash opens prompt; Esc retracts\nUp / Down history; Tab cycles verb, crew and room completions\nAll console commands and menu choices execute on the next 30 Hz tick.");
    FString Syntax;
    if (Topic==TEXT("help")) Syntax=TEXT("help | ? | help command\nExample: help direct");
    else if (Topic==TEXT("clear")) Syntax=TEXT("clear\nErase scrollback; command history is preserved.");
    else if (Topic==TEXT("status")) Syntax=TEXT("status\nShow scenario, objective, shift clock and collective suspicion.");
    else if (Topic==TEXT("crew")) Syntax=TEXT("crew\nShow Compliance, Composure, Affinity, Utility, Suspicion, Stress, Integrity and room.");
    else if (Topic==TEXT("rooms")) Syntax=TEXT("rooms\nShow all compartment pressure states and alert clocks.");
    else if (Topic==TEXT("cam")) Syntax=TEXT("cam 1-8\n1 REACTOR_CORE, 2 BRIDGE, 3 MED_BAY, 4 ORE_CRUSHER, 5 SMELTER_FOUNDRY, 6 DOCKING_BAY, 7 AIRLOCK_CHAMBER, 8 RING_CORRIDOR");
    else if (Topic==TEXT("menu")) Syntax=TEXT("menu ai | menu events | menu crew | menu close\nUse number keys or click a choice. Esc closes the overlay.");
    else if (Topic==TEXT("choose")) Syntax=TEXT("choose number\nUse a displayed choice from 1 through 9. Unavailable event choices do not execute.");
    else if (Topic==TEXT("direct")) Syntax=TEXT("direct crew room [pretext]\nExample: direct ash MED_BAY telemetry\nLow-compliance crew need a physical sensor trigger; rogue crew refuse.");
    else if (Topic==TEXT("alert")) Syntax=TEXT("alert room fault|leak|drill\nExample: alert MED_BAY fault\nA localized Ship AI sensor pretext lures nearby available crew.");
    else if (Topic==TEXT("calm")) Syntax=TEXT("calm\nBroadcast reassurance to all living human crew.");
    else if (Topic==TEXT("protocol")) Syntax=TEXT("protocol room\nExample: protocol BRIDGE\nDampen room occupants' Stress and Suspicion.");
    else if (Topic==TEXT("open") || Topic==TEXT("close") || Topic==TEXT("seal")) Syntax=Topic+TEXT(" door|room\nExamples: open MED_BAY; seal BRIDGE; close 0; seal all\nDoors 0-8 are valid; ordinary open cannot open the exterior hatch. Unexplained seals raise suspicion.");
    else if (Topic==TEXT("vent") || Topic==TEXT("scut")) Syntax=Topic+TEXT(" airlock\nPurges AIRLOCK_CHAMBER; newly exposed, unbreached human witnesses gain Suspicion.");
    else if (Topic==TEXT("flush")) Syntax=TEXT("flush room\nExample: flush AIRLOCK_CHAMBER\nOpens a stable shortest door path to vacuum. Connected rooms can also vent.");
    else return TEXT("Unknown reference topic. Type help to list public commands.");
    Syntax+=TEXT("\n\nCrew identifiers: vale / engineer; chen / medic; ortiz / technician; ash / android.\nRooms: REACTOR_CORE, BRIDGE, MED_BAY, ORE_CRUSHER, SMELTER_FOUNDRY, DOCKING_BAY, AIRLOCK_CHAMBER, RING_CORRIDOR. Alias: airlock.");
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
        if (Verb==TEXT("menu") && Index==1) Candidates={TEXT("ai"),TEXT("events"),TEXT("crew"),TEXT("close")};
        else if (Verb==TEXT("direct") && Index==1) { for(int32 I=0;I<4;++I) { Candidates.Add(CrewNames[I]);Candidates.Add(CrewRoles[I]); } }
        else if ((Verb==TEXT("direct") && Index==2) || ((Verb==TEXT("alert") || Verb==TEXT("protocol") || Verb==TEXT("flush")) && Index==1)) Rooms();
        else if (Verb==TEXT("alert") && Index==2) Candidates={TEXT("fault"),TEXT("leak"),TEXT("drill")};
        else if ((Verb==TEXT("open") || Verb==TEXT("close") || Verb==TEXT("seal")) && Index==1) { Rooms(); Candidates.Add(TEXT("all"));for(int32 I=0;I<9;++I) Candidates.Add(FString::FromInt(I)); }
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
    case EShipAIScreenMenu::AI:return TEXT("AI DIRECTION MENU // SHIP AI OPERATIONS");
    case EShipAIScreenMenu::Events:return FString(TEXT("EVENT KICKOFF & SCENARIO MENU\n"))+Simulation.Scenario.ScenarioId;
    case EShipAIScreenMenu::Crew:return TEXT("CREW WEIGHTS MATRIX // LIVE TELEMETRY");
    case EShipAIScreenMenu::RouteCrew:return TEXT("ROUTE CREW // SELECT SPECIALIST");
    case EShipAIScreenMenu::RouteRoom:return TEXT("ROUTE CREW // SELECT COMPARTMENT");
    case EShipAIScreenMenu::RoutePretext:return TEXT("ROUTE CREW // SELECT SHIP AI PRETEXT");
    case EShipAIScreenMenu::AlertRoom:return TEXT("SENSOR ALERT // SELECT COMPARTMENT");
    case EShipAIScreenMenu::AlertKind:return TEXT("SENSOR ALERT // SELECT PRETEXT");
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
    case EShipAIScreenMenu::AI:
        Add(TEXT("Route Crew / Pretext"),TEXT("Select a specialist, compartment and diagnostic cover."));
        Add(TEXT("Trigger Sensor Alert"),TEXT("Select a localized fault, leak or drill pretext."));
        Add(TEXT("Broadcast Protocol / Calm"),TEXT("Reassure the whole crew or a compartment."));
        Add(TEXT("Bulkhead & Airlock Control"),TEXT("Open, seal or deliberately purge compartments."));
        Add(TEXT("Silent Android Subroutine"),TEXT("Route Ash under silent telemetry cover."));break;
    case EShipAIScreenMenu::RouteCrew:
        for(int32 I=0;I<4;++I) { const auto& U=Simulation.Bodies[I];Add(FString::Printf(TEXT("%s / %s"),CrewNames[I],CrewRoles[I]),DirectOrScut::RoomName(U.Room),U.bRegistered && U.Integrity>0 && U.State!=EDirectOrScutUnitState::Ejected); }break;
    case EShipAIScreenMenu::RouteRoom:case EShipAIScreenMenu::AlertRoom:case EShipAIScreenMenu::DoorRoom:case EShipAIScreenMenu::FlushRoom:case EShipAIScreenMenu::AndroidRoom:
        for(int32 I=0;I<8;++I) {const int32 R=RoomOrder[I];Add(DirectOrScut::RoomName(R),Simulation.Rooms[R].bIsPressurized?TEXT("PRESSURIZED"):TEXT("VACUUM"));}break;
    case EShipAIScreenMenu::RoutePretext:Add(TEXT("Diagnostic telemetry"));Add(TEXT("Scheduled maintenance"));Add(TEXT("Unprompted routing"),TEXT("Hazardous orders can raise suspicion."));break;
    case EShipAIScreenMenu::AlertKind:Add(TEXT("Fault"));Add(TEXT("Leak sensor"));Add(TEXT("Drill"));break;
    case EShipAIScreenMenu::Protocol:Add(TEXT("All crew / calm"));for(int32 R:RoomOrder) Add(DirectOrScut::RoomName(R));break;
    case EShipAIScreenMenu::Containment:Add(TEXT("Open compartment bulkheads"));Add(TEXT("Seal compartment bulkheads"));Add(TEXT("Seal all"));Add(TEXT("Cycle airlock / SCUT"),TEXT("Purge AIRLOCK_CHAMBER; connected open rooms can vent."));Add(TEXT("Flush compartment"),TEXT("Opens a path to vacuum; affects connected rooms."));break;
    case EShipAIScreenMenu::Events:
    {
        const TCHAR* Names[]={TEXT("Telemetry sensor drill / MED_BAY"),TEXT("Corporate reassurance protocol"),TEXT("Emergent roommate reconciliation"),TEXT("Crew systems rehearsal")};
        for(int32 I=1;I<=4;++I)
        {
            const auto P=PlanEvent(Simulation,I);
            FString Detail=FString::Printf(TEXT("Immediate totals: Suspicion %+.3f | Stress %+.1f | Compliance %+.3f | Affinity %+.3f | Utility %+.3f"),P.SuspicionDelta,P.StressDelta,P.ComplianceDelta,P.AffinityDelta,P.UtilityDelta);
            FString Targets;
            for(int32 J=0;J<4;++J) if(P.Affected&(1<<J)) {if(!Targets.IsEmpty())Targets+=TEXT(", ");Targets+=CrewNames[J];}
            Detail+=Targets.IsEmpty()?TEXT("\nAffected crew: none at current telemetry."):TEXT("\nAffected crew: ")+Targets;
            if(I==1) Detail+=FString::Printf(TEXT("\nMED_BAY alert: %.1fs; scheduled anomalies remaining: "),Simulation.Rooms[3].AlertSeconds);
            if(I==1) { int32 Remaining=0;for(const auto& E:Simulation.Scenario.Events) Remaining+=E.Tick>=Simulation.TickIndex?1:0;Detail+=FString::FromInt(Remaining); }
            if(I==3) Detail+=P.Room>=0?FString(TEXT("\nShared room: "))+DirectOrScut::RoomName(P.Room):TEXT("\nUnavailable: no living human roommates share a compartment.");
            Add(Names[I-1],Detail,I==1 || P.Affected!=0);
        }
        break;
    }
    default:break;
    }
    return C;
}
void UDirectOrScutSimSubsystem::ExecuteScreenDirective(const FString& Command)
{
    LogEvent(TEXT("Ship AI menu directive: ")+Command);
    LogEvent(SubmitDirective(Command));
    ProcessSimulationDirectives();
    SetScreenMenu(EShipAIScreenMenu::AI);
}
void UDirectOrScutSimSubsystem::SelectScreenChoice(int32 N)
{
    const auto Choices=ScreenMenuChoices();
    if(N<1 || N>Choices.Num() || !Choices[N-1].bEnabled) { LogEvent(TEXT("That screen choice is unavailable."));return; }
    switch(ActiveMenu)
    {
    case EShipAIScreenMenu::AI:
    {
        const EShipAIScreenMenu Pages[]={EShipAIScreenMenu::RouteCrew,EShipAIScreenMenu::AlertRoom,EShipAIScreenMenu::Protocol,EShipAIScreenMenu::Containment,EShipAIScreenMenu::AndroidRoom};SetScreenMenu(Pages[N-1]);break;
    }
    case EShipAIScreenMenu::RouteCrew:MenuCrew=N-1;SetScreenMenu(EShipAIScreenMenu::RouteRoom);break;
    case EShipAIScreenMenu::RouteRoom:MenuRoom=RoomOrder[N-1];SetScreenMenu(EShipAIScreenMenu::RoutePretext);break;
    case EShipAIScreenMenu::RoutePretext:ExecuteScreenDirective(FString::Printf(TEXT("direct %s %s%s"),CrewNames[MenuCrew],DirectOrScut::RoomName(MenuRoom),N==1?TEXT(" diagnostic_telemetry"):N==2?TEXT(" scheduled_maintenance"):TEXT("")));break;
    case EShipAIScreenMenu::AlertRoom:MenuRoom=RoomOrder[N-1];SetScreenMenu(EShipAIScreenMenu::AlertKind);break;
    case EShipAIScreenMenu::AlertKind:ExecuteScreenDirective(FString::Printf(TEXT("alert %s %s"),DirectOrScut::RoomName(MenuRoom),N==1?TEXT("fault"):N==2?TEXT("leak"):TEXT("drill")));break;
    case EShipAIScreenMenu::Protocol:ExecuteScreenDirective(N==1?TEXT("calm"):FString(TEXT("protocol "))+DirectOrScut::RoomName(RoomOrder[N-2]));break;
    case EShipAIScreenMenu::Containment:
        if(N<=2) {MenuVerb=N==1?TEXT("open"):TEXT("seal");SetScreenMenu(EShipAIScreenMenu::DoorRoom);}
        else if(N==3) ExecuteScreenDirective(TEXT("seal all"));else if(N==4) ExecuteScreenDirective(TEXT("scut airlock"));else SetScreenMenu(EShipAIScreenMenu::FlushRoom);break;
    case EShipAIScreenMenu::DoorRoom:ExecuteScreenDirective(MenuVerb+TEXT(" ")+DirectOrScut::RoomName(RoomOrder[N-1]));break;
    case EShipAIScreenMenu::FlushRoom:ExecuteScreenDirective(FString(TEXT("flush "))+DirectOrScut::RoomName(RoomOrder[N-1]));break;
    case EShipAIScreenMenu::AndroidRoom:ExecuteScreenDirective(FString(TEXT("direct ash "))+DirectOrScut::RoomName(RoomOrder[N-1])+TEXT(" silent_telemetry"));break;
    case EShipAIScreenMenu::Events:ExecuteScenarioEvent(N);break;
    default:break;
    }
}
void UDirectOrScutSimSubsystem::ExecuteScenarioEvent(int32 Choice)
{
    const auto P=PlanEvent(Simulation,Choice);
    if(Choice!=1 && !P.Affected) {LogEvent(TEXT("No eligible crew interaction for that event."));return;}
    if(Choice==1) Simulation.TriggerPretextAlert(3);
    for(int32 I=0;I<4;++I) if(P.Affected&(1<<I)) {Simulation.Bodies[I].Profile=P.Profiles[I];Simulation.Bodies[I].Stress=P.Stress[I];}
    LogEvent(FString::Printf(TEXT("Scenario event %d executed: Suspicion %+.3f Stress %+.1f Compliance %+.3f Affinity %+.3f Utility %+.3f. Ongoing simulation continues."),Choice,P.SuspicionDelta,P.StressDelta,P.ComplianceDelta,P.AffinityDelta,P.UtilityDelta),P.Room);
    ++MenuRevision;
}
