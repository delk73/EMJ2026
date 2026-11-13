#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
class UDirectOrScutSimSubsystem;
class ACompartmentCameraRig;
class SEditableTextBox;
class SVerticalBox;
class SRetainerWidget;
class SScrollBox;
class SOverlay;
class UMaterialInstanceDynamic;
class SDirectOrScutWorkstationWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDirectOrScutWorkstationWidget) {} SLATE_ARGUMENT(UDirectOrScutSimSubsystem*,Sim) SLATE_ARGUMENT(ACompartmentCameraRig*,Rig) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnPreviewKeyDown(const FGeometry&,const FKeyEvent&) override;
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent&) override;
    virtual void Tick(const FGeometry&,double,float) override;
    int32 ActiveFeed=1;
    bool bGrid=true,bFullscreen=false;
    void SelectFeed(int32 Feed,bool AllowDoubleTap=true);
    void ToggleExpanded();
    void ShowGrid();
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
    TSharedPtr<SRetainerWidget> Retainer;
    TSharedPtr<SScrollBox> LogScroll;
    TStrongObjectPtr<UMaterialInstanceDynamic> Glass;
    FSlateBrush Brushes[9];
    int32 LastFeed=-1;
    uint64 SeenEvents=MAX_uint64;
    double LastTap=-1;
    TCHAR SuppressPromptChar=0;
    float DrawerSlide=0,MenuRefreshClock=0;
    uint64 SeenMenuRevision=MAX_uint64;
    TArray<FString> History,Completions,MenuLabels;
    TArray<bool> MenuEnabled;
    int32 HistoryCursor=0,CompletionCursor=0;
    FString HistoryDraft,LastCompletion;
    void BrowseHistory(int32 Direction);
    void Autocomplete();
    float DrawerHeight() const;
    void RebuildMenu();
    void RefreshMenuChoices();
    void Rebuild();
    TSharedRef<SWidget> Video(int32 Feed);
    void Commit(const FText&,ETextCommit::Type);
};
