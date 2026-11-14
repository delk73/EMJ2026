#pragma once
#include "Widgets/Layout/SBorder.h"
class SDirectOrScutWorkstationWidget;
class SScutCameraTile final : public SBorder
{
  public:
    SLATE_BEGIN_ARGS(SScutCameraTile)
    {
    }
    SLATE_ARGUMENT(TWeakPtr<SDirectOrScutWorkstationWidget>, Owner)
    SLATE_ARGUMENT(int32, Feed)
    SLATE_DEFAULT_SLOT(FArguments, Content) SLATE_END_ARGS()
    void Construct(const FArguments &Args);
    FReply OnMouseButtonDown(const FGeometry &Geometry, const FPointerEvent &Event) override;
    FReply OnMouseButtonDoubleClick(const FGeometry &Geometry, const FPointerEvent &Event) override;

  private:
    TWeakPtr<SDirectOrScutWorkstationWidget> Owner;
    int32 Feed = 1;
};
