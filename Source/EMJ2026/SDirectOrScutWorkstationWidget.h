#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
class UDirectOrScutSimSubsystem;
class ACompartmentCameraRig;
class SEditableTextBox;
class SVerticalBox;
class SRetainerWidget;
class SScrollBox;
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
    bool IsPromptFocused() const;
    TSharedRef<SWidget> GetContentRoot() const;
private:
    TWeakObjectPtr<UDirectOrScutSimSubsystem> Sim;
    TWeakObjectPtr<ACompartmentCameraRig> Rig;
    TSharedPtr<SEditableTextBox> Prompt;
    TSharedPtr<SVerticalBox> Root,Log;
    TSharedPtr<SRetainerWidget> Retainer;
    TSharedPtr<SScrollBox> LogScroll;
    TStrongObjectPtr<UMaterialInstanceDynamic> Glass;
    FSlateBrush Brushes[9];
    int32 LastFeed=-1;
    uint64 SeenEvents=MAX_uint64;
    double LastTap=-1;
    bool bSuppressPromptSlash=false;
    void Rebuild();
    TSharedRef<SWidget> Video(int32 Feed);
    void Commit(const FText&,ETextCommit::Type);
};
