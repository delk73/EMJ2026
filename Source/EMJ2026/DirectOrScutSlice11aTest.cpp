#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutHUD.h"
#include "CompartmentCameraRig.h"
#include "Components/SceneCaptureComponent2D.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"

class FSlice11aCheck : public IAutomationLatentCommand
{
public:
    explicit FSlice11aCheck(FAutomationTestBase* T):Test(T){}
    static TSharedPtr<SWidget> Find(const TSharedRef<SWidget>& W,FName Tag,bool Type=false) {
        if((Type?W->GetType():W->GetTag())==Tag)return W;
        auto* C=W->GetChildren();for(int32 I=0;I<C->Num();++I)if(auto R=Find(C->GetChildAt(I),Tag,Type))return R;
        return nullptr;
    }
    bool Update() override {
        const double Now=FPlatformTime::Seconds();if(!Started)Started=Now;
        if(Now-Started>100){Test->AddError(TEXT("Slice11a PIE timeout"));return true;}
        auto* W=GEditor->PlayWorld.Get();if(!W || !W->HasBegunPlay())return false;
        auto* S=W->GetSubsystem<UDirectOrScutSimSubsystem>();auto* PC=W->GetFirstPlayerController();auto* H=PC?Cast<ADirectOrScutHUD>(PC->GetHUD()):nullptr;
        if(!S || !S->Simulation.bReady || !H || !H->GetWorkstation())return false;
        auto UI=H->GetWorkstation();if(UI->GetCachedGeometry().GetLocalSize().X<100 || Now-Last<.55)return false;Last=Now;
        auto& App=FSlateApplication::Get();auto Root=UI->GetContentRoot();auto& M=S->Simulation;
        auto Key=[&](FKey K){App.ProcessKeyDownEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0));App.ProcessKeyUpEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0));};
        auto Click=[&](FName Tag){auto B=Find(Root,Tag);Test->TestTrue(TEXT("Hit-tested target exists"),B.IsValid());if(!B)return;const auto& G=B->GetCachedGeometry();const FVector2D P=G.LocalToAbsolute(G.GetLocalSize()*.5);auto Win=App.FindWidgetWindow(B.ToSharedRef());if(!Win){Test->AddError(TEXT("Target window missing"));return;}App.ProcessMouseMoveEvent(FPointerEvent(0,P,P-FVector2D(1,0),TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()));App.ProcessMouseButtonDownEvent(Win->GetNativeWindow(),FPointerEvent(0,P,P,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));App.ProcessMouseButtonUpEvent(FPointerEvent(0,P,P,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));};
        auto Prompt=StaticCastSharedPtr<SEditableTextBox>(Find(Root,TEXT("ScutPrompt")));
        auto Command=[&](const TCHAR* C){App.SetKeyboardFocus(Prompt);Prompt->SetText(FText::FromString(C));Key(EKeys::Enter);};
        auto Scroll=StaticCastSharedPtr<SScrollBox>(Find(Root,TEXT("ScutLogScroll")));
        auto Wheel=[&](float Delta){const auto& G=Scroll->GetCachedGeometry();const FVector2D P=G.LocalToAbsolute(G.GetLocalSize()*.5);App.ProcessMouseMoveEvent(FPointerEvent(0,P,P-FVector2D(1,0),TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()));App.ProcessMouseWheelOrGestureEvent(FPointerEvent(0,P,P,TSet<FKey>{},EKeys::Invalid,Delta,FModifierKeysState()),nullptr);};
        UE_LOG(LogTemp,Display,TEXT("Slice11a stage %d"),Stage);
        switch(Stage++) {
        case 0: {
            FPlatformApplicationMisc::ClipboardPaste(OriginalClipboard);S->bAutomaticAdvance=false;S->ResetExperiment();UI->SetConsoleOpen(false);UI->ShowGrid();App.SetKeyboardFocus(UI);
            Key(EKeys::Right);Key(EKeys::Down);Test->TestEqual(TEXT("Arrow selection preserves column"),UI->SelectedCameraIndex,5);Key(EKeys::Enter);Test->TestTrue(TEXT("Enter inspects single feed"),UI->bFullscreen);Key(EKeys::G);Test->TestTrue(TEXT("G restores grid"),UI->bGrid);
            S->AdvanceSteps(1);Test->TestFalse(TEXT("Routine movement excluded from terminal"),S->TerminalBuffer().Contains(TEXT("MovingToRoom")));Test->TestTrue(TEXT("Routine movement retained in cinematic history"),S->CinematicCues.Num()>0);
            Key(EKeys::Slash);App.ProcessKeyCharEvent(FCharacterEvent('/',FModifierKeysState(),0,false));Test->TestTrue(TEXT("Slash opens TUI without inserting slash"),UI->bConsoleOpen && Prompt->GetText().IsEmpty());Test->TestTrue(TEXT("Category execution waits for fixed tick"),S->ActiveMenu==EShipAIScreenMenu::None);S->AdvanceSteps(1);break;
        }
        case 1: {
            auto Overlay=StaticCastSharedPtr<SBorder>(Find(Root,TEXT("ScutTUIOverlay")));auto Workspace=Find(Root,TEXT("ScutWorkspace"));auto Telemetry=Find(Root,TEXT("ScutTelemetry"));
            Test->TestTrue(TEXT("Exact 65 percent backdrop"),Overlay->GetBorderBackgroundColor().GetSpecifiedColor().Equals(FLinearColor(.01f,.03f,.015f,.65f),.0001f));
            Test->TestTrue(TEXT("Fullscreen TUI shares workspace bounds"),Overlay->GetCachedGeometry().GetLocalSize().Equals(Workspace->GetCachedGeometry().GetLocalSize(),1));
            Test->TestTrue(TEXT("TUI begins below telemetry"),Overlay->GetCachedGeometry().GetAbsolutePosition().Y>=Telemetry->GetCachedGeometry().GetAbsolutePosition().Y+Telemetry->GetCachedGeometry().GetAbsoluteSize().Y);
            const float L=Find(Root,TEXT("ScutMenuPane"))->GetCachedGeometry().GetLocalSize().X,R=Find(Root,TEXT("ScutOutputPane"))->GetCachedGeometry().GetLocalSize().X;Test->TestTrue(TEXT("40/60 split panes"),FMath::Abs(L/(L+R)-.4f)<.02f);
            Test->TestFalse(TEXT("Floating drawer removed"),Find(Root,TEXT("ScutConsoleDrawer")).IsValid());
            for(TActorIterator<ACompartmentCameraRig> It(W);It;++It)for(int32 F=1;F<=8;++F){Counts[F]=It->CaptureCounts[F];Test->TestFalse(TEXT("Feed retains vanilla postprocessing"),It->Captures[F]->ShowFlags.PostProcessing);Test->TestTrue(TEXT("All eight camera tiles remain present"),Find(Root,FName(*FString::Printf(TEXT("ScutCamera%d"),F))).IsValid());}
            auto MenuBar=StaticCastSharedPtr<SScrollBar>(Find(Find(Root,TEXT("ScutMenuScroll")).ToSharedRef(),TEXT("SScrollBar"),true));Test->TestTrue(TEXT("Scrollbar collapses without overflow"),MenuBar.IsValid() && !MenuBar->IsNeeded() && MenuBar->GetVisibility()==EVisibility::Collapsed);
            Click(TEXT("ScutCategory0"));S->AdvanceSteps(1);break;
        }
        case 2:Key(EKeys::Down);Key(EKeys::Enter);Test->TestTrue(TEXT("Keyboard choice deferred"),S->ActiveMenu==EShipAIScreenMenu::AI);S->AdvanceSteps(1);Test->TestTrue(TEXT("Keyboard choice enters alert-room subtree"),S->ActiveMenu==EShipAIScreenMenu::AlertRoom);break;
        case 3:Click(TEXT("ScutMenuBack"));S->AdvanceSteps(1);Test->TestTrue(TEXT("Back returns to parent"),S->ActiveMenu==EShipAIScreenMenu::AI);Key(EKeys::Tab);S->AdvanceSteps(1);Test->TestTrue(TEXT("Tab cycles category"),S->ActiveMenu==EShipAIScreenMenu::Events);break;
        case 4:Click(TEXT("ScutCategory2"));S->AdvanceSteps(1);Test->TestTrue(TEXT("Crew category available"),S->ActiveMenu==EShipAIScreenMenu::Crew);Test->TestTrue(TEXT("Crew values queried by subsystem"),S->ScreenMenuChoices()[0].Detail.Contains(TEXT("Compliance")));break;
        case 5:Click(TEXT("ScutCategory3"));S->AdvanceSteps(1);break;
        case 6:Click(TEXT("ScutMenuChoice3"));S->AdvanceSteps(1);Test->TestTrue(TEXT("Bulkhead submenu unified"),S->ActiveMenu==EShipAIScreenMenu::DoorAction);break;
        case 7:Click(TEXT("ScutMenuChoice1"));S->AdvanceSteps(1);Command(TEXT("unseal MED_BAY"));S->AdvanceSteps(1);Test->TestFalse(TEXT("CLI unseal alias works"),S->IsRoomSealed(3));Command(TEXT("help"));Test->TestFalse(TEXT("Help deferred to next tick"),S->TerminalBuffer().Contains(TEXT("SYSTEM")));S->AdvanceSteps(1);Test->TestTrue(TEXT("Help routed into output pane"),S->TerminalBuffer().Contains(TEXT("SYSTEM")));Key(EKeys::Up);Test->TestEqual(TEXT("Prompt command history"),Prompt->GetText().ToString(),FString(TEXT("help")));Prompt->SetText(FText::FromString(TEXT("unse")));Key(EKeys::Tab);Test->TestEqual(TEXT("Prompt Tab completion"),Prompt->GetText().ToString(),FString(TEXT("unseal ")));break;
        case 8:Click(TEXT("ScutCategory4"));S->AdvanceSteps(1);break;
        case 9: {const auto Revision=S->RuntimeRevision;Click(TEXT("ScutMenuChoice1"));Test->TestEqual(TEXT("Experiment reset waits for tick"),S->RuntimeRevision,Revision);S->AdvanceSteps(1);Test->TestEqual(TEXT("Reset starts at tick zero"),M.TickIndex,uint64(0));Command(TEXT("help"));S->AdvanceSteps(1);for(int32 I=0;I<80;++I)S->LogEvent(FString::Printf(TEXT("Archive review %03d: operator telemetry assessment retained in full terminal buffer."),I));S->RecordCue(3,.9f,TEXT("Medical review requires operator attention"));break;}
        case 10:S->AdvanceSteps(1);Click(TEXT("ScutCopyLog"));Test->TestEqual(TEXT("Copy feedback flashes COPIED"),StaticCastSharedPtr<STextBlock>(Find(Find(Root,TEXT("ScutCopyLog")).ToSharedRef(),TEXT("STextBlock"),true))->GetText().ToString(),FString(TEXT("COPIED")));{FString Copied;FPlatformApplicationMisc::ClipboardPaste(Copied);Test->TestEqual(TEXT("Clipboard contains complete filtered buffer"),Copied,S->TerminalBuffer());Test->TestTrue(TEXT("Buffer retains more than old 64-event cap"),Copied.Contains(TEXT("Archive review 000")) && Copied.Contains(TEXT("Archive review 079")));}Wheel(8);break;
        case 11:PausedOffset=Scroll->GetScrollOffset();Test->TestTrue(TEXT("Manual wheel moves away from bottom"),PausedOffset<Scroll->GetScrollOffsetOfEnd()-2);S->LogEvent(TEXT("New incident while operator reads older output"));break;
        case 12:Test->TestTrue(TEXT("New output preserves manual scroll position"),FMath::IsNearlyEqual(Scroll->GetScrollOffset(),PausedOffset,2));Scroll->ScrollToEnd();break;
        case 13:Wheel(-2);S->LogEvent(TEXT("Tail tracking resumed"));break;
        case 14:Test->TestTrue(TEXT("Auto-scroll resumes at bottom"),FMath::Abs(Scroll->GetScrollOffset()-Scroll->GetScrollOffsetOfEnd())<3);{
            auto Bar=StaticCastSharedPtr<SScrollBar>(Find(Scroll.ToSharedRef(),TEXT("SScrollBar"),true));Test->TestTrue(TEXT("Overflow scrollbar needed"),Bar.IsValid() && Bar->IsNeeded());if(Bar)Test->TestTrue(TEXT("Ultra-slim scrollbar width"),Bar->GetCachedGeometry().GetLocalSize().X<=4);
            for(TActorIterator<ACompartmentCameraRig> It(W);It;++It)for(int32 F=1;F<=8;++F)Test->TestTrue(TEXT("Every camera continues capturing behind TUI"),It->CaptureCounts[F]>Counts[F]);
            S->QueueConsoleCommand(TEXT("menu ai"));S->AdvanceSteps(1);Wheel(100);break;
        }
        case 15: {
            TArray<FColor> Pixels;FIntVector Size;const bool Captured=App.TakeScreenshot(Root,Pixels,Size);Test->TestTrue(TEXT("Fullscreen TUI screenshot captured"),Captured);if(Captured){TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,Png);Test->TestTrue(TEXT("TUI screenshot saved"),FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice11aPIE.png"))));}
            Key(EKeys::Escape);Test->TestFalse(TEXT("Escape closes TUI"),UI->bConsoleOpen);Key(EKeys::Tilde);Test->TestTrue(TEXT("Backtick reopens TUI"),UI->bConsoleOpen);Key(EKeys::Tilde);Test->TestFalse(TEXT("Backtick closes TUI"),UI->bConsoleOpen);FPlatformApplicationMisc::ClipboardCopy(*OriginalClipboard);return true;
        }
        }
        return false;
    }
private:FAutomationTestBase* Test;double Started=0,Last=0;int32 Stage=0;float PausedOffset=0;uint64 Counts[9]{};FString OriginalClipboard;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlice11aTest,"DirectOrScut.Slice11a.FullscreenTUIPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSlice11aTest::RunTest(const FString&) {
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSlice11aCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
