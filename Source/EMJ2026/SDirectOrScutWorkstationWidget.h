#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "Curves/CurveLinearColor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
class UDirectOrScutSimSubsystem;
class ACompartmentCameraRig;
class SEditableTextBox;
class SVerticalBox;
class SRetainerWidget;
class SScrollBox;
class SOverlay;
class SBox;
class UMaterialInstanceDynamic;
enum class ETerminalInstrument : uint8
{
    ECG,
    Schematic,
    Reticle
};
struct FTerminalStreamBlock;
class SDirectOrScutWorkstationWidget : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SDirectOrScutWorkstationWidget)
    {
    }
    SLATE_ARGUMENT(UDirectOrScutSimSubsystem *, Sim)
    SLATE_ARGUMENT(ACompartmentCameraRig *, Rig)
    SLATE_END_ARGS()
    void Construct(const FArguments &Args);
    virtual ~SDirectOrScutWorkstationWidget() override;
    TSharedPtr<class IInputProcessor> TacticalInput;
    virtual bool SupportsKeyboardFocus() const override
    {
        return true;
    }
    virtual FReply OnPreviewKeyDown(const FGeometry &, const FKeyEvent &) override;
    virtual FReply OnKeyDown(const FGeometry &, const FKeyEvent &) override;
    virtual FReply OnMouseButtonDown(const FGeometry &, const FPointerEvent &) override;
    virtual void Tick(const FGeometry &, double, float) override;
    bool IsDebugEightCamGrid() const
    {
        return bDebugEightCamGrid;
    }
    void SetDebugEightCamGrid(bool Enabled);
    bool HandlePresentationCommand(const FString &Input);
    bool PresentOptic(int32 Room);
    bool PresentInstrument(ETerminalInstrument Kind, int32 Subject);
    void SetPresentationNoise(float ScrubTracking, float VoiceBleed);
    float GetInlineWipeProgress() const;
    UMaterialInstanceDynamic *GetInlineOpticMaterial() const;
    int32 GetInlineInstrumentCount() const;
    FLinearColor TerminalTint(int32 Room) const;
    int32 PaintTerminalInstrument(FSlateWindowElementList &Elements, int32 Layer, const FGeometry &Geometry,
                                  ETerminalInstrument Kind, int32 Subject) const;
    int32 ActiveFeed = 1;
    int32 SelectedCameraIndex = 0;
    void ExpandSelected();
    bool bGrid = false, bFullscreen = false;
    void SelectFeed(int32 Feed, bool AllowDoubleTap = true);
    void ToggleExpanded();
    void ShowGrid();
    // Legacy callers use these names; bConsoleOpen now tracks terminal input focus, never visibility.
    bool bConsoleOpen = false;
    void SetConsoleOpen(bool Open, bool Full = false);
    bool IsPromptFocused() const;
    TSharedRef<SWidget> GetContentRoot() const;
    FString EvaluationHighlights() const;

    // Presentation-only primitives: no simulation access or mutation.
    void BuildCompartmentOutline(int32 RoomId, const FVector2f &LocalSize, TArray<FVector2f> &OutPoints) const;
    void BuildReticlePoints(const FVector2f &Center, float Radius, int32 SegmentCount, float GapRadians,
                            TArray<TArray<FVector2f>> &OutArcs) const;
    void BuildPulseTrace(const FVector2f &Size, float Phase, float BPM, TArray<FVector2f> &OutPoints) const;
    void DrawTerminalLines(FSlateWindowElementList &Elements, int32 Layer, const FGeometry &Geometry,
                           const TArray<FVector2f> &Points, FLinearColor Color) const;
    void BuildSealIndicator(const FVector2f &Center, float Radius, bool bSealed, TArray<FVector2f> &OutPoints) const;
    void BuildRasterOverlay(const FVector2f &Size, float Progress, TArray<TArray<FVector2f>> &OutLines) const;
    FLinearColor EvaluatePhosphorColor(float Intensity, float Amber = 0, float Hypoxia = 0, float Offline = 0,
                                       float Deception = 0) const;
    void ApplyTerminalMaterialParameters(UMaterialInstanceDynamic &Material, UTexture *OpticRenderTarget,
                                         const FVector2f &PixelSize, float Time, float Wipe, float Deception) const;

  private:
    bool bDebugEightCamGrid = false;
    float ScrubTrackingNoise = 0, VoiceBleedIntensity = 0;
    float PresentationSeconds = 0;
    int32 UnifiedEventCursor = 0;
    TArray<TSharedPtr<FTerminalStreamBlock>> TerminalStream;
    TWeakPtr<FTerminalStreamBlock> ActiveOptic;
    TStrongObjectPtr<UMaterialInstanceDynamic> TerminalSurfaceMaterial;
    TSharedRef<SWidget> BuildUnifiedTerminal(const FText &Draft);
    void RebuildUnifiedStream();
    void AppendTerminalProse(const FString &Text);
    void AppendTerminalBlock(TSharedRef<FTerminalStreamBlock> Block);
    void Inquiry(int32 Index);
    void ResetTerminalPresentation();
    void UpdateTerminalPresentation(float Delta, const FGeometry &Geometry);
    void InitializePhosphorCurve();
    TStrongObjectPtr<UCurveLinearColor> PhosphorCurve;
    TStrongObjectPtr<UMaterialInstanceDynamic> TerminalMaterials[9];
    TStrongObjectPtr<UTexture2D> NeutralOptic;
    mutable TMap<int32, TPair<FVector2f, TArray<FVector2f>>> OutlineCache;
    mutable FVector2f ReticleCenter = FVector2f::ZeroVector;
    mutable float ReticleRadius = -1, ReticleGap = -1;
    mutable int32 ReticleSegments = 0;
    mutable TArray<TArray<FVector2f>> ReticleCache;
    TWeakObjectPtr<UDirectOrScutSimSubsystem> Sim;
    TWeakObjectPtr<ACompartmentCameraRig> Rig;
    TSharedPtr<SEditableTextBox> Prompt;
    TSharedPtr<SVerticalBox> Root, Log, MenuList;
    TSharedPtr<SOverlay> Body;
    TSharedPtr<SBox> CameraWall;
    bool bMenuFocused = false;
    void RebuildCameraWall();
    TSharedPtr<SRetainerWidget> Retainer;
    TSharedPtr<SScrollBox> LogScroll, MenuScroll;
    FScrollBarStyle SlimScrollStyle;
    TArray<TSharedPtr<SWidget>> MenuEntries;
    int32 ActiveCategory = 0, HighlightedChoice = 0;
    bool bLogAutoScroll = true;
    double CopiedUntil = 0;
    uint64 SeenRuntimeRevision = MAX_uint64;
    void SelectCategory(int32 Category, bool FocusMenu = true);
    void ExecuteHighlighted();
    void RefreshLog();
    FReply CopyLog();
    TSharedRef<SWidget> BuildTerminal(const FText &Draft);
    TSharedRef<SWidget> BuildRoomQuickActions();
    void ExecuteRoomQuickAction(FKey Key);
    FSlateBrush Brushes[9];
    TSharedPtr<class SImage> OpticImages[9];
    int32 LastFeed = -1;
    int32 LastFollowedCrew = -1, LastFollowRoom = -1;
    uint64 SeenFollowRevision = MAX_uint64;
    uint64 SeenEvents = MAX_uint64;
    double LastTap = -1;
    TCHAR SuppressPromptChar = 0;
    float MenuRefreshClock = 0;
    uint64 SeenMenuRevision = MAX_uint64;
    TArray<FString> History, Completions, MenuLabels;
    TArray<bool> MenuEnabled;
    int32 HistoryCursor = 0, CompletionCursor = 0;
    FString HistoryDraft, LastCompletion;
    void BrowseHistory(int32 Direction);
    void Autocomplete();
    void RebuildMenu();
    void RefreshMenuChoices();
    void Rebuild();
    TSharedRef<SWidget> BuildEvaluationDossier();
    TSharedRef<SWidget> BuildIncidentReelControls();
    TSharedRef<SWidget> BuildBriefingCard();
    bool bShowingEvaluation = false;
    TSharedRef<SWidget> Video(int32 Feed);
    void Commit(const FText &, ETextCommit::Type);
};
