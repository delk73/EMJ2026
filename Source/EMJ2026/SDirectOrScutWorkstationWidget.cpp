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
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

// Keep tiles alive on single selection so Slate can route the second click to the same widget.
class SScutCameraTile final : public SBorder
{
public:
    SLATE_BEGIN_ARGS(SScutCameraTile) {} SLATE_ARGUMENT(TWeakPtr<SDirectOrScutWorkstationWidget>,Owner) SLATE_ARGUMENT(int32,Feed) SLATE_DEFAULT_SLOT(FArguments,Content) SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Owner=Args._Owner; Feed=Args._Feed;
        SetTag(FName(*FString::Printf(TEXT("ScutCamera%d"),Feed)));
        SBorder::Construct(SBorder::FArguments().Padding(2).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor_Lambda([this](){auto W=Owner.Pin();return W && W->ActiveFeed==Feed?FLinearColor(.15f,.8f,.38f):FLinearColor(.035f,.08f,.055f);})[Args._Content.Widget]);
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
    for (int32 F=1;F<=8;++F) { Brushes[F].SetResourceObject(Rig->FeedMaterials[F]?static_cast<UObject*>(Rig->FeedMaterials[F].Get()):static_cast<UObject*>(Rig->Targets[F].Get())); Brushes[F].ImageSize=FVector2D(1024,640); Brushes[F].DrawAs=ESlateBrushDrawType::Image; }
    if (Rig->FeedMaterials[1]) { Glass.Reset(UMaterialInstanceDynamic::Create(Rig->FeedMaterials[1]->Parent,GetTransientPackage())); Glass->SetScalarParameterValue(TEXT("Glass"),1); Glass->SetScalarParameterValue(TEXT("LensK1"),0); }
    ChildSlot[SAssignNew(Retainer,SRetainerWidget)[SNew(SBorder).Padding_Lambda([this](){const FVector2D Size=GetCachedGeometry().GetLocalSize();return FMargin(FMath::Max(16.f,float(Size.X)*.035f),FMath::Max(18.f,float(Size.Y)*.04f));}).BorderBackgroundColor(FLinearColor(.015f,.025f,.02f,1))[SAssignNew(Root,SVerticalBox)]]];
    if (Glass.IsValid()) { Retainer->SetEffectMaterial(Glass.Get()); Retainer->SetTextureParameter(TEXT("Texture")); }
    Sim->LogEvent(TEXT("Private uplink established. Crew sees only automated Ship AI protocols."));
    Sim->LogEvent(Sim->SubmitDirective(TEXT("status")));
    Sim->LogEvent(TEXT("Backtick: console | Shift+backtick: full console | Enter / slash: command | Up/Down: history | Tab: autocomplete | help: reference"));
    Sim->LogEvent(TEXT("Crew: Vale / engineer, Chen / medic, Ortiz / technician, Ash / android. Commands use full room names, e.g. direct ash MED_BAY telemetry; alert AIRLOCK_CHAMBER fault."));
    Rebuild();
}
TSharedRef<SWidget> SDirectOrScutWorkstationWidget::GetContentRoot() const { return Root.ToSharedRef(); }
bool SDirectOrScutWorkstationWidget::IsPromptFocused() const { return Prompt.IsValid() && (Prompt->HasKeyboardFocus() || Prompt->HasFocusedDescendants()); }
TSharedRef<SWidget> SDirectOrScutWorkstationWidget::Video(int32 F)
{
    const FString Label=FString::Printf(TEXT("%d // %s"),F,DirectOrScut::RoomName(ACompartmentCameraRig::RoomForFeed(F)));
    return SNew(SScutCameraTile).Tag(FName(*FString::Printf(TEXT("ScutCamera%d"),F))).Owner(SharedThis(this)).Feed(F)
    [SNew(SBorder).Padding(3).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor::Black).Visibility(EVisibility::HitTestInvisible)
        [SNew(SOverlay)+SOverlay::Slot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SBox).WidthOverride(1024).HeightOverride(640)[SNew(SImage).Image(&Brushes[F])]]]
        +SOverlay::Slot().VAlign(VAlign_Top)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).Text(FText::FromString(Label)).AutoWrapText(true).ColorAndOpacity(FLinearColor(.18f,1,.42f))]]];
}
void SDirectOrScutWorkstationWidget::SelectFeed(int32 F,bool AllowDoubleTap)
{
    if (F<1 || F>8) return;
    const double Now=FPlatformTime::Seconds();
    const bool DoubleTap=AllowDoubleTap && LastFeed==F && Now-LastTap<.3;
    LastFeed=AllowDoubleTap?F:-1; LastTap=Now;
    const bool Changed=ActiveFeed!=F; ActiveFeed=F;
    if (DoubleTap) ToggleExpanded();
    else if (Changed && !bGrid) Rebuild();
}
void SDirectOrScutWorkstationWidget::ToggleExpanded() { bFullscreen=!bFullscreen; bGrid=!bFullscreen; Rebuild(); }
void SDirectOrScutWorkstationWidget::ShowGrid() { bGrid=true; bFullscreen=false; LastFeed=-1; Rebuild(); }
float SDirectOrScutWorkstationWidget::DrawerHeight() const
{
    const float Height=Body.IsValid()?float(Body->GetCachedGeometry().GetLocalSize().Y):float(GetCachedGeometry().GetLocalSize().Y);
    return Height*(bFullConsole?.90f:.55f);
}
void SDirectOrScutWorkstationWidget::SetConsoleOpen(bool Open,bool Full)
{
    bConsoleOpen=Open;
    if(Open) { bFullConsole=Full; FSlateApplication::Get().SetKeyboardFocus(Prompt,EFocusCause::SetDirectly); }
    else FSlateApplication::Get().SetKeyboardFocus(AsShared(),EFocusCause::SetDirectly);
}
void SDirectOrScutWorkstationWidget::Rebuild()
{
    const FText Draft=Prompt.IsValid()?Prompt->GetText():FText::GetEmpty();
    Root->ClearChildren();
    Root->AddSlot().AutoHeight().Padding(0,0,0,8)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).Text(FText::FromString(TEXT("DIRECT OR SCUT // DIRECTOR: RIDLEY SCUT // PRIVATE SHIP AI UPLINK"))).AutoWrapText(true).ColorAndOpacity(FLinearColor(.18f,1,.42f))];
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
    MenuContent->AddSlot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(MenuList,SVerticalBox)]];
    auto MenuPanel=SNew(SBox).WidthOverride_Lambda([this](){return Body.IsValid()?float(Body->GetCachedGeometry().GetLocalSize().X)*.90f:0.f;}).HeightOverride_Lambda([this](){const float H=Body.IsValid()?float(Body->GetCachedGeometry().GetLocalSize().Y)*.86f:0.f;return Sim->ActiveMenu==EShipAIScreenMenu::Crew?FMath::Min(H,260.f):H;})
        .Visibility_Lambda([this](){return Sim.IsValid() && Sim->ActiveMenu!=EShipAIScreenMenu::None?EVisibility::Visible:EVisibility::Collapsed;})
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.1f,.6f,.28f)).Padding(2)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.008f,.025f,.015f,.98f)).Padding(14)[MenuContent]]];
    auto Drawer=SNew(SBox).Tag(TEXT("ScutConsoleDrawer")).HeightOverride_Lambda([this](){return DrawerHeight()*DrawerSlide;}).Clipping(EWidgetClipping::ClipToBounds)
        .Visibility_Lambda([this](){return bConsoleOpen || DrawerSlide>.001f?EVisibility::Visible:EVisibility::Collapsed;}).IsEnabled_Lambda([this](){return bConsoleOpen;})
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.1f,.6f,.28f,.3f)).Padding(2)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.006f,.018f,.01f,.9f)).Padding(12)[Terminal]]];
    Root->AddSlot().FillHeight(1)[SAssignNew(Body,SOverlay).Clipping(EWidgetClipping::ClipToBounds)
        +SOverlay::Slot()[Main]
        +SOverlay::Slot().Padding(18).HAlign(HAlign_Center).VAlign(VAlign_Center)[MenuPanel]
        +SOverlay::Slot().VAlign(VAlign_Top)[Drawer]];
    Root->AddSlot().AutoHeight().Padding(0,6,0,0)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",10)).AutoWrapText(true).ColorAndOpacity(FLinearColor(.18f,.7f,.35f)).Text(FText::FromString(TEXT("1-8 SELECT // F INSPECT // TAB / G GRID // ` CONSOLE // ENTER COMMAND")))];
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
    if(Key==EKeys::Enter || Key==EKeys::Slash) {SuppressPromptChar=Key==EKeys::Slash?'/':0;SetConsoleOpen(true);return FReply::Handled();}
    const FKey Keys[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight,EKeys::Nine};
    for(int32 I=0;I<9;++I) if(Key==Keys[I])
    {
        if(Sim->ActiveMenu!=EShipAIScreenMenu::None) Sim->QueueConsoleCommand(FString::Printf(TEXT("choose %d"),I+1));
        else if(I<8) SelectFeed(I+1);
        return FReply::Handled();
    }
    if(Sim->ActiveMenu!=EShipAIScreenMenu::None) return FReply::Handled();
    if(Key==EKeys::Tab || Key==EKeys::G || Key==EKeys::F) {ToggleExpanded();return FReply::Handled();}
    return FReply::Unhandled();
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
        for(int32 I=0;I<4;++I) for(int32 C=0;C<8;++C) Table->AddSlot(C,I+1).Padding(6)[SNew(STextBlock).ColorAndOpacity(FLinearColor(.18f,1,.42f)).Text_Lambda([this,I,C](){
            const auto& U=Sim->Simulation.Bodies[I];const auto& P=U.Profile;const TCHAR* Names[]={TEXT("VALE"),TEXT("CHEN"),TEXT("ORTIZ"),TEXT("ASH / ANDROID")};
            if(C==0) return FText::FromString(Names[I]);
            if(C==7) return FText::FromString(FString::Printf(TEXT("%s / %s"),DirectOrScut::RoomName(U.Room),DirectOrScutStateName(U.State)));
            const double Values[]={P.Compliance,P.Composure,P.Affinity,P.Utility,P.Suspicion,U.Stress};return FText::FromString(FString::Printf(TEXT("%.2f"),Values[C-1]));
        })];
        MenuList->AddSlot().AutoHeight()[SNew(SScrollBox).Orientation(Orient_Horizontal)+SScrollBox::Slot()[Table]];
    }
    else for(int32 I=0;I<MenuLabels.Num();++I) MenuList->AddSlot().AutoHeight().Padding(0,3)
        [SNew(SButton).Tag(FName(*FString::Printf(TEXT("ScutMenuChoice%d"),I+1))).IsFocusable(false).IsEnabled_Lambda([this,I](){return MenuEnabled.IsValidIndex(I) && MenuEnabled[I];})
            .OnClicked_Lambda([this,I](){Sim->QueueConsoleCommand(FString::Printf(TEXT("choose %d"),I+1));return FReply::Handled().SetUserFocus(AsShared(),EFocusCause::Mouse);})
            [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).AutoWrapText(true).ColorAndOpacity(FLinearColor(.18f,1,.42f)).Text_Lambda([this,I](){return FText::FromString(MenuLabels.IsValidIndex(I)?MenuLabels[I]:TEXT(""));})]];
}
void SDirectOrScutWorkstationWidget::Tick(const FGeometry& G,double Now,float Delta)
{
    SCompoundWidget::Tick(G,Now,Delta);if(!Sim.IsValid() || !Rig.IsValid()) return;
    DrawerSlide=FMath::FInterpConstantTo(DrawerSlide,bConsoleOpen?1.f:0.f,Delta,7.f);
    if(Sim->RequestedFeed>=0) {SelectFeed(Sim->RequestedFeed,false);Sim->RequestedFeed=-1;}
    Rig->SetVisibleFeeds(ActiveFeed,-1,bGrid);
    if(Glass.IsValid()) Glass->SetScalarParameterValue(TEXT("Clock"),Now);
    if(SeenMenuRevision!=Sim->MenuRevision)
    {
        if(SeenMenuRevision!=MAX_uint64)
        {
            SetConsoleOpen(false);
            if(Sim->ActiveMenu==EShipAIScreenMenu::None) ShowGrid();
        }
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
