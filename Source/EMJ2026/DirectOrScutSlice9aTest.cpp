#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutHUD.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "CompartmentCameraRig.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

class FSlice9aRoutedInput : public IAutomationLatentCommand
{
public:
    explicit FSlice9aRoutedInput(FAutomationTestBase* InTest):Test(InTest) {}
    static TSharedPtr<SWidget> Find(const TSharedRef<SWidget>& W,FName Tag)
    {
        if(W->GetTag()==Tag) return W;
        auto* Children=W->GetChildren();
        for(int32 I=0;I<Children->Num();++I) if(auto Found=Find(Children->GetChildAt(I),Tag)) return Found;
        return nullptr;
    }
    bool Update() override
    {
        auto& App=FSlateApplication::Get(); const double Now=FPlatformTime::Seconds();
        if(!Started) Started=Now;
        if(Now-Started>90) { Test->AddError(TEXT("Slice 9a routed input timed out")); return true; }
        UWorld* W=GEditor?GEditor->PlayWorld.Get():nullptr; if(!W || !W->HasBegunPlay()) return false;
        auto* PC=W->GetFirstPlayerController(); auto* HUD=PC?Cast<ADirectOrScutHUD>(PC->GetHUD()):nullptr;
        if(!HUD || !HUD->GetWorkstation()) return false;
        auto UI=HUD->GetWorkstation(); auto* S=W->GetSubsystem<UDirectOrScutSimSubsystem>();
        if(!S || S->Simulation.TickIndex<30 || UI->GetCachedGeometry().GetLocalSize().X<100 || Now-Last<.16) return false;
        Last=Now;
        if (!Find(UI->GetContentRoot(),TEXT("ScutPrompt"))) { Test->AddError(TEXT("Prompt missing from workstation content")); return true; }
        auto Key=[&](FKey K){ App.ProcessKeyDownEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0));App.ProcessKeyUpEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0)); };
        auto Type=[&](const FString& Text){ for(TCHAR C:Text) { const FKey K(FName(*FString::Chr(FChar::ToUpper(C)))); App.ProcessKeyDownEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,C)); App.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false)); App.ProcessKeyUpEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,C)); } };
        auto Command=[&](const TCHAR* Text){Type(Text);Key(EKeys::Enter);};
        auto Click=[&](FName Tag,bool Double)
        {
            auto Widget=Find(UI->GetContentRoot(),Tag); Test->TestTrue(TEXT("Clickable widget exists"),Widget.IsValid()); if(!Widget) return;
            const auto& G=Widget->GetCachedGeometry(); const FVector2D P=G.LocalToAbsolute(G.GetLocalSize()*.5f);
            auto Window=App.FindWidgetWindow(Widget.ToSharedRef());
            Test->TestTrue(TEXT("Click has a live Slate window"),Window.IsValid()); if(!Window) return;
            App.ProcessMouseMoveEvent(FPointerEvent(0,P,P-FVector2D(1,0),TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()));
            const FPointerEvent Down(0,P,P,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
            if(Double) App.ProcessMouseButtonDoubleClickEvent(Window->GetNativeWindow(),Down); else App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),Down);
            App.ProcessMouseButtonUpEvent(FPointerEvent(0,P,P,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));
        };
        if(Stage==0)
        {
            Test->TestTrue(TEXT("Startup workstation owns keyboard focus without clicking"),UI->HasKeyboardFocus());
            Test->TestTrue(TEXT("Startup is CCTV grid alongside console"),UI->bGrid && !UI->bFullscreen);
            Test->TestFalse(TEXT("No tactical overview tile"),Find(UI->GetContentRoot(),TEXT("ScutCamera0")).IsValid());
            Test->TestTrue(TEXT("Telemetry ninth tile"),Find(UI->GetContentRoot(),TEXT("ScutTelemetry")).IsValid());
            for(TActorIterator<ACompartmentCameraRig> It(W);It;++It) { Rig=*It; break; }
            Test->TestNotNull(TEXT("Camera rig"),Rig.Get()); if(!Rig.IsValid()) return true;
            for(int32 F=1;F<=8;++F) { Test->TestTrue(TEXT("All eight CCTV cameras rendered at startup"),Rig->CaptureCounts[F]>0); InitialCaptures[F]=Rig->CaptureCounts[F]; }
        }
        else if(Stage<=8) { const FKey Keys[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight};Key(Keys[Stage-1]);Test->TestEqual(TEXT("Routed feed key selects camera"),UI->ActiveFeed,Stage);Test->TestTrue(TEXT("Single key preserves grid"),UI->bGrid); }
        else switch(Stage)
        {
        case 9: Key(EKeys::Eight);Test->TestTrue(TEXT("Double key expands"),UI->bFullscreen);break;
        case 10: Key(EKeys::Escape);Test->TestTrue(TEXT("Escape returns grid"),UI->bGrid);break;
        case 11: Key(EKeys::F);Test->TestTrue(TEXT("F expands"),UI->bFullscreen);break;
        case 12: Key(EKeys::Tab);Test->TestTrue(TEXT("Tab returns grid"),UI->bGrid);break;
        case 13: Key(EKeys::G);Test->TestTrue(TEXT("G expands selection"),UI->bFullscreen);break;
        case 14: Key(EKeys::G);Test->TestTrue(TEXT("G returns grid"),UI->bGrid);break;
        case 15: Key(EKeys::Enter);Test->TestTrue(TEXT("Enter focuses inner prompt editor"),UI->IsPromptFocused());break;
        case 16: Type(TEXT("status"));Test->TestEqual(TEXT("Characters route to prompt"),StaticCastSharedPtr<SEditableTextBox>(Find(UI->GetContentRoot(),TEXT("ScutPrompt")))->GetText().ToString(),FString(TEXT("status")));break;
        case 17: Key(EKeys::Enter);Test->TestTrue(TEXT("Submit keeps prompt focus"),UI->IsPromptFocused());Test->TestTrue(TEXT("Status submitted via Slate"),S->Events.Last().Text.Contains(TEXT("SCENARIO_01")));break;
        case 18: Command(TEXT("crew"));Test->TestTrue(TEXT("Crew command typed"),S->Events.Last().Text.Contains(TEXT("VALE")));break;
        case 19: Command(TEXT("rooms"));Test->TestTrue(TEXT("Rooms command typed"),S->Events.Last().Text.Contains(TEXT("REACTOR_CORE")));break;
        case 20: Command(TEXT("llm"));Test->TestTrue(TEXT("LLM command typed"),S->bAILink);break;
        case 21: Command(TEXT("cmd"));Test->TestFalse(TEXT("CMD command typed"),S->bAILink);break;
        case 22: Command(TEXT("direct ash med_bay telemetry"));break;
        case 23: Test->TestEqual(TEXT("Typed direct reached fixed simulation"),S->Simulation.Bodies[3].AssignedRoom,3);break;
        case 24: Command(TEXT("alert med_bay fault"));break;
        case 25: Command(TEXT("seal med_bay"));break;
        case 26: Command(TEXT("scut airlock"));break;
        case 27: Test->TestFalse(TEXT("Typed SCUT vents airlock"),S->Simulation.Rooms[7].bIsPressurized);break;
        case 28: Key(EKeys::Escape);Test->TestTrue(TEXT("Escape restores workstation hotkeys"),UI->HasKeyboardFocus());break;
        case 29: Key(EKeys::Slash);App.ProcessKeyCharEvent(FCharacterEvent('/',FModifierKeysState(),0,false));Test->TestTrue(TEXT("Slash focuses prompt"),UI->IsPromptFocused());Test->TestTrue(TEXT("Focus slash is not inserted"),StaticCastSharedPtr<SEditableTextBox>(Find(UI->GetContentRoot(),TEXT("ScutPrompt")))->GetText().IsEmpty());break;
        case 30: Command(TEXT("status"));break;
        case 31: Key(EKeys::Escape);Key(EKeys::Three);Test->TestEqual(TEXT("Hotkeys resume after typing"),UI->ActiveFeed,3);break;
        case 32: Click(TEXT("ScutCamera5"),false);break;
        case 33: Test->TestEqual(TEXT("Whole tile mouse hit selects"),UI->ActiveFeed,5);Test->TestTrue(TEXT("Mouse single click keeps grid"),UI->bGrid);Click(TEXT("ScutCamera5"),true);break;
        case 34: Test->TestTrue(TEXT("Whole tile double-click expands"),UI->bFullscreen);Key(EKeys::Escape);S->LogEvent(TEXT("Compartment link verification // MED_BAY"),3);break;
        case 35: Click(FName(*FString::Printf(TEXT("ScutEvent%d"),S->Events.Num()-1)),false);break;
        case 36: Test->TestEqual(TEXT("Event click selects compartment feed"),UI->ActiveFeed,3);Test->TestTrue(TEXT("Event click inspects compartment"),UI->bFullscreen);Key(EKeys::Tab);break;
        case 37:
            Test->TestTrue(TEXT("Final grid restored"),UI->bGrid);
            for(int32 F=1;F<=8;++F) Test->TestTrue(TEXT("Every visible CCTV feed continues capturing"),Rig->CaptureCounts[F]>InitialCaptures[F]);
            Test->TestEqual(TEXT("Tactical capture stays inactive"),Rig->CaptureCounts[0],uint64(0));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9aPIE.png"),true,false);break;
        case 40: return true;
        }
        ++Stage;return false;
    }
private:
    FAutomationTestBase* Test;
    TWeakObjectPtr<ACompartmentCameraRig> Rig;
    uint64 InitialCaptures[9]{};
    int32 Stage=0;
    double Started=0,Last=0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlice9aTest,"DirectOrScut.Slice9a.RoutedInputPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSlice9aTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/DirectOrScut/Maps/DirectOrScutBlockout")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSlice9aRoutedInput>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
