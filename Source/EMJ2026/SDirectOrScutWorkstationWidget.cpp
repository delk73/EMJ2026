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

// PIE editor bindings consume F1 before normal widget routing. Claim tactical keys
// only while this workstation owns focus, before editor command processing.
class FScutTacticalInput final : public IInputProcessor
{
public:
    TWeakPtr<SDirectOrScutWorkstationWidget> Owner;
    virtual void Tick(float,FSlateApplication&,TSharedRef<ICursor>) override {}
    virtual bool HandleKeyDownEvent(FSlateApplication&,const FKeyEvent& E) override {
        auto W=Owner.Pin();if(!W || !(W->HasKeyboardFocus() || W->HasFocusedDescendants()))return false;
        const auto K=E.GetKey();if(K!=EKeys::F1 && K!=EKeys::F2 && K!=EKeys::F3 && K!=EKeys::F4 && K!=EKeys::F9 && K!=EKeys::F10)return false;
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
    ChildSlot[SAssignNew(Retainer,SRetainerWidget)[SNew(SBorder).Padding_Lambda([this](){const FVector2D Size=GetCachedGeometry().GetLocalSize();return FMargin(FMath::Max(16.f,float(Size.X)*.035f),FMath::Max(18.f,float(Size.Y)*.04f));}).BorderBackgroundColor(FLinearColor(.015f,.025f,.02f,1))[SAssignNew(Root,SVerticalBox)]]];
    Sim->LogEvent(TEXT("Private uplink established. Crew sees only automated Ship AI protocols."));
    Sim->LogEvent(Sim->SubmitDirective(TEXT("status")));
    Sim->LogEvent(TEXT("Backtick: console | Shift+backtick: full console | Enter / F: inspect | slash: command | Up/Down: history | Tab: autocomplete | help: reference"));
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
float SDirectOrScutWorkstationWidget::DrawerHeight() const
{
    const float Height=Body.IsValid()?float(Body->GetCachedGeometry().GetLocalSize().Y):float(GetCachedGeometry().GetLocalSize().Y);
    return Height*(bFullConsole?.90f:.42f);
}
void SDirectOrScutWorkstationWidget::SetConsoleOpen(bool Open,bool Full)
{
    bConsoleOpen=Open;
    if(Open) { if(Sim->ActiveMenu!=EShipAIScreenMenu::None)Sim->QueueConsoleCommand(TEXT("menu close")); bFullConsole=Full; FSlateApplication::Get().SetKeyboardFocus(Prompt,EFocusCause::SetDirectly); }
    else FSlateApplication::Get().SetKeyboardFocus(AsShared(),EFocusCause::SetDirectly);
}
void SDirectOrScutWorkstationWidget::Rebuild()
{
    const FText Draft=Prompt.IsValid()?Prompt->GetText():FText::GetEmpty();
    Root->ClearChildren();
    Root->AddSlot().AutoHeight().Padding(0,0,0,8)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).ColorAndOpacity_Lambda([this](){float V=Sim->Simulation.CollectiveSuspicion();return V>.65f?FLinearColor(1,.2f,.12f):V>.3f?FLinearColor(1,.7f,.1f):FLinearColor(.2f,1,.4f);}).Text_Lambda([this](){const auto& M=Sim->Simulation;int32 T=int32(M.TickIndex/30),D=int32(M.Scenario.ShiftDuration),Alerts=0;for(const auto& R:M.Rooms) Alerts+=R.AlertSeconds>0 || !R.bIsPressurized;return FText::FromString(FString::Printf(TEXT("SHIFT TIME %02d:%02d / %02d:%02d  |  OBJECTIVE %.0f%%  |  GLOBAL SUSPICION %.0f%%  |  ACTIVE ALERTS %d  |  ACTIVE SCENARIO %s"),T/60,T%60,D/60,D%60,M.ObjectiveProgress*100,M.CollectiveSuspicion()*100,Alerts,*M.Scenario.ScenarioId));})];
    Root->AddSlot().AutoHeight()[SNew(SHorizontalBox)
        .Visibility_Lambda([this](){return Sim->FollowedCrew>=0?EVisibility::Visible:EVisibility::Collapsed;})
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Tag(TEXT("ScutFollowIndicator")).Font(FCoreStyle::GetDefaultFontStyle("Bold",12)).ColorAndOpacity(FLinearColor(.25f,1,.08f)).Text_Lambda([this](){return FText::FromString(Sim->FollowStatus()+TEXT(" // "));})]
        +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Tag(TEXT("ScutStopFollow")).IsFocusable(false).Text(FText::FromString(TEXT("[STOP FOLLOW]"))).OnClicked_Lambda([this](){Sim->QueueConsoleCommand(TEXT("follow off"));return FReply::Handled().SetUserFocus(AsShared());})]];
    Root->AddSlot().AutoHeight()[SNew(SButton).Tag(TEXT("ScutBackToGrid")).Visibility(bGrid?EVisibility::Collapsed:EVisibility::Visible).IsFocusable(false).Text(FText::FromString(TEXT("[BACK TO 8-CAM GRID (G / ESC)]"))).OnClicked_Lambda([this](){ShowGrid();return FReply::Handled().SetUserFocus(AsShared());})];
    TSharedRef<SWidget> Main=Video(ActiveFeed);
    if(bGrid)
    {
        auto Grid=SNew(SUniformGridPanel).SlotPadding(3);
        for(int32 F=1;F<=8;++F) Grid->AddSlot((F-1)%4,(F-1)/4)[Video(F)];
        Main=Grid;
    }
    auto Terminal=SNew(SVerticalBox);
    Terminal->AddSlot().AutoHeight()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).Text(FText::FromString(TEXT("SHIP AI // PRIVATE COMMAND DRAWER // help FOR REFERENCE"))).AutoWrapText(true).ColorAndOpacity(FLinearColor(1,.69f,.12f))];
    Terminal->AddSlot().FillHeight(1).Padding(0,8)[SAssignNew(LogScroll,SScrollBox)+SScrollBox::Slot()[SAssignNew(Log,SVerticalBox)]];
    Terminal->AddSlot().AutoHeight()[SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).Text_Lambda([this](){return FText::FromString(Sim.IsValid() && Sim->bAILink?TEXT("AI-LINK> "):TEXT("CMD> "));}).ColorAndOpacity(FLinearColor(.18f,1,.42f))]
        +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(Prompt,SEditableTextBox).Tag(TEXT("ScutPrompt")).Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).ForegroundColor(FLinearColor(.18f,1,.42f)).Text(Draft).ClearKeyboardFocusOnCommit(false)
        .HintText(FText::FromString(TEXT("help | menu ai | menu events | menu crew")))
        .OnKeyCharHandler_Lambda([this](const FGeometry&,const FCharacterEvent& E){const bool Suppress=SuppressPromptChar && (E.GetCharacter()==SuppressPromptChar || (SuppressPromptChar=='`' && E.GetCharacter()=='~'));SuppressPromptChar=0;return Suppress?FReply::Handled():FReply::Unhandled();})
        .OnTextCommitted(this,&SDirectOrScutWorkstationWidget::Commit)]];
    auto MenuContent=SNew(SVerticalBox);
    MenuContent->AddSlot().AutoHeight()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).AutoWrapText(true).ColorAndOpacity(FLinearColor(.18f,1,.42f)).Text_Lambda([this](){return FText::FromString(Sim->ScreenMenuTitle());})];
    MenuContent->AddSlot().AutoHeight().Padding(0,6)[SNew(STextBlock).Text(FText::FromString(TEXT("NUMBER KEYS / CLICK: choose  //  ESC: close  //  `: command drawer"))).AutoWrapText(true).ColorAndOpacity(FLinearColor(1,.69f,.12f))];
    MenuContent->AddSlot().AutoHeight()[SNew(SButton).Text(FText::FromString(TEXT("[< BACK]"))).OnClicked_Lambda([this](){Sim->QueueConsoleCommand(TEXT("menu back"));return FReply::Handled().SetUserFocus(AsShared());})];
    MenuContent->AddSlot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(MenuList,SVerticalBox)]];
    auto MenuPanel=SNew(SBox).WidthOverride_Lambda([this](){return Body.IsValid()?float(Body->GetCachedGeometry().GetLocalSize().X)*.66f:0.f;}).HeightOverride_Lambda([this](){const float H=Body.IsValid()?float(Body->GetCachedGeometry().GetLocalSize().Y)*.65f:0.f;return Sim->ActiveMenu==EShipAIScreenMenu::Crew?FMath::Min(H,300.f):H;})
        .Visibility_Lambda([this](){return Sim.IsValid() && Sim->ActiveMenu!=EShipAIScreenMenu::None?EVisibility::Visible:EVisibility::Collapsed;})
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.1f,.6f,.28f)).Padding(2)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.008f,.025f,.015f,.98f)).Padding(14)[MenuContent]]];
    auto Drawer=SNew(SBox).Tag(TEXT("ScutConsoleDrawer")).HeightOverride_Lambda([this](){return DrawerHeight()*DrawerSlide;}).Clipping(EWidgetClipping::ClipToBounds)
        .Visibility_Lambda([this](){return bConsoleOpen || DrawerSlide>.001f?EVisibility::Visible:EVisibility::Collapsed;}).IsEnabled_Lambda([this](){return bConsoleOpen;})
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.1f,.6f,.28f,.3f)).Padding(2)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.006f,.018f,.01f,.92f)).Padding(12)[Terminal]]];
    Root->AddSlot().FillHeight(1)[SAssignNew(Body,SOverlay).Clipping(EWidgetClipping::ClipToBounds)
        +SOverlay::Slot()[Main]
        +SOverlay::Slot().Padding(18).HAlign(HAlign_Right).VAlign(VAlign_Bottom)[MenuPanel]
        +SOverlay::Slot().VAlign(VAlign_Top)[Drawer]];
    auto Dock=SNew(SHorizontalBox);
    const TCHAR* Labels[]={TEXT("[F1] AI DIRECTIVES"),TEXT("[F2] SCENARIO EVENTS"),TEXT("[F3] CREW MATRIX"),TEXT("[F4] BULKHEADS"),TEXT("[~] CONSOLE"),TEXT("[G] 8-CAM GRID"),TEXT("[ENTER / F] INSPECT")};
    const TCHAR* Commands[]={TEXT("menu ai"),TEXT("menu events"),TEXT("menu crew"),TEXT("menu doors")};
    for(int32 I=0;I<7;++I) {const FString Command=I<4?Commands[I]:TEXT("");Dock->AddSlot().FillWidth(1).Padding(2)[SNew(SButton).Text(FText::FromString(Labels[I])).OnClicked_Lambda([this,I,Command](){if(I<4){SetConsoleOpen(false);Sim->QueueConsoleCommand(Command);}else if(I==4)SetConsoleOpen(!bConsoleOpen);else if(I==5)ShowGrid();else ExpandSelected();return FReply::Handled().SetUserFocus(bConsoleOpen?StaticCastSharedRef<SWidget>(Prompt.ToSharedRef()):AsShared());})];}
    Root->AddSlot().AutoHeight().Padding(0,6,0,0)[Dock];
    Root->AddSlot().AutoHeight()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",10)).ColorAndOpacity(FLinearColor(.6f,.9f,.7f)).Text_Lambda([this](){FString Status=Sim->Events.IsEmpty()?TEXT("UPLINK READY"):Sim->Events.Last().Text;int32 End;if(Status.FindChar('\n',End))Status=Status.Left(End);return FText::FromString(Status.Left(180));})];
    SeenEvents=MAX_uint64; SeenMenuRevision=MAX_uint64;
    if(bConsoleOpen) FSlateApplication::Get().SetKeyboardFocus(Prompt,EFocusCause::SetDirectly);
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
    const auto Key=E.GetKey();
    if(!E.IsRepeat() && (Key==EKeys::F9 || Key==EKeys::F10)) { if(Key==EKeys::F9)Sim->bCleanDevRendering=true;else Sim->bWorkstationVisible=!Sim->bWorkstationVisible;return FReply::Handled(); }
    const FKey MenuKeys[]={EKeys::F1,EKeys::F2,EKeys::F3,EKeys::F4};const TCHAR* Menus[]={TEXT("menu ai"),TEXT("menu events"),TEXT("menu crew"),TEXT("menu doors")};
    for(int32 I=0;I<4;++I)if(Key==MenuKeys[I] && !E.IsRepeat()){SetConsoleOpen(false);Sim->QueueConsoleCommand(Menus[I]);return FReply::Handled().SetUserFocus(AsShared());}
    if(Key==EKeys::BackSpace && !bConsoleOpen && Sim->ActiveMenu!=EShipAIScreenMenu::None){Sim->QueueConsoleCommand(TEXT("menu back"));return FReply::Handled();}
    if(Key==EKeys::Tilde && !E.IsRepeat())
    {
        SuppressPromptChar='`';
        if(E.IsShiftDown()) SetConsoleOpen(true,!bFullConsole);
        else SetConsoleOpen(!bConsoleOpen,false);
        return FReply::Handled();
    }
    if(Key==EKeys::Escape)
    {
        if(bConsoleOpen) SetConsoleOpen(false);
        else if(Sim->ActiveMenu!=EShipAIScreenMenu::None) {Sim->QueueConsoleCommand(TEXT("menu close"));ShowGrid();}
        else if(!bGrid) ShowGrid();
        return FReply::Handled().SetUserFocus(AsShared(),EFocusCause::SetDirectly);
    }
    if(bConsoleOpen)
    {
        if(Key==EKeys::Up) {BrowseHistory(-1);return FReply::Handled();}
        if(Key==EKeys::Down) {BrowseHistory(1);return FReply::Handled();}
        if(Key==EKeys::Tab) {Autocomplete();return FReply::Handled();}
        if(!IsPromptFocused()) FSlateApplication::Get().SetKeyboardFocus(Prompt,EFocusCause::SetDirectly);
        return FReply::Unhandled();
    }
    if(E.IsRepeat()) return FReply::Handled();
    if(Key==EKeys::Slash) {SuppressPromptChar='/';SetConsoleOpen(true);return FReply::Handled();}
    const FKey Keys[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight,EKeys::Nine};
    for(int32 I=0;I<9;++I) if(Key==Keys[I])
    {
        if(Sim->ActiveMenu!=EShipAIScreenMenu::None) Sim->QueueConsoleCommand(FString::Printf(TEXT("choose %d"),I+1));
        else if(I<8) SelectFeed(I+1);
        return FReply::Handled();
    }
    if(Sim->ActiveMenu!=EShipAIScreenMenu::None) return FReply::Handled();
    if(Key==EKeys::Tab || Key==EKeys::G) {ShowGrid();return FReply::Handled();}
    return FReply::Unhandled();
}
FReply SDirectOrScutWorkstationWidget::OnKeyDown(const FGeometry& Geometry,const FKeyEvent& E)
{
    if(bConsoleOpen || Sim->ActiveMenu!=EShipAIScreenMenu::None || !Sim->bWorkstationVisible)return FReply::Unhandled();
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
    MenuList->ClearChildren();RefreshMenuChoices();
    if(Sim->ActiveMenu==EShipAIScreenMenu::Crew)
    {
        auto Table=SNew(SGridPanel);const TCHAR* Headings[]={TEXT("CREW"),TEXT("COMPLIANCE"),TEXT("COMPOSURE"),TEXT("AFFINITY"),TEXT("UTILITY"),TEXT("SUSPICION"),TEXT("STRESS"),TEXT("ROOM / STATE")};
        for(int32 C=0;C<8;++C) Table->AddSlot(C,0).Padding(6)[SNew(STextBlock).Text(FText::FromString(Headings[C])).ColorAndOpacity(FLinearColor(1,.69f,.12f))];
        for(int32 I=0;I<4;++I) {
            for(int32 C=0;C<8;++C) {
                auto Cell=SNew(SVerticalBox);
                if(C==0)Cell->AddSlot().AutoHeight()[SNew(SButton).IsFocusable(false).IsEnabled_Lambda([this,I](){return Sim->CanFollowCrew(I);}).Tag(FName(*FString::Printf(TEXT("ScutDossierName%d"),I)))
                    .Text(FText::FromString(I==0?TEXT("VALE"):I==1?TEXT("CHEN"):I==2?TEXT("ORTIZ"):TEXT("ASH")))
                    .OnClicked_Lambda([this,I](){Sim->QueueConsoleCommand(FString::Printf(TEXT("choose %d"),I+1));return FReply::Handled().SetUserFocus(AsShared());})];
                Cell->AddSlot().AutoHeight()[SNew(STextBlock).Visibility(C==0?EVisibility::Collapsed:EVisibility::Visible).ColorAndOpacity(FLinearColor(.2f,1,.4f)).Text_Lambda([this,I,C](){const auto& U=Sim->Simulation.Bodies[I];const auto& P=U.Profile;const TCHAR* Names[]={TEXT("VALE"),TEXT("CHEN"),TEXT("ORTIZ"),TEXT("ASH")};if(C==0)return FText::FromString(Names[I]);if(C==7)return FText::FromString(FString::Printf(TEXT("%s / %s\n%.0f BPM | %.0f RPM | %.1f mm"),DirectOrScut::RoomName(U.Room),DirectOrScutStateName(U.State),P.HeartRateBPM,P.RespirationRateRPM,P.PupilDilationMM));const double V[]={P.Compliance*100,P.Composure*100,P.Affinity*100,P.Utility*100,P.Suspicion*100,U.Stress};return FText::FromString(FString::Printf(TEXT("%.0f%%"),V[C-1]));})];
                if(C>0 && C<7)Cell->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(6)[SNew(SProgressBar).Percent_Lambda([this,I,C](){const auto& U=Sim->Simulation.Bodies[I];const auto& P=U.Profile;float V[]={P.Compliance,P.Composure,(P.Affinity+1)*.5f,P.Utility,P.Suspicion,float(U.Stress/100)};return TOptional<float>(V[C-1]);}).FillColorAndOpacity(C>=5?FLinearColor(1,.3f,.15f):FLinearColor(.1f,.8f,.35f))]];
                if(C==7)Cell->AddSlot().AutoHeight()[SNew(SButton).IsFocusable(false).IsEnabled_Lambda([this,I](){return Sim->CanFollowCrew(I);}).Tag(FName(*FString::Printf(TEXT("ScutDossierFollow%d"),I))).Text(FText::FromString(FString::Printf(TEXT("[%d] FOLLOW CREW"),I+1))).OnClicked_Lambda([this,I](){Sim->QueueConsoleCommand(FString::Printf(TEXT("choose %d"),I+1));return FReply::Handled().SetUserFocus(AsShared());})];
                Table->AddSlot(C,I+1).Padding(4)[Cell];
            }
        }
        MenuList->AddSlot().AutoHeight()[SNew(SScrollBox).Orientation(Orient_Horizontal)+SScrollBox::Slot()[Table]];
    }
    else for(int32 I=0;I<MenuLabels.Num();++I) {
        auto Entry=SNew(SVerticalBox);
        Entry->AddSlot().AutoHeight()[SNew(SButton).Tag(FName(*FString::Printf(TEXT("ScutMenuChoice%d"),I+1))).IsFocusable(false).IsEnabled_Lambda([this,I](){return MenuEnabled.IsValidIndex(I) && MenuEnabled[I];})
            .OnClicked_Lambda([this,I](){Sim->QueueConsoleCommand(FString::Printf(TEXT("choose %d"),I+1));return FReply::Handled().SetUserFocus(AsShared(),EFocusCause::Mouse);})
            [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).AutoWrapText(true).ColorAndOpacity(FLinearColor(.18f,1,.42f)).Text_Lambda([this,I](){return FText::FromString(MenuLabels.IsValidIndex(I)?MenuLabels[I]:TEXT(""));})]];
        if(Sim->ActiveMenu==EShipAIScreenMenu::Events) {
            auto Badges=SNew(SHorizontalBox);
            for(int32 B=0;B<5;++B) {
                auto Delta=[this,I,B](){auto Choices=Sim->ScreenMenuChoices();if(!Choices.IsValidIndex(I))return FString();FString Line=Choices[I].Detail;int32 End;if(Line.FindChar('\n',End))Line=Line.Left(End);Line.RemoveFromStart(TEXT("Immediate totals: "));TArray<FString> Parts;Line.ParseIntoArray(Parts,TEXT(" | "));return Parts.IsValidIndex(B)?Parts[B]:FString();};
                Badges->AddSlot().AutoWidth().Padding(3)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.01f,.03f,.02f)).Padding(3)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",10)).Text_Lambda([Delta](){return FText::FromString(Delta());}).ColorAndOpacity_Lambda([Delta,B](){const bool Negative=Delta().Contains(TEXT("-"));const bool Good=B<2?Negative:!Negative;return Good?FLinearColor(.2f,1,.4f):FLinearColor(1,.3f,.15f);})]];
            }
            Entry->AddSlot().AutoHeight()[Badges];
        }
        MenuList->AddSlot().AutoHeight().Padding(0,3)[Entry];
    }
}
void SDirectOrScutWorkstationWidget::Tick(const FGeometry& G,double Now,float Delta)
{
    SCompoundWidget::Tick(G,Now,Delta);if(!Sim.IsValid() || !Rig.IsValid()) return;
    DrawerSlide=FMath::FInterpConstantTo(DrawerSlide,bConsoleOpen?1.f:0.f,Delta,7.f);
    if(Sim->RequestedFeed>=0) {SelectFeed(Sim->RequestedFeed,false);Sim->RequestedFeed=-1;if(bGrid)ToggleExpanded();}
    if(Sim->FollowedCrew>=0 && Sim->CanFollowCrew(Sim->FollowedCrew))
    {
        const int32 Crew=Sim->FollowedCrew,Room=Sim->Simulation.Bodies[Crew].Room;
        if(Crew!=LastFollowedCrew || Room!=LastFollowRoom || SeenFollowRevision!=Sim->FollowRevision)SelectFeed(ACompartmentCameraRig::FeedForRoom(Room),false);
        LastFollowedCrew=Crew;LastFollowRoom=Room;
    }
    else {LastFollowedCrew=-1;LastFollowRoom=-1;}
    SeenFollowRevision=Sim->FollowRevision;
    Retainer->SetVisibility(Sim->bWorkstationVisible?EVisibility::Visible:EVisibility::Hidden);
    Rig->SetVisibleFeeds(ActiveFeed,-1,bGrid);
    if(SeenMenuRevision!=Sim->MenuRevision)
    {
        if(Sim->ActiveMenu!=EShipAIScreenMenu::None) { SetConsoleOpen(false); DrawerSlide=0; }
        RebuildMenu();SeenMenuRevision=Sim->MenuRevision;
    }
    MenuRefreshClock+=Delta;
    if(MenuRefreshClock>=.2f) {if(Sim->ActiveMenu!=EShipAIScreenMenu::None) RefreshMenuChoices();MenuRefreshClock=0;}
    if(SeenEvents!=Sim->EventRevision)
    {
        Log->ClearChildren();const int32 Start=FMath::Max(0,Sim->Events.Num()-48);
        for(int32 I=Start;I<Sim->Events.Num();++I)
        {
            const auto Event=Sim->Events[I];
            Log->AddSlot().AutoHeight()[SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").Tag(FName(*FString::Printf(TEXT("ScutEvent%d"),I))).IsFocusable(false).OnClicked_Lambda([this,Event](){if(Event.Room>=0){SetConsoleOpen(false);SelectFeed(ACompartmentCameraRig::FeedForRoom(Event.Room),false);if(bGrid)ToggleExpanded();}return FReply::Handled().SetUserFocus(AsShared(),EFocusCause::Mouse);})
                [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).Text(FText::FromString(Event.Text)).AutoWrapText(true).ColorAndOpacity(FLinearColor(.18f,1,.42f))]];
        }
        LogScroll->ScrollToEnd();SeenEvents=Sim->EventRevision;
    }
}
