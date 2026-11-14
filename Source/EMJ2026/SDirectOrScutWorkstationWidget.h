#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"
class UDirectOrScutSimSubsystem;
class ACompartmentCameraRig;
class SEditableTextBox;
class SVerticalBox;
class SRetainerWidget;
class SScrollBox;
class SOverlay;
class SBox;
class UMaterialInstanceDynamic;
class SDirectOrScutWorkstationWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDirectOrScutWorkstationWidget) {} SLATE_ARGUMENT(UDirectOrScutSimSubsystem*,Sim) SLATE_ARGUMENT(ACompartmentCameraRig*,Rig) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual ~SDirectOrScutWorkstationWidget() override;
    TSharedPtr<class IInputProcessor> TacticalInput;
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnPreviewKeyDown(const FGeometry&,const FKeyEvent&) override;
    virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent&) override;
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent&) override;
    virtual void Tick(const FGeometry&,double,float) override;
    int32 ActiveFeed=1;
    int32 SelectedCameraIndex=0;
    void ExpandSelected();
    bool bGrid=true,bFullscreen=false;
    void SelectFeed(int32 Feed,bool AllowDoubleTap=true);
    void ToggleExpanded();
    void ShowGrid();
    // Legacy callers use these names; bConsoleOpen now tracks terminal input focus, never visibility.
    bool bConsoleOpen=false,bFullConsole=false;
    void SetConsoleOpen(bool Open,bool Full=false);
    bool IsPromptFocused() const;
    TSharedRef<SWidget> GetContentRoot() const;
private:
    TWeakObjectPtr<UDirectOrScutSimSubsystem> Sim;
    TWeakObjectPtr<ACompartmentCameraRig> Rig;
    TSharedPtr<SEditableTextBox> Prompt;
    TSharedPtr<SVerticalBox> Root,Log,MenuList;
    TSharedPtr<SOverlay> Body;
    TSharedPtr<SBox> CameraWall;
    bool bMenuFocused=false;
    void RebuildCameraWall();
    TSharedPtr<SRetainerWidget> Retainer;
    TSharedPtr<SScrollBox> LogScroll,MenuScroll;
    FScrollBarStyle SlimScrollStyle;
    TArray<TSharedPtr<SWidget>> MenuEntries;
    int32 ActiveCategory=0,HighlightedChoice=0;
    bool bLogAutoScroll=true;
    double CopiedUntil=0;
    uint64 SeenRuntimeRevision=MAX_uint64;
    void SelectCategory(int32 Category,bool FocusMenu=true);
    void ExecuteHighlighted();
    void RefreshLog();
    FReply CopyLog();
    TSharedRef<SWidget> BuildTerminal(const FText& Draft);
    FSlateBrush Brushes[9];
    int32 LastFeed=-1;
    int32 LastFollowedCrew=-1, LastFollowRoom=-1;
    uint64 SeenFollowRevision=MAX_uint64;
    uint64 SeenEvents=MAX_uint64;
    double LastTap=-1;
    TCHAR SuppressPromptChar=0;
    float MenuRefreshClock=0;
    uint64 SeenMenuRevision=MAX_uint64;
    TArray<FString> History,Completions,MenuLabels;
    TArray<bool> MenuEnabled;
    int32 HistoryCursor=0,CompletionCursor=0;
    FString HistoryDraft,LastCompletion;
    void BrowseHistory(int32 Direction);
    void Autocomplete();
    void RebuildMenu();
    void RefreshMenuChoices();
    void Rebuild();
    TSharedRef<SWidget> BuildEvaluationDossier();
    TSharedRef<SWidget> BuildBriefingCard();
    bool bShowingEvaluation=false;
    TSharedRef<SWidget> Video(int32 Feed);
    void Commit(const FText&,ETextCommit::Type);
};
