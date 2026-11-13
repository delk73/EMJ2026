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
        if (auto W=Owner.Pin()) { W->SelectFeed(Feed,false); return FReply::Handled().SetUserFocus(W.ToSharedRef(),EFocusCause::Mouse); }
        return FReply::Unhandled();
    }
    virtual FReply OnMouseButtonDoubleClick(const FGeometry&,const FPointerEvent& E) override
    {
        if (E.GetEffectingButton()!=EKeys::LeftMouseButton) return FReply::Unhandled();
        if (auto W=Owner.Pin()) { W->SelectFeed(Feed,false); W->ToggleExpanded(); return FReply::Handled().SetUserFocus(W.ToSharedRef(),EFocusCause::Mouse); }
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
    Sim->LogEvent(TEXT("1-8: select | double click / double tap / F: expand | Tab / G / Esc: grid | Enter / slash: prompt | Esc: hotkeys"));
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
void SDirectOrScutWorkstationWidget::Rebuild()
{
    const FText Draft=Prompt.IsValid()?Prompt->GetText():FText::GetEmpty();
    const bool HadFocus=IsPromptFocused();
    Root->ClearChildren();
    Root->AddSlot().AutoHeight().Padding(0,0,0,8)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).Text(FText::FromString(TEXT("DIRECT OR SCUT // DIRECTOR: RIDLEY SCUT // PRIVATE SHIP AI UPLINK"))).AutoWrapText(true).ColorAndOpacity(FLinearColor(.18f,1,.42f))];
    TSharedRef<SWidget> Main=Video(ActiveFeed);
    if (bGrid)
    {
        auto Grid=SNew(SUniformGridPanel).SlotPadding(3);
        for (int32 F=1;F<=8;++F) Grid->AddSlot((F-1)%3,(F-1)/3)[Video(F)];
        Grid->AddSlot(2,2)[SNew(SBorder).Tag(TEXT("ScutTelemetry")).Padding(8).BorderBackgroundColor(FLinearColor(.025f,.055f,.035f))
            [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).AutoWrapText(true).ColorAndOpacity(FLinearColor(.18f,1,.42f)).Text_Lambda([this](){
                const auto& M=Sim->Simulation; int32 Alerts=0; for(const auto& Room:M.Rooms) Alerts+=Room.AlertSeconds>0?1:0;
                return FText::FromString(FString::Printf(TEXT("SHIP TELEMETRY // ALERT MONITOR\n\nSHIFT %.1f / %.0fs\nOBJECTIVE %.0f%%\nCREW SUSPICION %.0f%%\nACTIVE ALERTS %d\n\nSELECTED CAMERA %d\nF: inspect | Tab / G: grid"),M.TickIndex/30.,M.Scenario.ShiftDuration,M.ObjectiveProgress*100,M.CollectiveSuspicion()*100,Alerts,ActiveFeed));
            })]];
        Main=Grid;
    }
    auto Console=SNew(SVerticalBox);
    Console->AddSlot().AutoHeight().Padding(0,0,0,6)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).Text(FText::FromString(TEXT("SHIP AI // CREW CHANNEL MASKED"))).AutoWrapText(true).ColorAndOpacity(FLinearColor(1,.69f,.12f))];
    Console->AddSlot().FillHeight(1)[SAssignNew(LogScroll,SScrollBox)+SScrollBox::Slot()[SAssignNew(Log,SVerticalBox)]];
    Root->AddSlot().FillHeight(1)[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(.68f)[Main]+SHorizontalBox::Slot().FillWidth(.32f).Padding(10,0,0,0)[Console]];
    Root->AddSlot().AutoHeight().Padding(0,8,0,0)[SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).Text_Lambda([this](){return FText::FromString(Sim.IsValid() && Sim->bAILink?TEXT("AI-LINK> "):TEXT("CMD> "));}).ColorAndOpacity(FLinearColor(.18f,1,.42f))]
        +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(Prompt,SEditableTextBox).Tag(TEXT("ScutPrompt")).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).ForegroundColor(FLinearColor(.18f,1,.42f)).Text(Draft).ClearKeyboardFocusOnCommit(false)
        .HintText(FText::FromString(TEXT("status | crew | rooms | direct | alert | seal | scut | llm")))
        .OnKeyCharHandler_Lambda([this](const FGeometry&,const FCharacterEvent& E){const bool Suppress=bSuppressPromptSlash && E.GetCharacter()=='/';bSuppressPromptSlash=false;return Suppress?FReply::Handled():FReply::Unhandled();})
        .OnTextCommitted(this,&SDirectOrScutWorkstationWidget::Commit)]];
    if (HadFocus) FSlateApplication::Get().SetKeyboardFocus(Prompt,EFocusCause::SetDirectly);
    SeenEvents=MAX_uint64;
}
void SDirectOrScutWorkstationWidget::Commit(const FText& Text,ETextCommit::Type Type)
{
    if (Type!=ETextCommit::OnEnter || !Sim.IsValid()) return;
    const FString Reply=Sim->SubmitDirective(Text.ToString()); Sim->LogEvent(Reply); Prompt->SetText(FText::GetEmpty());
}
FReply SDirectOrScutWorkstationWidget::OnMouseButtonDown(const FGeometry&,const FPointerEvent& E)
{
    return E.GetEffectingButton()==EKeys::LeftMouseButton?FReply::Handled().SetUserFocus(AsShared(),EFocusCause::Mouse):FReply::Unhandled();
}
FReply SDirectOrScutWorkstationWidget::OnPreviewKeyDown(const FGeometry&,const FKeyEvent& E)
{
    const auto Key=E.GetKey();
    if (Key==EKeys::Escape)
    {
        if (IsPromptFocused()) return FReply::Handled().SetUserFocus(AsShared(),EFocusCause::SetDirectly);
        if (!bGrid) ShowGrid();
        return FReply::Handled();
    }
    if (IsPromptFocused()) return FReply::Unhandled();
    if (E.IsRepeat()) return FReply::Handled();
    if (Key==EKeys::Enter || Key==EKeys::Slash) { bSuppressPromptSlash=Key==EKeys::Slash; return FReply::Handled().SetUserFocus(Prompt.ToSharedRef(),EFocusCause::SetDirectly); }
    const FKey Keys[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight};
    for (int32 I=0;I<8;++I) if (Key==Keys[I]) { SelectFeed(I+1); return FReply::Handled(); }
    if (Key==EKeys::Tab || Key==EKeys::G || Key==EKeys::F) { ToggleExpanded(); return FReply::Handled(); }
    return FReply::Unhandled();
}
void SDirectOrScutWorkstationWidget::Tick(const FGeometry& G,double Now,float Delta)
{
    SCompoundWidget::Tick(G,Now,Delta); if (!Sim.IsValid() || !Rig.IsValid()) return;
    if (Sim->RequestedFeed>=0) { SelectFeed(Sim->RequestedFeed,false); Sim->RequestedFeed=-1; }
    Rig->SetVisibleFeeds(ActiveFeed,-1,bGrid);
    if (Glass.IsValid()) Glass->SetScalarParameterValue(TEXT("Clock"),Now);
    if (SeenEvents!=Sim->EventRevision)
    {
        Log->ClearChildren(); int32 Start=FMath::Max(0,Sim->Events.Num()-18);
        for (int32 I=Start;I<Sim->Events.Num();++I)
        {
            const auto Event=Sim->Events[I];
            Log->AddSlot().AutoHeight()[SNew(SButton).Tag(FName(*FString::Printf(TEXT("ScutEvent%d"),I))).IsFocusable(false).OnClicked_Lambda([this,Event](){if(Event.Room>=0){SelectFeed(ACompartmentCameraRig::FeedForRoom(Event.Room),false);if(bGrid)ToggleExpanded();}return FReply::Handled().SetUserFocus(AsShared(),EFocusCause::Mouse);})
                [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).Text(FText::FromString(Event.Text)).AutoWrapText(true).ColorAndOpacity(FLinearColor(.18f,1,.42f))]];
        }
        LogScroll->ScrollToEnd();
        SeenEvents=Sim->EventRevision;
    }
}
