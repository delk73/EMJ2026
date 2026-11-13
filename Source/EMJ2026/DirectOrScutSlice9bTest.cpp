#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutHUD.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

class FSlice9bRoutedConsole : public IAutomationLatentCommand
{
public:
    explicit FSlice9bRoutedConsole(FAutomationTestBase* T):Test(T) {}
    static TSharedPtr<SWidget> Find(const TSharedRef<SWidget>& W,FName Tag)
    {
        if(W->GetTag()==Tag) return W;
        auto* Children=W->GetChildren();for(int32 I=0;I<Children->Num();++I) if(auto Found=Find(Children->GetChildAt(I),Tag)) return Found;
        return nullptr;
    }
    bool Update() override
    {
        auto& App=FSlateApplication::Get();const double Now=FPlatformTime::Seconds();if(!Started)Started=Now;
        if(Now-Started>90){Test->AddError(TEXT("Slice 9b input timed out"));return true;}
        UWorld* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W || !W->HasBegunPlay())return false;
        auto* PC=W->GetFirstPlayerController();auto* HUD=PC?Cast<ADirectOrScutHUD>(PC->GetHUD()):nullptr;
        if(!HUD || !HUD->GetWorkstation())return false;
        auto UI=HUD->GetWorkstation();auto* S=W->GetSubsystem<UDirectOrScutSimSubsystem>();
        if(!S || S->Simulation.TickIndex<30 || UI->GetCachedGeometry().GetLocalSize().X<100 || Now-Last<.23)return false;
        Last=Now;
        auto P=Find(UI->GetContentRoot(),TEXT("ScutPrompt"));if(!P){Test->AddError(TEXT("Console prompt missing"));return true;}
        auto Prompt=StaticCastSharedPtr<SEditableTextBox>(P);
        auto Key=[&](FKey K,bool Shift=false){const FModifierKeysState Mod(Shift,false,false,false,false,false,false,false,false);App.ProcessKeyDownEvent(FKeyEvent(K,Mod,0,false,0,0));App.ProcessKeyUpEvent(FKeyEvent(K,Mod,0,false,0,0));};
        auto Type=[&](const TCHAR* Text){for(const TCHAR* C=Text;*C;++C){const FKey K(FName(*FString::Chr(FChar::ToUpper(*C))));App.ProcessKeyDownEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,*C));App.ProcessKeyCharEvent(FCharacterEvent(*C,FModifierKeysState(),0,false));App.ProcessKeyUpEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,*C));}};
        auto Command=[&](const TCHAR* Text){Type(Text);Key(EKeys::Enter);};
        auto Click=[&](int32 N)
        {
            auto Button=Find(UI->GetContentRoot(),FName(*FString::Printf(TEXT("ScutMenuChoice%d"),N)));
            Test->TestTrue(TEXT("Menu mouse target exists"),Button.IsValid());if(!Button)return;
            const auto& G=Button->GetCachedGeometry();const FVector2D Pos=G.LocalToAbsolute(G.GetLocalSize()*.5f);auto Window=App.FindWidgetWindow(Button.ToSharedRef());if(!Window){Test->AddError(TEXT("Menu window missing"));return;}
            App.ProcessMouseMoveEvent(FPointerEvent(0,Pos,Pos-FVector2D(1,0),TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()));
            App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),FPointerEvent(0,Pos,Pos,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
            App.ProcessMouseButtonUpEvent(FPointerEvent(0,Pos,Pos,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));
        };
        auto Menu=[&](EShipAIScreenMenu Expected){Test->TestEqual(TEXT("Fixed-tick screen menu page"),int32(S->ActiveMenu),int32(Expected));};
        switch(Stage)
        {
        case 0:
        {
            Test->TestTrue(TEXT("Grid starts focused and drawer closed"),UI->HasKeyboardFocus() && UI->bGrid && !UI->bConsoleOpen);
            auto Tile=Find(UI->GetContentRoot(),TEXT("ScutCamera8"));Test->TestTrue(TEXT("Eight cameras present"),Tile.IsValid());
            if(Tile){const auto& T=Tile->GetCachedGeometry();const auto& R=UI->GetContentRoot()->GetCachedGeometry();const double Right=T.LocalToAbsolute(T.GetLocalSize()).X;Test->TestTrue(TEXT("CCTV grid fills width without side console"),Right>R.LocalToAbsolute(FVector2D(R.GetLocalSize().X*.9f,0)).X);}
            Key(EKeys::Tilde);App.ProcessKeyCharEvent(FCharacterEvent('`',FModifierKeysState(),0,false));break;
        }
        case 1:
        {
            Test->TestTrue(TEXT("Backtick immediately focuses drawer"),UI->bConsoleOpen && UI->IsPromptFocused());Test->TestTrue(TEXT("Backtick not inserted"),Prompt->GetText().IsEmpty());
            const float H=float(Find(UI->GetContentRoot(),TEXT("ScutConsoleDrawer"))->GetCachedGeometry().GetLocalSize().Y);const float RootH=float(UI->GetContentRoot()->GetCachedGeometry().GetLocalSize().Y);
            Test->TestTrue(TEXT("Default drawer is approximately 55 percent"),H>RootH*.4f && H<RootH*.6f);
            const uint64 Before=S->EventRevision;Command(TEXT("status"));Test->TestEqual(TEXT("Console status waits for fixed tick"),S->EventRevision,Before);break;
        }
        case 2:
            Test->TestTrue(TEXT("Status executed through queue"),S->Events.Last().Text.Contains(TEXT("SCENARIO_01")));Test->TestTrue(TEXT("Enter retains input focus"),UI->IsPromptFocused());
            Key(EKeys::Up);Test->TestEqual(TEXT("History up"),Prompt->GetText().ToString(),FString(TEXT("status")));Key(EKeys::Down);Test->TestTrue(TEXT("History down restores draft"),Prompt->GetText().IsEmpty());Type(TEXT("he"));Key(EKeys::Tab);Test->TestEqual(TEXT("Verb autocomplete"),Prompt->GetText().ToString(),FString(TEXT("help ")));Key(EKeys::Enter);break;
        case 3:
            Test->TestTrue(TEXT("Grouped help reference"),S->Events.Last().Text.Contains(TEXT("CONTAINMENT")) && S->Events.Last().Text.Contains(TEXT("DIRECTIVES")) && S->Events.Last().Text.Contains(TEXT("MENUS")));Key(EKeys::Tilde,true);break;
        case 4:
        {
            const float H=float(Find(UI->GetContentRoot(),TEXT("ScutConsoleDrawer"))->GetCachedGeometry().GetLocalSize().Y);const float RootH=float(UI->GetContentRoot()->GetCachedGeometry().GetLocalSize().Y);
            Test->TestTrue(TEXT("Shift tilde opens 90 percent console"),UI->bFullConsole && H>RootH*.75f);Key(EKeys::Tilde,true);Command(TEXT("help direct"));break;
        }
        case 5:Test->TestTrue(TEXT("Exact help includes identifiers and rooms"),S->Events.Last().Text.Contains(TEXT("direct crew room [pretext]")) && S->Events.Last().Text.Contains(TEXT("ash / android")) && S->Events.Last().Text.Contains(TEXT("RING_CORRIDOR")));Command(TEXT("menu ai"));Menu(EShipAIScreenMenu::None);break;
        case 6:Menu(EShipAIScreenMenu::AI);Test->TestFalse(TEXT("Screen menu retracts drawer"),UI->bConsoleOpen);Test->TestEqual(TEXT("Five AI operations"),S->ScreenMenuChoices().Num(),5);Key(EKeys::One);break;
        case 7:Menu(EShipAIScreenMenu::RouteCrew);Key(EKeys::Four);break;
        case 8:Menu(EShipAIScreenMenu::RouteRoom);Key(EKeys::Three);break;
        case 9:Menu(EShipAIScreenMenu::RoutePretext);Key(EKeys::One);break;
        case 10:Menu(EShipAIScreenMenu::AI);Test->TestEqual(TEXT("AI routing menu assigns Ash to MED_BAY"),S->Simulation.Bodies[3].AssignedRoom,3);Key(EKeys::Two);break;
        case 11:Menu(EShipAIScreenMenu::AlertRoom);Click(3);break;
        case 12:Menu(EShipAIScreenMenu::AlertKind);Key(EKeys::One);break;
        case 13:Menu(EShipAIScreenMenu::AI);Test->TestTrue(TEXT("Menu sensor alert executes"),S->Simulation.Rooms[3].AlertSeconds>0);Key(EKeys::Three);break;
        case 14:Menu(EShipAIScreenMenu::Protocol);Key(EKeys::One);break;
        case 15:Menu(EShipAIScreenMenu::AI);Key(EKeys::Four);break;
        case 16:Menu(EShipAIScreenMenu::Containment);Click(2);break;
        case 17:Menu(EShipAIScreenMenu::DoorRoom);Key(EKeys::Three);break;
        case 18:Menu(EShipAIScreenMenu::AI);Test->TestFalse(TEXT("Menu seal executes"),S->Simulation.Doors[1].bIsOpen);Key(EKeys::Five);break;
        case 19:Menu(EShipAIScreenMenu::AndroidRoom);Key(EKeys::One);break;
        case 20:Menu(EShipAIScreenMenu::AI);Test->TestEqual(TEXT("Silent Android menu executes"),S->Simulation.Bodies[3].AssignedRoom,0);Key(EKeys::Escape);break;
        case 21:Menu(EShipAIScreenMenu::None);Key(EKeys::Slash);App.ProcessKeyCharEvent(FCharacterEvent('/',FModifierKeysState(),0,false));Test->TestTrue(TEXT("Slash opens clean prompt"),UI->IsPromptFocused() && Prompt->GetText().IsEmpty());Type(TEXT("direct a"));Key(EKeys::Tab);Test->TestEqual(TEXT("Crew completion"),Prompt->GetText().ToString(),FString(TEXT("direct ash ")));Type(TEXT("med"));Key(EKeys::Tab);Test->TestEqual(TEXT("Room completion and caret position"),Prompt->GetText().ToString(),FString(TEXT("direct ash MED_BAY ")));Key(EKeys::Enter);break;
        case 22:Test->TestEqual(TEXT("Completed command executes"),S->Simulation.Bodies[3].AssignedRoom,3);Key(EKeys::Up);Test->TestTrue(TEXT("History stores completed commands"),Prompt->GetText().ToString().StartsWith(TEXT("direct ash MED_BAY")));Key(EKeys::Up);Test->TestEqual(TEXT("History cycles previous commands"),Prompt->GetText().ToString(),FString(TEXT("menu ai")));Key(EKeys::Down);Key(EKeys::Down);Command(TEXT("clear"));break;
        case 23:Test->TestTrue(TEXT("Clear wipes scrollback"),S->Events.IsEmpty());Key(EKeys::Up);Test->TestEqual(TEXT("Clear retains command history"),Prompt->GetText().ToString(),FString(TEXT("clear")));Key(EKeys::Down);Command(TEXT("menu events"));break;
        case 24:Menu(EShipAIScreenMenu::Events);Test->TestEqual(TEXT("Four scenario event choices"),S->ScreenMenuChoices().Num(),4);Test->TestTrue(TEXT("Event live preview includes weights"),S->ScreenMenuChoices()[3].Detail.Contains(TEXT("Utility")) && S->ScreenMenuChoices()[3].Detail.Contains(TEXT("Suspicion")));BeforeUtility=S->Simulation.Bodies[0].Profile.Utility;Key(EKeys::Four);Test->TestEqual(TEXT("Event key waits for fixed tick"),S->Simulation.Bodies[0].Profile.Utility,BeforeUtility);break;
        case 25:Test->TestEqual(TEXT("Training applies previewed utility"),S->Simulation.Bodies[0].Profile.Utility,FMath::Min(1.f,BeforeUtility+.05f));BeforeSuspicion=S->Simulation.Bodies[0].Profile.Suspicion;Click(2);break;
        case 26:Test->TestTrue(TEXT("Event mouse choice applies reassurance"),S->Simulation.Bodies[0].Profile.Suspicion<=FMath::Max(0.f,BeforeSuspicion-.08f)+.001f);Key(EKeys::Enter);Command(TEXT("menu crew"));break;
        case 27:Menu(EShipAIScreenMenu::Crew);Test->TestFalse(TEXT("Matrix projects without drawer"),UI->bConsoleOpen);Key(EKeys::Escape);break;
        case 28:Menu(EShipAIScreenMenu::None);Key(EKeys::Eight);Key(EKeys::F);Test->TestTrue(TEXT("Camera inspection beneath overlays"),UI->bFullscreen);break;
        case 29:Key(EKeys::G);Test->TestTrue(TEXT("Grid restored beneath drawer"),UI->bGrid);Key(EKeys::Enter);Command(TEXT("?"));break;
        case 30:Test->TestTrue(TEXT("Question mark help alias"),S->Events.Last().Text.Contains(TEXT("SYSTEM")));Key(EKeys::Tilde);Test->TestTrue(TEXT("Backtick retracts and restores hotkeys"),!UI->bConsoleOpen && UI->HasKeyboardFocus());break;
        case 31:
        {
            // Controlled fixed-step fixture: verify clamped previews and real changes share one plan.
            S->bAutomaticAdvance=false;S->ResetSimulation();auto& M=S->Simulation;
            M.Bodies[0].Room=M.Bodies[1].Room=2;M.Bodies[0].Profile.Affinity=.95f;M.Bodies[1].Profile.Affinity=.4f;
            M.Bodies[0].Profile.Utility=M.Bodies[0].Profile.Compliance=.99f;
            S->QueueConsoleCommand(TEXT("menu events"));Test->TestEqual(TEXT("Menu waits for fixed tick"),int32(S->ActiveMenu),int32(EShipAIScreenMenu::None));S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Preview clamps affinity"),S->ScreenMenuChoices()[2].Detail.Contains(TEXT("Affinity +0.150")));
            S->QueueConsoleCommand(TEXT("choose 3"));Test->TestEqual(TEXT("Choice queues without immediate mutation"),M.Bodies[0].Profile.Affinity,.95f);S->AdvanceSteps(1);
            Test->TestEqual(TEXT("Reconciliation clamps first affinity"),M.Bodies[0].Profile.Affinity,1.f);Test->TestEqual(TEXT("Reconciliation changes roommate affinity"),M.Bodies[1].Profile.Affinity,.5f);
            Test->TestTrue(TEXT("Preview clamps utility and compliance"),S->ScreenMenuChoices()[3].Detail.Contains(TEXT("Utility +0.010")) && S->ScreenMenuChoices()[3].Detail.Contains(TEXT("Compliance +0.010")));
            S->QueueConsoleCommand(TEXT("choose 4"));S->AdvanceSteps(1);Test->TestEqual(TEXT("Utility clamp executed"),M.Bodies[0].Profile.Utility,1.f);Test->TestEqual(TEXT("Compliance clamp executed"),M.Bodies[0].Profile.Compliance,1.f);
            M.Bodies[0].Profile.Suspicion=.2f;M.Bodies[1].Profile.Suspicion=0;S->SetDoorOpen(0,true);
            S->QueueConsoleCommand(TEXT("seal bridge"));S->QueueConsoleCommand(TEXT("menu events"));S->QueueConsoleCommand(TEXT("choose 2"));S->AdvanceSteps(1);
            Test->TestTrue(TEXT("Batched seal then reassurance preserves submission order"),FMath::Abs(M.Bodies[0].Profile.Suspicion-.28f)<.0001f);
            S->QueueConsoleCommand(TEXT("menu close"));S->AdvanceSteps(1);S->bAutomaticAdvance=true;break;
        }
        case 33:FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9bPIE.png"),true,false);break;
        case 36:return true;
        }
        ++Stage;return false;
    }
private:
    FAutomationTestBase* Test;
    double Started=0,Last=0;
    int32 Stage=0;
    float BeforeUtility=0,BeforeSuspicion=0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlice9bTest,"DirectOrScut.Slice9b.RoutedConsolePIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSlice9bTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/DirectOrScut/Maps/DirectOrScutBlockout")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSlice9bRoutedConsole>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
