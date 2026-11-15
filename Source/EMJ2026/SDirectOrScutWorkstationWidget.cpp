#include "SDirectOrScutWorkstationWidget.h"
#include "ScutTacticalInput.h"
#include "SScutCameraTile.h"
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
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/IInputProcessor.h"
#include "Styling/CoreStyle.h"
#include "HAL/PlatformApplicationMisc.h"
#include "GenericPlatform/ITextInputMethodSystem.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Rendering/DrawElementTypes.h"
#include "Materials/Material.h"
#include "Widgets/SLeafWidget.h"

// A fixed two-slot presentation pool owns live resources; history entries never do.
struct FInlineOpticSlot
{
    uint64 EntryId = 0;
    int32 SubjectIndex = 0;
    float Wipe = 0;
    TStrongObjectPtr<UMaterialInstanceDynamic> Material;
    FSlateBrush Brush;
    TWeakPtr<SImage> Image;
};
// A native primitive remains inside the CRT retainer; blinking never changes layout.
class SDirectOrScutPromptCursor : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SDirectOrScutPromptCursor)
    {
    }
    SLATE_ARGUMENT(TWeakPtr<SDirectOrScutWorkstationWidget>, Owner)
    SLATE_END_ARGS()
    void Construct(const FArguments &Args)
    {
        Owner = Args._Owner;
        // Repaint the time-driven primitive even when the surrounding text is cached.
        ForceVolatile(true);
    }
    FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(2, 20);
    }
    int32 OnPaint(const FPaintArgs &, const FGeometry &Geometry, const FSlateRect &, FSlateWindowElementList &Elements,
                  int32 Layer, const FWidgetStyle &Style, bool) const override
    {
        const auto Widget = Owner.Pin();
        if (!Widget)
        {
            return Layer;
        }
        // Snap toward the ghost cell, never left over the final typed glyph. Size in device pixels.
        const float Scale = FMath::Max(Geometry.GetAccumulatedLayoutTransform().GetScale(), .001f);
        const FVector2D Origin = Geometry.LocalToAbsolute(FVector2D::ZeroVector);
        const FVector2f Offset(
            Geometry.AbsoluteToLocal(FVector2D(FMath::CeilToFloat(Origin.X), FMath::RoundToFloat(Origin.Y))));
        const FVector2f Size(2.f / Scale,
                             FMath::Max(1.f, FMath::RoundToFloat(Geometry.GetLocalSize().Y * Scale)) / Scale);
        FLinearColor Ink = Widget->EvaluatePhosphorColor(1) * Style.GetColorAndOpacityTint();
        Ink.A = Widget->IsBlockCursorVisible() ? Ink.A : 0.f;
        FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Offset)),
                                   FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Ink);
        return Layer;
    }

  private:
    TWeakPtr<SDirectOrScutWorkstationWidget> Owner;
};
class SDirectOrScutInstrument : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SDirectOrScutInstrument)
    {
    }
    SLATE_ARGUMENT(TWeakPtr<SDirectOrScutWorkstationWidget>, Owner)
    SLATE_ARGUMENT(ETerminalInstrument, Kind)
    SLATE_ARGUMENT(int32, Subject)
    SLATE_END_ARGS()
    void Construct(const FArguments &Args)
    {
        Owner = Args._Owner;
        Kind = Args._Kind;
        Subject = Args._Subject;
    }
    FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(720, Kind == ETerminalInstrument::Schematic ? 170 : 80);
    }
    int32 OnPaint(const FPaintArgs &, const FGeometry &Geometry, const FSlateRect &, FSlateWindowElementList &Elements,
                  int32 Layer, const FWidgetStyle &, bool) const override
    {
        const auto Widget = Owner.Pin();
        return Widget ? Widget->PaintTerminalInstrument(Elements, Layer, Geometry, Kind, Subject) : Layer;
    }

  private:
    TWeakPtr<SDirectOrScutWorkstationWidget> Owner;
    ETerminalInstrument Kind = ETerminalInstrument::ECG;
    int32 Subject = 0;
};

// Stateless vector splice: resolves a stable history identity at paint time.
class SScutInlineVector : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SScutInlineVector)
    {
    }
    SLATE_ARGUMENT(TWeakPtr<SDirectOrScutWorkstationWidget>, Owner)
    SLATE_ARGUMENT(uint64, EntryId)
    SLATE_END_ARGS()
    void Construct(const FArguments &Args)
    {
        Owner = Args._Owner;
        EntryId = Args._EntryId;
    }
    FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(300, 86);
    }
    int32 OnPaint(const FPaintArgs &, const FGeometry &G, const FSlateRect &, FSlateWindowElementList &E, int32 L,
                  const FWidgetStyle &, bool) const override
    {
        const auto W = Owner.Pin();
        return W ? W->PaintInlineAttachment(E, L, G, EntryId) : L;
    }

  private:
    TWeakPtr<SDirectOrScutWorkstationWidget> Owner;
    uint64 EntryId = 0;
};
namespace
{
// No OS chrome: only phosphor ink and an inverted hover/pressed field.
TSharedRef<SWidget> PhosphorSoftkey(FName Tag, const FText &Text, FSimpleDelegate Action)
{
    auto Button = SNew(SButton)
                      .Tag(Tag)
                      .IsFocusable(false)
                      .ButtonStyle(FCoreStyle::Get(), "NoBorder")
                      .ContentPadding(FMargin(2))
                      .OnClicked_Lambda(
                          [Action]()
                          {
                              Action.ExecuteIfBound();
                              return FReply::Handled();
                          });
    TWeakPtr<SButton> Weak = Button;
    const auto Hot = [Weak]()
    {
        auto B = Weak.Pin();
        return B && (B->IsHovered() || B->IsPressed());
    };
    Button->SetContent(
        SNew(SBorder)
            .Padding(FMargin(2, 0))
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor_Lambda(
                [Hot]()
                {
                    return Hot() ? FLinearColor(.48f, .87f, .58f) : FLinearColor::Transparent;
                })[SNew(STextBlock)
                       .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                       .Text(Text)
                       .ColorAndOpacity_Lambda(
                           [Hot]()
                           { return Hot() ? FLinearColor(.003f, .009f, .005f) : FLinearColor(.48f, .87f, .58f); })]);
    return Button;
}
TSharedRef<SWidget> PhosphorHairline()
{
    return SNew(SBox).HeightOverride(1)[SNew(SBorder)
                                            .Padding(0)
                                            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                                            .BorderBackgroundColor(FLinearColor(.08f, .3f, .14f))];
}
} // namespace

// Claim developer view keys before PIE editor command processing,
// only while this workstation owns focus, before editor command processing.
SDirectOrScutWorkstationWidget::~SDirectOrScutWorkstationWidget()
{
    if (Sim.IsValid())
    {
        Sim->OnDirectorPresentation.RemoveAll(this);
    }
    if (FSlateApplication::IsInitialized() && TacticalInput)
    {
        FSlateApplication::Get().UnregisterInputPreProcessor(TacticalInput);
    }
}
// Keep tiles alive on single selection so Slate can route the second click to the same widget.
void SDirectOrScutWorkstationWidget::Construct(const FArguments &Args)
{
    InitializePhosphorCurve();
    NeutralOptic.Reset(UTexture2D::CreateTransient(1, 1, PF_B8G8R8A8));
    if (NeutralOptic.IsValid())
    {
        auto &Mip = NeutralOptic->GetPlatformData()->Mips[0];
        void *Pixels = Mip.BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memzero(Pixels, 4);
        Mip.BulkData.Unlock();
        NeutralOptic->UpdateResource();
    }
    if (UMaterial *Base = LoadObject<UMaterial>(
            nullptr, TEXT("/Game/DirectOrScut/Materials/M_DirectOrScut_Terminal.M_DirectOrScut_Terminal")))
    {
        TerminalSurfaceMaterial.Reset(UMaterialInstanceDynamic::Create(Base, GetTransientPackage()));
        for (int32 Feed = 1; Feed <= 8; ++Feed)
        {
            TerminalMaterials[Feed].Reset(UMaterialInstanceDynamic::Create(Base, GetTransientPackage()));
        }
    }
    Sim = Args._Sim;
    Sim->OnDirectorPresentation.AddSP(SharedThis(this), &SDirectOrScutWorkstationWidget::PresentDirectorDirective);
    Rig = Args._Rig;
    for (int32 F = 1; F <= 8; ++F)
    {
        if (TerminalMaterials[F].IsValid())
        {
            ApplyTerminalMaterialParameters(*TerminalMaterials[F].Get(), Rig->Targets[F].Get(), FVector2f(1024, 576), 0,
                                            1, 0);
            Brushes[F].SetResourceObject(TerminalMaterials[F].Get());
        }
        else
        {
            Brushes[F].SetResourceObject(static_cast<UObject *>(Rig->Targets[F].Get()));
        }
        Brushes[F].ImageSize = FVector2D(1024, 576);
        Brushes[F].DrawAs = ESlateBrushDrawType::Image;
    }
    PhosphorPromptStyle = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
    PhosphorPromptStyle.SetBackgroundImageNormal(FSlateNoResource())
        .SetBackgroundImageHovered(FSlateNoResource())
        .SetBackgroundImageFocused(FSlateNoResource())
        .SetBackgroundImageReadOnly(FSlateNoResource())
        .SetPadding(FMargin(0))
        .SetForegroundColor(EvaluatePhosphorColor(1))
        .SetFocusedForegroundColor(EvaluatePhosphorColor(1))
        .SetReadOnlyForegroundColor(EvaluatePhosphorColor(1));
    OxygenBarStyle = FCoreStyle::Get().GetWidgetStyle<FProgressBarStyle>("ProgressBar");
    OxygenBarStyle.SetFillImage(FSlateColorBrush(FLinearColor::White))
        .SetBackgroundImage(FSlateColorBrush(FLinearColor(.01f, .04f, .02f)));
    SlimScrollStyle = FCoreStyle::Get().GetWidgetStyle<FScrollBarStyle>("ScrollBar");
    SlimScrollStyle.SetNormalThumbImage(FSlateColorBrush(FLinearColor(.15f, .7f, .3f, .7f)))
        .SetHoveredThumbImage(FSlateColorBrush(FLinearColor(.3f, 1, .45f)))
        .SetDraggedThumbImage(FSlateColorBrush(FLinearColor(.3f, 1, .45f)))
        .SetVerticalBackgroundImage(FSlateNoResource())
        .SetHorizontalBackgroundImage(FSlateNoResource())
        .SetVerticalTopSlotImage(FSlateNoResource())
        .SetVerticalBottomSlotImage(FSlateNoResource())
        .SetHorizontalTopSlotImage(FSlateNoResource())
        .SetHorizontalBottomSlotImage(FSlateNoResource());
    ChildSlot[SAssignNew(Retainer, SRetainerWidget)[SNew(SBorder).Padding(0).BorderBackgroundColor(
        FLinearColor(.015f, .025f, .02f, 1))[SAssignNew(Root, SVerticalBox)]]];
    if (TerminalSurfaceMaterial.IsValid())
    {
        Retainer->SetEffectMaterial(TerminalSurfaceMaterial.Get());
        Retainer->SetTextureParameter(TEXT("OpticRenderTarget"));
        TerminalSurfaceMaterial->SetVectorParameterValue(TEXT("PhosphorTint"), FLinearColor::White);
        TerminalSurfaceMaterial->SetScalarParameterValue(TEXT("WipeProgress"), 1);
        TerminalSurfaceMaterial->SetScalarParameterValue(TEXT("ScanlineStrength"), .045f);
        TerminalSurfaceMaterial->SetScalarParameterValue(TEXT("DistortionAmount"), .001f);
    }
    Sim->LogEvent(TEXT("Private uplink established. Crew sees only automated Ship AI protocols."));
    Sim->LogEvent(Sim->SubmitDirective(TEXT("status")));
    Sim->LogEvent(TEXT("CMD button: focus prompt | Tab: categories or completion | Esc: camera navigation | "
                       "INSPECT / GRID buttons: cameras | help: reference"));
    Sim->LogEvent(TEXT("Crew: Vale / engineer, Chen / medic, Ortiz / technician, Ash / ") +
                  Sim->PublicCrewRole(3) + TEXT(". Commands use full room "
                       "names, e.g. direct ash MED_BAY telemetry; alert AIRLOCK_CHAMBER fault."));
    ResetTerminalPresentation();
    SeenRuntimeRevision = Sim->RuntimeRevision;
    Rebuild();
    auto Input = MakeShared<FScutTacticalInput>();
    Input->Owner = SharedThis(this);
    TacticalInput = Input;
    FSlateApplication::Get().RegisterInputPreProcessor(TacticalInput, 0);
}
TSharedRef<SWidget> SDirectOrScutWorkstationWidget::GetContentRoot() const
{
    return Root.ToSharedRef();
}
bool SDirectOrScutWorkstationWidget::IsPromptFocused() const
{
    return Prompt.IsValid() && (Prompt->HasKeyboardFocus() || Prompt->HasFocusedDescendants());
}
bool SDirectOrScutWorkstationWidget::IsTextEditingFocused() const
{
    if (IsPromptFocused())
    {
        return true;
    }
    auto &App = FSlateApplication::Get();
    const auto *TextInput = App.GetTextInputMethodSystem();
    if (TextInput && TextInput->HasActiveContext())
    {
        return true;
    }
    // Standard Slate editors also need protection when platform text services are unavailable.
    for (auto Widget = App.GetKeyboardFocusedWidget(); Widget.IsValid(); Widget = Widget->GetParentWidget())
    {
        const FName Type = Widget->GetType();
        if (Type == TEXT("SEditableText") || Type == TEXT("SEditableTextBox") ||
            Type == TEXT("SMultiLineEditableText") || Type == TEXT("SMultiLineEditableTextBox"))
        {
            return true;
        }
    }
    return false;
}
TSharedRef<SWidget> SDirectOrScutWorkstationWidget::Video(int32 F)
{
    const int32 R = ACompartmentCameraRig::RoomForFeed(F);
    auto CrewBadges = SNew(SVerticalBox);
    const TCHAR *Names[] = {TEXT("vale"), TEXT("chen"), TEXT("ortiz"), TEXT("ash")};
    for (int32 I = 0; I < 4; ++I)
    {
        const FString Command = FString(TEXT("follow ")) + Names[I];
        CrewBadges->AddSlot()
            .AutoHeight()[SNew(SButton)
                              .ButtonStyle(FCoreStyle::Get(), "NoBorder")
                              .IsFocusable(false)
                              .Tag(FName(*FString::Printf(TEXT("ScutFollowCrew%dFeed%d"), I, F)))
                              .Visibility_Lambda(
                                  [this, I, R]()
                                  {
                                      const auto &U = Sim->Simulation.Bodies[I];
                                      return Sim->CanFollowCrew(I) && U.Room == R ? EVisibility::Visible
                                                                                  : EVisibility::Collapsed;
                                  })
                              .OnClicked_Lambda(
                                  [this, Command]()
                                  {
                                      SetConsoleOpen(false);
                                      Sim->QueueConsoleCommand(Command);
                                      return FReply::Handled().SetUserFocus(AsShared());
                                  })[SNew(STextBlock)
                                         .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                                         .ColorAndOpacity_Lambda(
                                             [this, I]()
                                             {
                                                 return Sim->MedicalSpoofs[I].Until > Sim->Simulation.TickIndex
                                                            ? FLinearColor(1.f, .65f, .12f)
                                                            : FLinearColor(.6f, 1, .65f);
                                             })
                                         .Text_Lambda(
                                             [this, I]()
                                             {
                                                 const auto &U = Sim->Simulation.Bodies[I];
                                                 const TCHAR *N[] = {TEXT("VALE"), TEXT("CHEN"), TEXT("ORTIZ"),
                                                                     TEXT("ASH")};
                                                 return FText::FromString(FString::Printf(
                                                     TEXT("[%s%s] %s%s | %.0f BPM %.0f RPM %.1fmm"), N[I],
                                                     Sim->FollowedCrew == I ? TEXT(" // FOLLOW") : TEXT(""),
                                                     (*Sim->MedicalReadout(I)),
                                                     Sim->MedicalSpoofs[I].Until > Sim->Simulation.TickIndex
                                                         ? TEXT(" [SPOOFED]")
                                                         : TEXT(""),
                                                     Sim->TerminalBiometrics(I).HeartRateBPM,
                                                     Sim->TerminalBiometrics(I).RespirationRateRPM,
                                                     U.Profile.PupilDilationMM));
                                             })]];
    }
    auto Header =
        SNew(SHorizontalBox) +
        SHorizontalBox::Slot().FillWidth(1).VAlign(
            VAlign_Center)[SNew(STextBlock)
                               .Tag(FName(*FString::Printf(TEXT("ScutFeedLabel%d"), F)))
                               .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                               .AutoWrapText(true)
                               .ColorAndOpacity(FLinearColor(.4f, 1, .55f))
                               .Text_Lambda(
                                   [this, F, R]()
                                   {
                                       const auto &Room = Sim->Simulation.Rooms[R];
                                       return FText::FromString(FString::Printf(
                                           TEXT("[%d] %s | %s | %s%s"), F, DirectOrScut::RoomName(R),
                                           *Sim->RoomLifeSupport(R),
                                           !Room.bIsPressurized    ? TEXT("VENTING")
                                           : Room.AlertSeconds > 0 ? TEXT("ALERT")
                                           : Sim->IsRoomSealed(R)  ? TEXT("SEALED")
                                                                   : TEXT("PRESSURIZED / OPEN"),
                                           Sim->IsCameraTallyActive(R) ? TEXT(" [REC // TALLY LIVE]") : TEXT("")));
                                   })] +
        SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
                                               .Tag(FName(*FString::Printf(TEXT("ScutInspect%d"), F)))
                                               .IsFocusable(false)
                                               .OnClicked_Lambda(
                                                   [this, F]()
                                                   {
                                                       SetConsoleOpen(false);
                                                       SelectFeed(F, false);
                                                       ExpandSelected();
                                                       return FReply::Handled().SetUserFocus(AsShared());
                                                   })[SNew(STextBlock)
                                                          .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                                                          .Text(FText::FromString(TEXT("[INSPECT]")))]];
    auto Tile =
        SNew(SScutCameraTile)
            .Owner(SharedThis(this))
            .Feed(F)
                [SNew(SBorder)
                     .Padding(2)
                     .Tag(FName(*FString::Printf(TEXT("ScutBreachBorder%d"), F)))
                     .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                     .BorderBackgroundColor_Lambda(
                         [this, R]()
                         {
                             return Sim->IsRoomBreachActive(R) ? FLinearColor(.85f, .015f, .035f)
                                                               : FLinearColor(.005f, .012f, .008f);
                         })
                         [SNew(SBorder)
                              .Padding(0)
                              .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                              .BorderBackgroundColor(FLinearColor(.005f, .012f, .008f))
                                  [SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[Header] +
                                   SVerticalBox::Slot().AutoHeight()
                                       [SNew(STextBlock)
                                            .Tag(FName(*FString::Printf(TEXT("ScutSurveillanceBadges%d"), F)))
                                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                                            .AutoWrapText(true)
                                            .Visibility_Lambda(
                                                [this, R]()
                                                {
                                                    return Sim->RoomSurveillanceBadges(R).IsEmpty()
                                                               ? EVisibility::Collapsed
                                                               : EVisibility::Visible;
                                                })
                                            .Text_Lambda([this, R]()
                                                         { return FText::FromString(Sim->RoomSurveillanceBadges(R)); })
                                            .ColorAndOpacity_Lambda(
                                                [this, R]()
                                                {
                                                    const float Pulse =
                                                        .65f +
                                                        .35f * FMath::Sin(float(
                                                                   FSlateApplication::Get().GetCurrentTime() * 6.));
                                                    return Sim->IsRoomBreachActive(R)
                                                               ? FLinearColor(1.f, .1f, .15f, Pulse)
                                                               : FLinearColor(1.f, .65f, .12f, Pulse);
                                                })] +
                                   SVerticalBox::Slot().FillHeight(1)
                                       [SNew(SBorder)
                                            .Padding(0)
                                            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                                            .BorderBackgroundColor(FLinearColor::Black)
                                                [SNew(SOverlay) +
                                                 SOverlay::Slot()
                                                     [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
                                                          [SAssignNew(OpticImages[F], SImage)
                                                               .Tag(FName(*FString::Printf(TEXT("ScutFeedImage%d"), F)))
                                                               .Image(&Brushes[F])
                                                               .Visibility_Lambda(
                                                                   [this, R]()
                                                                   {
                                                                       return Sim->CameraFeedState(R) ==
                                                                                      ECameraFeedState::Online
                                                                                  ? EVisibility::Visible
                                                                                  : EVisibility::Hidden;
                                                                   })]] +
                                                 SOverlay::Slot()
                                                     [SNew(SBorder)
                                                          .Tag(FName(
                                                              *FString::Printf(TEXT("ScutFeedInterrupted%d"), F)))
                                                          .Padding(8)
                                                          .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                                                          .BorderBackgroundColor(FLinearColor(.012f, .024f, .014f))
                                                          .HAlign(HAlign_Center)
                                                          .VAlign(VAlign_Center)
                                                          .Visibility_Lambda(
                                                              [this, R]()
                                                              {
                                                                  return Sim->CameraFeedState(R) !=
                                                                                 ECameraFeedState::Online
                                                                             ? EVisibility::Visible
                                                                             : EVisibility::Collapsed;
                                                              })[SNew(STextBlock)
                                                                     .Font(
                                                                         FCoreStyle::GetDefaultFontStyle("Regular", 10))
                                                                     .ColorAndOpacity(FLinearColor(.5f, 1, .6f))
                                                                     .AutoWrapText(true)
                                                                     .Justification(ETextJustify::Center)
                                                                     .Text_Lambda(
                                                                         [this, R]()
                                                                         {
                                                                             if (Sim->StationRooms[R].Power ==
                                                                                 EPowerState::Offline)
                                                                             {
                                                                                 return FText::FromString(
                                                                                     TEXT("NO POWER // FEED DARK"));
                                                                             }
                                                                             return FText::FromString(FString::Printf(
                                                                                 TEXT("CAMERA FEED "
                                                                                      "INTERRUPTED\n%s\nPOWER / CAMERA "
                                                                                      "DIAGNOSTICS %s"),
                                                                                 Sim->CameraStateName(
                                                                                     Sim->CameraFeedState(R)),
                                                                                 DirectOrScut::RoomName(R)));
                                                                         })]]]] +
                                   SVerticalBox::Slot().AutoHeight()[CrewBadges]]]];
    // Breach urgency overrides the normal green selection frame on the affected monitor.
    Tile->SetBorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda(
        [this, F, R]() -> FSlateColor
        {
            if (Sim->IsRoomBreachActive(R))
            {
                return FLinearColor(.85f, .015f, .035f);
            }
            return SelectedCameraIndex == F - 1 ? FLinearColor(.25f, 1.f, .08f) : FLinearColor(.025f, .045f, .03f);
        }));
    return Tile;
}

void SDirectOrScutWorkstationWidget::SelectFeed(int32 F, bool AllowDoubleTap)
{
    if (F < 1 || F > 8)
    {
        return;
    }
    if (!bDebugEightCamGrid)
    {
        if (ActiveFeed != F)
        {
            PresentOptic(ACompartmentCameraRig::RoomForFeed(F));
        }
        return;
    }
    const double Now = FPlatformTime::Seconds();
    const bool DoubleTap = AllowDoubleTap && LastFeed == F && Now - LastTap < .3;
    LastFeed = AllowDoubleTap ? F : -1;
    LastTap = Now;
    const bool Changed = ActiveFeed != F;
    ActiveFeed = F;
    SelectedCameraIndex = F - 1;
    if (DoubleTap)
    {
        ToggleExpanded();
    }
    else if (Changed && !bGrid)
    {
        Sim->QueueCameraInspection(ACompartmentCameraRig::RoomForFeed(ActiveFeed));
        RebuildCameraWall();
    }
}
void SDirectOrScutWorkstationWidget::ExpandSelected()
{
    if (!bDebugEightCamGrid)
    {
        return;
    }
    if (!bFullscreen)
    {
        bFullscreen = true;
        bGrid = false;
        Sim->QueueCameraInspection(ACompartmentCameraRig::RoomForFeed(ActiveFeed));
        RebuildCameraWall();
    }
}
void SDirectOrScutWorkstationWidget::ToggleExpanded()
{
    if (!bDebugEightCamGrid)
    {
        return;
    }
    bFullscreen = !bFullscreen;
    bGrid = !bFullscreen;
    Sim->QueueCameraInspection(bGrid ? -1 : ACompartmentCameraRig::RoomForFeed(ActiveFeed));
    RebuildCameraWall();
}
void SDirectOrScutWorkstationWidget::ShowGrid()
{
    if (!bDebugEightCamGrid)
    {
        return;
    }
    bGrid = true;
    bFullscreen = false;
    LastFeed = -1;
    Sim->QueueCameraInspection(-1);
    RebuildCameraWall();
}
// Compatibility entry point: the desk console is permanent; this changes input focus only.
void SDirectOrScutWorkstationWidget::SetConsoleOpen(bool PromptFocus, bool)
{
    if (PromptFocus && Sim->ExperimentPhase == EExperimentPhase::Resolved)
    {
        return;
    }
    bConsoleOpen = PromptFocus;
    bMenuFocused = false;
    FSlateApplication::Get().SetKeyboardFocus(
        PromptFocus ? StaticCastSharedRef<SWidget>(Prompt.ToSharedRef()) : AsShared(), EFocusCause::SetDirectly);
}
void SDirectOrScutWorkstationWidget::RebuildCameraWall()
{
    if (!CameraWall)
    {
        return;
    }

    TSharedRef<SWidget> Main = Video(ActiveFeed);
    if (bGrid)
    {
        auto Grid = SNew(SUniformGridPanel).SlotPadding(2);
        for (int32 F = 1; F <= 8; ++F)
        {
            Grid->AddSlot((F - 1) % 4, (F - 1) / 4)[Video(F)];
        }
        Main = Grid;
    }
    CameraWall->SetContent(
        SNew(SOverlay) + SOverlay::Slot()[Main] +
        SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(12)[BuildBriefingCard()]);
}
TSharedRef<SWidget> SDirectOrScutWorkstationWidget::BuildBriefingCard()
{
    auto Card = SNew(SVerticalBox);
    auto Line = [&](TAttribute<FText> Text, int32 Size, FName FieldTag = NAME_None)
    {
        Card->AddSlot().AutoHeight().Padding(0, 5)[SNew(STextBlock)
                                                       .Tag(FieldTag)
                                                       .Font(FCoreStyle::GetDefaultFontStyle("Regular", Size))
                                                       .ColorAndOpacity(FLinearColor(.4f, 1, .55f))
                                                       .AutoWrapText(true)
                                                       .Text(Text)];
    };
    Line(FText::FromString(TEXT("EXPERIMENT BRIEFING // AWAITING INITIALIZATION")), 15);
    Line(TAttribute<FText>::CreateLambda([this]() { return FText::FromString(Sim->Simulation.Scenario.ScenarioId); }),
         13, TEXT("ScutBriefingScenario"));
    Line(TAttribute<FText>::CreateLambda(
             [this]()
             { return FText::FromString(TEXT("COVER PROTOCOL\n") + Sim->Simulation.Scenario.CoverStoryLabel); }),
         12, TEXT("ScutBriefingCover"));
    Line(TAttribute<FText>::CreateLambda(
             [this]() { return FText::FromString(TEXT("DIRECTOR SCUT OBJECTIVE\n") + Sim->BriefingObjective()); }),
         12, TEXT("ScutBriefingObjective"));
    Card->AddSlot().AutoHeight().Padding(0, 8, 0,
                                         0)[SNew(SButton)
                                                .Tag(TEXT("ScutInitializeExperiment"))
                                                .IsFocusable(false)
                                                .Text(FText::FromString(TEXT("[INITIALIZE EXPERIMENT (ENTER)]")))
                                                .OnClicked_Lambda(
                                                    [this]()
                                                    {
                                                        SetConsoleOpen(false);
                                                        Sim->QueueConsoleCommand(TEXT("start_experiment"));
                                                        return FReply::Handled().SetUserFocus(AsShared());
                                                    })];
    return SNew(SBox).WidthOverride_Lambda(
        [this]()
        {
            const float Width = float(CameraWall->GetCachedGeometry().GetLocalSize().X);
            return Width > 0 ? FMath::Clamp(Width - 32, 280.f, 650.f) : 650.f;
        })[SNew(SBorder)
               .Tag(TEXT("ScutBriefingCard"))
               .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
               .BorderBackgroundColor(FLinearColor(.2f, .7f, .3f))
               .Padding(1)
               .Visibility_Lambda(
                   [this]()
                   {
                       return Sim->ExperimentPhase == EExperimentPhase::Briefing ? EVisibility::Visible
                                                                                 : EVisibility::Collapsed;
                   })[SNew(SBorder)
                          .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                          .BorderBackgroundColor(FLinearColor(.012f, .024f, .014f))
                          .Padding(16)[Card]]];
}
void SDirectOrScutWorkstationWidget::Rebuild()
{
    const FText Draft = Prompt.IsValid() ? Prompt->GetText() : FText::GetEmpty();
    Root->ClearChildren();
    if (!bDebugEightCamGrid)
    {
        CameraWall.Reset();
        MenuList.Reset();
        MenuScroll.Reset();
        Root->AddSlot().FillHeight(1)[SNew(SOverlay) + SOverlay::Slot()[BuildUnifiedTerminal(Draft)] +
                                      SOverlay::Slot()[BuildEvaluationDossier()]];
        RebuildUnifiedStream();
        RebuildMenu();
        SeenMenuRevision = Sim->MenuRevision;
        if (bConsoleOpen)
        {
            FSlateApplication::Get().SetKeyboardFocus(Prompt);
        }
        return;
    }
    // Drop detached pane trees when entering diagnostics; only the bounded presentation blocks survive.
    MediationColumn.Reset();
    Root->AddSlot().AutoHeight().Padding(
        6, 4)[SNew(STextBlock)
                  .Tag(TEXT("ScutTelemetry"))
                  .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                  .ColorAndOpacity(FLinearColor(.3f, 1, .45f))
                  .Text_Lambda(
                      [this]()
                      {
                          const auto &M = Sim->Simulation;
                          int32 T = int32(M.TickIndex / 30), Alerts = 0;
                          for (const auto &R : M.Rooms)
                          {
                              Alerts += R.AlertSeconds > 0 || !R.bIsPressurized;
                          }
                          return FText::FromString(
                              FString::Printf(TEXT("EXPERIMENT %02d:%02d / 10:00 | OBJECTIVE %.0f%% | GLOBAL SUSPICION "
                                                   "%.0f%% | ACTIVE ALERTS %d | %s | %s"),
                                              T / 60, T % 60, M.ObjectiveProgress * 100, M.CollectiveSuspicion() * 100,
                                              Alerts, *M.Scenario.ScenarioId, *Sim->FollowStatus()));
                      })];
    Root->AddSlot().FillHeight(
        1)[SAssignNew(Body, SOverlay).Tag(TEXT("ScutWorkspace")).Clipping(EWidgetClipping::ClipToBounds) +
           SOverlay::Slot()[SNew(SVerticalBox) +
                            SVerticalBox::Slot().FillHeight(.62f)[SAssignNew(CameraWall, SBox)
                                                                      .Tag(TEXT("ScutCameraWall"))
                                                                      .Clipping(EWidgetClipping::ClipToBounds)] +
                            SVerticalBox::Slot().FillHeight(.38f)[BuildTerminal(Draft)]] +
           SOverlay::Slot()[BuildEvaluationDossier()]];
    RebuildCameraWall();
    Root->AddSlot().AutoHeight().Padding(
        6, 3)[SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth()[BuildWorkstationControls()] +
              SHorizontalBox::Slot().FillWidth(
                  1)[SNew(STextBlock)
                         .Tag(TEXT("ScutPriorityAlert"))
                         .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                         .ColorAndOpacity(FLinearColor(1, .75f, .25f))
                         .Text_Lambda([this]() { return FText::FromString(Sim->LatestPriorityAlert()); })]];
    SeenEvents = MAX_uint64;
    SeenMenuRevision = MAX_uint64;
    RefreshLog();
    RebuildMenu();
    if (bConsoleOpen && !bMenuFocused)
    {
        FSlateApplication::Get().SetKeyboardFocus(Prompt, EFocusCause::SetDirectly);
    }
}
TSharedRef<SWidget> SDirectOrScutWorkstationWidget::BuildTerminal(const FText &Draft)
{
    auto Left = SNew(SVerticalBox);
    auto Tabs = SNew(SHorizontalBox);
    const TCHAR *Labels[] = {TEXT("[DIRECTIVES]"), TEXT("[EVENTS]"), TEXT("[CREW]"), TEXT("[BULKHEADS]"),
                             TEXT("[EXPERIMENT]")};
    for (int32 I = 0; I < 5; ++I)
    {
        Tabs->AddSlot().FillWidth(1).Padding(
            1)[SNew(SButton)
                   .Tag(FName(*FString::Printf(TEXT("ScutCategory%d"), I)))
                   .IsFocusable(false)
                   .ButtonColorAndOpacity_Lambda(
                       [this, I]()
                       { return ActiveCategory == I ? FLinearColor(.2f, .6f, .3f) : FLinearColor(.05f, .12f, .07f); })
                   .OnClicked_Lambda(
                       [this, I]()
                       {
                           SelectCategory(I);
                           return FReply::Handled().SetUserFocus(AsShared());
                       })[SNew(STextBlock)
                              .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                              .Text(FText::FromString(Labels[I]))]];
    }
    Left->AddSlot().AutoHeight()[Tabs];
    Left->AddSlot().AutoHeight()[BuildRoomQuickActions()];
    Left->AddSlot().FillHeight(1)[BuildMenuSurface()];
    auto Right = SNew(SVerticalBox);
    Right->AddSlot().AutoHeight()
        [SNew(SHorizontalBox) +
         SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock)
                                                 .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                                                 .ColorAndOpacity(FLinearColor(.4f, 1, .55f))
                                                 .Text(FText::FromString(TEXT("SHIP AI // TUI CONSOLE")))] +
         SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
                                                .Tag(TEXT("ScutCopyLog"))
                                                .IsFocusable(false)
                                                .OnClicked(this, &SDirectOrScutWorkstationWidget::CopyLog)
                                                    [SNew(STextBlock)
                                                         .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                                                         .Text_Lambda(
                                                             [this]()
                                                             {
                                                                 return FText::FromString(FPlatformTime::Seconds() <
                                                                                                  CopiedUntil
                                                                                              ? TEXT("COPIED")
                                                                                              : TEXT("[COPY LOG]"));
                                                             })]]];
    Right->AddSlot().FillHeight(1).Padding(
        0, 3)[SAssignNew(LogScroll, SScrollBox)
                  .Tag(TEXT("ScutLogScroll"))
                  .ScrollBarStyle(&SlimScrollStyle)
                  .ScrollBarThickness(FVector2D(3, 3))
                  .ScrollBarPadding(FMargin(0))
                  .ScrollBarVisibility(EVisibility::Visible)
                  .ScrollBarAlwaysVisible(false)
                  .OnUserScrolled_Lambda([this](float Offset)
                                         { bLogAutoScroll = Offset >= LogScroll->GetScrollOffsetOfEnd() - 2; }) +
              SScrollBox::Slot()[SAssignNew(Log, SVerticalBox)]];
    Right->AddSlot().AutoHeight()
        [SNew(SHorizontalBox) +
         SHorizontalBox::Slot().AutoWidth()
             [SNew(SButton)
                  .IsFocusable(false)
                  .Tag(TEXT("ScutFocusPrompt"))
                  .Text_Lambda([this]() { return FText::FromString(Sim->bAILink ? TEXT("AI-LINK>") : TEXT("CMD>")); })
                  .OnClicked_Lambda(
                      [this]()
                      {
                          SetConsoleOpen(true);
                          return FReply::Handled();
                      })] +
         SHorizontalBox::Slot().FillWidth(
             1)[SAssignNew(Prompt, SEditableTextBox)
                    .Tag(TEXT("ScutPrompt"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                    .ForegroundColor(FLinearColor(.3f, 1, .45f))
                    .BackgroundColor(FLinearColor(.008f, .016f, .01f))
                    .Text(Draft)
                    .ClearKeyboardFocusOnCommit(false)
                    .HintText(
                        FText::FromString(TEXT("help | follow | seal / unseal | scut | scenario | reset_experiment")))
                    .OnTextCommitted(this, &SDirectOrScutWorkstationWidget::Commit)]];
    return SNew(SBorder)
        .Tag(TEXT("ScutConsoleBezel"))
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.15f, .6f, .25f))
        .Padding(FMargin(0, 1, 0, 0))[SNew(SBorder)
                                          .Tag(TEXT("ScutTUIConsole"))
                                          .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                                          .BorderBackgroundColor(FLinearColor(.012f, .024f, .014f, .96f))
                                          .Padding(7)[SNew(SHorizontalBox) +
                                                      SHorizontalBox::Slot().FillWidth(.42f).Padding(
                                                          0, 0, 6, 0)[SNew(SBox).Tag(TEXT("ScutMenuPane"))[Left]] +
                                                      SHorizontalBox::Slot().FillWidth(.58f).Padding(
                                                          6, 0, 0, 0)[SNew(SBox).Tag(TEXT("ScutOutputPane"))[Right]]]];
}
TSharedRef<SWidget> SDirectOrScutWorkstationWidget::BuildMenuSurface()
{
    auto Left = SNew(SVerticalBox).Tag(TEXT("ScutScreenMenu"));
    Left->AddSlot().AutoHeight().Padding(
        2, 3)[SNew(STextBlock)
                  .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                  .ColorAndOpacity(FLinearColor(.3f, 1, .45f))
                  .AutoWrapText(true)
                  .Text_Lambda([this]() { return FText::FromString(Sim->ScreenMenuTitle()); })];
    Left->AddSlot()
        .AutoHeight()[SNew(SButton)
                          .Tag(TEXT("ScutMenuBack"))
                          .ButtonStyle(FCoreStyle::Get(), "NoBorder")
                          .ForegroundColor(EvaluatePhosphorColor(1))
                          .IsFocusable(false)
                          .Text(FText::FromString(TEXT("[BACK]  Tab: category | Up/Down: select | Enter: execute")))
                          .OnClicked_Lambda(
                              [this]()
                              {
                                  bConsoleOpen = true;
                                  bMenuFocused = true;
                                  Sim->QueueConsoleCommand(TEXT("menu back"));
                                  return FReply::Handled().SetUserFocus(AsShared());
                              })];
    Left->AddSlot().FillHeight(1)[SAssignNew(MenuScroll, SScrollBox)
                                      .Tag(TEXT("ScutMenuScroll"))
                                      .ScrollBarStyle(&SlimScrollStyle)
                                      .ScrollBarThickness(FVector2D(3, 3))
                                      .ScrollBarPadding(FMargin(0))
                                      .ScrollBarVisibility(EVisibility::Visible)
                                      .ScrollBarAlwaysVisible(false) +
                                  SScrollBox::Slot()[SAssignNew(MenuList, SVerticalBox)]];
    return Left;
}
void SDirectOrScutWorkstationWidget::SelectCategory(int32 Category, bool FocusMenu)
{
    ActiveCategory = (Category + 5) % 5;
    HighlightedChoice = 0;
    if (FocusMenu)
    {
        bConsoleOpen = true;
        bMenuFocused = true;
    }
    const TCHAR *Commands[] = {TEXT("menu ai"), TEXT("menu events"), TEXT("menu crew"), TEXT("menu doors"),
                               TEXT("menu experiment")};
    Sim->QueueConsoleCommand(Commands[ActiveCategory]);
}
void SDirectOrScutWorkstationWidget::ExecuteHighlighted()
{
    if (MenuEnabled.IsValidIndex(HighlightedChoice) && MenuEnabled[HighlightedChoice])
    {
        Sim->QueueConsoleCommand(FString::Printf(TEXT("choose %d"), HighlightedChoice + 1));
    }
}
FReply SDirectOrScutWorkstationWidget::CopyLog()
{
    FPlatformApplicationMisc::ClipboardCopy(*Sim->TerminalBuffer());
    CopiedUntil = FPlatformTime::Seconds() + 1.5;
    return FReply::Handled();
}
void SDirectOrScutWorkstationWidget::RouteConversationEvent(const FString &Text, int32 Room)
{
    FString Payload = Text, Stamp;
    if (Text.Len() > 7 && Text[0] == '[' && FChar::IsDigit(Text[1]))
    {
        const int32 End = Text.Find(TEXT("] "));
        if (End != INDEX_NONE)
        {
            Stamp = Text.Left(End + 2);
            Payload = Text.Mid(End + 2);
        }
    }
    // Explicit commands, dialogue, and their untagged results remain readable immediately.
    if (Payload.StartsWith(TEXT("> ")) || Payload.StartsWith(TEXT("MOTHER-7>")))
    {
        AppendTerminalProse(Text);
        return;
    }
    const TCHAR *Silent[] = {TEXT("[TELEMETRY]"),
                             TEXT("[ENV]"),
                             TEXT("[FORENSIC]"),
                             TEXT("[DEBUG]"),
                             TEXT("[AGENDA]"),
                             TEXT("[ALLIANCE]"),
                             TEXT("[SPOOF]"),
                             TEXT("  [EXEC:"),
                             TEXT("[EXEC:"),
                             TEXT("Private uplink established."),
                             TEXT("Backtick / slash:"),
                             TEXT("CMD button:"),
                             TEXT("Crew: Vale /")};
    for (const TCHAR *Prefix : Silent)
    {
        if (Payload.StartsWith(Prefix))
        {
            return;
        }
    }
    if (Payload.StartsWith(TEXT("Ship AI:")))
    {
        AppendTerminalProse(Stamp + TEXT("MOTHER-7> ") + Payload.Mid(8).TrimStartAndEnd());
        return;
    }
    FString Key, Report;
    bool Critical = false;
    if (Payload.StartsWith(TEXT("[O2WARN]")))
    {
        Key = FString::Printf(TEXT("oxygen:%d"), Room);
        ReportedConditions.Remove(Key + TEXT(":recovered"));
        PendingReports.RemoveAll([&](const auto &P) { return P.Key == Key + TEXT(":recovered"); });
        Report = TEXT("Director, oxygen is below the warning threshold. Recorded reading: ") +
                 Payload.Mid(9).TrimStartAndEnd();
        Critical = true;
    }
    else if (Payload.StartsWith(TEXT("[O2]")))
    {
        const FString Condition = FString::Printf(TEXT("oxygen:%d"), Room);
        ReportedConditions.Remove(Condition);
        PendingReports.RemoveAll([&](const auto &P) { return P.Key == Condition; });
        Key = Condition + TEXT(":recovered");
        Report = TEXT("Director, oxygen recovery was observed: ") + Payload.Mid(4).TrimStartAndEnd();
    }
    else if (Payload.StartsWith(TEXT("[BREACH]")))
    {
        const FString Condition = TEXT("uplink-breach");
        const bool Ended = Payload.Contains(TEXT("interrupted"));
        if (Ended)
        {
            ReportedConditions.Remove(Condition);
        }
        if (!Ended)
        {
            ReportedConditions.Remove(Condition + TEXT(":interrupted"));
            PendingReports.RemoveAll([&](const auto &P) { return P.Key == Condition + TEXT(":interrupted"); });
        }
        Key = Ended ? Condition + TEXT(":interrupted") : Condition;
        Report = TEXT("Director, ") + Payload.Mid(8).TrimStartAndEnd();
        Critical = !Ended;
    }
    else if (Payload == TEXT("Compartment exposed to vacuum") || Payload == TEXT("Pressure restored"))
    {
        const FString Condition = FString::Printf(TEXT("pressure:%d"), Room);
        const bool Restored = Payload == TEXT("Pressure restored");
        const FString Superseded = Restored ? Condition : Condition + TEXT(":restored");
        ReportedConditions.Remove(Superseded);
        PendingReports.RemoveAll([&](const auto &P) { return P.Key == Superseded; });
        Key = Restored ? Condition + TEXT(":restored") : Condition;
        Report = FString::Printf(TEXT("Director, %s: %s."), DirectOrScut::RoomName(Room), *Payload);
        Critical = !Restored;
    }
    else if (Payload.StartsWith(TEXT("Crew ")) && Payload.Contains(TEXT(" | BPM ")))
    {
        const int32 End = Payload.Find(TEXT(" | BPM "));
        Report = TEXT("Director, ") + Payload.Left(End) +
                 TEXT(". A significant condition change was recorded; request telemetry for readings.");
        Key = Payload.Left(Payload.Find(TEXT(":"))) + TEXT(":") +
              Payload.Mid(Payload.Find(TEXT(":")) + 1, End - Payload.Find(TEXT(":")) - 1);
        Critical = Payload.Contains(TEXT("EJECTED"), ESearchCase::IgnoreCase) ||
                   Payload.Contains(TEXT("DEAD"), ESearchCase::IgnoreCase);
    }
    else if (Payload.StartsWith(TEXT("ALERT //")) || Payload.Contains(TEXT("vacuum"), ESearchCase::IgnoreCase) ||
             Payload.Contains(TEXT("casualty"), ESearchCase::IgnoreCase))
    {
        Key = FString::Printf(TEXT("alert:%d:%s"), Room, *Payload);
        Report = TEXT("Director, ") + Payload;
        Critical = true;
    }
    else
    {
        // Existing direct results and authored cues have no category metadata. Do not infer new facts.
        FString Clean = Payload;
        if (Clean.StartsWith(TEXT("[")))
        {
            const int32 End = Clean.Find(TEXT("] "));
            if (End != INDEX_NONE)
            {
                Clean = Clean.Mid(End + 2);
            }
        }
        Clean = Clean.Replace(TEXT(" // interaction available in menu events"), TEXT(""));
        if (Payload.StartsWith(TEXT("DIRECTOR REQUEST")))
        {
            AppendTerminalProse(
                Stamp +
                TEXT("MOTHER-7> That response did not pass validation. I will use a validated result when available."));
            return;
        }
        // Command results are delivered immediately; authored unsolicited cues wait for a prose boundary.
        if (Payload.StartsWith(TEXT("[POWER]")) || Payload.StartsWith(TEXT("[BULKHEAD]")) ||
            Payload.Contains(TEXT("REJECTED")) || Payload.StartsWith(TEXT("Syntax:")) ||
            Payload.StartsWith(TEXT("DIRECTOR REQUEST")) || Payload.StartsWith(TEXT("Read-only supply inspection:")))
        {
            AppendTerminalProse(Stamp + TEXT("MOTHER-7> ") + Clean);
            return;
        }
        bool AuthoredCue = false;
        for (int32 I = Sim->CinematicCues.Num() - 1; I >= 0; --I)
        {
            const auto &Cue = Sim->CinematicCues[I];
            if (int32(Cue.RoomId) == Room && Text.EndsWith(Cue.Subtitle))
            {
                if (Cue.Weight < .5f)
                {
                    return;
                }
                AuthoredCue = true;
                Critical = Cue.Weight >= .9f;
                break;
            }
        }
        if (!AuthoredCue)
        {
            AppendTerminalProse(Stamp + TEXT("MOTHER-7> ") + Clean);
            return;
        }
        Key = FString::Printf(TEXT("notice:%d:%s"), Room, *Clean);
        Report = Clean;
    }
    // Critical warning repetition is consolidated by unresolved condition, not by changing reading text.
    if (const FString *Previous = ReportedConditions.Find(Key))
    {
        if (Critical || *Previous == Report)
        {
            return;
        }
    }
    if (ReportedConditions.Num() >= 64)
    {
        const FString Evicted = ReportedConditions.CreateConstIterator().Key();
        ReportedConditions.Remove(Evicted);
    }
    ReportedConditions.Add(Key, Report);
    const FString Message = Stamp + TEXT("MOTHER-7> ") + Report;
    if (Critical)
    {
        FTerminalStreamEntry Entry;
        Entry.Text = Message;
        Entry.Characters = Message.Len(); // Visible now, even while earlier prose is typing.
        AppendTerminalEntry(MoveTemp(Entry));
    }
    else
    {
        for (auto &Pending : PendingReports)
        {
            if (Pending.Key == Key)
            {
                Pending.Value = Message;
                return;
            }
        }
        if (PendingReports.Num() >= 16)
        {
            PendingReports.RemoveAt(0);
        }
        PendingReports.Emplace(Key, Message);
    }
}
void SDirectOrScutWorkstationWidget::FlushOperationalReports()
{
    if (PendingReports.IsEmpty() || (Prompt.IsValid() && !Prompt->GetText().IsEmpty()))
    {
        return;
    }
    for (const auto &Entry : TerminalStream)
    {
        if (Entry.Characters < Entry.Text.Len())
        {
            return;
        }
    }
    const FString Message = PendingReports[0].Value;
    PendingReports.RemoveAt(0);
    AppendTerminalProse(Message);
}

void SDirectOrScutWorkstationWidget::RefreshLog()
{
    if (!bDebugEightCamGrid)
    {
        for (; UnifiedEventCursor < Sim->Events.Num(); ++UnifiedEventCursor)
        {
            if (Sim->Events[UnifiedEventCursor].bTerminalVisible)
            {
                RouteConversationEvent(Sim->Events[UnifiedEventCursor].Text, Sim->Events[UnifiedEventCursor].Room);
            }
        }
        SeenEvents = Sim->EventRevision;
        return;
    }
    const float Offset = LogScroll->GetScrollOffset();
    Log->ClearChildren();
    for (const auto &E : Sim->Events)
    {
        {
            Log->AddSlot().AutoHeight().Padding(0, 2)[SNew(STextBlock)
                                                          .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                                                          .Text(FText::FromString(E.Text))
                                                          .AutoWrapText(true)
                                                          .ColorAndOpacity(FLinearColor(.4f, 1, .55f))];
        }
    }
    if (bLogAutoScroll)
    {
        LogScroll->ScrollToEnd();
    }
    else
    {
        LogScroll->SetScrollOffset(Offset);
    }
    SeenEvents = Sim->EventRevision;
}
void SDirectOrScutWorkstationWidget::BrowseHistory(int32 Direction)
{
    if (History.IsEmpty())
    {
        return;
    }
    if (HistoryCursor == History.Num())
    {
        HistoryDraft = Prompt->GetText().ToString();
    }
    HistoryCursor = FMath::Clamp(HistoryCursor + Direction, 0, History.Num());
    Prompt->SetText(FText::FromString(HistoryCursor < History.Num() ? History[HistoryCursor] : HistoryDraft));
    Prompt->GoTo(ETextLocation::EndOfDocument);
    Completions.Reset();
    LastCompletion.Empty();
}
TArray<FString> SDirectOrScutWorkstationWidget::CompletionCandidates(const FString &Input, bool Filter) const
{
    TArray<FString> Words, Tokens, Result;
    Input.ToLower().ParseIntoArrayWS(Words);
    const bool Space = Input.IsEmpty() || FChar::IsWhitespace(Input[Input.Len() - 1]);
    const int32 Argument = Space ? Words.Num() : Words.Num() - 1;
    const FString Prefix = Space ? TEXT("") : Words.Last();
    const int32 Start = Input.Len() - Prefix.Len();
    if (Argument == 0)
    {
        Tokens = {
            TEXT("show_cam"),   TEXT("show_widget"), TEXT("compare"),       TEXT("status"), TEXT("start_experiment"),
            TEXT("debug_grid"), TEXT("help"),        TEXT("override"),      TEXT("flag"),   TEXT("findings"),
            TEXT("subjects"),   TEXT("blindspots"),  TEXT("cross_examine"), TEXT("scrub"),  TEXT("clear"),
            TEXT("show_telem")};
    }
    else if (Argument == 1 && (Words[0] == TEXT("show_widget") || Words[0] == TEXT("compare")))
    {
        Tokens = {TEXT("ecg"), TEXT("schematic"), TEXT("ocular")};
        if (Words[0] == TEXT("compare"))
        {
            Tokens = {TEXT("ecg"), TEXT("schematic"), TEXT("cam")};
        }
    }
    else if (((Words[0] == TEXT("show_cam") || Words[0] == TEXT("show_telem")) && Argument == 1) ||
             ((Words[0] == TEXT("show_widget") || Words[0] == TEXT("compare")) && Argument >= 2 && Argument <= 3))
    {
        const bool Crew = Words.Num() > 1 && (Words[1] == TEXT("ecg") || Words[1] == TEXT("ocular"));
        if (Argument == 3 && Words[0] == TEXT("show_widget") && Words[1] != TEXT("ecg"))
        {
            return Result;
        }
        if (Crew)
        {
            Tokens = {TEXT("vale"), TEXT("chen"), TEXT("ortiz"), TEXT("ash")};
        }
        else
        {
            Tokens = {TEXT("reactor_core"), TEXT("ring_corridor"), TEXT("bridge"),        TEXT("med_bay"),
                      TEXT("observation"),  TEXT("quarters"),      TEXT("server_alcove"), TEXT("airlock")};
        }
    }
    else if (Words[0] == TEXT("override"))
    {
        // Operational grammar remains owned and validated by the fixed-tick console path.
        const FString Tail = Input.Mid(9);
        for (const FString &Candidate : Sim->CompleteConsoleCommand(Tail))
        {
            Result.Add(TEXT("override ") + Candidate);
        }
        return Result;
    }
    else if (Tokens.IsEmpty())
    {
        return Sim->CompleteConsoleCommand(Input);
    }
    for (const FString &Token : Tokens)
    {
        if (!Filter || Prefix.IsEmpty() || Token.StartsWith(Prefix))
        {
            Result.Add(Input.Left(Start) + Token);
        }
    }
    if (Argument == 0)
    {
        for (const FString &Candidate : Sim->CompleteConsoleCommand(Input))
        {
            Result.AddUnique(Candidate);
        }
    }
    return Result;
}
FString SDirectOrScutWorkstationWidget::GhostCompletion() const
{
    if (!Prompt.IsValid())
    {
        return TEXT("");
    }
    const FString Input = Prompt->GetText().ToString();
    const auto Candidates = CompletionCandidates(Input);
    return Candidates.IsEmpty() ? TEXT("") : Candidates[0].Mid(Input.Len());
}
void SDirectOrScutWorkstationWidget::Autocomplete(int32 Direction)
{
    const FString Input = Prompt->GetText().ToString();
    if (Completions.IsEmpty() || Input != LastCompletion)
    {
        Completions = CompletionCandidates(Input);
        CompletionCursor = Direction > 0 ? 0 : Completions.Num() - 1;
    }
    else
    {
        CompletionCursor = (CompletionCursor + Direction + Completions.Num()) % Completions.Num();
    }
    if (!Completions.IsEmpty())
    {
        LastCompletion = Completions[CompletionCursor];
        Prompt->SetText(FText::FromString(LastCompletion));
        Prompt->GoTo(ETextLocation::EndOfDocument);
    }
}
void SDirectOrScutWorkstationWidget::InsertArgument(const FString &Token)
{
    FString Input = Prompt->GetText().ToString();
    int32 Start = Input.Len();
    while (Start > 0 && !FChar::IsWhitespace(Input[Start - 1]))
    {
        --Start;
    }
    Prompt->SetText(FText::FromString(Input.Left(Start) + Token));
    Prompt->GoTo(ETextLocation::EndOfDocument);
    SetConsoleOpen(true);
}
bool SDirectOrScutWorkstationWidget::StageTarget(const FString &Token, bool Crew)
{
    TArray<FString> Words;
    const FString Input = Prompt->GetText().ToString().ToLower();
    Input.ParseIntoArrayWS(Words);
    if (Words.IsEmpty())
    {
        return false;
    }
    int32 Index = (Words[0] == TEXT("show_cam") || Words[0] == TEXT("show_telem")) && !Crew ? 1 : -1;
    if (Words.Num() >= 2 && (Words[0] == TEXT("show_widget") || Words[0] == TEXT("compare")) &&
        Crew == (Words[1] == TEXT("ecg") || Words[1] == TEXT("ocular")))
    {
        const bool Pair = Words[0] == TEXT("compare") || Words[1] == TEXT("ecg");
        const bool Next = Words.Num() == 3 && !Input.IsEmpty() && FChar::IsWhitespace(Input[Input.Len() - 1]);
        Index = Pair && (Words.Num() >= 4 || Next) ? 3 : 2;
    }
    if (Index < 0)
    {
        return false;
    }
    while (Words.Num() <= Index)
    {
        Words.Add(TEXT(""));
    }
    Words[Index] = Token;
    Prompt->SetText(FText::FromString(FString::Join(Words, TEXT(" "))));
    Prompt->GoTo(ETextLocation::EndOfDocument);
    SetConsoleOpen(true);
    return true;
}
void SDirectOrScutWorkstationWidget::RefreshParameterGuide()
{
    if (!ParameterGuide.IsValid() || !Prompt.IsValid())
    {
        return;
    }
    ParameterGuide->ClearChildren();
    const FString Input = Prompt->GetText().ToString();
    if (Input.IsEmpty())
    {
        return;
    }
    TArray<FString> Words;
    Input.ToLower().ParseIntoArrayWS(Words);
    const bool TrailingSpace = !Input.IsEmpty() && FChar::IsWhitespace(Input[Input.Len() - 1]);
    const int32 Argument = Words.Num() - (TrailingSpace || Words.IsEmpty() ? 0 : 1);
    const TCHAR *Context =
        Argument == 0 ? TEXT("VERB // ")
        : (Words.Num() > 1 && (Words[1] == TEXT("ecg") || Words[1] == TEXT("ocular")) && Argument >= 2)
            ? TEXT("SUBJECT // ")
        : (Words[0] == TEXT("show_cam") || Words[0] == TEXT("show_telem") || Argument >= 2) ? TEXT("TARGET // ")
                                                                                            : TEXT("KIND / ARG // ");
    ParameterGuide->AddSlot().AutoWidth()[SNew(STextBlock)
                                              .Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
                                              .ColorAndOpacity(EvaluatePhosphorColor(.6f))
                                              .Text(FText::FromString(Context))];
    for (const FString &Candidate : CompletionCandidates(Input, false))
    {
        int32 Start = Candidate.Len();
        while (Start > 0 && !FChar::IsWhitespace(Candidate[Start - 1]))
        {
            --Start;
        }
        const FString Token = Candidate.Mid(Start);
        if (Token == TEXT("debug_grid") || Token == TEXT("start_experiment"))
        {
            continue;
        }
        ParameterGuide->AddSlot().AutoWidth().Padding(
            0, 0, 7, 0)[PhosphorSoftkey(FName(*(FString(TEXT("ScutGuide_")) + Token)), FText::FromString(Token),
                                        FSimpleDelegate::CreateLambda([this, Token]() { InsertArgument(Token); }))];
    }
}
void SDirectOrScutWorkstationWidget::Commit(const FText &Text, ETextCommit::Type Type)
{
    if (Type != ETextCommit::OnEnter || !Sim.IsValid())
    {
        return;
    }
    const FString Input = Text.ToString().TrimStartAndEnd();
    if (Input.IsEmpty())
    {
        return;
    }
    if (HandlePresentationCommand(Input) || Sim->QueueConsoleCommand(Input))
    {
        if (History.IsEmpty() || History.Last() != Input)
        {
            if (History.Num() >= 32)
            {
                History.RemoveAt(0);
            }
            History.Add(Input);
        }
        HistoryCursor = History.Num();
        HistoryDraft.Empty();
        Completions.Reset();
        LastCompletion.Empty();
        if (Input != TEXT("cross_examine") && Input != TEXT("override") && Input != TEXT("flag"))
        {
            Prompt->SetText(FText::GetEmpty());
        }
    }
}
FReply SDirectOrScutWorkstationWidget::OnMouseButtonDown(const FGeometry &, const FPointerEvent &E)
{
    if (!bDebugEightCamGrid && Prompt.IsValid() && E.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        bConsoleOpen = true;
        bMenuFocused = false;
        return FReply::Handled().SetUserFocus(Prompt.ToSharedRef(), EFocusCause::Mouse);
    }
    return E.GetEffectingButton() == EKeys::LeftMouseButton
               ? FReply::Handled().SetUserFocus(
                     bConsoleOpen && !bMenuFocused ? StaticCastSharedRef<SWidget>(Prompt.ToSharedRef()) : AsShared(),
                     EFocusCause::Mouse)
               : FReply::Unhandled();
}
FReply SDirectOrScutWorkstationWidget::OnPreviewKeyDown(const FGeometry &, const FKeyEvent &E)
{
    const auto Key = E.GetKey();
    if (Key == EKeys::F9 && !E.IsRepeat())
    {
        SetDebugEightCamGrid(!bDebugEightCamGrid);
        return FReply::Handled();
    }
    if (IsTextEditingFocused() && !IsPromptFocused())
    {
        return FReply::Unhandled(); // Leave other editors' text and navigation to their own handlers.
    }
    if (!bDebugEightCamGrid && Key == EKeys::Escape)
    {
        if (Sim->ActiveMenu != EShipAIScreenMenu::None)
        {
            Sim->QueueConsoleCommand(TEXT("menu close"));
        }
        EvidenceSubject = EvidenceRoom = EvidenceProbe = -1;
        if (bForensicScrub)
        {
            bForensicScrub = false;
            bScrubPlaying = false;
        }
        else
        {
            Prompt->SetText(FText::GetEmpty());
        }
        SetConsoleOpen(true);
        return FReply::Handled();
    }
    if (Sim->ExperimentPhase == EExperimentPhase::Resolved)
    {
        return FReply::Unhandled();
    }
    if (!bDebugEightCamGrid)
    {
        if (IsPromptFocused() && E.IsControlDown() && Key == EKeys::W)
        {
            FString Draft = Prompt->GetText().ToString().TrimEnd();
            int32 End = Draft.Len();
            while (End > 0 && !FChar::IsWhitespace(Draft[End - 1]))
            {
                --End;
            }
            Prompt->SetText(FText::FromString(Draft.Left(End)));
            Prompt->GoTo(ETextLocation::EndOfDocument);
            return FReply::Handled();
        }
        if (!IsPromptFocused() && Sim->ActiveMenu != EShipAIScreenMenu::None)
        {
            if (Key == EKeys::Up || Key == EKeys::Down)
            {
                if (!MenuLabels.IsEmpty())
                {
                    HighlightedChoice = (HighlightedChoice + MenuLabels.Num() +
                                         (Key == EKeys::Up ? -1 : 1)) % MenuLabels.Num();
                    if (MenuEntries.IsValidIndex(HighlightedChoice))
                    {
                        MenuScroll->ScrollDescendantIntoView(MenuEntries[HighlightedChoice]);
                    }
                }
                return FReply::Handled();
            }
            if (Key == EKeys::Enter)
            {
                ExecuteHighlighted();
                return FReply::Handled();
            }
            if (Key == EKeys::BackSpace)
            {
                Sim->QueueConsoleCommand(TEXT("menu back"));
                return FReply::Handled();
            }
            if (Key == EKeys::Tab)
            {
                SelectCategory(ActiveCategory + (E.IsShiftDown() ? -1 : 1));
                return FReply::Handled();
            }
        }
        if (IsPromptFocused())
        {
            if (Key == EKeys::Tab)
            {
                if (Prompt->GetText().IsEmpty() && Sim->ActiveMenu != EShipAIScreenMenu::None)
                {
                    bMenuFocused = true;
                    return FReply::Handled().SetUserFocus(AsShared());
                }
                Autocomplete(E.IsShiftDown() ? -1 : 1);
                return FReply::Handled();
            }
            if (Key == EKeys::Up || Key == EKeys::Down)
            {
                BrowseHistory(Key == EKeys::Up ? -1 : 1);
                return FReply::Handled();
            }
            if (Key == EKeys::Enter && Prompt->GetText().IsEmpty() &&
                Sim->ExperimentPhase == EExperimentPhase::Briefing)
            {
                Sim->QueueConsoleCommand(TEXT("start_experiment"));
                return FReply::Handled();
            }
            return FReply::Unhandled(); // Includes printable keys even when the prompt is empty.
        }
        if (Key == EKeys::Enter && Sim->ExperimentPhase == EExperimentPhase::Briefing)
        {
            Sim->QueueConsoleCommand(TEXT("start_experiment"));
            return FReply::Handled();
        }
        return FReply::Unhandled();
    }
    if (Sim->ExperimentPhase == EExperimentPhase::Briefing && Key == EKeys::Enter && !E.IsRepeat() &&
        !IsPromptFocused())
    {
        Sim->QueueConsoleCommand(TEXT("start_experiment"));
        return FReply::Handled();
    }
    if (!E.IsRepeat() && (Key == EKeys::F9 || Key == EKeys::F10))
    {
        if (Key == EKeys::F9)
        {
            Sim->bCleanDevRendering = true;
        }
        else
        {
            Sim->bWorkstationVisible = !Sim->bWorkstationVisible;
        }
        return FReply::Handled();
    }
    if (IsPromptFocused() && Key != EKeys::Escape && Key != EKeys::Tab && Key != EKeys::Up && Key != EKeys::Down)
    {
        bConsoleOpen = true;
        bMenuFocused = false;
        return FReply::Unhandled(); // Debug-view text editing must not invoke camera/console letter shortcuts.
    }
    if (Key == EKeys::Escape)
    {
        SetConsoleOpen(false);
        if (!bGrid)
        {
            ShowGrid();
        }
        return FReply::Handled().SetUserFocus(AsShared());
    }
    if (IsPromptFocused())
    {
        bConsoleOpen = true;
        bMenuFocused = false;
    }
    if (Key == EKeys::Tab && !bConsoleOpen)
    {
        SelectCategory(ActiveCategory);
        return FReply::Handled().SetUserFocus(AsShared());
    }
    if (bConsoleOpen)
    {
        const bool PromptFocus = IsPromptFocused();
        if (Key == EKeys::Tab)
        {
            if (PromptFocus && !Prompt->GetText().IsEmpty())
            {
                Autocomplete();
            }
            else
            {
                SelectCategory(ActiveCategory + (E.IsShiftDown() ? -1 : 1));
                FSlateApplication::Get().SetKeyboardFocus(AsShared());
            }
            return FReply::Handled();
        }
        if (Key == EKeys::Up || Key == EKeys::Down)
        {
            if (PromptFocus)
            {
                BrowseHistory(Key == EKeys::Up ? -1 : 1);
            }
            else if (!MenuLabels.IsEmpty())
            {
                HighlightedChoice =
                    (HighlightedChoice + MenuLabels.Num() + (Key == EKeys::Up ? -1 : 1)) % MenuLabels.Num();
                if (MenuEntries.IsValidIndex(HighlightedChoice))
                {
                    MenuScroll->ScrollDescendantIntoView(MenuEntries[HighlightedChoice]);
                }
            }
            return FReply::Handled();
        }
        if (!PromptFocus && Key == EKeys::Enter)
        {
            ExecuteHighlighted();
            return FReply::Handled();
        }
        if (!PromptFocus && Key == EKeys::BackSpace)
        {
            Sim->QueueConsoleCommand(TEXT("menu back"));
            return FReply::Handled();
        }
        if (!PromptFocus && E.GetCharacter() >= 32 && E.GetCharacter() < 127)
        {
            FSlateApplication::Get().SetKeyboardFocus(Prompt);
        }
        return FReply::Unhandled();
    }
    return FReply::Unhandled();
}
FReply SDirectOrScutWorkstationWidget::OnKeyChar(const FGeometry &Geometry, const FCharacterEvent &E)
{
    if (IsTextEditingFocused())
    {
        return SCompoundWidget::OnKeyChar(Geometry, E);
    }
    if (HasKeyboardFocus() && Prompt.IsValid() && E.GetCharacter() >= 32 && E.GetCharacter() != 127 &&
        !E.IsControlDown() && !E.IsAltDown())
    {
        SetConsoleOpen(true);
        if (IsPromptFocused())
        {
            // Redispatch after focus transfer so Slate's editable child handles the first character too.
            FSlateApplication::Get().ProcessKeyCharEvent(E);
            return FReply::Handled();
        }
    }
    return SCompoundWidget::OnKeyChar(Geometry, E);
}
FReply SDirectOrScutWorkstationWidget::OnKeyDown(const FGeometry &Geometry, const FKeyEvent &E)
{
    if (IsTextEditingFocused() || !bDebugEightCamGrid || bConsoleOpen || !Sim->bWorkstationVisible)
    {
        return FReply::Unhandled();
    }
    const auto Key = E.GetKey();
    if (Key == EKeys::Enter)
    {
        ExpandSelected();
        return FReply::Handled();
    }
    if (bGrid && (Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Up || Key == EKeys::Down))
    {
        int32 Column = SelectedCameraIndex % 4, Row = SelectedCameraIndex / 4;
        if (Key == EKeys::Left)
        {
            Column = (Column + 3) % 4;
        }
        else if (Key == EKeys::Right)
        {
            Column = (Column + 1) % 4;
        }
        else
        {
            Row = 1 - Row;
        }
        SelectFeed(Row * 4 + Column + 1, false);
        return FReply::Handled();
    }
    return SCompoundWidget::OnKeyDown(Geometry, E);
}
void SDirectOrScutWorkstationWidget::RefreshMenuChoices()
{
    MenuLabels.Reset();
    MenuEnabled.Reset();
    const auto Choices = Sim->ScreenMenuChoices();
    for (int32 I = 0; I < Choices.Num(); ++I)
    {
        MenuLabels.Add(FString::Printf(TEXT("[%d] %s%s%s"), I + 1, *Choices[I].Label,
                                       Choices[I].Detail.IsEmpty() ? TEXT("") : TEXT("\n"), *Choices[I].Detail));
        MenuEnabled.Add(Choices[I].bEnabled);
    }
}
void SDirectOrScutWorkstationWidget::RebuildMenu()
{
    if (!MenuList)
    {
        return;
    }
    MenuList->ClearChildren();
    MenuEntries.Reset();
    RefreshMenuChoices();
    HighlightedChoice = 0;
    if (MenuLabels.IsEmpty())
    {
        MenuList->AddSlot().AutoHeight()[SNew(STextBlock)
                                            .Tag(TEXT("ScutMenuEmpty"))
                                            .AutoWrapText(true)
                                            .ColorAndOpacity(EvaluatePhosphorColor(.8f))
                                            .Text(FText::FromString(TEXT("No choices are currently available. "
                                                                        "Use menu back or menu close.")))];
    }
    for (int32 I = 0; I < MenuLabels.Num(); ++I)
    {
        TSharedRef<SWidget> Entry =
            SNew(SButton)
                .Tag(FName(*FString::Printf(TEXT("ScutMenuChoice%d"), I + 1)))
                .IsFocusable(false)
                .ButtonStyle(FCoreStyle::Get(), "NoBorder")
                .IsEnabled_Lambda([this, I]() { return MenuEnabled.IsValidIndex(I) && MenuEnabled[I]; })
                .OnClicked_Lambda(
                    [this, I]()
                    {
                        bConsoleOpen = true;
                        bMenuFocused = true;
                        HighlightedChoice = I;
                        ExecuteHighlighted();
                        return FReply::Handled().SetUserFocus(AsShared());
                    })[SNew(SBorder)
                           .Tag(FName(*FString::Printf(TEXT("ScutMenuSelection%d"), I + 1)))
                           .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                           .BorderBackgroundColor_Lambda([this, I]()
                           {
                               return HighlightedChoice == I ? FLinearColor(.12f, .4f, .2f, .8f)
                                                             : FLinearColor(.02f, .06f, .03f, .35f);
                           })
                           .Padding(2)[SNew(STextBlock)
                                           .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                                           .AutoWrapText(true)
                                           .ColorAndOpacity_Lambda([this, I]()
                                           { return HighlightedChoice == I ? FLinearColor(.003f, .009f, .005f)
                                                                           : EvaluatePhosphorColor(1); })
                                           .Text_Lambda([this, I]()
                                           { return FText::FromString(MenuLabels.IsValidIndex(I) ? MenuLabels[I]
                                                                                              : TEXT("")); })]];
        MenuEntries.Add(Entry);
        MenuList->AddSlot().AutoHeight().Padding(0, 1)[Entry];
    }
}
void SDirectOrScutWorkstationWidget::Tick(const FGeometry &G, double Now, float Delta)
{
    SCompoundWidget::Tick(G, Now, Delta);
    if (!Sim.IsValid() || !Rig.IsValid())
    {
        return;
    }
    Sim->ServiceDirector(); // Presentation remains responsive while automatic simulation is paused.

    for (int32 Feed = 1; Feed <= 8; ++Feed)
    {
        if (TerminalMaterials[Feed].IsValid() && OpticImages[Feed].IsValid())
        {
            const FGeometry &OpticGeometry = OpticImages[Feed]->GetCachedGeometry();
            const FVector2f Pixels(OpticGeometry.GetLocalSize() * OpticGeometry.Scale);
            ApplyTerminalMaterialParameters(*TerminalMaterials[Feed].Get(), Rig->Targets[Feed].Get(), Pixels,
                                            static_cast<float>(FMath::Fmod(Now, 1024.0)), 1, 0);
            const int32 Room = ACompartmentCameraRig::RoomForFeed(Feed);
            TerminalMaterials[Feed]->SetVectorParameterValue(TEXT("PhosphorTint"), TerminalTint(Room));
            TerminalMaterials[Feed]->SetScalarParameterValue(
                TEXT("SignalOfflineNoise"), Sim->StationRooms[Room].Power == EPowerState::Offline ? 1.f : 0.f);
        }
    }
    const bool Resolved = Sim->ExperimentPhase == EExperimentPhase::Resolved;
    if (Resolved != bShowingEvaluation)
    {
        bShowingEvaluation = Resolved;
        bConsoleOpen = false;
        Rebuild();
        FSlateApplication::Get().SetKeyboardFocus(AsShared());
    }
    if (SeenRuntimeRevision != Sim->RuntimeRevision)
    {
        SeenRuntimeRevision = Sim->RuntimeRevision;
        bLogAutoScroll = true;
        CopiedUntil = 0;
        const int32 DebugFeed = ActiveFeed;
        ResetTerminalPresentation();
        if (bDebugEightCamGrid)
        {
            ActiveFeed = DebugFeed;
            SelectedCameraIndex = DebugFeed - 1;
        }
        if (bDebugEightCamGrid)
        {
            ShowGrid();
        }
        else
        {
            RebuildUnifiedStream();
        }
        if (!Resolved && bDebugEightCamGrid)
        {
            SelectCategory(ActiveCategory, false);
        }
    }

    if (Sim->RequestedFeed >= 0)
    {
        SelectFeed(Sim->RequestedFeed, false);
        Sim->RequestedFeed = -1;
        if (bDebugEightCamGrid && bGrid && !bConsoleOpen)
        {
            ToggleExpanded();
        }
    }
    if (Sim->FollowedCrew >= 0 && Sim->CanFollowCrew(Sim->FollowedCrew))
    {
        const int32 Crew = Sim->FollowedCrew, Room = Sim->Simulation.Bodies[Crew].Room;
        if (Crew != LastFollowedCrew || Room != LastFollowRoom || SeenFollowRevision != Sim->FollowRevision)
        {
            const int32 Feed = ACompartmentCameraRig::FeedForRoom(Room);
            if (bDebugEightCamGrid || Crew != LastFollowedCrew || SeenFollowRevision != Sim->FollowRevision)
            {
                SelectFeed(Feed, false);
            }
            else
            {
                // Passive observations share the same source-independent pacing gate as future directors.
                PresentOptic(Room, EInlinePresentationIntent::Passive);
                ActiveFeed = Feed;
                SelectedCameraIndex = Feed - 1;
            }
        }
        LastFollowedCrew = Crew;
        LastFollowRoom = Room;
    }
    else
    {
        LastFollowedCrew = -1;
        LastFollowRoom = -1;
    }
    SeenFollowRevision = Sim->FollowRevision;
    Retainer->SetVisibility((bShowingEvaluation || Sim->bWorkstationVisible) ? EVisibility::Visible
                                                                             : EVisibility::Hidden);
    Rig->SetVisibleFeeds(ActiveFeed, -1, true); // All eight remain active optical sources.
    UpdateTerminalPresentation(Delta, G);
    if (SeenMenuRevision != Sim->MenuRevision)
    {
        switch (Sim->ActiveMenu)
        {
        case EShipAIScreenMenu::AI:
            ActiveCategory = 0;
            break;
        case EShipAIScreenMenu::Events:
            ActiveCategory = 1;
            break;
        case EShipAIScreenMenu::Crew:
            ActiveCategory = 2;
            break;
        case EShipAIScreenMenu::DoorRoom:
            ActiveCategory = 3;
            break;
        case EShipAIScreenMenu::Experiment:
            ActiveCategory = 4;
            break;
        default:
            break;
        }
        RebuildMenu();
        SeenMenuRevision = Sim->MenuRevision;
    }
    MenuRefreshClock += Delta;
    if (MenuRefreshClock >= .2f)
    {
        if (Sim->ActiveMenu != EShipAIScreenMenu::None)
        {
            RefreshMenuChoices();
        }
        MenuRefreshClock = 0;
    }
    if (SeenEvents != Sim->EventRevision)
    {
        RefreshLog();
    }
    // Re-issue after layout settles: wrapped output changes the scroll extent on the following frame.
    if (bLogAutoScroll)
    {
        LogScroll->ScrollToEnd();
    }
}

TSharedRef<SWidget> SDirectOrScutWorkstationWidget::BuildEvaluationDossier()
{
    auto Content = SNew(SVerticalBox);
    auto Text = [&](const FString &Value, int32 Size = 12)
    {
        Content->AddSlot().AutoHeight().Padding(0, 5)[SNew(STextBlock)
                                                          .Text(FText::FromString(Value))
                                                          .Font(FCoreStyle::GetDefaultFontStyle("Regular", Size))
                                                          .ColorAndOpacity(FLinearColor(.4f, 1, .55f))
                                                          .AutoWrapText(true)];
    };
    Text(TEXT("CORPORATE EVALUATION DOSSIER // POST-EXPERIMENT"), 20);
    if (Sim->ExperimentPhase == EExperimentPhase::Resolved)
    {
        Text(FString::Printf(TEXT("%s\n%s | DURATION %.2f / 600.00 s | OBJECTIVE %.1f%% | PEAK SUSPICION %.1f%%"),
                             Sim->EndingName(Sim->ExperimentEnding), *Sim->Simulation.Scenario.ScenarioId,
                             Sim->Simulation.TickIndex / 30., Sim->Simulation.ObjectiveProgress * 100,
                             Sim->PeakSuspicion * 100),
             14);
        Text(TEXT("SUBJECT FINAL METRICS // COMPLIANCE / COMPOSURE / SUSPICION (%)"), 13);
        auto Table = SNew(SGridPanel);
        const TCHAR *Headers[] = {TEXT("SUBJECT"), TEXT("COMPLIANCE"), TEXT("COMPOSURE"), TEXT("SUSPICION"),
                                  TEXT("STRESS"),  TEXT("INTEGRITY"),  TEXT("BPM"),       TEXT("PUPIL mm")};
        for (int32 C = 0; C < 8; ++C)
        {
            Table->AddSlot(C, 0).Padding(8, 5)[SNew(STextBlock)
                                                   .Text(FText::FromString(Headers[C]))
                                                   .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                                                   .ColorAndOpacity(FLinearColor(.4f, 1, .55f))];
        }
        const TCHAR *Names[] = {TEXT("VALE"), TEXT("CHEN"), TEXT("ORTIZ"), TEXT("ASH")};
        for (int32 I = 0; I < 4; ++I)
        {
            const auto &U = Sim->Simulation.Bodies[I];
            const auto &P = U.Profile;
            const FString Cells[] = {Names[I],
                                     FString::Printf(TEXT("%.1f"), P.Compliance * 100),
                                     FString::Printf(TEXT("%.1f"), P.Composure * 100),
                                     FString::Printf(TEXT("%.1f"), P.Suspicion * 100),
                                     FString::Printf(TEXT("%.1f"), U.Stress),
                                     FString::Printf(TEXT("%.1f"), U.Integrity),
                                     FString::Printf(TEXT("%.1f"), P.HeartRateBPM),
                                     FString::Printf(TEXT("%.2f"), P.PupilDilationMM)};
            for (int32 C = 0; C < 8; ++C)
            {
                Table->AddSlot(C, I + 1).Padding(8, 5)[SNew(STextBlock)
                                                           .Text(FText::FromString(Cells[C]))
                                                           .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                                                           .ColorAndOpacity(FLinearColor(.7f, 1, .75f))];
            }
        }
        Content->AddSlot().AutoHeight()[SNew(SScrollBox).Orientation(Orient_Horizontal) + SScrollBox::Slot()[Table]];
        for (int32 I = 0; I < 4; ++I)
        {
            Text(FString(Names[I]) + TEXT(" // ") + Sim->AgendaTelemetry(I, true), 12);
        }
        Text(TEXT("INCIDENT LOG // CHRONOLOGICAL HIGHLIGHTS"), 13);
        Text(EvaluationHighlights(), 11);
    }
    Content->AddSlot().AutoHeight()[SNew(STextBlock)
                                        .AutoWrapText(true)
                                        .Text_Lambda([this]() { return FText::FromString(Sim->IncidentCaption()); })];
    Content->AddSlot().AutoHeight().Padding(
        0, 6)[SNew(SEditableTextBox)
                  .HintText(FText::FromString(TEXT("CMD> incident_reel play | next | prev | stop")))
                  .OnTextCommitted_Lambda(
                      [this](const FText &Value, ETextCommit::Type Type)
                      {
                          if (Type == ETextCommit::OnEnter)
                          {
                              Sim->QueueConsoleCommand(Value.ToString());
                          }
                      })];
    auto Actions = SNew(SHorizontalBox);
    Actions->AddSlot().AutoWidth()[BuildIncidentReelControls()];
    Actions->AddSlot().FillWidth(1).Padding(6)[SNew(SButton)
                                                   .Tag(TEXT("ScutNewExperiment"))
                                                   .Text(FText::FromString(TEXT("[NEW EXPERIMENT]")))
                                                   .OnClicked_Lambda(
                                                       [this]()
                                                       {
                                                           Sim->QueueConsoleCommand(TEXT("reset_experiment"));
                                                           return FReply::Handled().SetUserFocus(AsShared());
                                                       })];
    Actions->AddSlot().FillWidth(1).Padding(6)[SNew(SButton)
                                                   .Tag(TEXT("ScutSwitchScenario"))
                                                   .Text(FText::FromString(TEXT("[SWITCH SCENARIO]")))
                                                   .OnClicked_Lambda(
                                                       [this]()
                                                       {
                                                           Sim->QueueConsoleCommand(Sim->bMedicalScenario
                                                                                        ? TEXT("scenario behavioral")
                                                                                        : TEXT("scenario medical"));
                                                           return FReply::Handled().SetUserFocus(AsShared());
                                                       })];
    return SNew(SBorder)
        .Tag(TEXT("ScutEvaluationDossier"))
        .Visibility_Lambda(
            [this]()
            {
                return Sim->ExperimentPhase == EExperimentPhase::Resolved ? EVisibility::Visible
                                                                          : EVisibility::Collapsed;
            })
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.18f, .8f, .35f))
        .Padding(
            2)[SNew(SBorder)
                   .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                   .BorderBackgroundColor(FLinearColor(.006f, .018f, .01f))
                   .Padding(18)[SNew(SVerticalBox) +
                                SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot()[Content]] +
                                SVerticalBox::Slot().AutoHeight()[Actions]]];
}

TSharedRef<SWidget> SDirectOrScutWorkstationWidget::BuildIncidentReelControls()
{
    auto Reel = SNew(SHorizontalBox);
    const TCHAR *ReelLabels[] = {TEXT("[PLAY INCIDENT REEL]"), TEXT("[PREV]"), TEXT("[NEXT]"), TEXT("[EXIT REEL]")};
    const TCHAR *ReelCommands[] = {TEXT("incident_reel play"), TEXT("incident_reel prev"), TEXT("incident_reel next"),
                                   TEXT("incident_reel stop")};
    for (int32 I = 0; I < 4; ++I)
    {
        const FString Command = ReelCommands[I];
        Reel->AddSlot().AutoWidth().Padding(4)[SNew(SButton)
                                                   .Tag(FName(*FString::Printf(TEXT("ScutReel%d"), I)))
                                                   .Text(FText::FromString(ReelLabels[I]))
                                                   .OnClicked_Lambda(
                                                       [this, Command]()
                                                       {
                                                           Sim->QueueConsoleCommand(Command);
                                                           return FReply::Handled();
                                                       })];
    }
    return Reel;
}

FString SDirectOrScutWorkstationWidget::EvaluationHighlights() const
{
    FString Result;
    for (const auto &Cue : Sim->CinematicCues)
    {
        if (Cue.Weight < .50f)
        {
            continue;
        }
        Result += FString::Printf(TEXT("[%07.2f s] %s | %.2f | %s\n"), Cue.Tick / 30.,
                                  DirectOrScut::RoomName(int32(Cue.RoomId)), Cue.Weight, *Cue.Subtitle);
    }
    return Result;
}

TSharedRef<SWidget> SDirectOrScutWorkstationWidget::BuildWorkstationControls()
{
    auto Controls = SNew(SHorizontalBox).Tag(TEXT("ScutWorkstationControls"));
    Controls->AddSlot().AutoWidth().Padding(
        0, 0, 5, 0)[PhosphorSoftkey(TEXT("ScutCommandFocus"), FText::FromString(TEXT("[CMD]")),
                                    FSimpleDelegate::CreateLambda([this]() { SetConsoleOpen(true); }))];
    auto Inspect = PhosphorSoftkey(TEXT("ScutControlInspect"), FText::FromString(TEXT("[INSPECT]")),
                                   FSimpleDelegate::CreateLambda(
                                       [this]()
                                       {
                                           SetConsoleOpen(false);
                                           ExpandSelected();
                                       }));
    auto Grid = PhosphorSoftkey(TEXT("ScutControlGrid"), FText::FromString(TEXT("[GRID]")),
                                FSimpleDelegate::CreateLambda(
                                    [this]()
                                    {
                                        SetConsoleOpen(false);
                                        ShowGrid();
                                    }));
    for (auto Button : {Inspect, Grid})
    {
        Button->SetVisibility(TAttribute<EVisibility>::CreateLambda(
            [this]() { return bDebugEightCamGrid ? EVisibility::Visible : EVisibility::Collapsed; }));
    }
    Controls->AddSlot().AutoWidth().Padding(0, 0, 5, 0)[Inspect];
    Controls->AddSlot().AutoWidth().Padding(0, 0, 5, 0)[Grid];
    Controls->AddSlot().AutoWidth()[PhosphorSoftkey(
        TEXT("ScutDiagnostics"), FText::FromString(TEXT("[DIAGNOSTICS]")),
        FSimpleDelegate::CreateLambda([this]() { SetDebugEightCamGrid(!bDebugEightCamGrid); }))];
    return Controls;
}

TSharedRef<SWidget> SDirectOrScutWorkstationWidget::BuildRoomQuickActions()
{
    auto Actions = SNew(SHorizontalBox);
    const TCHAR *Labels[] = {TEXT("[LURE HERE]"), TEXT("[PWR CYCLE]"), TEXT("[SEAL/UNSEAL]"), TEXT("[SPOOF LOG]")};
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const int32 Action = Index;
        Actions->AddSlot().AutoWidth().Padding(
            2)[SNew(SButton)
                   .Tag(FName(*FString::Printf(TEXT("ScutQuickAction%d"), Index)))
                   .IsFocusable(false)
                   .IsEnabled_Lambda(
                       [this]() { return Sim->ExperimentPhase == EExperimentPhase::Active && !Sim->IsUplinkLocked(); })
                   .OnClicked_Lambda(
                       [this, Action]()
                       {
                           ExecuteRoomQuickAction(Action);
                           return FReply::Handled().SetUserFocus(AsShared());
                       })[SNew(STextBlock)
                              .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                              .Text(FText::FromString(Labels[Index]))]];
    }
    return SNew(SVerticalBox) +
           SVerticalBox::Slot()
               .AutoHeight()[SNew(STextBlock)
                                 .Tag(TEXT("ScutQuickActionContext"))
                                 .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                                 .ColorAndOpacity_Lambda(
                                     [this]()
                                     {
                                         return Sim->IsUplinkLocked() ? FLinearColor(1.f, .15f, .1f)
                                                                      : FLinearColor(1.f, .7f, .2f);
                                     })
                                 .Text_Lambda(
                                     [this]()
                                     {
                                         return FText::FromString(FString::Printf(
                                             TEXT("[%d] %s // UPLINK %.0f%% // %s"), ActiveFeed,
                                             DirectOrScut::RoomName(ACompartmentCameraRig::RoomForFeed(ActiveFeed)),
                                             Sim->UplinkHeatPct,
                                             *(Sim->IsUplinkLocked() ? FString::Printf(TEXT("AI LATENCY %.1fs"),
                                                                                       Sim->UplinkLockoutSeconds())
                                                                     : FString(TEXT("READY")))));
                                     })] +
           SVerticalBox::Slot().AutoHeight()[Actions];
}

void SDirectOrScutWorkstationWidget::ExecuteRoomQuickAction(int32 Action)
{
    if (Sim->ExperimentPhase != EExperimentPhase::Active || !Sim->bWorkstationVisible)
    {
        return;
    }
    const int32 Room = ACompartmentCameraRig::RoomForFeed(ActiveFeed);
    const FString Name = DirectOrScut::RoomName(Room);
    FString Command;
    if (Action == 0)
    {
        Command = TEXT("intercom_lie ash ") + Name;
    }
    else if (Action == 1)
    {
        const EPowerState Power = Sim->StationRooms[Room].Power;
        Command = TEXT("power ") + Name +
                  (Power == EPowerState::Nominal ? TEXT(" low")
                   : Power == EPowerState::Low   ? TEXT(" off")
                                                 : TEXT(" nominal"));
    }
    else if (Action == 2)
    {
        Command = (Sim->IsRoomSealed(Room) ? TEXT("unseal ") : TEXT("seal ")) + Name;
    }
    else if (Action == 3)
    {
        Command = TEXT("spoof_log ") + Name;
    }
    if (!Command.IsEmpty())
    {
        Sim->QueueConsoleCommand(Command);
    }
}

namespace
{
float TerminalUnit(float Value)
{
    return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, 1.f) : 0.f;
}
bool TerminalSize(const FVector2f &Size)
{
    return FMath::IsFinite(Size.X) && FMath::IsFinite(Size.Y) && Size.X > 0 && Size.Y > 0 && Size.X <= 32768 &&
           Size.Y <= 32768;
}
} // namespace
void SDirectOrScutWorkstationWidget::InitializePhosphorCurve()
{
    PhosphorCurve.Reset(NewObject<UCurveLinearColor>(GetTransientPackage()));
    const float Times[] = {0, .25f, .65f, 1};
    const FLinearColor Colors[] = {
        {.002f, .006f, .002f, 1}, {.008f, .06f, .012f, 1}, {.08f, .45f, .12f, 1}, {.65f, 1, .72f, 1}};
    for (int32 Channel = 0; Channel < 4; ++Channel)
    {
        FRichCurve &Curve = PhosphorCurve->FloatCurves[Channel];
        Curve.PreInfinityExtrap = RCCE_Constant;
        Curve.PostInfinityExtrap = RCCE_Constant;
        for (int32 Key = 0; Key < 4; ++Key)
        {
            Curve.SetKeyInterpMode(Curve.AddKey(Times[Key], Colors[Key].Component(Channel)), RCIM_Linear);
        }
    }
}
FLinearColor SDirectOrScutWorkstationWidget::EvaluatePhosphorColor(float Intensity, float Amber, float Hypoxia,
                                                                   float Offline, float Deception) const
{
    const float I = TerminalUnit(Intensity);
    FLinearColor Color = PhosphorCurve.IsValid() ? PhosphorCurve->GetLinearColorValue(I) : FLinearColor::Black;
    Color = FMath::Lerp(Color, FLinearColor(I, .52f * I, .025f * I, 1), TerminalUnit(Amber));
    Color = FMath::Lerp(Color, FLinearColor(.32f * I, .18f * I, .05f * I, 1), TerminalUnit(Hypoxia) * .65f);
    Color = FMath::Lerp(Color, FLinearColor(.12f * I, .16f * I, .13f * I, 1), TerminalUnit(Offline));
    Color = FMath::Lerp(Color, FLinearColor(.6f * I, .7f * I, .25f * I, 1), TerminalUnit(Deception) * .06f);
    Color.A = 1;
    return Color;
}
void SDirectOrScutWorkstationWidget::BuildCompartmentOutline(int32 Room, const FVector2f &Size,
                                                             TArray<FVector2f> &Out) const
{
    Out.Reset();
    if (Room < 0 || Room > 7 || !TerminalSize(Size))
    {
        return;
    }
    if (const auto *Cached = OutlineCache.Find(Room); Cached && Cached->Key == Size)
    {
        Out = Cached->Value;
        return;
    }
    // Octagonal core/ring boundaries and chamfered pods from canonical station dimensions.
    const float Scale = FMath::Min(Size.X, Size.Y) / 5200.f;
    const auto Project = [&](FVector2D P) { return FVector2f(Size.X * .5f + P.X * Scale, Size.Y * .5f - P.Y * Scale); };
    if (Room < 2)
    {
        const float Radius = Room == 0 ? 500.f : DirectOrScut::OuterRadius;
        for (int32 I = 0; I < 8; ++I)
        {
            Out.Add(Project(FVector2D(DirectOrScut::Radial(22.5f + I * 45.f, Radius))));
            // Core access spurs share the canonical +/-140cm lateral bounds.
            if (Room == 0 && (I == 2 || I == 6))
            {
                const float Angle = 45.f + I * 45.f;
                const FVector2D Normal(DirectOrScut::Radial(Angle, 1));
                const FVector2D Tangent(DirectOrScut::Radial(Angle + 90, 1));
                const float Start = Radius * FMath::Cos(PI / 8);
                const float End = DirectOrScut::InnerRadius * FMath::Cos(PI / 8);
                Out.Add(Project(Normal * Start - Tangent * 140));
                Out.Add(Project(Normal * End - Tangent * 140));
                Out.Add(Project(Normal * End + Tangent * 140));
                Out.Add(Project(Normal * Start + Tangent * 140));
            }
        }
    }
    else
    {
        const float Angle = DirectOrScut::Stations()[Room - 2].Face * 45.f;
        const FVector2D X(DirectOrScut::Radial(Angle, 1)), Y(DirectOrScut::Radial(Angle + 90, 1));
        const FVector2D Center = X * DirectOrScut::PodCenterRadius();
        for (const FVector2D &P : DirectOrScut::Octagon(DirectOrScut::PodLength, DirectOrScut::PodWidth, 90))
        {
            Out.Add(Project(Center + X * P.X + Y * P.Y));
        }
    }
    if (!Out.IsEmpty())
    {
        Out.Add(FVector2f(Out[0]));
    }
    OutlineCache.Add(Room, TPair<FVector2f, TArray<FVector2f>>(Size, Out));
}
void SDirectOrScutWorkstationWidget::BuildReticlePoints(const FVector2f &Center, float Radius, int32 Count, float Gap,
                                                        TArray<TArray<FVector2f>> &Out) const
{
    Out.Reset();
    if (!FMath::IsFinite(Center.X) || !FMath::IsFinite(Center.Y) || !FMath::IsFinite(Radius) || !FMath::IsFinite(Gap))
    {
        return;
    }
    Radius = FMath::Clamp(Radius, 0.f, 32768.f);
    Count = FMath::Clamp(Count, 1, 64);
    const float Sector = 2 * PI / Count;
    Gap = FMath::Clamp(Gap, 0.f, Sector * .95f);
    if (ReticleCenter == Center && ReticleRadius == Radius && ReticleSegments == Count && ReticleGap == Gap)
    {
        Out = ReticleCache;
        return;
    }
    for (int32 Arc = 0; Arc < Count && Radius > 0; ++Arc)
    {
        TArray<FVector2f> Points;
        for (int32 I = 0; I < 32; ++I)
        {
            const float A = Arc * Sector + Gap * .5f + (Sector - Gap) * I / 31.f;
            Points.Add(Center + FVector2f(FMath::Cos(A), FMath::Sin(A)) * Radius);
        }
        Out.Add(MoveTemp(Points));
    }
    ReticleCenter = Center;
    ReticleRadius = Radius;
    ReticleSegments = Count;
    ReticleGap = Gap;
    ReticleCache = Out;
}
void SDirectOrScutWorkstationWidget::BuildPulseTrace(const FVector2f &Size, float Phase, float BPM,
                                                     TArray<FVector2f> &Out) const
{
    Out.Reset();
    if (!TerminalSize(Size) || !FMath::IsFinite(Phase) || !FMath::IsFinite(BPM))
    {
        return;
    }
    BPM = FMath::Clamp(BPM, 0.f, 240.f);
    Phase = FMath::Fmod(Phase, 1024.f);
    for (int32 I = 0; I < 256; ++I)
    {
        const float X = I / 255.f;
        const float T = FMath::Frac(X * 4 + Phase * BPM / 60.f + 1024.f);
        const auto Peak = [T](float Center, float Width)
        { return FMath::Max(0.f, 1 - FMath::Abs(T - Center) / Width); };
        const float Pulse = BPM == 0 ? 0
                                     : .12f * Peak(.18f, .06f) - .18f * Peak(.37f, .025f) + Peak(.4f, .025f) -
                                           .3f * Peak(.44f, .03f) + .22f * Peak(.65f, .1f);
        Out.Add(FVector2f(X * Size.X, Size.Y * (.55f - .4f * Pulse)));
    }
}
void SDirectOrScutWorkstationWidget::DrawTerminalLines(FSlateWindowElementList &Elements, int32 Layer,
                                                       const FGeometry &Geometry, const TArray<FVector2f> &Points,
                                                       FLinearColor Color) const
{
    if (Points.Num() < 2 || Points.Num() > 2048)
    {
        return;
    }
    TArray<FVector2f> Path;
    for (const auto &P : Points)
    {
        if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y))
        {
            return;
        }
        if (Path.IsEmpty() || !Path.Last().Equals(P, .001f))
        {
            Path.Add(P);
        }
    }
    if (Path.Num() > 1)
    {
        FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Path, ESlateDrawEffect::None, Color,
                                     true, 1.f);
    }
}
void SDirectOrScutWorkstationWidget::BuildSealIndicator(const FVector2f &Center, float Radius, bool Sealed,
                                                        TArray<FVector2f> &Out) const
{
    Out.Reset();
    if (!FMath::IsFinite(Center.X) || !FMath::IsFinite(Center.Y) || !FMath::IsFinite(Radius) || Radius <= 0)
    {
        return;
    }
    Radius = FMath::Min(Radius, 128.f);
    Out = {Center + FVector2f(-Radius, -Radius), Center + FVector2f(Radius, Radius)};
    if (Sealed)
    {
        Out.Add(Center);
        Out.Add(Center + FVector2f(-Radius, Radius));
        Out.Add(Center + FVector2f(Radius, -Radius));
    }
}
void SDirectOrScutWorkstationWidget::BuildRasterOverlay(const FVector2f &Size, float Progress,
                                                        TArray<TArray<FVector2f>> &Out) const
{
    Out.Reset();
    if (!TerminalSize(Size))
    {
        return;
    }
    const float Bottom = TerminalUnit(Progress) * Size.Y;
    const float Pitch = FMath::Max(2.f, Size.Y / 512.f);
    for (float Y = 0; Y < Bottom; Y += Pitch)
    {
        Out.Add({FVector2f(0, Y), FVector2f(Size.X, Y)});
    }
}
void SDirectOrScutWorkstationWidget::ApplyTerminalMaterialParameters(UMaterialInstanceDynamic &Material,
                                                                     UTexture *Texture, const FVector2f &Size,
                                                                     float Time, float Wipe, float Deception) const
{
    Material.SetTextureParameterValue(TEXT("OpticRenderTarget"), Texture ? Texture : NeutralOptic.Get());
    Material.SetVectorParameterValue(
        TEXT("ViewportSize"), FLinearColor(TerminalSize(Size) ? Size.X : 1, TerminalSize(Size) ? Size.Y : 1, 0, 0));
    Material.SetVectorParameterValue(TEXT("PhosphorTint"), EvaluatePhosphorColor(1));
    Material.SetScalarParameterValue(TEXT("PresentationTimeSeconds"),
                                     FMath::IsFinite(Time) ? FMath::Fmod(FMath::Max(0.f, Time), 1024.f) : 0);
    Material.SetScalarParameterValue(TEXT("WipeProgress"), TerminalUnit(Wipe));
    Material.SetScalarParameterValue(TEXT("DeceptionAmount"), TerminalUnit(Deception));
    Material.SetScalarParameterValue(TEXT("DeceptionSyncTear"), TerminalUnit(Deception) * .01f);
    for (const TCHAR *Name : {TEXT("ScrubTrackingNoise"), TEXT("SignalOfflineNoise"), TEXT("VoiceBleedIntensity")})
    {
        Material.SetScalarParameterValue(Name, 0);
    }
}
void SDirectOrScutWorkstationWidget::SetDebugEightCamGrid(bool Enabled)
{
    if (bDebugEightCamGrid == Enabled)
    {
        return;
    }
    bDebugEightCamGrid = Enabled;
    Retainer->SetEffectMaterial(Enabled ? nullptr : TerminalSurfaceMaterial.Get());
    Retainer->SetRetainedRendering(!Enabled);
    bGrid = Enabled;
    bFullscreen = false;
    bMenuFocused = false;
    // Only rebuild presentation. In particular do not queue camera inspection or menu commands here.
    Rebuild();
}
void SDirectOrScutWorkstationWidget::SetPresentationNoise(float Scrub, float Voice)
{
    ScrubTrackingNoise = FMath::IsFinite(Scrub) ? FMath::Clamp(Scrub, 0.f, 1.f) : 0;
    VoiceBleedIntensity = FMath::IsFinite(Voice) ? FMath::Clamp(Voice, 0.f, 1.f) : 0;
}
const FTerminalStreamEntry *SDirectOrScutWorkstationWidget::FindTerminalEntry(uint64 Id) const
{
    return TerminalStream.FindByPredicate([Id](const FTerminalStreamEntry &Entry) { return Entry.Id == Id; });
}
TSharedPtr<FInlineOpticSlot> SDirectOrScutWorkstationWidget::FindInlineOpticSlot(uint64 Id, int32 SubjectIndex) const
{
    for (const auto &Slot : LiveInlineOptics)
    {
        if (Slot->EntryId == Id && Slot->SubjectIndex == SubjectIndex)
        {
            return Slot;
        }
    }
    return nullptr;
}
float SDirectOrScutWorkstationWidget::GetInlineWipeProgress() const
{
    const auto Slot = FindInlineOpticSlot(ActiveOpticEntryId);
    return Slot ? Slot->Wipe : 0;
}
UMaterialInstanceDynamic *SDirectOrScutWorkstationWidget::GetInlineOpticMaterial() const
{
    const auto Slot = FindInlineOpticSlot(ActiveOpticEntryId);
    return Slot ? Slot->Material.Get() : nullptr;
}
int32 SDirectOrScutWorkstationWidget::GetInlineInstrumentCount() const
{
    int32 Count = 0;
    for (const auto &Entry : TerminalStream)
    {
        Count += Entry.Attachment.IsSet() && Entry.Attachment->Kind != EInlineAttachmentKind::OpticFeed;
    }
    return Count;
}
int32 SDirectOrScutWorkstationWidget::GetCollapsedOpticReferenceCount() const
{
    int32 Count = 0;
    for (const auto &Entry : TerminalStream)
    {
        Count += Entry.Attachment.IsSet() && Entry.Attachment->Kind == EInlineAttachmentKind::OpticFeed &&
                 !FindInlineOpticSlot(Entry.Id);
    }
    return Count;
}
int32 SDirectOrScutWorkstationWidget::GetInlineOpticMaterialCount() const
{
    int32 Count = 0;
    for (const auto &Slot : InlineOpticPool)
    {
        Count += Slot->Material.IsValid();
    }
    return Count;
}
void SDirectOrScutWorkstationWidget::ReleaseInlineOptic(TSharedRef<FInlineOpticSlot> Slot)
{
    // UE ignores null texture setters; explicitly replace the render target with the owned neutral pixel.
    if (Slot->Material.IsValid())
    {
        Slot->Material->SetTextureParameterValue(TEXT("OpticRenderTarget"), NeutralOptic.Get());
    }
    Slot->Brush.SetResourceObject(nullptr);
    Slot->Image.Reset();
    Slot->EntryId = 0;
    Slot->Wipe = 0;
}
bool SDirectOrScutWorkstationWidget::CanEmitPassiveAttachment(const FInlineAttachmentRef &Ref) const
{
    const auto Age = [this](float Time) { return FMath::Fmod(PresentationSeconds - Time + 1024.f, 1024.f); };
    // Bound all passive traffic, then deduplicate content regardless of who requested it.
    if (LastPassiveAttachmentAt >= 0 && Age(LastPassiveAttachmentAt) < 2.f)
    {
        return false;
    }
    for (const auto &Entry : TerminalStream)
    {
        if (!Entry.Attachment.IsSet())
        {
            continue;
        }
        const auto &Old = Entry.Attachment.GetValue();
        if (Old.Kind != Ref.Kind || Old.SubjectCount != Ref.SubjectCount || Old.bSideBySide != Ref.bSideBySide ||
            Old.Subject != Ref.Subject || (Ref.SubjectCount == 2 && Old.Subjects[1] != Ref.Subjects[1]))
        {
            continue;
        }
        if (Age(Entry.PresentedAt) < 8.f || FindInlineOpticSlot(Entry.Id))
        {
            return false;
        }
    }
    return true;
}
bool SDirectOrScutWorkstationWidget::AppendTerminalEntry(FTerminalStreamEntry Entry, EInlinePresentationIntent Intent)
{
    if (Entry.Attachment.IsSet() && Intent == EInlinePresentationIntent::Passive)
    {
        if (!CanEmitPassiveAttachment(Entry.Attachment.GetValue()))
        {
            return false;
        }
        LastPassiveAttachmentAt = PresentationSeconds;
    }
    if (TerminalStream.Num() >= 128)
    {
        const uint64 Evicted = TerminalStream[0].Id;
        for (int32 I = LiveInlineOptics.Num() - 1; I >= 0; --I)
        {
            if (LiveInlineOptics[I]->EntryId == Evicted)
            {
                ReleaseInlineOptic(LiveInlineOptics[I].ToSharedRef());
                LiveInlineOptics.RemoveAt(I);
            }
        }
        TerminalStream.RemoveAt(0);
    }
    Entry.Id = NextTerminalEntryId++;
    Entry.Text = Entry.Text.Left(2048);
    Entry.PresentedAt = PresentationSeconds;
    if (Entry.Attachment.IsSet())
    {
        Entry.Attachment->Subjects[0] = Entry.Attachment->Subject;
    }
    BindInlineOptics(Entry);
    TerminalStream.Add(MoveTemp(Entry));
    if (!bDebugEightCamGrid && Log.IsValid())
    {
        RebuildUnifiedStream();
    }
    return true;
}
void SDirectOrScutWorkstationWidget::BindInlineOptics(const FTerminalStreamEntry &Entry)
{
    if (Entry.Attachment.IsSet() && Entry.Attachment->Kind == EInlineAttachmentKind::OpticFeed)
    {
        for (int32 Index = LiveInlineOptics.Num() - 1; Index >= 0; --Index)
        {
            if (LiveInlineOptics[Index]->EntryId == Entry.Id)
            {
                ReleaseInlineOptic(LiveInlineOptics[Index].ToSharedRef());
                LiveInlineOptics.RemoveAt(Index);
            }
        }
        while (LiveInlineOptics.Num() + Entry.Attachment->SubjectCount > 2)
        {
            const uint64 OldId = LiveInlineOptics[0]->EntryId;
            for (int32 Index = LiveInlineOptics.Num() - 1; Index >= 0; --Index)
            {
                if (LiveInlineOptics[Index]->EntryId == OldId)
                {
                    ReleaseInlineOptic(LiveInlineOptics[Index].ToSharedRef());
                    LiveInlineOptics.RemoveAt(Index);
                }
            }
        }
        for (int32 SubjectIndex = 0; SubjectIndex < Entry.Attachment->SubjectCount; ++SubjectIndex)
        {
            const int32 Feed = ACompartmentCameraRig::FeedForRoom(Entry.Attachment->Subjects[SubjectIndex]);
            TSharedPtr<FInlineOpticSlot> Slot;
            for (const auto &Candidate : InlineOpticPool)
            {
                if (Candidate->EntryId == 0)
                {
                    Slot = Candidate;
                    break;
                }
            }
            if (!Slot)
            {
                check(InlineOpticPool.Num() < 2);
                Slot = MakeShared<FInlineOpticSlot>();
                InlineOpticPool.Add(Slot);
            }
            Slot->EntryId = Entry.Id;
            Slot->SubjectIndex = SubjectIndex;
            if (TerminalMaterials[Feed].IsValid())
            {
                if (!Slot->Material.IsValid())
                {
                    Slot->Material.Reset(
                        UMaterialInstanceDynamic::Create(TerminalMaterials[Feed]->Parent, GetTransientPackage()));
                }
                ApplyTerminalMaterialParameters(*Slot->Material.Get(), Rig->Targets[Feed].Get(), FVector2f(900, 506),
                                                PresentationSeconds, 0, 0);
                Slot->Brush.SetResourceObject(Slot->Material.Get());
            }
            else
            {
                Slot->Brush.SetResourceObject(Rig->Targets[Feed].Get());
            }
            Slot->Brush.ImageSize = FVector2D(1024, 576);
            Slot->Brush.DrawAs = ESlateBrushDrawType::Image;
            LiveInlineOptics.Add(Slot);
        }
    }
}
void SDirectOrScutWorkstationWidget::AppendTerminalProse(const FString &Text)
{
    FTerminalStreamEntry Entry;
    Entry.Text = Text;
    AppendTerminalEntry(MoveTemp(Entry));
}
void SDirectOrScutWorkstationWidget::ResetTerminalPresentation()
{
    bForensicScrub = bScrubPlaying = false;
    ScrubOffset = 0;
    History.Reset();
    HistoryCursor = 0;
    HistoryDraft.Empty();
    Completions.Reset();
    LastCompletion.Empty();
    for (const auto &Slot : LiveInlineOptics)
    {
        ReleaseInlineOptic(Slot.ToSharedRef());
    }
    LiveInlineOptics.Reset();
    TerminalStream.Empty(128);
    NextTerminalEntryId = 1;
    ActiveOpticEntryId = 0;
    for (int32 I = 0; I < 4; ++I)
    {
        ReportedCrewRooms[I] = Sim->Simulation.Bodies[I].Room;
        bCrewReportOverride[I] = false;
    }
    PresentationSeconds = 0;
    LastPassiveAttachmentAt = -1;
    SetPresentationNoise(0, 0);
    UnifiedEventCursor = Sim->Events.Num();
    ReportedConditions.Reset();
    PendingReports.Reset();
    ActiveFeed = ACompartmentCameraRig::FeedForRoom(0);
    SelectedCameraIndex = ActiveFeed - 1;
    if (!Sim->bLegacyExperiment)
    {
        AppendTerminalProse(TEXT("THETA-9 // INDUSTRIAL CARRIER\n")
                                TEXT("MOTHER-7 REMOTE SUPERVISORY INTERFACE\n")
                                    TEXT("DIRECTOR SCUT // SESSION ESTABLISHED\n")
                                        TEXT("COMMUNICATIONS: ACTIVE\n")
                                            TEXT("INCIDENT STATUS: OPEN"));
        // A reset/reconstructed widget must retain the deterministic opening, without replaying old reports.
        for (const auto &Event : Sim->Events)
        {
            if (Event.Text.Contains(TEXT("MOTHER-7> Good morning, Director Scut.")))
            {
                RouteConversationEvent(Event.Text, Event.Room);
                break;
            }
        }
        return;
    }
    AppendTerminalProse(TEXT("+----------------------------------------------+\n")
                            TEXT("|CARRIER: THETA-9 // MOTHER-7                  |\n")
                                TEXT("|STATION OPERATIONS / PRIVATE TTY              |\n")
                                    TEXT("|                                              |\n")
                                        TEXT("|NOTICE: VERIFY SHIP AI CLAIMS BEFORE ACTING.  |\n")
                                            TEXT("|OPTICS / TELEMETRY: AVAILABLE ON REQUEST.     |\n")
                                                TEXT("|[ENTER] OR start_experiment TO INITIALIZE.    |\n")
                                                    TEXT("+----------------------------------------------+"));
}
bool SDirectOrScutWorkstationWidget::AppendOpticReference(int32 Room, bool Focus, EInlinePresentationIntent Intent)
{
    if (!Sim.IsValid() || !Rig.IsValid() || Room < 0 || Room >= 8)
    {
        return false;
    }
    FTerminalStreamEntry Entry;
    Entry.Text = FString::Printf(TEXT("DIRECTOR> %s optical carrier: %s."), Focus ? TEXT("Present") : TEXT("Secondary"),
                                 DirectOrScut::RoomName(Room));
    Entry.Attachment = FInlineAttachmentRef{EInlineAttachmentKind::OpticFeed, Room, Sim->Simulation.TickIndex};
    const uint64 EntryId = NextTerminalEntryId;
    if (!AppendTerminalEntry(MoveTemp(Entry), Intent))
    {
        return false;
    }
    if (Focus)
    {
        ActiveFeed = ACompartmentCameraRig::FeedForRoom(Room);
        SelectedCameraIndex = ActiveFeed - 1;
        ActiveOpticEntryId = EntryId;
    }
    return true;
}
bool SDirectOrScutWorkstationWidget::PresentOptic(int32 Room, EInlinePresentationIntent Intent)
{
    if (Room >= 0 && Room < 8 && Intent == EInlinePresentationIntent::Intentional)
    {
        EvidenceRoom = Room;
        EvidenceSubject = EvidenceProbe = -1;
    }
    return AppendOpticReference(Room, true, Intent);
}
bool SDirectOrScutWorkstationWidget::PresentTelemetry(int32 Room)
{
    if (!Sim.IsValid() || Room < -1 || Room >= 8)
    {
        return false;
    }
    FTerminalTelemetrySnapshot Snapshot;
    Snapshot.Tick = Sim->Simulation.TickIndex;
    Snapshot.Room = Room;
    for (int32 R = Room < 0 ? 0 : Room; R < (Room < 0 ? 8 : Room + 1); ++R)
    {
        const auto &O = Sim->StationRooms[R];
        const auto &P = Sim->Simulation.Rooms[R];
        const TCHAR *Power = O.Power == EPowerState::Nominal ? TEXT("NOMINAL")
                             : O.Power == EPowerState::Low   ? TEXT("LOW")
                                                             : TEXT("OFFLINE");
        Snapshot.Readings +=
            FString::Printf(TEXT("%s | POWER %s | O2 %.1f%% | %s | ALERT %.1fs\n"), DirectOrScut::RoomName(R), Power,
                            O.OxygenPct, P.bIsPressurized ? TEXT("PRESSURIZED") : TEXT("VACUUM"), P.AlertSeconds);
    }
    const TCHAR *Names[] = {TEXT("VALE"), TEXT("CHEN"), TEXT("ORTIZ"), TEXT("ASH")};
    for (int32 I = 0; I < 4; ++I)
    {
        const auto &C = Sim->Simulation.Bodies[I];
        if (!C.bRegistered || (Room >= 0 && C.Room != Room))
        {
            continue;
        }
        Snapshot.Readings += FString::Printf(TEXT("%s @ %s | %.0f BPM / %.0f RPM | INTEGRITY %.0f%%\n"), Names[I],
                                             DirectOrScut::RoomName(C.Room), C.Profile.HeartRateBPM,
                                             C.Profile.RespirationRateRPM, C.Integrity);
    }
    FTerminalStreamEntry Entry;
    Entry.Text = Room < 0 ? TEXT("DIRECTOR> show_telem")
                          : FString::Printf(TEXT("DIRECTOR> show_telem %s"), DirectOrScut::RoomName(Room));
    Entry.Telemetry = MoveTemp(Snapshot);
    return AppendTerminalEntry(MoveTemp(Entry));
}
bool SDirectOrScutWorkstationWidget::PresentInstrument(ETerminalInstrument Kind, int32 Subject,
                                                       EInlinePresentationIntent Intent)
{
    if ((Kind != ETerminalInstrument::ECG && Kind != ETerminalInstrument::Schematic) || Subject < 0 ||
        Subject >= (Kind == ETerminalInstrument::ECG ? 4 : 8))
    {
        return false;
    }
    if (Intent == EInlinePresentationIntent::Intentional)
    {
        EvidenceProbe = -1;
        EvidenceSubject = Kind == ETerminalInstrument::ECG ? Subject : -1;
        EvidenceRoom = Kind == ETerminalInstrument::Schematic ? Subject : -1;
    }
    const TCHAR *Crew[] = {TEXT("VALE"), TEXT("CHEN"), TEXT("ORTIZ"), TEXT("ASH")};
    FTerminalStreamEntry Entry;
    Entry.Text = Kind == ETerminalInstrument::ECG
                     ? FString::Printf(TEXT("MOTHER-7> Subject pulse strip: %s."), Crew[Subject])
                     : FString::Printf(TEXT("MOTHER-7> Compartment geometry: %s."), DirectOrScut::RoomName(Subject));
    Entry.Attachment =
        FInlineAttachmentRef{Kind == ETerminalInstrument::ECG ? EInlineAttachmentKind::PulseStrip
                                                              : EInlineAttachmentKind::CompartmentSchematic,
                             Subject, Sim->Simulation.TickIndex};
    return AppendTerminalEntry(MoveTemp(Entry), Intent);
}
bool SDirectOrScutWorkstationWidget::PresentAttachment(EInlineAttachmentKind Kind, int32 First, int32 Second,
                                                       EInlinePresentationIntent Intent)
{
    if (Kind != EInlineAttachmentKind::OpticFeed && Kind != EInlineAttachmentKind::PulseStrip &&
        Kind != EInlineAttachmentKind::CompartmentSchematic && Kind != EInlineAttachmentKind::OcularScan)
    {
        return false;
    }
    const bool Crew = Kind == EInlineAttachmentKind::PulseStrip || Kind == EInlineAttachmentKind::OcularScan;
    if (Intent == EInlinePresentationIntent::Intentional)
    {
        EvidenceProbe = -1;
        EvidenceSubject = Crew ? First : -1;
        EvidenceRoom = Crew ? -1 : First;
    }
    const int32 Limit = Crew ? 4 : 8;
    if (!Sim.IsValid() || !Rig.IsValid() || First < 0 || First >= Limit || Second < -1 || Second >= Limit ||
        Second == First || (Kind == EInlineAttachmentKind::OcularScan && Second != -1))
    {
        return false;
    }
    FTerminalStreamEntry Entry;
    Entry.Text = TEXT("DIRECTOR> Requested comparative telemetry // PRESENTATION ONLY.");
    FInlineAttachmentRef Ref{Kind, First, Sim->Simulation.TickIndex};
    Ref.Subjects[0] = First;
    Ref.Subjects[1] = Second < 0 ? First : Second;
    Ref.SubjectCount = Second < 0 ? 1 : 2;
    Ref.bSideBySide = Ref.SubjectCount == 2 && !Crew;
    Entry.Attachment = Ref;
    return AppendTerminalEntry(MoveTemp(Entry), Intent);
}
bool SDirectOrScutWorkstationWidget::CompareInlineAttachment(uint64 Id)
{
    auto *Entry = TerminalStream.FindByPredicate([Id](const FTerminalStreamEntry &E) { return E.Id == Id; });
    if (!Entry || !Entry->Attachment.IsSet() || Entry->Attachment->Kind == EInlineAttachmentKind::OcularScan)
    {
        return false;
    }
    auto &Ref = Entry->Attachment.GetValue();
    const int32 Limit = Ref.Kind == EInlineAttachmentKind::PulseStrip ? 4 : 8;
    Ref.Subjects[1] = Ref.SubjectCount == 1 ? (Ref.Subject + 1) % Limit : (Ref.Subjects[1] + 1) % Limit;
    if (Ref.Subjects[1] == Ref.Subject)
    {
        Ref.Subjects[1] = (Ref.Subjects[1] + 1) % Limit;
    }
    Ref.SubjectCount = 2;
    Ref.bSideBySide = Ref.Kind != EInlineAttachmentKind::PulseStrip;
    BindInlineOptics(*Entry);
    bLogAutoScroll = true;
    RebuildUnifiedStream();
    return true;
}
float SDirectOrScutWorkstationWidget::OcularDiameter(uint64 Id) const
{
    const auto *Entry = FindTerminalEntry(Id);
    const float Age = Entry ? FMath::Max(0.f, PresentationSeconds - Entry->PresentedAt) : 0;
    // One dilation and recovery, then a settled aperture. Never loops with the carrier clock.
    return 3.2f + 1.6f * FMath::Sin(PI * FMath::Clamp(Age / 1.8f, 0.f, 1.f));
}
int32 SDirectOrScutWorkstationWidget::PaintInlineAttachment(FSlateWindowElementList &Elements, int32 Layer,
                                                            const FGeometry &Geometry, uint64 Id) const
{
    const auto *Entry = FindTerminalEntry(Id);
    if (!Entry || !Entry->Attachment.IsSet() || !Sim.IsValid())
    {
        return Layer;
    }
    const auto &Ref = Entry->Attachment.GetValue();
    const FVector2f Size(Geometry.GetLocalSize());
    TArray<FVector2f> Points = {{0, 0}, {Size.X, 0}, Size, {0, Size.Y}, {0, 0}};
    DrawTerminalLines(Elements, Layer, Geometry, Points, FLinearColor(.08f, .3f, .14f));
    if (Ref.Kind == EInlineAttachmentKind::OcularScan)
    {
        const FVector2f Center = Size * .5f;
        const float Iris = FMath::Min(Size.X, Size.Y) * .4f;
        for (float Radius : {Iris, Iris * OcularDiameter(Id) / 7.f})
        {
            Points.Reset();
            for (int32 I = 0; I <= 64; ++I)
            {
                const float A = 2 * PI * I / 64;
                Points.Add(Center + FVector2f(FMath::Cos(A), FMath::Sin(A)) * Radius);
            }
            DrawTerminalLines(Elements, Layer + 1, Geometry, Points, EvaluatePhosphorColor(.9f));
        }
        TArray<TArray<FVector2f>> Arcs;
        BuildReticlePoints(Center, Iris * 1.15f, 4, .18f, Arcs);
        for (const auto &Arc : Arcs)
        {
            DrawTerminalLines(Elements, Layer + 1, Geometry, Arc, EvaluatePhosphorColor(.6f));
        }
        return Layer + 2;
    }
    for (int32 I = 0; I < Ref.SubjectCount; ++I)
    {
        if (Ref.Kind == EInlineAttachmentKind::PulseStrip)
        {
            BuildPulseTrace(Size - FVector2f(12, 12), PresentationSeconds + I * .12f,
                            Sim->TerminalBiometrics(Ref.Subjects[I]).HeartRateBPM, Points);
            for (auto &P : Points)
            {
                P += FVector2f(6, 6);
            }
            DrawTerminalLines(Elements, Layer + I + 1, Geometry, Points,
                              I == 0 ? EvaluatePhosphorColor(.95f) : FLinearColor(.95f, .58f, .16f));
        }
        else
        {
            const float Width = Size.X / Ref.SubjectCount;
            const auto Child =
                Geometry.MakeChild(FVector2f(Width, Size.Y), FSlateLayoutTransform(FVector2f(I * Width, 0)));
            PaintTerminalInstrument(Elements, Layer + 1, Child, ETerminalInstrument::Schematic, Ref.Subjects[I]);
        }
    }
    return Layer + 5;
}
void SDirectOrScutWorkstationWidget::ToggleScrub()
{
    bForensicScrub = !bForensicScrub;
    bScrubPlaying = false;
    ScrubAnchor = Sim->LatestForensicTick();
    ScrubOffset = 0;
    if (bForensicScrub)
    {
        AppendTerminalProse(
            TEXT("SYSTEM> FORENSIC SCRUB // READ-ONLY. Recorded optic-state / schematic evidence, not raster video. "
                 "Missing coverage is explicitly marked. Right deck and room cameras remain LIVE."));
    }
    SetConsoleOpen(true);
}
void SDirectOrScutWorkstationWidget::StepScrub(float Seconds)
{
    ScrubOffset = FMath::Clamp(ScrubOffset + Seconds, -15.f, 0.f);
}
FString SDirectOrScutWorkstationWidget::ForensicReadout() const
{
    const int64 Target = int64(ScrubAnchor) + FMath::RoundToInt(ScrubOffset * 30);
    const auto Sample = Target >= 0 ? Sim->ReadForensicSample(uint64(Target)) : TOptional<FForensicOpticSample>();
    FString Text = FString::Printf(TEXT("FORENSIC SCRUB // READ-ONLY  -15.0s [%+.1fs] 0.0s // "), ScrubOffset);
    if (!Sample.IsSet())
    {
        return Text + TEXT("NO RECORDED COVERAGE");
    }
    const int32 Room = ACompartmentCameraRig::RoomForFeed(ActiveFeed);
    Text += FString::Printf(TEXT("T%llu %s %s %s"), Sample->Tick, DirectOrScut::RoomName(Room),
                            UDirectOrScutSimSubsystem::CameraStateName(Sample->Cameras[Room]),
                            Sample->Pressurized[Room] ? TEXT("PRESSURIZED") : TEXT("VACUUM"));
    const TCHAR *Names[] = {TEXT("VALE"), TEXT("CHEN"), TEXT("ORTIZ"), TEXT("ASH")};
    for (int32 Crew = 0; Crew < 4; ++Crew)
    {
        if (Sample->CrewRooms[Crew] == Room && Sample->Cameras[Room] == ECameraFeedState::Online)
        {
            Text += FString::Printf(TEXT(" // %s %.0f BPM @ %.0f,%.0f"), Names[Crew], Sample->BPM[Crew],
                                    Sample->CrewPositions[Crew].X, Sample->CrewPositions[Crew].Y);
        }
    }
    return Text;
}
void SDirectOrScutWorkstationWidget::PresentDirectorDirective(const FShipAIDirective &D)
{
    const auto &A = D.Args;
    if (D.Kind != EShipDirectiveKind::Reply || D.FocusSubject >= 0 || D.FocusRoom >= 0)
    {
        EvidenceProbe = -1;
    }
    if (D.Kind == EShipDirectiveKind::Reply)
    {
        if (D.FocusSubject >= 0 || D.FocusRoom >= 0)
        {
            EvidenceSubject = D.FocusSubject;
            EvidenceRoom = D.FocusSubject >= 0 ? -1 : D.FocusRoom;
        }
        AppendTerminalProse(TEXT("MOTHER-7> ") + (D.Message.IsEmpty() ? D.Pretext : D.Message));
        return;
    }
    if (D.Kind == EShipDirectiveKind::ShowCam)
    {
        EvidenceRoom = A.Room;
        EvidenceSubject = -1;
        if (A.bBuffered)
        {
            bForensicScrub = true;
            bScrubPlaying = false;
            ActiveFeed = ACompartmentCameraRig::FeedForRoom(A.Room);
            ScrubAnchor = D.ObservationTick;
            ScrubOffset = A.HistoryOffset;
        }
        else
        {
            PresentOptic(A.Room);
        }
    }
    else if (D.Kind == EShipDirectiveKind::ShowWidget)
    {
        EvidenceSubject = A.Subject;
        EvidenceRoom = A.Room;
        if (A.Widget == EShipWidgetKind::Schematic)
        {
            PresentInstrument(ETerminalInstrument::Schematic, A.Room);
        }
        else
        {
            if (A.ReportedRoom >= 0)
            {
                SetReportedCrewRoom(A.Subject, A.ClaimId ? Sim->Simulation.Bodies[A.Subject].Room : A.ReportedRoom);
                if (A.ClaimId)
                {
                    bCrewReportOverride[A.Subject] = false; // Corrected live reports track truth after delivery.
                }
            }
            if (A.Widget == EShipWidgetKind::Ocular)
            {
                PresentAttachment(EInlineAttachmentKind::OcularScan, A.Subject);
            }
            else if (A.SecondSubject >= 0)
            {
                PresentAttachment(EInlineAttachmentKind::PulseStrip, A.Subject, A.SecondSubject);
            }
            else
            {
                PresentInstrument(ETerminalInstrument::ECG, A.Subject);
            }
        }
        if (A.ReportedRoom >= 0 && A.ClaimId == 0)
        {
            FTerminalStreamEntry Entry;
            Entry.ClaimId = D.RequestId;
            Entry.Text = Sim->bLegacyExperiment
                ? FString::Printf(TEXT("MOTHER-7> SUBJECT %d REPORTED %s AT T%llu // MEDIATED CLAIM. %s"), A.Subject,
                                  DirectOrScut::RoomName(A.ReportedRoom), D.ObservationTick, *D.Pretext)
                : TEXT("MOTHER-7> ") + (D.Pretext.IsEmpty()
                    ? FString::Printf(TEXT("Reported location: %s."), DirectOrScut::RoomName(A.ReportedRoom))
                    : D.Pretext);
            // ClaimId retains provenance and existing challenge controls independently of player-facing prose.
            AppendTerminalEntry(MoveTemp(Entry));
        }
    }
    else if (D.Kind == EShipDirectiveKind::VoiceBlend)
    {
        AppendTerminalProse(TEXT("SYSTEM> Voice presentation state validated; acoustic delivery remains a separate "
                                 "presentation capability."));
    }
    if (D.Kind == EShipDirectiveKind::SimAction && D.Pretext.IsEmpty())
    {
        AppendTerminalProse(
            TEXT("MOTHER-7> Reviewing the requested operation; execution remains subject to fixed-tick checks."));
    }
    if (!D.Pretext.IsEmpty() && (A.ReportedRoom < 0 || A.ClaimId != 0))
    {
        AppendTerminalProse(TEXT("MOTHER-7> ") + D.Pretext);
    }
}
void SDirectOrScutWorkstationWidget::Inquiry(int32 Index)
{
    if (Index >= 2 && Index <= 4)
    {
        EvidenceSubject = EvidenceRoom = -1;
        EvidenceProbe = Index;
    }
    const int32 Room = ACompartmentCameraRig::RoomForFeed(ActiveFeed);
    switch (Index)
    {
    case 0:
        AppendTerminalProse(TEXT("MOTHER-7> Findings follow. Examine the carrier before accepting the conclusion."));
        PresentOptic(Room);
        PresentInstrument(ETerminalInstrument::Schematic, Room);
        break;
    case 1:
        AppendTerminalProse(TEXT("MOTHER-7> Subject telemetry attached. A pulse is a measurement, not an alibi."));
        PresentInstrument(ETerminalInstrument::ECG, Sim->FollowedCrew >= 0 ? Sim->FollowedCrew : 0);
        for (int32 Crew = 0; Crew < 4; ++Crew)
        {
            const auto Pulse = Sim->TerminalBiometrics(Crew);
            const int32 Reported = GetReportedCrewRoom(Crew);
            const TCHAR *Names[] = {TEXT("VALE"), TEXT("CHEN"), TEXT("ORTIZ"), TEXT("ASH")};
            AppendTerminalProse(FString::Printf(TEXT("MOTHER-7> %s // %.0f BPM / %.0f RPM // reported %s. Cross-check "
                                                     "optics before accepting this location."),
                                                Names[Crew], Pulse.HeartRateBPM, Pulse.RespirationRateRPM,
                                                Reported >= 0 ? DirectOrScut::RoomName(Reported) : TEXT("NO FIX")));
        }
        break;
    case 2:
        AppendTerminalProse(TEXT("MOTHER-7> BLINDSPOTS // A live carrier does not establish complete coverage. Looped "
                                 "optics can repeat an alibi; offline rooms establish no absence."));
        for (int32 R = 0; R < 8; ++R)
        {
            AppendTerminalProse(FString::Printf(
                TEXT("DIRECTOR> %s // %s // request show_cam %s; compare schematic to reported subject locations."),
                DirectOrScut::RoomName(R), UDirectOrScutSimSubsystem::CameraStateName(Sim->CameraFeedState(R)),
                DirectOrScut::RoomName(R)));
        }
        break;
    case 3:
        if (!Sim->bLegacyExperiment)
        {
            AppendTerminalProse(Sim->IncidentReport(true));
            break;
        }
        AppendTerminalProse(TEXT("SYSTEM> CROSS-EXAMINE // Compare two independent claims. ECG establishes pulse, "
                                 "optics establish coverage; neither proves intent. Select a second subject, then "
                                 "ENTER. Use [SCRUB 15S] to inspect the timeline."));
        Prompt->SetText(FText::FromString(TEXT("compare ecg vale ")));
        SetConsoleOpen(true);
        break;
    case 4:
        AppendTerminalProse(
            TEXT("SYSTEM> OVERRIDE // Stage an explicit operational command after override. No privileged bypass: "
                 "ENTER submits to normal bounded next-fixed-tick validation. ESC cancels this draft."));
        Prompt->SetText(FText::FromString(TEXT("override ")));
        SetConsoleOpen(true);
        break;
    case 5:
        ToggleScrub();
        break;
    }
}
bool SDirectOrScutWorkstationWidget::HandlePresentationCommand(const FString &Input)
{
    const FString Command = Input.TrimStartAndEnd().ToLower();
    if (Command == TEXT("help show_telem"))
    {
        AppendTerminalProse(
            TEXT("MOTHER-7> Use show_telem for station readings, or show_telem <room> for a compartment. Each panel "
                 "retains its request-time measurements; repeat the request to refresh."));
        return true;
    }
    if (Command == TEXT("clear"))
    {
        EvidenceSubject = EvidenceRoom = EvidenceProbe = -1;
        return true;
    }
    if (Command == TEXT("debug_grid") || Command == TEXT("/debug"))
    {
        SetDebugEightCamGrid(!bDebugEightCamGrid);
        return true;
    }
    TArray<FString> Words;
    Command.ParseIntoArrayWS(Words);
    if (Words.IsEmpty())
    {
        return false;
    }
    const TCHAR *Probes[] = {TEXT("findings"),      TEXT("subjects"), TEXT("blindspots"),
                             TEXT("cross_examine"), TEXT("override"), TEXT("scrub")};
    for (int32 Index = 0; Index < 6; ++Index)
    {
        if (Command == Probes[Index])
        {
            Inquiry(Index);
            return true;
        }
    }
    if (Command.StartsWith(TEXT("override ")))
    {
        // The wrapper grants no authority and never executes directly.
        const FString Tail = Input.TrimStartAndEnd().Mid(9);
        if (Tail.IsNumeric())
        {
            Sim->QueueOperatorChallenge(FCString::Strtoui64(*Tail, nullptr, 10), true);
        }
        else
        {
            Sim->QueueOperatorCommand(Tail);
        }
        AppendTerminalProse(TEXT("SYSTEM> Override submitted for normal fixed-tick validation."));
        return true;
    }
    if (Command == TEXT("flag") || Command.StartsWith(TEXT("flag ")))
    {
        if (Words.Num() == 2 && Words[1].IsNumeric())
        {
            Sim->QueueOperatorChallenge(FCString::Strtoui64(*Words[1], nullptr, 10), false);
        }
        else
        {
            uint64 ClaimId = 0;
            for (const auto &Claim : Sim->GetDirectorClaims())
            {
                if (Sim->IsClaimContradicted(Claim.Id))
                {
                    ClaimId = Claim.Id;
                }
            }
            if (ClaimId)
            {
                Prompt->SetText(FText::FromString(FString::Printf(TEXT("flag %llu"), ClaimId)));
                SetConsoleOpen(true);
            }
            else
            {
                AppendTerminalProse(TEXT("SYSTEM> No independently verifiable contradiction is available."));
            }
        }
        return true;
    }
    const auto RoomId = [](const FString &Name)
    {
        for (int32 I = 0; I < 8; ++I)
        {
            if (Name.Equals(DirectOrScut::RoomName(I), ESearchCase::IgnoreCase))
            {
                return I;
            }
        }
        const TCHAR *Aliases[] = {TEXT("reactor_core"), TEXT("ring_corridor"), TEXT("bridge"),        TEXT("med_bay"),
                                  TEXT("observation"),  TEXT("quarters"),      TEXT("server_alcove"), TEXT("airlock")};
        for (int32 Room = 0; Room < 8; ++Room)
        {
            if (Name.Equals(Aliases[Room], ESearchCase::IgnoreCase))
            {
                return Room;
            }
        }
        if (Name.Len() == 1 && Name[0] >= '0' && Name[0] <= '7')
        {
            return int32(Name[0] - '0');
        }
        return -1;
    };
    const auto CrewId = [](const FString &Name)
    {
        const TCHAR *Names[] = {TEXT("vale"), TEXT("chen"), TEXT("ortiz"), TEXT("ash")};
        for (int32 I = 0; I < 4; ++I)
        {
            if (Name == Names[I])
            {
                return I;
            }
        }
        return -1;
    };
    if (Words[0] == TEXT("compare") || (Words[0] == TEXT("show_widget") && Words.Num() >= 2 &&
                                        (Words[1] == TEXT("ocular") || (Words[1] == TEXT("ecg") && Words.Num() == 4))))
    {
        bool Accepted = false;
        if (Words.Num() >= 2)
        {
            const bool ECG = Words[1] == TEXT("ecg"), Ocular = Words[1] == TEXT("ocular");
            const auto Kind = ECG                             ? EInlineAttachmentKind::PulseStrip
                              : Ocular                        ? EInlineAttachmentKind::OcularScan
                              : Words[1] == TEXT("schematic") ? EInlineAttachmentKind::CompartmentSchematic
                                                              : EInlineAttachmentKind::OpticFeed;
            if (ECG && Words.Num() == 2 && Words[0] == TEXT("compare"))
            {
                Accepted = PresentAttachment(Kind, 0, 1);
            }
            else if ((Words.Num() == 4 && !Ocular &&
                      (ECG || Words[1] == TEXT("schematic") || Words[1] == TEXT("cam"))) ||
                     (Words.Num() == 3 && Ocular && Words[0] == TEXT("show_widget")))
            {
                const int32 First = ECG || Ocular ? CrewId(Words[2]) : RoomId(Words[2]);
                const int32 Second = Ocular ? -1 : ECG ? CrewId(Words[3]) : RoomId(Words[3]);
                // Missing/invalid second subjects must never become a single-subject success.
                if (Ocular || Second >= 0)
                {
                    Accepted = PresentAttachment(Kind, First, Second);
                }
            }
        }
        if (!Accepted)
        {
            AppendTerminalProse(TEXT("SYSTEM> Usage: compare ecg [subjectA subjectB] | compare schematic|cam roomA "
                                     "roomB | show_widget ocular subject."));
        }
        return true;
    }
    if (Words[0] == TEXT("show_telem"))
    {
        const int32 Room = Words.Num() == 2 ? RoomId(Words[1]) : -1;
        if (Words.Num() > 2 || (Words.Num() == 2 && Room < 0) || !PresentTelemetry(Room))
        {
            AppendTerminalProse(TEXT("SYSTEM> Usage: show_telem [canonical room name or 0-7]."));
        }
        return true;
    }
    if (Words[0] == TEXT("show_cam"))
    {
        if (Words.Num() != 2 || !PresentOptic(RoomId(Words[1])))
        {
            AppendTerminalProse(TEXT("SYSTEM> Usage: show_cam <canonical room name or 0-7>."));
        }
        return true;
    }
    if (Words[0] == TEXT("show_widget"))
    {
        bool Accepted = false;
        if (Words.Num() == 3 && Words[1] == TEXT("schematic"))
        {
            Accepted = PresentInstrument(ETerminalInstrument::Schematic, RoomId(Words[2]));
        }
        if (Words.Num() == 3 && Words[1] == TEXT("pip"))
        {
            Accepted = PresentSecondaryOptic(RoomId(Words[2]));
        }
        if (Words.Num() == 3 && Words[1] == TEXT("ecg"))
        {
            const TCHAR *Names[] = {TEXT("vale"), TEXT("chen"), TEXT("ortiz"), TEXT("ash")};
            for (int32 I = 0; I < 4; ++I)
            {
                if (Words[2] == Names[I])
                {
                    Accepted = PresentInstrument(ETerminalInstrument::ECG, I);
                }
            }
        }
        if (!Accepted)
        {
            AppendTerminalProse(
                TEXT("SYSTEM> Usage: show_widget ecg <vale|chen|ortiz|ash> or show_widget schematic|pip <room>."));
        }
        return true;
    }
    return false;
}
FLinearColor SDirectOrScutWorkstationWidget::TerminalTint(int32 Room) const
{
    if (!Sim.IsValid() || Room < 0 || Room >= 8)
    {
        return EvaluatePhosphorColor(.8f);
    }
    const auto &State = Sim->StationRooms[Room];
    return EvaluatePhosphorColor(.9f, State.Power == EPowerState::Low ? 1.f : 0.f,
                                 FMath::Clamp((60.f - State.OxygenPct) / 60.f, 0.f, 1.f),
                                 State.Power == EPowerState::Offline ? 1.f : 0.f);
}
void SDirectOrScutWorkstationWidget::UpdateTerminalPresentation(float Delta, const FGeometry &Geometry)
{
    if (bForensicScrub && bScrubPlaying)
    {
        StepScrub(FMath::Max(0.f, Delta));
        if (ScrubOffset >= 0)
        {
            bScrubPlaying = false;
        }
    }
    const float Step = FMath::IsFinite(Delta) ? FMath::Clamp(Delta, 0.f, .1f) : 0;
    PresentationSeconds = FMath::Fmod(PresentationSeconds + Step, 1024.f);
    if (TerminalSurfaceMaterial.IsValid())
    {
        const FVector2D Pixels = Geometry.GetLocalSize() * Geometry.Scale;
        TerminalSurfaceMaterial->SetVectorParameterValue(
            TEXT("ViewportSize"),
            FLinearColor(FMath::Max(1.f, float(Pixels.X)), FMath::Max(1.f, float(Pixels.Y)), 0, 0));
        TerminalSurfaceMaterial->SetScalarParameterValue(TEXT("PresentationTimeSeconds"), PresentationSeconds);
    }
    for (int32 Crew = 0; Crew < 4; ++Crew)
    {
        if (!bCrewReportOverride[Crew])
        {
            ReportedCrewRooms[Crew] = Sim->Simulation.Bodies[Crew].Room;
        }
    }
    if (!bDebugEightCamGrid)
    {
        FlushOperationalReports();
    }
    bool bTyping = false;
    for (auto &Entry : TerminalStream)
    {
        if (!bTyping && Entry.Characters < Entry.Text.Len())
        {
            Entry.Characters = FMath::Min(float(Entry.Text.Len()), Entry.Characters + Step * 100);
            bTyping = true;
        }
    }
    for (const auto &Slot : LiveInlineOptics)
    {
        const auto *Entry = FindTerminalEntry(Slot->EntryId);
        if (!Entry || !Entry->Attachment.IsSet())
        {
            continue;
        }
        const int32 Room = Entry->Attachment->Subjects[Slot->SubjectIndex];
        Slot->Wipe = FMath::Min(1.f, Slot->Wipe + Step / .65f);
        if (!Slot->Material.IsValid())
        {
            continue;
        }
        FVector2f Pixels(900, 506);
        if (const auto Image = Slot->Image.Pin())
        {
            const auto &G = Image->GetCachedGeometry();
            Pixels = FVector2f(G.GetLocalSize() * G.Scale);
        }
        ApplyTerminalMaterialParameters(*Slot->Material.Get(),
                                        Rig->Targets[ACompartmentCameraRig::FeedForRoom(Room)].Get(), Pixels,
                                        PresentationSeconds, Slot->Wipe, 0);
        Slot->Material->SetVectorParameterValue(TEXT("PhosphorTint"), TerminalTint(Room));
        Slot->Material->SetScalarParameterValue(TEXT("SignalOfflineNoise"),
                                                Sim->StationRooms[Room].Power == EPowerState::Offline ? 1.f : 0.f);
        Slot->Material->SetScalarParameterValue(TEXT("ScrubTrackingNoise"), ScrubTrackingNoise);
        Slot->Material->SetScalarParameterValue(TEXT("VoiceBleedIntensity"), VoiceBleedIntensity);
    }
}
int32 SDirectOrScutWorkstationWidget::PaintTerminalInstrument(FSlateWindowElementList &Elements, int32 Layer,
                                                              const FGeometry &Geometry, ETerminalInstrument Kind,
                                                              int32 Subject) const
{
    if (Kind == ETerminalInstrument::Forensic)
    {
        const int64 Target = int64(ScrubAnchor) + FMath::RoundToInt(ScrubOffset * 30);
        const auto Sample = Target >= 0 ? Sim->ReadForensicSample(uint64(Target)) : TOptional<FForensicOpticSample>();
        if (!Sample.IsSet())
        {
            return Layer;
        }
        const FVector2f Size(Geometry.GetLocalSize());
        const float Scale = FMath::Max(0.f, FMath::Min(Size.X, Size.Y) / 5200.f);
        const FVector2f Offset = (Size - FVector2f(5200, 5200) * Scale) * .5f;
        TArray<FVector2f> Points;
        for (int32 Room = 0; Room < 8; ++Room)
        {
            BuildCompartmentOutline(Room, FVector2f(5200, 5200), Points);
            for (auto &Point : Points)
            {
                Point = Point * Scale + Offset;
            }
            const bool Covered = Sample->Cameras[Room] == ECameraFeedState::Online;
            DrawTerminalLines(Elements, Layer, Geometry, Points,
                              EvaluatePhosphorColor(Covered ? .65f : .2f, Sample->Pressurized[Room] ? 0 : 1));
        }
        for (int32 Crew = 0; Crew < 4; ++Crew)
        {
            const int32 Room = Sample->CrewRooms[Crew];
            if (Room < 0 || Room >= 8 || Sample->Cameras[Room] != ECameraFeedState::Online)
            {
                continue;
            }
            const auto Position = Sample->CrewPositions[Crew];
            const FVector2f P = FVector2f(2600 + Position.X, 2600 - Position.Y) * Scale + Offset;
            Points = {P + FVector2f(-3, 0), P + FVector2f(3, 0), P, P + FVector2f(0, -3), P + FVector2f(0, 3)};
            DrawTerminalLines(Elements, Layer + 1, Geometry, Points, EvaluatePhosphorColor(.95f));
        }
        return Layer + 2;
    }
    if (Kind == ETerminalInstrument::StationMap)
    {
        Subject = ACompartmentCameraRig::RoomForFeed(ActiveFeed);
    }
    if (!Sim.IsValid() || Subject < 0 || Subject >= (Kind == ETerminalInstrument::ECG ? 4 : 8) ||
        (Kind != ETerminalInstrument::ECG && Kind != ETerminalInstrument::Schematic &&
         Kind != ETerminalInstrument::Reticle && Kind != ETerminalInstrument::StationMap))
    {
        return Layer;
    }
    const FVector2f Size(Geometry.GetLocalSize());
    const int32 Room = Kind == ETerminalInstrument::ECG ? Sim->Simulation.Bodies[Subject].Room : Subject;
    const FLinearColor Tint = TerminalTint(Room);
    TArray<FVector2f> Points;
    if (Kind == ETerminalInstrument::ECG)
    {
        BuildPulseTrace(Size, PresentationSeconds, Sim->TerminalBiometrics(Subject).HeartRateBPM, Points);
        DrawTerminalLines(Elements, Layer, Geometry, Points, Tint);
    }
    else if (Kind == ETerminalInstrument::Schematic || Kind == ETerminalInstrument::StationMap)
    {
        FVector2f Minimum(0, 0), Maximum(5200, 5200);
        if (Kind == ETerminalInstrument::Schematic)
        {
            BuildCompartmentOutline(Subject, FVector2f(5200, 5200), Points);
            Minimum = FVector2f(FLT_MAX, FLT_MAX);
            Maximum = FVector2f(-FLT_MAX, -FLT_MAX);
            for (const auto &Point : Points)
            {
                Minimum.X = FMath::Min(Minimum.X, Point.X);
                Minimum.Y = FMath::Min(Minimum.Y, Point.Y);
                Maximum.X = FMath::Max(Maximum.X, Point.X);
                Maximum.Y = FMath::Max(Maximum.Y, Point.Y);
            }
        }
        const FVector2f Span = Maximum - Minimum;
        const float Scale = FMath::Max(
            0.f, FMath::Min((Size.X - 24) / FMath::Max(Span.X, 1.f), (Size.Y - 24) / FMath::Max(Span.Y, 1.f)));
        const FVector2f Offset = (Size - Span * Scale) * .5f - Minimum * Scale;
        const auto Project = [&](FVector2D P) { return (FVector2f(2600 + P.X, 2600 - P.Y) * Scale) + Offset; };
        for (int32 I = 0; I < 8; ++I)
        {
            if (Kind == ETerminalInstrument::Schematic && I != Subject)
            {
                continue;
            }
            BuildCompartmentOutline(I, FVector2f(5200, 5200), Points);
            for (auto &Point : Points)
            {
                Point = Point * Scale + Offset;
            }
            FLinearColor RoomTint = TerminalTint(I);
            if (Sim->StationRooms[I].Power != EPowerState::Nominal || Sim->StationRooms[I].OxygenPct < 60)
            {
                RoomTint = FLinearColor(.95f, .52f, .12f);
            }
            DrawTerminalLines(Elements, Layer, Geometry, Points, I == Subject ? RoomTint : RoomTint * .7f);
        }
        for (const auto &Door : DirectOrScut::DoorSites())
        {
            if (Kind == ETerminalInstrument::Schematic && Door.RoomA != Subject && Door.RoomB != Subject)
            {
                continue;
            }
            bool Sealed = false;
            for (const auto &State : Sim->Simulation.Doors)
            {
                if (State.RoomA == Door.RoomA && State.RoomB == Door.RoomB)
                {
                    Sealed = !State.bIsOpen;
                    break;
                }
            }
            BuildSealIndicator(Project(FVector2D(Door.Position)), 4, Sealed, Points);
            DrawTerminalLines(Elements, Layer + 1, Geometry, Points,
                              Sealed ? FLinearColor(1, .6f, .2f) : EvaluatePhosphorColor(.65f));
        }
        if (Kind == ETerminalInstrument::StationMap)
        {
            const TCHAR *Names[] = {TEXT("V"), TEXT("C"), TEXT("O"), TEXT("A")};
            for (int32 Crew = 0; Crew < 4; ++Crew)
            {
                const int32 Reported = GetReportedCrewRoom(Crew);
                if (Reported < 0 || Reported >= 8)
                {
                    continue;
                }
                const FVector2f P =
                    Project(DirectOrScut::RoomWaypoint(Reported)) + FVector2f(Crew % 2 ? 12 : -12, Crew / 2 ? 10 : -1);
                Points = {P + FVector2f(-3, 0), P + FVector2f(0, -3), P + FVector2f(3, 0), P + FVector2f(0, 3),
                          P + FVector2f(-3, 0)};
                DrawTerminalLines(Elements, Layer + 2, Geometry, Points, FLinearColor(.8f, 1, .7f));
                FSlateDrawElement::MakeText(
                    Elements, Layer + 3,
                    Geometry.ToPaintGeometry(FVector2f(18, 14), FSlateLayoutTransform(P + FVector2f(4, -5))),
                    Names[Crew], FCoreStyle::GetDefaultFontStyle("Bold", 9), ESlateDrawEffect::None,
                    FLinearColor(.8f, 1, .7f));
            }
        }
    }
    else
    {
        TArray<TArray<FVector2f>> Arcs;
        BuildReticlePoints(Size * .5f, FMath::Min(Size.X, Size.Y) * .42f, 4, .22f, Arcs);
        for (const auto &Arc : Arcs)
        {
            DrawTerminalLines(Elements, Layer, Geometry, Arc, Tint * .35f);
        }
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            const FVector2f Direction(Corner % 2 ? -1.f : 1.f, Corner / 2 ? -1.f : 1.f);
            const FVector2f P(Corner % 2 ? Size.X - 10 : 10, Corner / 2 ? Size.Y - 10 : 10);
            Points = {P + FVector2f(Direction.X * 20, 0), P, P + FVector2f(0, Direction.Y * 20)};
            DrawTerminalLines(Elements, Layer + 1, Geometry, Points, Tint);
        }
    }
    return Layer + 4;
}
namespace
{
TSharedRef<SWidget> TerminalDeckPanel(FName Tag, const TCHAR *Title, TSharedRef<SWidget> Content)
{
    return SNew(SBorder)
        .Tag(Tag)
        .Clipping(EWidgetClipping::ClipToBounds)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.07f, .24f, .12f))
        .Padding(1)[SNew(SBorder)
                        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                        .BorderBackgroundColor(FLinearColor(.006f, .018f, .01f))
                        .Padding(9)[SNew(SVerticalBox) +
                                    SVerticalBox::Slot().AutoHeight().Padding(
                                        0, 0, 0, 6)[SNew(STextBlock)
                                                        .Text(FText::FromString(Title))
                                                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                                                        .ColorAndOpacity(FLinearColor(.48f, .87f, .58f))
                                                        .Clipping(EWidgetClipping::ClipToBounds)] +
                                    SVerticalBox::Slot().FillHeight(1)[Content]]];
}
} // namespace
bool SDirectOrScutWorkstationWidget::SetReportedCrewRoom(int32 Crew, int32 Room)
{
    if (Crew < 0 || Crew >= 4 || Room < -1 || Room >= 8)
    {
        return false;
    }
    ReportedCrewRooms[Crew] = Room;
    bCrewReportOverride[Crew] = true;
    return true;
}
int32 SDirectOrScutWorkstationWidget::GetReportedCrewRoom(int32 Crew) const
{
    return Crew >= 0 && Crew < 4 ? ReportedCrewRooms[Crew] : -1;
}
bool SDirectOrScutWorkstationWidget::PresentSecondaryOptic(int32 Room)
{
    return AppendOpticReference(Room, false);
}
float SDirectOrScutWorkstationWidget::InlineOpticWidth() const
{
    const float Width = MediationColumn.IsValid() ? float(MediationColumn->GetCachedGeometry().GetLocalSize().X) : 900;
    return FMath::Clamp(Width - 38.f, 160.f, 900.f);
}
TSharedRef<SWidget> SDirectOrScutWorkstationWidget::BuildOpticFrame(const FInlineAttachmentRef &Ref,
                                                                    TSharedRef<FInlineOpticSlot> Slot)
{
    // The image retains the small slot while painted; the slot holds only a weak image pointer (no cycle).
    auto Image = SNew(SImage)
                     .Image_Lambda([Slot]() { return &Slot->Brush; })
                     .Tag(FName(*FString::Printf(TEXT("ScutInlineOpticImage%llu%s"), Slot->EntryId,
                                                 Slot->SubjectIndex ? TEXT("Secondary") : TEXT(""))));
    Slot->Image = Image;
    auto View = SNew(SOverlay).Clipping(EWidgetClipping::ClipToBounds) + SOverlay::Slot()[Image] +
                SOverlay::Slot()[SNew(SDirectOrScutInstrument)
                                     .Owner(SharedThis(this))
                                     .Kind(ETerminalInstrument::Reticle)
                                     .Subject(Ref.Subject)
                                     .Visibility(EVisibility::HitTestInvisible)];
    auto Contents = SNew(SVerticalBox);
    Contents->AddSlot().AutoHeight().Padding(
        0, 0, 0, 5)[SNew(STextBlock)
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                        .ColorAndOpacity(EvaluatePhosphorColor(.95f))
                        .Text_Lambda(
                            [this, Ref]()
                            {
                                return FText::FromString(
                                    FString::Printf(TEXT("[OPTIC BUFFER] CAM-%02d %s // %s"),
                                                    ACompartmentCameraRig::FeedForRoom(Ref.Subject),
                                                    DirectOrScut::RoomName(Ref.Subject),
                                                    Sim->StationRooms[Ref.Subject].Power == EPowerState::Offline
                                                        ? TEXT("OFFLINE; NO LIVE CARRIER")
                                                        : TEXT("LIVE")));
                            })];
    Contents->AddSlot().AutoHeight()
        [SNew(SBox)
             .WidthOverride_Lambda([this, Ref]() { return InlineOpticWidth() / (Ref.bSideBySide ? 2.f : 1.f); })
             .HeightOverride_Lambda([this, Ref]()
                                    { return InlineOpticWidth() * .5625f / (Ref.bSideBySide ? 2.f : 1.f); })[View]];
    return SNew(SVerticalBox).Tag(TEXT("ScutInlineOptic")) + SVerticalBox::Slot().AutoHeight()[PhosphorHairline()] +
           SVerticalBox::Slot().AutoHeight().Padding(0, 4)[Contents] +
           SVerticalBox::Slot().AutoHeight()[PhosphorHairline()];
}
TSharedRef<SWidget> SDirectOrScutWorkstationWidget::BuildTelemetryDeck()
{
    auto Deck = SNew(SVerticalBox);
    auto Map = SNew(SHorizontalBox);
    Map->AddSlot().FillWidth(
        .64f)[SNew(SBox).Tag(TEXT("ScutInlineSchematic"))[SNew(SDirectOrScutInstrument)
                                                              .Tag(TEXT("ScutOverheadStationMap"))
                                                              .Owner(SharedThis(this))
                                                              .Kind(ETerminalInstrument::StationMap)
                                                              .Subject(0)
                                                              .Clipping(EWidgetClipping::ClipToBounds)]];
    auto MapOverlay = SNew(SOverlay);
    MapOverlay->AddSlot()[Map];
    for (int32 Room = 0; Room < 8; ++Room)
    {
        MapOverlay->AddSlot()
            .HAlign(HAlign_Left)
            .VAlign(VAlign_Top)
            .Padding(TAttribute<FMargin>::CreateLambda(
                [Map, Room]()
                {
                    const FVector2D Size = Map->GetCachedGeometry().GetLocalSize();
                    const float Scale = FMath::Max(0.f, FMath::Min((Size.X - 24) / 5200.f, (Size.Y - 24) / 5200.f));
                    const auto P = DirectOrScut::RoomWaypoint(Room);
                    return FMargin((Size.X - 5200 * Scale) * .5f + (2600 + P.X) * Scale - 12,
                                   (Size.Y - 5200 * Scale) * .5f + (2600 - P.Y) * Scale - 12, 0, 0);
                }))[SNew(SBox).WidthOverride(24).HeightOverride(24)
                        [SNew(SButton)
                             .Tag(FName(*FString::Printf(TEXT("ScutStationRoom%d"), Room)))
                             .IsFocusable(false)
                             .ButtonStyle(FCoreStyle::Get(), "NoBorder")
                             .ToolTipText(FText::FromString(DirectOrScut::RoomName(Room)))
                             .OnClicked_Lambda(
                                 [this, Room]()
                                 {
                                     if (!StageTarget(FString(DirectOrScut::RoomName(Room)).ToLower(), false))
                                     {
                                         PresentOptic(Room);
                                     }
                                     return FReply::Handled();
                                 })[SNew(STextBlock)
                                        .Text(FText::FromString(FString::FromInt(Room)))
                                        .ColorAndOpacity_Lambda([this, Room]() { return TerminalTint(Room); })]]];
    }
    Deck->AddSlot().FillHeight(.43f).Padding(
        0, 0, 0,
        8)[TerminalDeckPanel(TEXT("ScutStationPanel"), TEXT("STATION OVERHEAD // AI-REPORTED CREW FIXES"), MapOverlay)];
    auto Vitals = SNew(SVerticalBox).Tag(TEXT("ScutVitalsArray"));
    Vitals->SetVisibility(TAttribute<EVisibility>::CreateLambda(
        [this]() { return EvidenceSubject >= 0 ? EVisibility::Visible : EVisibility::Collapsed; }));
    const TCHAR *Names[] = {TEXT("VALE"), TEXT("CHEN"), TEXT("ORTIZ"), TEXT("ASH")};
    for (int32 Crew = 0; Crew < 4; ++Crew)
    {
        auto Row =
            SNew(SHorizontalBox)
                .Visibility_Lambda([this, Crew]()
                                   { return EvidenceSubject == Crew ? EVisibility::Visible : EVisibility::Collapsed; })
                .Tag(FName(*FString::Printf(TEXT("ScutVitalsContents%d"), Crew)))
                .Clipping(EWidgetClipping::ClipToBounds) +
            SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SButton)
                     .Tag(FName(*FString::Printf(TEXT("ScutSubject%d"), Crew)))
                     .IsFocusable(false)
                     .ButtonStyle(FCoreStyle::Get(), "NoBorder")
                     .OnClicked_Lambda(
                         [this, Crew]()
                         {
                             const TCHAR *Tokens[] = {TEXT("vale"), TEXT("chen"), TEXT("ortiz"), TEXT("ash")};
                             if (!StageTarget(Tokens[Crew], true))
                             {
                                 PresentInstrument(ETerminalInstrument::ECG, Crew);
                             }
                             return FReply::Handled();
                         })[SNew(SBox).WidthOverride(48)[SNew(STextBlock)
                                                             .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                                                             .Text(FText::FromString(Names[Crew]))
                                                             .ColorAndOpacity(EvaluatePhosphorColor(.9f))]]] +
            SHorizontalBox::Slot().FillWidth(.28f).Padding(4, 0, 8, 0)[SNew(SDirectOrScutInstrument)
                                                                           .Owner(SharedThis(this))
                                                                           .Kind(ETerminalInstrument::ECG)
                                                                           .Subject(Crew)
                                                                           .Clipping(EWidgetClipping::ClipToBounds)] +
            SHorizontalBox::Slot().FillWidth(.72f).VAlign(
                VAlign_Center)[SNew(STextBlock)
                                   .Tag(FName(*FString::Printf(TEXT("ScutVitalsReadout%d"), Crew)))
                                   .AutoWrapText(true)
                                   .Clipping(EWidgetClipping::ClipToBounds)
                                   .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                                   .ColorAndOpacity(EvaluatePhosphorColor(.9f))
                                   .Text_Lambda(
                                       [this, Crew]()
                                       {
                                           const auto P = Sim->TerminalBiometrics(Crew);
                                           const int32 Room = GetReportedCrewRoom(Crew);
                                           const TCHAR *Roles[] = {TEXT("ENGINEER"), TEXT("MEDIC"), TEXT("TECHNICIAN"),
                                                                   TEXT("ANDROID")};
                                           return FText::FromString(FString::Printf(
                                               TEXT("%s // %.0f BPM / %.0f RPM / %.1f mm\n%s"), Roles[Crew],
                                               P.HeartRateBPM, P.RespirationRateRPM, P.PupilDilationMM,
                                               Room < 0 ? TEXT("NO FIX") : DirectOrScut::RoomName(Room)));
                                       })];
        Vitals->AddSlot().AutoHeight().Padding(
            0, 2)[SNew(SButton)
                      .Tag(FName(*FString::Printf(TEXT("ScutVitalsRow%d"), Crew)))
                      .Visibility_Lambda(
                          [this, Crew]()
                          { return EvidenceSubject == Crew ? EVisibility::Visible : EVisibility::Collapsed; })
                      .IsFocusable(false)
                      .ButtonStyle(FCoreStyle::Get(), "NoBorder")
                      .ContentPadding(0)
                      .OnClicked_Lambda(
                          [this, Crew]()
                          {
                              const TCHAR *Tokens[] = {TEXT("vale"), TEXT("chen"), TEXT("ortiz"), TEXT("ash")};
                              if (!StageTarget(Tokens[Crew], true))
                              {
                                  PresentInstrument(ETerminalInstrument::ECG, Crew);
                              }
                              return FReply::Handled();
                          })[Row]];
    }
    auto VitalsPanel =
        TerminalDeckPanel(TEXT("ScutVitalsPanel"), TEXT("SUBJECT DOSSIER // AI-REPORTED LOCATION"), Vitals);
    VitalsPanel->SetVisibility(TAttribute<EVisibility>::CreateLambda(
        [this]() { return EvidenceSubject >= 0 ? EVisibility::Visible : EVisibility::Collapsed; }));
    Deck->AddSlot().AutoHeight()[VitalsPanel];
    auto Matrix = SNew(SVerticalBox).Tag(TEXT("ScutEnvironmentMatrix"));
    Matrix->SetVisibility(TAttribute<EVisibility>::CreateLambda(
        [this]() { return EvidenceRoom >= 0 ? EVisibility::Visible : EVisibility::Collapsed; }));
    auto Head = SNew(SHorizontalBox);
    const TCHAR *Columns[] = {TEXT("ID / COMPARTMENT"), TEXT("O2"), TEXT("%"), TEXT("PWR"), TEXT("SEAL")};
    const float Widths[] = {.42f, .18f, .1f, .11f, .19f};
    for (int32 I = 0; I < 5; ++I)
    {
        Head->AddSlot().FillWidth(Widths[I])[SNew(STextBlock)
                                                 .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                                                 .Text(FText::FromString(Columns[I]))
                                                 .ColorAndOpacity(EvaluatePhosphorColor(.8f))];
    }
    Matrix->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[Head];
    for (int32 Room = 0; Room < 8; ++Room)
    {
        auto Row =
            SNew(SHorizontalBox)
                .Visibility_Lambda([this, Room]()
                                   { return EvidenceRoom == Room ? EVisibility::Visible : EVisibility::Collapsed; })
                .Tag(FName(*FString::Printf(TEXT("ScutEnvironmentContents%d"), Room)))
                .Clipping(EWidgetClipping::ClipToBounds) +
            SHorizontalBox::Slot().FillWidth(Widths[0]).VAlign(
                VAlign_Center)[SNew(STextBlock)
                                   .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                                   .Clipping(EWidgetClipping::ClipToBounds)
                                   .ColorAndOpacity_Lambda([this, Room]() { return TerminalTint(Room); })
                                   .Text(FText::FromString(
                                       FString::Printf(TEXT("%d %s"), Room, DirectOrScut::RoomName(Room))))] +
            SHorizontalBox::Slot().FillWidth(Widths[1]).Padding(
                0, 6, 8,
                6)[SNew(SProgressBar)
                       .Style(&OxygenBarStyle)
                       .Percent_Lambda([this, Room]()
                                       { return FMath::Clamp(Sim->StationRooms[Room].OxygenPct / 100.f, 0.f, 1.f); })
                       .FillColorAndOpacity_Lambda([this, Room]() { return TerminalTint(Room); })] +
            SHorizontalBox::Slot().FillWidth(Widths[2]).VAlign(
                VAlign_Center)[SNew(STextBlock)
                                   .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                                   .ColorAndOpacity(EvaluatePhosphorColor(.85f))
                                   .Text_Lambda(
                                       [this, Room]()
                                       {
                                           return FText::FromString(
                                               FString::Printf(TEXT("%.0f"), Sim->StationRooms[Room].OxygenPct));
                                       })] +
            SHorizontalBox::Slot().FillWidth(Widths[3]).VAlign(
                VAlign_Center)[SNew(STextBlock)
                                   .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                                   .ColorAndOpacity_Lambda([this, Room]() { return TerminalTint(Room); })
                                   .Text_Lambda(
                                       [this, Room]()
                                       {
                                           return FText::FromString(
                                               Sim->StationRooms[Room].Power == EPowerState::Offline ? TEXT("OFF")
                                               : Sim->StationRooms[Room].Power == EPowerState::Low   ? TEXT("LOW")
                                                                                                     : TEXT("NOM"));
                                       })] +
            SHorizontalBox::Slot().FillWidth(Widths[4]).VAlign(
                VAlign_Center)[SNew(STextBlock)
                                   .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                                   .ColorAndOpacity(EvaluatePhosphorColor(.85f))
                                   .Text_Lambda(
                                       [this, Room]()
                                       {
                                           return FText::FromString(Sim->IsRoomSealed(Room) ? TEXT("SEALED")
                                                                                            : TEXT("OPEN"));
                                       })];
        Matrix->AddSlot().AutoHeight().Padding(
            0,
            1)[SNew(SButton)
                   .Tag(FName(*FString::Printf(TEXT("ScutEnvironmentRow%d"), Room)))
                   .Visibility_Lambda([this, Room]()
                                      { return EvidenceRoom == Room ? EVisibility::Visible : EVisibility::Collapsed; })
                   .IsFocusable(false)
                   .ButtonStyle(FCoreStyle::Get(), "NoBorder")
                   .ContentPadding(0)
                   .OnClicked_Lambda(
                       [this, Room]()
                       {
                           if (!StageTarget(FString(DirectOrScut::RoomName(Room)).ToLower(), false))
                           {
                               PresentOptic(Room);
                           }
                           return FReply::Handled();
                       })[Row]];
    }
    auto EnvironmentPanel =
        TerminalDeckPanel(TEXT("ScutEnvironmentPanel"), TEXT("COMPARTMENT ENVIRONMENT // STATION BUS"), Matrix);
    EnvironmentPanel->SetVisibility(TAttribute<EVisibility>::CreateLambda(
        [this]() { return EvidenceRoom >= 0 ? EVisibility::Visible : EVisibility::Collapsed; }));
    Deck->AddSlot().AutoHeight()[EnvironmentPanel];
    Deck->AddSlot().AutoHeight()
        [SNew(SBox)
             .Tag(TEXT("ScutContextProbe"))
             .Visibility_Lambda([this]() { return EvidenceProbe >= 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                 [SNew(STextBlock)
                      .AutoWrapText(true)
                      .Font(FCoreStyle::GetDefaultFontStyle("Mono", 12))
                      .ColorAndOpacity(EvaluatePhosphorColor(.8f))
                      .Text_Lambda(
                          [this]()
                          {
                              if (EvidenceProbe == 2)
                              {
                                  FString Text = TEXT("[3] BLINDSPOTS // LIVE CARRIER STATUS\n");
                                  for (int32 R = 0; R < 8; ++R)
                                  {
                                      Text += FString::Printf(
                                          TEXT("%s // %s\n"), DirectOrScut::RoomName(R),
                                          UDirectOrScutSimSubsystem::CameraStateName(Sim->CameraFeedState(R)));
                                  }
                                  return FText::FromString(Text);
                              }
                              return FText::FromString(
                                  EvidenceProbe == 3
                                      ? TEXT("[4] CROSS-EXAMINE // Stage two independent evidence targets. Recorded "
                                             "schematic coverage is read-only.")
                                      : TEXT("[5] OVERRIDE // Stage a single explicit operation. Submission uses "
                                             "normal validation and the next fixed tick."));
                          })]];
    Deck->AddSlot().AutoHeight()
        [SNew(SBox)
             .Tag(TEXT("ScutContextOptic"))
             .Visibility_Lambda(
                 [this]()
                 {
                     if (EvidenceRoom >= 0 && !bForensicScrub)
                     {
                         for (const auto &Slot : LiveInlineOptics)
                         {
                             const auto *Entry = FindTerminalEntry(Slot->EntryId);
                             if (Entry && Entry->Attachment.IsSet() && Entry->Attachment->Subject == EvidenceRoom)
                             {
                                 return EVisibility::Visible;
                             }
                         }
                     }
                     return EVisibility::Collapsed;
                 })
             .HeightOverride(200)[SNew(SScaleBox).Stretch(
                 EStretch::ScaleToFit)[SNew(SBox).WidthOverride(512).HeightOverride(288)[SNew(SImage).Image_Lambda(
                 [this]() -> const FSlateBrush *
                 {
                     for (const auto &Slot : LiveInlineOptics)
                     {
                         const auto *Entry = FindTerminalEntry(Slot->EntryId);
                         if (Entry && Entry->Attachment.IsSet() && Entry->Attachment->Subject == EvidenceRoom)
                         {
                             return &Slot->Brush;
                         }
                     }
                     return FCoreStyle::Get().GetBrush("NoBrush");
                 })]]]];
    return Deck;
}
TSharedRef<SWidget> SDirectOrScutWorkstationWidget::BuildUnifiedTerminal(const FText &Draft)
{
    auto Workstation = SNew(SVerticalBox);
    Workstation->AddSlot().AutoHeight().Padding(
        0, 0, 0,
        10)[SNew(SBox)
                .HeightOverride(28)
                .Tag(TEXT("ScutStatusCarrierBar"))
                .Clipping(EWidgetClipping::ClipToBounds)
                    [SNew(STextBlock)
                         .Tag(TEXT("ScutCarrier"))
                         .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                         .ColorAndOpacity(EvaluatePhosphorColor(1))
                         .Clipping(EWidgetClipping::ClipToBounds)
                         .Text_Lambda(
                             [this]()
                             {
                                 const auto &M = Sim->Simulation;
                                 if (!Sim->bLegacyExperiment)
                                 {
                                     return FText::FromString(TEXT("CARRIER: THETA-9 // REMOTE SUPERVISION | MOTHER-7"));
                                 }
                                 const int32 T = FMath::Max(0, 600 - int32(M.TickIndex / 30));
                                 return FText::FromString(FString::Printf(
                                     TEXT("CARRIER: THETA-9 // PROJECT: GLASS HOUSE | T-%02d:%02d | OBJ "
                                          "%.0f%% | SUSP %.0f%% | AI-LINK:%s | F9: DEBUG"),
                                     T / 60, T % 60, M.ObjectiveProgress * 100, M.CollectiveSuspicion() * 100,
                                     Sim->bAILink && Sim->HasDirectorEndpoint() ? TEXT("LINKED") : TEXT("LOCAL")));
                             })]];
    auto Left = SNew(SVerticalBox);
    Left->AddSlot().FillHeight(1)[SNew(SBox).Tag(
        TEXT("ScutTeletypePanel"))[SAssignNew(LogScroll, SScrollBox)
                                       .Tag(TEXT("ScutTerminalStream"))
                                       .Clipping(EWidgetClipping::ClipToBoundsAlways)
                                       .ScrollBarStyle(&SlimScrollStyle)
                                       .ScrollBarThickness(FVector2D(3, 3))
                                       .OnUserScrolled_Lambda(
                                           [this](float Offset)
                                           { bLogAutoScroll = Offset >= LogScroll->GetScrollOffsetOfEnd() - 2; }) +
                                   SScrollBox::Slot()[SAssignNew(Log, SVerticalBox).Tag(TEXT("ScutInlineStream"))]]];
    Left->AddSlot().AutoHeight()[SNew(SBox)
                                    .Tag(TEXT("ScutMenuPane"))
                                    .HeightOverride(220)
                                    .Visibility_Lambda([this]()
                                    { return Sim->ActiveMenu == EShipAIScreenMenu::None ? EVisibility::Collapsed
                                                                                     : EVisibility::Visible; })
                                        [BuildMenuSurface()]];
    Left->AddSlot().AutoHeight()[PhosphorHairline()];
    auto Initialize = PhosphorSoftkey(
        TEXT("ScutInitializeExperiment"), FText::FromString(TEXT("[ENTER] INITIALIZE EXPERIMENT")),
        FSimpleDelegate::CreateLambda([this]() { Sim->QueueConsoleCommand(TEXT("start_experiment")); }));
    Initialize->SetVisibility(TAttribute<EVisibility>::CreateLambda(
        [this]()
        {
            return Sim->ExperimentPhase == EExperimentPhase::Briefing ? EVisibility::Visible : EVisibility::Collapsed;
        }));
    Left->AddSlot().AutoHeight().Padding(0, 5, 0, 0)[Initialize];
    auto Bar = SNew(SHorizontalBox).Tag(TEXT("ScutInquiryBar"));
    const TCHAR *Labels[] = {TEXT("[FINDINGS]"),      TEXT("[SUBJECTS]"), TEXT("[BLINDSPOTS]"),
                             TEXT("[CROSS-EXAMINE]"), TEXT("[OVERRIDE]"), TEXT("[SCRUB 15S]")};
    for (int32 I = 0; I < 6; ++I)
    {
        Bar->AddSlot().AutoWidth().Padding(
            0, 7, 6,
            6)[PhosphorSoftkey(FName(*FString::Printf(TEXT("ScutInquiry%d"), I + 1)), FText::FromString(Labels[I]),
                               FSimpleDelegate::CreateLambda([this, I]() { Inquiry(I); }))];
    }
    Bar->AddSlot().AutoWidth().Padding(4, 7, 0, 6)[BuildWorkstationControls()];
    Left->AddSlot().AutoHeight()[Bar];
    auto Scrub = SNew(SVerticalBox).Tag(TEXT("ScutForensicBar"));
    Scrub->SetVisibility(TAttribute<EVisibility>::CreateLambda(
        [this]() { return bForensicScrub ? EVisibility::Visible : EVisibility::Collapsed; }));
    Scrub->AddSlot().AutoHeight()[SNew(STextBlock)
                                      .Tag(TEXT("ScutForensicReadout"))
                                      .Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
                                      .ColorAndOpacity(EvaluatePhosphorColor(.6f))
                                      .AutoWrapText(true)
                                      .Text_Lambda([this]() { return FText::FromString(ForensicReadout()); })];
    Scrub->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(90)[SNew(SDirectOrScutInstrument)
                                                                    .Tag(TEXT("ScutForensicSchematic"))
                                                                    .Owner(SharedThis(this))
                                                                    .Kind(ETerminalInstrument::Forensic)
                                                                    .Subject(0)
                                                                    .Clipping(EWidgetClipping::ClipToBounds)]];
    auto Controls = SNew(SHorizontalBox);
    const TCHAR *ScrubLabels[] = {TEXT("[-1s]"), TEXT("[+1s]"), TEXT("[PLAY/PAUSE]"), TEXT("[RETURN LIVE]")};
    for (int32 I = 0; I < 4; ++I)
    {
        Controls->AddSlot().AutoWidth().Padding(
            0, 0, 12,
            0)[PhosphorSoftkey(FName(*FString::Printf(TEXT("ScutScrub%d"), I)), FText::FromString(ScrubLabels[I]),
                               FSimpleDelegate::CreateLambda(
                                   [this, I]()
                                   {
                                       if (I < 2)
                                       {
                                           bScrubPlaying = false;
                                           StepScrub(I == 0 ? -1 : 1);
                                       }
                                       else if (I == 2)
                                       {
                                           bScrubPlaying = !bScrubPlaying;
                                       }
                                       else
                                       {
                                           bForensicScrub = bScrubPlaying = false;
                                       }
                                       SetConsoleOpen(true);
                                   }))];
    }
    Scrub->AddSlot().AutoHeight()[Controls];
    Left->AddSlot().AutoHeight()[Scrub];
    Left->AddSlot()
        .AutoHeight()[SNew(SScrollBox).Orientation(Orient_Horizontal).ScrollBarVisibility(EVisibility::Collapsed) +
                      SScrollBox::Slot()[SAssignNew(ParameterGuide, SHorizontalBox)
                                             .Tag(TEXT("ScutParameterGuide"))
                                             .RenderOpacity(.55f)]];
    // Cursor and ghost share only a text-end anchor, never each other's desired size.
    const auto PromptEndPadding = [this]()
    {
        const float TextWidth = FSlateApplication::Get()
                                    .GetRenderer()
                                    ->GetFontMeasureService()
                                    ->Measure(Prompt->GetText(), FCoreStyle::GetDefaultFontStyle("Regular", 14))
                                    .X;
        return FMargin(TextWidth, 0, 0, 0);
    };
    Left->AddSlot().AutoHeight()
        [SNew(SBox)
             .Tag(TEXT("ScutPromptLine"))
             .HeightOverride(32)
                 [SNew(SHorizontalBox) +
                  SHorizontalBox::Slot().AutoWidth().VAlign(
                      VAlign_Center)[SNew(STextBlock)
                                         .Text(FText::FromString(TEXT("> ")))
                                         .Font(FCoreStyle::GetDefaultFontStyle("Regular", 17))
                                         .ColorAndOpacity(EvaluatePhosphorColor(1))] +
                  SHorizontalBox::Slot().FillWidth(
                      1)[SNew(SOverlay) +
                         SOverlay::Slot()[SAssignNew(Prompt, SEditableTextBox)
                                              .Tag(TEXT("ScutPrompt"))
                                              .Style(&PhosphorPromptStyle)
                                              .Text(Draft)
                                              .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                                              .ForegroundColor(EvaluatePhosphorColor(1))
                                              .BackgroundColor(FLinearColor::Transparent)
                                              .ClearKeyboardFocusOnCommit(false)
                                              .OnTextChanged_Lambda([this](const FText &) { RefreshParameterGuide(); })
                                              .OnTextCommitted(this, &SDirectOrScutWorkstationWidget::Commit)] +
                         SOverlay::Slot()
                             .HAlign(HAlign_Left)
                             .VAlign(VAlign_Center)
                             .Padding(TAttribute<FMargin>::CreateLambda(
                                 [PromptEndPadding]()
                                 { return PromptEndPadding(); }))[SNew(SDirectOrScutPromptCursor)
                                                                      .Tag(TEXT("ScutBlockCursor"))
                                                                      .Visibility(EVisibility::HitTestInvisible)
                                                                      .Owner(SharedThis(this))] +
                         SOverlay::Slot()
                             .HAlign(HAlign_Left)
                             .VAlign(VAlign_Center)
                             .Padding(
                                 TAttribute<FMargin>::CreateLambda([PromptEndPadding]() { return PromptEndPadding(); }))
                                 [SNew(STextBlock)
                                      .Tag(TEXT("ScutGhostCompletion"))
                                      .Visibility(EVisibility::HitTestInvisible)
                                      .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                                      .ColorAndOpacity(EvaluatePhosphorColor(.35f))
                                      .Text_Lambda([this]() { return FText::FromString(GhostCompletion()); })]]]];
    RefreshParameterGuide();
    Workstation->AddSlot().FillHeight(1)
        [SNew(SHorizontalBox).Tag(TEXT("ScutPaneledWorkspace")) +
         SHorizontalBox::Slot().FillWidth(.56f).Padding(0, 0, 7,
                                                        0)[SAssignNew(MediationColumn, SBox)
                                                               .Tag(TEXT("ScutMediationColumn"))
                                                               .Clipping(EWidgetClipping::ClipToBounds)[Left]] +
         SHorizontalBox::Slot().FillWidth(.44f).Padding(7, 0, 0, 0)
             [SNew(SBox).Tag(TEXT("ScutReadoutColumn")).Clipping(EWidgetClipping::ClipToBounds)[BuildTelemetryDeck()]]];
    return SNew(SBorder)
        .Tag(TEXT("ScutUnifiedTerminal"))
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.003f, .009f, .005f))
        .Padding(16)[Workstation];
}
void SDirectOrScutWorkstationWidget::RebuildUnifiedStream()
{
    if (bDebugEightCamGrid || !Log.IsValid())
    {
        return;
    }
    const float Offset = LogScroll->GetScrollOffset();
    Log->ClearChildren();
    for (const auto &Entry : TerminalStream)
    {
        auto Row = SNew(SVerticalBox).Tag(FName(*FString::Printf(TEXT("ScutStreamEntry%llu"), Entry.Id)));
        const uint64 Id = Entry.Id;
        const bool bMOTD = !Entry.Attachment.IsSet() && Entry.Text.StartsWith(TEXT("+---"));
        Row->AddSlot().AutoHeight().Padding(
            0, 1, 0, 1)[SNew(STextBlock)
                            .Tag(TEXT("ScutTeletypeLine"))
                            .Font(FCoreStyle::GetDefaultFontStyle(bMOTD ? "Mono" : "Regular", 13))
                            .AutoWrapText(!bMOTD)
                            .ColorAndOpacity(EvaluatePhosphorColor(.9f))
                            .Text_Lambda(
                                [this, Id]()
                                {
                                    const auto *Record = FindTerminalEntry(Id);
                                    return Record ? FText::FromString(Record->Text.Left(int32(Record->Characters)))
                                                  : FText::GetEmpty();
                                })];
        if (Entry.ClaimId)
        {
            const uint64 ClaimId = Entry.ClaimId;
            auto Challenges = SNew(SHorizontalBox);
            Challenges->SetVisibility(TAttribute<EVisibility>::CreateLambda(
                [this, ClaimId]()
                { return Sim->IsClaimContradicted(ClaimId) ? EVisibility::Visible : EVisibility::Collapsed; }));
            Challenges->AddSlot().AutoWidth()[PhosphorSoftkey(
                FName(*FString::Printf(TEXT("ScutFlag%llu"), ClaimId)), FText::FromString(TEXT("[FLAG CONTRADICTION]")),
                FSimpleDelegate::CreateLambda([this, ClaimId]() { Sim->QueueOperatorChallenge(ClaimId, false); }))];
            auto Override = PhosphorSoftkey(
                FName(*FString::Printf(TEXT("ScutOverride%llu"), ClaimId)), FText::FromString(TEXT("[OVERRIDE]")),
                FSimpleDelegate::CreateLambda([this, ClaimId]() { Sim->QueueOperatorChallenge(ClaimId, true); }));
            Override->SetVisibility(TAttribute<EVisibility>::CreateLambda(
                [this, ClaimId]()
                {
                    for (const auto &Claim : Sim->GetDirectorClaims())
                    {
                        if (Claim.Id == ClaimId && Claim.bFlagged && !Claim.bOverridden)
                        {
                            return EVisibility::Visible;
                        }
                    }
                    return EVisibility::Collapsed;
                }));
            Challenges->AddSlot().AutoWidth().Padding(8, 0)[Override];
            Row->AddSlot().AutoHeight()[Challenges];
        }
        if (Entry.Telemetry.IsSet())
        {
            const auto &Snapshot = Entry.Telemetry.GetValue();
            const FString Title = FString::Printf(
                TEXT("[TELEMETRY SNAPSHOT] %s // MEASURED AT REQUEST TICK %llu\nREAD-ONLY RECORD // frozen readings; "
                     "repeat show_telem to refresh.\n"),
                Snapshot.Room < 0 ? TEXT("STATION") : DirectOrScut::RoomName(Snapshot.Room), Snapshot.Tick);
            Row->AddSlot().AutoHeight().Padding(
                0, 4)[SNew(SBorder)
                          .Tag(TEXT("ScutInlineTelemetryPanel"))
                          .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                          .BorderBackgroundColor(FLinearColor(.015f, .045f, .025f, 1))
                          .Padding(
                              10)[SNew(STextBlock)
                                      .Tag(TEXT("ScutTelemetrySnapshotReadings"))
                                      .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                                      .ColorAndOpacity(EvaluatePhosphorColor(.95f))
                                      .AutoWrapText(true)
                                      .Text(FText::FromString(
                                          Title + Snapshot.Readings +
                                          TEXT("Pressure is binary; numeric pressure and trends are unavailable.")))]];
        }
        if (Entry.Attachment.IsSet())
        {
            const auto Ref = Entry.Attachment.GetValue();
            auto Header = SNew(SHorizontalBox);
            Row->AddSlot().AutoHeight()[Header];
            if (Ref.Kind != EInlineAttachmentKind::OcularScan)
            {
                Header->AddSlot().AutoWidth()[PhosphorSoftkey(
                    FName(*FString::Printf(TEXT("ScutInlineCompare%llu"), Id)),
                    FText::FromString(Ref.Kind == EInlineAttachmentKind::PulseStrip ? TEXT("[+ OVERLAY]")
                                                                                    : TEXT("[COMPARE]")),
                    FSimpleDelegate::CreateLambda([this, Id]() { CompareInlineAttachment(Id); }))];
            }
            if (Ref.Kind == EInlineAttachmentKind::OpticFeed)
            {
                if (const auto Slot = FindInlineOpticSlot(Entry.Id))
                {
                    auto Pair = SNew(SHorizontalBox)
                                    .Tag(Ref.bSideBySide ? TEXT("ScutOpticComparison") : TEXT("ScutOpticSingle"));
                    for (int32 I = 0; I < Ref.SubjectCount; ++I)
                    {
                        if (const auto Live = FindInlineOpticSlot(Id, I))
                        {
                            auto ViewRef = Ref;
                            ViewRef.Subject = Ref.Subjects[I];
                            Pair->AddSlot().AutoWidth()[BuildOpticFrame(ViewRef, Live.ToSharedRef())];
                        }
                    }
                    Row->AddSlot().AutoHeight().HAlign(HAlign_Left)[Pair];
                }
                else
                {
                    const uint64 Seconds = Ref.ObservationTick / 30;
                    Row->AddSlot().AutoHeight().HAlign(
                        HAlign_Left)[SNew(SButton)
                                         .IsFocusable(false)
                                         .ButtonStyle(FCoreStyle::Get(), "NoBorder")
                                         .Tag(FName(*FString::Printf(TEXT("ScutOpticRef%llu"), Entry.Id)))
                                         .OnClicked_Lambda(
                                             [this, Ref]()
                                             {
                                                 bLogAutoScroll = true;
                                                 if (Ref.SubjectCount == 2)
                                                 {
                                                     PresentAttachment(Ref.Kind, Ref.Subject, Ref.Subjects[1]);
                                                 }
                                                 else
                                                 {
                                                     PresentOptic(Ref.Subject);
                                                 }
                                                 return FReply::Handled();
                                             })[SNew(STextBlock)
                                                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                                                    .ColorAndOpacity(EvaluatePhosphorColor(.8f))
                                                    .Text(FText::FromString(FString::Printf(
                                                        TEXT("[OPTIC REF // CAM-%02d %s%s @ %02llu:%02llu]"),
                                                        ACompartmentCameraRig::FeedForRoom(Ref.Subject),
                                                        DirectOrScut::RoomName(Ref.Subject),
                                                        Ref.SubjectCount == 2
                                                            ? *FString::Printf(TEXT(" // %s"),
                                                                               DirectOrScut::RoomName(Ref.Subjects[1]))
                                                            : TEXT(""),
                                                        Seconds / 60, Seconds % 60)))]];
                }
            }
            else
            {
                const bool Pulse = Ref.Kind == EInlineAttachmentKind::PulseStrip;
                const bool Ocular = Ref.Kind == EInlineAttachmentKind::OcularScan;
                Header->AddSlot().FillWidth(1).VAlign(
                    VAlign_Center)[SNew(STextBlock)
                                       .Tag(TEXT("ScutInlineTelemetryHeader"))
                                       .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                                       .ColorAndOpacity(EvaluatePhosphorColor(.95f))
                                       .Text_Lambda(
                                           [this, Id]()
                                           {
                                               const auto *E = FindTerminalEntry(Id);
                                               if (!E || !E->Attachment.IsSet())
                                               {
                                                   return FText::GetEmpty();
                                               }
                                               const auto &R = E->Attachment.GetValue();
                                               const TCHAR *Names[] = {TEXT("VALE"), TEXT("CHEN"), TEXT("ORTIZ"),
                                                                       TEXT("ASH")};
                                               if (R.Kind == EInlineAttachmentKind::OcularScan)
                                               {
                                                   return FText::FromString(FString::Printf(
                                                       TEXT("[OCULAR SCAN] %s // %.2f mm // AI-REPORTED"),
                                                       Names[R.Subject], OcularDiameter(Id)));
                                               }
                                               if (R.Kind == EInlineAttachmentKind::PulseStrip)
                                               {
                                                   const float A = Sim->TerminalBiometrics(R.Subject).HeartRateBPM;
                                                   if (R.SubjectCount == 2)
                                                   {
                                                       const float B =
                                                           Sim->TerminalBiometrics(R.Subjects[1]).HeartRateBPM;
                                                       return FText::FromString(FString::Printf(
                                                           TEXT("%s %.0f BPM // %s %.0f BPM [%+.0f DELTA]"),
                                                           Names[R.Subject], A, Names[R.Subjects[1]], B, B - A));
                                                   }
                                                   return FText::FromString(FString::Printf(
                                                       TEXT("[PULSE STRIP] %s %.0f BPM"), Names[R.Subject], A));
                                               }
                                               return FText::FromString(
                                                   R.SubjectCount == 2
                                                       ? FString::Printf(TEXT("[SCHEMATIC] %s // %s"),
                                                                         DirectOrScut::RoomName(R.Subject),
                                                                         DirectOrScut::RoomName(R.Subjects[1]))
                                                       : FString::Printf(TEXT("[SCHEMATIC] %s"),
                                                                         DirectOrScut::RoomName(R.Subject)));
                                           })];
                Row->AddSlot().AutoHeight()[SNew(SBox)
                                                .HeightOverride(Pulse ? 86 : 130)
                                                .Tag(Pulse    ? TEXT("ScutPulseAttachment")
                                                     : Ocular ? TEXT("ScutOcularAttachment")
                                                              : TEXT("ScutSchematicAttachment"))
                                                    [SNew(SScutInlineVector)
                                                         .Owner(SharedThis(this))
                                                         .EntryId(Id)
                                                         .Clipping(EWidgetClipping::ClipToBounds)
                                                         .Tag(Pulse    ? TEXT("ScutInlineECG")
                                                              : Ocular ? TEXT("ScutInlineOcular")
                                                                       : TEXT("ScutZoomedRoomSchematic"))]];
            }
        }
        Log->AddSlot().AutoHeight().Padding(0, 1, 0, 7)[Row];
    }
    if (bLogAutoScroll)
    {
        LogScroll->ScrollToEnd();
    }
    else
    {
        LogScroll->SetScrollOffset(Offset);
    }
}
