#include "SDirectOrScutWorkstationWidget.h"
#include "CompartmentCameraRig.h"
#include "Engine/TextureRenderTarget2D.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutGeometry.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Slate/SRetainerWidget.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Styling/CoreStyle.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"

// Claim developer view keys before PIE editor command processing,
// only while this workstation owns focus, before editor command processing.
class FScutTacticalInput final : public IInputProcessor
{
public:
    TWeakPtr<SDirectOrScutWorkstationWidget> Owner;
    virtual void Tick(float,FSlateApplication&,TSharedRef<ICursor>) override {}
    virtual bool HandleKeyDownEvent(FSlateApplication&,const FKeyEvent& E) override {
        auto W=Owner.Pin();if(!W || !(W->HasKeyboardFocus() || W->HasFocusedDescendants()))return false;
        const auto K=E.GetKey();if(K!=EKeys::F9 && K!=EKeys::F10)return false;
        return W->OnPreviewKeyDown(W->GetCachedGeometry(),E).IsEventHandled();
    }
};
SDirectOrScutWorkstationWidget::~SDirectOrScutWorkstationWidget() { if(FSlateApplication::IsInitialized() && TacticalInput)FSlateApplication::Get().UnregisterInputPreProcessor(TacticalInput); }
// Keep tiles alive on single selection so Slate can route the second click to the same widget.
class SScutCameraTile final : public SBorder
{
public:
    SLATE_BEGIN_ARGS(SScutCameraTile) {} SLATE_ARGUMENT(TWeakPtr<SDirectOrScutWorkstationWidget>,Owner) SLATE_ARGUMENT(int32,Feed) SLATE_DEFAULT_SLOT(FArguments,Content) SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Owner=Args._Owner; Feed=Args._Feed;
        SetTag(FName(*FString::Printf(TEXT("ScutCamera%d"),Feed)));
        SBorder::Construct(SBorder::FArguments().Padding(3).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor_Lambda([this](){auto W=Owner.Pin();return W && W->SelectedCameraIndex==Feed-1?FLinearColor(.25f,1.f,.08f):FLinearColor(.025f,.045f,.03f);})[Args._Content.Widget]);
    }
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent& E) override
    {
        if (E.GetEffectingButton()!=EKeys::LeftMouseButton) return FReply::Unhandled();
        if (auto W=Owner.Pin()) { W->SetConsoleOpen(false); W->SelectFeed(Feed,false); return FReply::Handled().SetUserFocus(W.ToSharedRef(),EFocusCause::Mouse); }
        return FReply::Unhandled();
    }
    virtual FReply OnMouseButtonDoubleClick(const FGeometry&,const FPointerEvent& E) override
    {
        if (E.GetEffectingButton()!=EKeys::LeftMouseButton) return FReply::Unhandled();
        if (auto W=Owner.Pin()) { W->SetConsoleOpen(false); W->SelectFeed(Feed,false); W->ToggleExpanded(); return FReply::Handled().SetUserFocus(W.ToSharedRef(),EFocusCause::Mouse); }
        return FReply::Unhandled();
    }
private:
    TWeakPtr<SDirectOrScutWorkstationWidget> Owner;
    int32 Feed=1;
};
void SDirectOrScutWorkstationWidget::Construct(const FArguments& Args)
{
    Sim=Args._Sim; Rig=Args._Rig;
    for (int32 F=1;F<=8;++F) { Brushes[F].SetResourceObject(static_cast<UObject*>(Rig->Targets[F].Get())); Brushes[F].ImageSize=FVector2D(1024,640); Brushes[F].DrawAs=ESlateBrushDrawType::Image; }
    SlimScrollStyle=FCoreStyle::Get().GetWidgetStyle<FScrollBarStyle>("ScrollBar");
    SlimScrollStyle.SetNormalThumbImage(FSlateColorBrush(FLinearColor(.15f,.7f,.3f,.7f)))
        .SetHoveredThumbImage(FSlateColorBrush(FLinearColor(.3f,1,.45f)))
        .SetDraggedThumbImage(FSlateColorBrush(FLinearColor(.3f,1,.45f)))
        .SetVerticalBackgroundImage(FSlateNoResource()).SetHorizontalBackgroundImage(FSlateNoResource())
        .SetVerticalTopSlotImage(FSlateNoResource()).SetVerticalBottomSlotImage(FSlateNoResource())
        .SetHorizontalTopSlotImage(FSlateNoResource()).SetHorizontalBottomSlotImage(FSlateNoResource());
    ChildSlot[SAssignNew(Retainer,SRetainerWidget)[SNew(SBorder).Padding(0).BorderBackgroundColor(FLinearColor(.015f,.025f,.02f,1))[SAssignNew(Root,SVerticalBox)]]];
    Sim->LogEvent(TEXT("Private uplink established. Crew sees only automated Ship AI protocols."));
    Sim->LogEvent(Sim->SubmitDirective(TEXT("status")));
    Sim->LogEvent(TEXT("Backtick / slash: fullscreen TUI | Tab: categories or completion | Up/Down: menu or history | Enter: execute | Esc: close | help: reference"));
    Sim->LogEvent(TEXT("Crew: Vale / engineer, Chen / medic, Ortiz / technician, Ash / android. Commands use full room names, e.g. direct ash MED_BAY telemetry; alert AIRLOCK_CHAMBER fault."));
    Rebuild();
    auto Input=MakeShared<FScutTacticalInput>();Input->Owner=SharedThis(this);TacticalInput=Input;FSlateApplication::Get().RegisterInputPreProcessor(TacticalInput,0);
}
TSharedRef<SWidget> SDirectOrScutWorkstationWidget::GetContentRoot() const { return Root.ToSharedRef(); }
bool SDirectOrScutWorkstationWidget::IsPromptFocused() const { return Prompt.IsValid() && (Prompt->HasKeyboardFocus() || Prompt->HasFocusedDescendants()); }
TSharedRef<SWidget> SDirectOrScutWorkstationWidget::Video(int32 F)
{
    const int32 R=ACompartmentCameraRig::RoomForFeed(F);
    auto CrewBadges=SNew(SVerticalBox);
    const TCHAR* Names[]={TEXT("vale"),TEXT("chen"),TEXT("ortiz"),TEXT("ash")};
    for(int32 I=0;I<4;++I)
    {
        const FString Command=FString(TEXT("follow "))+Names[I];
        CrewBadges->AddSlot().AutoHeight()[SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").IsFocusable(false)
            .Tag(FName(*FString::Printf(TEXT("ScutFollowCrew%dFeed%d"),I,F)))
            .Visibility_Lambda([this,I,R](){const auto& U=Sim->Simulation.Bodies[I];return Sim->CanFollowCrew(I) && U.Room==R?EVisibility::Visible:EVisibility::Collapsed;})
            .OnClicked_Lambda([this,Command](){SetConsoleOpen(false);Sim->QueueConsoleCommand(Command);return FReply::Handled().SetUserFocus(AsShared());})
            [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",10)).ColorAndOpacity(FLinearColor(.6f,1,.65f))
                .Text_Lambda([this,I](){const auto& U=Sim->Simulation.Bodies[I];const TCHAR* N[]={TEXT("VALE"),TEXT("CHEN"),TEXT("ORTIZ"),TEXT("ASH")};return FText::FromString(FString::Printf(TEXT("[%s%s] %s | %.0f BPM %.0f RPM %.1fmm"),N[I],Sim->FollowedCrew==I?TEXT(" // FOLLOW"):TEXT(""),DirectOrScutStateName(U.State),U.Profile.HeartRateBPM,U.Profile.RespirationRateRPM,U.Profile.PupilDilationMM));})]];
    }
    return SNew(SScutCameraTile).Owner(SharedThis(this)).Feed(F)
    [SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor::Black)[SNew(SOverlay)
        +SOverlay::Slot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SImage).Image(&Brushes[F])]]
        +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
            [SNew(SBorder).Padding(4).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this,F](){return SelectedCameraIndex==F-1?FLinearColor(.25f,1,.08f):FLinearColor(.005f,.015f,.01f);})
                [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",10))
                    .ColorAndOpacity_Lambda([this,F](){return SelectedCameraIndex==F-1?FLinearColor::Black:FLinearColor(.3f,1,.45f);})
                    .Text_Lambda([this,F,R](){const auto& Room=Sim->Simulation.Rooms[R];return FText::FromString(FString::Printf(TEXT("%s[%d] %s | %s"),SelectedCameraIndex==F-1?TEXT(">> "):TEXT(""),F,DirectOrScut::RoomName(R),!Room.bIsPressurized?TEXT("VENTING"):Room.AlertSeconds>0?TEXT("ALERT"):Sim->IsRoomSealed(R)?TEXT("SEALED"):TEXT("NOMINAL")));})]]
        +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0,25,4,0)
            [SNew(SButton).Tag(FName(*FString::Printf(TEXT("ScutInspect%d"),F))).IsFocusable(false).Text(FText::FromString(TEXT("[INSPECT]")))
                .OnClicked_Lambda([this,F](){SetConsoleOpen(false);SelectFeed(F,false);ExpandSelected();return FReply::Handled().SetUserFocus(AsShared());})]
        +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom)[CrewBadges]]];
}

void SDirectOrScutWorkstationWidget::SelectFeed(int32 F,bool AllowDoubleTap)
{
    if (F<1 || F>8) return;
    const double Now=FPlatformTime::Seconds();
    const bool DoubleTap=AllowDoubleTap && LastFeed==F && Now-LastTap<.3;
    LastFeed=AllowDoubleTap?F:-1; LastTap=Now;
    const bool Changed=ActiveFeed!=F; ActiveFeed=F;SelectedCameraIndex=F-1;
    if (DoubleTap) ToggleExpanded();
    else if (Changed && !bGrid) Rebuild();
}
void SDirectOrScutWorkstationWidget::ExpandSelected() {if(!bFullscreen){bFullscreen=true;bGrid=false;Rebuild();}}
void SDirectOrScutWorkstationWidget::ToggleExpanded() { bFullscreen=!bFullscreen; bGrid=!bFullscreen; Rebuild(); }
void SDirectOrScutWorkstationWidget::ShowGrid() { bGrid=true; bFullscreen=false; LastFeed=-1; Rebuild(); }
void SDirectOrScutWorkstationWidget::SetConsoleOpen(bool Open,bool)
{
    if(Open && Sim->ExperimentPhase==EExperimentPhase::Resolved)return;
    bConsoleOpen=Open;
    if(Open && !bGrid)ShowGrid();
    if(Open) {
        if(Sim->ActiveMenu==EShipAIScreenMenu::None)SelectCategory(ActiveCategory);
        FSlateApplication::Get().SetKeyboardFocus(Prompt,EFocusCause::SetDirectly);
    } else FSlateApplication::Get().SetKeyboardFocus(AsShared(),EFocusCause::SetDirectly);
}
void SDirectOrScutWorkstationWidget::Rebuild()
{
    const FText Draft=Prompt.IsValid()?Prompt->GetText():FText::GetEmpty();
    Root->ClearChildren();
    Root->AddSlot().AutoHeight().Padding(6,4)[SNew(STextBlock).Tag(TEXT("ScutTelemetry")).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).ColorAndOpacity(FLinearColor(.3f,1,.45f)).Text_Lambda([this](){
        const auto& M=Sim->Simulation;int32 T=int32(M.TickIndex/30),Alerts=0;for(const auto& R:M.Rooms)Alerts+=R.AlertSeconds>0 || !R.bIsPressurized;
        return FText::FromString(FString::Printf(TEXT("EXPERIMENT %02d:%02d / 10:00 | OBJECTIVE %.0f%% | GLOBAL SUSPICION %.0f%% | ACTIVE ALERTS %d | %s | %s"),T/60,T%60,M.ObjectiveProgress*100,M.CollectiveSuspicion()*100,Alerts,*M.Scenario.ScenarioId,*Sim->FollowStatus()));})];
    TSharedRef<SWidget> Main=Video(ActiveFeed);
    if(bGrid) {
        auto Grid=SNew(SUniformGridPanel).SlotPadding(2);
        for(int32 F=1;F<=8;++F)Grid->AddSlot((F-1)%4,(F-1)/4)[Video(F)];
        Main=Grid;
    }
    Root->AddSlot().FillHeight(1)[SAssignNew(Body,SOverlay).Tag(TEXT("ScutWorkspace")).Clipping(EWidgetClipping::ClipToBounds)
        +SOverlay::Slot()[Main]
        +SOverlay::Slot()[BuildTerminal(Draft)]
        +SOverlay::Slot()[BuildEvaluationDossier()]];
    Root->AddSlot().AutoHeight().Padding(6,3)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",10)).ColorAndOpacity(FLinearColor(.4f,.85f,.5f)).Text(FText::FromString(TEXT("[~ / /] TUI TERMINAL | [ARROWS / 1-8] SELECT FEED | [ENTER / F] INSPECT | [G] 8-CAM GRID  ")))]
        +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Tag(TEXT("ScutPriorityAlert")).Font(FCoreStyle::GetDefaultFontStyle("Regular",10)).ColorAndOpacity(FLinearColor(1,.75f,.25f)).Text_Lambda([this](){return FText::FromString(Sim->LatestPriorityAlert());})]];
    SeenEvents=MAX_uint64;SeenMenuRevision=MAX_uint64;
    RefreshLog();RebuildMenu();
    if(bConsoleOpen)FSlateApplication::Get().SetKeyboardFocus(Prompt,EFocusCause::SetDirectly);
}
TSharedRef<SWidget> SDirectOrScutWorkstationWidget::BuildTerminal(const FText& Draft)
{
    auto Terminal=SNew(SVerticalBox);
    Terminal->AddSlot().AutoHeight()[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",16)).ColorAndOpacity(FLinearColor(.3f,1,.45f)).Text(FText::FromString(TEXT("SHIP AI // TUI COMMAND TERMINAL")))]
        +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Tag(TEXT("ScutCopyLog")).IsFocusable(false).OnClicked(this,&SDirectOrScutWorkstationWidget::CopyLog)
            [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(FPlatformTime::Seconds()<CopiedUntil?TEXT("COPIED"):TEXT("[COPY LOG]"));})]]];
    auto Tabs=SNew(SHorizontalBox);
    const TCHAR* Labels[]={TEXT("[DIRECTIVES]"),TEXT("[EVENTS]"),TEXT("[CREW]"),TEXT("[BULKHEADS]"),TEXT("[EXPERIMENT]")};
    for(int32 I=0;I<5;++I)Tabs->AddSlot().FillWidth(1).Padding(2)[SNew(SButton).Tag(FName(*FString::Printf(TEXT("ScutCategory%d"),I))).IsFocusable(false)
        .ButtonColorAndOpacity_Lambda([this,I](){return ActiveCategory==I?FLinearColor(.2f,.6f,.3f,.8f):FLinearColor(.05f,.12f,.07f,.5f);})
        .Text(FText::FromString(Labels[I])).OnClicked_Lambda([this,I](){SelectCategory(I);return FReply::Handled().SetUserFocus(AsShared());})];
    Terminal->AddSlot().AutoHeight().Padding(0,8)[Tabs];
    auto Left=SNew(SVerticalBox);
    Left->AddSlot().AutoHeight()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).ColorAndOpacity(FLinearColor(.3f,1,.45f)).AutoWrapText(true).Text_Lambda([this](){return FText::FromString(Sim->ScreenMenuTitle());})];
    Left->AddSlot().AutoHeight().Padding(0,6)[SNew(SButton).Tag(TEXT("ScutMenuBack")).IsFocusable(false).Text(FText::FromString(TEXT("[BACK]  Tab: category | Up/Down: select | Enter: execute"))).OnClicked_Lambda([this](){Sim->QueueConsoleCommand(TEXT("menu back"));return FReply::Handled().SetUserFocus(AsShared());})];
    Left->AddSlot().FillHeight(1)[SAssignNew(MenuScroll,SScrollBox).Tag(TEXT("ScutMenuScroll")).ScrollBarStyle(&SlimScrollStyle).ScrollBarThickness(FVector2D(3,3)).ScrollBarPadding(FMargin(0)).ScrollBarVisibility(EVisibility::Visible).ScrollBarAlwaysVisible(false)
        +SScrollBox::Slot()[SAssignNew(MenuList,SVerticalBox)]];
    auto Right=SNew(SVerticalBox);
    Right->AddSlot().AutoHeight()[SNew(STextBlock).ColorAndOpacity(FLinearColor(.6f,1,.7f)).Text(FText::FromString(TEXT("OUTPUT & PRIORITY ALERTS // scroll to pause; return to bottom to resume")))];
    Right->AddSlot().FillHeight(1).Padding(0,8)[SAssignNew(LogScroll,SScrollBox).Tag(TEXT("ScutLogScroll")).ScrollBarStyle(&SlimScrollStyle).ScrollBarThickness(FVector2D(3,3)).ScrollBarPadding(FMargin(0)).ScrollBarVisibility(EVisibility::Visible).ScrollBarAlwaysVisible(false)
        .OnUserScrolled_Lambda([this](float Offset){bLogAutoScroll=Offset>=LogScroll->GetScrollOffsetOfEnd()-2;})+SScrollBox::Slot()[SAssignNew(Log,SVerticalBox)]];
    Terminal->AddSlot().FillHeight(1)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(.4f).Padding(0,0,12,0)[SNew(SBox).Tag(TEXT("ScutMenuPane"))[Left]]
        +SHorizontalBox::Slot().FillWidth(.6f).Padding(12,0,0,0)[SNew(SBox).Tag(TEXT("ScutOutputPane"))[Right]]];
    Terminal->AddSlot().AutoHeight().Padding(0,8,0,0)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).IsFocusable(false).Tag(TEXT("ScutFocusPrompt")).Text_Lambda([this](){return FText::FromString(Sim->bAILink?TEXT("AI-LINK>"):TEXT("CMD>"));}).OnClicked_Lambda([this](){return FReply::Handled().SetUserFocus(Prompt.ToSharedRef());})]
        +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(Prompt,SEditableTextBox).Tag(TEXT("ScutPrompt")).Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).ForegroundColor(FLinearColor(.3f,1,.45f)).BackgroundColor(FLinearColor(0,0,0,.2f)).Text(Draft).ClearKeyboardFocusOnCommit(false)
            .HintText(FText::FromString(TEXT("help | follow | seal / unseal | scut | scenario | reset_experiment")))
            .OnKeyCharHandler_Lambda([this](const FGeometry&,const FCharacterEvent& E){const bool Suppress=SuppressPromptChar && (E.GetCharacter()==SuppressPromptChar || (SuppressPromptChar=='`' && E.GetCharacter()=='~'));SuppressPromptChar=0;return Suppress?FReply::Handled():FReply::Unhandled();})
            .OnTextCommitted(this,&SDirectOrScutWorkstationWidget::Commit)]];
    return SNew(SBorder).Tag(TEXT("ScutTUIOverlay")).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.01f,.03f,.015f,.65f)).Padding(18)
        .Visibility_Lambda([this](){return bConsoleOpen && !bShowingEvaluation?EVisibility::Visible:EVisibility::Collapsed;})[Terminal];
}
void SDirectOrScutWorkstationWidget::SelectCategory(int32 Category)
{
    ActiveCategory=(Category+5)%5;HighlightedChoice=0;
    const TCHAR* Commands[]={TEXT("menu ai"),TEXT("menu events"),TEXT("menu crew"),TEXT("menu doors"),TEXT("menu experiment")};
    Sim->QueueConsoleCommand(Commands[ActiveCategory]);
}
void SDirectOrScutWorkstationWidget::ExecuteHighlighted()
{
    if(MenuEnabled.IsValidIndex(HighlightedChoice) && MenuEnabled[HighlightedChoice])Sim->QueueConsoleCommand(FString::Printf(TEXT("choose %d"),HighlightedChoice+1));
}
FReply SDirectOrScutWorkstationWidget::CopyLog()
{
    FPlatformApplicationMisc::ClipboardCopy(*Sim->TerminalBuffer());CopiedUntil=FPlatformTime::Seconds()+1.5;
    return FReply::Handled();
}
void SDirectOrScutWorkstationWidget::RefreshLog()
{
    const float Offset=LogScroll->GetScrollOffset();
    Log->ClearChildren();
    for(const auto& E:Sim->Events)if(E.bTerminalVisible)Log->AddSlot().AutoHeight().Padding(0,2)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).Text(FText::FromString(E.Text)).AutoWrapText(true).ColorAndOpacity(FLinearColor(.4f,1,.55f))];
    if(bLogAutoScroll)LogScroll->ScrollToEnd();else LogScroll->SetScrollOffset(Offset);
    SeenEvents=Sim->EventRevision;
}
void SDirectOrScutWorkstationWidget::BrowseHistory(int32 Direction)
{
    if(History.IsEmpty()) return;
    if(HistoryCursor==History.Num()) HistoryDraft=Prompt->GetText().ToString();
    HistoryCursor=FMath::Clamp(HistoryCursor+Direction,0,History.Num());
    Prompt->SetText(FText::FromString(HistoryCursor<History.Num()?History[HistoryCursor]:HistoryDraft));
    Prompt->GoTo(ETextLocation::EndOfDocument);
    Completions.Reset();LastCompletion.Empty();
}
void SDirectOrScutWorkstationWidget::Autocomplete()
{
    const FString Input=Prompt->GetText().ToString();
    if(Completions.IsEmpty() || Input!=LastCompletion) {Completions=Sim->CompleteConsoleCommand(Input);CompletionCursor=0;}
    else CompletionCursor=(CompletionCursor+1)%Completions.Num();
    if(!Completions.IsEmpty()) {LastCompletion=Completions[CompletionCursor];Prompt->SetText(FText::FromString(LastCompletion));Prompt->GoTo(ETextLocation::EndOfDocument);}
}
void SDirectOrScutWorkstationWidget::Commit(const FText& Text,ETextCommit::Type Type)
{
    if(Type!=ETextCommit::OnEnter || !Sim.IsValid()) return;
    const FString Input=Text.ToString().TrimStartAndEnd();if(Input.IsEmpty()) return;
    if(Sim->QueueConsoleCommand(Input))
    {
        if(History.IsEmpty() || History.Last()!=Input) {if(History.Num()>=64) History.RemoveAt(0);History.Add(Input);}
        HistoryCursor=History.Num();HistoryDraft.Empty();Completions.Reset();LastCompletion.Empty();Prompt->SetText(FText::GetEmpty());
    }
}
FReply SDirectOrScutWorkstationWidget::OnMouseButtonDown(const FGeometry&,const FPointerEvent& E)
{
    return E.GetEffectingButton()==EKeys::LeftMouseButton?FReply::Handled().SetUserFocus(bConsoleOpen?StaticCastSharedRef<SWidget>(Prompt.ToSharedRef()):AsShared(),EFocusCause::Mouse):FReply::Unhandled();
}
FReply SDirectOrScutWorkstationWidget::OnPreviewKeyDown(const FGeometry&,const FKeyEvent& E)
{
    if(Sim->ExperimentPhase==EExperimentPhase::Resolved)return FReply::Handled();
    const auto Key=E.GetKey();
    if(!E.IsRepeat() && (Key==EKeys::F9 || Key==EKeys::F10)) {if(Key==EKeys::F9)Sim->bCleanDevRendering=true;else Sim->bWorkstationVisible=!Sim->bWorkstationVisible;return FReply::Handled();}
    if(Key==EKeys::Tilde && !E.IsRepeat()) {SuppressPromptChar='`';SetConsoleOpen(!bConsoleOpen);return FReply::Handled();}
    if(Key==EKeys::Escape) {if(bConsoleOpen)SetConsoleOpen(false);else if(!bGrid)ShowGrid();return FReply::Handled().SetUserFocus(AsShared());}
    if(bConsoleOpen) {
        const bool PromptFocus=IsPromptFocused();
        if(Key==EKeys::Tab) {
            if(PromptFocus && !Prompt->GetText().IsEmpty())Autocomplete();
            else {SelectCategory(ActiveCategory+(E.IsShiftDown()?-1:1));FSlateApplication::Get().SetKeyboardFocus(AsShared());}
            return FReply::Handled();
        }
        if(Key==EKeys::Up || Key==EKeys::Down) {
            if(PromptFocus)BrowseHistory(Key==EKeys::Up?-1:1);
            else if(!MenuLabels.IsEmpty()) {HighlightedChoice=(HighlightedChoice+MenuLabels.Num()+(Key==EKeys::Up?-1:1))%MenuLabels.Num();if(MenuEntries.IsValidIndex(HighlightedChoice))MenuScroll->ScrollDescendantIntoView(MenuEntries[HighlightedChoice]);}
            return FReply::Handled();
        }
        if(!PromptFocus && Key==EKeys::Enter){ExecuteHighlighted();return FReply::Handled();}
        if(!PromptFocus && Key==EKeys::BackSpace){Sim->QueueConsoleCommand(TEXT("menu back"));return FReply::Handled();}
        if(!PromptFocus && Key==EKeys::Slash){SuppressPromptChar='/';FSlateApplication::Get().SetKeyboardFocus(Prompt);return FReply::Handled();}
        if(!PromptFocus && E.GetCharacter()>=32 && E.GetCharacter()<127)FSlateApplication::Get().SetKeyboardFocus(Prompt);
        return FReply::Unhandled();
    }
    if(E.IsRepeat())return FReply::Unhandled();
    if(Key==EKeys::Slash){SuppressPromptChar='/';SetConsoleOpen(true);return FReply::Handled();}
    const FKey Keys[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight};
    for(int32 I=0;I<8;++I)if(Key==Keys[I]){SelectFeed(I+1);return FReply::Handled();}
    if(Key==EKeys::G){ShowGrid();return FReply::Handled();}
    return FReply::Unhandled();
}
FReply SDirectOrScutWorkstationWidget::OnKeyDown(const FGeometry& Geometry,const FKeyEvent& E)
{
    if(bConsoleOpen || !Sim->bWorkstationVisible)return FReply::Unhandled();
    const auto Key=E.GetKey();
    if(Key==EKeys::Enter || Key==EKeys::F) {ExpandSelected();return FReply::Handled();}
    if(bGrid && (Key==EKeys::Left || Key==EKeys::Right || Key==EKeys::Up || Key==EKeys::Down))
    {
        int32 Column=SelectedCameraIndex%4,Row=SelectedCameraIndex/4;
        if(Key==EKeys::Left)Column=(Column+3)%4;
        else if(Key==EKeys::Right)Column=(Column+1)%4;
        else Row=1-Row;
        SelectFeed(Row*4+Column+1,false);return FReply::Handled();
    }
    return SCompoundWidget::OnKeyDown(Geometry,E);
}
void SDirectOrScutWorkstationWidget::RefreshMenuChoices()
{
    MenuLabels.Reset();MenuEnabled.Reset();const auto Choices=Sim->ScreenMenuChoices();
    for(int32 I=0;I<Choices.Num();++I) {MenuLabels.Add(FString::Printf(TEXT("[%d] %s%s%s"),I+1,*Choices[I].Label,Choices[I].Detail.IsEmpty()?TEXT(""):TEXT("\n"),*Choices[I].Detail));MenuEnabled.Add(Choices[I].bEnabled);}
}
void SDirectOrScutWorkstationWidget::RebuildMenu()
{
    MenuList->ClearChildren();MenuEntries.Reset();RefreshMenuChoices();HighlightedChoice=0;
    for(int32 I=0;I<MenuLabels.Num();++I) {
        TSharedRef<SWidget> Entry=SNew(SButton).Tag(FName(*FString::Printf(TEXT("ScutMenuChoice%d"),I+1))).IsFocusable(false)
            .ButtonColorAndOpacity_Lambda([this,I](){return HighlightedChoice==I?FLinearColor(.12f,.4f,.2f,.8f):FLinearColor(.02f,.06f,.03f,.35f);})
            .IsEnabled_Lambda([this,I](){return MenuEnabled.IsValidIndex(I) && MenuEnabled[I];})
            .OnClicked_Lambda([this,I](){HighlightedChoice=I;ExecuteHighlighted();return FReply::Handled().SetUserFocus(AsShared());})
            [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).AutoWrapText(true).ColorAndOpacity(FLinearColor(.4f,1,.55f)).Text_Lambda([this,I](){return FText::FromString(MenuLabels.IsValidIndex(I)?MenuLabels[I]:TEXT(""));})];
        MenuEntries.Add(Entry);MenuList->AddSlot().AutoHeight().Padding(0,4)[Entry];
    }
}
void SDirectOrScutWorkstationWidget::Tick(const FGeometry& G,double Now,float Delta)
{
    SCompoundWidget::Tick(G,Now,Delta);if(!Sim.IsValid() || !Rig.IsValid()) return;
    const bool Resolved=Sim->ExperimentPhase==EExperimentPhase::Resolved;
    if(Resolved!=bShowingEvaluation) { bShowingEvaluation=Resolved;bConsoleOpen=false;Rebuild();FSlateApplication::Get().SetKeyboardFocus(AsShared()); }
    if(SeenRuntimeRevision!=Sim->RuntimeRevision) {SeenRuntimeRevision=Sim->RuntimeRevision;bLogAutoScroll=true;CopiedUntil=0;if(bConsoleOpen && !Resolved)SelectCategory(ActiveCategory);}

    if(Sim->RequestedFeed>=0) {SelectFeed(Sim->RequestedFeed,false);Sim->RequestedFeed=-1;if(bGrid && !bConsoleOpen)ToggleExpanded();}
    if(Sim->FollowedCrew>=0 && Sim->CanFollowCrew(Sim->FollowedCrew))
    {
        const int32 Crew=Sim->FollowedCrew,Room=Sim->Simulation.Bodies[Crew].Room;
        if(Crew!=LastFollowedCrew || Room!=LastFollowRoom || SeenFollowRevision!=Sim->FollowRevision)SelectFeed(ACompartmentCameraRig::FeedForRoom(Room),false);
        LastFollowedCrew=Crew;LastFollowRoom=Room;
    }
    else {LastFollowedCrew=-1;LastFollowRoom=-1;}
    SeenFollowRevision=Sim->FollowRevision;
    Retainer->SetVisibility((bShowingEvaluation || Sim->bWorkstationVisible)?EVisibility::Visible:EVisibility::Hidden);
    Rig->SetVisibleFeeds(ActiveFeed,-1,bGrid);
    if(SeenMenuRevision!=Sim->MenuRevision)
    {
        switch(Sim->ActiveMenu) {
        case EShipAIScreenMenu::AI:ActiveCategory=0;break;
        case EShipAIScreenMenu::Events:ActiveCategory=1;break;
        case EShipAIScreenMenu::Crew:ActiveCategory=2;break;
        case EShipAIScreenMenu::DoorRoom:ActiveCategory=3;break;
        case EShipAIScreenMenu::Experiment:ActiveCategory=4;break;
        default:break;
        }
        RebuildMenu();SeenMenuRevision=Sim->MenuRevision;
    }
    MenuRefreshClock+=Delta;
    if(MenuRefreshClock>=.2f) {if(Sim->ActiveMenu!=EShipAIScreenMenu::None) RefreshMenuChoices();MenuRefreshClock=0;}
    if(SeenEvents!=Sim->EventRevision)RefreshLog();
    // Re-issue after layout settles: wrapped output changes the scroll extent on the following frame.
    if(bLogAutoScroll && bConsoleOpen)LogScroll->ScrollToEnd();
}

TSharedRef<SWidget> SDirectOrScutWorkstationWidget::BuildEvaluationDossier()
{
    auto Content=SNew(SVerticalBox);
    auto Text=[&](const FString& Value,int32 Size=12) {
        Content->AddSlot().AutoHeight().Padding(0,5)[SNew(STextBlock).Text(FText::FromString(Value))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)).ColorAndOpacity(FLinearColor(.4f,1,.55f)).AutoWrapText(true)];
    };
    Text(TEXT("CORPORATE EVALUATION DOSSIER // POST-EXPERIMENT"),20);
    if(Sim->ExperimentPhase==EExperimentPhase::Resolved) {
        Text(FString::Printf(TEXT("%s\n%s | DURATION %.2f / 600.00 s | OBJECTIVE %.1f%% | PEAK SUSPICION %.1f%%"),
            Sim->EndingName(Sim->ExperimentEnding),*Sim->Simulation.Scenario.ScenarioId,Sim->Simulation.TickIndex/30.,Sim->Simulation.ObjectiveProgress*100,Sim->PeakSuspicion*100),14);
        Text(TEXT("SUBJECT FINAL METRICS // COMPLIANCE / COMPOSURE / SUSPICION (%)"),13);
        auto Table=SNew(SGridPanel);
        const TCHAR* Headers[]={TEXT("SUBJECT"),TEXT("COMPLIANCE"),TEXT("COMPOSURE"),TEXT("SUSPICION"),TEXT("STRESS"),TEXT("INTEGRITY"),TEXT("BPM"),TEXT("PUPIL mm")};
        for(int32 C=0;C<8;++C)Table->AddSlot(C,0).Padding(8,5)[SNew(STextBlock).Text(FText::FromString(Headers[C])).Font(FCoreStyle::GetDefaultFontStyle("Bold",11)).ColorAndOpacity(FLinearColor(.4f,1,.55f))];
        const TCHAR* Names[]={TEXT("VALE"),TEXT("CHEN"),TEXT("ORTIZ"),TEXT("ASH")};
        for(int32 I=0;I<4;++I) {
            const auto& U=Sim->Simulation.Bodies[I];const auto& P=U.Profile;
            const FString Cells[]={Names[I],FString::Printf(TEXT("%.1f"),P.Compliance*100),FString::Printf(TEXT("%.1f"),P.Composure*100),FString::Printf(TEXT("%.1f"),P.Suspicion*100),FString::Printf(TEXT("%.1f"),U.Stress),FString::Printf(TEXT("%.1f"),U.Integrity),FString::Printf(TEXT("%.1f"),P.HeartRateBPM),FString::Printf(TEXT("%.2f"),P.PupilDilationMM)};
            for(int32 C=0;C<8;++C)Table->AddSlot(C,I+1).Padding(8,5)[SNew(STextBlock).Text(FText::FromString(Cells[C])).Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).ColorAndOpacity(FLinearColor(.7f,1,.75f))];
        }
        Content->AddSlot().AutoHeight()[SNew(SScrollBox).Orientation(Orient_Horizontal)+SScrollBox::Slot()[Table]];
        Text(TEXT("INCIDENT LOG // CHRONOLOGICAL HIGHLIGHTS"),13);
        for(const auto& Cue:Sim->CinematicCues)Text(FString::Printf(TEXT("[%07.2f s] %s | %.2f | %s"),Cue.Tick/30.,DirectOrScut::RoomName(int32(Cue.RoomId)),Cue.Severity,*Cue.Subtitle),11);
    }
    auto Actions=SNew(SHorizontalBox);
    Actions->AddSlot().FillWidth(1).Padding(6)[SNew(SButton).Tag(TEXT("ScutNewExperiment")).Text(FText::FromString(TEXT("[NEW EXPERIMENT]"))).OnClicked_Lambda([this](){Sim->QueueConsoleCommand(TEXT("reset_experiment"));return FReply::Handled().SetUserFocus(AsShared());})];
    Actions->AddSlot().FillWidth(1).Padding(6)[SNew(SButton).Tag(TEXT("ScutSwitchScenario")).Text(FText::FromString(TEXT("[SWITCH SCENARIO]"))).OnClicked_Lambda([this](){Sim->QueueConsoleCommand(Sim->bMedicalScenario?TEXT("scenario behavioral"):TEXT("scenario medical"));return FReply::Handled().SetUserFocus(AsShared());})];
    return SNew(SBorder).Tag(TEXT("ScutEvaluationDossier")).Visibility_Lambda([this](){return Sim->ExperimentPhase==EExperimentPhase::Resolved?EVisibility::Visible:EVisibility::Collapsed;})
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.18f,.8f,.35f)).Padding(2)
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.006f,.018f,.01f)).Padding(18)
            [SNew(SVerticalBox)+SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Content]]+SVerticalBox::Slot().AutoHeight()[Actions]]];
}
