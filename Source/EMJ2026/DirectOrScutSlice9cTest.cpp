#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutHUD.h"
#include "CompartmentCameraRig.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

class FSlice9cTactical : public IAutomationLatentCommand
{
public:
    explicit FSlice9cTactical(FAutomationTestBase* T):Test(T) {}
    static TSharedPtr<SWidget> Find(const TSharedRef<SWidget>& W,FName Tag)
    {
        if(W->GetTag()==Tag) return W;
        auto* Children=W->GetChildren();for(int32 I=0;I<Children->Num();++I) if(auto Found=Find(Children->GetChildAt(I),Tag)) return Found;
        return nullptr;
    }
    bool Update() override
    {
        auto& App=FSlateApplication::Get();const double Now=FPlatformTime::Seconds();if(!Started)Started=Now;
        if(Now-Started>90){Test->AddError(TEXT("Slice 9c input timed out"));return true;}
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
        auto Step=[&](const TCHAR* C){S->QueueConsoleCommand(C);S->AdvanceSteps(1);};
        UE_LOG(LogTemp,Display,TEXT("9c stage %d menu %d visible %d focus %d"),Stage,int32(S->ActiveMenu),S->bWorkstationVisible,UI->HasKeyboardFocus());
        switch(Stage)
        {
        case 0: S->bAutomaticAdvance=false;Test->TestTrue(TEXT("Clean default"),S->bCleanDevRendering);Key(EKeys::F1);break;
        case 1: Menu(EShipAIScreenMenu::AI);Key(EKeys::One);break;
        case 2: Menu(EShipAIScreenMenu::RouteCrew);Click(4);break;
        case 3: Menu(EShipAIScreenMenu::RouteRoom);Key(EKeys::Three);break;
        case 4: Menu(EShipAIScreenMenu::AI);Test->TestEqual(TEXT("Route executes at room selection"),S->Simulation.Bodies[3].AssignedRoom,3);Key(EKeys::Two);break;
        case 5: Menu(EShipAIScreenMenu::AlertRoom);Key(EKeys::Two);break;
        case 6: Menu(EShipAIScreenMenu::AlertKind);Key(EKeys::BackSpace);break;
        case 7: Menu(EShipAIScreenMenu::AlertRoom);Key(EKeys::Two);break;
        case 8: Key(EKeys::Three);break;
        case 9: Test->TestTrue(TEXT("Pressure alert executes"),S->Simulation.Rooms[2].AlertSeconds>0);Key(EKeys::F4);break;
        case 10: Menu(EShipAIScreenMenu::DoorRoom);Click(7);break;
        case 11: Menu(EShipAIScreenMenu::DoorAction);Key(EKeys::Two);break;
        case 12: Menu(EShipAIScreenMenu::ConfirmScut);Test->TestFalse(TEXT("SCUT not executed before confirmation"),S->Simulation.Doors[8].bIsOpen);Key(EKeys::One);break;
        case 13: Test->TestTrue(TEXT("SCUT opens vacuum route"),S->Simulation.Doors[8].bIsOpen);Key(EKeys::Five);break;
        case 14: Menu(EShipAIScreenMenu::AndroidRoom);Key(EKeys::One);break;
        case 15: Menu(EShipAIScreenMenu::AndroidTask);Click(1);break;
        case 16: Test->TestEqual(TEXT("Shadow task routes Ash"),S->Simulation.Bodies[3].AssignedRoom,0);Key(EKeys::F9);Test->TestFalse(TEXT("F9 CRT"),S->bCleanDevRendering);Key(EKeys::F9);Key(EKeys::F10);break;
        case 17: Test->TestFalse(TEXT("F10 scene"),S->bWorkstationVisible);Key(EKeys::F10);break;
        case 18: Test->TestTrue(TEXT("F10 workstation recovery"),S->bWorkstationVisible);Key(EKeys::Enter);Command(TEXT("menu events"));break;
        case 19: Menu(EShipAIScreenMenu::Events);Test->TestFalse(TEXT("Menu retracts drawer"),UI->bConsoleOpen);Test->TestTrue(TEXT("Delta previews"),S->ScreenMenuChoices().Num()>1 && S->ScreenMenuChoices()[1].Detail.Contains(TEXT("SUS")));Click(2);break;
        case 20: Key(EKeys::F3);break;
        case 21: Menu(EShipAIScreenMenu::Crew);Key(EKeys::Four);break;
        case 22: Test->TestEqual(TEXT("Crew focus camera"),UI->ActiveFeed,ACompartmentCameraRig::FeedForRoom(S->Simulation.Bodies[3].Room));Test->TestFalse(TEXT("Crew focus expands"),UI->bGrid);Step(TEXT("fx crt"));Test->TestFalse(TEXT("fx command"),S->bCleanDevRendering);Step(TEXT("fx clean"));Step(TEXT("view 3d"));Test->TestFalse(TEXT("view command"),S->bWorkstationVisible);Step(TEXT("view crt"));Key(EKeys::G);break;
        case 23: Test->TestTrue(TEXT("G grid"),UI->bGrid);FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9cPIE.png"),true,false);break;
        case 26: Key(EKeys::F1);break;
        case 27: FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9cMenuPIE.png"),true,false);break;
        case 30: Key(EKeys::Three);break;
        case 31: Menu(EShipAIScreenMenu::AI);Test->TestTrue(TEXT("Calm prints delta"),S->Events.Last().Text.Contains(TEXT("CALM:")));Key(EKeys::F4);break;
        case 32: Key(EKeys::Two);break;
        case 33: Menu(EShipAIScreenMenu::DoorAction);Key(EKeys::One);break;
        case 34: Test->TestFalse(TEXT("Unseal opens room"),S->IsRoomSealed(2));Key(EKeys::F4);break;
        case 35: Key(EKeys::Two);break;
        case 36: Click(1);break;
        case 37: Test->TestTrue(TEXT("Seal closes room"),S->IsRoomSealed(2));Key(EKeys::Five);break;
        case 38: Key(EKeys::Two);break;
        case 39: Key(EKeys::Two);break;
        case 40: Test->TestTrue(TEXT("Scrub task executes"),S->Events.Last().Text.Contains(TEXT("scrubbed")));Key(EKeys::Five);break;
        case 41: Key(EKeys::Two);break;
        case 42: Key(EKeys::Three);break;
        case 43: Test->TestFalse(TEXT("Android override opens room"),S->IsRoomSealed(2));Key(EKeys::F2);break;
        case 44: Menu(EShipAIScreenMenu::Events);FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9cEventsPIE.png"),true,false);break;
        case 47: Key(EKeys::F3);break;
        case 48: FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9cCrewPIE.png"),true,false);break;
        case 51: Key(EKeys::Enter);break;
        case 52: Command(TEXT("help"));break;
        case 53: Test->TestTrue(TEXT("Drawer stays open after dismissing menu"),UI->bConsoleOpen);Test->TestTrue(TEXT("Help leaves tactical body visible"),Find(UI->GetContentRoot(),TEXT("ScutCamera1"))->GetCachedGeometry().GetLocalSize().Y>250);FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9cConsolePIE.png"),true,false);break;
        case 56: Key(EKeys::Escape);Key(EKeys::F10);break;
        case 57: FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9cScenePIE.png"),true,false);break;
        case 60: Key(EKeys::F10);Key(EKeys::F9);break;
        case 61: for(TActorIterator<ACompartmentCameraRig> It(W);It;++It) {Test->TestFalse(TEXT("Rig CRT mode"),It->bCleanDevRendering);for(int32 F=1;F<=8;++F)Test->TestEqual(TEXT("CRT shader enabled"),It->FeedMaterials[F]->K2_GetScalarParameterValue(TEXT("CleanDev")),0.f);}FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9cCrtPIE.png"),true,false);break;
        case 64: Key(EKeys::F9);break;
        case 65: for(TActorIterator<ACompartmentCameraRig> It(W);It;++It)for(int32 F=1;F<=8;++F)Test->TestEqual(TEXT("Clean shader restored"),It->FeedMaterials[F]->K2_GetScalarParameterValue(TEXT("CleanDev")),1.f);return true;
        }
        S->AdvanceSteps(1);++Stage;return false;
    }
private:
    FAutomationTestBase* Test;
    double Started=0,Last=0;
    int32 Stage=0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlice9cTest,"DirectOrScut.Slice9c.TacticalPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSlice9cTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/DirectOrScut/Maps/DirectOrScutBlockout")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSlice9cTactical>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
