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
#include "Framework/Application/IInputProcessor.h"
#include "Styling/CoreStyle.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Rendering/DrawElementTypes.h"
#include "Materials/Material.h"
#include "Widgets/SLeafWidget.h"

// Stable heap-owned blocks keep Slate brush addresses valid across bounded stream eviction.
struct FTerminalStreamBlock
{
    enum class EKind
    {
        Prose,
        Optic,
        Instrument
    } Kind = EKind::Prose;
    FString Text;
    int32 Subject = 0;
    ETerminalInstrument Instrument = ETerminalInstrument::ECG;
    float Characters = 0, Wipe = 0;
    bool bClosed = false;
    TStrongObjectPtr<UMaterialInstanceDynamic> Material;
    FSlateBrush Brush;
    TSharedPtr<SImage> Image;
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

// Claim developer view keys before PIE editor command processing,
// only while this workstation owns focus, before editor command processing.
SDirectOrScutWorkstationWidget::~SDirectOrScutWorkstationWidget()
{
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
    Sim->LogEvent(TEXT("Backtick / slash: focus CMD> | Tab: categories or completion | Esc: camera navigation | "
                       "Enter/F: inspect | G: grid | help: reference"));
    Sim->LogEvent(TEXT("Crew: Vale / engineer, Chen / medic, Ortiz / technician, Ash / android. Commands use full room "
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
        if (bConsoleOpen)
        {
            FSlateApplication::Get().SetKeyboardFocus(Prompt);
        }
        return;
    }
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
        6, 3)[SNew(SHorizontalBox) +
              SHorizontalBox::Slot()
                  .AutoWidth()[SNew(STextBlock)
                                   .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                                   .ColorAndOpacity(FLinearColor(.4f, .85f, .5f))
                                   .Text(FText::FromString(TEXT("[~ / /] CMD> | [TAB] MENUS | [ESC] CAMERAS | [ARROWS "
                                                                "/ 1-8] FEED | [ENTER / F] INSPECT | [G] GRID  ")))] +
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
    Left->AddSlot().AutoHeight().Padding(
        2, 3)[SNew(STextBlock)
                  .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                  .ColorAndOpacity(FLinearColor(.3f, 1, .45f))
                  .AutoWrapText(true)
                  .Text_Lambda([this]() { return FText::FromString(Sim->ScreenMenuTitle()); })];
    Left->AddSlot()
        .AutoHeight()[SNew(SButton)
                          .Tag(TEXT("ScutMenuBack"))
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
                    .OnKeyCharHandler_Lambda(
                        [this](const FGeometry &, const FCharacterEvent &E)
                        {
                            const bool Suppress =
                                SuppressPromptChar && (E.GetCharacter() == SuppressPromptChar ||
                                                       (SuppressPromptChar == '`' && E.GetCharacter() == '~'));
                            SuppressPromptChar = 0;
                            return Suppress ? FReply::Handled() : FReply::Unhandled();
                        })
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
void SDirectOrScutWorkstationWidget::RefreshLog()
{
    if (!bDebugEightCamGrid)
    {
        for (; UnifiedEventCursor < Sim->Events.Num(); ++UnifiedEventCursor)
        {
            if (Sim->Events[UnifiedEventCursor].bTerminalVisible)
            {
                AppendTerminalProse(TEXT("SYSTEM> ") + Sim->Events[UnifiedEventCursor].Text);
            }
        }
        SeenEvents = Sim->EventRevision;
        return;
    }
    const float Offset = LogScroll->GetScrollOffset();
    Log->ClearChildren();
    for (const auto &E : Sim->Events)
    {
        if (E.bTerminalVisible)
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
void SDirectOrScutWorkstationWidget::Autocomplete()
{
    const FString Input = Prompt->GetText().ToString();
    if (Completions.IsEmpty() || Input != LastCompletion)
    {
        Completions = Sim->CompleteConsoleCommand(Input);
        CompletionCursor = 0;
    }
    else
    {
        CompletionCursor = (CompletionCursor + 1) % Completions.Num();
    }
    if (!Completions.IsEmpty())
    {
        LastCompletion = Completions[CompletionCursor];
        Prompt->SetText(FText::FromString(LastCompletion));
        Prompt->GoTo(ETextLocation::EndOfDocument);
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
            if (History.Num() >= 64)
            {
                History.RemoveAt(0);
            }
            History.Add(Input);
        }
        HistoryCursor = History.Num();
        HistoryDraft.Empty();
        Completions.Reset();
        LastCompletion.Empty();
        Prompt->SetText(FText::GetEmpty());
    }
}
FReply SDirectOrScutWorkstationWidget::OnMouseButtonDown(const FGeometry &, const FPointerEvent &E)
{
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
    if (Sim->ExperimentPhase == EExperimentPhase::Resolved)
    {
        return FReply::Unhandled();
    }
    if (!bDebugEightCamGrid)
    {
        if (Key == EKeys::Escape)
        {
            SetConsoleOpen(false);
            return FReply::Handled().SetUserFocus(AsShared());
        }
        if ((Key == EKeys::Tilde || Key == EKeys::Slash) && !IsPromptFocused())
        {
            SuppressPromptChar = Key == EKeys::Tilde ? '`' : '/';
            SetConsoleOpen(true);
            return FReply::Handled();
        }
        if (IsPromptFocused())
        {
            if (Key == EKeys::Tab)
            {
                Autocomplete();
                return FReply::Handled();
            }
            if (Key == EKeys::Up || Key == EKeys::Down)
            {
                BrowseHistory(Key == EKeys::Up ? -1 : 1);
                return FReply::Handled();
            }
            return FReply::Unhandled();
        }
        const FKey Probes[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::R};
        for (int32 I = 0; I < 6; ++I)
        {
            if (Key == Probes[I] && !E.IsRepeat())
            {
                Inquiry(I);
                return FReply::Handled();
            }
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
    if (Key == EKeys::Tilde && !E.IsRepeat())
    {
        SuppressPromptChar = '`';
        SetConsoleOpen(true);
        return FReply::Handled();
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
    if (Key == EKeys::Slash && !IsPromptFocused())
    {
        SuppressPromptChar = '/';
        SetConsoleOpen(true);
        return FReply::Handled();
    }
    if (IsPromptFocused())
    {
        bConsoleOpen = true;
        bMenuFocused = false;
    }
    if (!IsPromptFocused() && !E.IsRepeat() && !E.IsControlDown() && !E.IsAltDown() &&
        (Key == EKeys::Q || Key == EKeys::E || Key == EKeys::B || Key == EKeys::X))
    {
        ExecuteRoomQuickAction(Key);
        return FReply::Handled().SetUserFocus(AsShared());
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
    if (E.IsRepeat())
    {
        return FReply::Unhandled();
    }
    const FKey Keys[] = {EKeys::One,  EKeys::Two, EKeys::Three, EKeys::Four,
                         EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight};
    for (int32 I = 0; I < 8; ++I)
    {
        if (Key == Keys[I])
        {
            SelectFeed(I + 1);
            return FReply::Handled();
        }
    }
    if (Key == EKeys::G)
    {
        ShowGrid();
        return FReply::Handled();
    }
    return FReply::Unhandled();
}
FReply SDirectOrScutWorkstationWidget::OnKeyDown(const FGeometry &Geometry, const FKeyEvent &E)
{
    if (!bDebugEightCamGrid || bConsoleOpen || !Sim->bWorkstationVisible)
    {
        return FReply::Unhandled();
    }
    const auto Key = E.GetKey();
    if (Key == EKeys::Enter || Key == EKeys::F)
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
    if (!bDebugEightCamGrid || !MenuList)
    {
        return;
    }
    MenuList->ClearChildren();
    MenuEntries.Reset();
    RefreshMenuChoices();
    HighlightedChoice = 0;
    for (int32 I = 0; I < MenuLabels.Num(); ++I)
    {
        TSharedRef<SWidget> Entry =
            SNew(SButton)
                .Tag(FName(*FString::Printf(TEXT("ScutMenuChoice%d"), I + 1)))
                .IsFocusable(false)
                .ButtonColorAndOpacity_Lambda(
                    [this, I]()
                    {
                        return HighlightedChoice == I ? FLinearColor(.12f, .4f, .2f, .8f)
                                                      : FLinearColor(.02f, .06f, .03f, .35f);
                    })
                .IsEnabled_Lambda([this, I]() { return MenuEnabled.IsValidIndex(I) && MenuEnabled[I]; })
                .OnClicked_Lambda(
                    [this, I]()
                    {
                        bConsoleOpen = true;
                        bMenuFocused = true;
                        HighlightedChoice = I;
                        ExecuteHighlighted();
                        return FReply::Handled().SetUserFocus(AsShared());
                    })[SNew(STextBlock)
                           .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                           .AutoWrapText(true)
                           .ColorAndOpacity(FLinearColor(.4f, 1, .55f))
                           .Text_Lambda(
                               [this, I]()
                               { return FText::FromString(MenuLabels.IsValidIndex(I) ? MenuLabels[I] : TEXT("")); })];
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
            SelectFeed(ACompartmentCameraRig::FeedForRoom(Room), false);
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
    if (bDebugEightCamGrid && SeenMenuRevision != Sim->MenuRevision)
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
    if (bDebugEightCamGrid && MenuRefreshClock >= .2f)
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

TSharedRef<SWidget> SDirectOrScutWorkstationWidget::BuildRoomQuickActions()
{
    auto Actions = SNew(SHorizontalBox);
    const FKey Keys[] = {EKeys::Q, EKeys::E, EKeys::B, EKeys::X};
    const TCHAR *Labels[] = {TEXT("[Q] LURE HERE"), TEXT("[E] PWR CYCLE"), TEXT("[B] SEAL/UNSEAL"),
                             TEXT("[X] SPOOF LOG")};
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const FKey Key = Keys[Index];
        Actions->AddSlot().AutoWidth().Padding(
            2)[SNew(SButton)
                   .Tag(FName(*FString::Printf(TEXT("ScutQuickAction%d"), Index)))
                   .IsFocusable(false)
                   .IsEnabled_Lambda(
                       [this]() { return Sim->ExperimentPhase == EExperimentPhase::Active && !Sim->IsUplinkLocked(); })
                   .OnClicked_Lambda(
                       [this, Key]()
                       {
                           ExecuteRoomQuickAction(Key);
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

void SDirectOrScutWorkstationWidget::ExecuteRoomQuickAction(FKey Key)
{
    if (Sim->ExperimentPhase != EExperimentPhase::Active || !Sim->bWorkstationVisible)
    {
        return;
    }
    const int32 Room = ACompartmentCameraRig::RoomForFeed(ActiveFeed);
    const FString Name = DirectOrScut::RoomName(Room);
    FString Command;
    if (Key == EKeys::Q)
    {
        Command = TEXT("intercom_lie ash ") + Name;
    }
    else if (Key == EKeys::E)
    {
        const EPowerState Power = Sim->StationRooms[Room].Power;
        Command = TEXT("power ") + Name +
                  (Power == EPowerState::Nominal ? TEXT(" low")
                   : Power == EPowerState::Low   ? TEXT(" off")
                                                 : TEXT(" nominal"));
    }
    else if (Key == EKeys::B)
    {
        Command = (Sim->IsRoomSealed(Room) ? TEXT("unseal ") : TEXT("seal ")) + Name;
    }
    else if (Key == EKeys::X)
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
float SDirectOrScutWorkstationWidget::GetInlineWipeProgress() const
{
    const auto Block = ActiveOptic.Pin();
    return Block ? Block->Wipe : 0;
}
UMaterialInstanceDynamic *SDirectOrScutWorkstationWidget::GetInlineOpticMaterial() const
{
    const auto Block = ActiveOptic.Pin();
    return Block ? Block->Material.Get() : nullptr;
}
int32 SDirectOrScutWorkstationWidget::GetInlineInstrumentCount() const
{
    int32 Count = 0;
    for (const auto &Block : TerminalStream)
    {
        Count += Block->Kind == FTerminalStreamBlock::EKind::Instrument;
    }
    return Count;
}
void SDirectOrScutWorkstationWidget::AppendTerminalBlock(TSharedRef<FTerminalStreamBlock> Block)
{
    if (TerminalStream.Num() >= 48)
    {
        TerminalStream.RemoveAt(0);
    }
    TerminalStream.Add(Block);
    if (!bDebugEightCamGrid && Log.IsValid())
    {
        RebuildUnifiedStream();
    }
}
void SDirectOrScutWorkstationWidget::AppendTerminalProse(const FString &Text)
{
    auto Block = MakeShared<FTerminalStreamBlock>();
    Block->Text = Text.Left(2048);
    AppendTerminalBlock(Block);
}
void SDirectOrScutWorkstationWidget::ResetTerminalPresentation()
{
    TerminalStream.Reset();
    ActiveOptic.Reset();
    PresentationSeconds = 0;
    SetPresentationNoise(0, 0);
    UnifiedEventCursor = Sim->Events.Num();
    AppendTerminalProse(
        TEXT("MOTHER-7> Carrier established. Eight compartments answer. Their silence is not evidence of safety."));
    PresentOptic(0);
    PresentInstrument(ETerminalInstrument::ECG, 0);
    PresentInstrument(ETerminalInstrument::Schematic, 0);
}
bool SDirectOrScutWorkstationWidget::PresentOptic(int32 Room)
{
    if (!Sim.IsValid() || !Rig.IsValid() || Room < 0 || Room >= 8)
    {
        return false;
    }
    const int32 Feed = ACompartmentCameraRig::FeedForRoom(Room);
    if (const auto Previous = ActiveOptic.Pin())
    {
        Previous->bClosed = true;
    }
    auto Block = MakeShared<FTerminalStreamBlock>();
    Block->Kind = FTerminalStreamBlock::EKind::Optic;
    Block->Subject = Room;
    Block->Text = FString::Printf(TEXT("[OPTIC BUFFER] %s"), DirectOrScut::RoomName(Room));
    if (TerminalMaterials[Feed].IsValid())
    {
        Block->Material.Reset(UMaterialInstanceDynamic::Create(TerminalMaterials[Feed]->Parent, GetTransientPackage()));
        ApplyTerminalMaterialParameters(*Block->Material.Get(), Rig->Targets[Feed].Get(), FVector2f(480, 270),
                                        PresentationSeconds, 0, 0);
        Block->Brush.SetResourceObject(Block->Material.Get());
    }
    else
    {
        Block->Brush.SetResourceObject(Rig->Targets[Feed].Get());
    }
    Block->Brush.ImageSize = FVector2D(1024, 576);
    Block->Brush.DrawAs = ESlateBrushDrawType::Image;
    ActiveFeed = Feed;
    SelectedCameraIndex = Feed - 1;
    ActiveOptic = Block;
    AppendTerminalBlock(Block);
    return true;
}
bool SDirectOrScutWorkstationWidget::PresentInstrument(ETerminalInstrument Kind, int32 Subject)
{
    if ((Kind != ETerminalInstrument::ECG && Kind != ETerminalInstrument::Schematic) || Subject < 0 ||
        Subject >= (Kind == ETerminalInstrument::ECG ? 4 : 8))
    {
        return false;
    }
    auto Block = MakeShared<FTerminalStreamBlock>();
    Block->Kind = FTerminalStreamBlock::EKind::Instrument;
    Block->Instrument = Kind;
    Block->Subject = Subject;
    const TCHAR *Crew[] = {TEXT("VALE"), TEXT("CHEN"), TEXT("ORTIZ"), TEXT("ASH")};
    Block->Text = Kind == ETerminalInstrument::ECG
                      ? FString::Printf(TEXT("[PULSE STRIP] %s // LIVE TELEMETRY"), Crew[Subject])
                      : FString::Printf(TEXT("[COMPARTMENT SCHEMATIC] %s // BULKHEAD INDICATION"),
                                        DirectOrScut::RoomName(Subject));
    AppendTerminalBlock(Block);
    return true;
}
void SDirectOrScutWorkstationWidget::Inquiry(int32 Index)
{
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
        break;
    case 2:
        AppendTerminalProse(TEXT("MOTHER-7> Optical availability does not establish complete coverage. Request "
                                 "show_cam <room> to inspect another compartment."));
        break;
    case 3:
        AppendTerminalProse(TEXT("SYSTEM> Cross-examination director is not available yet. Request show_cam <room> or "
                                 "show_widget schematic <room> for current evidence."));
        break;
    case 4:
        AppendTerminalProse(TEXT("SYSTEM> No privileged override is granted. Enter an explicit operational command; "
                                 "normal fixed-tick validation applies."));
        SetConsoleOpen(true);
        break;
    case 5:
        AppendTerminalProse(TEXT("SYSTEM> SCRUB 15S unavailable: rolling forensic coverage is not implemented. Current "
                                 "optics remain LIVE; simulation time is unchanged."));
        break;
    }
}
bool SDirectOrScutWorkstationWidget::HandlePresentationCommand(const FString &Input)
{
    const FString Command = Input.TrimStartAndEnd().ToLower();
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
    const auto RoomId = [](const FString &Name)
    {
        for (int32 I = 0; I < 8; ++I)
        {
            if (Name.Equals(DirectOrScut::RoomName(I), ESearchCase::IgnoreCase))
            {
                return I;
            }
        }
        if (Name.Len() == 1 && Name[0] >= '0' && Name[0] <= '7')
        {
            return int32(Name[0] - '0');
        }
        return -1;
    };
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
                TEXT("SYSTEM> Usage: show_widget ecg <vale|chen|ortiz|ash> or show_widget schematic <room>."));
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
    bool bTyping = false;
    for (const auto &Block : TerminalStream)
    {
        if (Block->Kind == FTerminalStreamBlock::EKind::Prose && Block->Characters < Block->Text.Len() && !bTyping)
        {
            Block->Characters = FMath::Min(float(Block->Text.Len()), Block->Characters + Step * 100);
            bTyping = true;
        }
        if (Block->Kind != FTerminalStreamBlock::EKind::Optic || Block->bClosed)
        {
            continue;
        }
        Block->Wipe = FMath::Min(1.f, Block->Wipe + Step / .65f);
        if (!Block->Material.IsValid())
        {
            continue;
        }
        FVector2f Pixels(480, 270);
        if (Block->Image.IsValid())
        {
            const auto &G = Block->Image->GetCachedGeometry();
            Pixels = FVector2f(G.GetLocalSize() * G.Scale);
        }
        ApplyTerminalMaterialParameters(*Block->Material.Get(),
                                        Rig->Targets[ACompartmentCameraRig::FeedForRoom(Block->Subject)].Get(), Pixels,
                                        PresentationSeconds, Block->Wipe, 0);
        Block->Material->SetVectorParameterValue(TEXT("PhosphorTint"), TerminalTint(Block->Subject));
        Block->Material->SetScalarParameterValue(
            TEXT("SignalOfflineNoise"), Sim->StationRooms[Block->Subject].Power == EPowerState::Offline ? 1.f : 0.f);
        Block->Material->SetScalarParameterValue(TEXT("ScrubTrackingNoise"), ScrubTrackingNoise);
        Block->Material->SetScalarParameterValue(TEXT("VoiceBleedIntensity"), VoiceBleedIntensity);
    }
}
int32 SDirectOrScutWorkstationWidget::PaintTerminalInstrument(FSlateWindowElementList &Elements, int32 Layer,
                                                              const FGeometry &Geometry, ETerminalInstrument Kind,
                                                              int32 Subject) const
{
    if (!Sim.IsValid() || Subject < 0 || Subject >= (Kind == ETerminalInstrument::ECG ? 4 : 8) ||
        (Kind != ETerminalInstrument::ECG && Kind != ETerminalInstrument::Schematic &&
         Kind != ETerminalInstrument::Reticle))
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
    else if (Kind == ETerminalInstrument::Schematic)
    {
        for (int32 I = 0; I < 8; ++I)
        {
            BuildCompartmentOutline(I, Size, Points);
            DrawTerminalLines(Elements, Layer, Geometry, Points, I == Subject ? Tint : Tint * .22f);
        }
        const float Scale = FMath::Min(Size.X, Size.Y) / 5200.f;
        for (const auto &Door : DirectOrScut::DoorSites())
        {
            if (Door.RoomA != Subject && Door.RoomB != Subject)
            {
                continue;
            }
            const FVector2f Center(Size.X * .5f + Door.Position.X * Scale, Size.Y * .5f - Door.Position.Y * Scale);
            bool bSealed = false;
            for (const auto &State : Sim->Simulation.Doors)
            {
                if (State.RoomA == Door.RoomA && State.RoomB == Door.RoomB)
                {
                    bSealed = !State.bIsOpen;
                    break;
                }
            }
            BuildSealIndicator(Center, 4, bSealed, Points);
            DrawTerminalLines(Elements, Layer + 1, Geometry, Points, Tint);
        }
    }
    else
    {
        TArray<TArray<FVector2f>> Arcs;
        BuildReticlePoints(Size * .5f, FMath::Min(Size.X, Size.Y) * .42f, 4, .22f, Arcs);
        for (const auto &Arc : Arcs)
        {
            DrawTerminalLines(Elements, Layer, Geometry, Arc, Tint * .5f);
        }
    }
    return Layer + 1;
}
TSharedRef<SWidget> SDirectOrScutWorkstationWidget::BuildUnifiedTerminal(const FText &Draft)
{
    auto Stream = SNew(SVerticalBox);
    Stream->AddSlot().AutoHeight().Padding(
        0, 0, 0, 12)[SNew(STextBlock)
                         .Tag(TEXT("ScutCarrier"))
                         .Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
                         .ColorAndOpacity(EvaluatePhosphorColor(1))
                         .Text(FText::FromString(TEXT("CARRIER: THETA-9 // PROJECT: GLASS HOUSE")))];
    Stream->AddSlot().AutoHeight().Padding(
        0, 0, 0,
        12)[SNew(STextBlock)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                .ColorAndOpacity(EvaluatePhosphorColor(.8f))
                .Text(FText::FromString(TEXT(
                    "MOTHER-7 / OPTICAL MEDIATION TERMINAL                                      F9: DIAGNOSTICS")))];
    Stream->AddSlot().FillHeight(
        1)[SAssignNew(LogScroll, SScrollBox)
               .Tag(TEXT("ScutTerminalStream"))
               .ScrollBarStyle(&SlimScrollStyle)
               .ScrollBarThickness(FVector2D(3, 3))
               .OnUserScrolled_Lambda([this](float Offset)
                                      { bLogAutoScroll = Offset >= LogScroll->GetScrollOffsetOfEnd() - 2; }) +
           SScrollBox::Slot()[SAssignNew(Log, SVerticalBox).Tag(TEXT("ScutInlineStream"))]];
    // Briefing is an inline system block; initialization is still a validated queued command.
    Stream->AddSlot().AutoHeight()[SNew(SButton)
                                       .Tag(TEXT("ScutInitializeExperiment"))
                                       .IsFocusable(false)
                                       .Visibility_Lambda(
                                           [this]()
                                           {
                                               return Sim->ExperimentPhase == EExperimentPhase::Briefing
                                                          ? EVisibility::Visible
                                                          : EVisibility::Collapsed;
                                           })
                                       .Text(FText::FromString(TEXT("[ENTER] INITIALIZE EXPERIMENT")))
                                       .OnClicked_Lambda(
                                           [this]()
                                           {
                                               Sim->QueueConsoleCommand(TEXT("start_experiment"));
                                               return FReply::Handled();
                                           })];
    auto Bar = SNew(SHorizontalBox);
    const TCHAR *Labels[] = {TEXT("[1] FINDINGS"),      TEXT("[2] SUBJECTS"), TEXT("[3] BLINDSPOTS"),
                             TEXT("[4] CROSS-EXAMINE"), TEXT("[5] OVERRIDE"), TEXT("[R] SCRUB 15S")};
    for (int32 I = 0; I < 6; ++I)
    {
        Bar->AddSlot().AutoWidth().Padding(0, 8, 14,
                                           6)[SNew(SButton)
                                                  .IsFocusable(false)
                                                  .Tag(FName(*FString::Printf(TEXT("ScutInquiry%d"), I + 1)))
                                                  .ButtonStyle(FCoreStyle::Get(), "NoBorder")
                                                  .OnClicked_Lambda(
                                                      [this, I]()
                                                      {
                                                          Inquiry(I);
                                                          return FReply::Handled();
                                                      })[SNew(STextBlock)
                                                             .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                                                             .ColorAndOpacity(EvaluatePhosphorColor(1))
                                                             .Text(FText::FromString(Labels[I]))]];
    }
    Stream->AddSlot().AutoHeight()[Bar];
    Stream->AddSlot().AutoHeight()
        [SNew(SHorizontalBox) +
         SHorizontalBox::Slot().AutoWidth().VAlign(
             VAlign_Center)[SNew(STextBlock)
                                .Text(FText::FromString(TEXT("> ")))
                                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
                                .ColorAndOpacity(EvaluatePhosphorColor(1))] +
         SHorizontalBox::Slot().FillWidth(
             1)[SAssignNew(Prompt, SEditableTextBox)
                    .Tag(TEXT("ScutPrompt"))
                    .Text(Draft)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
                    .ForegroundColor(EvaluatePhosphorColor(1))
                    .BackgroundColor(FLinearColor(.003f, .008f, .004f))
                    .ClearKeyboardFocusOnCommit(false)
                    .HintText(FText::FromString(TEXT("show_cam med_bay | show_widget ecg vale | operational command")))
                    .OnKeyCharHandler_Lambda(
                        [this](const FGeometry &, const FCharacterEvent &E)
                        {
                            const bool Suppress = SuppressPromptChar &&
                                                  (E.GetCharacter() == SuppressPromptChar || E.GetCharacter() == '~');
                            SuppressPromptChar = 0;
                            return Suppress ? FReply::Handled() : FReply::Unhandled();
                        })
                    .OnTextCommitted(this, &SDirectOrScutWorkstationWidget::Commit)]];
    return SNew(SBorder)
        .Tag(TEXT("ScutUnifiedTerminal"))
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.003f, .009f, .005f))
        .Padding(FMargin(30, 20))[Stream];
}
void SDirectOrScutWorkstationWidget::RebuildUnifiedStream()
{
    if (bDebugEightCamGrid || !Log.IsValid())
    {
        return;
    }
    const float Offset = LogScroll->GetScrollOffset();
    Log->ClearChildren();
    for (const auto &Block : TerminalStream)
    {
        auto Contents = SNew(SVerticalBox);
        Contents->AddSlot()
            .AutoHeight()[SNew(STextBlock)
                              .AutoWrapText(true)
                              .Font(FCoreStyle::GetDefaultFontStyle(
                                  "Regular", Block->Kind == FTerminalStreamBlock::EKind::Prose ? 15 : 12))
                              .ColorAndOpacity(EvaluatePhosphorColor(.9f))
                              .Text_Lambda(
                                  [this, Block]()
                                  {
                                      if (Block->Kind == FTerminalStreamBlock::EKind::Prose)
                                      {
                                          return FText::FromString(Block->Text.Left(int32(Block->Characters)));
                                      }
                                      if (Block->Kind == FTerminalStreamBlock::EKind::Optic)
                                      {
                                          return FText::FromString(
                                              Block->Text +
                                              (Block->bClosed ? TEXT(" // CLOSED; NO RECORDED FRAME")
                                               : Sim->StationRooms[Block->Subject].Power == EPowerState::Offline
                                                   ? TEXT(" // OFFLINE; NO LIVE CARRIER")
                                                   : TEXT(" // LIVE OPTICAL SOURCE")));
                                      }
                                      return FText::FromString(Block->Text);
                                  })];
        if (Block->Kind == FTerminalStreamBlock::EKind::Optic && !Block->bClosed)
        {
            Contents->AddSlot()
                .AutoHeight()
                .HAlign(HAlign_Left)
                .Padding(0, 8)
                    [SNew(SBox)
                         .WidthOverride_Lambda(
                             [this]()
                             { return FMath::Clamp(float(GetCachedGeometry().GetLocalSize().X) - 80.f, 160.f, 480.f); })
                         .HeightOverride_Lambda(
                             [this]()
                             {
                                 return FMath::Clamp(float(GetCachedGeometry().GetLocalSize().X) - 80.f, 160.f, 480.f) *
                                        .5625f;
                             })
                         .Tag(TEXT("ScutInlineOptic"))
                             [SNew(SOverlay) + SOverlay::Slot()[SAssignNew(Block->Image, SImage).Image(&Block->Brush)] +
                              SOverlay::Slot()[SNew(SDirectOrScutInstrument)
                                                   .Owner(SharedThis(this))
                                                   .Kind(ETerminalInstrument::Reticle)
                                                   .Subject(Block->Subject)
                                                   .Visibility(EVisibility::HitTestInvisible)]]];
        }
        else if (Block->Kind == FTerminalStreamBlock::EKind::Instrument)
        {
            Contents->AddSlot().AutoHeight().HAlign(HAlign_Left)[SNew(SBox).WidthOverride_Lambda(
                [this]()
                {
                    return FMath::Clamp(float(GetCachedGeometry().GetLocalSize().X) - 80.f, 160.f, 720.f);
                })[SNew(SDirectOrScutInstrument)
                       .Owner(SharedThis(this))
                       .Kind(Block->Instrument)
                       .Subject(Block->Subject)
                       .Tag(Block->Instrument == ETerminalInstrument::ECG ? TEXT("ScutInlineECG")
                                                                          : TEXT("ScutInlineSchematic"))]];
        }
        Log->AddSlot().AutoHeight().Padding(0, 4, 0, 8)[Contents];
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
